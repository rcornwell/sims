/* sel32_cpu.c: Sel 32 CPU simulator

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

#include "sel32_defs.h"

#define UNIT_V_MODEL    (UNIT_V_UF + 0)
#define UNIT_MODEL      (7 << UNIT_V_MODEL)
#define MODEL(x)        (x << UNIT_V_MODEL)
#define UNIT_V_MSIZE    (UNIT_V_MODEL + 3)
#define UNIT_MSIZE      (0x1F << UNIT_V_MSIZE)
#define MEMAMOUNT(x)    (x << UNIT_V_MSIZE)

#define MODEL_55        0                     /* 512K Mode Only */
#define MODEL_75        1                     /* Extended */
#define MODEL_27        2                     /* */
#define MODEL_67        3                     /* */
#define MODEL_87        4                     /* */
#define MODEL_97        5                     /* */
#define MODEL_V6        6                     /* V6 CPU */
#define MODEL_V9        7                     /* V9 CPU */

#define TMR_RTC         1

#define HIST_MIN        64
#define HIST_MAX        10000
#define HIST_PC         0x80000000

int              cpu_index;                   /* Current CPU running */
uint32           M[MAXMEMSIZE] = { 0 };       /* Memory */
uint32           GPR[8];                      /* General Purpose Registers */
uint32           BR[8];                       /* Base registers */
uint32           PC;                          /* Program counter */
uint8            CC:                          /* Condition code register */
uint32           SPAD[256];                   /* Scratch pad memory */
#define CC1      0x40
#define CC2      0x20
#define CC3      0x10
#define CC4      0x08
#define AEXP     0x01                         /* Arithmetic exception PSD 1 bit 7 */
                                              /* Held in CC */

uint8            modes;                       /* Operating modes */
#define PRIV     0x80                         /* Privileged mode  PSD 1 bit 0 */
#define EXTD     0x04                         /* Extended Addressing PSD 1 bit 5 */
#define BASE     0x02                         /* Base Mode PSD 1 bit 6 */
#define MAP      0x40                         /* Map mode, PSD 2 bit 0 */
#define RET      0x20                         /* Retain current map, PSD 2 bit 15 */

uint8            irq_flags;                   /* Interrupt control flags PSD 2 bits 16&17 */
uint16           cpix;                        /* Current Process index */
uint16           bpix;                        /* Base process index */

struct InstHistory
{
    uint32   pc;
    uint32   inst;
    uint32   ea;
    uint64   dest;
    uint64   source;
    uint64   res;
    uint8    cc;
};

t_stat              cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr,
                           int32 sw);
t_stat              cpu_dep(t_value val, t_addr addr, UNIT * uptr,
                            int32 sw);
t_stat              cpu_reset(DEVICE * dptr);
t_stat              cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              cpu_show_hist(FILE * st, UNIT * uptr, int32 val,
                                  CONST void *desc);
t_stat              cpu_set_hist(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
uint32              cpu_cmd(UNIT * uptr, uint16 cmd, uint16 dev);
t_stat              cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *cpu_description (DEVICE *dptr);

/* History information */
int32               hst_p = 0;                  /* History pointer */
int32               hst_lnt = 0;                /* History length */
struct InstHistory *hst = NULL;                 /* History stack */

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT                cpu_unit =
    { UDATA(rtc_srv, UNIT_BINK | MODEL(MODEL_27) | MEMAMOUNT(0),
            MAXMEMSIZE ), 120 };

REG                 cpu_reg[] = {
    {ORDATAD(IC, IC, 15, "Instruction Counter"), REG_FIT},
    {ORDATAD(AC, AC, 38, "Accumulator"), REG_FIT, 0},
    {ORDATAD(MQ, MQ, 36, "Multiplier Quotent"), REG_FIT, 0},
    {BRDATAD(XR, XR, 8, 15, 8, "Index registers"), REG_FIT},
    {ORDATAD(ID, ID, 36, "Indicator Register")},
#ifdef EXTRA_SL
    {ORDATAD(SL, SL, 8, "Sense Lights"), REG_FIT},
#else
    {ORDATAD(SL, SL, 4, "Sense Lights"), REG_FIT},
#endif
#ifdef EXTRA_SW
    {ORDATAD(SW, SW, 12, "Sense Switches"), REG_FIT},
#else
    {ORDATAD(SW, SW, 6, "Sense Switches"), REG_FIT},
#endif
#endif
    {ORDATAD(KEYS, KEYS, 36, "Console Key Register"), REG_FIT},
    {ORDATAD(MTM, MTM, 1, "Multi Index registers"), REG_FIT},
    {ORDATAD(TM, TM, 1, "Trap mode"), REG_FIT},
    {ORDATAD(STM, STM, 1, "Select trap mode"), REG_FIT},
    {ORDATAD(CTM, CTM, 1, "Copy Trap Mode"), REG_FIT},
    {ORDATAD(FTM, FTM, 1, "Floating trap mode"), REG_FIT},
    {ORDATAD(NMODE, nmode, 1, "Storage null mode"), REG_FIT},
    {ORDATAD(ACOVF, acoflag, 1, "AC Overflow Flag"), REG_FIT},
    {ORDATAD(MQOVF, mqoflag, 1, "MQ Overflow Flag"), REG_FIT},
    {ORDATAD(IOC, iocheck, 1, "I/O Check flag"), REG_FIT},
    {ORDATAD(DVC, dcheck, 1, "Divide Check flag"), REG_FIT},
    {ORDATAD(RELOC, relocaddr, 14, "Relocation offset"), REG_FIT},
    {ORDATAD(BASE, baseaddr, 14, "Relocation base"), REG_FIT},
    {ORDATAD(LIMIT, limitaddr, 14, "Relocation limit"), REG_FIT},
    {ORDATAD(ENB, ioflags, 36, "I/O Trap Flags"), REG_FIT},
    {FLDATA(INST_BASE, bcore, 0), REG_FIT},
    {FLDATA(DATA_BASE, bcore, 1), REG_FIT},
    {NULL}
};

MTAB                cpu_mod[] = {
    {UNIT_MODEL, MODEL(MODEL_55), "32/55", "32/55", NULL, NULL, NULL, "Concept 32/55"},
    {UNIT_MODEL, MODEL(MODEL_75), "32/75", "32/75", NULL, NULL, NULL, "Concept 32/75"},
    {UNIT_MODEL, MODEL(MODEL_27), "32/27", "32/27", NULL, NULL, NULL, "Concept 32/27"},
    {UNIT_MODEL, MODEL(MODEL_67), "32/67", "32/67", NULL, NULL, NULL, "Concept 32/67"},
    {UNIT_MODEL, MODEL(MODEL_87), "32/87", "32/87", NULL, NULL, NULL, "Concept 32/87"},
    {UNIT_MODEL, MODEL(MODEL_97), "32/97", "32/97", NULL, NULL, NULL, "Concept 32/97"},
    {UNIT_MODEL, MODEL(MODEL_V6), "V6", "V6", NULL, NULL, NULL, "Concept V6"},
    {UNIT_MODEL, MODEL(MODEL_V9), "V9", "V9", NULL, NULL, NULL, "Concept V9"},
    {UNIT_MSIZE, MEMAMOUNT(0), "128K", "128K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(1), "256K", "256K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(2), "512K", "512K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(3),   "1M",   "1M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(4),   "2M",   "2M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(5),   "3M",   "3M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(6),   "4M",   "4M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(7),   "8M",   "8M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(8),  "16M",  "16M", &cpu_set_size},
    {MTAB_XTD | MTAB_VDV | MTAB_NMO | MTAB_SHP, 0, "HISTORY", "HISTORY",
     &cpu_set_hist, &cpu_show_hist},
    {0}
};

DEVICE              cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 24, 1, 8, 32,
    &cpu_ex, &cpu_dep, &cpu_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description
};

