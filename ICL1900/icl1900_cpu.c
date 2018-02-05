/* icl1900_cpu.c: ICL 1900 cpu simulator

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

   The ICL1900 was a 24 bit CPU that supported either 32Kwords of memory or
   4Mwords of memory, depending on model.

   Level A:   lacked 066, 116 and 117 instructions and 22 bit addressing.
   Level B:   Adds 066, 116 and 117 instructions, but lack 22 bit addressing.
   Level C:   All primary and 22 bit addressing.

   Sub-level 1:  Norm 114,115 available only if FP option.
   Sub-level 2:  Norm 114,115 always available. 
*/

#include "icl1900_defs.h"
#include "sim_timer.h"

#define UNIT_V_MSIZE    (UNIT_V_UF + 0)
#define UNIT_MSIZE      (7 << UNIT_V_MSIZE)
#define MEMAMOUNT(x)    (x << UNIT_V_MSIZE)
#define UNIT_V_MODEL    (UNIT_V_MSIZE + 4)
#define UNIT_MODEL      (0x1f << UNIT_V_MODEL)
#define MODEL(x)        (UNIT_MODEL & (x << UNIT_V_MODEL))


#define TMR_RTC         1

#define HIST_PC         BM1
#define HIST_MAX        50000
#define HIST_MIN        64

/* Level A Primary no 066, 116, 117 15AM and DBM only */
/* Level B All Primary              15AM and DBM only */
/* Level C All Primary              15AM and 22AM, DBM and EBM */

/* Level x1, NORM when FP */
/* Level x2, NORM always */

#define MOD1            0       /* Ax OPT */
#define MOD1A           1       /* A1 OPT 04x -076 111-3 */
#define MOD1S           2       /* Ax OPT 04x -076 111-3 */
#define MOD1T           3       /* Ax OPT 04x -076 111-3 */
#define MOD2            4       /* Ax OPT 04x -076 111-3 */
#define MOD2A           5       /* B1 OPT */
#define MOD2S           6       /* B1 or C1 OPT 04x -076 111-3 */
#define MOD2T           7       /* B1 or C1 OPT 04x -076 111-3 */
#define MOD3            8       /* A1 or A2 OPT 04x -076 111-3 */
#define MOD3A           9       /* B1 or C1 OPT 04x -076 111-3 */
#define MOD3S           10      /* B1 or C1 OPT 04x -076 111-3 */
#define MOD3T           11      /* A1 or A2 OPT */
#define MOD4            12      /* A2 OPT */
#define MOD4A           13      /* C2 OPT */
#define MOD4E           14      /* C2 OPT */
#define MOD4F           12+32
#define MOD4S           15      /* Ax OPT */
#define MOD5            16      /* A2 FP */
#define MOD5A           17      /* Ax FP */
#define MOD5E           18      /* C2 FP */
#define MOD5F           16+32
#define MOD5S           19      /* Ax FP */
#define MOD6            20      /* C2 OPT */
#define MOD6A           21      /* Ax OPT 076 131 */
#define MOD6E           22      /* Ax OPT 076 */
#define MOD6F           20+32
#define MOD6S           23      /* Ax OPT */
#define MOD7            24      /* C2 FP */
#define MOD7A           25      /* Ax FP */
#define MOD7E           26      /* Ax FP */
#define MOD7F           24+32
#define MOD7S           27      /* Ax FP */
#define MOD8            28      /* Ax FP */
#define MOD8A           29      /* Ax FP */
#define MOD8S           30      /* Ax FP */
#define MOD9            31      /* A2 FP */
#define MODXF           32      /* C2 FP */



int                 cpu_index;                  /* Current running cpu */
uint32              M[MAXMEMSIZE] = { 0 };      /* memory */
uint32              RA;                         /* Temp register */
uint32              RB;                         /* Temp register */
uint32              RC;                         /* Instruction Code */
uint32              RD;                         /* Datum pointer */
uint16              RK;                         /* Counter */
uint8               RF;                         /* Function code */
uint32              RL;                         /* Limit register */
uint8               RG;                         /* General register */
uint32              RM;                         /* M field register */
uint32              RN;                         /* Current address */
uint32              RP;                         /* Temp register */
uint32              RS;                         /* Temp register */
uint32              RT;                         /* Temp register */
uint32              MB;                         /* Memory buffer */
uint32              MA;                         /* Memory address */
uint8               RX;                         /* X field register */
uint32              XR[8];                      /* Index registers */
uint32              faccl;                      /* Floating point accumulator low */
uint32              facch;                      /* Floating point accumulator high */
uint8               fovr;                       /* Floating point overflow */
uint8               BCarry;                     /* Carry bit */
uint8               BV;                         /* Overflow flag */
uint8               Mode;                       /* Mode */
uint8               exe_mode = 1;               /* Executive mode */
#define     EJM     040                            /* Extended jump Mode */
#define     DATUM   020                            /* Datum mode */
#define     AM22    010                            /* 22 bit addressing */
#define     EXTRC   004                            /* Executive trace mode */
/*                  002       */                   /* unused mode bit */
#define     ZERSUP  001                            /* Zero suppression */
uint8               OIP;                        /* Obey instruction */
uint8               PIP;                        /* Pre Modify instruction */
uint8               OPIP;                       /* Saved Pre Modify instruction */
uint32              SR1;                        /* Mill timer */
uint32              SR2;                        /* Typewriter I/P */
uint32              SR3;                        /* Typewriter O/P */
uint32              SR64;                       /* Interrupt status */
uint32              SR65;                       /* Interrupt status */
uint32              adrmask;                    /* Mask for addressing memory */


struct InstHistory
{
    uint32    rc;
    uint32    op;
    uint32    ea;
    uint32    xr;
    uint32    ra;
    uint32    rb;
    uint32    rr;
    uint8     c;
    uint8     v;
    uint8     e;
};

struct InstHistory *hst = NULL;
int32               hst_p = 0;
int32               hst_lnt = 0;


t_stat              cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr,
                           int32 sw);
t_stat              cpu_dep(t_value val, t_addr addr, UNIT * uptr,
                            int32 sw);
t_stat              cpu_reset(DEVICE * dptr);
t_stat              cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              cpu_show_size(FILE * st, UNIT * uptr, int32 val,
                                  CONST void *desc);
t_stat              cpu_show_hist(FILE * st, UNIT * uptr, int32 val,
                                  CONST void *desc);
