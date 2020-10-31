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
#define UNIT_MSIZE      (0x1ff << UNIT_V_MSIZE)
#define MEMAMOUNT(x)    (x << UNIT_V_MSIZE)
#define UNIT_V_MODEL    (UNIT_V_MSIZE + 9)
#define UNIT_MODEL      (0x3f << UNIT_V_MODEL)
#define MODEL(x)        (UNIT_MODEL & (x << UNIT_V_MODEL))


#define TMR_RTC         1

#define HIST_PC         BM1
#define HIST_MAX        500000
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
#define MOD4F           15
#define MOD4S           16      /* Ax OPT */
#define MOD5            17      /* A2 FP */
#define MOD5A           18      /* Ax FP */
#define MOD5E           19      /* C2 FP */
#define MOD5F           20
#define MOD5S           21      /* Ax FP */
#define MOD6            22      /* C2 OPT */
#define MOD6A           23      /* Ax OPT 076 131 */
#define MOD6E           24      /* Ax OPT 076 */
#define MOD6F           25
#define MOD6S           26      /* Ax OPT */
#define MOD7            27      /* C2 FP */
#define MOD7A           28      /* Ax FP */
#define MOD7E           29      /* Ax FP */
#define MOD7F           30
#define MOD7S           31      /* Ax FP */
#define MOD8            32      /* Ax FP */
#define MOD8A           33      /* Ax FP */
#define MOD8S           34      /* Ax FP */
#define MOD9            35      /* A2 FP */
#define MODXF           36      /* C2 FP */



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
uint32              RP;                         /* Temp register */
uint32              RS;                         /* Temp register */
uint32              RT;                         /* Temp register */
uint8               RX;                         /* X field register */
uint32              XR[8];                      /* Index registers */
uint32              faccl;                      /* Floating point accumulator low */
uint32              facch;                      /* Floating point accumulator high */
uint8               fovr;                       /* Floating point overflow */
uint8               BCarry;                     /* Carry bit */
uint8               BV;                         /* Overflow flag */
uint8               Mode;                       /* Mode */
uint8               Zero;                       /* Zero suppression flag */
uint8               exe_mode = 1;               /* Executive mode */
#define     EJM     040                            /* Extended jump Mode */
#define     DATUM   020                            /* Datum mode */
#define     AM22    010                            /* 22 bit addressing */
#define     EXTRC   004                            /* Executive trace mode */
/*                  002       */                   /* unused mode bit */
uint8               OIP;                        /* Obey instruction */
uint8               PIP;                        /* Pre Modify instruction */
uint8               OPIP;                       /* Saved Pre Modify instruction */
uint32              SR1;                        /* Mill timer */
uint32              SR2;                        /* Typewriter I/P */
uint32              SR3;                        /* Typewriter O/P */
uint32              SR64;                       /* Interrupt status */
uint32              SR65;                       /* Interrupt status */
uint32              adrmask;                    /* Mask for addressing memory */
uint32              memmask;                    /* Memory address range mask */
uint8               loading;                    /* Loading bootstrap */


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
    uint8     mode;
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
t_stat              cpu_set_model(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              cpu_set_float(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              cpu_show_float(FILE * st, UNIT * uptr, int32 val,
                                  CONST void *desc);
t_stat              cpu_set_mult(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              cpu_show_mult(FILE * st, UNIT * uptr, int32 val,
                                  CONST void *desc);
t_stat              cpu_show_hist(FILE * st, UNIT * uptr, int32 val,
                                  CONST void *desc);
t_stat              cpu_set_hist(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              cpu_help(FILE *, DEVICE *, UNIT *, int32, const char *);
/* Interval timer */
t_stat              rtc_srv(UNIT * uptr);
void                time_read(uint32 *word);
int32               rtc_tps = 60 ;
int32               tmxr_poll = 10000;



CPUMOD  cpu_modtab[] = {
     { "1901",   MOD1,    TYPE_A1|FLOAT_STD|FLOAT_OPT|MULT_OPT|SV, 0, 10 },
     { "1901A",  MOD1A,   TYPE_A1|FLOAT_STD|FLOAT_OPT|MULT_OPT|SV, 0, 10 },
     { "1901S",  MOD1S,   TYPE_A1|FLOAT_STD|FLOAT_OPT|MULT_OPT|SV, 0, 10 },
     { "1901T",  MOD1T,   TYPE_A1|FLOAT_STD|FLOAT_OPT|MULT_OPT|SV, 0, 10 },
     { "1902",   MOD2,    TYPE_A1|FLOAT_STD|FLOAT_OPT|MULT|SV, 0, 10 },
     { "1902A",  MOD2A,   TYPE_C2|FLOAT_STD|FLOAT_OPT|MULT|SV, 0, 10 },
     { "1902S",  MOD2S,   TYPE_C1|FLOAT_STD|FLOAT_OPT|MULT|SV, EXT_IO, 10 },
     { "1902T",  MOD2T,   TYPE_C1|FLOAT_STD|FLOAT_OPT|MULT|SV, EXT_IO, 10 },
     { "1903",   MOD3,    TYPE_A2|FLOAT_STD|FLOAT_OPT|MULT_OPT|SV, 0, 10 },
     { "1903A",  MOD3A,   TYPE_C2|FLOAT_STD|FLOAT_OPT|MULT|SV, 0, 10 },
     { "1903S",  MOD3S,   TYPE_C2|FLOAT_STD|FLOAT_OPT|MULT_OPT|SV, EXT_IO, 10 },
     { "1903T",  MOD3T,   TYPE_A2|FLOAT_STD|FLOAT_OPT|MULT_OPT|WG, 0, 10 },
     { "1904",   MOD4,    TYPE_B2|FLOAT_OPT|MULT|WG, 0, 1 },
     { "1904A",  MOD4A,   TYPE_C2|FLOAT_OPT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1904E",  MOD4E,   TYPE_C2|FLOAT_OPT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1904F",  MOD4F,   TYPE_C2|FLOAT_OPT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1904S",  MOD4S,   TYPE_C2|FLOAT_OPT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1905",   MOD5,    TYPE_A2|FLOAT|MULT|WG, 0, 1 },
     { "1905A",  MOD5A,   TYPE_A2|FLOAT|MULT|WG|SL_FLOAT, 0, 10 },
     { "1905E",  MOD5E,   TYPE_C2|FLOAT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1905F",  MOD5F,   TYPE_C2|FLOAT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1905S",  MOD5S,   TYPE_C2|FLOAT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1906",   MOD6,    TYPE_A2|FLOAT|MULT|WG|SL_FLOAT, 0, 10 },
     { "1906A",  MOD6A,   TYPE_A2|FLOAT|MULT|WG|SL_FLOAT, 0, 100 },
     { "1906E",  MOD6E,   TYPE_C2|FLOAT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1906F",  MOD6F,   TYPE_C2|FLOAT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1906S",  MOD6S,   TYPE_C2|FLOAT|MULT|WG|SL_FLOAT, EXT_IO, 100 },
     { "1907",   MOD7,    TYPE_A2|FLOAT|MULT|WG|SL_FLOAT, 0, 10 },
     { "1907A",  MOD7A,   TYPE_A2|FLOAT|MULT|WG|SL_FLOAT, 0, 10 },
     { "1907E",  MOD7E,   TYPE_C2|FLOAT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1907F",  MOD7F,   TYPE_C2|FLOAT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1907S",  MOD7S,   TYPE_C2|FLOAT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1908",   MOD8,    TYPE_A2|FLOAT|MULT|WG|SL_FLOAT, 0, 10 },
     { "1908A",  MOD8A,   TYPE_A2|FLOAT|MULT|WG|SL_FLOAT, 0, 10 },
     { "1908S",  MOD8S,   TYPE_C2|FLOAT|MULT|WG|SL_FLOAT, EXT_IO, 10 },
     { "1909",   MOD9,    TYPE_C2|FLOAT|MULT|WG|SL_FLOAT, EXT_IO, 1 },
     { NULL, 0, 0, 0, 0},
};

uint16   cpu_flags = TYPE_C2|FLOAT_OPT|MULT;
uint8    io_flags  = EXT_IO;

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT                cpu_unit[] =
    {{ UDATA(rtc_srv, MODEL(MOD4A)|MEMAMOUNT(7)|UNIT_IDLE, MAXMEMSIZE ), 16667 }};

REG                 cpu_reg[] = {
    {ORDATAD(C, RC,  22, "Instruction code"), REG_FIT},
    {ORDATAD(F, RF,   7, "Order Code"), REG_FIT},
    {ORDATAD(G, RG,   3, "General register"), REG_FIT},
    {ORDATAD(D, RD,  22, "Datum"), REG_FIT},
    {ORDATAD(L, RL,  22, "Limit"), REG_FIT},
    {ORDATAD(M, Mode, 7, "Mode Register"), REG_FIT},
    {BRDATAD(X, XR,   8, 24, 8, "Index Register"), REG_FIT},
    {NULL}
};

MTAB                cpu_mod[] = {
    /* Stevenage */
    {UNIT_MODEL, MODEL(MOD1),  "1901",  "1901",  &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD1A), "1901A", "1901A", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD1S), "1901S", "1901S", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD1T), "1901T", "1901T", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD2),  "1902",  "1902",  &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD2A), "1902A", "1902A", &cpu_set_model, NULL, NULL},  /* C1 */
    {UNIT_MODEL, MODEL(MOD2S), "1902S", "1902S", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD2T), "1902T", "1902T", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD3),  "1903",  "1903",  &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD3A), "1903A", "1903A", &cpu_set_model, NULL, NULL},  /* C1 */
    {UNIT_MODEL, MODEL(MOD3S), "1903S", "1903S", &cpu_set_model, NULL, NULL},
    /* West Gorton */
    {UNIT_MODEL, MODEL(MOD3T), "1903T", "1903T", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD4),  "1904",  "1904",  &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD4A), "1904A", "1904A", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD4E), "1904E", "1904E", &cpu_set_model, NULL, NULL},  /* C */
    {UNIT_MODEL, MODEL(MOD4F), "1904F", "1904F", &cpu_set_model, NULL, NULL},  /* C */
    {UNIT_MODEL, MODEL(MOD4S), "1904S", "1904S", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD5),  "1905",  "1905",  &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD5E), "1905E", "1905E", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD5F), "1905F", "1905F", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD5S), "1905S", "1905S", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD6),  "1906",  "1906",  &cpu_set_model, NULL, NULL},    /* C */
    {UNIT_MODEL, MODEL(MOD6A), "1906A", "1906A", &cpu_set_model, NULL, NULL},  /* C */
    {UNIT_MODEL, MODEL(MOD6E), "1906E", "1906E", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD6F), "1906F", "1906F", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD7),  "1907",  "1907",  &cpu_set_model, NULL, NULL},    /* C */
    {UNIT_MODEL, MODEL(MOD7E), "1907E", "1907E", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD7F), "1907F", "1907F", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD8A), "1908A", "1908A", &cpu_set_model, NULL, NULL},
    {UNIT_MODEL, MODEL(MOD9),  "1909",  "1909",  &cpu_set_model, NULL, NULL},
    {MTAB_XTD|MTAB_VDV, 0,  NULL, "NOFLOAT", &cpu_set_float, NULL, NULL, "Disable floating point"},
    {MTAB_XTD|MTAB_VDV, 1,  "FLOAT",  "FLOAT", &cpu_set_float, &cpu_show_float, NULL, "Enable floating point"},
    {MTAB_XTD|MTAB_VDV,  0, NULL, "NOMULT", &cpu_set_mult, NULL, NULL, "Disable hardware multiply"},
    {MTAB_XTD|MTAB_VDV,  1, "MULT",   "MULT", &cpu_set_mult, &cpu_show_mult, NULL, "Enable hardware multiply"},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(0), NULL, "4K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(1), NULL, "8K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(3), NULL, "16K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(7), NULL, "32K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(11), NULL, "48K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(15), NULL, "64K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(23), NULL, "96K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(31), NULL, "128K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(63), NULL, "256K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(127), NULL, "512K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(254), NULL, "1024K", &cpu_set_size},
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




