/* Ridge32_cpu.c: Ridge 32 cpu simulator.

   Copyright (c) 2019, Richard Cornwell

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

#include "ridge32_defs.h"                            /* simulator defns */


#define UNIT_V_MSIZE (UNIT_V_UF + 0)             /* dummy mask */
#define UNIT_MSIZE   (0xf << UNIT_V_MSIZE)
#define MEMAMOUNT(x) (x << UNIT_V_MSIZE)
#define UNIT_V_LDENA (UNIT_V_UF + 4) 
#define UNIT_LDENA   (0x1 << UNIT_V_LDENA)

#define TMR_RTC      0

#define HIST_MAX     5000000
#define HIST_MIN     64
#define HIST_PC      0x1000000
#define HIST_TRAP    0x2000000
#define HIST_USER    0x4000000

uint32       *M = NULL;
uint32       regs[16];             /* CPU Registers */
uint32       PC;                   /* Program counter */
uint32       sregs[16];            /* Special registers */
uint32       tlb[32];              /* Translation look aside buffer */
uint32       vrt[32];              /* VRT address for Modify */
uint8        user;                 /* Set when in user mode */
uint8        wait;                 /* Wait for interrupt */
uint8        ext_irq;              /* External interrupt pending */
uint32       trapwd;               /* Current trap word */
uint16       trapcode;             /* Current trap code + 0x8000 indicating trap */
int          timer1_irq = 0;       /* Timer1 irq */
int          timer2_irq = 0;       /* Timer2 irq */
int          boot_sw;              /* Boot device */

#define TRAP      0x8000
#define DATAAL    0x8100           /* Data alignment trap */
#define ILLINS    0x8101           /* Illegal instruction trap */
#define DBLPRY    0x8102           /* Double bit parity error code fetch - not on simulator */
#define DBLEXC    0x8103           /* Double bit parity error execute - not on simulator */
#define PGFLT     0x8104           /* Page fault */
#define KERVOL    0x8105           /* Kernel Violation */
#define CHKTRP    0x8106           /* Check trap */
#define TRPWD     0x8107           /* General trap */
#define EXTIRQ    0x8108           /* External Interrupt */
#define SW0IRQ    0x8109           /* Switch 0 Interrupt */
#define PWRFAL    0x810A           /* Power fail - not on simulator */
#define PWRGLT    0x810B           /* Power glitch - not on simulator */
#define TIMER1    0x810C           /* Timer 1 interrupt */
#define TIMER2    0x810D           /* Timer 2 interrupt */

#define INTOVR    0x8000           /* Integer overflow */
#define DIVZER    0x4000           /* Divide by zero */
#define FPOVER    0x2000           /* Floating point overflow */
#define FPUNDR    0x1000           /* Floating point underflow */
#define FPDVZR    0x0800           /* Floating point divide by zero. */

#define FMASK     0xffffffff
#define AMASK     0x00ffffff
#define MSIGN     0x80000000
#define WMASK     0x00fffffe