#define IND     0x00100000
#define F_BIT   0x00080000

/* CPU Instruction decode flags */
#define INV     0x0000            /* Instruction is invalid */
#define HLF     0x0001            /* Half word instruction */
#define ADR     0x0002            /* Normal addressing mode */
#define IMM     0x0003            /* Immediate mode */
#define WRD     0x0004            /* Word addressing, no index */
#define SCC     0x0008            /* Sets CC */
#define RR      0x0010            /* Read source register */
#define R1      0x0020            /* Read register 1 */
#define RB      0x0040            /* Read base register into dest */
#define SD      0x0080            /* Stores into destination register */
#define SDD     0x0100            /* Stores double into destination */
#define RM      0x0200            /* Reads memory */
#define SM      0x0400            /* Stores memory */
#define DBL     0x0800            /* Double word operation */
#define SB      0x1000            /* Store Base register */

int nobase_mode[] = {
   /*    00            04             08             0C  */
   /*    00            ANR,           ORR,           EOR */ 
        HLF,           SCC|SD|HLF,     SCC|SD|HLF,     SCC|SD|HLF, 
   /*    10            14             18             1C */
   /*    CAR,          CMR,           SBR            ZBR */
      SCC|RR|R1|HLF,   RR|R1| HLF,        SD|HLF,        SD|HLF,
   /*    20            24             28             2C  */
   /*    ABR           TBR                           TRR  */
      SD|HLF,          HLF,           INV,           SCC|HLF, 
   /*    30            34             38             3C */
   /*    CALM          LA             ADR            SUR */
       HLF,            SD|ADR,        SCC|SD|HLF,     SCC|SD|HLF,
   /*    40            44             48             4C  */ 
   /*    MPR           DVR                             */
      SD|HLF,          SD|HLF,        INV,           INV, 
   /*    50            54             58             5C */
   /*                                                 */
        INV,           INV,           INV,           INV,
   /*    60            64             68             6C   */
   /*    NOR           NORD           SCZ            SRA  */
      SD|HLF,          SDD|HLF,       SCC|SD|HLF,     SD|HLF, 
   /*    70            74             78             7C */
   /*    SRL           SRC            SRAD           SRLD */ 
      SD|HLF,          SD|HLF,        SDD|HLF,       SDD|HLF,
   /*    80            84             88             8C   */
   /*    LEAR          ANM            ORM            EOM  */
       SD|ADR,         SCC|SD|RR|RM|ADR,  SCC|SD|RR|RM|ADR,  SCC|SD|RR|RM|ADR,  
   /*    90            94             98             9C */
   /*    CAM           CMM            SBM            ZBM  */ 
      SCC|RM|ADR,      RM|ADR,     SM|RM|ADR,     SM|RM|ADR,
   /*    A0            A4             A8             AC  */
   /*    ABM           TBM            EXM            L    */
      SD|RM|ADR,       RM|ADR,        RM|ADR,        SCC|SD|RM|ADR,
   /*    B0            B4             B8             BC */
   /*    LM            LN             ADM            SUM  */ 
     SCC|SD|RM|ADR,     SCC|SD|RM|ADR,  CC|SD|RM|ADR,  SCC|SD|RM|ADR,
   /*    C0            C4             C8             CC    */
   /*    MPM           DVM            IMMD           LF  */
     SCC|SD|RM|ADR,     SCC|RM|ADR,     IMM,     ADR, 
   /*    D0            D4             D8             DC */
   /*    LEA           ST             STM            */ 
       SD|ADR,         SM|ADR,        SM|ADR,        INV,  
   /*    E0            E4             E8             EC   */
   /*    ADF           MPF            ARM            BCT  */
     SCC|SD|RM|ADR,     SCC|RM|ADR,     SM|RM|ADR,     ADR, 
   /*    F0            F4             F8             FC */
   /*    BCF           BI             MISC           IO */ 
        ADR,           SD|ADR,        ADR,           IMM,  
};

int base_mode[] = {
   /* 00        04            08         0C      */
   /* 00        AND,          OR,        EOR  */
     HLF,      SD|HLF,     SD|HLF,     SD|HLF,  
   /* 10        14           18        1C  */
   /* SACZ                  xBR         SRx */
     SD|HLF,    INV,        SD|HLF,    SD|HLF,
   /* 20        24            28         2C   */
   /* SRxD      SRC          REG        TRR      */
    SD|HLF,   SD|HLF,         HLF,       HLF,    
   /*    30      34          38           3C */
   /*         LA          FLRop        SUR */
    INV,    INV,       SD|HLF,      SD|HLF,
   /* 40        44            48         4C     */
   /*                                        */
      INV,      INV,        INV,       INV,  
    /* 50      54          58            5C */
   /*   LA      BASE       BASE        CALLM */ 
    SD|ADR,    ADR,        ADR,         ADR,
   /* 60        64            68         6C     */
   /*                                         */
      INV,      INV,         INV,      INV,   
   /* 70       74           78           7C */
   /*                                          */ 
    INV,     INV,        INV,        INV,  
   /* LEAR      ANM          ORM        EOM   */
   /* 80        84            88         8C   */
    SD|ADR,    SD|RM|ADR, SD|RM|ADR, SD|RM|ADR, 
   /*  CAM      CMM          SBM       ZBM  */ 
   /*   90      94            98         9C */
  RM|ADR,  RM|ADR,   SM|RM|ADR, SM|RM|ADR,
   /* A0        A4            A8         AC   */
   /* ABM       TBM          EXM        L     */
    SD|RM|ADR, RM|ADR,      RM|ADR,   SD|RM|ADR,
   /*   B0      B4            B8         BC */
   /*  LM       LN            ADM       SUM  */ 
    SD|RM|ADR, SD|RM|ADR,  SD|RM|ADR, SD|RM|ADR,
   /* C0        C4            C8         CC    */
   /* MPM       DVM          IMMD       LF    */
    SD|RM|ADR, RM|ADR,     IMM,      ADR, 
   /*  D0      D4            D8         DC */
   /*  LEA      ST            STM            */ 
   SD|ADR,   SM|ADR,       SM|ADR,     INV,  
   /* E0        E4            E8         EC     */
   /* ADF       MPF          ARM        BCT      */
    SD|RM|ADR, RM|ADR,     SM|RM|ADR,    ADR, 
   /* F0      F4            F8         FC */
  /*  BCF     BI             MISC       IO */ 
      ADR,   RR|SR|WRD,         ADR,     IMM,  
};

int page_lookup(uint32 addr, uint32 *loc, int wr) {
}

int Mem_read(uint32 addr, uint32 *data) {
    addr &= (modes & EXTD) ? 0xFFFFFC : 0x7FFFF;
    if (modes & MAP && page_lookup(addr, &addr, 0))
        return 1;
    if (addr > MSIZE) {
        /* Set NXM fault */
        return 1;
    }
    *data = M[addr >> 2];
    return 0;
}