t_stat              cpu_set_hist(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              cpu_help(FILE *, DEVICE *, UNIT *, int32, const char *);
/* Interval timer */
t_stat              rtc_srv(UNIT * uptr);

int32               rtc_tps = 60 ;


/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT                cpu_unit[] =
    {{ UDATA(rtc_srv, MEMAMOUNT(7)|UNIT_IDLE, MAXMEMSIZE ), 16667 }};

REG                 cpu_reg[] = {
    {ORDATAD(C, RC, 22, "Instruction code"), REG_FIT},
    {ORDATAD(F, RF, 7,  "Order Code"), REG_FIT},
    {BRDATAD(X, XR, 8, 24, 8, "Index Register"), REG_FIT},
    {NULL}
};

MTAB                cpu_mod[] = {
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(0), NULL, "4K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(1), NULL, "8K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(3), NULL, "16K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(7), NULL, "32K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(8), NULL, "48K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(9), NULL, "64K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(10), NULL, "96K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(11), NULL, "128K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(12), NULL, "256K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(13), NULL, "512K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(14), NULL, "1024K", &cpu_set_size},
    /* Stevenage */
    {UNIT_MODEL, MODEL(MOD1),  "1901",  "1901",  NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD1A), "1901A", "1901A", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD1S), "1901S", "1901S", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD1T), "1901T", "1901T", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD2),  "1902",  "1902",  NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD2A), "1902A", "1902A", NULL, NULL, NULL},  /* C1 */
    {UNIT_MODEL, MODEL(MOD2S), "1902S", "1902S", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD2T), "1902T", "1902T", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD3),  "1903",  "1903",  NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD3A), "1903A", "1903A", NULL, NULL, NULL},  /* C1 */
    {UNIT_MODEL, MODEL(MOD3S), "1903S", "1903S", NULL, NULL, NULL},
    /* West Gorton */         
    {UNIT_MODEL, MODEL(MOD3T), "1903T", "1903T", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD4),  "1904",  "1904",  NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD4A), "1904A", "1904A", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD4E), "1904E", "1904E", NULL, NULL, NULL},  /* C */
    {UNIT_MODEL, MODEL(MOD4F), "1904F", "1904F", NULL, NULL, NULL},  /* C */
    {UNIT_MODEL, MODEL(MOD4S), "1904S", "1904S", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD5),  "1905",  "1905",  NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD5E), "1905E", "1905E", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD5F), "1905F", "1905F", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD5S), "1905S", "1905S", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD6),  "1906",  "1906",  NULL, NULL, NULL},    /* C */
    {UNIT_MODEL, MODEL(MOD6A), "1906A", "1906A", NULL, NULL, NULL},  /* C */
    {UNIT_MODEL, MODEL(MOD6E), "1906E", "1906E", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD6F), "1906F", "1906F", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD7),  "1907",  "1907",  NULL, NULL, NULL},    /* C */
    {UNIT_MODEL, MODEL(MOD7E), "1907E", "1907E", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD7F), "1907F", "1907F", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD8A), "1908A", "1908A", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD9),  "1909",  "1909",  NULL, NULL, NULL},
    {MTAB_VDV, 0, "MEMORY", NULL, NULL, &cpu_show_size},
    {MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
    {MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
    {MTAB_XTD | MTAB_VDV | MTAB_NMO | MTAB_SHP, 0, "HISTORY", "HISTORY",
     &cpu_set_hist, &cpu_show_hist},
    {0}
};

DEVICE              cpu_dev = {
    "CPU", cpu_unit, cpu_reg, cpu_mod,
    1, 8, 22, 1, 8, 24,
    &cpu_ex, &cpu_dep, &cpu_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, dev_debug, 
    NULL, NULL, &cpu_help
};




uint8 Mem_read(uint32 addr, uint32 *data, uint8 flag) {
    addr &= M22;
    if (addr < 8) {
        *data = XR[addr];
        return 0;
    }
    if (!exe_mode || (flag && (Mode & DATUM) != 0)) {
       addr = (addr + RD) ;//& adrmask;
fprintf(stderr, "access:: %08o %08o\n\r", addr, RL);
    }
    if (!exe_mode && RL && (addr < RD || addr >= RL)) {
       SR64 |= B1;
fprintf(stderr, "rev: %08o\n\r", addr);
       return 1;
    }
    addr &= adrmask;
    if (addr > MEMSIZE) {
       SR64 |= B1;
fprintf(stderr, "mem: %08o\n\r", addr);
       return 1;
    }
    *data = M[addr];
    return 0;
}

uint8 Mem_write(uint32 addr, uint32 *data, uint8 flag) {
    addr &= M22;
    if (addr < 8) {
        XR[addr] = *data;
        return 0;
    }
    if (!exe_mode || (flag && (Mode & DATUM) != 0)) 
       addr = (addr + RD) ; //& adrmask;
    if (!exe_mode && RL && (addr < RD || addr >= RL)) {
       SR64 |= B1;
fprintf(stderr, "wrev: %08o\n\r", addr);
       return 1;
    }
    addr &= adrmask;
    if (addr > MEMSIZE) {
       SR64 |= B1;
fprintf(stderr, "mem: %08o\n\r", addr);
       return 1;
    }
    M[addr] = *data;
    return 0;
}

t_stat
sim_instr(void)
{
    t_stat              reason;
    uint32              temp;
    uint32              t;
    int                 x_reg;
    int                 m;
    int                 n;
    int                 e1,e2;          /* Temp for exponents */

    reason = 0;
    adrmask = (Mode & AM22) ? M22 : M15;

    while (reason == 0) {       /* loop until halted */
       if (sim_interval <= 0) {        /* event queue? */
           reason = sim_process_event();
           if (reason != SCPE_OK) {
                break; /* process */
           }
       }

       if (sim_brk_summ) {
           if(sim_brk_test(RC, SWMASK('E'))) {
               reason = SCPE_STOP;
               break;
           }
       }

intr:
       if (!exe_mode && (SR64 | SR65) != 0) {
            exe_mode = 1;
            /* Store registers */
            Mem_write(RD+13, &facch, 0);  /* Save F.P.U. */
            Mem_write(RD+12, &faccl, 0);  
            RA = 0;         /* Build ZSTAT */
            if (Mode & 1)
                RA |= B3;
            if (OPIP | PIP)
                RA |= B2;
            Mem_write(RD+9, &RA, 0);
            RA = RC;
            if (BV)
              RA |= B0;
            if (BCarry)
              RA |= B1;
#if 0
            if ((Mode & AM22) == 0 && (Mode & 1))
                RA |= B8;
#endif
            Mem_write(RD+8, &RA, 0);
            for (n = 0; n < 8; n++) 
               Mem_write(RD+n, &XR[n], 0);
            Mode = 0;
            adrmask = (Mode & AM22) ? M22 : M15;
            RC = 020;
            PIP = 0;
       }

fetch:
       if (Mem_read(RC, &temp, 0)) {
           if (hst_lnt) {      /* history enabled? */
               hst_p = (hst_p + 1);    /* next entry */
               if (hst_p >= hst_lnt)
                   hst_p = 0;
               hst[hst_p].rc = RC | HIST_PC;
               hst[hst_p].ea = RC;
               hst[hst_p].op = 0;
               hst[hst_p].xr = 0;
               hst[hst_p].ra = 0;
               hst[hst_p].rb = 0;
               hst[hst_p].rr = 0;
               hst[hst_p].c = BCarry;
               hst[hst_p].v = BV;
               hst[hst_p].e = exe_mode;
           }
           RC = (RC + 1) & adrmask;
           goto intr;
       }

obey:
       RM = temp & 037777;
       RF = 0177 & (temp >> 14);
       RX = 07 & (temp >> 21);
       /* Check if branch opcode */
       if (RF >= 050 && RF < 0100) { 
           RA = XR[RX];
           RM = RB = temp & 077777;
           if ((Mode & EJM) && (RF & 1) == 0) {
               RB |= (RB & 020000) ? 017740000 : 0; /* Sign extend RB */
fprintf(stderr, "Rel B: %08o PC=%08o -> ", RB, RC);
               RB = (RB + RC) & adrmask;
fprintf(stderr, " %08o\n\r", RC);
           }
           if (PIP && ((Mode & EJM) == 0 || (RF & 1) == 0)) {
               RB = (RB + RP) & adrmask;
           }
       } else {
           RA = XR[RX];
           m = 03 & (RM >> 12);
           RB = RM & 07777;
           if (PIP) 
             RB = (RB + RP) & adrmask;
           if (m != 0) 
             RB = (RB + XR[m]) & adrmask;
           RS = RB;
           if (RF < 050) {
              if (Mem_read(RS, &RB, 1)) {
                  if (hst_lnt) {      /* history enabled? */
                      hst_p = (hst_p + 1);    /* next entry */
                      if (hst_p >= hst_lnt)
                          hst_p = 0;
                      hst[hst_p].rc = (RC - 1) | HIST_PC;
                      hst[hst_p].ea = RS;
                      hst[hst_p].op = temp;
                      hst[hst_p].xr = XR[RX];
                      hst[hst_p].ra = RA;
                      hst[hst_p].rb = RB;
                      hst[hst_p].rr = RB;
                      hst[hst_p].c = BCarry;
                      hst[hst_p].v = BV;
                      hst[hst_p].e = exe_mode;
                  }
                  RC = (RC + 1) & adrmask;
                  goto intr;
              }
              if (RF & 010) {
                 t = RA;
                 RA = RB;
                 RB = t;
              }
           }
       }
       OPIP = PIP;
       PIP = 0;

       if (hst_lnt) {      /* history enabled? */
           hst_p = (hst_p + 1);    /* next entry */
           if (hst_p >= hst_lnt)
               hst_p = 0;
           hst[hst_p].rc = RC | HIST_PC;
           hst[hst_p].ea = RS;
           hst[hst_p].op = temp;
           hst[hst_p].xr = XR[RX];
           hst[hst_p].ra = RA;
           hst[hst_p].rb = RB;
           hst[hst_p].rr = RB;
           hst[hst_p].c = BCarry;
           hst[hst_p].v = BV;
           hst[hst_p].e = exe_mode;
       }

       /* Advance to next location */
       if (RF != 023)
           RC = (RC + 1) & adrmask;
       OIP = 0;

       switch (RF) {
       case OP_LDX:          /* Load to X */
       case OP_LDXC:         /* Load into X with carry */
       case OP_LDN:          /* Load direct to X */
       case OP_LDNC:         /* Load direct into X with carry */
       case OP_STO:          /* Store contents of X */
       case OP_STOC:         /* Store contents of X with carry */
       case OP_NGS:          /* Negative into Store */
       case OP_NGSC:         /* Negative into Store with carry */
       case OP_NGN:          /* Negative direct to X */
       case OP_NGNC:         /* Negative direct to X with carry */
       case OP_NGX:          /* Negative to X */
       case OP_NGXC:         /* Negative to X with carry */
                     RA = 0;
                     /* Fall through */

       case OP_SBX:          /* Subtract from X */
       case OP_SBXC:         /* Subtract from X with carry */
       case OP_SBS:          /* Subtract from store */
       case OP_SBSC:         /* Subtract from store with carry */
       case OP_SBN:          /* Subtract direct from X */
       case OP_SBNC:         /* Subtract direct from X with carry */
       case OP_ADX:          /* Add to X */
       case OP_ADXC:         /* Add to X with carry */
       case OP_ADN:          /* Add direct to X */
       case OP_ADNC:         /* Add direct to X with carry */
       case OP_ADS:          /* Add X to store */
       case OP_ADSC:         /* Add X to store with carry */
                     if (RF & 02) {
                         RB = RB ^ FMASK;
                         BCarry = !BCarry;
                     }
                     t = (RA & B0) != 0;
                     RA = RA + RB + BCarry;
                     if (RF & 04) {
                        if (RF & 02)
                            BCarry = (RA & BM1) == 0;
                        else
                            BCarry = (RA & B0) != 0;
                        RA &= M23;
                     } else {
                        int t2 = (RB & B0) != 0;
                        int tr = (RA & B0) != 0;
                        if ((t && t2 && !tr) || (!t && !t2 && tr)) 
                           BV = 1;
                        BCarry = 0;
                     }
                     RA &= FMASK;
                     if (RF & 010) {
                         if (Mem_write(RS, &RA, 1)) {
                             goto intr;
                         }
                     } else {
                         XR[RX] = RA;
                     }
                     break;

       case OP_ANDX:         /* Logical AND into X */
       case OP_ANDS:         /* Logical AND into store */
       case OP_ANDN:         /* Logical AND direct into X */
                     RA = RA & RB;
                     BCarry = 0;
                     if (RF & 010) {
                         if (Mem_write(RS, &RA, 1)) {
                             goto intr;
                         }
                     } else {
                         XR[RX] = RA;
                     }
                     break;

       case OP_ORX:          /* Logical OR into X */
       case OP_ORS:          /* Logical OR into store */
       case OP_ORN:          /* Logical OR direct into X */
                     RA = RA | RB;
                     BCarry = 0;
                     if (RF & 010) {
                         if (Mem_write(RS, &RA, 1)) {
                             goto intr;
                         }
                     } else {
                         XR[RX] = RA;
                     }
                     break;

       case OP_ERX:          /* Logical XOR into X */
       case OP_ERS:          /* Logical XOR into store */
       case OP_ERN:          /* Logical XOR direct into X */
                     RA = RA ^ RB;
                     BCarry = 0;
                     if (RF & 010) {
                         if (Mem_write(RS, &RA, 1)) {
                             goto intr;
                         }
                     } else {
                         XR[RX] = RA;
                     }
                     break;

       case OP_OBEY:         /* Obey instruction at N */
                     temp = RB;
                     OIP = 1;
                     goto obey;

       case OP_LDCH:         /* Load Character to X */
                     m = (m == 0) ? 3 : (XR[m] >> 22) & 3;
                     RA = RB >> (6 * (3 - m));
                     RA = XR[RX] = RA & 077;
                     BCarry = 0;
                     break;

       case OP_LDEX:         /* Load Exponent */
                     RA = XR[RX] = RB & M9;
                     BCarry = 0;
                     break;

       case OP_TXU:          /* Test X unequal */
                     if (RA != RB)
                       BCarry = 1;
                     break;

       case OP_TXL:          /* Test X Less */
                     RB += BCarry;
                     if (RB != RA)
                         BCarry = (RB > RA);
                     break;

       case OP_STOZ:         /* Store Zero */
                     RA = 0;
                     BCarry = 0;
                     if (Mem_write(RS, &RA, 1)) {
                         goto intr;
                     }
                     break;

       case OP_DCH:          /* Deposit Character to X */
                     m = (m == 0) ? 3 : (XR[m] >> 22) & 3;
                     m = 6 * (3 - m);
                     RB = (RB & 077) << m;
                     RA &= ~(077 << m);
                     RA |= RB;
                     BCarry = 0;
                     if (Mem_write(RS, &RA, 1)) {
                         goto intr;
                     }
                     break;

       case OP_DEX:          /* Deposit Exponent */
                     RA &= ~M9;
                     RA = RA | (RB & M9);
                     BCarry = 0;
                     if (Mem_write(RS, &RA, 1)) {
                         goto intr;
                     }
                     break;

       case OP_DSA:          /* Deposit Short Address */
                     RA &= ~M12;
                     RA |= (RB & M12);
                     BCarry = 0;
                     if (Mem_write(RS, &RA, 1)) {
                         goto intr;
                     }
                     break;

       case OP_DLA:          /* Deposit Long Address */
                     RA &= ~M15;
                     RA |= (RB & M15);
                     BCarry = 0;
                     if (Mem_write(RS, &RA, 1)) {
                         goto intr;
                     }
                     break;

       case OP_MPY:          /* Multiply */
       case OP_MPR:          /* Multiply and Round  */
       case OP_MPA:          /* Multiply and Accumulate */
                     if (RA == B0 && RB == B0) {
                         if (RF != OP_MPA || (XR[(RX + 1) & 7] & B0) == 0)
                             BV = 1;
                     }
                     RP = RA;
                     RA = RB;
                     t = RP & 1;
                     RP >>= 1;
                     if (RF & 1)  /* Multiply and Round  */
                         RP |= B0;
                     RB = 0; 
                     for(RK = 23; RK != 0; RK--) {
                        if (t) 
                           RB += RA;
                        t = RP & 1;
                        RP >>= 1;
                        if (RB & 1)
                           RP |= B0;
                        if (RB & B0)
                           RB |= BM1;
                        RB >>= 1;
                     }
        
                     if (t) {
                         RB += (RA ^ FMASK) + 1;
                     }
                     t = RP & 1;         /* Check for MPR */
                     if (t && RP & B0)
                        RB++;
                     RP >>= 1;
                     if (RF == OP_MPA) {
                         RA = XR[(RX + 1) & 7];
                         RP += RA;
                         if (RA & B0)
                             RB--;
                         else if (RP & B0)
                             RB++;
                     }
         
                     XR[RX] = RB & FMASK;
                     RA = XR[(RX+1) & 7] = RP & M23;
                     BCarry = 0;
                     break;

       case OP_CDB:          /* Convert Decimal to Binary */
                     m = (m == 0) ? 3 : (XR[m] >> 22) & 3;
                     RB = (RB >> (6 * (3 - m))) & 077;
                     if (RB > 9) {
                        BCarry = 1;
                        break;
                     }
                     /* Fall through */

       case OP_CBD:          /* Convert Binary to Decimal */
                     RT = RB;
                     RB = XR[(RX+1) & 7];
                     /* Multiply by 10 */
                     RB <<= 2;
                     RA <<= 2;
                     RA |= (RB >> 23) & 07;
                     RB &= M23;
                     RB += XR[(RX+1) & 7];
                     if (RB & B0)
                        RA++;
                     RA += XR[RX];
                     RB <<= 1;
                     RA <<= 1;
                     if (RB & B0)
                        RA++;
                     RB &= M23;
                     if (RF == OP_CDB) {
                        /* Add in RT */
                        RB += RT;
                        if (RB & B0)
                           RA++;
                        RB &= M23;
                        if (RA & ~(M23))
                           BV = 1;
                        RA &= M23;
                     } else {
                        /* Save bits over 23 to char */
                        m = (m == 0) ? 3 : (XR[m] >> 22) & 3;
                        m = 6 * (3 - m);
                        RP = (RA >> 23) & 017;
                        if ((Mode & 1) != 0 && RP == 0) 
                            RP = 020;
                        else 
                            Mode &= ~1;
                        RA &= M23;
                        RT &= ~(077 << m);
                        RT |= (RP << m);
                        if (Mem_write(RS, &RT, 1)) {
                            goto intr;
                        }
                     }
                     XR[(RX+1) & 7] = RB;
                     XR[RX] = RA;
                     break;

       case OP_DVD:          /* Unrounded Double Length Divide */
       case OP_DVR:          /* Rounded Double Length Divide */
       case OP_DVS:          /* Single Length Divide */
                     /* Load X and X* */
                     RP = RA;
                     RA = RB;
                     RB = RP;
                     RB = XR[RX];
                     RP = XR[(RX+1) & 07];
                     if (RF == OP_DVS)    /* Make double length */
                         RB = (RP & B0) ? FMASK : 0;
                     RP &= M23;
fprintf(stderr, "DVD: %08o %08o %08o - %3o C=%08o\n\r", RA, RP, RB, RF, RC);
                     /* Save Divisor */
                     RS = RA; 
                     /* Get Absolute of divisor  */
                     if ((RA & B0) == 0) {
                         if (RA == 0) {  /* Zero divisor */
                             BV = 1;
                             BCarry = 0;
                             break;
                         }
                         RA = (RA ^ FMASK) + 1;  /* Form -DIVISOR */
                     }
fprintf(stderr, "DVD1: %08o %08o %08o \n\r", RA, RP, RB);
                     /* Set flag based on sign */
                     BCarry = (RB & B0) != 0;
                     RT = (RA ^ FMASK) + 1;      /* Form +DIVISOR */

                     /* Main loop */
                     for(RK = 24; RK != 0; RK--) {
                         RB = RB + ((BCarry)? RA: RT);  /* Add/Sub divisor as previous sign */
                         RP <<= 1;
                         if (RB & BM1) {
                             BCarry = !BCarry;
                             RP |= 1;
                         }
                         RB <<= 1;
                         if (RP & B0)
                             RB |= 1;
                         RB &= FMASK;
                         RP &= FMASK;
fprintf(stderr, "DVD2: %08o %08o %08o \n\r", RA, RP, RB);
                     }
                     /* 24th iteration */
                     RB = RB + ((BCarry)? RT: RA);  /* Add/Sub divisor as previous sign */
                     RP <<= 1;
                     if (RB & BM1) {
                         BCarry = !BCarry;
                         RP |= 1;
                     }
                     RT = B0 + ((RP & B0) != 0);
                     RB = RB + ((BCarry)? RT: RA);  /* Add/Sub divisor as previous sign */
fprintf(stderr, "DVD3: %08o %08o %08o \n\r", RA, RP, RB);

                     BCarry = (RP ^ B0) != 0;
                     if ((RS & B0) == 0) {
                         RP ^= FMASK;
                         RB = RB + RS;
                     } else if (RS != RB) {
rnd:
                         RB = RB - RS;
                         BV = 0;
                         RP = RP + 1;
                         if (RP & BM1) {
                             BV = 1;
                             BCarry = !BCarry;
                         }
                         goto edex;
                     }
fprintf(stderr, "DVD4: %08o %08o %08o \n\r", RA, RP, RB);

                     if ((RF & 1) == 1 && RB != 0) {
                         temp = RB;
                         temp += (RS ^ FMASK) + 1;
                         temp += RB;
                         temp ^= temp;
                         if ((temp & B0) == 0)
                             goto rnd;
                     }

edex:
fprintf(stderr, "DVD5: %08o %08o %08o \n\r", RA, RP, RB);
                     if (BCarry)
                        BV = 0;
#if 0
                     RP = XR[(RX+1) & 7];            /* VR */
                     RA = RB;  /* Divisor to RA */
                     RB = XR[RX];  /* Dividend to RB/RP */
fprintf(stderr, "DVD: %08o %08o %08o - %3o C=%08o\n\r", RA, RP, RB, RF, RC);
if (RA == FMASK && RP == 1 && RB == 0) {
 XR[RX] = 0;
 XR[(RX+1) & 7] = FMASK;
 break;
}
     
                     if (RA == 0) { /* Exit on zero divisor */  /* VI */
                        BV = 1;
                        BCarry = 0;
                        break;
                     }
                     BCarry = (RP & B0) != 0;
                     temp = (RP | RB) == 0;   /* Save zero dividend */

                     /* Setup for specific divide order code */ /* V11 */
                     if (RF & 2) {     /* DVS */
                        if (BCarry) {
                           RB = FMASK;
                        } else {
                           RB = 0;
                        }
                     }
                     RP <<= 1;
                     RP &= FMASK;
                     BCarry = 0;
fprintf(stderr, "DVD1: %08o %08o %08o \n\r", RA, RP, RB);

                     /* First partial remainder */   /* V12 */
                     if (((RB ^ RA) & B0) == 0) {
                        t = RB + (RA ^ FMASK) + 1; 
                        RK=1;
                     } else {
                        t = RB + RA;
                        RK=0;
                     }
                     if (((t ^ RA) & B0) != 0) 
                         BCarry = 1;
                     BCarry = RK != BCarry;
                     RP <<= 1;
                     if (((t ^ RA) & B0) == 0) {
                         RP |= 1;
                     }
                     RB = t << 1;
                     if (RP & BM1)
                         RB |= 1;
                     RB &= FMASK;
                     RP &= FMASK;
fprintf(stderr, "DVD2: %08o %08o %08o \n\r", RA, RP, RB);

                     /* Main divide loop */         /* V13 */
                     for (RK = 22; RK != 0; RK--) {
                         if (((t ^ RA) & B0) == 0) {
                             t = RB + (RA ^ FMASK) + 1;
                         } else {
                             t = RB + RA;
                         }
                         RP <<= 1;
                         if (((t ^ RA) & B0) == 0) {
                            RP |= 1;
                         }
                         RB = t << 1;
                         if (RP & BM1)
                            RB |= 1;
                         RB &= FMASK;
                         RP &= FMASK;
fprintf(stderr, "DVD3: %08o %08o %08o \n\r", RA, RP, RB);
                     }

                     /* Final product */
                     if (((t ^ RA) & B0) == 0) {   /* V14 */
                         t = RB + (RA ^ FMASK) + 1;
                     } else {
                         t = RB + RA;
                     }
                     RP <<= 1;
                     if (((t ^ RA) & B0) == 0) {
                        RP |= 1;
                     }
                     RP &= FMASK;
fprintf(stderr, "DVD4: %08o %08o %08o \n\r", RA, RP, RB);

                     /* Final Remainder */
                     if (RP & 1) {
                         RB = (RB + (RA ^ FMASK) + 1) & FMASK;
                     } else {
                         RB = (RB + RA) & FMASK;
                     }
                     /* End correction */
                     if ((RP & 1) == 0) {
                         RB = (RB + RA) & FMASK;
                     }
fprintf(stderr, "DVD5: %08o %08o %08o \n\r", RA, RP, RB);
                     /* Form final partial product */
                     if (RA & B0) {
                        t = (RB + (RA ^ FMASK) + 1) & FMASK;
fprintf(stderr, "DVD5: %08o %08o %08o %08o\n\r", RA, RP, RB, RT);
                        if (t == 0) {
                            RB = 0;
                            goto dvd1;
                        }
                     }
                     if ((RF & 1) == 0)    /* DVR */
                         goto dvd2;
                     if (RB == 0)
                         goto dvd2;
                     RT = RB + (RA ^ FMASK) + 1;
fprintf(stderr, "DVDA: %08o %08o %08o %08o \n\r", RA, RP, RB, RT);
                     RA = RB;
                     if ((((RT + RA) ^ RA) & B0) != 0)
                         goto dvd2;
                     RB = RT & FMASK;
dvd1:
fprintf(stderr, "DVD6: %08o %08o %08o \n\r", RA, RP, RB);
                     RT = RP;
                     RP++;
                     if ((RT ^ RP) & B0)
                         BCarry = !BCarry;
                     if (RP & BM1)
                         BCarry = 1;
dvd2:
fprintf(stderr, "DVD7: %08o %08o %08o \n\r", RA, RP, RB);
                     if (temp)
                         BCarry = 0;
                     if (BCarry)
                         BV = 1;
#endif
                     BCarry = 0;
                     XR[RX] = RB & FMASK;
                     XR[(RX+1) & 7] = RP & FMASK;
                     break;
       case OP_BZE:          /* Branch if X is Zero */
       case OP_BZE1: 
                     BCarry = 0;
                     if (RA == 0)
                         goto branch;
                     break;

       case OP_BNZ:          /* Branch if X is not Zero */
       case OP_BNZ1:
                     BCarry = 0;
                     if (RA != 0)
                         goto branch;
                     break;

       case OP_BPZ:          /* Branch if X is Positive or zero */
       case OP_BPZ1:
                     BCarry = 0;
                     if ((RA & B0) == 0)
                         goto branch;
                     break;

       case OP_BNG:          /* Branch if X is Negative */
       case OP_BNG1: 
                     BCarry = 0;
                     if ((RA & B0) != 0)
                         goto branch;
                     break;

       case OP_BUX:          /* Branch on Unit indexing */
       case OP_BUX1:
                     BCarry = 0;
                     if (Mode & AM22) {
                        RA = ((RA+1) & M22) | (RA & CMASK);
                        XR[RX] = RA;
                        goto branch;
                     } else {
                        RS = CNTMSK + RA;  /* Actualy a subtract 1 */
                        RS &= CNTMSK;
                        RA = ((RA + 1) & M15) | RS;
                     }
                     XR[RX] = RA;
                     if (RS != 0)
                        goto branch;
                     break;

       case OP_BDX:          /* Branch on Double Indexing */
       case OP_BDX1:
                     BCarry = 0;
                     if (Mode & AM22) {
                        RA = ((RA+2) & M22) | (RA & CMASK);
                        XR[RX] = RA;
                        goto branch;
                     } else {
                        RS = CNTMSK + RA;  /* Actualy a subtract 1 */
                        RS &= CNTMSK;
                        RA = ((RA + 2) & M15) | RS;
                     }
                     XR[RX] = RA;
                     if (RS != 0)
                        goto branch;
                     break;

       case OP_BCHX:         /* Branch on Character Indexing */
       case OP_BCHX1:
                     BCarry = 0;
                     RA += 020000000;
                     n = (RA & BM1) != 0;
                     if (Mode & AM22) {
                        RA = ((RA + n) & M22) | (RA & CMASK);
                        XR[RX] = RA;
                        goto branch;
                     } else {
                        RS = CHCMSK + RA;  /* Actually a subtract 1 */
                        RS &= CHCMSK;
                        RA = ((RA + n) & M15) | RS | (RA & CMASK);
                     }
                     XR[RX] = RA;
                     if (RS != 0)
                        goto branch;
                     break;

        /* Not on A */
       case OP_BCT:          /* Branch on Count - BC */
       case OP_BCT1:
                     BCarry = 0;
                     if (Mode & AM22) {
                        RA = ((RA-1) & M22) | (RA & CMASK);
                        RS = RA & M22;
                     } else {
                        RA = ((RA - 1) & M15) | (CNTMSK & RA);
                        RS = RA & M15;
                     }
                     XR[RX] = RA;
                     if (RS != 0)
                        goto branch;
                     break;

       case OP_CALL:         /* Call Subroutine */
       case OP_CALL1:
                     RA = RC;
                     if (BV)
                       RA |= B0;
                     if ((Mode & (AM22|EJM)) == 0) {
                        if (Mode & 1)
                           RA |= B8;
                     } else {
                        if (Mode & 1)
                           RA |= B1;
                     }
                     BV = 0;
                     BCarry = 0;
                     XR[RX] = RA;
branch:
                     if ((Mode & EJM) != 0) {
                         if ((RF & 1) != 0) {
                             RB &= 037777;
fprintf(stderr, "Rep: %08o ->", RB);
                             if (Mem_read(RB, &RB, 0)) {
                                 goto intr;
                             }
fprintf(stderr, " %08o \n\r", RB);
                             RB &= adrmask;
                             if (OPIP) 
                                 RB = (RB + RP) & adrmask;
                         }
                     } 
                     if (hst_lnt) {      /* history enabled? */
                         hst[hst_p].ea = RB;
                     }
                     if (Mem_read(RB, &temp, 0)) {
                         goto intr;
                     }
    /* Monitor mode 2 -> Exec Mon */
    /* Read address to store from location 262. */
    /* Store address transfer address at location, increment 262 mod 128 */
                     RC = RB;
   /* Monitor mode 3 -> int */
                     goto obey;

       case OP_EXIT:         /* Exit Subroutine */
       case OP_EXIT1:
                     if (RA & B0)
                        BV = 1;
                     Mode &= ~1;
                     if ((Mode & (AM22|EJM)) == 0) {
                        if (RA & B8) 
                           Mode |= 1;
                     } else {
                        if (RA & B1) 
                           Mode |= 1;
                     }
                     BCarry = 0;
                     RM |= (RM & 020000) ? 017740000 : 0; /* Sign extend RM */
                     RA = RA + RM;
                     if (PIP)
                         RA += RP;
                     if (Mem_read(RA, &temp, 0)) {
                         goto intr;
                     }
                     RC = RA & adrmask;
                     goto obey;
                     
       case OP_BRN:          /* Branch unconditional */
       case OP_BRN1:
                    /* If priorit mode -> 164 */
                    switch(RX) {
                    case 0:  /* BRN */
                          goto branch;

                    case 1:  /* BVS */
                          if (BV) 
                              goto branch;
                          break;

                    case 2:  /* BVSR */
                          n = BV;
                          BV = 0;
                          if (n)
                              goto branch;
                          break;

                    case 3:   /* BVC */
                          if (BV == 0)
                              goto branch;
                          break;

                    case 4:   /* BVCR */
                          if (BV == 0)
                              goto branch;
                          BV = 0;
                          break;

                    case 5:  /* BCS */
                          n = BCarry;
                          BCarry = 0;
                          if (n) 
                              goto branch;
                          break;

                    case 6:  /* BCC */
                          n = BCarry;
                          BCarry = 0;
                          if (!n)
                              goto branch;
                          break;

                    case 7:   /* BVC */
                          n = BV;
                          BV = !BV;
                          if (n == 0)
                              goto branch;
                          break;
                    }
                    break;

       /* B with Floating or C */
       case OP_BFP:          /* Branch state of floating point accumulator */
       case OP_BFP1:
                    switch (RX & 06) {
                    case 0:  n = (faccl | facch) != 0; break;
                    case 2:  n = (faccl & B0) != 0; break;
                    case 4:  n = fovr; break;
                    case 6:  SR64 |= B1; goto intr;
                    }
                    if (n == (RX & 1)) 
                        goto branch;
                    break;

       case OP_SLL:          /* Shift Left */
                    m = (RB >> 10) & 03;
                    RK = RB & 01777;
                    BCarry = 0;
                    while (RK != 0) {
                       t = 0;
                       switch (m) {
                       case 0: t = (RA & B0) != 0; break;
                       case 1: break;
                       case 2:
                       case 3: temp = RA & B0;
                       }
                       RA <<= 1;
                       RA |= t;
                       if ((m & 2) && temp != (RA & B0))
                          BV = 1;
                       RA &= FMASK;
                       RK--;
                    }
                    XR[RX] = RA;
                    break;

       case OP_SLD:          /* Shift Left Double */
                    m = (RB >> 10) & 03;
                    RK = RB & 01777;
                    BCarry = 0;
                    RB = XR[(RX+1) & 07];
                    while (RK != 0) {
                       switch (m) {
                       case 0: 
                               RB <<= 1;
                               RA <<= 1;
                               if (RA & BM1)
                                   RB |= 1;
                               if (RB & BM1)
                                   RA |= 1;
                               break;
                       case 1: 
                               RB <<= 1;
                               RA <<= 1;
                               if (RB & BM1)
                                   RA |= 1;
                               break;
                       case 2:
                       case 3:
                               RB <<= 1;
                               RA <<= 1;
                               if (RB & B0)
                                   RA |= 1;
                               RB &= M23;
                               t = (RA & B0) != 0;
                               temp = (RA & BM1) != 0;
                               if (t != temp)
                                   BV = 1;
                       }
                       RA &= FMASK;
                       RB &= FMASK;
                       RK--;
                    }
                    XR[RX] = RA;
                    XR[(RX+1) & 07] = RB;
                    break;

       case OP_SRL:          /* Shift Right */
                    m = (RB >> 10) & 03;
                    RK = RB & 01777;
                    t = RA & B0;
                    BCarry = 0;
                    switch(m) {
                    case 0: break;
                    case 1: t = 0; break;
                    case 2: break;
                    case 3: if (BV) {
                                t = B0 ^ t;
                                BV = 0;
                            }
                    }
                    while (RK != 0) {
                       if (m == 0)
                          t = (RA & 1) ? B0 : 0;
                       temp = RA & 1;
                       RA >>= 1;
                       RA |= t;
                       RK--;
                    }
                    if (m > 1 && temp == 1)
                      RA = (RA + 1) & FMASK;
                    XR[RX] = RA;
                    break;

       case OP_SRD:          /* Shift Right Double */
                    m = (RB >> 10) & 03;
                    RK = RB & 01777;
                    RB = XR[(RX+1) & 07];
                    BCarry = 0;
                    t = RA & B0;
                    if (m == 3 && RK != 0 && BV)  {
                         t = B0^t;
                         BV = 0;
                    }
                    while (RK != 0) {
                       switch (m) {
                       case 0:
                               if (RA & 1)
                                  RB |= BM1;
                               if (RB & 1)
                                  RA |= BM1;
                               RA >>= 1;
                               RB >>= 1;
                               break;
                       case 1:
                               RB >>= 1;
                               if (RA & 1)
                                  RB |= B0;
                               RA >>= 1;
                               break;
                       case 2:
                       case 3:
                               RB >>= 1;
                               if (RA & 1)
                                  RB |= B1;
                               RA >>= 1;
                               RA |= t;
                       }
                       RK--;
                    }
                    XR[RX] = RA;
                    XR[(RX+1) & 07] = RB;
                    break;

       case OP_NORM:         /* Nomarlize Single -2 +FP */
       case OP_NORMD:        /* Normalize Double -2 +FP */
                    RT = RB;
                    RB = (RF & 1) ? XR[(RX+1) & 07] & M23 : 0;
                    if (RT & 04000) {
                         RT = 0;
                    } else {
                         RT &= 01777;
                    }
                    if (RT == 0) {
                        RA = RB = 0;
                    } else if (BV) {
                        RT++;
                        t = (RA & B0) ^ B0;
                        if (RA & 1 && RF & 1)
                           RB |= B0;
                        RB >>= 1;
                        RA >>= 1;
                        RA |= t;
                    } else if (RA != 0 || RB != 0) {
                        /* Shift left until sign and B1 not same */
                        while ((((RA >> 1) ^ RA) & B1) == 0) {
                           RT--;
                           RA <<= 1;
                           if (RB & B1)
                             RA |= 1;
                           RB <<= 1;
                           RA &= FMASK;
                           RB &= M23;
                        }
                        /* Check for overflow */
                        if (RT & B0) { /* < 0 */
                           RA = RB = 0;
                           goto norm1;
                        }
                    } else
                        RT = 0;
                    /* Round RB if needed */
                    RP = RB;
                    RB += 0400;
                    if (RB & B0)  {
                        RB = RP;
                        t = (RA & M23) +1;
                        if (t & B0) 
                           RA = RB = 0;
                    }
                    RB = (RB & MMASK) | (RT & M9);
                    BV = 0;
                    if (RT > M9)    /* Exponent overlfow */
                        BV = 1;
 norm1:
                    XR[(RX+1) & 07] = RB;
                    XR[RX] = RA;
                    break;

        /* Not on A*/
       case OP_MVCH:         /* Move Characters - BC */
                     RK = RB;
                     RB = XR[(RX+1) & 07];
                     do {
                         if (Mem_read(RA, &RT, 0)) {
                             goto intr;
                         }
                         m = (RA >> 22) & 3;
                         RT = (RT >> (6 * (3 - m))) & 077;
                         if (Mem_read(RB, &RS, 0)) {
                             goto intr;
                         }
                         m = (RB >> 22) & 3;
                         m = 6 * (3 - m);
                         RS &= ~(077 << m);
                         RS |= (RT & 077) << m;
                         if (Mem_write(RB, &RS, 0)) {
                             goto intr;
                         }
                         RA += 020000000;
                         m = (RA & BM1) != 0;
                         RA = ((RA + m) & M22) | (RA & CMASK);
                         RB += 020000000;
                         m = (RB & BM1) != 0;
                         RB = ((RB + m) & M22) | (RB & CMASK);
                         RK = (RK - 1) & 0777;
                      } while (RK != 0);
                      XR[RX] = RA;
                      XR[(RX+1)&07] = RB;
                      break;

        /* Not on A*/
       case OP_SMO:          /* Supplementary Modifier - BC  */
                     if (OPIP) {      /* Error */
                         SR64 |= B1;
                         goto intr;
                     }
                     if (Mem_read(RS, &RP, 1)) {
                         goto intr;
                     }
                     PIP = 1;
                     break;

       case OP_NULL:         /* No Operation */
                     break;

       case OP_LDCT:         /* Load Count */
                     RA = CNTMSK & (RB << 15);
                     XR[RX] = RA;
                     break;

       case OP_MODE:         /* Set Mode */
                     if (exe_mode) 
                        Mode = RB & 077;
                     else
                        Mode = (Mode & 076) | (RB & 1);
                     break;

       case OP_MOVE:         /* Copy N words */
                     RK = RB;
                     RA &= adrmask;
                     RB = XR[(RX+1) & 07] & adrmask;
                     do {
                         if (Mem_read(RA, &RT, 0)) {
                             goto intr;
                         }
                         if (Mem_write(RB, &RT, 0)) {
                             goto intr;
                         }
                         RA++;
                         RB++;
                         RK = (RK - 1) & 0777;
                     } while (RK != 0);
                     break;

       case OP_SUM:          /* Sum N words */
                     RK = RB;
                     RA = 0;
                     RB = XR[(RX+1) & 07] & adrmask;
                     do {
                         if (Mem_read(RB, &RT, 0)) {
                             goto intr;
                         }
                         RA = (RA + RT) & FMASK;
                         RB++;
                         RK = (RK - 1) & 0777;
                     } while (RK != 0);
                     XR[RX] = RA;
                     break;

/* B or C with Floating Point */
       case OP_FLOAT:        /* Convert Fixed to Float +FP */
                     if (Mem_read(RA, &RB, 0)) {
                        goto intr;
                     }
                     RB++;
                     if (Mem_read(RB, &RB, 0)) {
                        goto intr;
                     }
                     facch = RA;
                     faccl = RB;
                     fovr = (RB & B0) != 0;
                     RT = 279;
                     /* Check for zero or -1 */
                     if ((faccl & M23) == (facch & M23)) {
                         if (facch == 0) 
                             break;
                         if (facch == FMASK) {
                             facch &= MMASK;
                             facch |= RT;
                             break;
                         }
                     }

                     /* Shift left until sign and B1 not same */
                     while ((((facch >> 1) ^ facch) & B1) == 0) {
                        RT--;
                        faccl <<= 1;
                        if (facch & B1)
                          faccl |= 1;
                        facch <<= 1;
                        faccl &= FMASK;
                        facch &= M23;
                    }
                    facch &= MMASK;
                    facch |= RT & M9;
                    if (RT < 0) {
                       fovr = 1;
                       facch |= B0;
                    }
                    break;

       case OP_FIX:          /* Convert Float to Fixed +FP */
                    RA = faccl;
                    RB = facch & MMASK;
                    RT = 279 - (facch & M9);
                    if (RT > 0) {
                       while (RT > 0) {
                         if (RA & 1) 
                             RB |= B0;
                         if (RA & B0)
                             RA |= BM1;
                         RA >>= 1;
                         RB >>= 1;
                         RT--;
                       }
                       while (RT < 0 && (((RA >> 1) ^ RA) & B1) == 0) {
                         RA <<= 1;
                         if (RB & B1)
                           RA |= 1;
                         RB <<= 1;
                         RA &= FMASK;
                         RB &= M23;
                         RT++;
                       }
                    }
                    if (RT != 0 || fovr)
                        BV = 1;
                    if (Mem_write(RS, &RA, 0)) {
                       goto intr;
                    }
                    RS++;
                    if (Mem_write(RS, &RB, 0)) {
                       goto intr;
                    }
                    break;

       case OP_FAD:          /* Floating Point Add +FP */
       case OP_FSB:          /* Floating Point Subtract +FP */
                    if (Mem_read(RS, &RA, 0)) {
                        goto intr;
                    }
                    RS++;
                    if (Mem_read(RS, &RB, 0)) {
                        goto intr;
                    }
fprintf(stderr, "FAD0: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                    fovr |= (RB & B0) != 0;
                    RB &= M23;
                    if (RX & 4) { /* See if we should swap operands */
                        RT = facch;
                        facch = RA;
                        RA = RT;
                        RT = faccl;
                        faccl = RB;
                        RB = RT;
fprintf(stderr, "FAD1: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                    }
                    if (RF == OP_FSB) { /* If subtract invert RA&RB */
                        RA ^= FMASK;
                        RB ^= MMASK;
                        RB += 01000;
                        if (RB & B0)
                           RA = (RA + 1) & FMASK;
                        RB &= M23;
fprintf(stderr, "FAD2: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                    }
                    e1 = (facch & M9);
                    facch &= MMASK;
                    e2 = (RB & M9);
                    RB &= MMASK;
                    n = e1 - e2;
fprintf(stderr, "FADx: %03o %03o %d\n\r", e1, e2, n);
                    if (n < 0) {
                        e1 = e2;
                        if (n < -37) {  /* See if more then 37 bits difference */
                           faccl = RA;
                           facch = RB | e1;
                           break;
                        }
                        while(n < 0) {
                           if (faccl & B0)
                              faccl |= BM1;
                           if (faccl & 1)
                              facch |= B0;
                           facch >>= 1;
                           faccl >>= 1;
                           n++;
fprintf(stderr, "FAD3: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                        }
                    } else if (n > 0) {
                        if (n > 37) { /* See if more then 37 bits difference */
                            facch |= e1;
                            break;
                        }
                        while(n > 0) {
                           if (RA & B0)
                              RA |= BM1;
                           if (RA & 1)
                              RB |= B0;
                           RA >>= 1;
                           RB >>= 1;
                           n--;
fprintf(stderr, "FAD4: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                        }
                    }
fprintf(stderr, "FAD5: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                    if (facch & B0)
                        facch |= BM1;
                    if (RA & B0)
                        RA |= BM1;
                    faccl += RA;
                    facch += RB;
fprintf(stderr, "FAD6: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                    if (facch & B0) {
                        facch &= M23;
                        faccl ++;
                    }
                    if (faccl & B0)
                       faccl |= BM1;
                    if (faccl & 1)
                       facch |= B0;
                    faccl >>= 1;
                    facch >>= 1;
                    e1--;
fprintf(stderr, "FAD7: %08o %08o %08o %08o %08o %03o\n\r", RA, RB, RC, faccl, facch, e1);
                    if ((facch | faccl) == 0) 
                         break;
                    if (faccl == FMASK && (facch & MMASK) == MMASK) {
                         facch &= MMASK;
                         facch |= e1;
                         break;
                    }
fprintf(stderr, "FAD8: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                    /* Shift left until sign and B1 not same */
                    while ((((faccl >> 1) ^ faccl) & B1) == 0) {
                        e1++;
                        facch <<= 1;
                        faccl <<= 1;
                        if (facch & B0)
                          faccl |= 1;
                        faccl &= FMASK;
                        facch &= M23;
fprintf(stderr, "FAD9: %08o %08o %08o %08o %08o %03o\n\r", RA, RB, RC, faccl, facch, e1);
                    }
                    if (e1 < 0) 
                       fovr = 1;
                    facch &= MMASK;
                    facch |= e1 & M9;
fprintf(stderr, "FADA: %08o %08o %08o %08o %08o %03o\n\r", RA, RB, RC, faccl, facch, e1);
                    break;

       case OP_FMPY:         /* Floating Point Multiply +FP */
                    break;

       case OP_FDVD:         /* Floating Point Divide +FP */
                    break;

       case OP_LFP:          /* Load Floating Point +FP */
                    if (RX & 1) {
                       faccl = facch = fovr = 0;
fprintf(stderr, "LFP: %08o %08o %o\n\r", faccl, facch, RX);
                       break;
                    }
                    if (Mem_read(RB, &RA, 0)) {
                        goto intr;
                    }
                    RB++;
                    if (Mem_read(RB, &RS, 0)) {
                        goto intr;
                    }
                    faccl = RA;
                    facch = RS & M23;
                    fovr = (RS & B0) != 0;
fprintf(stderr, "LFP: %08o %08o %08o\n\r", faccl, facch, RC);
                    break;

       case OP_SFP:          /* Store Floating Point +FP */
fprintf(stderr, "SFP: %08o %08o\n\r", faccl, facch);
                    if (Mem_write(RB, &faccl, 0)) {
                       goto intr;
                    }
                    RA = facch;
                    if (fovr) {
                       RA |= B0;
                       BV = 1;
                    }
                    RB++;
                    if (Mem_write(RB, &RA, 0)) {
                       goto intr;
                    }
                    if (RX & 1) 
                       faccl = facch = fovr = 0;
                    break;

       case 0150:
       case 0151:
                    if (exe_mode) {
                        break;
                    }
       case 0170:            /* Read special register */
                    if (exe_mode) {
                         RA = 0;
                         switch(RB) {
                         case 1: RA = SR1; break;
                         case 2: RA = SR2; break;
                         case 3: RA = SR3; break;
                         case 64: RA = SR64; SR64 = 0; break;
                         case 65: RA = SR65; SR65 = 0; break;
                         }
                         XR[RX] = RA;
                         break;
                    }
                    /* Fall through */
       case 0171:            /* Write special register */
                    if (exe_mode) {
                         break;
                    }
                    /* Fall through */
       case 0172:            /* Exit from executive */
       case 0173:            /* Load Datum limit and G */
                     if (exe_mode) {
#if 0 /* Type A & B */   /* For non extended address processors. */
                         Mem_read(RB, &RA, 0);
                         RG = RA & 077;
                         RD = RA & 077700;
                         RL = (RA >> 9) & 077700;
#else
                         Mem_read(RB, &RA, 0); /* Read datum */
                         RD = RA & (M22 & ~077);
                         RG = (RA & 17) << 3;
                         Mem_read(RB+1, &RA, 0); /* Read Limit */
                         RL = RA & (M22 & ~077);
                         RG |= (RA & 07);
                         Mode &= ~(EJM|AM22|EXTRC);
                         Mode |= (EJM|AM22|EXTRC) & RA;
                         adrmask = (Mode & AM22) ? M22 : M15;
fprintf(stderr, "Load limit: %08o D:=%08o %02o\n\r", RL, RD, Mode);
#endif
                         if (RF & 1)              /* Check if 172 or 173 order code */
                             break;
                         /* Restore floating point ACC from D12/D13 */
                         for (n = 0; n < 8; n++)  /* Restore user mode registers */
                             Mem_read(RD+n, &XR[n], 0);
                         Mem_read(RD+9, &RA, 0);    /* Read ZStatus and mode */
                         Mode &= ~1;
                         if ((Mode & AM22) && (RA & B3)) 
                            Mode |= 1;
                         Mem_read(RD+8, &RC, 0);     /* Restore C register */
fprintf(stderr, "Load PC: %08o D:=%08o z=%08o\n\r", RC, RD, RA);
                         if ((Mode & AM22) == 0 && (RA & B8)) 
                            Mode |= 1;
                         BV =  (RC & B1) != 0;
                         BCarry =  (RC & B0) != 0;
                         RC &= adrmask;
                         Mem_read(RD+12, &faccl, 0);  /* Restore F.P.U. */
                         Mem_read(RD+13, &facch, 0);  /* Restore F.P.U. */
                         exe_mode = 0;
                         break;
                    }
                    /* Fall through */
       case 0174:            /* Send control character to peripheral */
                    if (exe_mode) {
                         break;
                    }
                    /* Fall through */
       case 0175:                          /* Null operation in Executive mode */
       case 0176:
                    if (exe_mode) {
                         break;
                    }
                    /* Fall through */
       case 0177:            /* Test Datum and Limit */
                    if (exe_mode) {
                         if (RA < RG || RA > RL) 
                            BCarry = 1;
                         break;
                    }
       default:
                    /* Voluntary entry to executive */
voluntary:
                    if (exe_mode) {
                       reason = SCPE_STOP;
                       break;
                    }
                    exe_mode = 1;
                    /* Store registers */
                    Mem_write(RD+13, &facch, 0);  /* Save F.P.U. */
                    Mem_write(RD+12, &faccl, 0);  
                    RA = 0;         /* Build ZSTAT */
                    if (Mode & 1)
                        RA |= B3;
                    if (OPIP)
                        RA |= B2;
                    Mem_write(RD+9, &RA, 0);
                    RA = RC;
                    if (BV)
                      RA |= B0;
                    if (BCarry)
                      RA |= B1;
#if 0 /* Type A & B */
                    if (Mode & 1)
                        RA |= B8;
#endif
                    Mem_write(RD+8, &RA, 0);
                    for (n = 0; n < 8; n++) 
                       Mem_write(RD+n, &XR[n], 0);
                    Mode = 0;
                    adrmask = (Mode & AM22) ? M22 : M15;
                    XR[1] = RB;
                    XR[2] = temp;
                    RC = 040; 
                    break;
       }

       if (hst_lnt) {      /* history enabled? */
           hst[hst_p].rr = RA;
       }
       sim_interval--;
    }                           /* end while */
   
/* Simulation halted */

    return reason;
}

/* Interval timer routines */
t_stat
rtc_srv(UNIT * uptr)
{
    int32 t;

    t = sim_rtcn_calb(rtc_tps, TMR_RTC);
    sim_activate_after(uptr, 1000000/rtc_tps);
//    tmxr_poll = t;
    return SCPE_OK;
}
/* Reset routine */

t_stat
cpu_reset(DEVICE * dptr)
{
    sim_brk_types = sim_brk_dflt = SWMASK('E') | SWMASK('A') | SWMASK('B');
    hst_p = 0;

    sim_register_clock_unit (&cpu_unit[0]);
    sim_rtcn_init (cpu_unit[0].wait, TMR_RTC);
    sim_activate(&cpu_unit[0], cpu_unit[0].wait) ;

    return SCPE_OK;
}


/* Memory examine */

t_stat
cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr >= MAXMEMSIZE)
        return SCPE_NXM;
    if (vptr != NULL) {
        if (addr < 010) 
           *vptr = (t_value)XR[addr];
        else
           *vptr = (t_value)M[addr];
    }
    return SCPE_OK;
}

/* Memory deposit */

t_stat
cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr >= MAXMEMSIZE)
        return SCPE_NXM;
    if (addr < 010) 
       XR[addr] = val;
    else
       M[addr] = val;
    return SCPE_OK;
}

t_stat
cpu_show_size(FILE *st, UNIT *uptr, int32 val, CONST void *desc) 
{
    fprintf(st, "%dK", MEMSIZE/1024);
    return SCPE_OK;
}

t_stat
cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    t_uint64            mc = 0;
    uint32              i;

    cpu_unit[0].flags &= ~UNIT_MSIZE;
    cpu_unit[0].flags |= val;
    cpu_unit[1].flags &= ~UNIT_MSIZE;
    cpu_unit[1].flags |= val;
    val >>= UNIT_V_MSIZE;
    val = (val + 1) * 4096;
    if ((val < 0) || (val > MAXMEMSIZE))
        return SCPE_ARG;
    for (i = val; i < MEMSIZE; i++)
        mc |= M[i];
    if ((mc != 0) && (!get_yn("Really truncate memory [N]?", FALSE)))
        return SCPE_OK;
    MEMSIZE = val;
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
//            hst[i].c = 0;
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
    fprintf(st, "       C       EA       XR        A        B   Result c v e Op\n\n");
    for (k = 0; k < lnt; k++) { /* print specified */
        h = &hst[(++di) % hst_lnt];     /* entry pointer */

        if (h->rc & HIST_PC) {   /* instruction? */
            int i;
            fprintf(st, " %07o %08o %08o %08o %08o %08o %o %o %o",
                    h->rc & M22 , h->ea, h->xr, h->ra, h->rb, h->rr,
                    h->c, h->v, h->e);
            sim_eval = h->op;
            fprint_sym(st, h->rc & M22, &sim_eval, &cpu_unit[0], SWMASK('M'));
            fputc('\n', st);    /* end line */
        }                       /* end else instruction */
    }                           /* end for */
    return SCPE_OK;
}


t_stat
cpu_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr) 
{
    fprintf(st, "ICL1900 CPU\n\n");
    fprintf(st, "The ICL1900 \n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}