int hst_lnt;
int hst_p;
struct InstHistory
{
   t_addr        pc;
   t_addr        addr;
   uint32        src1;
   uint32        src1h;
   uint32        src2;
   uint32        dest;
   t_value       inst[6];
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

/* Interval timer option */
t_stat       rtc_srv(UNIT * uptr);
t_stat       rtc_reset(DEVICE * dptr);
int32        rtc_tps = 1000;
int32        tmxr_poll;


/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = { UDATA (&rtc_srv, UNIT_IDLE|UNIT_BINK|UNIT_FIX, MAXMEMSIZE), 1000 };

REG cpu_reg[] = {
    { HRDATA (PC, PC, 24) },
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
    { HRDATA (SR0, sregs[00], 32) },
    { HRDATA (SR1, sregs[01], 32) },
    { HRDATA (SR2, sregs[02], 32) },
    { HRDATA (SR3, sregs[03], 32) },
    { HRDATA (SR4, sregs[04], 32) },
    { HRDATA (SR5, sregs[05], 32) },
    { HRDATA (SR6, sregs[06], 32) },
    { HRDATA (SR7, sregs[07], 32) },
    { HRDATA (SR8, sregs[010], 32) },
    { HRDATA (SR9, sregs[011], 32) },
    { HRDATA (SR10, sregs[012], 32) },
    { HRDATA (SR11, sregs[013], 32) },
    { HRDATA (SR12, sregs[014], 32) },
    { HRDATA (SR13, sregs[015], 32) },
    { HRDATA (SR14, sregs[016], 32) },
    { HRDATA (SR15, sregs[017], 32) },
    { BRDATA (SR, sregs, 16, 32, 16) },
    { HRDATA (USER, user, 1) },
    { NULL }
    };

MTAB cpu_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
    { UNIT_MSIZE, MEMAMOUNT(1),   "1M",  "1M", &cpu_set_size },
    { UNIT_MSIZE, MEMAMOUNT(2),   "2M",  "2M", &cpu_set_size },
    { UNIT_MSIZE, MEMAMOUNT(4),   "4M",  "4M", &cpu_set_size },
    { UNIT_MSIZE, MEMAMOUNT(8),   "8M",  "8M", &cpu_set_size },
    { UNIT_MSIZE, MEMAMOUNT(12), "12M", "12M", &cpu_set_size },
    { UNIT_MSIZE, MEMAMOUNT(16), "16M", "16M", &cpu_set_size },
    { UNIT_LDENA, 0, NULL, "NOLOAD", NULL, NULL, NULL, "Turns off load enable switch"},
    { UNIT_LDENA, UNIT_LDENA, "LOAD", "LOAD", NULL, NULL, NULL, "Turns on load enable switch"},
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

/*
 * Translate an address from virtual to physical.
 */
int  TransAddr(t_addr va, t_addr *pa, uint8 code, uint wr) {

     if (user) {
         /* Tlb has virtual address + 12 bit page */
         /* VRT has valid bit, modify, vrt address / 4 */
         uint32      page = va >> 12;
         int         entry = (page & 0xf) + (code ? 0x10 : 0);
         uint32      seg = ((code) ? sregs[8] : sregs[9]) & 0xFFFF;
         uint32      mat = (seg << 16) | (va >> 16);
         uint32      addr;
         /* If not the same, walk through VRT to find correct page */
         if ((vrt[entry] & 0x7000) != 0x7000 || tlb[entry] != mat) {
             uint32 ntag = (((seg + page) & sregs[13]) << 3);
             uint32 tag;
             uint32 link;
             do {
                 tag = (ntag + sregs[12]) >> 2;
                 addr = M[tag++]; 
                 link = M[tag];
                 ntag = (link >> 16);
            sim_debug(DEBUG_EXP, &cpu_dev,
                      "Load trans: %08x %08x -> %08x %08x %08x\n", seg, va, tag, addr, link);
             } while (addr != mat && ntag != 0);
             /* Did we find entry? */
             if (addr != mat || (link & 0x7000) != 0x7000) {
                 /* Nope this is a fault */
                 sregs[1] = -1;
                 sregs[2] = seg;
                 sregs[3] = va;
                 trapcode = PGFLT;
            sim_debug(DEBUG_EXP, &cpu_dev,
                      "Page fault: %08x %08x -> %08x %08x %08x\n", seg, va, tag, addr, link);
                 return 1;
             }
             /* Update reference and modify bits */
            sim_debug(DEBUG_EXP, &cpu_dev,
                      "Load Tlb: %08x %08x -> %08x %08x %08x\n", seg, va, tag, addr, link);
             link |= 0x8000;
             if (wr)
                 link |= 0x800;
             /* Update the tlb entry */
             M[tag] = link;
             tag -= sregs[12] >> 2;   /* Subtract offset. */
             tag <<= 16;              /* Move to upper half */
             tag |= link & 0xFFFF;    /* Copy link over */
             vrt[entry] = tag;        /* Save for furture */
             tlb[entry] = mat;
             addr = link;
         } else {
             /* Update modify bit if not already set */
             addr = vrt[entry];  /* Tag and flag bits */
             if (wr && (addr & 0x800) == 0) {
                 uint32  link = (vrt[entry] >> 16) & 0xffff;
                 link += sregs[12] >> 2;
                 M[link] |= 0x800;
                 vrt[entry] |= 0x800;
            sim_debug(DEBUG_EXP, &cpu_dev,
                      "Mod Tlb: %08x %08x -> %08x %08x\n", seg, va, link, vrt[entry]);
             }
         }
         *pa = ((addr & 0x7ff) << 12) | (va & 0xfff);
     } else {
         *pa = va & 0x7fffff;
     }
     return 0;
}

/*
 * Read a full word from memory, checking protection
 * and alignment restrictions. Return 1 if failure, 0 if
 * success.
 */
int  ReadFull(t_addr addr, uint32 *data, uint8 code) {
     uint32     temp;
     t_addr     pa;

     /* Check alignment */
     if ((addr & 3) != 0) {
         trapcode = DATAAL;
         sregs[2] = code ? sregs[8] : sregs[9];
         sregs[3] = addr;
         return 1;
     }

     /* Validate address */
     if (TransAddr(addr, &pa, code, 0))
         return 1;

     /* Check against memory size */
     if (pa >= MEMSIZE)
         return 0;

     /* Actual read */
     pa >>= 2;
     *data = M[pa];
     return 0;
}

int WriteFull(t_addr addr, uint32 data) {
     int        offset;
     t_addr     pa;

     /* Check alignment */
     if ((addr & 3) != 0) {
         trapcode = DATAAL;
         sregs[2] = sregs[9];
         sregs[3] = addr;
         return 1;
     }

     /* Validate address */
     if (TransAddr(addr, &pa, 0, 1))
         return 1;

     /* Check against memory size */
     if (pa >= MEMSIZE)
         return 0;

     /* Actual write */
     pa >>= 2;
     M[pa] = data;
     return 0;
}

int WriteHalf(t_addr addr, uint32 data) {
     uint32     mask;
     t_addr     pa;
     int        offset;

     /* Check alignment */
     if ((addr & 1) != 0) {
         trapcode = DATAAL;
         sregs[2] = sregs[9];
         sregs[3] = addr;
         return 1;
     }

     /* Validate address */
     if (TransAddr(addr, &pa, 0, 1))
         return 1;

     /* Check against memory size */
     if (pa >= MEMSIZE)
         return 0;

     /* Do actual write. */
     data &= 0xffff;
     if (pa & 0x2) {
         mask = 0xffff0000;
     } else {
         data <<= 16;
         mask = 0xffff;
     }
     pa >>= 2;
     M[pa] &= mask;
     M[pa] |= data;
     return 0;
}

int WriteByte(t_addr addr, uint32 data) {
     uint32     mask;
     t_addr     pa;
     int        offset;

     /* Validate address */
     if (TransAddr(addr, &pa, 0, 1))
         return 1;

     /* Check against memory size */
     if (pa >= MEMSIZE)
         return 0;

     /* Do actual write. */
     offset = 8 * (3 - (pa & 0x3));
     pa >>= 2;
     mask = 0xff;
     data &= mask;
     data <<= offset;
     mask <<= offset;
     M[pa] &= ~mask;
     M[pa] |= data;
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
    uint32          dest;          /* Results of operation */
    uint32          desth;
    uint32          nPC;           /* Next PC */
    t_addr          disp;          /* Computed address for memory ref */
    uint8           op;            /* Current opcode */
    uint8           reg1;          /* First register */
    uint8           reg2;          /* Second register */
    uint16          irq;
    int             temp, temp2;
    uint16          ops[3];
    int             code_seg;

    reason = SCPE_OK;
    chan_set_devs();

    while (reason == SCPE_OK) {

wait_loop:

        if (sim_interval <= 0) {
            reason = sim_process_event();
            if (reason != SCPE_OK)
               return reason;
        }

        if (sim_brk_summ && sim_brk_test(PC, SWMASK('E'))) {
           return STOP_IBKPT;
        }


        /* If in user mode and no PCB, just wait */
        if (user && (sregs[11] == 1 || sregs[14] == 1)) {
           sim_interval--;
           if (sregs[11] != 1 && (timer1_irq || timer2_irq))
              goto trap;
           if (trapcode == 0 && ext_irq == 0)
              goto wait_loop;
           sim_debug(DEBUG_CMD, &cpu_dev, "Exit wait %4x %d\n", trapcode, ext_irq);
        }

trap:

        if (trapcode) {
            uint32 ccb = sregs[11] >> 2;
            if (user) {
                sregs[0] = 1;
                sregs[15] = PC;
            } else {
                sregs[0] = PC;
            }
            PC = M[ccb + (trapcode & 0x1FF)];
            sim_debug(DEBUG_TRAP, &cpu_dev, "Trap %04x\n", trapcode & 0x1FF);
            if (hst_lnt) {
                 hst_p = hst_p + 1;
                 if (hst_p >= hst_lnt)
                     hst_p = 0;
                 hst[hst_p].pc = PC | HIST_TRAP;
                 hst[hst_p].addr = trapcode << 2;
            }
            trapcode = 0;
            user = 0;
        } else if (user && io_rd(&sregs[0])) {
            uint32 ccb = sregs[11] >> 2;
            sregs[15] = PC;
            if (sregs[11] != 1)
                PC = M[ccb + (EXTIRQ & 0x1FF)];
            else
                PC = 0x3e000;
            sim_debug(DEBUG_TRAP, &cpu_dev, "IRQ %08x\n", sregs[0]);
            if (hst_lnt) {
                 hst_p = hst_p + 1;
                 if (hst_p >= hst_lnt)
                     hst_p = 0;
                 hst[hst_p].pc = PC | HIST_TRAP;
                 hst[hst_p].addr = EXTIRQ << 2;
            }
            user = 0;
        } else if (timer1_irq) {
            uint32 ccb = sregs[11] >> 2;
            if (user) {
                sregs[0] = 1;
                sregs[15] = PC;
                PC = M[ccb + (TIMER1 & 0x1FF)];
                sim_debug(DEBUG_TRAP, &cpu_dev, "TIMER1\n");
                if (hst_lnt) {
                     hst_p = hst_p + 1;
                     if (hst_p >= hst_lnt)
                         hst_p = 0;
                     hst[hst_p].pc = PC | HIST_TRAP;
                     hst[hst_p].addr = TIMER1 << 2;
                }
                user = 0;
            }
            timer1_irq = 0;
        } else if (timer2_irq) {
            uint32 ccb = sregs[11] >> 2;
            if (user) {
                sregs[0] = 1;
                sregs[15] = PC;
                PC = M[ccb + (TIMER2 & 0x1FF)];
                sim_debug(DEBUG_TRAP, &cpu_dev, "TIMER2\n");
                if (hst_lnt) {
                     hst_p = hst_p + 1;
                     if (hst_p >= hst_lnt)
                         hst_p = 0;
                     hst[hst_p].pc = PC | HIST_TRAP;
                     hst[hst_p].addr = TIMER2 << 2;
                }
                user = 0;
            }
            timer2_irq = 0;
        }
            
        if (hst_lnt) {
             hst_p = hst_p + 1;
             if (hst_p >= hst_lnt)
                hst_p = 0;
             hst[hst_p].pc = PC | HIST_PC | (user?HIST_USER:0);
        }

        /* Fetch the operator and possible displacement */
        if (ReadFull(PC & ~3, &dest, 1)) {
            goto trap;
        }
        nPC = PC + 2;
        /* Check which half */
        if (PC & 0x2) {
           op = (dest >> 8) & 0xff;
           reg1 = (dest >> 4) & 0xf;
           reg2 = dest & 0xf;
           /* Check if need displacment */
           if (op & 0x80) {
               /* In next word */
               if (ReadFull((PC + 2) & ~3, &disp, 1)) {
                   goto trap;
               }
               /* Check if short displacement */
               if ((op & 0x10) == 0) 
                   /* Move high half to lower */
                   disp = (disp >> 16) & 0xffff;
           }
        } else {
           op = (dest >> 24) & 0xff;
           reg1 = (dest >> 20) & 0xf;
           reg2 = (dest >> 16) & 0xf;
           /* Check if long half of displacment */
           if ((op & 0x90) == 0x90) {
               /* Rest in high part of next word */
               if (ReadFull((PC + 4) & ~3, &disp, 1)) {
                   goto trap;
               }
               /* Merge current lower and next upper */
               disp >>= 16;
               disp |= (dest & 0xffff) << 16;
           } else {
               disp = dest & 0xffff;
           }
        }

        /* Print instruction trace in debug log */
        if (sim_deb && (cpu_dev.dctrl & DEBUG_INST)) {
            t_value    inst[8];
            inst[0] = op;
            inst[1] = (reg1 << 4) | reg2;
            sim_debug(DEBUG_INST, &cpu_dev,
                      "R00=%08x R01=%08x R02=%08x R03=%08x\n",
                       regs[0], regs[1], regs[2], regs[3]);
            sim_debug(DEBUG_INST, &cpu_dev,
                      "R04=%08x R05=%08x R06=%08x R07=%08x\n",
                       regs[4], regs[5], regs[6], regs[7]);
            sim_debug(DEBUG_INST, &cpu_dev,
                      "R08=%08x R09=%08x R10=%08x R11=%08x\n",
                       regs[8], regs[9], regs[10], regs[11]);
            sim_debug(DEBUG_INST, &cpu_dev,
                      "R12=%08x R13=%08x R14=%08x R15=%08x\n",
                       regs[12], regs[13], regs[14], regs[15]);
            if (user == 0) {
                sim_debug(DEBUG_INST, &cpu_dev,
                          "SR00=%08x SR01=%08x SR02=%08x SR03=%08x\n",
                           sregs[0], sregs[1], sregs[2], sregs[3]);
                sim_debug(DEBUG_INST, &cpu_dev,
                          "SR04=%08x SR05=%08x SR06=%08x SR07=%08x\n",
                           sregs[4], sregs[5], sregs[6], sregs[7]);
                sim_debug(DEBUG_INST, &cpu_dev,
                          "SR08=%08x SR09=%08x SR10=%08x SR11=%08x\n",
                           sregs[8], sregs[9], sregs[10], sregs[11]);
                sim_debug(DEBUG_INST, &cpu_dev,
                          "SR12=%08x SR13=%08x SR14=%08x SR15=%08x\n",
                           sregs[12], sregs[13], sregs[14], sregs[15]);
            }
            sim_debug(DEBUG_INST, &cpu_dev, "PC=%06x %c INST=%02x%02x ", PC,
                    (user) ? 'u': 'k', inst[0], inst[1]);
            if (op & 0x80) {
                if (op & 0x10) {
                    sim_debug(DEBUG_INST, &cpu_dev, "%08x", disp);
                    inst[2] = (disp >> 24) & 0xff;
                    inst[3] = (disp >> 16) & 0xff;
                    inst[4] = (disp >> 8) & 0xff;
                    inst[5] = disp & 0xff;
                } else {
                    sim_debug(DEBUG_INST, &cpu_dev, "%04x    ", disp & 0xffff);
                    inst[2] = (disp >> 8) & 0xff;
                    inst[3] = disp & 0xff;
                }
           } else {
                sim_debug(DEBUG_INST, &cpu_dev, "        ");
           }
           sim_debug(DEBUG_INST, &cpu_dev, "    ");
           fprint_inst(sim_deb, PC & WMASK, inst);
           sim_debug(DEBUG_INST, &cpu_dev, "\n");
        }

        /* Check if memory reference */
        if (op & 0x80) {
            /* Move high half to lower and extend */
            if (op & 0x10) {
                nPC = PC + 6;
            } else {
                if (disp & 0x8000)
                    disp |= 0xffff0000;
                nPC = PC + 4;
            }
        }

        if (hst_lnt) {
            hst[hst_p].inst[0] = op;
            hst[hst_p].inst[1] = (reg1 << 4) | reg2;
            if (op & 0x80) {
                 if (op & 0x10) {
                     hst[hst_p].inst[2] = (disp >> 24) & 0xff;
                     hst[hst_p].inst[3] = (disp >> 16) & 0xff;
                     hst[hst_p].inst[4] = (disp >> 8) & 0xff;
                     hst[hst_p].inst[5] = disp & 0xff;
                 } else {
                     hst[hst_p].inst[2] = (disp >> 8) & 0xff;
                     hst[hst_p].inst[3] = disp & 0xff;
                 }
            }
        }

        /* Load the two registers */
        src1 = regs[reg1];
        if ((op & 0xF0) == 0x10 || (op & 0xF0) == 0x70)
            src2 = reg2;
        else
            src2 = regs[reg2];

        if (op & 0x80) {   /* Memory reference */
            code_seg = (op & 0x60) == 0x60;

            /* Check if indexed load or store or laddr */
            if (op > 0xA0 && (op & 0x81) == 0x81) 
                disp += regs[reg2];

            /* Check if code segment access */
            if (((op ^ (op << 1)) & 0x40) == 0)
                disp += PC;
        }

        if (hst_lnt) {
            hst[hst_p].src1 = src1;
            hst[hst_p].src2 = src2; 
            hst[hst_p].addr = disp; 
        }

        /* Preform opcode */
        switch (op) {
        case OP_MOVEI:
        case OP_MOVE:
                     regs[reg1] = src2;
                     break;

        case OP_NOP:
                     break;

        case OP_NEG:
                     if (src2 == MSIGN && trapwd & INTOVR) {
                         sregs[2] = 16;
                         trapcode = TRPWD;
                     }
                     regs[reg1] = -src2;
                     break;

        case OP_SUBI:
        case OP_SUB:
                     src2 = -src2;
                     /* Fall through */
        case OP_ADDI:
        case OP_ADD:
                     dest = src1 + src2;
                     src1 = (src1 & MSIGN) != 0;
                     src2 = (src2 & MSIGN) != 0;
                     if ((src1 && src2 && (dest & MSIGN) == 0) ||
                         (!src1 && !src2 && (dest & MSIGN) != 0)) {
                         if (trapwd & INTOVR) {
                             sregs[2] = 16;
                             trapcode = TRPWD;
                         }
                     }
                     regs[reg1] = dest;
                     break;

        case OP_ESUB:
                     src2 = -src2;
                     /* Fall through */
        case OP_EADD:
                     temp = regs[0] & 1;
                     regs[0] = 0;
                     dest = src1 + src2;
                     src1 = (src1 & MSIGN) != 0;
                     src2 = (src2 & MSIGN) != 0;
                     if ((src1 && src2 && (dest & MSIGN) == 0) ||
                         (!src1 && !src2 && (dest & MSIGN) != 0)) {
                         regs[0] = 2;
                     }
                     if (src1 < src2) {
                         regs[0] |= 1;
                     }
                     if (temp) {
                         if (dest == FMASK)
                             regs[0] = 3;
                         dest++;
                     }
                     regs[reg1] = dest;
                     break;

        case OP_NOTI:
        case OP_NOT:
                     regs[reg1] = ~src2;
                     break;
        
        case OP_OR:
                     regs[reg1] = src1 | src2;
                     break;

        case OP_XOR:
                     regs[reg1] = src1 ^ src2;
                     break;
        
        case OP_ANDI:
        case OP_AND:
                     regs[reg1] = src1 & src2;
                     break;

        case OP_EMPY:
        case OP_MPYI:
        case OP_MPY:
                     reg2 = 0;
                     if (src1 & MSIGN) {
                         reg2 = 1;
                         src1 = -src1;
                     }
                     if (src2 & MSIGN) {
                         reg2 ^= 1;
                         src2 = -src2;
                     }
                     dest = desth = 0;
                     if (src1 != 0 && src2 != 0) {
                         for (temp = 32; temp > 0; temp--) {
                              if (src1 & 1)
                                 desth += src2;
                              src1 >>= 1;
                              dest >>= 1;
                              if (desth & 1)
                                  dest |= MSIGN;
                              desth >>= 1;
                         }
                     }
                     if (op != OP_EMPY && desth != 0) {
                         if (trapwd & INTOVR) {
                             sregs[2] = 16;
                             trapcode = TRPWD;
                         }
                     }   
                     if (reg2) {
                         desth ^= FMASK;
                         dest ^= FMASK;
                         if (dest == FMASK)
                             desth ++;
                         dest++;
                     }
                     regs[reg1] = dest;
                     if (op == OP_EMPY)
                         regs[(reg1+1) & 0xf] = desth;
                     break;

        case OP_EDIV:
                     src1h = regs[(reg1+1) & 0xf]; 
                     if (hst_lnt) {
                         hst[hst_p].src1h = src1h;
                     }
                     if (src2 == 0) {
                         if (trapwd & DIVZER) {
                             sregs[2] = 17;
                             trapcode = TRPWD;
                         }
                         break;
                     }
                     dest = 0;
                     for (temp = 0; temp < 32; temp++) {
                         /* Shift left by one */
                         src1 <<= 1;
                         if (src1h & MSIGN)
                             src1 |= 1;
                         src1h <<= 1;
                         /* Subtract remainder from divisor */
                         desth = src1 - src2;
                    
                         /* Shift quotent left one bit */
                         dest <<= 1;
                    
                         /* If remainder larger then divisor replace */
                         if ((desth & MSIGN) == 0) {
                             src1 = desth;
                             dest |= 1;
                         }
                     }
                     /* Check for overflow */
                     if ((dest & MSIGN) != 0) {
                         if (trapwd & INTOVR) {
                             sregs[2] = 16;
                             trapcode = TRPWD;
                         }
                     }
                     regs[reg1] = dest;
                     regs[reg2] = desth;
                     break;

        case OP_DIV:
        case OP_REM:
                     if (src2 == 0) {
                         if (trapwd & DIVZER) {
                             sregs[2] = 17;
                             trapcode = TRPWD;
                         }
                         break;
                     }
                     reg2 = 0;
                     if (src1 & MSIGN) {
                         reg2 = 3;
                         src1 = -src1;
                     }
                     src1h = src1;
                     src1 = 0;
                     if (src2 & MSIGN) {
                         reg2 ^= 1;
                         src2 = -src2;
                     }
                     dest = 0;
                     for (temp = 0; temp < 32; temp++) {
                         /* Shift left by one */
                         src1 <<= 1;
                         if (src1h & MSIGN)
                             src1 |= 1;
                         src1h <<= 1;
                         /* Subtract remainder to dividend */
                         desth = src1 - src2;
                   
                         /* Shift quotent left one bit */
                         dest <<= 1;
                   
                         /* If remainder larger then divisor replace */
                         if ((desth & MSIGN) == 0) {
                             src1 = desth;
                             dest |= 1;
                         }
                     }
                     /* Check for overflow */
                     if ((dest & MSIGN) != 0) {
                         if (trapwd & INTOVR) {
                             sregs[2] = 16;
                             trapcode = TRPWD;
                         }
                         goto trap;
                     }
                     if (op & 1) { /* REM */
                         dest = (reg2 & 2) ? -src1 : src1;
                     } else if (reg2 & 1) /* DIV */
                         dest = -dest;
                     regs[reg1] = dest;
                     break;

        case OP_CBIT:
        case OP_SBIT:
                     dest = (MSIGN >> (src2 & 037)); 
                     if (src2 & 040) {
                         if (op & 1) 
                             regs[(reg1+1)& 0xf] |= dest;
                         else
                             regs[(reg1+1)&0xf] &= ~dest;
                     } else {  
                         if (op & 1) 
                             regs[reg1] |= dest;
                         else
                             regs[reg1] &= ~dest;
                     }
                     break;

        case OP_TBIT:
                     dest = (MSIGN >> (src2 & 037)); 
                     if (src2 & 040) {
                         dest &= regs[(reg1+1)& 0xf];
                     } else {  
                         dest &= regs[reg1];
                     }
                     regs[reg1] = dest != 0;
                     break;

        case OP_CHK:
                     if ((int32)src1 > (int32)src2) {
                          sregs[2] = reg1;
                          sregs[3] = reg2;
                          trapcode = CHKTRP;
                     }
                     break;

        case OP_CHKI:
                     if (!((src1 & MSIGN) == 0 && src1 <= (int32)src2)) {
                          sregs[2] = reg1;
                          sregs[3] = reg2;
                          trapcode = CHKTRP;
                     }
                     break;

        case OP_LCOMP:
                     if ((int32)src1 < (int32)src2)
                         dest = -1;
                     else if (src1 != src2)
                         dest = 1;
                     else
                         dest = 0;
                     regs[reg1] = dest;
                     break;

        case OP_DCOMP:
                     if (src1 == src2) {
                         src1 = regs[(reg1+1) & 0xf]; 
                         src2 = regs[(reg2+1) & 0xf]; 
                         if ((uint32)src1 < (uint32)src2)
                             dest = FMASK;
                         else if (src1 != src2)
                             dest = 1;
                         else
                              dest = 0;
                     } else if ((int32)src1 < (int32)src2)
                         dest = FMASK;
                     else
                         dest = 1;
                     regs[reg1] = dest;
                     break;

        case OP_LSLI:
        case OP_LSL:
                     dest = src1 << (src2 & 037);
                     regs[reg1] = dest;
                     break;

        case OP_LSRI:
        case OP_LSR:
                     dest = src1 >> (src2 & 037);
                     regs[reg1] = dest;
                     break;

        case OP_ASRI:
        case OP_ASR:
                    dest = (int32)src1 >> (src2 & 037);
                    regs[reg1] = dest;
                    break;

        case OP_ASLI:
        case OP_ASL:
                    src2h = src1 & MSIGN;
                    dest = src1 &  ~MSIGN;
                    src2 &= 037;
                    while (src2 > 0) {
                        dest <<= 1;
                        if ((dest & MSIGN) != src2h && trapwd & INTOVR) {
                              sregs[2] = 16;
                              trapcode = TRPWD;
                        }
                        src2--;
                    }
                    regs[reg1] = dest;
                    break;

        case OP_DLSRI:
        case OP_DLSR:
                   src2 &= 077;
                   while(src2 > 0) {
                       src1h >>= 1;
                       if (src1 & 1)
                           src1h |= MSIGN;
                       src1 >>= 1;
                       src2--;
                   }
                   regs[reg1] = src1;
                   regs[(reg1 + 1) & 0xf] = src1h;
                   break;
   
        case OP_DLSLI:
        case OP_DLSL:
                   src2 &= 077;
                   while(src2 > 0) {
                       src1 <<= 1;
                       if (src1h & MSIGN)
                           src1 |= 1;
                       src1h <<= 1;
                       src2--;
                   }
                   regs[reg1] = src1;
                   regs[(reg1 + 1) & 0xf] = src1h;
                   break;

        case OP_CSLI:
        case OP_CSL:
                   src2 &= 037;
                   dest = ((src1 << src2) | (src1 >> (32 - src2)));
                   regs[reg1] = dest;
                   break;

        case OP_SEH:
                   dest = regs[reg2] &  0xffff;
                   if (dest & 0x8000)
                        dest |= 0xffff0000;
                   regs[reg1] = dest;
                   break;

        case OP_SEB:
                   dest = regs[reg2] &  0xff;
                   if (dest & 0x80)
                        dest |= 0xffffff00;
                   regs[reg1] = dest;
                   break;
 
        case 0x54:    /* TESTI > */
                   src2 = reg2;
                    /* Fall through */
        case 0x50:   /* TEST > */
                   regs[reg1] = ((int32)src1) > ((int32)src2);
                   break;

        case 0x56:   /* TESTI = */
                   src2 = reg2;
                    /* Fall through */
        case 0x52:   /* TEST = */
                   regs[reg1] = ((int32)src1) == ((int32)src2);
                   break;

        case 0x55:   /* TESTI < */
                   src2 = reg2;
                    /* Fall through */
        case 0x51:   /* TEST < */
                   regs[reg1] = ((int32)src1) < ((int32)src2);
                   break;

        case 0x5C:   /* TESTI <= */
                   src2 = reg2;
                    /* Fall through */
        case 0x58:   /* TEST <= */
                   regs[reg1] = ((int32)src1) <= ((int32)src2);
                   break;

        case 0x5e:   /* TESTI  <> */
                   src2 = reg2;
                    /* Fall through */
        case 0x5a:   /* TEST  <> */
                   regs[reg1] = ((int32)src1) != ((int32)src2);
                   break;

        case 0x5d:   /* TESTI  >= */
                   src2 = reg2;
                    /* Fall through */
        case 0x59:   /* TEST  >= */
                   regs[reg1] = ((int32)src1) >= ((int32)src2);
                   break;

        case OP_FIXT:   /* Fix with truncate to integer */
        case OP_FIXR:   /* Fix with round to integer */
        case OP_RNEG:   /* Negate real */
        case OP_RADD:   /* Real add */
        case OP_RSUB:   /* Real subtract */
        case OP_RMPY:   /* Real product */
        case OP_RDIV:   /* Real divide */
        case OP_FLOAT:  /* Make integer real */
        case OP_MAKERD: /* Convert real to double */
        case OP_RCOMP:  /* Compare two reals */
        case OP_DFIXT:  /* Fix with trancate to integer */
        case OP_DFIXR:  /* Fix with round to integer */
        case OP_DRNEG:  /* Negate real */
        case OP_DRADD:  /* Double add */
        case OP_DRSUB:  /* Double subtract */
        case OP_DRMPY:  /* Double product */
        case OP_DRDIV:  /* Double divide */
        case OP_MAKEDR: /* Convert double to real */
        case OP_DFLOAT: /* Convert integer to double */
        case OP_DRCOMP: /* Compare two doubles */
                   break;

        case OP_TRAP:
                   if ((MSIGN >> reg2) & trapwd) {
                       trapcode = TRPWD;
                       sregs[2] = reg2;
                   }
                   break;

        case OP_SUS:
                   if (user) {
                        goto priv_trap;
                   } else {
                        if ((sregs[14] & 0x1) == 0) {
                            uint32  pcb = sregs[14] >> 2;
                            do {
                                M[pcb + reg1] = regs[reg1];
                                reg1++;
                            } while (reg1 <= reg2);
                            M[pcb + 16] = sregs[15];
                        }   
                   }
                   break;

        case OP_LUS:
                   if (user) {
                        goto priv_trap;
                   } else {
                        if ((sregs[14] & 0x1) == 0) {
                            uint32  pcb = sregs[14] >> 2;
                            do {
                                regs[reg1] = M[pcb + reg1];
                                reg1++;
                            } while (reg1 <= reg2);
                            sregs[8] = (M[pcb + 17] >> 16) & 0xFFFF;
                            sregs[9] =  M[pcb + 17] & 0xFFFF;
                            sregs[10] = M[pcb + 19];
                            sregs[15] = M[pcb + 16];
                            for (reg1 = 0;
                                 reg1 < (sizeof(tlb)/sizeof(uint32));
                                 reg1++)
                                vrt[reg1] = 0;
                        }   
                   }
                   break;

        case OP_RUM:
                   if (user) {
                       goto priv_trap;
                   } else {
                       nPC = sregs[15];
                       user = 1;
                   }
                   break;

        case OP_LDREGS:
                   if (user) {
                       goto priv_trap;
                   } else {
                       if ((sregs[14] & 0x1) == 0) {
                           uint32  pcb = sregs[14] >> 2;
                           do {
                               regs[reg1] = M[pcb + reg1];
                               reg1++;
                           } while (reg1 <= reg2);
                       }   
                   }
                   break;

        case OP_TRANS:
        case OP_DIRT:
                   if (user) {
                        goto priv_trap;
                   } else {
                        uint32 seg  = regs[reg2];                       /* Segment */
                        uint32 page = regs[(reg2 + 1) & 0xf] >> 12;     /* Address = page */
                        uint32 mat  = (seg << 16) | (page >> 4);        /* Match word */
                        uint32 na  = (((seg + page) & sregs[13]) << 3); /* Next address */
                        uint32 a;                                       /* Current address */
                        uint32 link;                                    /* Link word */
                        uint32 e;                                       /* Entry */
                        src1 = regs[(reg2 + 1) & 0xf];
                        do {
                            a = (na + sregs[12]) >> 2;
                            link = M[a++]; 
                            e = M[a];
                            na = (e >> 16);
            sim_debug(DEBUG_EXP, &cpu_dev,
                      "Load trans: %08x %08x -> %08x %08x %08x\n", seg, regs[(reg2 + 1) & 0xf], a, link, e);
                        } while (link != mat && na != 0);
                        /* Did we find entry? */
                        if (link != mat || (e & 0x7000) != 0x7000) {
                            regs[reg1] = FMASK;
                        } else {
                            /* Update reference and modify bits */
                            e |= 0x8000;
                            if ((op & 1) != 0)
                                e |= 0x800;
                            M[a] = e;
                            regs[reg1] = ((e & 0x7ff) << 12) | (src1 & 0xfff);
                        }
                   }
                   break;

        case OP_MOVESR:
                   if (user) {
                        goto priv_trap;
                   } else {
                        sregs[reg1] = regs[reg2];
                   }
                   break;

        case OP_MOVERS:
                   if (user) {
                        goto priv_trap;
                   } else {
                        regs[reg1] = sregs[reg2];
                   }
                   break;

        case OP_MAINT:
                   /* Reg2 determines actual opcode */
                   if (user && (sregs[10] & 1) == 0) {
priv_trap:              sregs[1] = op;
                        sregs[2] = reg1;
                        sregs[3] = reg2;
                        trapcode = KERVOL;
                        break;
                   }
                   switch (reg2) {
                   case 0:    /* ELOGR */
                         /* 1 = Load enable */
                         /* 2 = Secondary boot device */
                         /* 4 = External interrupt */
                         dest = 0x8000 | (ext_irq << 4);
                         if (cpu_unit.flags & UNIT_LDENA)
                             dest |= 1;
                         if (boot_sw != 0)
                             dest |= 2;
                         regs[reg1] = dest;
                         break;

                   case 1:    /* ELOGW */
                         break;

                   case 5:    /* TWRITED */
                         break;

                   case 6:    /* FLUSH */
                         for (reg1 = 0;
                              reg1 < (sizeof(tlb)/sizeof(uint32)); 
                              reg1++)
                              vrt[reg1] = 0;
                         break;

                   case 7:    /* TRAPEXIT */
                         if (user)
                             goto priv_trap;
                         nPC = sregs[0];
                         break;

                   case 8:    /* ITEST */
                         if (user)
                             goto priv_trap;
                         regs[reg1] = !io_rd(&regs[(reg1 + 1) & 0xf]);
                         break;

                   case 10:   /* MACHINEID */
                         regs[reg1] = 0;
                         break;

                   case 11:   /* Version */
                   case 12:   /* CREG */
                   case 13:   /* RDLOG */
                        fprintf(stderr, "Maint %d %d %08x\n\r", reg1, reg2, src1);
                         regs[reg1] = 0;
                         break;

                   default:
                         break;
                   }
                   break;

        case OP_READ:
                    if (user && (sregs[10] & 1) == 0) {
                         goto priv_trap;
                    } else {
                         src2 = regs[reg2];
                         dest = io_read(src2, &regs[(reg1+1) & 0xf]);
                         regs[reg1] = dest;
                    }
                    break;

        case OP_WRITE:
                    if (user && (sregs[10] & 1) == 0) {
                         goto priv_trap;
                    } else {
                         src2 = regs[reg2];
                         dest = io_write(src2, regs[reg1]);
                         regs[reg1] = dest;
                    }
                    break;

        case OP_KCALL:
                    if (user) {
                        trapcode = TRAP | (reg1 << 4) | reg2;
                        PC = nPC & WMASK;   /* Low order bit can't be set */
                    } else {
                        trapcode =KERVOL;
                        sregs[1] = op;
                        sregs[2] = reg1;
                        sregs[3] = reg2;
                    }
                    break;

        case OP_RET:
                    regs[reg1] = nPC;
                    nPC = src2 & WMASK;
                    break;

        case OP_CALLR:
                    regs[reg1] = nPC;
                    nPC = (PC + src2) & WMASK;
                    break;

        case 0x93:    /* CALL */
        case 0x83:    /* CALL */
                    regs[reg1] = nPC;
                    nPC = disp & WMASK;
                    break;

        case 0x87:    /* LOOP */
        case 0x97:    /* LOOP */
                    dest = src1 + reg2;
                    if (dest & MSIGN)
                       nPC = disp & WMASK;
                    regs[reg1] = dest;
                    break;

        case 0x8b:    /* BR */
        case 0x9b:    /* BR */
                    nPC = disp & WMASK;
                    break;

        case 0x80:    /* BR   > */
        case 0x90:    /* BR   > */
                    if (((int32)src1) > ((int32)src2))
                        nPC = disp & WMASK;
                    break;

        case 0x84:    /* BRI  > */
        case 0x94:    /* BRI  > */
                    if (((int32)src1) > ((int32)reg2))
                        nPC = disp & WMASK;
                    break;

        case 0x82:    /* BR   = */
        case 0x92:    /* BR   = */
                    if (((int32)src1) == ((int32)src2))
                        nPC = disp & WMASK;
                    break;

        case 0x86:    /* BRI  = */
        case 0x96:    /* BRI  = */
                    if (((int32)src1) == ((int32)reg2))
                        nPC = disp & WMASK;
                    break;

        case 0x85:    /* BRI  < */
        case 0x95:    /* BRI  < */
                    if (((int32)src1) < ((int32)reg2))
                        nPC = disp & WMASK;
                    break;

        case 0x88:    /* BR   <= */
        case 0x98:    /* BR   <= */
                    if (((int32)src1) <= ((int32)src2))
                        nPC = disp & WMASK;
                    break;

        case 0x8c:    /* BRI  <= */
        case 0x9c:    /* BRI  <= */
                    if (((int32)src1) <= ((int32)reg2))
                        nPC = disp & WMASK;
                    break;

        case 0x8a:    /* BR   <> */
        case 0x9a:    /* BR   <> */
                    if (((int32)src1) != ((int32)src2))
                        nPC = disp & WMASK;
                    break;

        case 0x8e:    /* BRI  <> */
        case 0x9e:    /* BRI  <> */
                    if (((int32)src1) != ((int32)reg2))
                        nPC = disp & WMASK;
                    break;
        case 0x8d:    /* BRI  >= */
        case 0x9d:    /* BRI  >= */
                    if (((int32)src1) >= ((int32)reg2))
                        nPC = disp & WMASK;
                    break;

        case 0xa0:    /* StoreB */
        case 0xa1:    /* StoreB */
        case 0xb0:    /* StoreB */
        case 0xb1:    /* StoreB */
                   if (WriteByte(disp, src1))
                        break;
                   break;

        case 0xa2:    /* StoreH */
        case 0xa3:    /* StoreH */
        case 0xb2:    /* StoreH */
        case 0xb3:    /* StoreH */
                   /* Check if we handle unaligned access */
                   if (WriteHalf(disp, src1))
                        break;
                   break;

        case 0xa6:    /* Store */
        case 0xa7:    /* Store */
        case 0xb6:    /* Store */
        case 0xb7:    /* Store */
                   if (WriteFull(disp, src1))
                        break;
                   break;

        case 0xa8:    /* StoreD */
        case 0xa9:    /* StoreD */
        case 0xb8:    /* StoreD */
        case 0xb9:    /* StoreD */
                   /* Check if we handle unaligned access */
                   src1h = regs[(reg1 + 1) & 0xf];
                   if (hst_lnt) {
                       hst[hst_p].src2 = src1h; 
                   }
                   if (WriteFull(disp, src1))
                        break;
                   if (WriteFull(disp+4, src1h))
                        break;
                   break;

        case 0xe0:    /* LoadB */
        case 0xe1:    /* LoadB */
        case 0xf0:    /* LoadB */
        case 0xf1:    /* LoadB */
        case 0xc0:    /* LoadB */
        case 0xc1:    /* LoadB */
        case 0xd0:    /* LoadB */
        case 0xd1:    /* LoadB */
                   dest = 0;
                   if (ReadFull(disp & ~(3), &dest, code_seg))
                       break;
                   dest >>= 8 * (3 - (disp & 0x3));
                   regs[reg1] = dest & 0xff;
                   break;

        case 0xe2:    /* LoadH */
        case 0xe3:    /* LoadH */
        case 0xf2:    /* LoadH */
        case 0xf3:    /* LoadH */
        case 0xc2:    /* LoadH */
        case 0xc3:    /* LoadH */
        case 0xd2:    /* LoadH */
        case 0xd3:    /* LoadH */
                   /* Check if we handle unaligned access */
                   dest = 0;
                   if ((disp & 1) != 0) {
                       trapcode = DATAAL;
                       sregs[2] = code_seg ? sregs[8] : sregs[9];
                       sregs[3] = disp;
                       break;
                   }
                   if (ReadFull(disp & ~(3), &dest, code_seg))
                       break;
                   if ((disp & 2) == 0)
                       dest >>= 16;
                   regs[reg1] = dest & 0xffff;
                   break;

        case 0xe6:    /* Load */
        case 0xe7:    /* Load */
        case 0xf6:    /* Load */
        case 0xf7:    /* Load */
        case 0xc6:    /* Load */
        case 0xc7:    /* Load */
        case 0xd6:    /* Load */
        case 0xd7:    /* Load */
                   dest = 0;
                   if (ReadFull(disp & ~(0x3), &dest, code_seg))
                       break;
                   regs[reg1] = dest;
                   break;

        case 0xe8:    /* LoadD */
        case 0xe9:    /* LoadD */
        case 0xf8:    /* LoadD */
        case 0xf9:    /* LoadD */
        case 0xc8:    /* LoadD */
        case 0xc9:    /* LoadD */
        case 0xd8:    /* LoadD */
        case 0xd9:    /* LoadD */
                   dest = 0;
                   desth = 0;
                   if (ReadFull(disp, &dest, code_seg))
                       break;
                   if (ReadFull(disp+4, &desth, code_seg))
                       break;
                   regs[reg1] = dest;
                   regs[(reg1+1) &0xf] = desth;
                   if (hst_lnt) {
                       hst[hst_p].src1 = dest; 
                       hst[hst_p].src2 = desth; 
                   }
                   break;

        case 0xee:    /* Laddr */
        case 0xef:    /* Laddr */
        case 0xfe:    /* Laddr */
        case 0xff:    /* Laddr */
        case 0xce:    /* Laddr */
        case 0xcf:    /* Laddr */
        case 0xde:    /* Laddr */
        case 0xdf:    /* Laddr */
                   regs[reg1] = disp;
                   break;

        default:
ill_inst:
                   trapcode = ILLINS;
                   sregs[1] = op;
                   sregs[2] = reg1;
                   if (op & 0x80)
                      sregs[3] = disp;
                   else
                      sregs[3] = reg2;
        }

        if (trapcode == 0)
            PC = nPC & WMASK;   /* Low order bit can't be set */
        if (hst_lnt) {
            hst[hst_p].dest = regs[reg1];
        }
        sim_interval--;

    }
    PC = nPC & WMASK;   /* Low order bit can't be set */
}


/* Reset */

t_stat
cpu_reset (DEVICE *dptr)
{
    if (M == NULL) {                        /* first time init? */
        sim_brk_types = sim_brk_dflt = SWMASK ('E');
        M = (uint32 *) calloc (((uint32) MEMSIZE) >> 2, sizeof (uint32));
        if (M == NULL)
            return SCPE_MEM;
    }
    sregs[2] = MEMSIZE;
    sregs[4] = 0xff;
    sregs[11] = 1;
    sregs[14] = 1;
    trapcode = 0;
    timer1_irq = timer2_irq = 0;
    ext_irq = 0;
    sim_rtcn_init_unit (&cpu_unit, cpu_unit.wait, TMR_RTC);
    sim_activate(&cpu_unit, cpu_unit.wait);
    return SCPE_OK;
}

void
cpu_boot (int sw)
{
    sregs[2] = MEMSIZE;
    sregs[4] = 0xff;
    sregs[11] = 1;
    sregs[14] = 1;
    timer1_irq = timer2_irq = 0;
    user = 1;
    boot_sw = sw;
    trapcode = 0;
    ext_irq = 0;
}


/* Interval timer routines */
t_stat
rtc_srv(UNIT * uptr)
{
   int32 t;
   t = sim_rtcn_calb (rtc_tps, TMR_RTC);
   sim_activate_after(uptr, 1000000/rtc_tps);
   tmxr_poll = t/2;
   if ((sregs[11] & 1) == 0) {
       uint32 ccb = sregs[11] >> 2;
       uint32 s;
       if ((sregs[14] & 1) == 0) {
           M[(sregs[14] + 0x80) >> 2] ++;
       } else {
           M[ccb + 0x10F]++;
       }
       M[ccb + 0x110]--;
       M[ccb + 0x111]--;
       if ((M[ccb + 0x110] & MSIGN) != 0)   /* Timer 1 */
           timer1_irq = 1;
       if ((M[ccb + 0x111] & MSIGN) != 0)   /* Timer 2 */
           timer2_irq = 1;
       s = M[ccb + 0x113] + 1000000; /* Bump by 1 ns */
       if (s < M[ccb + 0x113])
           M[ccb + 0x112] ++;
       M[ccb + 0x113] = s;
       sim_debug(DEBUG_EXP, &cpu_dev, "Timer: %08x t1=%08x t2=%08x d=%08x %08x\n",
            M[ccb + 0x10F], M[ccb + 0x110], M[ccb + 0x111], M[ccb + 0x112], M[ccb+0x113]);
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
val = 1024 * 1024 * val;
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
cpu_unit.flags |= (val / (1024 * 1024)) << UNIT_V_MSIZE;
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
    fprintf(st, "PC      A1     D1       D2       RESULT\n\n");
    for (k = 0; k < lnt; k++) { /* print specified */
        h = &hst[(++di) % hst_lnt];     /* entry pointer */
        if (h->pc & HIST_PC) {   /* instruction? */
            int i;
            fprintf(st, "%06x%c %06x %08x %08x %08x %02x%02x ",
                      h->pc & AMASK, (h->pc & HIST_USER) ? 'v':' ',
                      h->addr & AMASK,
                      h->src1, h->src2, h->dest, h->inst[0], h->inst[1]);
            if ((h->inst[0] & 0x80) != 0)
                  fprintf(st, "%02x%02x ", h->inst[2], h->inst[3]);
            else
                  fprintf(st, "     ");
            if ((h->inst[0] & 0x90) == 0x90)
                  fprintf(st, "%02x%02x ", h->inst[4], h->inst[5]);
            else
                  fprintf(st, "     ");
            fprintf(st, "  ");
            fprint_inst(st, h->pc & AMASK, h->inst);
            fputc('\n', st);    /* end line */
        }                       /* end else instruction */
        else if (h->pc & HIST_TRAP) {   /* Trap */
            int i;
            fprintf(st, "%06x %06x\n",
                      h->pc & AMASK, h->addr & AMASK);
        }                       /* end else trap */
    }                           /* end for */
    return SCPE_OK;
}


t_stat
cpu_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "Ridge 32 CPU\n\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *
cpu_description (DEVICE *dptr)
{
       return "Ridge 32 CPU";
}