int Mem_write(uint32 addr, uint32 *data) {
    addr &= (modes & EXTD) ? 0xFFFFFC : 0x7FFFF;
    if (modes & MAP && page_lookup(addr, &addr, 1))
        return 1;
    if (addr > MSIZE) {
        /* Set NXM fault */
        return 1;
    }
    M[addr >> 2] = *data;
    return 0;
}
/* Opcode definitions */

t_stat
sim_instr(void)
{
    t_stat              reason;
    uint64              dest;             /* Holds destination/source register */
    uint64              source;           /* Holds source or memory data */
    uint32              addr;             /* Holds address of last access */
    uint32              temp;             /* General holding place for stuff */
    uint32              IR;               /* Instruction register */
    uint16              opr;              /* Top half of Instruction register */
    uint8               fc;               /* Current F&C bits */
    int                 reg;              /* GPR or Base register */
    int                 dbl;              /* Double word */
    int                 ovr;              /* Overflow flag */
    int                 t;                /* Temporary */
    reason = 0;


    while (reason == 0) {       /* loop until halted */

        if (sim_interval <= 0) {        /* event queue? */
            reason = sim_process_event();
            if (reason != SCPE_OK) {
                if (reason == SCPE_STEP && iowait)
                    stopnext = 1;
                else
                    break;      /* process */
            }
        }

        if (iowait == 0 && sim_brk_summ &&
                 sim_brk_test(((bcore & 2)? CORE_B:0)|IC, SWMASK('E'))) {
            reason = STOP_IBKPT;
            break;
        }

/* Check if we need to take any traps */
        /* fill IR */
        if (Mem_read(PC, &IR)) {
           /* Fault on Fetch read */
        }

        /* If executing right half */
        if (PC & 2) 
            IR <<= 16;
exec:
/* Update history for this instruction */

        /* Split instruction into pieces */
        opr = (IR >> 16) & 0xFFFF;
        op = (opr >> 26) & 03F;
        FC =  (IR & F_BIT) ? 0x4 : 0;
        reg = (opr >> 23) & 0x7;
        dest = (uint64)IR;
        dbl = 0;
        ovr = 0;
        if (mode & BASE) {
             i_flags = base_mode[op];
             addr = IR & 0xFF00FFFF;
             switch(i_flags & 07) {
             case HLF:
                source = GPR[(IR >> 20) & 07];
                break;
             case IMMD:
                if (PC & 02) {
                /* Error */
                }
                break;
             case ADR:
             case WRD:
                if (PC & 02) {
                /* Error */
                }
                ix = (IR >> 21) & 7;
                if (ix != 0) {
                   addr += GPR[ix];
                }
                ix = (IR >> 16) & 7;
                if (ix != 0) 
                   addr += BR[ix];
                FC |= addr & 3;
                break;
                if (PC & 02) {
                /* Error */
                }
             case INV:
                break;
             }
        } else {
             i_flags = nobase_mode[op];
             addr = IR & 0xFF07FFFF;
             switch(i_flags & 07) {
             case HLF:
                source = GPR[(IR >> 20) & 07];
                break;
             case IMMD:
                if (PC & 02) {
                /* Error */
                }
                break;
             case ADR:
                if (PC & 02) {
                /* Error */
                }
                ix = (IR >> 21) & 3;
                if (ix != 0) {
                   addr += GPR[ix];
                }
                FC |= addr & 3;
                while (IR & IND) {
                   if (Mem_read(addr, &temp)) {
                        /* Fault */
                   }
                   addr = temp & 0xFF07FFFF;
                   dest = (uint64)temp;
                   ix = (temp >> 21) & 3;
                   if (ix != 0) 
                       addr += GPR[ix];
                   if ((temp & F_BIT) || (addr & 3)) 
                      FC =  ((temp & F_BIT) ? 0x4 : 0) | (addr & 3);
                } 
                break;
             case WRD:
                if (PC & 02) {
                /* Error */
                }
                FC |= addr & 3;
                while (IR & IND) {
                   if (Mem_read(addr, &temp)) {
                        /* Fault */
                   }
                   addr = temp & 0xFF07FFFF;
                   dest = (uint64)temp;
                   ix = (temp >> 21) & 3;
                   if (ix != 0) 
                       addr += GPR[ix];
                   if ((temp & F_BIT) || (addr & 3)) 
                      FC =  ((temp & F_BIT) ? 0x4 : 0) | (addr & 3);
                } 
                break;
             case INV:
             }
       }

       /* Read into memory operand */
       if (i_flags & RM) {
           if (Mem_read(addr, &temp)) {
               /* Fault */
           }
           source = (uint64)temp;
           switch(FC) {
           case 0:    if ((addr & 3) != 0) {
                         /* Address fault */
                      }
                      break;
           case 1:    source >>= 16;
                      /* Fall through */
           case 3:    
                      if ((addr & 1) != 0) {
                          /* Address Fault */
                      }
                      source = EXT(source);
                      break;
           case 2:    if ((addr & 7) != 0) {
                      }
                      if (Mem_read(addr + 4, &temp)) {
                      }
                      source |= ((uint64)temp) << 32;
                      dbl = 1;
                      break;
           case 4:
           case 5:
           case 6:
                      source >>= 8 * (7 - FC);
           case 7:
                      break;
           }
       }

       /* Read in if from register */
       if (i_flags & RR) {
           dest = (uint64)GPR[reg];
           if (dbl) {
              if (reg & 1) {
                  /* Spec fault */
              }
              dest |= ((uint64)GPR[reg|1]) << 32;
           } else {
              dest |= (dest & FSIGN) ? 0xFFFFFFFF << 32: 0;
           }
       }

       /* For Base mode */
       if (i_flags & RB) {
           dest = (uint64)BR[reg];
       }

       /* For register instructions */
       if (i_flags & R1) {
           int r = (IR >> 20) & 07;
           source = (uint64)GPR[r];
           if (dbl) {
              if (r & 1) {
                  /* Spec fault */
              }
              source |= ((uint64)GPR[r|1]) << 32;
           } else {
              source |= (source & FSIGN) ? 0xFFFFFFFF << 32: 0;
           }
       }

       switch (op) {
       case 0x00:          /* CPU General operations */
                  switch(IR & 0xF) {
                  case 0x0:   /* HALT */
                             break;
                  case 0x1:   /* WAIT */
                             break;
                  case 0x2:   /* NOP */
                             break;
                  case 0x3:   /* LCS */
                             break;
                  case 0x4:   /* ES */
                            temp = GPR[reg];
                            GPR[(reg+1)&7] = (temp & FSIGN) ? FMASK : 0;
                            CC &= AEXP;
                            if (ovr) 
                               CC |= CC1;
                            else if (temp & FSIGN)
                               CC |= CC3;
                            else if (temp == 0)
                               CC |= CC4;
                            else 
                               CC |= CC2;
                            break;
                  case 0x5:   /* RND */
                            temp = GPR[reg];
                            if (GPR[(reg+1)&7] & FSIGN) {
                               temp ++;
                               if (temp < GPR[reg])
                                  ovr = 1;
                               GPR[reg] = temp;
                            }
                            CC &= AEXP;
                            if (ovr) 
                               CC |= CC1;
                            else if (temp & FSIGN)
                               CC |= CC3;
                            else if (temp == 0)
                               CC |= CC4;
                            else 
                               CC |= CC2;
                            break;
                  case 0x6:   /* BEI */
                             break;
                  case 0x7:   /* UEI */
                             break;
                  case 0x8:   /* EAE */
                             CC |= AEXP;
                             break;
                  case 0x9:   /* RDSTS */
                             break;
                  case 0xA:   /* SIPU */
                             break;
                  case 0xB:   /* INV */
                  case 0xC:   /* INV */
                             break;
                  case 0xD:   /* SEA */
                             modes |= EXTD;
                             break;
                  case 0xE:   /* DAE */
                             CC &= ~AEXP;
                             break;

                  case 0xF:   /* CEA */
                             modes &= ~EXTD;
                             break;
                  }
                  break;
       case 0x01:             /* 0x04 */
                  switch(IR & 0xF) {
                  case 0x0:   /* ANR */  /* SCC|SD */
                             dest &= source;
                             break;
                  case 0xA:   /* CMC */
                             break;
                  case 0x7:   /* SMC */
                             break;
                  case 0xB:   /* RPSWT */
                             break;
                  default:    /* INV */
                             break;
                  }
                  break;
       case 0x02:             /* 0x08 */ /* ORR or ORRM */
                  dest |= source;
                  if (IR & 0x8)
                     dest &= GPR[4];
                  break;
       case 0x03:             /* 0x0c */ /* EOR or EORM */
                  dest ^= source;
                  if (IR & 0x8)
                     dest &= GPR[4];
                  break;
       case 0x04:             /* 0x10 */ /* CAR or (basemode SACZ ) */
                  if (modes & BASE) {
                      temp = GPR[reg];
                      t = (IR >> 20) & 7;
                      temp = temp - GPR[t];
                      CC &= AEXP;
                      else if (temp & FSIGN)
                         CC |= CC3;
                      else if (temp == 0)
                         CC |= CC4;
                      else 
                         CC |= CC2;
                  } else {
scaz:
                      temp = GPR[reg];
                      t = 0;
                      CC &= AEXP;
                      if (temp != 0) {
                         while((temp & FSIGN) == 0) {
                            temp <<= 1;
                            t++;
                         }
                         temp <<= 1;
                      } else {
                         CC |= CC4;
                      }
                      GPR[(IR >> 20) & 7] = t; 
                  }
                  break;

       case 0x05:             /* 0x14 */ /* SBR, (basemode ZBR, ABR, TBR */
       case 0x06:             /* 0x18 */ /* SRABR, SRLBR, SLABR, SLLBR  */
                  if ((mode & BASE) == 0) {
                      int r = (IR >> 20) & 7;
                      int b = 31 - (((IR >> 13) & 030) | reg);
                      temp = GPR[r];
                      ovr = ((1 << b) & temp) != 0;
                      GPR[r] |= (1 << temp);
                      CC = ((ovr)?CC1:0) | ((CC >> 1) & (CC2|CC3|CC4)) | (CC & AEXP);
                  }
                  break;

       case 0x07:             /* 0x1C */ /* ZBR non-basemode */
                  if ((mode & BASE) == 0) {
                      int r = (IR >> 20) & 7;
                      int b = 31 - (((IR >> 13) & 030) | reg);
                      temp = GPR[r];
                      ovr = ((1 << b) & temp) != 0;
                      GPR[r] &= ~(1 << temp);
                      CC = ((ovr)?CC1:0) | ((CC >> 1) & (CC2|CC3|CC4)) | (CC & AEXP);
                  }
                  break;

       case 0x08:             /* 0x20 */ /* ABR (basemode SRADBR, SRLDBR, SLADBR, SLLDBR) */
                  if ((mode & BASE) == 0) {
                      int r = (IR >> 20) & 7;
                      int b = 31 - (((IR >> 13) & 030) | reg);
                      temp = GPR[r];
                      ovr = (temp & FSIGN) != 0;
                      temp += b;
                      ovr ^= (temp & FSIGN) != 0;
                      GPR[r] = temp;
                      CC &= AEXP;
                      if (ovr) 
                         CC |= CC1;
                      else if (temp & FSIGN)
                         CC |= CC3;
                      else if (temp == 0)
                         CC |= CC4;
                      else 
                         CC |= CC2;
                  }
                  break;

       case 0x09:             /* 0x24 */ /* TBR (basemode SRCBR)  */
                  if ((mode & BASE) == 0) {
                      int r = (IR >> 20) & 7;
                      int b = 31 - (((IR >> 13) & 030) | reg);
                      temp = GPR[r];
                      ovr = ((1 << b) & temp) != 0;
                      CC = ((ovr)?CC1:0) | ((CC >> 1) & (CC2|CC3|CC4)) | (CC & AEXP);
                  }
                  break;

       case 0x0A:             /* 0x28 */
                  temp = GPR[reg];
                  switch(IR & 0xF) {
                  case 0x0:   /* TRSW */
                            PC = temp & FMASK;
                            CC = ((CC1|CC2|CC3|CC4) & ((uint8)(temp >> 24))) |
                                  CC | AEXP;
                            break;

                  case 0x1:   /* TRBR */
                            if (modes & BASE) {
                                t = (IR >> 20) & 7;
                                BR[reg] = GPR[t];
                            } else {
                              /* Fault */
                            }
                            break;

                  case 0x2:   /* XCBR */
                            if (modes & BASE) {
                                temp = BR[reg];
                                t = (IR >> 20) & 7;
                                addr = BR[t];
                                BR[t] = temp;
                                temp = addr;
                                BR[reg] = temp;;
                            } else {
                              /* Fault */
                            }
                            break;

                  case 0x3:   /* TCCR */
                            temp = (CC & (CC1|CC2|CC3|CC4)) >> 3;
                            break;

                  case 0x4:   /* TRCC */
                            CC = ((CC1|CC2|CC3|CC4) & ((uint8)(temp << 3))) |
                                  CC | AEXP;
                            break;

                  case 0x5:   /* BSUB */
                             break;
                  case 0x8:   /* CALL */
                             break;
                  case 0xC:   /* PTCBR */
                            if (mode & BASE) {
                               BR[reg] = PC;
                               break;
                            } else {
                               /* Fault */
                            }
                            break;

                  case 0xE:   /* RETURN */
                             break;
                  case 0xD:   /* INV */
                  case 0x6:   /* INV */
                  case 0x7:   /* INV */
                  case 0x9:   /* INV */
                  case 0xA:   /* INV */
                  case 0xB:   /* INV */
                  case 0xF:   /* INV */
                  }
                  GPR[reg] = temp;;
                  break;
       case 0x0B:             /* 0x2C */
                  temp = GPR[reg];
                  t = (IR >> 20) & 7;
                  addr = GPR[t];
                  switch(IR & 0xF) {
                  case 0x0:   /* TRR */ /* SCC|SD|R1 */
                           temp = addr;
                           break;
                  case 0x1:   /* TRDR */
                             break;
                  case 0x2:   /* TBRR */
                            if (modes & BASE) {
                                t = (IR >> 20) & 7;
                                GPR[reg] = BR[t];
                            } else {
                              /* Fault */
                            }
                            break;

                  case 0x3:   /* TRC */
                           temp = addr ^ FMASK;
                           break;
                  case 0x4:   /* TRN */
                           temp = -addr;
                           if (temp == addr)
                               ovr = 1;
                           break;
                  case 0x5:   /* XCR */
                           GPR[t] = temp;
                           temp = addr;
                           ovr = 0;
                           break;

                  case 0x6:   /* INV */
                             break;
                  case 0x7:   /* LMAP */
                             break;
                  case 0x8:   /* TRRM */ /* SCC|SD|R1 */
                           temp = addr & GPR[4];
                           break;
                  case 0x9:   /* SETCPU */
                             break;
                  case 0xA:   /* TMAPR */
                             break;
                  case 0xB:   /* TRCM */
                           temp = (addr ^ FMASK) & GPR[4];
                           break;
                  case 0xC:   /* TRNM */
                           temp = -addr;
                           if (temp == addr)
                               ovr = 1;
                           temp &= GPR[4];
                           break;
                  case 0xD:   /* XCRM */
                           addr &= GPR[4];
                           GPR[t] = temp & GPR[4];
                           temp = addr;
                           ovr = 0;
                           break;
                  case 0xE:   /* TRSC */
                  case 0xF:   /* TSCR */
                  }
                  GPR[reg] = temp;
                  if ((IR & 0xF) < 6) {
                      CC &= AEXP;
                      if (ovr) 
                         CC |= CC1;
                      else if (temp & FSIGN)
                         CC |= CC3;
                      else if (temp == 0)
                         CC |= CC4;
                      else 
                         CC |= CC2;
                  }
                  break;
       case 0x0C:             /* 0x30 */ /* CALM */
                             break;
       case 0x0D:             /* 0x34 */ /* LA non-basemode */
                             break;
       case 0x0E:             /* 0x38 */
                  temp = GPR[reg];
                  t = (IR >> 20) & 7;
                  addr = GPR[t];
                  switch(IR & 0xF) {
                  case 0x0:   /* ADR */
                            t = (temp & FSIGN) != 0;
                            t |= ((addr & FSIGN) != 0) ? 2 : 0;
                            temp = temp + addr;
                            if ((t == 3 && (temp & FSIGN) == 0) ||
                                (t == 0 && (addr & FSIGN) != 0))
                                ovr = 1;
                            break;
                  case 0x1:   /* ADRFW */
                             break;
                  case 0x2:   /* MPRBR */
                             break;
                  case 0x3:   /* SURFW */
                             break;
                  case 0x4:   /* DVRFW */
                             break;
                  case 0x5:   /* FIXW */
                             break;
                  case 0x6:   /* MPRFW */
                             break;
                  case 0x7:   /* FLTW */
                             break;
                  case 0x8:   /* ADRM */
                            t = (temp & FSIGN) != 0;
                            t |= ((addr & FSIGN) != 0) ? 2 : 0;
                            temp = temp + addr;
                            if ((t == 3 && (temp & FSIGN) == 0) ||
                                (t == 0 && (addr & FSIGN) != 0))
                                ovr = 1;
                            temp &= GPR[4];
                            break;
                  case 0x9:   /* INV */
                             break;
                  case 0xA:   /* DVRBR */
                             break;
                  case 0xB:   /* SURFD */
                             break;
                  case 0xC:   /* DVRFD */
                             break;
                  case 0xD:   /* FIXD */
                             break;
                  case 0xE:   /* MPRFD */
                             break;
                  case 0xF:   /* FLTD */
                             break;
                  }
                  GPR[reg] = temp;
                  if ((IR & 0xF) < 6) {
                      CC &= AEXP;
                      if (ovr) 
                         CC |= CC1;
                      else if (temp & FSIGN)
                         CC |= CC3;
                      else if (temp == 0)
                         CC |= CC4;
                      else 
                         CC |= CC2;
                  }
                  break;
       case 0x0F:             /* 0x3C */ /* SUR and SURM */
                  temp = -GPR[reg];
                  t = (IR >> 20) & 7;
                  addr = GPR[t];
                  switch(IR & 0xF) {
                  case 0x0:   /* SUR */
                            
                            t = (temp & FSIGN) != 0;
                            t |= ((addr & FSIGN) != 0) ? 2 : 0;
                            temp = temp + addr;
                            if ((t == 3 && (temp & FSIGN) == 0) ||
                                (t == 0 && (addr & FSIGN) != 0))
                                ovr = 1;
                            break;
                  case 0x8:   /* SURM */
                            t = (temp & FSIGN) != 0;
                            t |= ((addr & FSIGN) != 0) ? 2 : 0;
                            temp = addr + temp;
                            if ((t == 3 && (temp & FSIGN) == 0) ||
                                (t == 0 && (temp & FSIGN) != 0))
                                ovr = 1;
                            temp &= GPR[4];
                            break;
                  case 0x9:   /* INV */
                             break;
                  case 0xA:   /* DVRBR */
                             break;
                  case 0xB:   /* SURFD */
                             break;
                  case 0xC:   /* DVRFD */
                             break;
                  case 0xD:   /* FIXD */
                             break;
                  case 0xE:   /* MPRFD */
                             break;
                  case 0xF:   /* FLTD */
                             break;
                  }
                  GPR[reg] = temp;
                  if ((IR & 0xF) < 6) {
                      CC &= AEXP;
                      if (ovr) 
                         CC |= CC1;
                      else if (temp & FSIGN)
                         CC |= CC3;
                      else if (temp == 0)
                         CC |= CC4;
                      else 
                         CC |= CC2;
                  }
                  break;
       case 0x10:             /* 0x40 */ /* MPR */
                  if (reg & 1) {
                       /* Spec fault */
                  }
                  temp = GPR[reg];
                  t = (IR >> 20) & 7;
                  addr = GPR[t];
                  dest = (uint64)temp | (temp & FSIGN) ? FMASK << 32: 0;
                  source = (uint64)addr | (addr & FSIGN) ? FMASK << 32: 0;
                  GPR[reg] = (uint32)(dest &  FMASK);
                  GPR[reg|1] = (uint32)((dest >> 32) & FMASK);
                  CC &= AEXP;
                  if (dest & MSIGN)
                     CC |= CC3;
                  else if (dest == 0)
                     CC |= CC4;
                  else 
                     CC |= CC2;
                  break;

       case 0x11:             /* 0x44 */ /* DVR */
                  if (reg & 1) {
                      /* Spec fault */
                  }
                  t = (IR >> 20) & 7;
                  source = (uint64)GPR[t];
                  source |=  (source & FSIGN) ? FMASK << 32: 0;
                  if (source == 0) {
                      ovr = 1;
                      break;
                  }
                  dest = (uint64)GPR[reg];
                  dest |= ((uint64)GPR[reg|1]) << 32;
                  t = (int64)dest % (int64)source;
                  dbl = (t < 0);
                  if ((t ^ (dest & MSIGN)) != 0)  /* Fix sign if needed */
                      t = -t;
                  dest = (int64)dest / (int64)source;
                  if ((dest & LMASK) != 0 && (dest & LMASK) != LMASK)
                      ovr = 1;
                  GPR[reg] = (uint32)t;
                  GPR[reg|1] = (uint32)(dest &  FMASK);
                  CC &= AEXP;
                  if (dest & MSIGN)
                     CC |= CC3;
                  else if (dest == 0)
                     CC |= CC4;
                  else 
                     CC |= CC2;
                  break;
       case 0x12:             /* 0x48 */
       case 0x13:             /* 0x4C */
       case 0x14:             /* 0x50 */ /* (basemode LA) */
                  if (modes & (BASE|EXTD)) {
                     dest = addr;
                  } else {
                     dest = addr | ((FC & 4) << 18);
                  }
                  break;

       case 0x15:             /* 0x54 */ /* (basemode STWBR) */
                  if (FC != 0) {
                     /* Fault */
                  }
                  break;
       case 0x16:             /* 0x58 */ /* (basemode SUABR and LABR) */
                  if ((FC & 4) != 0) {
                     dest = addr;
                  } else {
                     dest += addr;
                  }
                  break;
       case 0x17:             /* 0x5C */ /* (basemode LWBR and BSUBM) */
                  if (FC != 0) {
                     /* Fault */
                  }
                  break;
       case 0x18:             /* 0x60 */ /* NOR */
                  if ((modes & BASE) == 0) {
                      temp = GPR[reg];
                      t = 0;
                      if (temp != 0 && temp != FMASK) {
                          uint32 m = temp & 0xF8000000;
                          while ((m == 0) || (m == 0xF8000000)) {
                              temp <<= 4;
                              m = temp & 0xF8000000;
                              t++;
                          }
                          GPR[reg] = temp;
                      }
                      GPR[(IR >> 20) & 7] = t;
                  }
                  break;

       case 0x19:             /* 0x64 */ /* NORD */
                  if ((modes & BASE) == 0) {
                      if (reg & 1) {
                          /* Fault */
                      }
                      temp = GPR[reg|1];
                      addr = GPR[reg];
                      t = 0;
                      if ((temp|addr) != 0 && (temp&addr) != FMASK) {
                          uint32 m = temp & 0xF8000000;
                          while ((m == 0) || (m == 0xF8000000)) {
                              temp <<= 4;
                              m = temp & 0xF8000000;
                              temp |= (addr >> 28) & 0xf;
                              addr <<= 4;
                              t++;
                          }
                          GPR[reg|1] = temp;
                          GPR[reg] = addr;
                      }
                      GPR[(IR >> 20) & 7] = t;
                  }
                  break;

       case 0x1A:             /* 0x68 */ /* SCZ */
                  if ((modes & BASE) == 0) 
                     goto scaz;
 
       case 0x1B:             /* 0x6C */ /* non-basemode SRA & SLA */
       case 0x1C:             /* 0x70 */ /* non-basemode SRL & SLL */
       case 0x1D:             /* 0x74 */ /* non-basemode SRC & SLC */
       case 0x1E:             /* 0x78 */ /* non-basemode SRAD & SLAD */
       case 0x1F:             /* 0x7C */ /* non-basemode SRLD & SLLD */
       case 0x20:             /* 0x80 */ /* LEAR */
       case 0x21:             /* 0x84 */ /* ANMx */
                  dest &= source;
                  break;

       case 0x22:             /* 0x88 */ /* ORMx */
                  dest |= source;
                  break;

       case 0x23:             /* 0x8C */ /* EOMx */
                  dest ^= source;
                  break;

       case 0x24:             /* 0x90 */ /* CAMx */
                  dest -= source;
                  break;

       case 0x25:             /* 0x94 */ /* CMMx */
                  dest ^= source;
                  CC = CC & AEXP;
                  if (dest == 0)
                     CC |= CC4;
                  break;

       case 0x26:             /* 0x98 */ /* SBM */
                  if ((FC & 04) != 0)  {
                      /* Fault */
                  }
                  if (Mem_read(addr, &temp)) {
                      /* Fault */
                  }
                  t = 1 << (31 - (((FC & 3) << 3) | reg));
                  ovr = (temp & t) != 0;
                  temp |= t;
                  if (Mem_write(addr, &temp)) {
                      /* Fault */
                  }
                  CC = ((ovr)?CC1:0) | ((CC >> 1) & (CC2|CC3|CC4)) | (CC & AEXP);
                  break;
                  
       case 0x27:             /* 0x9C */ /* ZBM */
                  if ((FC & 04) != 0)  {
                      /* Fault */
                  }
                  if (Mem_read(addr, &temp)) {
                      /* Fault */
                  }
                  t = 1 << (31 - (((FC & 3) << 3) | reg));
                  ovr = (temp & t) != 0;
                  temp &= ~t;
                  if (Mem_write(addr, &temp)) {
                      /* Fault */
                  }
                  CC = ((ovr)?CC1:0) | ((CC >> 1) & (CC2|CC3|CC4)) | (CC & AEXP);
                  break;

       case 0x28:             /* 0xA0 */ /* ABM */
                  if ((FC & 04) != 0)  {
                      /* Fault */
                  }
                  if (Mem_read(addr, &temp)) {
                      /* Fault */
                  }
                  t = 1 << (31 - (((FC & 3) << 3) | reg));
                  ovr = (temp & FSIGN) != 0;
                  temp += t;
                  ovr ^= (temp & FSIGN) != 0;
                  if (Mem_write(addr, &temp)) {
                      /* Fault */
                  }
                  dest = (uint64)temp | (temp & FSIGN) ? 0xFFFFFFFFLL << 32: 0;
                  break;

       case 0x29:             /* 0xA4 */ /* TBM */
                  if ((FC & 04) != 0)  {
                      /* Fault */
                  }
                  if (Mem_read(addr, &temp)) {
                      /* Fault */
                  }
                  t = 1 << (31 - (((FC & 3) << 3) | reg));
                  ovr = (temp & t) != 0;
                  CC = ((ovr)?CC1:0) | ((CC >> 1) & (CC2|CC3|CC4)) | (CC & AEXP);
                  break;

       case 0x2A:             /* 0xA8 */ /* EXM */
                  if ((FC & 04) != 0 || FC == 2) {
                      /* Fault */
                  } 
                  IR = (uint32)source;
                  if (FC == 3)
                      IR <<= 16;
                  if ((IR & 0xFC7F0000) == 0xC8070000 ||
                      (IR & 0xFF800000) == 0xA8000000) {
                      /* Fault */
                  }
                  goto exec;
 
       case 0x2B:             /* 0xAC */ /* Lx */
                  dest = source;
                  break;

       case 0x2C:             /* 0xB0 */ /* LMx */
                  dest = source & GPR[4];
                  break;
 
       case 0x2D:             /* 0xB4 */ /* LNx */
                  dest = (source ^ DMASK) + 1;
                  if (dest == source)
                      ovr = 1;
                  break;

       case 0x2F:             /* 0xBC */ /* SUMx */
                  source = -source;
                  /* Fall through */

       case 0x3A:             /* 0xE8 */ /* ARMx */
       case 0x2E:             /* 0xB8 */ /* ADMx */
                  t = (source & MSIGN) != 0;
                  t |= ((dest & MSIGN) != 0) ? 2 : 0;
                  dest = dest + source;
                  if ((t == 3 && (dest & MSIGN) == 0) ||
                      (t == 0 && (dest & MSIGN) != 0))
                      ovr = 1;
                  if (dbl == 0 && (dest & LMASK) != 0 && (dest & LMASK) != LMASK)
                      ovr = 1;
                  break;
       case 0x30:             /* 0xC0 */ /* MPMx */
                  if (FC == 3) {
                     /* Fault */
                  }
                  if (reg & 1) {
                      /* Spec fault */
                  }
                  dest = (uint64)((int64)dest * (int64)source);
                  dbl = 1;
                  break;

       case 0x31:             /* 0xC4 */ /* DVMx */
                  if (FC == 3) {
                     /* Fault */
                  }
                  if (reg & 1) {
                      /* Spec fault */
                  }
                  if (source == 0) {
                      ovr = 1;
                      break;
                  }
                  dest = (uint64)GPR[reg];
                  dest |= ((uint64)GPR[reg|1]) << 32;
                  t = (int64)dest % (int64)source;
                  dbl = (t < 0);
                  if ((t ^ (dest & MSIGN)) != 0)  /* Fix sign if needed */
                      t = -t;
                  dest = (int64)dest / (int64)source;
                  if ((dest & LMASK) != 0 && (dest & LMASK) != LMASK)
                      ovr = 1;
                  GPR[reg] = (uint32)t;
                  reg|=1;
                  break;

       case 0x32:             /* 0xC8 */ /* Immedate */
                  temp = GPR[reg];
                  addr = SEXT(IR);
                  switch(IR & 0xF) {
                  case 0x0:   /* LI */  /* SCC | SR */
                            temp = addr;
                            GPR[reg] = temp;
                            CC &= AEXP;
                            if (temp & FSIGN)
                               CC |= CC3;
                            else if (temp == 0)
                               CC |= CC4;
                            else 
                               CC |= CC2;
                            break;
                  case 0x2:   /* SUI */
                             addr = -addr;
                  case 0x1:   /* ADI */
                            t = (temp & FSIGN) != 0;
                            t |= ((addr & FSIGN) != 0) ? 2 : 0;
                            temp = temp + addr;
                            if ((t == 3 && (temp & FSIGN) == 0) ||
                                (t == 0 && (temp & FSIGN) != 0))
                                ovr = 1;
                            GPR[reg] = temp;
                            CC &= AEXP;
                            if (ovr) 
                               CC |= CC1;
                            else if (temp & FSIGN)
                               CC |= CC3;
                            else if (temp == 0)
                               CC |= CC4;
                            else 
                               CC |= CC2;
                            break;
                  case 0x3:   /* MPI */
                            if (reg & 1) {
                                 /* Spec fault */
                            }
                            dest = (uint64)temp | (temp & FSIGN) ? FMASK << 32: 0;
                            source = (uint64)addr | (addr & FSIGN) ? FMASK << 32: 0;
                            GPR[reg] = (uint32)(dest &  FMASK);
                            GPR[reg|1] = (uint32)((dest >> 32) & FMASK);
                            CC &= AEXP;
                            if (dest & MSIGN)
                               CC |= CC3;
                            else if (dest == 0)
                               CC |= CC4;
                            else 
                               CC |= CC2;
                            break;

                  case 0x4:   /* DVI */
                            if (reg & 1) {
                                /* Spec fault */
                            }
                            source = (uint64)addr | (addr & FSIGN) ? FMASK << 32: 0;
                            if (source == 0) {
                                ovr = 1;
                                break;
                            }
                            dest = (uint64)GPR[reg];
                            dest |= ((uint64)GPR[reg|1]) << 32;
                            t = (int64)dest % (int64)source;
                            dbl = (t < 0);
                            if ((t ^ (dest & MSIGN)) != 0)  /* Fix sign if needed */
                                t = -t;
                            dest = (int64)dest / (int64)source;
                            if ((dest & LMASK) != 0 && (dest & LMASK) != LMASK)
                                ovr = 1;
                            GPR[reg] = (uint32)t;
                            GPR[reg|1] = (uint32)(dest &  FMASK);
                            CC &= AEXP;
                            if (dest & MSIGN)
                               CC |= CC3;
                            else if (dest == 0)
                               CC |= CC4;
                            else 
                               CC |= CC2;
                            break;

                  case 0x5:   /* CI */   /* SCC */
                            temp -= addr;
                            CC &= AEXP;
                            if (temp & FSIGN)
                               CC |= CC3;
                            else if (temp == 0)
                               CC |= CC4;
                            else 
                               CC |= CC2;
                            break;
                  case 0x6:   /* SVC */
                             break;
                  case 0x7:   /* EXR */
                            IR = temp;
                            if (addr & 2)
                                IR <<= 16;
                            if ((IR & 0xFC7F0000) == 0xC8070000 ||
                                (IR & 0xFF800000) == 0xA8000000) {
                                /* Fault */
                            }
                            goto exec;

                  case 0x8:   /* SEM */
                             break;
                  case 0x9:   /* LEM */
                             break;
                  case 0xA:   /* CEMA */
                             break;
                  case 0xB:   /* INV */
                  case 0xC:   /* INV */
                  case 0xD:   /* INV */
                  case 0xE:   /* INV */
                  case 0xF:   /* INV */
                             break;
                  }
                  break;

       case 0x33:             /* 0xCC */ /* LF */
                  /* For machines with Base mode 0xCC08 stores base registers */
                  /* Validate access read addr to 8 - reg */
                  temp = addr + (8 - reg);
                  if ((temp & 0x1f) != (addr & 0x1f)) {
                      /* Fault? */
                  }
                  while (reg < 8) {
                     (void)Mem_read(addr, &GPR[reg]);
                     reg++;
                     addr += 4;
                  }
                  break;

       case 0x34:             /* 0xD0 */ /* LEA */
                  dest = (uint64)(addr);
                  /* if IX == 00 => dest = IR */
                  /* if IX == 0g => dest = IR + reg */
                  /* if IX == Ix => dest = ind + reg */
                  break;

       case 0x35:             /* 0xD4 */ /* STx */
                  break;

       case 0x36:             /* 0xD8 */ /* STMx */
                  dest &= GPR[4];
                  break;

       case 0x37:             /* 0xDC */ /* STFx */
                  /* For machines with Base mode 0xDC08 stores base registers */
                  /* Validate access write addr to 8 - reg */
                  temp = addr + (8 - reg);
                  if ((temp & 0x1f) != (addr & 0x1f)) {
                      /* Fault? */
                  }
                  while (reg < 8) {
                     (void)Mem_write(addr, &GPR[reg]);
                     reg++;
                     addr += 4;
                  }
                  break;

       case 0x38:             /* 0xE0 */ /* ADFx, SUFx */
       case 0x39:             /* 0xE4 */ /* MPFx, DVFx */
       case 0x3B:             /* 0xEC */ /* Branch True */
                  switch(reg) {
                  case 0:   ovr = 1; break;
                  case 1:   ovr = (CC & CC1) != 0; break;
                  case 2:   ovr = (CC & CC2) != 0; break;
                  case 3:   ovr = (CC & CC3) != 0; break;
                  case 4:   ovr = (CC & CC4) != 0; break;
                  case 5:   ovr = (CC & (CC2|CC4)) != 0; break;
                  case 6:   ovr = (CC & (CC3)|CC4) != 0; break;
                  case 7:   ovr = (CC & (CC1|CC2|CC3|CC4)) != 0; break;
                  }
                  if (ovr) {
                     PC = addr;
                     CC = ((CC1|CC2|CC3|CC4) & (addr >> 24)) | (AEXP & CC);
                  }
                  break;

       case 0x3C:             /* 0xF0 */ /* Branch False */
                  if ((FC & 5) != 0) {
                       /* Fault */
                  }
                  switch(reg) {
                  case 0:   ovr = (GPR[4] & (1 << ((CC >> 3) + 16))) == 0; break;
                  case 1:   ovr = (CC & CC1) == 0; break;
                  case 2:   ovr = (CC & CC2) == 0; break;
                  case 3:   ovr = (CC & CC3) == 0; break;
                  case 4:   ovr = (CC & CC4) == 0; break;
                  case 5:   ovr = (CC & (CC2|CC4)) == 0; break;
                  case 6:   ovr = (CC & (CC3)|CC4) == 0; break;
                  case 7:   ovr = (CC & (CC1|CC2|CC3|CC4)) == 0; break;
                  }
                  if (ovr) {
                     PC = addr;
                     CC = ((CC1|CC2|CC3|CC4) & (addr >> 24)) | (AEXP & CC);
                  }
                  break;

       case 0x3D:             /* 0xF4 */ /* Branch increment */
                  dest += 1 << (IR >> 21);
                  if (dest == 0)
                      PC = addr;
                  break;

       case 0x3E:             /* 0xF8 */ /* ZMx, BL, BRI, LPSD, LPSDCM, TPR, RRP */
                  switch((IR >> 7) & 0x7) {
                  case 0x0:   /* ZMx */  /* SM */
                            dest = 0;
                            break;
                  case 0x1:   /* BL */
                            GPR[0] = (CC << 24) | PC;
                            PC = addr;
                            CC = ((CC1|CC2|CC3|CC4) & (addr >> 24)) | (AEXP & CC);
                            break;

                  case 0x2:   /* BRI */
                             break;
                  case 0x3:   /* LPSD */
                             break;
                  case 0x4:   /* INV */
                             break;
                  case 0x5:   /* LPSDCM */
                             break;
                  case 0x6:   /* TRP */
                             break;
                  case 0x7:   /* TPR */
                  }
                  break;

       case 0x3F:             /* 0xFC */ /* IO */
       }

       /* Store result to register */
       if (i_flags & SD) {
          if (dbl) 
             GPR[reg|1] = (uint32)(dest>>32);
          GPR[reg] = (uint32)(dest & FMASK);
       }

       /* Store result to base register */
       if (i_flags & SBR) {
          if (dbl)  {
              /* Fault */
          }
          BR[reg] = (uint32)(dest & FMASK);
       }

       /* Store result to memory */
       if (i_flags & SM) {
           /* Check if byte of half word */
           if (((FC & 04) || (FC & 5) == 1) && Mem_read(addr, &temp)) {
               /* Fault */
           }
           switch(FC) {
           case 2:    if ((addr & 7) != 0) {
                      }
                      temp = (uint32)(dest >> 32);
                      if (Mem_write(addr + 4, &temp)) {
                      }
                      /* Fall through */

           case 0:    temp = (uint32)(dest & FMASK);
                      if ((addr & 3) != 0) {
                         /* Address fault */
                      }
                      break;

           case 1:    temp &= RMASK;
                      temp |= (uint32)(dest & RMASK) << 16;
                      if ((addr & 1) != 0) {
                          /* Address Fault */
                      }
                      break;

           case 3:    temp &= LMASK;
                      temp |= (uint32)(dest & RMASK);
                      if ((addr & 1) != 0) {
                          /* Address Fault */
                      }
                      break;

           case 4:
           case 5:
           case 6:
           case 7:
                      temp &= ~(0xFF << (8 * (7 - FC)));
                      temp |= (uint32)(dest & 0xFF) << (8 * (7 - FC));
                      break;
           }
           if (Mem_write(addr, &temp)) {
              /* Fault */
           }
       }

       /* Update condition code registers */
       if (i_flags & SCC) {
          CC = CC & AEXP;
          if (ovr) 
             CC |= CC1;
          else if (dest & MSIGN) 
             CC |= CC3;
          else if (dest == 0)
             CC |= CC4;
          else
             CC |= CC2;
       }

       /* Update instruction pointer to next instruction */
       if (i_flags & HLF) {
           PC = (PC + 2) | (((PC & 2) >> 1) & 1);
       } else {
           PC = (PC + 4) | (((PC & 2) >> 1) & 1);
       }
       PC &= (modes & EXTD) ? 0xFFFFFF : 0x7FFFF;
    }                           /* end while */

/* Simulation halted */

    return reason;
}

    
/* Reset routine */

