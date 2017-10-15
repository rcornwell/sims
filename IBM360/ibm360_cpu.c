/* ibm360_cpu.c: ibm 360 cpu simulator.

   Copyright (c) 2016, Richard Cornwell

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "ibm360_defs.h"                            /* simulator defns */


#define FEAT_PROT    (1 << (UNIT_V_UF + 8))      /* Storage protection feature */
#define FEAT_DEC     (1 << (UNIT_V_UF + 9))      /* Decimal instruction set */
#define FEAT_FLOAT   (1 << (UNIT_V_UF + 10))     /* Floating point instruction set */
#define FEAT_UNIV    (3 << (UNIT_V_UF + 9))      /* All instructions */
#define FEAT_STOR    (1 << (UNIT_V_UF + 11))     /* No alignment restrictions */
#define FEAT_TIMER   (1 << (UNIT_V_UF + 12))     /* Interval timer */
#define EXT_IRQ      (1 << (UNIT_V_UF_31))       /* External interrupt */

#define UNIT_V_MSIZE (UNIT_V_UF + 0)             /* dummy mask */
#define UNIT_MSIZE   (0xff << UNIT_V_MSIZE)
#define MEMAMOUNT(x) (x << UNIT_V_MSIZE)

#define HIST_MAX     50000
#define HIST_MIN     64
#define HIST_PC      0x1000000
#define HIST_SPW     0x2000000
#define HIST_LPW     0x4000000

uint32       *M = NULL;
uint8        key[MAXMEMSIZE/2048];
uint32       regs[16];             /* CPU Registers */
uint32       PC;                   /* Program counter */
uint32       fpregs[8];            /* Floating point registers */
uint32       cregs[16];            /* Control registers /67 or 370 only */
uint8        sysmsk;               /* Interupt mask */
uint8        st_key;               /* Storage key */
uint8        cc;                   /* CC */
uint8        pmsk;                 /* Program mask */
uint16       irqcode;              /* Interupt code */
uint8        flags;                /* Misc flags */
uint16       irqaddr;              /* Address of IRQ vector */
uint16       loading;              /* Doing IPL */

#define ASCII       0x08           /* ASCII/EBCDIC mode */
#define MCHECK      0x04           /* Machine check flag */
#define WAIT        0x02
#define PROBLEM     0x01           /* Problem state */
#define ILMASK      0xC0           /* Instruction length mask */

#define FIXOVR      0x08           /* Fixed point overflow */
#define DECOVR      0x04           /* Decimal overflow */
#define EXPOVR      0x02           /* Exponent overflow */
#define SIGMSK      0x01           /* Significance  */


/* low addresses */
#define IPSW        0x00           /* IPSW */
#define ICCW1       0x08           /* ICCW1 */
#define ICCW2       0x10           /* ICCW2 */
#define OEPSW       0x18           /* External old PSW */
#define OSPSW       0x20           /* Supervisior call old PSW */
#define OPPSW       0x28           /* Program old PSW */
#define OMPSW       0x30           /* Machine check PSW */
#define OIOPSW      0x38           /* IO old PSW */
#define CSW         0x40           /* CSW */
#define CAW         0x48           /* CAW */
#define TIMER       0x50           /* timer */
#define NEPSW       0x58           /* External new PSW */
#define NSPSW       0x60           /* SVC new PSW */
#define NPPSW       0x68           /* Program new PSW */
#define NMPSW       0x70           /* Machine Check PSW */
#define NIOPSW      0x78           /* IOPSW */
#define DIAGAREA    0x80           /* Diag scan area. */


#define IRC_OPR     0x0001         /* Operations exception */
#define IRC_PRIV    0x0002         /* Privlege violation */
#define IRC_EXEC    0x0003         /* Execution */
#define IRC_PROT    0x0004         /* Protection violation */
#define IRC_ADDR    0x0005         /* Address error */
#define IRC_SPEC    0x0006         /* Specification error */
#define IRC_DATA    0x0007         /* Data exception */
#define IRC_FIXOVR  0x0008         /* Fixed point overflow */
#define IRC_FIXDIV  0x0009         /* Fixed point divide */
#define IRC_DECOVR  0x000a         /* Decimal overflow */
#define IRC_DECDIV  0x000b         /* Decimal divide */
#define IRC_EXPOVR  0x000c         /* Exponent overflow */
#define IRC_EXPUND  0x000d         /* Exponent underflow */
#define IRC_SIGNIF  0x000e         /* Significance error */
#define IRC_FPDIV   0x000f         /* Floating pointer divide */

#define MSIGN       0x80000000     /* Minus sign */
#define MMASK       0x00ffffff     /* Mantissa mask */
#define EMASK       0x7f000000     /* Exponent mask */
#define XMASK       0x0fffffff     /* Working FP mask */
#define CMASK       0xf0000000     /* Carry mask */

int hst_lnt;
int hst_p;
struct InstHistory
{
   uint32        pc;
   uint32        addr1;
   uint32        addr2;
   uint32        src1;
   uint32        src2;
   uint32        dest;
   uint16       inst[3];
   uint8        op;
   uint8        reg;
   uint8        cc;
};

struct InstHistory *hst = NULL;

/* Forward and external declarations */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                     const char *cptr);
const char          *cpu_description (DEVICE *dptr);

t_bool build_dev_tab (void);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = { UDATA (/*&rtc_srv*/NULL, UNIT_BINK, MAXMEMSIZE) };

REG cpu_reg[] = {
    { HRDATA (PC, PC, 24) },
    { HRDATA (R0, regs[00], 32) },                       /* addr in memory */
    { HRDATA (R1, regs[01], 32) },                       /* modified at exit */
    { HRDATA (R2, regs[02], 32) },                       /* to SCP */
    { HRDATA (R3, regs[03], 32) },
    { HRDATA (R4, regs[04], 32) },
    { HRDATA (R5, regs[05], 32) },
    { HRDATA (R6, regs[06], 32) },
    { HRDATA (R7, regs[07], 32) },
    { HRDATA (R8, regs[010], 32) },
    { HRDATA (R9, regs[011], 32) },
    { HRDATA (R10, regs[012], 32) },
    { HRDATA (R11, regs[013], 32) },
    { HRDATA (R12, regs[014], 32) },
    { HRDATA (R13, regs[015], 32) },
    { HRDATA (R14, regs[016], 32) },
    { HRDATA (R15, regs[017], 32) },
    { BRDATA (R, regs, 16, 32, 017) },
    { NULL }
    };

MTAB cpu_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
    { UNIT_MSIZE, MEMAMOUNT(1), "16K", "16K", &cpu_set_size },
    { UNIT_MSIZE, MEMAMOUNT(2), "32K", "32K", &cpu_set_size },
    { UNIT_MSIZE, MEMAMOUNT(4), "64K", "64K", &cpu_set_size },
    { UNIT_MSIZE, MEMAMOUNT(8), "128K", "128K", &cpu_set_size },
    { UNIT_MSIZE, MEMAMOUNT(12), "196K", "196K", &cpu_set_size },
    { UNIT_MSIZE, MEMAMOUNT(16), "256K", "256K", &cpu_set_size },
    { UNIT_MSIZE, MEMAMOUNT(32), "512K", "512K", &cpu_set_size },
    { FEAT_PROT, 0, NULL, "NOPROT", NULL, NULL, NULL, "No Storage protection"},
    { FEAT_PROT, FEAT_PROT, "PROT", "PROT", NULL, NULL, NULL, "Storage protection"},
    { FEAT_DEC, 0, NULL, "NODECIMAL", NULL, NULL, NULL},
    { FEAT_DEC, FEAT_DEC, "DECIMAL", "DECIMAL", NULL, NULL, NULL, "Decimal instruction set"},
    { FEAT_FLOAT, 0, NULL, "NOFLOAT", NULL, NULL, NULL},
    { FEAT_FLOAT, FEAT_FLOAT, "FLOAT", "FLOAT", NULL, NULL, NULL, "Floating point instruction"},
    { FEAT_UNIV, FEAT_UNIV, NULL, "UNIV", NULL, NULL, NULL, "Universal instruction"},
    { FEAT_STOR, 0, NULL, "NOSTORE", NULL, NULL, NULL},
    { FEAT_STOR, FEAT_STOR, "STORE", "DECIMAL", NULL, NULL, NULL, "No storage alignment"},
    { FEAT_TIMER, 0, "NOTIMER",  NULL, NULL, NULL},
    { FEAT_TIMER, FEAT_TIMER, "TIMER", "TIMER", NULL, NULL, NULL, "Interval timer"},
    { EXT_IRQ, 0, "NOEXT",  NULL, NULL, NULL},
    { EXT_IRQ, EXT_IRQ, "EXT", "EXT", NULL, NULL, NULL, "External Irq"},
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 16, 24, 1, 16, 8,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL, NULL, 0, 0, NULL,
    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description
    };