/* Test if we can access a word */
uint8 Mem_test(uint32 addr) {
    addr &= M22;

    if (!exe_mode) {
        if (addr < 8)
            return 0;
        addr = addr + RD;
    } else if (addr < 8)
        return 0;

    if (!exe_mode && RL && (addr < RD || addr >= RL)) {
        SR64 |= B1;
        return 1;
    }
    addr &= memmask;
    if (addr > MEMSIZE) {
        SR64 |= B1;
        return 1;
    }
    return 0;
}

uint8 Mem_read(uint32 addr, uint32 *data, uint8 flag) {
    addr &= M22;

    SR1++;
    if (!exe_mode) {
        if (addr < 8) {
            *data = XR[addr];
            return 0;
        }
        addr = addr + RD;
    } else if (flag && (Mode & DATUM) != 0) {
        addr = addr + RD;
    } else if (addr < 8) {
        *data = XR[addr];
        return 0;
    }

    if (!exe_mode && RL && (addr < RD || addr >= RL)) {
        SR64 |= B1;
        return 1;
    }
    addr &= memmask;
    if (addr > MEMSIZE) {
        SR64 |= B1;
        return 1;
    }
    *data = M[addr];
    return 0;
}

uint8 Mem_write(uint32 addr, uint32 *data, uint8 flag) {
    addr &= M22;

    if (!exe_mode) {
        if (addr < 8) {
            XR[addr] = *data;
            return 0;
        }
        addr = addr + RD;
    } else if (flag && (Mode & DATUM) != 0) {
        addr = addr + RD;
    } else if (addr < 8) {
        XR[addr] = *data;
        return 0;
    }
    if (!exe_mode && RL && (addr < RD || addr >= RL)) {
        SR64 |= B1;
        return 1;
    }
    addr &= memmask;
    if (addr > MEMSIZE) {
        SR64 |= B1;
        return 1;
    }
    M[addr] = *data;
    return 0;
}

