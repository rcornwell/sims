/* ibm360_cpu.c: ibm 360 cpu simulator.

   Copyright (c) 2017, Richard Cornwell

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
#define FEAT_DAT     (1 << (UNIT_V_UF + 13))     /* Dynamic address translation */
#define EXT_IRQ      (1 << (UNIT_V_UF_31))       /* External interrupt */

#define UNIT_V_MSIZE (UNIT_V_UF + 0)             /* dummy mask */
#define UNIT_MSIZE   (0xff << UNIT_V_MSIZE)
#define MEMAMOUNT(x) (x << UNIT_V_MSIZE)

#define TMR_RTC      0

#define HIST_MAX     5000000
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
uint8        sysmskh;              /* High order system mask */
uint8        st_key;               /* Storage key */
uint8        cc;                   /* CC */
uint8        ilc;                  /* Instruction length code */
uint8        pmsk;                 /* Program mask */
uint16       irqcode;              /* Interupt code */
uint8        flags;                /* Misc flags */
uint16       irqaddr;              /* Address of IRQ vector */
uint16       loading;              /* Doing IPL */
uint8        interval_irq = 0;     /* Interval timer IRQ */
uint8        dat_en = 0;           /* Translate addresses */
uint32       segtable;             /* Address of segment table */
uint8        seglen;               /* Length of segment table */
uint32       tlb[256];             /* Translation look aside buffer */
uint32       execp_error;          /* Translation error */

#define DAT_ENABLE  0x01           /* DAT enabled */


#define ASCII       0x08           /* ASCII/EBCDIC mode */
#define MCHECK      0x04           /* Machine check flag */
#define WAIT        0x02
#define PROBLEM     0x01           /* Problem state */

#define FIXOVR      0x08           /* Fixed point overflow */
#define DECOVR      0x04           /* Decimal overflow */
#define EXPUND      0x02           /* Exponent overflow */
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
#define IRC_SEG     0x0010         /* Segment translation */
#define IRC_PAGE    0x0011         /* Pagre translation */

#define AMASK       0x00ffffff     /* Mask address bits */
#define MSIGN       0x80000000     /* Minus sign */
#define MMASK       0x00ffffff     /* Mantissa mask */
#define EMASK       0x7f000000     /* Exponent mask */
#define XMASK       0x0fffffff     /* Working FP mask */
#define HMASK       0x7fffffff     /* Working FP mask */
#define FMASK       0xffffffff     /* Working FP mask */
#define CMASK       0x10000000     /* Carry mask */
#define NMASK       0x00f00000     /* Normalize mask */

#define R1(x)   (((x)>>4) & 0xf)
#define R2(x)   ((x) & 0xf)
#define B1(x)   (((x) >> 12) & 0xf)
#define D1(x)   ((x) & 0xfff)
#define X2(x)   R2(x)

#define PTE_LEN     0xff000000     /* Page table length */
#define PTE_ADR     0x00fffffe     /* Address of table */
#define PTE_VALID   0x00000001     /* table valid */

#define PTE_PHY     0xfff0         /* Physical page address */
#define PTE_AVAL    0x0008         /* Page tranlation error */
#define PTE_MBZ     0x0007         /* Bits must be zero */

#define TLB_SEG     0x7ffff000     /* Segment address */
#define TLB_VALID   0x80000000     /* Entry valid */
#define TLB_PHY     0x00000fff     /* Physical page */

#define SEG_MASK    0xfffff000     /* Mask segment */

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

#if 0
#include <math.h>

double cnvt_float(uint32 l, uint32 h) {
    t_uint64        t64;
    double          d;
    int             e;
    e = ((l >> 24) & 0x7f) - 64;
    t64 = ((t_uint64)l & MMASK) << 32LL | h;
    d = (double)t64;
    d *= exp2(-56 + 4*e);
    if (l & MSIGN)
    d *= -1.0;
    return d;
}
#endif

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

void   dec_add(int op, uint32 addr1, uint8 len1, uint32 addr2, uint8 len2);
void   dec_mul(int op, uint32 addr1, uint8 len1, uint32 addr2, uint8 len2);
void   dec_div(int op, uint32 addr1, uint8 len1, uint32 addr2, uint8 len2);

t_bool build_dev_tab (void);

/* Interval timer option */
t_stat              rtc_srv(UNIT * uptr);
t_stat              rtc_reset(DEVICE * dptr);
int32               rtc_tps = 60;


/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = { UDATA (&rtc_srv, UNIT_BINK, MAXMEMSIZE) };

REG cpu_reg[] = {
    { HRDATA (PC, PC, 24) },
    { HRDATA (CC, cc, 2) },
    { HRDATA (PMASK, pmsk, 4) },
    { HRDATA (FLAGS, flags, 4) },
    { HRDATA (KEY, st_key, 4) },
    { HRDATA (R0, regs[00], 32) },
    { HRDATA (R1, regs[01], 32) },
    { HRDATA (R2, regs[02], 32) },
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
    { BRDATA (R, regs, 16, 32, 16) },
    { HRDATA (FP0, fpregs[00], 32) },
    { HRDATA (FP2, fpregs[02], 32) },
    { HRDATA (FP4, fpregs[04], 32) },
    { HRDATA (FP6, fpregs[06], 32) },
    { BRDATA (FP, fpregs, 16, 32, 8) },
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
    { UNIT_MSIZE, MEMAMOUNT(128), "2M", "2M", &cpu_set_size },
    { FEAT_PROT, 0, NULL, "NOPROT", NULL, NULL, NULL, "No Storage protection"},
    { FEAT_PROT, FEAT_PROT, "PROT", "PROT", NULL, NULL, NULL, "Storage protection"},
    { FEAT_DEC, 0, NULL, "NODECIMAL", NULL, NULL, NULL},
    { FEAT_DEC, FEAT_DEC, "DECIMAL", "DECIMAL", NULL, NULL, NULL, "Decimal instruction set"},
    { FEAT_FLOAT, 0, NULL, "NOFLOAT", NULL, NULL, NULL},
    { FEAT_FLOAT, FEAT_FLOAT, "FLOAT", "FLOAT", NULL, NULL, NULL, "Floating point instruction"},
    { FEAT_UNIV, FEAT_UNIV, NULL, "UNIV", NULL, NULL, NULL, "Universal instruction"},
    { FEAT_STOR, 0, NULL, "NOSTORE", NULL, NULL, NULL},
    { FEAT_STOR, FEAT_STOR, "STORE", "STORE", NULL, NULL, NULL, "No storage alignment"},
    { FEAT_TIMER, 0, NULL,  "NOTIMER", NULL, NULL},
    { FEAT_TIMER, FEAT_TIMER, "TIMER", "TIMER", NULL, NULL, NULL, "Interval timer"},
    { FEAT_DAT, 0, NULL,  "NODAT", NULL, NULL},
    { FEAT_DAT, FEAT_DAT, "DAT", "DAT", NULL, NULL, NULL, "Dat /67"},
    { EXT_IRQ, 0, "NOEXT",  NULL, NULL, NULL},
    { EXT_IRQ, EXT_IRQ, "EXT", "EXT", NULL, NULL, NULL, "External Irq"},
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 16, 24, 1, 16, 8,
    &cpu_ex, &cpu_dep, &cpu_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, dev_debug,
//    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description
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
        sim_debug(DEBUG_INST, &cpu_dev, "store %02x %d %x PSW=%08x %08x  ", addr, ilc, cc, (((uint32)sysmsk) << 24) |
            (((uint32)st_key) << 16) | (((uint32)flags) << 16) | ((uint32)ircode),
             (((uint32)ilc) << 30) | (((uint32)cc) << 28) | (((uint32)pmsk) << 24) | PC);
     irqaddr = addr + 0x40;
     word = (((uint32)sysmsk) << 24) |
            (((uint32)st_key) << 16) |
            (((uint32)flags) << 16) |
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
     word = (((uint32)ilc) << 30) |
            (((uint32)cc) << 28) |
            (((uint32)pmsk) << 24) |
            PC;
     M[addr >> 2] = word;
        if (hst_lnt) {
             hst[hst_p].src2 = word;
        }
     irqcode = ircode;
}

/*
 * Translate an address from virtual to physical.
 */