#if 0       /* 370 Operators */
MVCL 0e
CLCL 0f
STNSM ac                SI
STOSM ad                SI
SIGP  ae                RS
MC    af                SI
LRA   B1                RX
spec  B2                S
STCTL B6                RS
LCTL  B7                RS
CS    BA                RS
CDS   bb                RS
CLM   bd                RS
STCM  BE                RS
ICM   BF                RS

#endif

void post_extirq() {
     cpu_unit.flags |= EXT_IRQ;
}


void storepsw(uint32 addr, uint16 ircode) {
     uint32 word;
     irqaddr = addr + 0x40;
     word = ((uint32)sysmsk) << 24 |
            ((uint32)st_key) << 16 |
            ((uint32)flags) << 16 |
            ((uint32)ircode);
     M[addr >> 2] = word;
        if (hst_lnt) {
             hst_p = hst_p + 1;
             if (hst_p >= hst_lnt)
                hst_p = 0;
             hst[hst_p].pc = addr | HIST_SPW;
             hst[hst_p].src1 = word;
        }
     addr += 4;
     word = ((uint32)pmsk) << 24 |
            ((uint32)cc) << 28 |
            PC;
     M[addr >> 2] = word;
        if (hst_lnt) {
             hst[hst_p].src2 = word;
        }
     irqcode = ircode;
}

/*
 * Read a full word from memory, checking protection
 * and alignment restrictions. Return 1 if failure, 0 if
 * success.
 */
int  ReadFull(uint32 addr, uint32 *data) {
     uint32  temp;
     int     offset;
     uint8   k;

     /* Ignore high order bits */
     addr &= 0xffffff;
     if (addr >= MEMSIZE) {
        storepsw(OPPSW, IRC_ADDR);
        return 1;
     }

     offset = addr & 0x3;
     addr >>= 2;

     /* Check storage key */
     if (st_key != 0) {
        if ((cpu_unit.flags & FEAT_PROT) == 0) {
            storepsw(OPPSW, IRC_PROT);
            return 1;
        }
         k = key[addr >> 10];
        if ((k & 0x8) != 0 && (k & 0xf0) != st_key) {
            storepsw(OPPSW, IRC_PROT);
            return 1;
        }
     }

     if (offset != 0) {
        if ((cpu_unit.flags & FEAT_STOR) == 0) {
             storepsw(OPPSW, IRC_SPEC);
             return 1;
        }
        temp = M[addr];
     }
     *data = M[addr];
     return 0;
}

int ReadByte(uint32 addr, uint32 *data) {

     if (ReadFull(addr & (~0x3), data))
        return 1;
     *data >>= 8 * (3 - (addr & 0x3));
     *data &= 0xff;
     return 0;
}

int ReadHalf(uint32 addr, uint32 *data) {
     if (addr & 0x1) {
        storepsw(OPPSW, IRC_SPEC);
         return 1;
     }
     if (ReadFull(addr & (~0x3), data))
        return 1;
     *data >>= (addr & 2) ? 0 : 16;
     *data &= 0xffff;
     if (*data & 0x8000)
        *data |= 0xffff0000;
     return 0;
}

int WriteFull(uint32 addr, uint32 data) {
     int     offset;
     uint8   k;
     /* Ignore high order bits */
     addr &= 0xffffff;
     if (addr >= MEMSIZE) {
        storepsw(OPPSW, IRC_ADDR);
        return 1;
     }
//     if ((addr& 0xffff00) == 0x1800) {
//           fprintf(stderr, "Write %x %x\n\r", addr, data);
//     }

     offset = addr & 0x3;
     addr >>= 2;

     /* Check storage key */
     if (st_key != 0) {
        if ((cpu_unit.flags & FEAT_PROT) == 0) {
            storepsw(OPPSW, IRC_PROT);
            return 1;
        }
         k = key[addr >> 10];
        if ((k & 0xf0) != st_key) {
            storepsw(OPPSW, IRC_PROT);
            return 1;
        }
     }

     switch (offset) {
     case 0:
         M[addr] = data;
          break;
     case 1:
        if ((cpu_unit.flags & FEAT_STOR) == 0) {
             storepsw(OPPSW, IRC_SPEC);
             return 1;
        }
        M[addr] &= 0xff000000;
        M[addr] |= 0xffffff & (data >> 8);
        M[addr+1] &= 0xffffff;
        M[addr+1] |= 0xff000000 & (data << 24);
        break;
     case 2:
        if ((cpu_unit.flags & FEAT_STOR) == 0) {
             storepsw(OPPSW, IRC_SPEC);
             return 1;
        }
        M[addr] &= 0xffff0000;
        M[addr] |= 0xffff & (data >> 16);
        M[addr+1] &= 0xffff;
        M[addr+1] |= 0xffff0000 & (data << 16);
        break;
     case 3:
        if ((cpu_unit.flags & FEAT_STOR) == 0) {
             storepsw(OPPSW, IRC_SPEC);
             return 1;
        }
        M[addr] &= 0xffffff00;
        M[addr] |= 0xff & (data >> 24);
        M[addr+1] &= 0xff;
        M[addr+1] |= 0xffffff00 & (data << 8);
        break;
     }
     return 0;
}

int WriteByte(uint32 addr, uint32 data) {
     uint32        mask;
     uint8   k;
     int     offset;

     /* Ignore high order bits */
     addr &= 0xffffff;
     if (addr >= MEMSIZE) {
        storepsw(OPPSW, IRC_ADDR);
        return 1;
     }
//     if ((addr& 0xffff00) == 0x1800) {
//           fprintf(stderr, "Write Byte %x %x\n\r", addr, data);
//     }

     offset = 8 * (3 - (addr & 0x3));
     addr >>= 2;

     /* Check storage key */
     if (st_key != 0) {
        if ((cpu_unit.flags & FEAT_PROT) == 0) {
            storepsw(OPPSW, IRC_PROT);
            return 1;
        }
         k = key[addr >> 10];
        if ((k & 0xf0) != st_key) {
            storepsw(OPPSW, IRC_PROT);
            return 1;
        }
     }

     mask = 0xff;
     data &= mask;
     data <<= offset;
     mask <<= offset;
     M[addr] &= ~mask;
     M[addr] |= data;
     return 0;
}

int WriteHalf(uint32 addr, uint32 data) {
     uint32        mask;
     uint8      k;
     int        offset;
     int        o;

     /* Ignore high order bits */
     addr &= 0xffffff;
     if (addr >= MEMSIZE) {
        storepsw(OPPSW, IRC_ADDR);
        return 1;
     }
//     if ((addr& 0xffff00) == 0x1800) {
//           fprintf(stderr, "Write Half %x %x\n\r", addr, data);
//     }

     offset = addr & 0x3;
     addr >>= 2;

     /* Check storage key */
     if (st_key != 0) {
        if ((cpu_unit.flags & FEAT_PROT) == 0) {
            storepsw(OPPSW, IRC_PROT);
            return 1;
        }
         k = key[addr >> 10];
        if ((k & 0xf0) != st_key) {
            storepsw(OPPSW, IRC_PROT);
            return 1;
        }
     }

     mask = 0xffff;
     data &= mask;
     switch (offset) {
     case 0:
         M[addr] &= ~(mask << 16);
         M[addr] |= data << 16;
          break;
     case 1:
         if ((cpu_unit.flags & FEAT_STOR) == 0) {
             storepsw(OPPSW, IRC_SPEC);
             return 1;
         }
         M[addr] &= ~(mask << 8);
         M[addr] |= data << 8;
         break;
     case 2:
         M[addr] &= ~mask;
         M[addr] |= data;
         break;
     case 3:
         if ((cpu_unit.flags & FEAT_STOR) == 0) {
              storepsw(OPPSW, IRC_SPEC);
              return 1;
         }
         M[addr] &= 0xffffff00;
         M[addr] |= 0xff & (data >> 8);
         M[addr+1] &= 0x00ffffff;
         M[addr+1] |= 0xff000000 & (data << 24);
         break;
     }
     return 0;
}