t_stat
cpu_reset(DEVICE * dptr)
{
    int                 i;

    sim_brk_types = sim_brk_dflt = SWMASK('E');

    return SCPE_OK;
}

/* Interval timer routines */
t_stat
rtc_srv(UNIT * uptr)
{
    return SCPE_OK;
}

/* Memory examine */

t_stat
cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw)
{
    uint32 wrd;
    if (addr >= MSIZE)
        return SCPE_NXM;
    if (vptr == NULL) 
        return SCPE_OK;
    wrd = M[addr >> 2];
    wrd >>= 8 * (3 - (addr & 3));
    *vptr = wrd;
    return SCPE_OK;
}

/* Memory deposit */

t_stat
cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw)
{
    uint32 wrd;
    uint32 msk;
    int    of;
    if (addr >= MSIZE)
        return SCPE_NXM;
    if (vptr == NULL) 
        return SCPE_OK;
    of = 8 * (3 - (addr & 3));
    addr >>= 2;
    wrd = M[addr];
    msk = 0xFF << of;
    wrd &= ~msk;
    wrd |= (*vptr * 0xFF) << of;
    return SCPE_OK;
}

t_stat
cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    t_uint64            mc = 0;
    uint32              i;

    cpu_unit.flags &= ~UNIT_MSIZE;
    cpu_unit.flags |= val;
    val >>= UNIT_V_MSIZE;
    val = (val + 1) * 128 * 1024;
    if ((val < 0) || (val > MAXMEMSIZE))
        return SCPE_ARG;
    for (i = val; i < MEMSIZE; i++)
        mc |= M[i];
    if ((mc != 0) && (!get_yn("Really truncate memory [N]?", FALSE)))
        return SCPE_OK;
    MEMSIZE = val;
    memmask = val - 1;
    for (i = MEMSIZE; i < MAXMEMSIZE; i++)
        M[i] = 0;
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
            hst[i].ic = 0;
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
    char               *cptr = (char *) desc;
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
    fprintf(st, " \n\n");
    for (k = 0; k < lnt; k++) { /* print specified */
        h = &hst[(++di) % hst_lnt];     /* entry pointer */
        if (h->ic & HIST_PC) {  /* instruction? */
            fputc('\n', st);    /* end line */
        }                       /* end else instruction */
    }                           /* end for */
    return SCPE_OK;
}

const char *
cpu_description (DEVICE *dptr) 
{
       return "SEL 32 CPU";
}

t_stat
cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "The CPU can be set to \n");
fprintf (st, "The CPU can maintain a history of the most recently executed instructions.\n"
);
fprintf (st, "This is controlled by the SET CPU HISTORY and SHOW CPU HISTORY commands:\n\n"
);
fprintf (st, "   sim> SET CPU HISTORY                 clear history buffer\n");
fprintf (st, "   sim> SET CPU HISTORY=0               disable history\n");
fprintf (st, "   sim> SET CPU HISTORY=n{:file}        enable history, length = n\n");
fprintf (st, "   sim> SHOW CPU HISTORY                print CPU history\n");
return SCPE_OK;
}