int  TransAddr(uint32 va, uint32 *pa) {
     uint32  seg;
     uint32  page;
     uint32  entry;
     uint32  addr;

     /* Check address in range */
     va &= AMASK;
     if (va >= MEMSIZE) {
        storepsw(OPPSW, IRC_ADDR);
        return 1;
     }

     if (!dat_en) {
        *pa = va;
        return 0;
     }

     seg = (va & SEG_MASK) >> 12;
     page = seg & 0xff;
     /* Quick check if TLB correct */
     entry = tlb[page];
     if ((entry & TLB_VALID) != 0 && ((entry ^ seg) & TLB_SEG) == 0) {
         *pa = (va & 0xfff) | ((entry & TLB_PHY) << 12);
         return 0;
     }
     /* TLB not correct, try loading correct entry */
     seg >>= 8;         /* Segment number to word address */
     if ((seg >> 4) != 0) {
         execp_error = va;
        storepsw(OPPSW, IRC_SEG);
         /* Not valid address */
         return 1;
     }
     addr = ((seg & 0xFFF) << 2) + segtable;
     /* Ignore high order bits */
     addr &= AMASK;
     if (addr >= MEMSIZE) {
        storepsw(OPPSW, IRC_ADDR);
        return 1;
     }
     entry = M[addr >> 2];
     /* Check if entry valid and in correct length */
     if (entry & PTE_VALID || page > (entry >> 24)) {
        storepsw(OPPSW, IRC_PAGE);
        execp_error = va;
        return 1;
     }

     /* Now we need to fetch the actual entry */
     addr = (((entry & PTE_ADR) >> 1) + page) << 2;
     addr &= AMASK;
     if (addr >= MEMSIZE) {
        storepsw(OPPSW, IRC_ADDR);
        return 1;
     }
     entry = M[addr >> 2];
     entry >>= (addr & 2) ? 0 : 16;
     entry &= 0xffff;

     if ((entry & (PTE_MBZ)) != 0) {
         storepsw(OPPSW, IRC_SPEC);
         execp_error = va;
         return 1;
     }

     /* Check if entry valid and in correct length */
     if (entry & PTE_AVAL) {
        storepsw(OPPSW, IRC_PAGE);
         execp_error = va;
        return 1;
     }

     /* Compute correct entry */
     entry >>= 4; /* Move physical to correct spot */
     entry |= (va & TLB_SEG) | TLB_VALID;
     tlb[page] = entry;
     *pa = (va & 0xfff) | ((entry & TLB_PHY) << 12);
     return 0;
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
     addr &= AMASK;
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
         k = key[addr >> 9];
         if ((k & 0x8) != 0 && (k & 0xf0) != st_key) {
//fprintf(stderr, "Protstore %08x %x %x\n\r", addr <<2, k ,st_key);
             storepsw(OPPSW, IRC_PROT);
             return 1;
         }
     }

     *data = M[addr];
     if (offset != 0) {
         if ((cpu_unit.flags & FEAT_STOR) == 0) {
              storepsw(OPPSW, IRC_SPEC);
              return 1;
         }
         temp = addr + 1;
         if ((temp & 0x1ff) == 0 && st_key != 0) {
             k = key[temp >> 9];
             if ((k & 0x8) != 0 && (k & 0xf0) != st_key) {
//fprintf(stderr, "Protstore %08x %x %x\n\r", addr <<2, k ,st_key);
                 storepsw(OPPSW, IRC_PROT);
                 return 1;
             }
         }
         temp = M[temp];
         *data <<= 8 * offset;
         temp >>= 8 * (4 - offset);
         *data = temp;
     }
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
         if ((cpu_unit.flags & FEAT_STOR) == 0) {
              storepsw(OPPSW, IRC_SPEC);
              return 1;
         }

         /* Check if past a word */
         if (addr & 0x2) {
             uint32   temp = 0;
             if (ReadFull(addr + 1, &temp))
                 return 1;
             if (ReadFull(addr & (~0x3), data))
                 return 1;
             *data <<= 8;
             *data |= (temp >> 24);
         } else {
             if (ReadFull(addr & (~0x3), data))
                 return 1;
             *data >>= 8;
         }
     } else {
         if (ReadFull(addr & (~0x3), data))
             return 1;
         *data >>= (addr & 2) ? 0 : 16;
     }
     *data &= 0xffff;
     if (*data & 0x8000)
         *data |= 0xffff0000;
     return 0;
}