t_stat
sim_instr(void)
{
        t_stat          reason;
        uint32          src1;
        uint32          src1h;
        uint32          src2;
        uint32          src2h;
        uint32          dest;
        uint32          addr1;
        uint32          addr2;
        uint8           op;
        uint8           fill;
        uint8           reg;
        uint8           reg1;
        int             e1, e2;
        int             temp, temp2;
        t_uint64        t64;
        t_uint64        t64a;

        reason = SCPE_OK;
        while (reason == SCPE_OK) {

wait_loop:
        if (sim_interval <= 0) {
             reason = sim_process_event();
             if (reason != SCPE_OK)
                return reason;
        }

        irqcode = scan_chan(sysmsk);
        if (irqcode != 0) {
             if (loading) {
                (void)WriteHalf(0x2, irqcode);
                loading = 0;
                irqaddr = 0;
             } else
                storepsw(OIOPSW, irqcode);
             goto supress;
        }

        if ((cpu_unit.flags & EXT_IRQ) && (sysmsk & 01)) {
             cpu_unit.flags &= ~EXT_IRQ;
             storepsw(OEPSW, 0x40);
             goto supress;
        }

        if (loading || flags & WAIT) {
            /* CPU IDLE */
            sim_interval = -1;
            goto wait_loop;
        }

        if (sim_brk_summ && sim_brk_test(PC, SWMASK('E'))) {
           return STOP_IBKPT;
        }

        if (PC & 1) {
            storepsw(OPPSW, IRC_SPEC);
            goto supress;
        }

        if (hst_lnt) {
             hst_p = hst_p + 1;
             if (hst_p >= hst_lnt)
                hst_p = 0;
             hst[hst_p].pc = PC | HIST_PC;
        }

        if (ReadHalf(PC, &src1))
            goto supress;
        if (hst_lnt)
             hst[hst_p].inst[0] = src1;
        PC += 2;
        pmsk &= ~ILMASK;
        reg = (uint8)(src1 & 0xff);
        reg1 = (reg >> 4) & 0xf;
        op = (uint8)(src1 >> 8);
         pmsk += 0x40;
        /* Check if RX, RR, SI, RS, SS opcode */
        if (op & 0xc0) {
            pmsk += 0x40;
            if (ReadHalf(PC, &addr1))
                goto supress;
                if (hst_lnt)
                     hst[hst_p].inst[1] = addr1;
            PC += 2;
            /* Check if SS */
            if ((op & 0xc0) == 0xc0) {
                pmsk += 0x40;
                if (ReadHalf(PC, &addr2))
                    goto supress;;
                PC += 2;
                if (hst_lnt)
                     hst[hst_p].inst[2] = addr2;
            }
        }

        /* Add in history here */
opr:
        if (op & 0xc0) {
            uint32        temp;

            temp = addr1 & 0xf000;
            addr1 &= 0xfff;
            if (temp)
                addr1 += regs[(temp >> 12) & 0xf];
            /* Handle RX type operands */
            if ((op & 0x80) == 0 && (reg & 0xf) != 0)
                addr1 += regs[reg & 0xf];
            addr1 &= 0xffffff;
            if ((op & 0xc0) == 0xc0) {
                temp = addr2 & 0xf000;
                addr2 &= 0xfff;
                if (temp)
                    addr2 += regs[(temp >> 12) & 0xf];
                addr2 &= 0xffffff;
            }
         }

        /* Check if floating point */
        if ((op & 0xA0) == 0x20) {
            if ((cpu_unit.flags & FEAT_FLOAT) == 0) {
                storepsw(OPPSW, IRC_OPR);
                goto supress;
            }
            if (reg1 & 0x9) {
                reason=1;
                storepsw(OPPSW, IRC_SPEC);
                goto supress;
            }
            src1 = fpregs[reg1];
            src1h = fpregs[reg1|1];
            if (op & 0x40) {
                if ((op & 0x10) != 0 && (addr1 & 0x7) != 0) {
                   storepsw(OPPSW, IRC_SPEC);
                   goto supress;
                }

                if (ReadFull(addr1, &src2))
                   goto supress;
                if (op & 0x10) {
                   if (ReadFull(addr1+ 4, &src2h))
                       goto supress;
                } else
                   src2h = 0;
            } else {
                if (reg & 0x9) {
                   storepsw(OPPSW, IRC_SPEC);
                   goto supress;
                }
                src2 = fpregs[reg];
                src2h = fpregs[reg|1];
            }
                /* All RR opcodes */
        } else if ((op & 0xe0) == 0) {
                src1 = regs[reg1];
                dest = addr1 = src2 = regs[reg & 0xf];
                /* All RX integer ops */
        } else if ((op & 0xe0) == 0x40) {
                dest = src1 = regs[reg1];
                if ((op & 0x1c) == 0x08 || op == OP_MH)  {
                     if (ReadHalf(addr1, &src2))
                        goto supress;
                } else if ((op & 0x10) && (op & 0x0c) != 0) {
                    if (ReadFull(addr1, &src2))
                        goto supress;
                } else
                    src2 = addr1;
        }

        if (hst_lnt) {
             hst[hst_p].op = op;
             hst[hst_p].reg = reg;
             hst[hst_p].addr1 = addr1;
             hst[hst_p].addr2 = addr2;
             hst[hst_p].src1 = src1;
             hst[hst_p].src2 = src2;
             hst[hst_p].dest = dest;
        }

        /* Preform opcode */
        switch (op) {
        case OP_SPM:
                dest = src1;
                pmsk &= ~0xf;
                pmsk |= (src1 >> 24) & 0xf;
                cc = (src1 >> 28) & 0x3;
                break;

        case OP_BALR:
        case OP_BAL:
                dest = ((((cc & 03) << 4) | pmsk) << 24) | PC;
                if (op != OP_BALR || (reg & 0xf))
                    PC = addr1 & 0xffffff;
                regs[reg1] = dest;
                break;

        case OP_BCTR:
        case OP_BCT:
                dest = src1 - 1;
                if (dest != 0 && (op != OP_BCTR || (reg & 0xf) != 0))
                    PC = addr1 & 0xffffff;
                regs[reg1] = dest;
                break;

        case OP_BCR:
        case OP_BC:
                dest = src1;
                if (((0x8 >> cc) & reg1) != 0 && (op != OP_BCR || (reg & 0xf) != 0))
                    PC = addr1 & 0xffffff;
                break;

        case OP_BXH:
                reg &= 0xf;
                dest = regs[reg1] = regs[reg1] + regs[reg];
                if (regs[reg1] > regs[reg|1])
                   PC = addr1 & 0xffffff;
                break;

        case OP_BXLE:
                reg &= 0xf;
                dest = regs[reg1] = regs[reg1] + regs[reg];
                if (regs[reg1] <= regs[reg|1])
                   PC = addr1 & 0xffffff;
                break;

        case OP_SSK:
                dest = src1;
                if ((cpu_unit.flags & FEAT_PROT) == 0) {
                    storepsw(OPPSW, IRC_OPR);
                } else if (flags & PROBLEM) {
                    storepsw(OPPSW, IRC_PRIV);
                } else if ((addr1 & 0xF) != 0) {
                    storepsw(OPPSW, IRC_SPEC);
                } else if (addr1 >= MEMSIZE) {
                    storepsw(OPPSW, IRC_ADDR);
                } else if (cpu_unit.flags & FEAT_PROT) {
                    key[addr1 >> 12] = src1 & 0xf8;
                }
                break;

        case OP_ISK:
                dest = src1;
                if ((cpu_unit.flags & FEAT_PROT) == 0) {
                    storepsw(OPPSW, IRC_PROT);
                } if (flags & PROBLEM) {
                    storepsw(OPPSW, IRC_PRIV);
                } else if ((addr1 & 0xF) != 0) {
                    storepsw(OPPSW, IRC_SPEC);
                } else if (addr1 >= MEMSIZE) {
                    storepsw(OPPSW, IRC_ADDR);
                } else {
                    dest &= 0xffffff00;
                    dest |= key[addr1 >> 12];
                    regs[reg1] = dest;
                }
                break;

        case OP_SVC:
                storepsw(OSPSW, reg);
                break;

        case OP_SSM:
                if (flags & PROBLEM) {
                    storepsw(OPPSW, IRC_PRIV);
                } else {
                    ReadByte(addr1, &src1);
                    sysmsk = src1 & 0xff;
                    irq_pend = 1;
                }
                break;

        case OP_LPSW:
                if (flags & PROBLEM) {
                    storepsw(OPPSW, IRC_PRIV);
                } else if ((addr1 & 0x7) != 0) {
                    storepsw(OPPSW, IRC_SPEC);
                } else {
                    irqaddr = addr1;
                    irqcode = 0;
                    goto supress;
                }
                break;

        case OP_SIO:
                if (flags & PROBLEM)
                    storepsw(OPPSW, IRC_PRIV);
                else
                    cc = startio(addr1);
                break;

        case OP_TIO:
                if (flags & PROBLEM)
                    storepsw(OPPSW, IRC_PRIV);
                else
                    cc = testio(addr1);
                break;

        case OP_HIO:
                if (flags & PROBLEM)
                    storepsw(OPPSW, IRC_PRIV);
                else
                    cc = haltio(addr1);
                break;

        case OP_TCH:
                if (flags & PROBLEM)
                    storepsw(OPPSW, IRC_PRIV);
                else
                    cc = testchan(addr1);
                break;

        case OP_DIAG:
                if (flags & PROBLEM)
                    storepsw(OPPSW, IRC_PRIV);
                else
                    storepsw(OMPSW, reg);
                break;

        case OP_LPR:
                if ((dest & MSIGN) == 0)
                   goto set_cc;
                /* Fall through */

        case OP_LCR:
                if (dest == MSIGN)
                   goto set_cc3;
                dest = -dest;
                /* Fall through */

        case OP_LTR:
set_cc:
                regs[reg1] = dest;
                cc = (dest & MSIGN) ? 1 : (dest == 0) ? 0 : 2;
                break;

        case OP_LNR:
                if ((dest & MSIGN) == 0)
                   dest = -dest;
                goto set_cc;

        case OP_LA:
        case OP_L:
        case OP_LH:
        case OP_LR:
                dest = src2;
                regs[reg1] = dest;
                break;

        case OP_C:
        case OP_CR:
        case OP_CH:
                dest = src1;
                cc = 0;
                if ((int32)(src1) > (int32)(src2))
                    cc = 2;
                else if (src1 != src2)
                    cc = 1;
                break;

        case OP_S:
        case OP_SR:
        case OP_SH:
                src2 = -src2;
                /* Fall through */

        case OP_A:
        case OP_AR:
        case OP_AH:
                dest = src1 + src2;
                src1 = (src1 & MSIGN) != 0;
                src2 = (src2 & MSIGN) != 0;
                if ((src1 && src2 && (dest & MSIGN) == 0) ||
                    (!src1 && !src2 && (dest & MSIGN) != 0)) {
set_cc3:
                    regs[reg1] = dest;
                    cc = 3;
                    if (pmsk & FIXOVR) {
                       storepsw(OPPSW, IRC_FIXOVR);
                reason =1;
          }
                }
                goto set_cc;

        case OP_SL:
        case OP_SLR:
                src2 = -src2;
                /* Fall through */

        case OP_AL:
        case OP_ALR:
                dest = src1 + src2;
                cc = 0;
                if ((uint32)src1 > (uint32)src2)
                   cc |= 2;
                if (dest == 0)
                   cc |= 1;
                regs[reg1] = dest;
                break;

        case OP_CL:
        case OP_CLR:
                dest = src1;
                cc = 0;
                if ((uint32)(src1) > (uint32)(src2))
                    cc = 2;
                else if (src1 != src2)
                    cc = 1;
                break;

        case OP_M:
        case OP_MR:
                if (reg1 & 1) {
                   storepsw(OPPSW, IRC_SPEC);
                   break;
                }
        case OP_MH:
                fill = 0;
                fprintf(stderr, "Mul %d x %d = ", src1, src2);

                if (src1 & MSIGN) {
                    fill = 1;
                    src1 = -src1;
                }
                if (src2 & MSIGN) {
                    fill ^= 1;
                    src2 = -src2;
                }
                dest = 0;
                if (src1 != 0 || src2 != 0) {
                    for (reg = 32; reg > 0; reg--) {
                         if (src1 & 1)
                            dest += src2;
                         src1 >>= 1;
                         if (dest & 1)
                             src1 |= MSIGN;
                         dest >>= 1;
                    }
                }
                if (fill) {
                    dest ^= 0xffffffff;
                    src1 ^= 0xffffffff;
                    if (src1 == 0xfffffff)
                        dest ++;
                    src1++;
                }
                if (op != OP_MH) {
                    regs[reg1|1] = src1;
                    regs[reg1] = dest;
                } else {
                    regs[reg1] = src1;
                }
fprintf(stderr, " %d  %08x %08x\n\r", src1, dest, src1);
//                reason =1;
                break;

//                dest = (uint32)((int32)src1 * (int32)src2);
 //               regs[reg1] = dest;
  //              reason =1;
   //             break;

        case OP_D:
        case OP_DR:
                if (reg1 & 1) {
                   storepsw(OPPSW, IRC_SPEC);
                   break;
                }
                fill = 0;
                dest = src2;
                src2 = regs[reg1|1];
                src1 = regs[reg1];
                fprintf(stderr, "Div %08x %08x / %08x  = ", src1, src2, dest);

                if (dest == 0) {
                    storepsw(OPPSW, IRC_FIXDIV);
                    fprintf(stderr, "zero\n\r");
                    break;
                }
                t64 = (((t_uint64)src1) << 32) | ((t_uint64)src2);
                if (src1 & MSIGN) {
                    fill = 3;
                    t64 = -t64;
                }
                if (dest & MSIGN) {
                    fill ^= 1;
                    dest = -dest;
                }
                fprintf(stderr, "%lld / %d   ", t64, dest);
                t64a = t64 % (t_uint64)dest;
                t64 = t64 / (t_uint64)dest;
                fprintf(stderr, "%lld , %lld   ", t64, t64a);
                if ((t64 & 0xffffffff80000000) != 0) {
                    storepsw(OPPSW, IRC_FIXDIV);
                    fprintf(stderr, "zero\n\r");
                    break;
                }

                src1 = (uint32)t64;
                src2 = (uint32)t64a;
#if 0
                if (src2 & MSIGN) {
                    fill = 3;
                    src2 ^= 0xffffffff;
                    src1 ^= 0xffffffff;
                    if (src1 == 0xfffffff)
                       src2 ++;
                    src1++;
                }
                if (dest & MSIGN) {
                    fill ^= 1;
                    dest = -dest;
                }
                src2 <<= 1;
                if (src1 & MSIGN)
                   src2 |= 1;
                src1 <<= 1;
                for (reg = 0; reg < 32; reg++) {
                    int f = 0;
                    if (src1 < dest) {
                       src1 = src1 - dest;
                       f = 1;
                    }
                    src2 <<= 1;
                    src2 |= f;
                    if (src1 & MSIGN)
                       src2 |= 1;
                    src1 <<= 1;
                }
#endif
                if (fill & 1)
                    src1 = -src1;
                if (fill & 2)
                    src2 = -src2;
                regs[reg1|1] = src2;
                regs[reg1] = src1;
fprintf(stderr, " %08x %08x\n\r", src1, src2);
//                reason =1;
                break;

        case OP_NR:
        case OP_N:
                dest = src1 & src2;
                cc = (dest == 0) ? 0 : 1;
                regs[reg1] = dest;
                break;

        case OP_OR:
        case OP_O:
                dest = src1 | src2;
                cc = (dest == 0) ? 0 : 1;
                regs[reg1] = dest;
                break;

        case OP_XR:
        case OP_X:
                dest = src1 ^ src2;
                cc = (dest == 0) ? 0 : 1;
                regs[reg1] = dest;
                break;

        case OP_MVI:
                src1 = reg;
                /* Fall through */

        case OP_STC:
                WriteByte(addr1, src1);
                break;

        case OP_NI:
                if (ReadByte(addr1, &dest))
                    break;
                if (hst_lnt)
                    hst[hst_p].src1 = dest;
                dest &= reg;
char_save:
                cc = (dest == 0) ? 0 : 1;
                WriteByte(addr1, dest);
                break;

        case OP_OI:
                if (ReadByte(addr1, &dest))
                    break;
                if (hst_lnt)
                    hst[hst_p].src1 = dest;
                dest |= reg;
                goto char_save;

        case OP_XI:
                if (ReadByte(addr1, &dest))
                    break;
                if (hst_lnt)
                    hst[hst_p].src1 = dest;
                dest ^= reg;
                goto char_save;

        case OP_CLI:
                if (ReadByte(addr1, &dest))
                    break;
                if (hst_lnt)
                    hst[hst_p].src1 = dest;
                dest &= 0xff;
                cc = (dest == reg) ? 0 : (dest < reg) ? 1 : 2;
                break;

        case OP_IC:
                if (ReadByte(addr1, &dest))
                    break;
                dest = (src1 & 0xffffff00) | (dest & 0xff);
                regs[reg1] = dest;
                break;

        case OP_ST:
                dest = src1;
                WriteFull(addr1, dest);
                break;

        case OP_STH:
                dest = src1;
                WriteHalf(addr1, dest);
                break;

        case OP_TS:
                dest = 0xff;
                if (ReadByte(addr1, &src2))
                  break;
                cc = (src2 & 0x80) ? 1 : 0;
                WriteByte(addr1, src1);
                break;

        case OP_TM:
                if (ReadByte(addr1, &dest))
                    break;
                if (hst_lnt)
                    hst[hst_p].src1 = dest;
                dest &= reg;
                if (dest != 0) {
                    cc = 1;
                    if (reg == dest)
                        cc = 3;
                 } else
                    cc = 0;
                 break;

        case OP_SRL:
                dest = regs[reg1];
                dest = ((uint32)dest) >> (addr1 & 0x3f);
                regs[reg1] = dest;
                break;

        case OP_SLL:
                dest = regs[reg1];
                dest = ((uint32)dest) << (addr1 & 0x3f);
                regs[reg1] = dest;
                break;

        case OP_SRA:
                dest = regs[reg1];
                dest = (int32)dest >> (addr1 & 0x3f);
                goto set_cc;

        case OP_SLA:
                dest = regs[reg1];
                src2 = dest & MSIGN;
                addr1 &= 0x3f;
                cc = 0;
                while (addr1 > 0) {
                    dest <<= 1;
                    if ((dest & MSIGN) != src2)
                        cc = 3;
                    addr1--;
                }
                if (cc == 3)
                    goto set_cc3;
                else
                    goto set_cc;
                break;

        case OP_SRDL:
                if (reg & 1)
                   storepsw(OPPSW, IRC_SPEC);
                else {
                   src1 = regs[reg1];
                   src1h = regs[reg1|1];
                   if (hst_lnt) {
                        hst[hst_p].src1 = src1;
                        hst[hst_p].src2 = src1h;
                   }
                   addr1 &= 0x3f;
                   while(addr1 > 0) {
                       src1h >>= 1;
                       if (src1 & 1)
                           src1h |= MSIGN;
                       src1 >>= 1;
                       addr1--;
                   }
                   regs[reg1|1] = src1h;
                   regs[reg1] = src1;
                   dest = src1;
                }
                break;

        case OP_SLDL:
                if (reg & 1)
                   storepsw(OPPSW, IRC_SPEC);
                else {
                   src1 = regs[reg1];
                   src1h = regs[reg1|1];
                   if (hst_lnt) {
                        hst[hst_p].src1 = src1;
                        hst[hst_p].src2 = src1h;
                   }
                   addr1 &= 0x3f;
                   while(addr1 > 0) {
                       src1 <<= 1;
                       if (src1h & MSIGN)
                           src1 |= 1;
                       src1h <<= 1;
                       addr1--;
                   }
                   regs[reg1|1] = src1h;
                   regs[reg1] = src1;
                   dest = src1;
                }
                break;

        case OP_SLDA:
                if (reg & 1)
                   storepsw(OPPSW, IRC_SPEC);
                else {
                   src1 = regs[reg1];
                   src1h = regs[reg1|1];
                   if (hst_lnt) {
                        hst[hst_p].src1 = src1;
                        hst[hst_p].src2 = src1h;
                   }
                   dest = src1 & MSIGN;
                   addr1 &= 0x3f;
                   cc = 0;
                   while(addr1 > 0) {
                       src1 <<= 1;
                       if ((src1 & MSIGN) != dest)
                           cc = 3;
                       if (src1h & MSIGN)
                           src1 |= 1;
                       src1h <<= 1;
                       addr1--;
                   }
save_dbl:
                   regs[reg1|1] = src1h;
                   regs[reg1] = src1;
                   dest = src1;
                   if (cc != 3 && (src1 | src1h) != 0)
                      cc = (src1 & MSIGN) ? 1 : 2;
                   if (cc == 3 && (pmsk & FIXOVR))
                      storepsw(OPPSW, IRC_FIXOVR);
                }
                break;

        case OP_SRDA:
                if (reg & 1)
                   storepsw(OPPSW, IRC_SPEC);
                else {
                   src1 = regs[reg1];
                   src1h = regs[reg1|1];
                   dest = src1 & MSIGN;
                   addr1 &= 0x3f;
                   cc = 0;
                   while(addr1 > 0) {
                       src1h >>= 1;
                       if (src1 & 1)
                           src1h |= MSIGN;
                       src1 >>= 1;
                       src1 |= dest;
                       addr1--;
                   }
                   goto save_dbl;
                }
                break;

        case OP_STM:
                reg &= 0xf;
                for (;;) {
                    if (WriteFull(addr1, regs[reg1]))
                        goto supress;
                    if (reg1 == reg)
                        break;
                    reg1++;
                    reg1 &= 0xf;
                    addr1 += 4;
                } ;
                break;

        case OP_LM:
                reg &= 0xf;
                for (;;) {
                    if (ReadFull(addr1, &regs[reg1]))
                        goto supress;
                    if (reg1 == reg)
                        break;
                    reg1++;
                    reg1 &= 0xf;
                    addr1 += 4;
                };
                break;

        case OP_NC:
        case OP_OC:
        case OP_XC:
                cc = 0;
                /* Fall through */

        case OP_MVN:
        case OP_MVZ:
        case OP_MVC:
//fprintf(stderr, "Mov %x %x %x, ", op, addr1, addr2);
                do {
                   if (ReadByte(addr2, &src1))
                       break;
                   if (op != OP_MVC) {
                       if (ReadByte(addr1, &dest))
                           break;
                       switch(op) {
                       case OP_MVZ: dest = (dest & 0x0f) | (src1 & 0xf0); break;
                       case OP_MVN: dest = (dest & 0xf0) | (src1 & 0x0f); break;
                       case OP_NC:  dest &= src1; if (dest != 0) cc = 1;  break;
                       case OP_OC:  dest |= src1; if (dest != 0) cc = 1;  break;
                       case OP_XC:  dest ^= src1; if (dest != 0) cc = 1;  break;
                       }
                   } else {
                       dest = src1;
                   }
//fprintf(stderr, " %x -> %x", src1, dest);
                   if (WriteByte(addr1, dest))
                       break;
                   addr1++;
                   addr2++;
                   reg--;
                } while (reg != 0xff);
//fprintf(stderr, "\n\r");
                break;

        case OP_CLC:
                cc = 0;
//fprintf(stderr, "CLC %d %06x %06x - ", reg+1, addr1, addr2);
                do {
                    if (ReadByte(addr1, &src1))
                       break;
                    if (ReadByte(addr2, &src2))
                       break;
//fprintf(stderr, "%02x %02x, ", src1 &0xff, src2 &0xff);
                    if (src1 != src2) {
                       dest = src1 - src2;
                       cc = (dest & MSIGN) ? 1 : (dest == 0) ? 0 : 2;
                       break;
                    }
                    addr1++;
                    addr2++;
                    reg--;
                } while(reg != 0xff);
//fprintf(stderr, "cc=%d\n\r",cc);
                break;

        case OP_TR:
                do {
                   if (ReadByte(addr1, &src1))
                       break;
                   if (ReadByte(addr2 + (src1 & 0xff), &dest))
                       break;
                   if (WriteByte(addr1, dest))
                       break;
                   addr1++;
                   reg--;
                } while (reg != 0xff);
                break;

        case OP_TRT:
                cc = 0;
                do {
                   if (ReadByte(addr1, &src1))
                       break;
                   if (ReadByte(addr2 + (src1 & 0xff), &dest))
                       break;
                   if (dest != 0) {
                       regs[1] &= 0xff000000;
                       regs[1] |= addr1 & 0xffffff;
                       regs[2] &= 0xffffff00;
                       regs[2] |= dest & 0xff;
                       cc = (reg == 0) ? 2 : 1;
                       break;
                   }
                   addr1++;
                   reg--;
                } while(reg != 0xff);
                break;

        case OP_PACK:
                reg &= 0xf;
//fprintf(stderr, "PACK %d, %d %06x %06x - ", reg, reg1, addr1, addr2);
                addr2 += reg;
                addr1 += reg1;
//fprintf(stderr, " %06x %06x - ", addr1, addr2);
                /* Flip first location */
                if (ReadByte(addr2, &dest))
                    break;
//fprintf(stderr, "%02x ", dest &0xff);
                dest = ((dest >> 4) & 0xf) | ((dest << 4) & 0xf0);
                if (WriteByte(addr1, dest))
                    break;
//fprintf(stderr, "%02x, ", dest &0xff);
                addr1--;
                addr2--;
                dest = 0;
                while(reg != 0 && reg1 != 0) {
//fprintf(stderr, "(%06x %06x) - ", addr1, addr2);
                     if (ReadByte(addr2, &dest))
                         goto supress;
//fprintf(stderr, "%02x ", dest &0xff);
                     dest &= 0xf;
                     addr2--;
                     reg--;
                     if (reg != 0) {
                         if (ReadByte(addr2, &src1))
                             goto supress;
//fprintf(stderr, "%02x ", src1 &0xff);
                         dest |= (src1 << 4) & 0xf0;
                         addr2--;
                         reg--;
                     }
                     if (WriteByte(addr1, dest))
                         goto supress;
//fprintf(stderr, "->%02x, ", dest &0xff);
                     addr1--;
                     reg1--;
                };
                dest = 0;
                while(reg1 != 0) {
                     if (WriteByte(addr1, dest))
                         break;
//fprintf(stderr, "%02x, ", dest &0xff);
                     addr1--;
                     reg1--;
                };
//fprintf(stderr, "\n\r");
                break;

        case OP_UNPK:
                reg &= 0xf;
//fprintf(stderr, "UNPK %d, %d %06x %06x - ", reg, reg1, addr1, addr2);
                addr2 += reg;
                addr1 += reg1;
                if (ReadByte(addr2, &dest))
                    break;
                dest = ((dest >> 4) & 0xf) | ((dest << 4) & 0xf0);
//fprintf(stderr, "(%06x %06x) - %02x ", addr1, addr2, dest &0xff);
                if (WriteByte(addr1, dest))
                    break;
                addr1--;
                addr2--;
                src2 = (flags & ASCII)? 0x50 : 0xf0;
                while(reg != 0 && reg1 != 0) {
                    if (ReadByte(addr2, &dest))
                        goto supress;
                    addr2--;
                    reg--;
                    src1 = (dest & 0xf) | src2;
//fprintf(stderr, "(%06x %06x) - %02x ", addr1, addr2, src1);
                    if (WriteByte(addr1, src1))
                        goto supress;
                    addr1--;
                    reg1--;
                    if (reg1 != 0) {
                        src1 = ((dest >> 4) & 0xf) | src2;
//fprintf(stderr, "(%06x %06x) - %02x ", addr1, addr2, src1);
                        if (WriteByte(addr1, src1))
                            goto supress;
                        addr1--;
                        reg1--;
                    }
                };
                while(reg1 != 0) {
                    if (WriteByte(addr1, src2))
                        break;
                    addr1--;
                    reg1--;
                };
//fprintf(stderr, "\n\r");
                break;

        case OP_MVO:
                reg &= 0xf;
                addr2 += reg;
                addr1 += reg1;
                if (ReadByte(addr1, &dest))
                    break;
                if (ReadByte(addr2, &src1))
                    break;
                addr2--;
                dest = (dest & 0xf) | ((src1 << 4) & 0xf0);
                WriteByte(addr1, dest);
                addr1--;
                while(reg1 != 0) {
                    dest = (src1 >> 4) & 0xf;
                    if (reg != 0) {
                        if (ReadByte(addr2, &src1))
                            break;
                        addr2--;
                        reg--;
                    } else
                        src1 = 0;
                    dest |= (src1 << 4) & 0xf0;
                    if (WriteByte(addr1, dest))
                        break;
                    reg1--;
                    addr1--;
                };
                break;

        case OP_CVB:
                if (ReadFull(addr1, &src1))
                    break;
                if (ReadFull(addr1+4, &src1h))
                    break;
//fprintf(stderr, "CVB: %08x %08x ", src1, src2);
                fill = src1h & 0xf; /* Save away sign */
                if (fill < 0xA) {
                    storepsw(OPPSW, IRC_DATA);
//fprintf(stderr, "\n\r");
                    break;
                }
                if (fill == 0xB || fill == 0xD)
                    fill = 1;
                else
                    fill = 0;
//fprintf(stderr, "%d ", fill);
                dest = 0;
                /* Convert upper first */
                for(temp = 28; temp >= 0; temp-=4) {
                    int d = (src1 >> temp) & 0xf;
//fprintf(stderr, "%d %d ", temp, d);
                    if (d > 0xA) {
                        storepsw(OPPSW, IRC_DATA);
//fprintf(stderr, "\n\r");
                        break;
                    }
                    dest = (dest * 10) + d;
                }
//fprintf(stderr, "- ");
                /* Convert lower */
                for(temp = 28; temp > 0; temp-=4) {
                    int d = (src1h >> temp) & 0xf;
//fprintf(stderr, "%d %d ", temp, d);
                    if (d > 0xA) {
                        storepsw(OPPSW, IRC_DATA);
//fprintf(stderr, "\n\r");
                        break;
                    }
                    dest = (dest * 10) + d;
                }
                if (dest & MSIGN) {
                    storepsw(OPPSW, IRC_FIXDIV);
                reason =1;
//fprintf(stderr, "\n\r");
                    break;
                }
                if (fill)
                    dest = -dest;
                regs[reg1] = dest;
//fprintf(stderr, "%d \n\r", dest);
                break;

        case OP_CVD:
                dest = regs[reg1];
                src1 = 0;
                src1h = 0;
//fprintf(stderr, "CVD: %08x ", dest);
                if (dest & MSIGN) {
                    dest = -dest;
                    fill = 1;
                } else
                    fill = 0;
//fprintf(stderr, "%d ", fill);
                temp = 4;
                while (dest != 0) {
                    int d = dest % 10;
                    dest /= 10;
                    if (temp > 32)
                        src1 |= (d << (temp - 32));
                    else
                        src1h |= (d << temp);
                    temp += 4;
//fprintf(stderr, "%d %d ", temp, d);
                }
                if (fill) {
                    src1h |= ((flags & ASCII)? 0xb : 0xd);
                } else {
                    src1h |= ((flags & ASCII)? 0xa : 0xc);
                }
//fprintf(stderr, "-> %08x %08x\n\r", src1, src2);

                if (WriteFull(addr1, src1))
                    break;
                WriteFull(addr1+4, src1h);
                break;

        case OP_ED:
        case OP_EDMK:
                ReadByte(addr1, &src1);
                fill = src1;
                addr1++;
                src2 = 0;
                src1 = 1;
                cc = 0;
                while(reg != 0) {
                    uint8        t;
                    uint32  temp;
                    ReadByte(addr1, &temp);
                    t = temp;
                    if (src1) {
                        ReadByte(addr2, &dest);
                        addr2--;
                        reg --;
                    }
                    if (t == 0x21) {
                        src2 = 1;
                        t = 0x20;
                        if (op == OP_EDMK)
                             regs[1] = addr1;
                    }
                    if (t == 0x20) {
                        if (src2 || (dest >> (4*src1)) & 0xf) {
                             t = (dest >> (4*src1)) & 0xf;
                             t |= (flags & ASCII)?0x5:0xf;
                             if (!src2) {
                                 src2 = 1;
                                 if (op == OP_EDMK)
                                     regs[1] = addr1;
                             }
                             if (t & 0xf)
                                 cc = 2;
                        } else
                             t = fill;
                        WriteByte(addr1, t);
                    } else if (t == 0x22) {
                        src2 = 0;
                        t = fill;
                        WriteByte(addr1, t);
                    }
                    addr1--;
                    reg --;
                    src1 = !src1;
                };
                dest &= 0xf;
                if (cc != 0 && (dest == 0xd || dest == 0xb))
                    cc = 1;
                break;

        case OP_EX:
                if (addr1 & 1) {
                   storepsw(OPPSW, IRC_SPEC);
                   break;
                }
                if (ReadHalf(addr1, &dest))
                   break;
                if (reg1) {
                   dest |= src1 & 0xff;
                }
                reg = dest & 0xff;
                reg1 = (reg >> 4) & 0xf;
                op = dest >> 8;
                if (hst_lnt) {
                    hst[hst_p].cc = cc;
                    hst_p = hst_p + 1;
                    if (hst_p >= hst_lnt)
                        hst_p = 0;
                    hst[hst_p].pc = addr1 | HIST_PC;
                    hst[hst_p].inst[0] = dest;
                }
                addr1 += 2;
                if (op == OP_EX)
                    storepsw(OPPSW, IRC_EXEC);
                /* Check if RX, RR, SI, RS, SS opcode */
                else {
                    if (op & 0xc0) {
                        if (ReadHalf(addr1, &src1))
                            break;
                        addr1+=2;
                        /* Check if SS */
                        if ((op & 0xc0) == 0xc0) {
                            if(ReadHalf(addr1, &src2))
                               break;
                            addr2 = src2;
                        }
                        addr1 = src1;
                        if (hst_lnt) {
                            hst[hst_p].inst[1] = addr1;
                            hst[hst_p].inst[2] = addr2;
                        }
                    }
                    goto opr;
                }
                break;


                     /* Floating Half register */
        case OP_HDR:
                src2h >>= 1;
                if (src2 & 1)
                   src2h |= MSIGN;
                /* Fall through */

        case OP_HER:
                src2 = (src2 & (EMASK|MSIGN)) | ((src2 & MMASK) >> 1);
                /* Fall through */

                /* Floating Load register */
        case OP_LER:
        case OP_LDR:
        case OP_LE:
        case OP_LD:
                if ((op & 0x10) == 0)
                    fpregs[reg1|1] = src2h;
                fpregs[reg1] = src2;
                break;

                 /* Floating Load register change sign */
        case OP_LPDR:
        case OP_LNDR:
        case OP_LTDR:
        case OP_LCDR:
        case OP_LPER:
        case OP_LNER:
        case OP_LTER:
        case OP_LCER:
                if ((op & 0x2) == 0)  /* LP, LT */
                    src2 &= ~MSIGN;
                if ((op & 0x1))       /* LN, LC */
                    src2 ^= MSIGN;
                cc = 0;
                src1 = src2 & MMASK;
                if ((op & 0x10) == 0) {
                    fpregs[reg1|1] = src2h;
                    src1 |= src2h;
                }
                if (src1 != 0)
                    cc = (src2 & MSIGN) ? 1 : 2;
                fpregs[reg1] = src2;
                break;

                  /* Floating Store register */
        case OP_STD:
                if (WriteFull(addr2 + 4, src1h))
                    break;
                /* Fall through */

        case OP_STE:
                WriteFull(addr2, src1);
                break;

                  /* Floating Compare */
        case OP_CE:      /* 79 */
        case OP_CD:      /* 69 */
        case OP_CER:     /* 39 */
        case OP_CDR:     /* 29 */

                  /* Floating Subtract */
        case OP_SE:      /* 7B */
        case OP_SD:      /* 6B */
        case OP_SW:      /* 6F */
        case OP_SU:      /* 7F */
        case OP_SER:     /* 3B */
        case OP_SDR:     /* 2B */
        case OP_SWR:     /* 2F */
        case OP_SUR:     /* 3F */
                src2 ^= MSIGN;
                   /* Fall through */

                   /* Floating Add */
        case OP_AE:      /* 7A */
        case OP_AD:      /* 6A */
        case OP_AW:      /* 6E */
        case OP_AU:      /* 7E */
        case OP_AER:     /* 3A */
        case OP_ADR:     /* 2A */
        case OP_AWR:     /* 2E */
        case OP_AUR:     /* 3E */
                  /* Extract numbers and adjust */
                  e1 = (src1 & EMASK) >> 24;
                  e2 = (src2 & EMASK) >> 24;
                  fill = 0;
                  if (src1 & MSIGN)
                     fill |= 1;
                  if (src2 & MSIGN)
                     fill |= 2;
                  /* Create gaurd digit */
                  src1 = ((src1 & MMASK) << 4) | ((src1h >> 28) & 0xf);
                  src2 = ((src2 & MMASK) << 4) | ((src2h >> 28) & 0xf);
                  src1h &= XMASK;
                  src2h &= XMASK;
                  /* Shift src2 right if src1 larger expo - expo */
                  while (e1 > e2) {
                     src2h >>= 4;
                     src2h |= (src2 & 0xf) << 24;
                     src2 >>= 4;
                     e2 ++;
                  }
                  /* Shift src1 right if src2 larger expo - expo */
                  while (e2 > e1) {
                     src1h >>= 4;
                     src1h |= (src1 & 0xf) << 24;
                     src1 >>= 4;
                     e1 ++;
                  }
                  /* Exponents should be equal now. */
                  /* Add results */
                  if (fill == 2 || fill == 1) {
                       /* Different signs do subtract */
                       src1 ^= XMASK;
                       src1h ^= XMASK;
                       src1h++;
                       if (src1 & CMASK) {
                          src1++;
                          src1h &= XMASK;
                       }
                       src1h += src2h;
                       if (src1h & CMASK) {
                           src1 += ((src1h >> 28) & 0xf);
                           src1h &= XMASK;
                       }
                       src1 += src2;
                       if (src1 & CMASK)
                           src1 ^= CMASK;
                       else {
                           fill ^= 2;
                           src1 ^= XMASK;
                           src1h ^= XMASK;
                           src1h++;
                           if (src1 & CMASK) {
                              src1++;
                              src1h &= XMASK;
                           }
                       }
                  } else {
                       src1h += src2h;
                       if (src1h & CMASK) {
                           src1 += ((src1h >> 28) & 0xf);
                           src1h &= XMASK;
                       }
                       src1 += src2;
                  }
                  /* If src1 not normal shift left + expo */
                  if (src1 & CMASK) {
                     src1h >>= 4;
                     src1h |= (src1 & 0xf) << 24;
                     src1 >>= 4;
                     e1 ++;
                  }
                  /* Compute sign of result */
                  cc = 0;
                  if ((src1h | src1) != 0)
                       cc = (temp & 2) ? 1 : 2;
                  /* Set condition codes */
                  /* If (op & 0xF) == 0x9, compare */
                  if ((op & 0xF) == 0x9)  /* Compare operator */
                     break;               /* All done */
                  /* If (op & 0xE) != 0xE, normalize */
                  if ((op & 0xE) != 0xE && cc != 0) { /* Normalize operator */
                     while ((src1 & EMASK) == 0) {
                        src1 = (src1 << 4) | ((src1h >> 24) & 0xf);
                        src1h <<= 4;
                        src1h &= XMASK;
                        e1 --;
                     }
                  }
                  /* Store result */
                  src1h |= (src1 & 0xf) << 28;
                  src1 >>= 4;
                  /* If exp < 0, underflow */
                  /* If exp > 64 overflow */
                  src1 |= (e1 << 24) & EMASK;
                  if (fill & 2)
                     src1 |= MSIGN;
                  if ((op & 0x10) == 0) {
                      fpregs[reg1|1] = src2h;
                      src1 |= src2h;
                  }
                  fpregs[reg1] = src2;
                  break;

                  /* Multiply */
        case OP_MDR:
        case OP_MER:
        case OP_ME:
        case OP_MD:

                  /* Divide */
        case OP_DER:
        case OP_DDR:
        case OP_DD:
        case OP_DE:

                  /* Decimal operations */
        case OP_CP:
        case OP_SP:
        case OP_ZAP:
        case OP_AP:
                  /* Get sign of second operand */
                  /* If op & 1 flip sign */
                  /* Get sign of first operand */
                  /* Compute sign of result */
        case OP_MP:
        case OP_DP:
//                storepsw(OPPSW, IRC_OPR);
 //               goto supress;
                break;

                  /* Extended precision load round */
        case OP_LRER:
        case OP_LRDR:
        case OP_SXR:
        case OP_AXR:
        case OP_MXR:
        case OP_MXDR:
        case OP_MXD:
        default:
                reason=1;
                storepsw(OPPSW, IRC_OPR);
                goto supress;
        }

        if (hst_lnt) {
             hst[hst_p].dest = dest;
             hst[hst_p].cc = cc;
        }

        if (irqaddr != 0) {
supress:
             src1 = M[irqaddr>>2];
        if (hst_lnt) {
             hst_p = hst_p + 1;
             if (hst_p >= hst_lnt)
                hst_p = 0;
             hst[hst_p].pc = irqaddr | HIST_LPW;
             hst[hst_p].src1 = src1;
        }
             irqaddr += 4;
             src2 = M[irqaddr>>2];
        if (hst_lnt) {
             hst[hst_p].src2 = src2;
        }
             sysmsk = (src1 >> 24) & 0xff;
             st_key = (src1 >> 16) & 0xf0;
             flags = (src1 >> 16) & 0xf;
             irqcode = 0;
             irqaddr = 0;
             pmsk = (src2 >> 24) & 0xcf;
             cc = (src2 >> 28) & 0x3;
             PC = src2 & 0xffffff;
             irq_pend = 1;
        }
        sim_interval--;
    }
}