t_stat
sim_instr(void)
{
    t_stat              reason;
    uint32              temp;           /* Hol order code being obeyed */
    int                 m;              /* Holds index register for address modification */
    int                 n;              /* Generic short term temp register */
    int                 e1,e2;          /* Temp for exponents */
    int                 f;              /* Used to hold flags */

    memmask = (CPU_TYPE < TYPE_C1) ? M15: M22;
    adrmask = (Mode & AM22) ? M22 : M15;
    reason = chan_set_devs();

    while (reason == SCPE_OK) {        /* loop until halted */
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

       while (loading) {        /* event queue? */
           reason = sim_process_event();
           if (reason != SCPE_OK) {
                break; /* process */
           }
           if ((SR64 | SR65) != 0) {
              loading = 0;
              exe_mode = 1;
              RC = 020;
           }
           sim_interval--;
       }

intr:
       if (!exe_mode && (SR64 | SR65) != 0) {
            if (CPU_TYPE < TYPE_C1 && !exe_mode)
                RC += RD;
            exe_mode = 1;
            loading = 0;
            /* Store registers */
            if (cpu_flags & FLOAT && cpu_flags & SL_FLOAT) {
                Mem_write(RD+12, &faccl, 0);
                RT = facch;
                if (fovr)
                    RT |= B0;
                Mem_write(RD+13, &RT, 0);  /* Save F.P.U. */
            }
            RA = 0;         /* Build ZSTAT */
            if (cpu_flags & SV) {
                Mem_read(RD+9, &RA, 0);
                RA &= M15;
                RA |= ((Mode|DATUM) << 16);
                if (Mode & DATUM)
                   RA |= 1 << 16;
                if (BCarry)
                   RA |= B1;
            } else {
                if (CPU_TYPE >= TYPE_C1)  {
                    Mem_read(RD+9, &RA, 0);
                    RA &= M15;
                    if (Zero)
                        RA |= B3;
                    if (OPIP | PIP)
                        RA |= B2;
                    Mem_write(RD+9, &RA, 0);
                }
            }
            RA = RC & memmask;
            if (io_flags & EXT_IO) {
                if (BCarry)
                  RA |= B1;
            } else {
                RC &= M15;
                if (Zero)
                    RA |= B8;
            }
            if (BV)
                RA |= B0;
            Mem_write(RD+8, &RA, 0);
            for (n = 0; n < 8; n++)
               Mem_write(RD+n, &XR[n], 0);
            BV = BCarry = Mode = Zero = 0;
            adrmask = M15;
            RC = 020;
            PIP = 0;
       }

fetch:
       if (!exe_mode && (Mode & 7) == 1)
           SR64 |= B2;

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
               hst[hst_p].mode = Mode;
           }
           RC = (RC + 1) & ((Mode & (EJM|AM22)) ? M22: M15);
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
           /* Handle PC relative addressing */
           if ((Mode & EJM) != 0 && (RF & 1) == 0) {
               RB = RB | ((RB & 020000) ? 017740000 : 0); /* Sign extend RB */
               RB = (RB + RC) & M22;
           }
           m = 0;
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
                      hst[hst_p].mode = Mode;
                  }
                  RC = (RC + 1) & ((Mode & (EJM|AM22)) ? M22: M15);
                  goto intr;
              }
              if (RF & 010) {
                 uint32 t;
                 t = RA;
                 RA = RB;
                 RB = t;
              }
           }
       }
       OPIP = PIP;
       PIP = 0;

       if (hst_lnt && RC != 7) {      /* history enabled? */
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
           hst[hst_p].mode = Mode;
       }

       /* Advance to next location, except on OBEY order */
       if (RF != OP_OBEY)
           RC = (RC + 1) & ((Mode & (EJM|AM22)) ? M22: M15);
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
                     n = (RA & B0) != 0;
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
                        if ((n && t2 && !tr) || (!n && !t2 && tr)) {
                           BV = 1;
                           if (!exe_mode && (Mode & 7) == 4)
                               SR64 |= B2;
                        }
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
                     /* Stevenage Machines */
                     if ((cpu_flags & SV) != 0 && exe_mode && RX != 0)
                         XR[RX] = RA;
                     RB = 0;
                     BCarry = 0;
                     if (Mem_write(RS, &RB, 1)) {
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
                     if ((cpu_flags & MULT) == 0)
                         goto voluntary;
                     if (RA == B0 && RB == B0) {
                         if (RF != OP_MPA || (XR[(RX + 1) & 7] & B0) == 0) {
                             BV = 1;
                             if (!exe_mode && (Mode & 7) == 4)
                                 SR64 |= B2;
                         }
                     }
                     RP = RA;
                     RA = RB;
                     n = RP & 1;
                     RP >>= 1;
                     if (RF & 1)  /* Multiply and Round  */
                         RP |= B0;
                     RB = 0;
                     for(RK = 23; RK != 0; RK--) {
                        if (n)
                           RB += RA;
                        n = RP & 1;
                        RP >>= 1;
                        if (RB & 1)
                           RP |= B0;
                        if (RB & B0)
                           RB |= BM1;
                        RB >>= 1;
                     }

                     if (n) {
                         RB += (RA ^ FMASK) + 1;
                     }
                     n = RP & 1;         /* Check for MPR */
                     if (n && RP & B0)
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
                         if (RA & ~(M23)) {
                             BV = 1;
                             if (!exe_mode && (Mode & 7) == 4)
                                 SR64 |= B2;
                         }
                         RA &= M23;
                     } else {
                         /* Save bits over 23 to char */
                         m = (m == 0) ? 3 : (XR[m] >> 22) & 3;
                         m = 6 * (3 - m);
                         RP = (RA >> 23) & 017;
                         if (Zero && RP == 0)
                             RP = 020;
                         else
                             Zero = 0;
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
                     if ((cpu_flags & MULT) == 0)
                         goto voluntary;
                     RP = XR[(RX+1) & 7];            /* VR */
                     RA = RB;  /* Divisor to RA */
                     RB = XR[RX];  /* Dividend to RB/RP */
//fprintf(stderr, "DVD0: %08o %08o %08o - %3o C=%08o\n\r", RA, RB, RP, RF, RC);

                     if (RA == 0) { /* Exit on zero divisor */  /* VI */
                         BV = 1;
                         if (!exe_mode && (Mode & 7) == 4)
                             SR64 |= B2;
                         BCarry = 0;
                         break;
                     }

                     /* Setup for specific divide order code */ /* V11 */
                     if (RF & 2) {     /* DVS */
                         if (RP & B0) { /* RC22 */ /* Sign extend RB */
                             RB = FMASK;
                         } else {
                             RB = 0;
                         }
                     }

// {
//   long int   divsor;
//   long int   dividend;
//   int sign;
//
//   dividend = (((long int)(RB)) << 23) | (long int)(RP);
//   if (RB & B0)   {
//      dividend |= 0xffff800000000000;
//   }
//   sign = 1;
//   divsor = (long int)(RA);
//   if (RA & B0) {
//      divsor |= 0xffffffffff000000;
//      sign = -1;
//   }
//fprintf(stderr, "DVD1: %08o %08o %08o %ld (%lo) / %ld (%lo) = %ld (%lo) m %ld (%lo) %o\n\r", RA, RB, RP,
//  dividend, dividend, divsor, divsor, dividend/divsor, dividend/divsor,
//   sign* (dividend%divsor), sign *(dividend % divsor), BV);
//}
                     BCarry = 0;
                     if ((RB | RP) == 0)
                         goto dvd_zero;
                     RP <<= 1;
                     RP &= FMASK;
                     BCarry = 0;

                     /* First partial remainder */   /* V12 */
                     if (((RB ^ RA) & B0) == 0) {
                         RS = RB + (RA ^ FMASK) + 1;
                         RK=1;
                     } else {
                         RS = RB + RA;
                         RK=0;
                     }
                     /* Check if potiential overflow */
                     if (((RS ^ RA) & B0) != 0)
                         BCarry = 1;
                     BCarry = RK != BCarry;
                     /* Shift left quotent and remainder */
                     RP <<= 1;
                     if (((RS ^ RA) & B0) == 0) {
                         RP |= 1;    /* First quotient digit */
                     }
                     RB = RS << 1;
                     if (RP & BM1)
                         RB |= 1;
                     RB &= FMASK;
                     RP &= FMASK;
//fprintf(stderr, "DVD2: %08o %08o %08o \n\r", RA, RP, RB);

                     /* Main divide loop */         /* V13 */
                     for (RK = 22; RK != 0; RK--) {
                         if (((RS ^ RA) & B0) == 0) {
                             RS = RB + (RA ^ FMASK) + 1;
                         } else {
                             RS = RB + RA;
                         }
                         RP <<= 1;
                         if (((RS ^ RA) & B0) == 0) {
                             RP |= 1;
                         }
                         RB = RS << 1;
                         if (RP & BM1)
                             RB |= 1;
                         RB &= FMASK;
                         RP &= FMASK;
//fprintf(stderr, "DVD3: %08o %08o %08o %08o \n\r", RA, RP, RB, RS);
                     }

                     /* Final product */
                     if (((RS ^ RA) & B0) == 0) {   /* V14 */
                         RS = RB + (RA ^ FMASK) + 1;
                     } else {
                         RS = RB + RA;
                     }
                     RP <<= 1;
                     if (((RS ^ RA) & B0) == 0) {
                         RP |= 1;
                     }
                     RP &= FMASK;
                     RB = RS & FMASK;
//fprintf(stderr, "DVD4: %08o %08o %08o %08o \n\r", RA, RP, RB, RS);
                     /* End correction */
                     if ((RP & 1) == 0) {
                         RB = (RB + RA) & FMASK;
                     }
//fprintf(stderr, "DVD5: %08o %08o %08o \n\r", RA, RP, RB);
                     if ((RF & 1) == 0 || RB == 0) {
//fprintf(stderr, "DVD7: %08o %08o %08o \n\r", RA, RP, RB);
                         /* If remainder same as divisor, bump quotent zero remainder */
                         if (RB == RA) {
                             RT = RP;
                             RP++;
                             if ((RT & B0) != (RP & B0))
                                 BCarry = !BCarry;
                             RB = 0;
                         }
                     } else {   /* DVR */
                         RT = RB + (RA ^ FMASK) + 1;
//fprintf(stderr, "DVDA: %08o %08o %08o %08o \n\r", RA, RP, RB, RT);
                         RA = RB;
                         if ((((RT + RA) ^ RA) & B0) == 0) {
                             RB = RT & FMASK;
//fprintf(stderr, "DVD6: %08o %08o %08o \n\r", RA, RP, RB);
                             RT = RP;
                             RP++;
                             if ((RT ^ RP) & B0)
                                 BCarry = !BCarry;
                             if (RP & BM1)
                                 BCarry = 1;
                         }
                     }
dvd_zero:
                     /* Set overflow if BCarry still set */
                     if (BCarry) {
                         BV = 1;
                         if (!exe_mode && (Mode & 7) == 4)
                             SR64 |= B2;
                     }
                     BCarry = 0;
// {
//   int32   a;
//   int32   b;
//
//   a = RB;
//   if (RB & B0)
//      a |= 0xff000000;
//   b = RP;
//   if (RP & B0)
//      b |= 0xff000000;
//fprintf(stderr, "DVD8: q=%08o(%d) r=%08o(%d) %d %d %d\n\r", RP, b, RB, a, BV);
//}
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
                     RA += B1;
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
                     if ((Mode & (AM22|EJM)) == 0) {
                        RA &= adrmask;
                        if (Zero)
                           RA |= B8;
                     } else {
                        if (Zero)
                           RA |= B1;
                     }
                     if (BV)
                       RA |= B0;
                     BV = 0;
                     BCarry = 0;
                     XR[RX] = RA;
branch:
                     /* Monitor mode 3 -> int */
                     if (!exe_mode && (Mode & 7) == 3) {
                         SR64 |= B2;
                         break;
                     }
                     /* Handle replace jump */
                     if ((Mode & EJM) != 0 && (RF & 1) != 0) {
                         RB &= 037777;
                         if (Mem_read(RB, &RB, 0)) {
                             goto intr;
                         }
                     }
                     /* Handle SMO */
                     if (OPIP)
                          RB = (RB + RP) & adrmask;
                     if (hst_lnt) {      /* history enabled? */
                         hst[hst_p].ea = RB;
                     }
                     /* Don't transfer if address not valid */
                     if (Mem_test(RB))
                         goto intr;
                     /* Monitor mode 2 -> Exec Mon */
                     /* Read address to store from location 262. */
                     /* Store address transfer address at location, increment 262 mod 128 */
                     if (!exe_mode && (Mode & 7) == 2) {
                         temp = M[262];
                         M[temp & adrmask] = RB;
                         M[262] =  (temp & ~ 0177) + ((temp + 1) & 0177);
                     }
                     if ((Mode & (EJM|AM22)) == 0)
                         RB &= M15;
                     else
                         RB &= M22;
                     RC = RB;
                     break;

       case OP_EXIT:         /* Exit Subroutine */
       case OP_EXIT1:
                     if (RA & B0) {
                         BV = 1;
                         if (!exe_mode && (Mode & 7) == 4)
                             SR64 |= B2;
                     }
                     Zero = 0;
                     if ((Mode & (AM22|EJM)) == 0) {
                        if (RA & B8)
                           Zero = 1;
                     } else {
                        if (RA & B1)
                           Zero = 1;
                     }
                     BCarry = 0;
                     RM = RM | ((RM & 040000) ? 017740000 : 0); /* Sign extend RM */
                     RA = RA + RM;
                     if (OPIP)
                         RA = RA + RP;
                     if (hst_lnt) {      /* history enabled? */
                         hst[hst_p].ea = RA;
                     }
                     if ((Mode & (EJM|AM22)) == 0)
                         RA &= M15;
                     else
                         RA &= M22;
                     if (Mem_test(RA)) {  /* Verify memory location accessable */
                         goto intr;
                     }
                     RC = RA;
                     break;

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
                          if (!exe_mode && (Mode & 7) == 4 && BV)
                              SR64 |= B2;
                          if (n == 0)
                              goto branch;
                          break;
                    }
                    break;

       /* B with Floating or C */
       case OP_BFP:          /* Branch state of floating point accumulator */
       case OP_BFP1:
                    if ((cpu_flags & FLOAT) == 0)
                        goto voluntary;
                    if ((RX & 04) == 0 && fovr)
                        BV = 1;
                    switch (RX & 06) {
                    default:
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
                       n = 0;
                       switch (m) {
                       case 0: n = (RA & B0) != 0; break;
                       case 1: break;
                       case 2:
                       case 3: temp = RA & B0;
                       }
                       RA <<= 1;
                       RA |= n;
                       if ((m & 2) && temp != (RA & B0)) {
                          BV = 1;
                          if (!exe_mode && (Mode & 7) == 4)
                              SR64 |= B2;
                       }
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
                               n = (RA & B0) != 0;
                               temp = (RA & BM1) != 0;
                               if ((uint32)n != temp) {
                                   BV = 1;
                                   if (!exe_mode && (Mode & 7) == 4)
                                       SR64 |= B2;
                               }
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
                    RT = RA & B0;
                    BCarry = 0;
                    switch(m) {
                    case 0: break;
                    case 1: RT = 0; break;
                    case 2: break;
                    case 3: if (BV) {
                                RT = B0 ^ RT;
                                BV = 0;
                            }
                    }
                    while (RK != 0) {
                       if (m == 0)
                          RT = (RA & 1) ? B0 : 0;
                       temp = RA & 1;
                       RA >>= 1;
                       RA |= RT;
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
                    RT = RA & B0;
                    if (m == 3 && RK != 0 && BV)  {
                         RT = B0^RT;
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
                               RA |= RT;
                       }
                       RK--;
                    }
                    XR[RX] = RA;
                    XR[(RX+1) & 07] = RB;
                    break;

       case OP_NORM:         /* Nomarlize Single -2 +FP */
       case OP_NORMD:        /* Normalize Double -2 +FP */
                    if ((cpu_flags & NORM_OP) == 0)
                        goto voluntary;
                    RT = RB;
                    RB = (RF & 1) ? XR[(RX+1) & 07] & M23 : 0;
//fprintf(stderr, "Norm0: %08o %08o %08o\n\r", RA, RB, RT);
                    if (RT & 04000) {
                         RT = 0;
                    } else {
                         RT &= 01777;
                    }
                    if (RT == 0) {
                        RA = RB = 0;
                    } else if (BV) {
                        RT++;
                        RP = (RA & B0) ^ B0;
                        if (RA & 1 && RF & 1)
                           RB |= B0;
                        RB >>= 1;
                        RA >>= 1;
                        RA |= RP;
                        if ((RF & 1) == 0) {
                            RB = RT;
                            goto norm3;
                        }
//fprintf(stderr, "Norm1: %08o %08o %08o\n\r", RA, RB, RT);
                    }  else if (RA != 0 || RB != 0) {
                        /* Shift left until sign and B1 not same */
//fprintf(stderr, "Norm2: %08o %08o %08o\n\r", RA, RB, RT);
                        while ((((RA >> 1) ^ RA) & B1) == 0) {
                           RT--;
                           RA <<= 1;
                           if (RB & B1)
                             RA |= 1;
                           RB <<= 1;
                           RA &= FMASK;
                           RB &= M23;
                        }
//fprintf(stderr, "Norm3: %08o %08o %08o\n\r", RA, RB, RT);
                        /* Check for overflow */
                        if (RT & B0) { /* < 0 */
                           RA = RB = 0;
                           goto norm1;
                        }
                        if (RT > M9)   /* NO Round if overflow */
                           goto norm2;
                    } else
                        RT = 0;
                    if (RF & 1) {   /* Round only on NORMD order codes */
//fprintf(stderr, "Norm4: %08o %08o %08o\n\r", RA, RB, RT);
                        /* Round RB if needed */
                        RP = RB;
                        RB += 0400;
//fprintf(stderr, "Norm4a: %08o %08o %08o\n\r", RA, RB, RT);
                        if (RB & B0 && RT <= M9)  {
                            RB = RP;
                            if (((RA & M23) + 1) & B0)
                               RA = RB = 0;
                        }
                    }
norm2:
                    RB = (RB & (MMASK|B0)) | (RT & M9);
norm3:
                    BV = 0;
                    if (RT > M9) {  /* Exponent overlfow */
                        BV = 1;
                        if (!exe_mode && (Mode & 7) == 4)
                            SR64 |= B2;
                    }
norm1:
//fprintf(stderr, "Norm5: %08o %08o %08o\n\r", RA, RB, RT);
                    XR[(RX+1) & 07] = RB;
                    XR[RX] = RA;
                    break;

        /* Not on A*/
       case OP_MVCH:         /* Move Characters - BC */
                    if (CPU_TYPE < TYPE_B1)
                        goto voluntary;
                    RK = RB;
                    RB = XR[(RX+1) & 07];
                    do {
                        if (Mem_read(RA & adrmask, &RT, 1)) {
                            goto intr;
                        }
                        m = (RA >> 22) & 3;
                        RT = (RT >> (6 * (3 - m))) & 077;
                        if (Mem_read(RB & adrmask, &RS, 1)) {
                            goto intr;
                        }
                        m = (RB >> 22) & 3;
                        m = 6 * (3 - m);
                        RS &= ~(077 << m);
                        RS |= (RT & 077) << m;
                        if (Mem_write(RB & adrmask, &RS, 1)) {
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
                    if (CPU_TYPE < TYPE_C1)
                        goto voluntary;
                    if (OPIP) {      /* Error */
                        SR64 |= B1;
                        goto intr;
                    }
                    if (Mem_read(RS, &RP, 1)) {
                        goto intr;
                    }
                    PIP = 1;
                    goto fetch;

       case OP_NULL:        /* No Operation */
                    if (!exe_mode && RX == 7 && (Mode & 7) > 0 && (Mode & 7) < 5)
                        SR64 |= B2;
                    break;

       case OP_LDCT:        /* Load Count */
                    RA = CNTMSK & (RB << 15);
                    XR[RX] = RA;
                    break;

       case OP_MODE:         /* Set Mode */
                    /* Stevenage Machines */
                    if ((cpu_flags & SV) != 0 && exe_mode) {
                       if (RX == 0) {
                           /* Remap modes settings */
                           Mode = 0;
                           if (RB & 02)
                               Mode |= DATUM;
                           if (RB & 020)
                               Mode |= AM22;
                           if (RB & 0100)
                               Mode |= EJM;
                           if (RB & 0200)
                               BCarry = 1;
                        } else if (RX == 1) {
                           temp = RB; /* Set interrupt enable mode */
                        }
                    } else if (exe_mode)
                       Mode = RB & 076;
                    Zero = RB & 1;
                    adrmask = (Mode & AM22) ? M22 : M15;
                    break;

       case OP_MOVE:        /* Copy N words */
                    if (CPU_TYPE < TYPE_B1)
                        goto voluntary;
                    RK = RB;
                    RA &= adrmask;
                    RB = XR[(RX+1) & 07] & adrmask;
                    do {
                        if (Mem_read(RA, &RT, 1)) {
                            goto intr;
                        }
                        if (Mem_write(RB, &RT, 1)) {
                            goto intr;
                        }
                        RA++;
                        RB++;
                        RK = (RK - 1) & 0777;
                    } while (RK != 0);
                    break;

       case OP_SUM:         /* Sum N words */
                    if (CPU_TYPE < TYPE_B1)
                        goto voluntary;
                    RK = RB;
                    RB = XR[(RX+1) & 07] & adrmask;
                    RA = 0;
                    do {
                        if (Mem_read(RB, &RT, 1)) {
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
                    if ((cpu_flags & FLOAT) == 0)
                        goto voluntary;
                     if (Mem_read(RS, &RA, 1)) {
                        goto intr;
                     }
                     RS++;
                     if (Mem_read(RS, &RB, 1)) {
                        goto intr;
                     }
                     faccl = RA;
                     facch = RB;
                     fovr = (RB & B0) != 0;
                     e1 = 23;
//fprintf(stderr, "FLOAT: %08o %08o %08o %o\n\r", faccl, facch, RC, RX);
                     RX = 0;
                     goto fn;
       case OP_FIX:          /* Convert Float to Fixed +FP */
                    if ((cpu_flags & FLOAT) == 0)
                        goto voluntary;
                    RA = faccl;
                    RB = facch & MMASK;
                    e1 = 279 - (facch & M9);
//fprintf(stderr, "FIX: %08o %08o %08o %o %d\n\r", faccl, facch, RC, RX, e1);
                    if (e1 < 46) {
                       while (e1 > 0) {
                         if (RA & 1)
                             RB |= B0;
                         if (RA & B0)
                             RA |= BM1;
                         RA >>= 1;
                         RB >>= 1;
                         e1--;
                       }
                       while (e1 < 0) {
                         RA <<= 1;
                         if (RB & B1)
                           RA |= 1;
                         RB <<= 1;
                         RA &= FMASK;
                         RB &= M23;
                         e1++;
                       }
                    } else {
                        RB = RA = 0;
                        e1 = 0;
                    }
                    if (e1 != 0 || fovr) {
                        BV = 1;
                        if (!exe_mode && (Mode & 7) == 4)
                            SR64 |= B2;
                    }
//fprintf(stderr, "FIX1: %08o %08o %08o %o %d\n\r", RA, RB, RC, RX, e1);
                    if (Mem_write(RS, &RA, 1)) {
                       goto intr;
                    }
                    RS++;
                    if (Mem_write(RS, &RB, 1)) {
                       goto intr;
                    }
                    break;

       case OP_FAD:          /* Floating Point Add +FP */
       case OP_FSB:          /* Floating Point Subtract +FP */
                    if ((cpu_flags & FLOAT) == 0)
                        goto voluntary;
                    if (Mem_read(RS, &RA, 1)) { /* Read lower */
                        goto intr;
                    }
                    RS++;
                    if (Mem_read(RS, &RB, 1)) { /* Read high + exp */
                        goto intr;
                    }
//fprintf(stderr, "FAD0: %08o %08o %08o %08o %08o %o\n\r", RA, RB, RC, faccl, facch, RX);
                    fovr |= (RB & B0) != 0;
                    RB &= M23;
                    if (RX & 4) { /* See if we should swap operands */
                        RT = facch;
                        facch = RB;
                        RB = RT;
                        RT = faccl;
                        faccl = RA;
                        RA = RT;
//fprintf(stderr, "FAD1: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                    }
                    if (RF == OP_FSB) { /* If subtract invert RA&RB */
                        RA ^= FMASK;
                        RB ^= MMASK;
                        RB += 01000;
                        if (RB & B0)
                           RA = (RA + 1) & FMASK;
                        RB &= M23;
//fprintf(stderr, "FAD2: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                    }
                    /* Extract exponents and numbers */
                    e1 = ((facch & M9)) - 256;
                    facch &= MMASK;
                    e2 = ((RB & M9)) - 256;
                    RB &= MMASK;
                    n = e1 - e2;
//fprintf(stderr, "FAD3: %08o %08o %08o %08o %08o %d %d %d\n\r", RA, RB, RC, faccl, facch, e1, e2, n);
                    /* Align mantissa's to add */
                    if (n < 0) {
                        e1 = e2;
                        if (n < -37) {  /* See if more then 37 bits difference */
                            faccl = RA;
                            facch = RB;
                            goto fn;
                        }
                        while(n < 0) {
                            if (faccl & B0)
                               faccl |= BM1;
                            if (faccl & 1)
                               facch |= B0;
                            facch >>= 1;
                            faccl >>= 1;
                            n++;
//fprintf(stderr, "FAD4a: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                        }
                    } else if (n > 0) {
                        if (n > 37) { /* See if more then 37 bits difference */
                            goto fn;
                        }
                        while(n > 0) {
                            if (RA & B0)
                               RA |= BM1;
                            if (RA & 1)
                               RB |= B0;
                            RA >>= 1;
                            RB >>= 1;
                            n--;
//fprintf(stderr, "FAD4b: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                        }
                    }
//fprintf(stderr, "FAD5: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                    /* Add the numbers */
                    n = (faccl & B0) != 0;  /* Save signs */
                    if (RA & B0)
                       n |= 2;
                    faccl += RA;
                    facch += RB;
                    if (facch & B0) {
                        facch &= M23;
                        faccl ++;
                    }
                    /* Sign of result */
                    if ((faccl & B0) != 0)
                       n |= 4;
//fprintf(stderr, "FAD6: %08o %08o %08o %o\n\r", faccl, facch, RC, n);
                    /* Result sign not equal same sign as addens */
                    if (n == 3 || n == 4) {
                        if (faccl & 1)
                           facch |= B0;
                        faccl >>= 1;
                        facch >>= 1;
                        facch &= MMASK;
                        if ((n & 4) == 0)
                           faccl |= B0;   /* Set sign */
                        e1++;
//fprintf(stderr, "FAD6a: %08o %08o %08o\n\r", faccl, facch, RC);
                    }
                    if (n == 7)   /* Handle minus with overflow */
                       e1--;
fn:
                    /* Common normalize routine */
                    faccl &= FMASK;
//fprintf(stderr, "FAD7: %08o %08o %08o %03o %d\n\r",  faccl, facch, RC,  e1, e1);
                    if ((facch | faccl) == 0)
                         break;
//fprintf(stderr, "FAD8: %08o %08o %08o %o\n\r",faccl, facch, RC, RX );
                    /* Shift left until sign and B1 not same */
                    if ((RX & 2) == 0 ) {
                        while ((((faccl >> 1) ^ faccl) & B1) == 0) {
                            e1--;
                            facch <<= 1;
                            faccl <<= 1;
                            if (facch & B0)
                              faccl |= 1;
                            faccl &= FMASK;
                            facch &= M23;
//fprintf(stderr, "FAD9: %08o %08o %08o %03o %d\n\r", faccl, facch, RC,  e1, e1);
                        }
                    }
                    /* Do rounding if needed */
                    if ((RX & 1) == 0 && (facch & B16) != 0) {
                        facch += B16;
                        if (facch & B0)
                           faccl ++;
                        facch &= M23;
                        faccl &= FMASK;
                        /* renormalize if things changed */
                        if ((RX & 2) == 0 && (((faccl >> 1) ^ faccl) & B1) == 0) {
                            e1--;
                            facch <<= 1;
                            faccl <<= 1;
                            if (facch & B0)
                              faccl |= 1;
                            faccl &= FMASK;
                            facch &= M23;
//fprintf(stderr, "FADr: %08o %08o %08o %03o %d\n\r", faccl, facch, RC,  e1, e1);
                        }
//fprintf(stderr, "FADR: %08o %08o %08o %03o\n\r",  faccl, facch, RC,  e1);
                    }
                    faccl &= FMASK;
                    facch &= MMASK;
                    /* Check if exponent in range */
                    if (e1 < -256) {
                        facch = faccl = 0;
                        e1 = -256;
                    }
                    if (e1 > 255)  {
                        fovr = 1;
                        e1 = (- e1);
                    }
                    if (fovr == 0 && ((faccl & FMASK) | (facch & MMASK)) == 0)
                        facch = faccl = 0;
                    else
                        facch |= (e1 + 256) & M9;
                    RA = faccl;
//fprintf(stderr, "FADA: %08o %08o %08o %03o %o\n\r", faccl, facch, RC, e1, fovr);
                    break;

       case OP_FMPY:         /* Floating Point Multiply +FP */
                    if ((cpu_flags & FLOAT) == 0)
                        goto voluntary;
                    if (Mem_read(RS, &RA, 1)) { /* Read lower */
                        goto intr;
                    }
                    RS++;
                    if (Mem_read(RS, &RB, 1)) { /* Read high + exp */
                        goto intr;
                    }
//fprintf(stderr, "FMP0: %08o %08o %08o %08o %08o %o\n\r", RA, RB, RC, faccl, facch, RX);
                    fovr |= (RB & B0) != 0;
                    RB &= M23;
                    /* Not really needed for Multiply */
                    if (RX & 4) { /* See if we should swap operands */
                        RT = facch;
                        facch = RB;
                        RB = RT;
                        RT = faccl;
                        faccl = RA;
                        RA = RT;
//fprintf(stderr, "FMP1: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                    }
                    /* Extract exponents and mantissa's */
                    e1 = ((facch & M9)) - 256;
                    facch &= MMASK;
                    e2 = ((RB & M9)) - 256;
                    RB &= MMASK;
                    e1 += e2;  /* Exponent is sum of exponents */
                    /* Make both numbers positive and compute final size */
                    f = 0;
                    if (faccl & B0) {
                        f = 1;
                        faccl ^= FMASK;
                        facch ^= MMASK;
                        facch += B15;
                        if (facch & B0) {
                           faccl += 1;
                           faccl &= FMASK;
                           facch &= MMASK;
                        }
                    }
                    if (RA & B0) {
                        f = !f;
                        RA ^= FMASK;
                        RB ^= MMASK;
                        RB += B15;
                        if (RB & B0) {
                           RA += 1;
                           RA &= FMASK;
                           RB &= MMASK;
                        }
                    }
//fprintf(stderr, "FMP2: %08o %08o %08o %08o %08o %03o %d %o\n\r", RA, RB, RC, faccl, facch, e1, e1, f);
                    RT = faccl;
                    RP = facch;
                    faccl =  facch = 0; /* Clear product */
                    /* Do actual multiply */

                    for(RK = 37; RK != 0; RK--) {
                        /* If High order bit one, add in RB,RA */
                        if (RP & B15) {
                            facch += RB;
                            faccl += RA;
                            if (facch & B0)
                                faccl++;
                            facch &= M23;
                        }
                        /* Shift faccl,fach,RT,t right one */
                        if (RT & 1)
                            RP |= B0;
                        if (facch & 1)
                            RT |= B0;
                        if (faccl & 1)
                            facch |= B0;
                        RP >>= 1;
                        RT >>= 1;
                        facch >>= 1;
                        faccl >>= 1;
//fprintf(stderr, "FMP3: %08o %08o %08o %08o %08o %08o %08o %02o\n\r", RA, RB, RC, faccl, facch, RT, RP, RK);
                    }
                    /* Check if still negative multiplican */
                    if (RP & B15) {
                        facch += RB;
                        faccl += RA;
                        if (facch & B0)
                            faccl++;
                        facch &= M23;
                    }

                    /* Check if underflow */
                    if ((RX & 2) == 0 && faccl == 0 && facch != 0) {
                        while ((faccl & B1) == 0) {
                            e1--;
                            RP <<= 1;
                            RT <<= 1;
                            facch <<= 1;
                            faccl <<= 1;
                            if (RP & B0)
                                RT |= 1;
                            if (RT & B0)
                                facch |= 1;
                            if (facch & B0)
                                faccl |= 1;
                            faccl &= FMASK;
                            facch &= M23;
                            RT &= M23;
                            RP &= M23;
                        }
                    }
                    /* Fix up if over flow */
                    if (faccl & B0) {
                        if (faccl & 1)
                           facch |= B0;
                        faccl >>= 1;
                        facch >>= 1;
                        facch &= MMASK;
                        e1++;
                    }

//fprintf(stderr, "FMP4: %08o %08o\n\r", faccl, facch);
                    /* Fix sign */
                    if (f) {
                        faccl ^= FMASK;
                        facch ^= M23;
                        facch ++;
                        if (facch & B0) {
                           faccl ++;
                           faccl &= FMASK;
                           facch &= MMASK;
                        }
                    }
//fprintf(stderr, "FMP5: %08o %08o\n\r", faccl, facch);
                    /* Go normalize and round */
                    goto fn;

       case OP_FDVD:         /* Floating Point Divide +FP */
                    if ((cpu_flags & FLOAT) == 0)
                        goto voluntary;
                    if (Mem_read(RS, &RA, 1)) { /* Read lower */
                        goto intr;
                    }
                    RS++;
                    if (Mem_read(RS, &RB, 1)) { /* Read high + exp */
                        goto intr;
                    }
//fprintf(stderr, "FDV0: %08o %08o %08o %08o %08o %o\n\r", RA, RB, RC, faccl, facch, RX);
                    fovr |= (RB & B0) != 0;
                    RB &= M23;
                    if (RX & 4) { /* See if we should swap operands */
                        RT = facch;
                        facch = RB;
                        RB = RT;
                        RT = faccl;
                        faccl = RA;
                        RA = RT;
//fprintf(stderr, "FDV1: %08o %08o %08o %08o %08o\n\r", RA, RB, RC, faccl, facch);
                    }
                    /* Extract exponents and mantissas */
                    e1 = ((facch & M9)) - 256;
                    facch &= MMASK;
                    e2 = ((RB & M9)) - 256;
                    RB &= MMASK;
                    e1 -= e2; /* Final exponent is difference of terms */
                    /* Make both positive and comupte final sign */
                    f = 0;
                    if (faccl & B0) {
                        f = 1;
                        faccl ^= FMASK;
                        facch ^= MMASK;
                        facch += B15;
                        if (facch & B0) {
                            faccl += 1;
                            faccl &= FMASK;
                            facch &= MMASK;
                        }
                    }
                    if ((RA & B0) != 0){
                        f = !f;
                        RA ^= FMASK;
                        RB ^= MMASK;
                        RB += B15;
                        if (RB & B0) {
                            RA += 1;
                            RA &= FMASK;
                            RB &= MMASK;
                        }
                    }
                    /* Handle zero divide */
                    if ((RA | RB) == 0) {
                        faccl = 0;
                        facch = 0400;
                        fovr=1;
                        break;
                    }
                    RA ^= M23;       /* precompliment */
                    RB ^= M23;
                    RP = faccl;   /* Set dividend into upper half */
                    RT = facch;
                    faccl = 0;
                    facch = 0;
                    n = 0;
                    /* DO actual divide */
                    for (RK=46; RK != 0; RK--) {
                         uint32    t0, t1;
                         t0 = RT + RB + 1;
                         t1 = RP + RA;
                         if (t0 & B0)
                             t1++;
                         if (n || (t1 & B0)) {
                             RT = t0;
                             RP = t1;
                             facch |= 1;
                         }
                         facch <<= 1;
                         faccl <<= 1;
                         RT <<= 1;
                         RP <<= 1;
                         if (facch & B0)
                             faccl |= 1;
                         if (RT & B0)
                             RP |= 1;
                         n = (RP & B0) != 0;
                         RT &= M23;
                         RP &= M23;
                         facch &= M23;
//fprintf(stderr, "FDV3: %08o %08o %08o %08o %08o %08o %08o %02o %08o %08o %o\n\r", RA, RB, RC, faccl, facch, RP, RT, RK,t1, t0, n);
                    }
                    /* If rounding and positive and negative result, adjust */
                    if (((RX & 2) == 0 || f == 0) && faccl & B0) {
                        if (faccl & 1)
                            facch |= B0;
                        faccl >>= 1;
                        facch >>= 1;
                        e1++;
                    }
                    /* Fix sign */
                    if (f) {
                        if (faccl & B0 && (RX & 2)) {
                            if (faccl != B0)
                                e1++;
                            facch = (e1 + 256) & M9;
                            faccl = B0;
                            fovr=1;
                            break;
                        } else {
                            faccl ^= FMASK;
                            facch ^= M23;
                            facch ++;
                            if (facch & B0)
                               faccl ++;
                            faccl &= FMASK;
                            facch &= M23;
                            if (faccl == B0)
                                fovr=1;
                        }
                    }
//fprintf(stderr, "FDV4: %08o %08o %o\n\r", faccl, facch, f);
                    goto fn;

       case OP_LFP:          /* Load Floating Point +FP */
                    if ((cpu_flags & FLOAT) == 0)
                        goto voluntary;
                    if (RX & 1) {
                       faccl = facch = fovr = 0;
//fprintf(stderr, "LFPZ: %08o %08o %o\n\r", faccl, facch, RX);
                       break;
                    }
                    if (Mem_read(RB, &RA, 1)) {  /* Read low order */
                        goto intr;
                    }
                    RB++;
                    if (Mem_read(RB, &RS, 1)) {  /* Read high order + exp */
                        goto intr;
                    }
                    faccl = RA;
                    facch = RS & M23;
                    fovr = (RS & B0) != 0;
//fprintf(stderr, "LFP: %08o %08o %08o %o\n\r", faccl, facch, RC, fovr);
                    break;

       case OP_SFP:          /* Store Floating Point +FP */
//fprintf(stderr, "SFP: %08o %08o %o\n\r", faccl, facch, fovr);
                    if ((cpu_flags & FLOAT) == 0)
                        goto voluntary;
                    if (Mem_write(RB, &faccl, 1)) {
                       goto intr;
                    }
                    RA = facch;
                    if (fovr) {
                        RA |= B0;
                        BV = 1;
                        if (!exe_mode && (Mode & 7) == 4)
                            SR64 |= B2;
                    }
                    RB++;
                    if (Mem_write(RB, &RA, 1)) {
                       goto intr;
                    }
                    if (RX & 1)
                       faccl = facch = fovr = 0;
                    break;

       case 0160:   /* Stevenage machines */   /* Load accumulators */
                    if ((cpu_flags & SV) != 0 && exe_mode) {
                         /* Restore registers */
                         for (n = 0; n < 8; n++)     /* Restore user mode registers */
                             Mem_read(RB+n, &XR[n], 0);
                         break;
                    }
                    /* Fall through */

       case 0161:   /* Stevenage machines */   /* Store accumulators */
                    if ((cpu_flags & SV) != 0 && exe_mode) {
                         /* Dump registers */
                         for (n = 0; n < 8; n++)     /* Restore user mode registers */
                             Mem_write(RB+n, &XR[n], 0);
                         break;
                    }
                    /* Fall through */

       case 0162:   /* Stevenage machines */
       case 0163:   /* Stevenage machines */   /* Stop and Display */
       case 0164:   /* Stevenage machines */   /* Search List N for Word X */
                    if ((cpu_flags & SV) != 0 && exe_mode) {
                        RK = RB;
                        RB = XR[(RX+1) & 07] & adrmask;
                        do {
                            if (Mem_read(RA, &RT, 1)) {
                                goto intr;
                            }
                            RB++;
                            if (RA == RT)
                               BCarry = 1;
                            RK = (RK - 1) & 0777;
                        } while (RA != RT && RK != 0);
                        XR[(RX+1) & 07] = RB;
                        break;
                    }
                    /* Fall through */
                        
       case 0165:   /* Stevenage machines */   /* Parity Search */
                    if ((cpu_flags & SV) != 0 && exe_mode) {
                        RK = RB;
                        RB = XR[(RX+1) & 07] & adrmask;
                        do {
                            if (Mem_read(RA, &RT, 1)) {
                                goto intr;
                            }
                            RA++;
                            RB++;
                            RK = (RK - 1) & 0777;
                        } while (RK != 0);
                        XR[RX] = RA;
                        XR[(RX+1) & 07] = RB;
                        break;
                    }
                    /* Fall through */

       case 0166:   /* Stevenage machines */   /* Test X unequal */
                    if ((cpu_flags & SV) != 0 && exe_mode) {
                        if (RA != RB)
                            BCarry = 1;
                        break;
                    }
                    /* Fall through */

       case 0167:   /* Stevenage machines */   /* Test X Less */
                    if ((cpu_flags & SV) != 0 && exe_mode) {
                        RB += BCarry;
                        if (RB != RA)
                            BCarry = (RB > RA);
                        break;
                    }

                    /* If we get here and not in executive mode do volintary */
                    if (exe_mode) {
                        reason = SCPE_STOP;
                        break;
                    }
                    /* Fall through */

       case 0170:            /* Read special register */
                    if (exe_mode) {
                         RA = 0;
                         switch(RB) {
                         case 0: /* Time of day clock */
                                 time_read(&RA);
                                 break;
                         case 1: RA = SR1; SR1 = 0; break;
                         case 64: RA = SR64; SR64 &= 003777777; break;
                         case 65: RA = SR65; break;
                         default: if (RB < 64)
                                      chan_nsi_status(RB, &RA);
                                  break;
                         }
                         XR[RX] = RA;
                         break;
                    }
                    /* Fall through */
       case 0171:            /* Write special register */
                    if (exe_mode) {
//fprintf(stderr, "WR SR %o %08o\n\r", RB, RA);
                         if (RB < 64)
                             chan_nsi_cmd(RB, RA);
                         break;
                    }
                    /* Fall through */
       case 0172:            /* Exit from executive */
       case 0173:            /* Load Datum limit and G */
                     if (exe_mode) {
                         if (CPU_TYPE < TYPE_C1) {
                             /* For non extended address processors. */
                             Mem_read(RB, &RA, 0);
                             RG = RA & 077;
                             RD = RA & 077700;
                             RL = (RA >> 9) & 077700;
                         } else {
                             Mem_read(RB, &RA, 0); /* Read datum */
                             RD = RA & (M22 & ~077);
                             RG = (RA & 17) << 3;
                             Mem_read(RB+1, &RA, 0); /* Read Limit */
                             RL = RA & (M22 & ~077);
                             RG |= (RA & 07);
                             Mode = RA & 077;
                         }
                         adrmask = (Mode & (AM22)) ? M22 : M15;
//fprintf(stderr, "Load C=%08o limit: %08o D:=%08o %02o\n\r", RC, RL, RD, Mode);
                         if (RF & 1)                 /* Check if 172 or 173 order code */
                             break;
                         /* Restore registers */
                         for (n = 0; n < 8; n++)     /* Restore user mode registers */
                             Mem_read(RD+n, &XR[n], 0);
                         Mem_read(RD+9, &RA, 0);     /* Read ZStatus and mode */
                         Mem_read(RD+8, &RC, 0);     /* Restore C register */
//fprintf(stderr, "Load PC: %08o D:=%08o z=%08o\n\r", RC, RD, RA);
                         BV = (RC & B0) != 0;
                         BCarry = (RC & B1) != 0;
                         Zero = 0;
                         /* Type A & B */
                         if (CPU_TYPE < TYPE_C1) {
                             if (RC & B8)
                                 Zero = 1;
                             RC &= M15;
                             RC -= RD;
                         } else {
                             if (RA & B3)
                                 Zero = 1;
                         }
                         RC &= (Mode & (EJM|AM22)) ? M22 : M15;
                         if (cpu_flags & FLOAT && cpu_flags & SL_FLOAT) {
                             /* Restore floating point ACC from D12/D13 */
                             Mem_read(RD+12, &faccl, 0);  /* Restore F.P.U. */
                             Mem_read(RD+13, &facch, 0);  /* Restore F.P.U. */
                             fovr = (facch & B0) != 0;
                             facch &= M23;
                         }
                         exe_mode = 0;
                         break;
                    }
                    /* Fall through */
       case 0174:            /* Send control character to peripheral */
                    if (exe_mode) {
                         chan_send_cmd(RB, RA & 07777, &RT);
//fprintf(stderr, "CMD  C=%08o %04o %04o %08o\n\r", RC, RT, RB, RA);
                         m = (m == 0) ? 3 : (XR[m] >> 22) & 3;
                         m = 6 * (3 - m);
                         RT = (RT & 077) << m;
                         RA &= ~(077 << m);
                         RA |= RT;
                         XR[RX] = RA;
                         break;
                    }
                    /* Fall through */
       case 0175:                          /* Null operation in Executive mode */
                    if (exe_mode) {
//fprintf(stderr, "CMD 175 C=%08o %04o %08o\n\r", RC, RB, RA);
                         break;
                    }
       case 0176:
                    if (exe_mode) {
//fprintf(stderr, "CMD 176 C=%08o %04o %08o\n\r", RC, RB, RA);
                         break;
                    }
                    /* Fall through */
       case 0177:            /* Test Datum and Limit */
                    if (exe_mode) {
                        if (RA < RD || RA >= RL)
                            BCarry = 1;
                        break;
                    }
       case 0140:
       case 0141:
       case 0142:
       case 0143:
       case 0144:
       case 0145:
       case 0146:
       case 0147:
       case 0150:
       case 0151:
       case 0152:
       case 0153:
       case 0154:
       case 0155:
       case 0156:
       case 0157:
       default:
                    /* Voluntary entry to executive */
voluntary:
                    if (exe_mode) {
                        reason = SCPE_STOP;
                        break;
                    }
                    if ((CPU_TYPE < TYPE_C1) && !exe_mode)
                        RC += RD;
                    exe_mode = 1;
                    if (cpu_flags & FLOAT && cpu_flags & SL_FLOAT) {
                       /* Store registers */
                       Mem_write(RD+12, &faccl, 0);
                       RT = facch;
                       if (fovr)
                           RT |= B0;
                       Mem_write(RD+13, &RT, 0);  /* Save F.P.U. */
                    }
                    if (CPU_TYPE >= TYPE_C1) {
                        Mem_read(RD+9, &RT, 0);
                        RT &= M15;
                        /* Build ZSTAT and ASTAT */
                        if (Zero)
                            RT |= B3;
                        if (OPIP)
                            RT |= B2;
                        Mem_write(RD+9, &RT, 0);
                    }
                    RT = RC;
                    if (BV)
                        RT |= B0;
                    if (BCarry)
                        RT |= B1;
                    /* Type A & B */
                    if (CPU_TYPE < TYPE_C1 && Zero)
                        RT |= B8;
                    Mem_write(RD+8, &RT, 0);
                    for (n = 0; n < 8; n++)
                        Mem_write(RD+n, &XR[n], 0);
                    Zero = Mode = 0;
                    BCarry = BV = 0;
                    adrmask = M15;
                    if ((cpu_flags & SV) != 0) {
                        if ((RF & 0170) == 0140 || (RF & 0170) == 0110)
                           XR[1] = RD+RX;
                        XR[2] = RB;
                        XR[3] = RF & 07;
                        RC = 020 + ((RF >> 3) & 017);
                    } else {
                        XR[1] = RB;
                        XR[2] = temp;
                        RC = 040;
                    }
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
    (void)sim_rtcn_calb(rtc_tps, TMR_RTC);
    sim_activate_after(uptr, 1000000/rtc_tps);
    SR64 |= B3;
    return SCPE_OK;
}

int
bcd_2d(int n)
{
    uint8               d1, d2;

    d1 = n / 10;
    d2 = n % 10;
    return (d1 << 4) | d2;
}

void
time_read(uint32 *word)
{
    time_t              curtim;
    struct tm          *tptr;

    curtim = sim_get_time(NULL);/* get time */
    tptr = localtime(&curtim);  /* decompose */
    if (tptr == NULL)
        return;                 /* error? */

    /* Convert and fill buffer */
    *word = bcd_2d(tptr->tm_sec);
    *word |= bcd_2d(tptr->tm_min) << 7;
    *word |= bcd_2d(tptr->tm_hour) << 14;
    return;
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
    SR64 = SR65 = 0;

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
    uint32     mc = 0;
    uint32     i;

    cpu_unit[0].flags &= ~UNIT_MSIZE;
    cpu_unit[0].flags |= val;
    val >>= UNIT_V_MSIZE;
    val = (val + 1) * 4096;
    if ((val < 0) || (val > MAXMEMSIZE))
        return SCPE_ARG;
    for (i = val - 1; i < MEMSIZE; i++)
        mc |= M[i];
    if ((mc != 0) && (!get_yn("Really truncate memory [N]?", FALSE)))
        return SCPE_OK;
    MEMSIZE = val;
    for (i = MEMSIZE; i < MAXMEMSIZE; i++)
        M[i] = 0;
    return SCPE_OK;
}

t_stat
cpu_set_model(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    CPUMOD    *ptr;

    val >>= UNIT_V_MODEL;
    for(ptr = &cpu_modtab[0]; ptr->name != NULL; ptr++) {
        if (ptr->mod_num == val) {
           cpu_flags = ptr->cpu_flags;
           io_flags = ptr->io_flags;
           rtc_tps = ptr->ticker;
           cpu_unit[0].flags &= ~(UNIT_MODEL);
           cpu_unit[0].flags |= MODEL(val);
           return SCPE_OK;
        }
    }
    return SCPE_ARG;
}

t_stat
cpu_set_float(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 oval = val;
    cpu_flags &= ~FLOAT;
    if (val)
        cpu_flags |= FLOAT;
    if (oval != val)
       return SCPE_ARG;
    return SCPE_OK;
}

t_stat
cpu_show_float(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "%s", (cpu_flags & FLOAT) ? "FLOAT":"NOFLOAT");
    return SCPE_OK;
}

t_stat
cpu_set_mult(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 oval = val;
    cpu_flags &= ~MULT;
    if (val)
        cpu_flags |= MULT;
    if (oval != val)
       return SCPE_ARG;
    return SCPE_OK;
}

t_stat
cpu_show_mult(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "%s", (cpu_flags & MULT) ? "MULT":"NOMULT");
    return SCPE_OK;
}

/* Handle execute history */

/* Set history */
t_stat
cpu_set_hist(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int32               lnt;
    t_stat              r;

    if (cptr == NULL) {
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
    t_value             v[1];
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
    fprintf(st, "       C       EA       XR        A        B   Result c v e M  Op\n\n");
    for (k = 0; k < lnt; k++) { /* print specified */
        h = &hst[(++di) % hst_lnt];     /* entry pointer */

        if (h->rc & HIST_PC) {   /* instruction? */
            fprintf(st, " %07o %08o %08o %08o %08o %08o %o %o %o %02o ",
                    h->rc & M22 , h->ea, h->xr, h->ra, h->rb, h->rr,
                    h->c, h->v, h->e, h->mode);
            v[0] = h->op;
            (void)fprint_sym(st, h->rc & M22, v, &cpu_unit[0], SWMASK('M'));
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