int WriteFull(uint32 addr, uint32 data) {
     int     offset;
     uint8   k;
     /* Ignore high order bits */
     addr &= AMASK;
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
         k = key[addr >> 9];
         if ((k & 0xf0) != st_key) {
//fprintf(stderr, "Protstore %08x %x %x\n\r", addr <<2, k ,st_key);
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
        if (((addr & 0x1ff) == 0x1ff) &&  st_key != 0) {
            k = key[(addr + 1) >> 9];
            if ((k & 0xf0) != st_key) {
//fprintf(stderr, "Protstore %08x %x %x\n\r", addr <<2, k ,st_key);
                storepsw(OPPSW, IRC_PROT);
                return 1;
            }
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
        if (((addr & 0x1ff) == 0x1ff) &&  st_key != 0) {
            k = key[(addr + 1) >> 9];
            if ((k & 0xf0) != st_key) {
//fprintf(stderr, "Protstore %08x %x %x\n\r", addr <<2, k ,st_key);
                storepsw(OPPSW, IRC_PROT);
                return 1;
            }
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
        if (((addr & 0x1ff) == 0x1ff) &&  st_key != 0) {
            k = key[(addr + 1) >> 9];
            if ((k & 0xf0) != st_key) {
//fprintf(stderr, "Protstore %08x %x %x\n\r", addr <<2, k ,st_key);
                storepsw(OPPSW, IRC_PROT);
                return 1;
            }
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
     addr &= AMASK;
     if (addr >= MEMSIZE) {
         storepsw(OPPSW, IRC_ADDR);
         return 1;
     }

     offset = 8 * (3 - (addr & 0x3));
     addr >>= 2;

     /* Check storage key */
     if (st_key != 0) {
         if ((cpu_unit.flags & FEAT_PROT) == 0) {
             storepsw(OPPSW, IRC_PROT);
             return 1;
         }
         k = key[addr >> 9];
         if ((k & 0xf0) != st_key) {
//fprintf(stderr, "Protstore %08x %x %x\n\r", addr <<2, k ,st_key);
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
     addr &= AMASK;
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
        k = key[addr >> 9];
        if ((k & 0xf0) != st_key) {
//fprintf(stderr, "Protstore %08x %x %x\n\r", addr <<2, k ,st_key);
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
          if (((addr & 0x1ff) == 0x1ff) &&  st_key != 0) {
              k = key[(addr + 1) >> 9];
              if ((k & 0xf0) != st_key) {
//fprintf(stderr, "Protstore %08x %x %x\n\r", addr <<2, k ,st_key);
                  storepsw(OPPSW, IRC_PROT);
                  return 1;
              }
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
    uint32          desth;
    uint32          addr1;
    uint32          addr2;
    uint8           op;
    uint8           fill;
    uint8           reg;
    uint8           reg1;
    uint16          irq;
    int             e1, e2;
    int             temp, temp2;
    t_uint64        t64;
    t_uint64        t64a;
    uint16          ops[3];
//double    a, b, c;

    reason = SCPE_OK;
    ilc = 0;
    /* Enable timer if option set */
    if (cpu_unit.flags & FEAT_TIMER) {
        sim_activate(&cpu_unit, 100);
    }
    interval_irq = 0;

    while (reason == SCPE_OK) {

wait_loop:
        if (sim_interval <= 0) {
            reason = sim_process_event();
            if (reason != SCPE_OK)
               return reason;
        }

        irq= scan_chan(sysmsk);
        if (irq!= 0) {
            ilc = 0;
            sim_debug(DEBUG_DETAIL, &cpu_dev, "IRQ=%04x %08x\n", irq, PC);
            if (loading) {
               irqcode = irq;
               (void)WriteHalf(0x2, irq);
               loading = 0;
               irqaddr = 0;
            } else
               storepsw(OIOPSW, irq);
            goto supress;
        }

        if ((cpu_unit.flags & EXT_IRQ) && (sysmsk & 01)) {
            ilc = 0;
            cpu_unit.flags &= ~EXT_IRQ;
            storepsw(OEPSW, 0x40);
            goto supress;
        }

        if (interval_irq && (sysmsk & 01)) {
            ilc = 0;
            interval_irq = 0;
            storepsw(OEPSW, 0x80);
            goto supress;
        }

        if (loading || flags & WAIT) {
            /* CPU IDLE */
            if (flags & WAIT && sysmsk == 0)
               return STOP_HALT;
            sim_interval--;
            goto wait_loop;
        }

        if (sim_brk_summ && sim_brk_test(PC, SWMASK('E'))) {
           return STOP_IBKPT;
        }

        if (PC & 1) {
            ilc = 0;
            storepsw(OPPSW, IRC_SPEC);
            goto supress;
        }

        if (hst_lnt) {
             hst_p = hst_p + 1;
             if (hst_p >= hst_lnt)
                hst_p = 0;
             hst[hst_p].pc = PC | HIST_PC;
        }

        sim_debug(DEBUG_INST, &cpu_dev, "PSW=%08x %08x  ", (((uint32)sysmsk) << 24) |
            (((uint32)st_key) << 16) | (((uint32)flags) << 16) | ((uint32)irqcode),
             (((uint32)ilc) << 30) | (((uint32)cc) << 28) | (((uint32)pmsk) << 24) | PC);
        ilc = 0;
        if (ReadHalf(PC, &dest))
            goto supress;
        ops[0] = dest;
        ilc = 1;
        if (hst_lnt)
             hst[hst_p].inst[0] = ops[0];
        PC += 2;
        reg = (uint8)(ops[0] & 0xff);
        reg1 = R1(reg);
        op = (uint8)(ops[0] >> 8);
        /* Check if RX, RR, SI, RS, SS opcode */
        if (op & 0xc0) {
            if (ReadHalf(PC, &dest))
                goto supress;
            ops[1] = dest;
            ilc = 2;
            PC += 2;
            if (hst_lnt)
                hst[hst_p].inst[1] = ops[1];
            /* Check if SS */
            if ((op & 0xc0) == 0xc0) {
                if (ReadHalf(PC, &dest))
                    goto supress;;
                ops[2] = dest;
                PC += 2;
                ilc = 3;
                if (hst_lnt)
                     hst[hst_p].inst[2] = ops[2];
            }
        }

        if (sim_deb && (cpu_dev.dctrl & DEBUG_INST)) {
           sim_debug(DEBUG_INST, &cpu_dev, "%d INST=%04x", ilc, ops[0]);
           if (ops[0] & 0xc000) {
               sim_debug(DEBUG_INST, &cpu_dev, "%04x",  ops[1]);
               if ((ops[0] & 0xc000) == 0xc000)
                   sim_debug(DEBUG_INST, &cpu_dev, "%04x", ops[2]);
               else
                   sim_debug(DEBUG_INST, &cpu_dev, "    ");
           } else
               sim_debug(DEBUG_INST, &cpu_dev, "        ");
           sim_debug(DEBUG_INST, &cpu_dev, "    ");
           fprint_inst(sim_deb, ops);
        }

        /* Add in history here */
opr:

        /* If RX or RS or SS SI etc compute first address */
        if (op & 0xc0) {
            uint32        temp;

            temp = B1(ops[1]);
            addr1 = D1(ops[1]);
            if (temp)
                addr1 = (addr1 + regs[temp]) & AMASK;
            /* Handle RX type operands */
            if ((op & 0x80) == 0 && X2(reg) != 0)
                addr1 = (addr1 + regs[X2(reg)]) & AMASK;
            /* Check if SS */
            if ((op & 0xc0) == 0xc0) {
                temp = B1(ops[2]);
                addr2 = D1(ops[2]);
                if (temp)
                    addr2 = (addr2 + regs[temp]) & AMASK;
            }
         }

        /* Check if floating point */
        if ((op & 0xA0) == 0x20) {
            if ((cpu_unit.flags & FEAT_FLOAT) == 0) {
                storepsw(OPPSW, IRC_OPR);
                goto supress;
            }
            if (reg1 & 0x9) {
//fprintf(stderr, "Spec FP Reg=%x\n\r", reg1);
                storepsw(OPPSW, IRC_SPEC);
                goto supress;
            }
            /* Load operands */
            src1 = fpregs[reg1];
            if ((op & 0x10)  == 0)
                 src1h = fpregs[reg1|1];
            else
                 src1h = 0;
            if (op & 0x40) {
                if ((op & 0x10) != 0 && (addr1 & 0x3) != 0) {
//fprintf(stderr, "Spec FP Op=%0x  Op=%x\n\r", op, addr1);
                   storepsw(OPPSW, IRC_SPEC);
                   goto supress;
                }

                if (ReadFull(addr1, &src2))
                   goto supress;
                if ((op & 0x10) == 0) {
                   if (ReadFull(addr1+ 4, &src2h))
                       goto supress;
                } else
                   src2h = 0;
//if ((op & 0xf) > 8)
//fprintf(stderr, "RD FP Op=%0x %08x %08x\n\r", op, src2, src2h);
            } else {
                if (reg & 0x9) {
//fprintf(stderr, "Spec FP Reg2=%x\n\r", reg);
                   storepsw(OPPSW, IRC_SPEC);
                   goto supress;
                }
                src2 = fpregs[R2(reg)];
                if ((op & 0x10) == 0)
                    src2h = fpregs[R2(reg)|1];
                else
                    src2h = 0;
//if ((op & 0xf) > 8)
//fprintf(stderr, "RD FP Op=%0x %08x %08x\n\r", op, src2, src2h);
            }
                /* All RR opcodes */
        } else if ((op & 0xe0) == 0) {
                src1 = regs[reg1];
                dest = src2 = regs[R2(reg)];
                addr1 = dest & AMASK;
                /* All RX integer ops */
        } else if ((op & 0xe0) == 0x40) {
                dest = src1 = regs[reg1];
                /* Read half word if 010010xx or 01001100 */
                if ((op & 0x1c) == 0x08 || op == OP_MH)  {
                     if (ReadHalf(addr1, &src2))
                        goto supress;
                /* Read full word if 0101xxx and not xxxx00xx (ST) */
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
        }
        sim_debug(DEBUG_INST, &cpu_dev, "\n");

        /* Preform opcode */
        switch (op) {
        case OP_SPM:
                dest = src1;
                pmsk = (src1 >> 24) & 0xf;
                cc = (src1 >> 28) & 0x3;
                break;

        case OP_BASR:
        case OP_BAS:
                if ((cpu_unit.flags & FEAT_DAT) == 0) {
                    storepsw(OPPSW, IRC_PROT);
                } else {
                    dest = PC;
                    if (op != OP_BASR || R2(reg) != 0)
                        PC = addr1 & AMASK;
                    regs[reg1] = dest;
                }
                break;

        case OP_BALR:
        case OP_BAL:
                dest = (((uint32)ilc) << 30) |
                       ((uint32)(cc & 03) << 28) |
                       (((uint32)pmsk) << 24) | PC;
                if (op != OP_BALR || R2(reg) != 0)
                    PC = addr1 & AMASK;
                regs[reg1] = dest;
                break;

        case OP_BCTR:
        case OP_BCT:
                dest = src1 - 1;
                if (dest != 0 && (op != OP_BCTR || R2(reg) != 0))
                    PC = addr1 & AMASK;
                regs[reg1] = dest;
                break;

        case OP_BCR:
        case OP_BC:
                dest = src1;
                if (((0x8 >> cc) & reg1) != 0 && (op != OP_BCR || R2(reg) != 0))
                    PC = addr1 & AMASK;
                break;

        case OP_BXH:
                reg = R2(reg);
                src1 = regs[reg|1];
                dest = regs[reg1] = regs[reg1] + regs[reg];
                if ((int32)dest > (int32)src1)
                   PC = addr1 & AMASK;
                break;

        case OP_BXLE:
                reg = R2(reg);
                src1 = regs[reg|1];
                dest = regs[reg1] = regs[reg1] + regs[reg];
                if ((int32)dest <= (int32)src1)
                   PC = addr1 & AMASK;
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
                    key[addr1 >> 11] = src1 & 0xf8;
//if ((src1 & 0xff) != 0)
//fprintf(stderr, "Protset %08x %02x\n\r", addr1, src1);
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
                    dest |= key[addr1 >> 11];
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
                    if (ReadFull(addr1, &src1))
                        goto supress;
                    if (ReadFull(addr1+4, &src2))
                        goto supress;
                    if (hst_lnt) {
                         hst_p = hst_p + 1;
                         if (hst_p >= hst_lnt)
                            hst_p = 0;
                         hst[hst_p].pc = irqaddr | HIST_LPW;
                         hst[hst_p].src1 = src1;
                         hst[hst_p].src2 = src2;
                    }
                    goto lpsw;
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
                    if (pmsk & FIXOVR)
                       storepsw(OPPSW, IRC_FIXOVR);
                    break;
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
                if ((uint32)dest < (uint32)src1)
                   cc |= 2;
                if (dest != 0)
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
                src1 = regs[reg1|1];
        case OP_MH:
                fill = 0;

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
                break;

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

                if (dest == 0) {
                    storepsw(OPPSW, IRC_FIXDIV);
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
                t64a = t64 % (t_uint64)dest;
                t64 = t64 / (t_uint64)dest;
                if ((t64 & 0xffffffff80000000) != 0) {
                    storepsw(OPPSW, IRC_FIXDIV);
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
                regs[reg1] = src2;
                regs[reg1|1] = src1;
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
                if (ReadByte(addr1, &src1))
                  break;
                if (hst_lnt)
                    hst[hst_p].src1 = src1;
                cc = (src1 & 0x80) ? 1 : 0;
                WriteByte(addr1, dest);
                break;

        case OP_TM:
                if (ReadByte(addr1, &dest))
                    break;
                if (hst_lnt)
                    hst[hst_p].src1 = dest;
                dest &= reg;
                if (dest != 0) {
                    if (reg == dest)
                        cc = 3;
                    else
                        cc = 1;
                 } else
                    cc = 0;
                 break;

        case OP_SRL:
                dest = regs[reg1];
                if (hst_lnt)
                    hst[hst_p].src1 = dest;
                dest = ((uint32)dest) >> (addr1 & 0x3f);
                regs[reg1] = dest;
                break;

        case OP_SLL:
                dest = regs[reg1];
                if (hst_lnt)
                    hst[hst_p].src1 = dest;
                dest = ((uint32)dest) << (addr1 & 0x3f);
                regs[reg1] = dest;
                break;

        case OP_SRA:
                dest = regs[reg1];
                if (hst_lnt)
                    hst[hst_p].src1 = dest;
                dest = (int32)dest >> (addr1 & 0x3f);
                goto set_cc;

        case OP_SLA:
                dest = regs[reg1];
                if (hst_lnt)
                    hst[hst_p].src1 = dest;
                src2 = dest & MSIGN;
                dest &= ~MSIGN;
                addr1 &= 0x3f;
                cc = 0;
                while (addr1 > 0) {
                    dest <<= 1;
                    if ((dest & MSIGN) != src2)
                        cc = 3;
                    addr1--;
                }
                dest |= src2;
                if (cc == 3)
                    goto set_cc3;
                else
                    goto set_cc;
                break;

        case OP_SRDL:
                if (reg & 1) {
                   storepsw(OPPSW, IRC_SPEC);
                } else {
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

        case OP_STMC:
                if ((cpu_unit.flags & FEAT_DAT) == 0) {
                    storepsw(OPPSW, IRC_PROT);
                } else if (flags & PROBLEM) {
                    storepsw(OPPSW, IRC_PRIV);
                } else {
                    reg &= 0xf;
                    for (;;) {
                        dest = 0;
                        switch (reg) {
                        case 0x0:     /* Segment table address */
                                  dest = segtable;
                                  break;
                        case 0x2:     /* Translation execption */
                                  dest = execp_error;
                                  break;
                        case 0x4:     /* Extended mask */
                        case 0x6:     /* Maskes */
                                  break;
                        case 0x8:     /* Partitioning register */
                        case 0x9:     /* Partitioning register */
                                   /* Compute amount of memory and
                                      assign in 256k blocks to CPU 1 */
                        case 0xA:     /* Partitioning register */
                                   /* Address each 256k bank to 0-0xF */
                        case 0xB:     /* Partitioning register */
                        case 0xC:     /* Partitioning register */
                        case 0xD:     /* Partitioning register */
                        case 0xE:     /* Partitioning register */
                                   /* Return 0 */
                        case 0x1:     /* Unassigned */
                        case 0x3:     /* Unassigned */
                        case 0x5:     /* Unassigned */
                        case 0x7:     /* Unassigned */
                        case 0xF:     /* Unassigned */
                                  break;
                        }
                        if (WriteFull(addr1, dest))
                            goto supress;
                        if (reg1 == reg)
                            break;
                        reg1++;
                        reg1 &= 0xf;
                        addr1 += 4;
                    } ;
                }
                break;

        case OP_LMC:
                if ((cpu_unit.flags & FEAT_DAT) == 0) {
                    storepsw(OPPSW, IRC_PROT);
                } else if (flags & PROBLEM) {
                    storepsw(OPPSW, IRC_PRIV);
                } else {
                    reg &= 0xf;
                    for (;;) {
                        if (ReadFull(addr1, &dest))
                            goto supress;
                        switch (reg) {
                        case 0x0:     /* Segment table address */
                                  if ((dest & 0x3f) != 0)
                                     storepsw(OPPSW, IRC_PRIV);
                                  segtable = dest & AMASK;
                                  for (temp = 0;
                                       temp < sizeof(tlb)/sizeof(uint32);
                                       temp++)
                                      tlb[temp] = 0;
                                  break;
                        case 0x2:     /* Translation execption */
                                  execp_error = dest;
                                  break;
                        case 0x4:     /* Extended mask */
                        case 0x6:     /* Maskes */
                                  break;
                        case 0x1:     /* Unassigned */
                        case 0x3:     /* Unassigned */
                        case 0x5:     /* Unassigned */
                        case 0x7:     /* Unassigned */
                        case 0x8:     /* Partitioning register */
                        case 0x9:     /* Partitioning register */
                        case 0xA:     /* Partitioning register */
                        case 0xB:     /* Partitioning register */
                        case 0xC:     /* Partitioning register */
                        case 0xD:     /* Partitioning register */
                        case 0xE:     /* Partitioning register */
                        case 0xF:     /* Unassigned */
                                   break;
                        }
                        if (reg1 == reg)
                            break;
                        reg1++;
                        reg1 &= 0xf;
                        addr1 += 4;
                    };
                }
                break;

        case OP_LRA:
                if ((cpu_unit.flags & FEAT_DAT) == 0) {
                    storepsw(OPPSW, IRC_PROT);
                } else if (flags & PROBLEM) {
                    storepsw(OPPSW, IRC_PRIV);
                } else {
                    uint32  seg;
                    uint32  page;

                    /* RX in RS range */
                    if (X2(reg) != 0)
                        addr1 = (addr1 + regs[X2(reg)]) & AMASK;
                    addr2 = (addr1 & SEG_MASK) >> 12;
                    src2 = addr2 & 0xff;

                    addr2 >>= 8;         /* Segment number to word address */
                    if ((addr2 >> 4) != 0) {
                        cc = 1;
                        break;
                    }
                    addr2 = ((addr2 & 0xFFF) << 2) + segtable;
                    /* Ignore high order bits */
                    addr2 &= AMASK;
                    if (addr2 >= MEMSIZE) {
                        storepsw(OPPSW, IRC_ADDR);
                        break;
                    }
                    dest = M[addr2 >> 2];
                    /* Check if entry valid and in correct length */
                    if (dest & PTE_VALID || src2 > (dest >> 24)) {
                        cc = 1;
                        break;
                    }

                    /* Now we need to fetch the actual entry */
                    addr2 = (((dest & PTE_ADR) >> 1) + src2) << 2;
                    addr2 &= AMASK;
                    if (addr2 >= MEMSIZE) {
                       storepsw(OPPSW, IRC_ADDR);
                       break;
                    }
                    dest = M[addr2 >> 2];
                    dest >>= (addr2 & 2) ? 0 : 16;
                    dest &= 0xffff;

                    if ((dest & (PTE_AVAL)) != 0) {
                        cc = 2;
                    } else {
                        /* Compute correct entry */
                        dest = (addr1 & 0xfff) | ((dest & TLB_PHY) << 12);
                        regs[reg1] = dest;
                        cc = 0;
                    }
                }
                break;

        case OP_NC:
        case OP_OC:
        case OP_XC:
                cc = 0;
                /* Fall through */

        case OP_MVN:
        case OP_MVZ:
        case OP_MVC:
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
                   if (WriteByte(addr1, dest))
                       break;
                   addr1++;
                   addr2++;
                   reg--;
                } while (reg != 0xff);
                break;

        case OP_CLC:
                cc = 0;
                do {
                    if (ReadByte(addr1, &src1))
                       break;
                    if (ReadByte(addr2, &src2))
                       break;
                    if (src1 != src2) {
                       dest = src1 - src2;
                       cc = (dest & MSIGN) ? 1 : (dest == 0) ? 0 : 2;
                       break;
                    }
                    addr1++;
                    addr2++;
                    reg--;
                } while(reg != 0xff);
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
                       regs[1] |= addr1 & AMASK;
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
                addr2 += reg;
                addr1 += reg1;
                /* Flip first location */
                if (ReadByte(addr2, &dest))
                    break;
                dest = ((dest >> 4) & 0xf) | ((dest << 4) & 0xf0);
                if (WriteByte(addr1, dest))
                    break;
                addr1--;
                addr2--;
                dest = 0;
                while(reg != 0 && reg1 != 0) {
                     if (ReadByte(addr2, &dest))
                         goto supress;
                     dest &= 0xf;
                     addr2--;
                     reg--;
                     if (reg != 0) {
                         if (ReadByte(addr2, &src1))
                             goto supress;
                         dest |= (src1 << 4) & 0xf0;
                         addr2--;
                         reg--;
                     }
                     if (WriteByte(addr1, dest))
                         goto supress;
                     addr1--;
                     reg1--;
                };
                dest = 0;
                while(reg1 != 0) {
                     if (WriteByte(addr1, dest))
                         break;
                     addr1--;
                     reg1--;
                };
                break;

        case OP_UNPK:
                reg &= 0xf;
                addr2 += reg;
                addr1 += reg1;
                if (ReadByte(addr2, &dest))
                    break;
                dest = ((dest >> 4) & 0xf) | ((dest << 4) & 0xf0);
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
                    if (WriteByte(addr1, src1))
                        goto supress;
                    addr1--;
                    reg1--;
                    if (reg1 != 0) {
                        src1 = ((dest >> 4) & 0xf) | src2;
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
                fill = src1h & 0xf; /* Save away sign */
                if (fill < 0xA) {
                    storepsw(OPPSW, IRC_DATA);
                    break;
                }
                if (fill == 0xB || fill == 0xD)
                    fill = 1;
                else
                    fill = 0;
                dest = 0;
                /* Convert upper first */
                for(temp = 28; temp >= 0; temp-=4) {
                    int d = (src1 >> temp) & 0xf;
                    if (d > 0xA) {
                        storepsw(OPPSW, IRC_DATA);
                        break;
                    }
                    dest = (dest * 10) + d;
                }
                /* Convert lower */
                for(temp = 28; temp > 0; temp-=4) {
                    int d = (src1h >> temp) & 0xf;
                    if (d > 0xA) {
                        storepsw(OPPSW, IRC_DATA);
                        break;
                    }
                    dest = (dest * 10) + d;
                }
                if (dest & MSIGN) {
                    storepsw(OPPSW, IRC_FIXDIV);
                    break;
                }
                if (fill)
                    dest = -dest;
                regs[reg1] = dest;
                break;

        case OP_CVD:
                dest = regs[reg1];
                src1 = 0;
                src1h = 0;
                if (dest & MSIGN) {
                    dest = -dest;
                    fill = 1;
                } else
                    fill = 0;
                temp = 4;
                while (dest != 0) {
                    int d = dest % 10;
                    dest /= 10;
                    if (temp > 32)
                        src1 |= (d << (temp - 32));
                    else
                        src1h |= (d << temp);
                    temp += 4;
                }
                if (fill) {
                    src1h |= ((flags & ASCII)? 0xb : 0xd);
                } else {
                    src1h |= ((flags & ASCII)? 0xa : 0xc);
                }

                if (WriteFull(addr1, src1))
                    break;
                WriteFull(addr1+4, src1h);
                break;

        case OP_ED:
        case OP_EDMK:
                if (ReadByte(addr1, &src1))
                    break;
                fill = src1;
                addr1++;
                src2 = 0;
                src1 = 1;
                cc = 0;
                while(reg != 0) {
                    uint8        t;
                    uint32  temp;
                    if (ReadByte(addr1, &temp))
                        break;
                    t = temp;
                    if (src1) {
                        if (ReadByte(addr2, &dest))
                            break;
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
                        if (WriteByte(addr1, t))
                            break;
                    } else if (t == 0x22) {
                        src2 = 0;
                        t = fill;
                        if (WriteByte(addr1, t))
                            break;
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
                ops[0] = dest;
                if (reg1) {
                   ops[0] |= src1 & 0xff;
                }
                reg = ops[0] & 0xff;
                reg1 = R1(reg);
                op = ops[0] >> 8;
                if (hst_lnt) {
                    hst[hst_p].cc = cc;
                    hst_p = hst_p + 1;
                    if (hst_p >= hst_lnt)
                        hst_p = 0;
                    hst[hst_p].pc = addr1 | HIST_PC;
                    hst[hst_p].inst[0] = ops[0];
                }
                addr1 += 2;
                if (op == OP_EX)
                    storepsw(OPPSW, IRC_EXEC);
                /* Check if RX, RR, SI, RS, SS opcode */
                else {
                    if (op & 0xc0) {
                        if (ReadHalf(addr1, &dest))
                            break;
                        ops[1] = dest;
                        addr1+=2;
                        /* Check if SS */
                        if ((op & 0xc0) == 0xc0) {
                            if(ReadHalf(addr1, &dest))
                               break;
                            ops[2] = dest;
                        }
                        if (hst_lnt) {
                            hst[hst_p].inst[1] = ops[1];
                            hst[hst_p].inst[2] = ops[2];
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
//fprintf(stderr, "FP HD Op=%0x src1=%08x %08x\n\r", op, src2, src2h);
                src2 = (src2 & (EMASK|MSIGN)) | ((src2 & MMASK) >> 1);
                /* Normalize the result if needed */
                if ((src2 & NMASK) == 0) {
                    e1 = (src2 & EMASK) >> 24;
                    src2 &= MSIGN|MMASK; /* Clear off exponent */
                    src2 = (src2 & MSIGN) | (src2 << 4) | (src2h >> 28) & 0xf;
                    src2h <<= 4;
                    e1 --;
                    src2 |= EMASK & (e1 << 24);

                    /* Check if underflow */
                    if (e1 < 0) {
                        if (pmsk & EXPUND)
                           storepsw(OPPSW, IRC_EXPUND);
                        else
                           src2h = src2 = 0;
                    }
                }

                /* Fall through */

                /* Floating Load register */
        case OP_LER:
        case OP_LDR:
        case OP_LE:
        case OP_LD:
//fprintf(stderr, "FP LD Op=%0x src1=%08x %08x\n\r", op, src1, src1h);
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
//fprintf(stderr, "FP LD Op=%0x src1=%08x %08x\n\r", op, src1, src1h);
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
                if (WriteFull(addr1 + 4, src1h))
                    break;
                /* Fall through */

        case OP_STE:
//fprintf(stderr, "FP STD Op=%0x src1=%08x %08x\n\r", op, src1, src1h);
                WriteFull(addr1, src1);
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
                if ((cpu_unit.flags & FEAT_FLOAT) == 0) {
                    storepsw(OPPSW, IRC_OPR);
                    goto supress;
                }
//                a = cnvt_float(src1, src1h);
//                b = cnvt_float(src2, src2h);
//fprintf(stderr, "FP + Op=%0x src1=%08x %08x, src2=%08x %08x %e %e %e\n\r", op, src1, src1h, src2, src2h, a, b, a+b);
                /* Extract numbers and adjust */
                e1 = (src1 & EMASK) >> 24;
                e2 = (src2 & EMASK) >> 24;
                fill = 0;
                if (src1 & MSIGN)
                   fill |= 2;
                if (src2 & MSIGN)
                   fill |= 1;
                src2 &= MMASK;
                src1 &= MMASK;
                temp = e1 - e2;
                if (temp > 0) {
                    /* Shift src2 right if src1 larger expo - expo */
                    while (temp-- != 0) {
                       src2h >>= 4;
                       src2h |= (src2 & 0xf) << 28;
                       src2 >>= 4;
                       e2 ++;
//fprintf(stderr, "FP +2 Op=%0x src1=%08x %08x, src2=%08x %08x %e %e %e\n\r", op, src1, src1h, src2, src2h, a, b, a+b);
                    }
                } else if (temp < 0) {
                    /* Shift src1 right if src2 larger expo - expo */
                    while (temp++ != 0) {
                       src1h >>= 4;
                       src1h |= (src1 & 0xf) << 28;
                       src1 >>= 4;
                       e1 ++;
//fprintf(stderr, "FP +1 Op=%0x src1=%08x %08x, src2=%08x %08x %e %e %e\n\r", op, src1, src1h, src2, src2h, a, b, a+b);
                    }
                }

                /* Exponents should be equal now. */

                /* Create guard digit by shifting left 4 bits */
                src1 = ((src1 & MMASK) << 4) | ((src1h >> 28) & 0xf);
                src2 = ((src2 & MMASK) << 4) | ((src2h >> 28) & 0xf);
                src1h &= XMASK;
                src2h &= XMASK;

                /* Add results */
                if (fill == 2 || fill == 1) {
                     /* Different signs do subtract */
                     src2 ^= XMASK;
                     src2h ^= XMASK;
                     src2h++;
                     if (src2h & CMASK) {
                        src2++;
                        src2h &= XMASK;
                     }
                     desth = src1h + src2h;
                     dest = src1 + src2;
                     if (desth & CMASK) {
                         dest ++;
                         desth &= XMASK;
                     }
                     if (dest & CMASK)
                         dest &= XMASK;
                     else {
                         fill ^= 2;
                         dest ^= XMASK;
                         desth ^= XMASK;
                         desth++;
                         if (desth & CMASK) {
                            dest++;
                            desth &= XMASK;
                         }
                     }
                } else {
                     desth = src1h + src2h;
                     dest = src1 + src2;
                     if (desth & CMASK) {
                         dest ++;
                         desth &= XMASK;
                     }
                }
                /* If src1 not normal shift left + expo */
//fprintf(stderr, "FP +n res=%08x %08x %d\n\r", dest, desth, cc);
                if (dest & CMASK) {
                    desth >>= 4;
                    desth |= (dest & 0xf) << 28;
                    dest >>= 4;
                    e1 ++;
                    if (e1 >= 128) {
                        storepsw(OPPSW, IRC_EXPOVR);
// fprintf(stderr, "FP ov %d\n\r", e1);
                    }
                }

                /* Set condition codes */
                cc = 0;
                if ((desth | dest) != 0)
                     cc = (fill & 2) ? 1 : 2;

//fprintf(stderr, "FP +p res=%08x %08x %d\n\r", dest, desth, cc);
                /* If compare operator, we are done */
                if ((op & 0xF) == 0x9) {
//fprintf(stderr, "FP = %d\n\r", cc);
                   break;               /* All done */
                }

                /* Remove DP Guard bit */
                desth |= (dest & 0xf) << 28;
                dest >>= 4;

                /* Check signifigance exception */
                if (cc == 0 && pmsk & SIGMSK) {
                    storepsw(OPPSW, IRC_EXPOVR);
// fprintf(stderr, "FP Signifigance\n\r");
                    goto fpstore;
                }

                /* Check if we are normalized addition */
                if ((op & 0xE) != 0xE) {
                   if (cc != 0) {   /* Only if non-zero result */
                       while ((dest & NMASK) == 0 && e1 > 0) {
//fprintf(stderr, "FP +n res=%08x %08x %x\n\r", dest, desth, e1);
                          dest = (dest << 4) | ((desth >> 28) & 0xf);
                          desth <<= 4;
                          e1 --;
                       }
                       /* Check if underflow */
                       if (e1 < 0) {
                           if (pmsk & EXPUND) {
                              storepsw(OPPSW, IRC_EXPUND);
// fprintf(stderr, "FP under\n\r");
                           } else {
                              desth = dest = 0;
                              fill = e1 = 0;
                           }
                       }
                   } else {
                       fill = e1 = 0;   /* Return true zero */
                   }
                }

fpstore:
                /* Store result */
                dest |= (e1 << 24) & EMASK;
                if (cc != 0 && fill & 2)
                   dest |= MSIGN;
                if ((op & 0x10) == 0)
                    fpregs[reg1|1] = desth;
                fpregs[reg1] = dest;
//fprintf(stderr, "FP + res=%08x %08x %d %e\n\r", dest, desth, cc, cnvt_float(dest,desth));
                break;

                  /* Multiply */
        case OP_MDR:
        case OP_MER:
        case OP_ME:
        case OP_MD:
                if ((cpu_unit.flags & FEAT_FLOAT) == 0) {
                    storepsw(OPPSW, IRC_OPR);
                    goto supress;
                }

//                a = cnvt_float(src1, src1h);
//                b = cnvt_float(src2, src2h);
//fprintf(stderr, "FP * Op=%0x src1=%08x %08x, src2=%08x %08x %e %e %e\n\r", op, src1, src1h, src2, src2h, a, b, a*b);
                /* Extract numbers and adjust */
                e1 = (src1 & EMASK) >> 24;
                e2 = (src2 & EMASK) >> 24;
                fill = 0;
                if ((src1 & MSIGN) != (src2 & MSIGN))
                   fill = 1;
                src1 &= MMASK;
                src2 &= MMASK;

                /* Pre-nomalize src2 and src1 */
                while ((src2 | src2h) != 0 && (src2 & NMASK) == 0) {
                   src2 = ((src2 & MMASK) << 4) | ((src2h >> 28) & 0xf);
                   src2h <<= 4;
                   e2 --;
                }
                while ((src1 | src1h) != 0 && (src1 & NMASK) == 0) {
                   src1 = ((src1 & MMASK) << 4) | ((src1h >> 28) & 0xf);
                   src1h <<= 4;
                   e1 --;
                }
                e1 = e1 + e2 - 64;

                /* Create guard bit */
                src2 <<= 1;
                if (src2h & MSIGN) {
                    src2 |= 1;
                    src2h & HMASK;
                }
                dest = desth = 0;
                /* Do multiply */
                for (temp = 0; temp < 56; temp++) {
//fprintf(stderr, "FP *s  src1=%08x %08x, src2=%08x %08x dest=%08x %08x %d\n\r", src1, src1h, src2, src2h, dest, desth, temp);
                     /* Add if we need too */
                     if (src1h & 1) {
                         desth += src2h;
                         dest += src2;
                         if (desth & MSIGN) {
                             dest ++;
                             desth &= HMASK;
                         }
                     }
                     /* Shift right by one */
                     src1h >>= 1;
                     if (src1 & 1)
                        src1h |= MSIGN;
                     src1 >>= 1;
                     if (dest & 1)
                        desth |= MSIGN;
                     desth >>= 1;
                     dest >>= 1;
                }
fpnorm:
                /* Remove guard but */
                if (dest & 1) {
                    desth |= MSIGN;
                }
                dest >>= 1;

                /* If src1 not normal shift left + expo */
                if (dest & CMASK) {
                   desth >>= 4;
                   desth |= (dest & 0xf) << 28;
                   dest >>= 4;
                   e1 ++;
                   if (e1 >= 128) {
                       storepsw(OPPSW, IRC_EXPOVR);
// fprintf(stderr, "FP ov\n\r");
                   }
                }
                /* Align the results */
                if ((dest | desth) != 0) {
                    while ((dest & NMASK) == 0 && e1 > 0) {
//fprintf(stderr, "FP *n res=%08x %08x %x\n\r", dest, desth, e1);
                        dest = (dest << 4) | ((desth >> 28) & 0xf);
                        desth <<= 4;
                        e1 --;
                    }
                    /* Check if underflow */
                    if (e1 < 0) {
                        if (pmsk & EXPUND) {
                            storepsw(OPPSW, IRC_EXPUND);
// fprintf(stderr, "FP un\n\r");
                        } else {
                            desth = dest = 0;
                            fill = e1 = 0;
                        }
                    }
                } else
                    fill = 0;
                dest |= (e1 << 24) & EMASK;
                if (fill)
                   dest |= MSIGN;
                if ((op & 0x10) == 0)
                    fpregs[reg1|1] = desth;
                fpregs[reg1] = dest;
//fprintf(stderr, "FP * res=%08x %08x %d %e\n\r", dest, desth, cc, cnvt_float(dest,desth));
                break;

                  /* Divide */
        case OP_DER:
        case OP_DDR:
        case OP_DD:
        case OP_DE:
                if ((cpu_unit.flags & FEAT_FLOAT) == 0) {
                    storepsw(OPPSW, IRC_OPR);
                    goto supress;
                }
//                a = cnvt_float(src1, src1h);
//                b = cnvt_float(src2, src2h);
//fprintf(stderr, "FP / Op=%0x src1=%08x %08x, src2=%08x %08x %e %e %e\n\r", op, src1, src1h, src2, src2h, a, b, a/b);

                /* Extract numbers and adjust */
                e1 = (src1 & EMASK) >> 24;
                e2 = (src2 & EMASK) >> 24;
                fill = 0;
                fill = 0;
                if ((src1 & MSIGN) != (src2 & MSIGN))
                   fill = 1;
                src1 &= MMASK;
                src2 &= MMASK;
                if ((src2 | src2h) == 0) {
                    storepsw(OPPSW, IRC_FPDIV);
                    break;
                }

                /* Pre-nomalize src2 and src1 */
                while ((src2 | src2h) != 0 && (src2 & NMASK) == 0) {
                    src2 = ((src2 & MMASK) << 4) | ((src2h >> 28) & 0xf);
                    src2h <<= 4;
                    e2 --;
                }
                while ((src1 | src1h) != 0 && (src1 & NMASK) == 0) {
                    src1 = ((src1 & MMASK) << 4) | ((src1h >> 28) & 0xf);
                    src1h <<= 4;
                    e1 --;
                }
                e1 = e1 - e2 + 64;

                /* Shift numbers up 4 bits so as not to lose precision below */
                src2 = ((src2 & MMASK) << 4) | ((src2h >> 28) & 0xf);
                src2h <<= 4;
                src1 = ((src1 & MMASK) << 4) | ((src1h >> 28) & 0xf);
                src1h <<= 4;

                /* Expand for DP math */
                src2 <<= 1;
                if (src2h & MSIGN) {
                    src2 |= 1;
                    src2h & HMASK;
                }
                src1 <<= 1;
                if (src1h & MSIGN) {
                    src1 |= 1;
                    src1h & HMASK;
                }

                /* Check if we need to adjust divsor it larger then dividend */
                if (src1 > src2) {
//fprintf(stderr, "FP /o  src1=%08x %08x, src2=%08x %08x dest=%08x %08x\n\r", src1, src1h, src2, src2h, dest, desth);
                    src1h >>= 4;
                    src1h |= (src1 & 0xf) <<27;
                    src1 >>= 4;
                    e1++;
                }

                /* Change sign of src2 so we can add */
                src2 ^= HMASK /*0x3fffffff*/;
                src2h ^= HMASK;
                src2h++;
                if (src2h & MSIGN) {
                    src2++;
                    src2h &= HMASK;
                }
                dest = desth = 0;
                /* Do divide */
                for (temp = 56; temp > 0; temp--) {
                     uint32    tlow, thigh;

//fprintf(stderr, "FP /s  src1=%08x %08x, src2=%08x %08x dest=%08x %08x %d\n\r", src1, src1h, src2, src2h, dest, desth, temp);
                     /* Shift left by one */
                     src1 <<= 1;
                     src1h <<= 1;
                     if (src1h & MSIGN) {
                         src1 |= 1;
                         src1h & HMASK;
                     }
                     /* Subtract remainder to dividend */
                     thigh = src1h + src2h;
                     tlow = src1 + src2;
                     if (thigh & MSIGN) {
                         tlow ++;
                         thigh &= HMASK;
                     }

                     /* Shift quotent left one bit */
                     dest <<= 1;
                     desth <<= 1;
                     if (desth & MSIGN) {
                         dest |= 1;
                         desth &= HMASK;
                     }

                     /* If remainder larger then divisor replace */
                     if ((tlow & MSIGN/*0x40000000*/) != 0) {
                         src1 = tlow;
                         src1h = thigh;
                         desth |= 1;
                     }
                }
                goto fpnorm;

                  /* Decimal operations */
        case OP_CP:    /* 1001 */
        case OP_SP:    /* 1011 */
        case OP_ZAP:   /* 1000 */
        case OP_AP:    /* 1010 */
                if ((cpu_unit.flags & FEAT_DEC) == 0) {
                    storepsw(OPPSW, IRC_OPR);
                    goto supress;
                }
                dec_add(op, addr1, reg1, addr2, reg & 0xf);
                break;

        case OP_MP:
                if ((cpu_unit.flags & FEAT_DEC) == 0) {
                    storepsw(OPPSW, IRC_OPR);
                    goto supress;
                }
                dec_mul(op, addr1, reg1, addr2, reg & 0xf);
                break;

        case OP_DP:
                if ((cpu_unit.flags & FEAT_DEC) == 0) {
                    storepsw(OPPSW, IRC_OPR);
                    goto supress;
                }
                dec_div(op, addr1, reg1, addr2, reg & 0xf);
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
                storepsw(OPPSW, IRC_OPR);
                goto supress;
        }

        if (hst_lnt) {
             hst[hst_p].dest = dest;
             hst[hst_p].cc = cc;
        }

        if ((op & 0xA0) == 0x20) {
        sim_debug(DEBUG_INST, &cpu_dev,
                  "GR00=%08x GR01=%08x GR02=%08x GR03=%08x\n",
                   regs[0], regs[1], regs[2], regs[3]);
        sim_debug(DEBUG_INST, &cpu_dev,
                  "GR04=%08x GR05=%08x GR06=%08x GR07=%08x\n",
                   regs[4], regs[5], regs[6], regs[7]);
        sim_debug(DEBUG_INST, &cpu_dev,
                  "GR08=%08x GR09=%08x GR10=%08x GR11=%08x\n",
                   regs[8], regs[9], regs[10], regs[11]);
        sim_debug(DEBUG_INST, &cpu_dev,
                  "GR12=%08x GR13=%08x GR14=%08x GR15=%08x\n",
                   regs[12], regs[13], regs[14], regs[15]);
        if ((op & 0xA0) == 0x20) {
            sim_debug(DEBUG_INST, &cpu_dev,
                  "FP00=%08x FP01=%08x FP02=%08x FP03=%08x\n",
                   fpregs[0], fpregs[1], fpregs[2], fpregs[3]);
            sim_debug(DEBUG_INST, &cpu_dev,
                  "FP04=%08x FP05=%08x FP06=%08x FP07=%08x\n",
                   fpregs[4], fpregs[5], fpregs[6], fpregs[7]);
        }
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
lpsw:
             sysmsk = (src1 >> 24) & 0xff;
             st_key = (src1 >> 16) & 0xf0;
             flags = (src1 >> 16) & 0xf;
             irqaddr = 0;
             pmsk = (src2 >> 24) & 0xf;
             cc = (src2 >> 28) & 0x3;
             PC = src2 & AMASK;
             irq_pend = 1;
             sim_debug(DEBUG_INST, &cpu_dev, "PSW=%08x %08x  ",
                   (((uint32)sysmsk) << 24) | (((uint32)st_key) << 16) |
                   (((uint32)flags) << 16) | ((uint32)irqcode),
                   (((uint32)ilc) << 30) | (((uint32)cc) << 28) |
                   (((uint32)pmsk) << 24) | PC);
        }
        sim_interval--;
    }
}

/*
 * Load a decimal number into temp storage.
 * return 1 if error.
 * return 0 if ok.
 */
int dec_load(uint8 *data, uint32 addr, uint len, int *sign)
{
    uint32   temp;
    int      i, j;
    int      err = 0;

    addr += len;     /* Point to end */
    memset(data, 0, 32);
    j = 0;
    /* Read it into temp backwards */
    for (i = 0; i <= len; i++) {
        int t;
        if (ReadByte(addr, &temp))
            return 1;
        t = temp & 0xf;
        if (j != 0 && t > 0x9)
            err = 1;
        data[j++] = t;
        t = (temp >> 4) & 0xf;
        if (t > 0x9)
            err = 1;
        data[j++] = t;
        addr--;
    }
    /* Check if sign valid and return it */
    if (data[0] == 0xB || data[0] == 0xD)
        *sign = 1;
    else if (data[0] < 0xA)
        err = 1;
    else
        *sign = 0;
    if (err) {
        storepsw(OPPSW, IRC_DATA);
        return 1;
    }
    return 0;
}

/*
 * Store a decimal number into memory storage.
 * return 1 if error.
 * return 0 if ok.
 */
int dec_store(uint8 *data, uint32 addr, uint len, int sign)
{
    uint32   temp;
    int      i, j;
    addr += len;

    if (sign) {
        data[0] = ((flags & ASCII)? 0xb : 0xd);
    } else {
        data[0] = ((flags & ASCII)? 0xa : 0xc);
    }
    j = 0;
    for (i = 0; i <= len; i++) {
        temp = data[j++] & 0xf;
        temp |= (data[j++] & 0xf) << 4;
        if (WriteByte(addr, temp))
            return 1;
        addr--;
    }
    return 0;
}


/*
 * Handle AP, SP, CP and ZAP instructions.
 *
 * ZAP = F8    00
 * CP  = F9    01
 * AP  = FA    10
 * SP  = FB    11
 */
void
dec_add(int op, uint32 addr1, uint8 len1, uint32 addr2, uint8 len2)
{
    uint8    a[32];
    uint8    b[32];
    int      i;
    uint8    acc;
    uint8    cy;
    int      len = (int)len1;
    int      sa, sb;
    int      addsub = 0;
    int      zero;
    int      ov = 0;

    if (len2 > len1)
        len = (int)len2;
    /* Always load second operand */
    if (dec_load(b, addr2, (int)len2, &sb))
        return;

    len = 2*(len+1);
    /* On all but ZAP load first operand */
    if ((op & 3) != 0) {
        if (dec_load(a, addr1, (int)len1, &sa))
            return;
    } else {
        /* For ZAP just clear A */
        memset(a, 0, 32);
        sa = 0;
    }
    if (op & 1) {
        if (sa == sb)
            addsub = 1;
    } else {
        if (sa != sb)
            addsub = 1;
    }
    cy = addsub;
    zero = 1;
    /* Add numbers together */
    for (i = 1; i < len; i++) {
        acc = a[i] + ((addsub)? (0x9 - b[i]):b[i]) + cy;
        if (acc > 0x9)
           acc += 0x6;
        a[i] = acc & 0xf;
        cy = (acc >> 4) & 0xf;
        if ((acc & 0xf) != 0)
            zero = 0;
    }
    if (cy) {
        if (addsub)
           sa = !sa;
        else
           ov = 1;
    } else {
        if (addsub) {
           /* We need to recomplent the result */
           sa = !sa;
           cy = 1;
           zero = 1;
           for (i = 1; i < len; i++) {
                acc = (0x9 - a[i]) + cy;
                if (acc > 0x9)
                   acc += 0x6;
                a[i] = acc & 0xf;
                cy = (acc >> 4) & 0xf;
                if ((acc & 0xf) != 0)
                    zero = 0;
           }
        }
    }
    if (zero && !ov)
       sa = 0;
    cc = 0;
    if (zero)  /* Really not zero */
       cc = (sa)? 2: 1;
    if ((op & 3) == 1) {
        if (!zero && !ov) {
           /* Start at len1 and go to len2 and see if any non-zero digits */
           for (i = (len1+1)*2; i < len; i++) {
               if (a[i] != 0) {
                  ov = 1;
                  break;
               }
           }
        }
        dec_store(a, addr1, (int)len1, sa);
        if (ov)
            cc = 3;
        if (ov && pmsk & DECOVR)
            storepsw(OPPSW, IRC_DECOVR);
    }
}

void
dec_mul(int op, uint32 addr1, uint8 len1, uint32 addr2, uint8 len2)
{
    uint8    a[32];
    uint8    b[32];
    int      i;
    int      j;
    int      k;
    uint8    acc;
    uint8    cy;
    int      sa, sb;
    int      mul;
    int      len;

    if (len2 > 7 || len2 >= len1) {
fprintf(stderr, "Spec DecMP  len=%x len=%x\n\r", len1, len2);
        storepsw(OPPSW, IRC_SPEC);
        return;
    }
    if (dec_load(b, addr2, (int)len2, &sb))
        return;
    if (dec_load(a, addr1, (int)len1, &sa))
        return;
    len = (int)len1;
    len1 = (len1 + 1) * 2;
    len2 = (len2 + 1) * 2;
    /* Verify that we have len2 zeros at start of a */
    for (i = len1; i > len2; i--) {
        if (a[i] != 0) {
            storepsw(OPPSW, IRC_DATA);
            return;
        }
    }
    sa ^= sb;     /* Compute sign */
    /* Start at end and work backwards */
    for (j = len2; j > 1; j--) {
        mul = a[j];
        a[j] = 0;
        while(mul != 0) {
            /* Add numbers together */
            cy = 0;
            for (i = j, k = 1; i <= len1; i++, k--) {
                acc = a[i] + b[k] + cy;
                if (acc > 0x9)
                   acc += 0x6;
                a[i] = acc & 0xf;
                cy = (acc >> 4) & 0xf;
            }
            mul--;
        }
    }
    dec_store(a, addr1, len, sa);
}

void
dec_div(int op, uint32 addr1, uint8 len1, uint32 addr2, uint8 len2)
{
    uint8    a[32];
    uint8    b[32];
    uint8    c[32];
    int      i;
    int      j;
    int      k;
    uint8    acc;
    uint8    cy;
    int      sa, sb;
    int      q;
    int      len;

    if (len2 > 7 || len2 >= len1) {
fprintf(stderr, "Spec DecDP  len=%x len=%x\n\r", len1, len2);
        storepsw(OPPSW, IRC_SPEC);
        return;
    }
    if (dec_load(b, addr2, (int)len2, &sb))
       return;
    if (dec_load(a, addr1, (int)len1, &sa))
       return;
    /* Add numbers together */
    for (i = 1; i <= len; i++) {
        acc = a[i] + ((op)? (0x9 - b[i]):b[i]) + cy;
        if (acc > 0x9)
           acc += 0x6;
        a[i] = acc & 0xf;
        cy = (acc >> 4) & 0xf;
    }
    sb ^= sa;     /* Compute sign */
    for (j = len2; j > 1; j--) {
        q = 0;
        do {
            /* Subtract divisor */
            cy = 1;
            for (i = j, k = 1; i <= len1; i++, k--) {
                 c[i] = a[i];   /* Save if we divide too far */
                 acc = a[i] + (0x9 - b[k]) + cy;
                 if (acc > 0x9)
                     acc += 0x6;
                 a[i] = acc & 0xf;
                 cy = (acc >> 4) & 0xf;
            }
            if (cy) {
                a[i] = q;
                memcpy(a, c, 32);  /* Restore the result */
                if (q > 9) {
                    storepsw(OPPSW, IRC_DECDIV);
                    return;
                }
            } else {
                q++;
            }
        } while(cy == 0);
    }
    /* Set sign of quotient */
    if (sb) {
        a[len2+1] = ((flags & ASCII)? 0xb : 0xd);
    } else {
        a[len2+1] = ((flags & ASCII)? 0xa : 0xc);
    }
    dec_store(a, addr1, len, sa);
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


/* Interval timer routines */
t_stat
rtc_srv(UNIT * uptr)
{
    if (cpu_unit.flags & FEAT_TIMER) {
        int32 t;
        t = sim_rtcn_calb (rtc_tps, TMR_RTC);
        sim_activate_after(uptr, 1000000/rtc_tps);
        if ((M[0x50>>2] & 0xfffffc00) == 0)  {
            sim_debug(DEBUG_INST, &cpu_dev, "TIMER IRQ %08x\n\r", M[0x50>>2]);
            interval_irq = 1;
        }
        M[0x50>>2] -= 0x40;
        sim_debug(DEBUG_INST, &cpu_dev, "TIMER = %08x\n", M[0x50>>2]);
    }
    return SCPE_OK;
}


/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;
uint32 byte;
uint32 offset = 8 * (3 - (addr & 0x3));

if (vptr == NULL)
    return SCPE_ARG;
/* Ignore high order bits */
addr &= AMASK;
if (addr >= MEMSIZE)
    return SCPE_NXM;
addr >>= 2;
byte = M[addr] >> offset;
byte &= 0xff;
*vptr = byte;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
uint32 addr = (uint32) exta;
uint32 offset = 8 * (3 - (addr & 0x3));
uint32 word;
uint32 mask;

/* Ignore high order bits */
addr &= AMASK;
if (addr >= MEMSIZE)
    return SCPE_NXM;
addr >>= 2;
mask = 0xff << offset;
word = M[addr];
word &= ~mask;
word |= (val & 0xff) << offset;
M[addr] = word;
fprintf(stderr, "dep %08x %02x %08x\n\r", (addr <<2), val, word);
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