/* Reset */

t_stat cpu_reset (DEVICE *dptr)
{
    st_key = cc = pmsk = irqcode = flags = irqaddr = loading = 0;
    chan_set_devs();
    if (M == NULL) {                        /* first time init? */
        sim_brk_types = sim_brk_dflt = SWMASK ('E');
        M = (uint32 *) calloc (((uint32) MEMSIZE) >> 2, sizeof (uint32));
        if (M == NULL)
            return SCPE_MEM;
    }
    return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
int32 st;
uint32 addr = (uint32) exta;
uint32 byte;

if (vptr == NULL)
    return SCPE_ARG;
     /* Ignore high order bits */
     addr &= 0xffffff;
     if (addr >= MEMSIZE)
        return SCPE_NXM;

     byte = M[addr >> 2] >> (8 * (3 - (addr & 0x3)));
     byte &= 0xff;
*vptr = byte;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
int32 st;
uint32 addr = (uint32) exta;

if (WriteByte (addr, val))
    return SCPE_NXM;
return SCPE_OK;
}

/* Memory allocation */

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 mc = 0;
int32 i, clim;
uint32 *nM = NULL;
int32 max = MEMSIZE >> 2;

val = val >> UNIT_V_MSIZE;
val = 16 * 1024 * val;
if ((val <= 0) || (val > MAXMEMSIZE))
    return SCPE_ARG;
for (i = val>>2; i < max; i++)
    mc = mc | M[i];
if ((mc != 0) && !get_yn ("Really truncate memory [N]?", FALSE))
    return SCPE_OK;
nM = (uint32 *) calloc (val >> 2, sizeof (uint32));
if (nM == NULL)
    return SCPE_MEM;
clim = (val < MEMSIZE)? val >> 2: max;
for (i = 0; i < clim; i++)
    nM[i] = M[i];
free (M);
M = nM;
fprintf(stderr, "Mem size=%x\n\r", val);
MEMSIZE = val;
cpu_unit.flags &= ~UNIT_MSIZE;
cpu_unit.flags |= val / (16 * 1024);
reset_all (0);
return SCPE_OK;
}

/* Handle execute history */

/* Set history */
t_stat
cpu_set_hist(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int32               i, lnt;
    t_stat              r;

    if (cptr == NULL) {
        for (i = 0; i < hst_lnt; i++)
            hst[i].pc = 0;
        hst_p = 0;
        return SCPE_OK;
    }
    lnt = (int32) get_uint(cptr, 10, HIST_MAX, &r);
    if ((r != SCPE_OK) || (lnt && (lnt < HIST_MIN)))
        return SCPE_ARG;
    hst_p = 0;
    if (hst_lnt) {
        free(hst);
        hst_lnt = 0;
        hst = NULL;
    }
    if (lnt) {
        hst = (struct InstHistory *)calloc(sizeof(struct InstHistory), lnt);

        if (hst == NULL)
            return SCPE_MEM;
        hst_lnt = lnt;
    }
    return SCPE_OK;
}

/* Show history */

t_stat
cpu_show_hist(FILE * st, UNIT * uptr, int32 val, CONST void *desc)
{
    int32               k, di, lnt;
    const char          *cptr = (const char *) desc;
    t_stat              r;
    t_value             sim_eval;
    struct InstHistory *h;

    if (hst_lnt == 0)
        return SCPE_NOFNC;      /* enabled? */
    if (cptr) {
        lnt = (int32) get_uint(cptr, 10, hst_lnt, &r);
        if ((r != SCPE_OK) || (lnt == 0))
            return SCPE_ARG;
    } else
        lnt = hst_lnt;
    di = hst_p - lnt;           /* work forward */
    if (di < 0)
        di = di + hst_lnt;
    fprintf(st, "PC     A1     A2     D1       D2       RESULT   CC\n\n");
    for (k = 0; k < lnt; k++) { /* print specified */
        h = &hst[(++di) % hst_lnt];     /* entry pointer */
        if (h->pc & HIST_PC) {   /* instruction? */
            int i;
            fprintf(st, "%06x %06x %06x %08x %08x %08x %1x %04x ",
                       h->pc & PAMASK, h->addr1 & PAMASK, h->addr2 & PAMASK,
                       h->src1, h->src2, h->dest, h->cc, h->inst[0]);
            if ((h->op & 0xc0) != 0)
                  fprintf(st, "%04x ", h->inst[1]);
            else
                  fprintf(st, "     ");
            if ((h->op & 0xc0) == 0xc0)
                  fprintf(st, "%04x ", h->inst[2]);
            else
                  fprintf(st, "     ");
            fprintf(st, "  ");
            fprint_inst(st, h->inst);
            fputc('\n', st);    /* end line */
        }                       /* end else instruction */
        if (h->pc & HIST_LPW) {   /* load PSW */
            int i;
            fprintf(st, " LPSW  %06x     %08x %08x\n", h->pc & PAMASK, h->src1, h->src2);
        }                       /* end else instruction */
        if (h->pc & HIST_SPW) {   /* load PSW */
            int i;
            fprintf(st, " SPSW  %06x     %08x %08x\n", h->pc & PAMASK,  h->src1, h->src2);
        }                       /* end else instruction */
    }                           /* end for */
    return SCPE_OK;
}


t_stat              cpu_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "IBM360 CPU\n\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *
cpu_description (DEVICE *dptr)
{
       return "IBM 360 CPU";
}

