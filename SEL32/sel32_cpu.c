/* sel32_cpu.c: Sel 32 CPU simulator

   Copyright (c) 2018, James C. Bevier
   Portions provided by Richard Cornwell and other SIMH contributers

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
   JAMES C. BEVIER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "sel32_defs.h"

/* instruction trace controls */
//#define UTXBUG                              /* debugging for UTX */
//#define TRME                                /* set defined to enable instruction trace */
//#define TRMEMPX                             /* set defined to enable instruction trace for MPX */
//#undef TRME
int traceme = 0;                              /* dynamic trace function */
/* start on second diag starting */
//int trstart = 0x1230;                           /* 32/27 diag count of when to start tracing */
//int trstart = 0x166;                            /* 32/67 count of when to start tracing */
//int trstart = 0x167;                            /* 32/87 count of when to start tracing */
//int trstart = 0x15d;                            /* 32/97 count of when to start tracing */
//int trstart = 0x15c;                         /* V9 count of when to start tracing */
//int trstart = 0x15c;                         /* V6 count of when to start tracing */
/* start on J.INIT */
//int trstart = 0x780;                           /* UTX 32/87 diag count of when to start tracing */
//int trstart = 1235;                           /* UTX 32/67 diag count of when to start tracing */
////int trstart = 0x3;                           /* UTX 32/67 diag count of when to start tracing */
//int trstart = 0x1e0;                          /* DIAG V9 diag count of when to start tracing */
//int trstart = 0xd;                           /* UTX 32/67 diag count of when to start tracing */
//int trstart = 0xd8;                           /* UTX 32/67 diag count of when to start tracing */
//int trstart = 1;                            /* UTX 32/97 count of when to start tracing */
//int trstart = 0x8000000;                      /* count of when to start tracing */
//int trstart = 37;                           /* count of when to start tracing */
//int trstart = 0x18f;                           /* V6 count of when to start tracing */

/* 32/7x PSW/PSD Mode Trap/Interrupt Priorities */
/* Relative Logical  Int Vect TCW  IOCD Description */
/* Priority Priority Location Addr Addr             */
/*   0                 0F4              Power Fail Safe Trap */
/*   1                 0FC              System Override Trap (Not Used) */
/*   2                 0E8*             Memory Parity Trap */
/*   3                 190              Nonpresent Memory Trap */
/*   4                 194              Undefined Instruction Trap */
/*   5                 198              Privilege Violation Trap */
/*   6                 180              Supervisor Call Trap (SVC) */
/*   7                 184              Machine Check Trap */
/*   8                 188              System Check Trap */
/*   9                 18C              Map Fault Trap */
/*   A                                  Not Used */
/*   B                                  Not Used */
/*   C                                  Not Used */
/*   D                                  Not Used */
/*   E                 0E4              Block Mode Timeout Trap */
/*   F                 1A4*             Arithmetic Exception Trap */
/*  10        00       0F0              Power Fail Safe Interrupt */
/*  11        01       0F8              System Override Interrupt */
/*  12        12       0E8*             Memory Parity Trap */
/*  13        13       0EC              Attention Interrupt */
/*  14        14       140    100  700  I/O Channel 0 interrupt */  
/*  15        15       144    104  708  I/O Channel 1 interrupt */  
/*  16        16       148    108  710  I/O Channel 2 interrupt */  
/*  17        17       14C    10C  718  I/O Channel 3 interrupt */  
/*  18        18       150    110  720  I/O Channel 4 interrupt */  
/*  19        19       154    114  728  I/O Channel 5 interrupt */  
/*  1A        1A       158    118  730  I/O Channel 6 interrupt */  
/*  1B        1B       15C    11C  738  I/O Channel 7 interrupt */  
/*  1C        1C       160    120  740  I/O Channel 8 interrupt */  
/*  1D        1D       164    124  748  I/O Channel 9 interrupt */  
/*  1E        1E       168    128  750  I/O Channel A interrupt */  
/*  1F        1F       16C    12C  758  I/O Channel B interrupt */  
/*  20        20       170    130  760  I/O Channel C interrupt */  
/*  21        21       174    134  768  I/O Channel D interrupt */  
/*  22        22       178    138  770  I/O Channel E interrupt */  
/*  23        23       17C    13C  778  I/O Channel F interrupt */  
/*  24        24       190*             Nonpresent Memory Trap */
/*  25        25       194*             Undefined Instruction Trap */
/*  26        26       198*             Privlege Violation Trap */
/*  27        27       19C              Call Monitor Interrupt */
/*  28        28       1A0              Real-Time Clock Interrupt */
/*  29        29       1A4*             Arithmetic Exception Interrupt */
/*  2A        2A       1A8              External/Software Interrupt */
/*  2B        2B       1AC              External/Software Interrupt */
/*  2C        2C       1B0              External/Software Interrupt */
/*  2D        2D       1B4              External/Software Interrupt */
/*  2E        2E       1B8              External/Software Interrupt */
/*  2F        2F       1BC              External/Software Interrupt */
/*  30        30       1C0              External/Software Interrupt */
/*  31        31       1C4              External/Software Interrupt */
/* THRU      THRU     THRU                        THRU              */ 
/*  77        77       2DC              External/Software Interrupt */
/*  78                 2E0              End of IPU Processing Trap (CPU) */
/*  79                 2E4              Start IPU Processing Trap (IPU) */
/*  7A                 2E8              Supervisor Call Trap (IPU) */
/*  7B                 2EC              Error Trap (IPU) */
/*  7C                 2F0              Call Monitor Trap (IPU) */
/*  7D        7D       2F4              Stop IPU Processing Trap (IPU) */
/*  7E        7E       2F8              External/Software Interrupt */
/*  7F        7F       2FC              External/Software Interrupt */

/* Concept 32 PSD Mode Trap/Interrupt Priorities */
/* Relative|Logical |Int Vect|TCW |IOCD|Description */
/* Priority|Priority|Location|Addr|Addr             */
/*   -                 080              Power Fail Safe Trap */
/*   -                 084              Power On Trap */
/*   -                 088              Memory Parity Trap */
/*   -                 08C              Nonpresent Memory Trap */
/*   -                 090              Undefined Instruction Trap */
/*   -                 094              Privilege Violation Trap */
/*   -                 098              Supervisor Call Trap (SVC) */
/*   -                 09C              Machine Check Trap */
/*   -                 0A0              System Check Trap */
/*   -                 0A4              Map Fault Trap */
/*   -                 0A8              Undefined IPU Instruction Trap */
/*   -                 0AC              Signal CPU or Signal IPU Trap */
/*   -                 0B0              Address Specification Trap */
/*   -                 0B4              Console Attention Trap */
/*   -                 0B8              Privlege Mode Halt Trap */
/*   -                 0BC              Arithmetic Exception Trap */
/*   -                 0C0              Cache Error Trap (V9 Only) */
/*   -                 0C4              Demand Page Fault Trap (V6&V9 Only) */
/*                                                                */
/*   0        00       100              External/software Interrupt 0 */
/*   1        01       104              External/software Interrupt 1 */
/*   2        02       108              External/software Interrupt 2 */
/*   3        03       10C              External/software Interrupt 3 */
/*   4        04       110    704  700  I/O Channel 0 interrupt */  
/*   5        05       114    70C  708  I/O Channel 1 interrupt */  
/*   6        06       118    714  710  I/O Channel 2 interrupt */  
/*   7        07       11C    71C  718  I/O Channel 3 interrupt */  
/*   8        08       120    724  720  I/O Channel 4 interrupt */  
/*   9        09       124    72C  728  I/O Channel 5 interrupt */  
/*   A        0A       128    734  730  I/O Channel 6 interrupt */  
/*   B        0B       12C    73C  738  I/O Channel 7 interrupt */  
/*   C        0C       130    744  740  I/O Channel 8 interrupt */  
/*   D        0D       134    74C  748  I/O Channel 9 interrupt */  
/*   E        0E       138    754  750  I/O Channel A interrupt */  
/*   F        0F       13C    75C  758  I/O Channel B interrupt */  
/*  10        10       140    764  760  I/O Channel C interrupt */  
/*  11        11       144    76C  768  I/O Channel D interrupt */  
/*  12        12       148    774  770  I/O Channel E interrupt */  
/*  13        13       14c    77C  778  I/O Channel F interrupt */  
/*  14        14       150              External/Software Interrupt */
/*  15        15       154              External/Software Interrupt */
/*  16        16       158              External/Software Interrupt */
/*  17        17       15C              External/Software Interrupt */
/*  18        18       160              Real-Time Clock Interrupt */
/*  19        19       164              External/Software Interrupt */
/*  1A        1A       1A8              External/Software Interrupt */
/*  1B        1B       1AC              External/Software Interrupt */
/*  1C        1C       1B0              External/Software Interrupt */
/* THRU      THRU     THRU                        THRU              */ 
/*  6C        6C       2B0              External/Software Interrupt */
/*  6D        6D       2B4              External/Software Interrupt */
/*  6E        6E       2B8              External/Software Interrupt */
/*  6F        6F       2BC              Interval Timer Interrupt */

/* IVL ------------> ICB   Trap/Interrupt Vector Location points to Interrupt Context Block */
/*                   Wd 0 - Old PSD Word 1  points to return location */
/*                   Wd 1 - Old PSD Word 2 */
/*                   Wd 2 - New PSD Word 1  points to first instruction of service routine */
/*                   Wd 3 - New PSD Word 2 */
/*                   Wd 4 - CPU Status word at time of interrupt/trap */
/*                   Wd 5 - N/U For Traps/Interrupts */

/* IVL ------------> ICB   XIO Interrupt Vector Location */
/*                   Wd 0 - Old PSD Word 1  points to return location */
/*                   Wd 1 - Old PSD Word 2 */
/*                   Wd 2 - New PSD Word 1  points to first instruction of service routine */
/*                   Wd 3 - New PSD Word 2 */
/*                   Wd 4 - Input/Output Command List Address (IOCL) for the Class F I/O CHannel */
/*                   Wd 5 - 24 bit real address of the channel status word */

/* CPU registers, map cache, spad, and other variables */
int             cpu_index;                  /* Current CPU running */
uint32          PSD[2];                     /* the PC for the instruction */
#define PSD1 PSD[0]                         /* word 1 of PSD */
#define PSD2 PSD[1]                         /* word 2 of PSD */
uint32          M[MAXMEMSIZE] = { 0 };      /* Memory */
uint32          GPR[8];                     /* General Purpose Registers */
uint32          BR[8];                      /* Base registers */
uint32          PC;                         /* Program counter */
uint32          CC;                         /* Condition codes, bits 1-4 of PSD1 */
uint32          SPAD[256];                  /* Scratch pad memory */
uint32          INTS[112];                  /* Interrupt status flags */
uint32          CPUSTATUS;                  /* cpu status word */
uint32          TRAPSTATUS;                 /* trap status word */
uint32          CMCR;                       /* Cache Memory Control Register */
uint32          SMCR;                       /* Shared Memory Control Register */
uint32          CCW;                        /* Computer Configuration Word */
/* CPU mapping cache entries */
/* 32/55 has none */
/* 32/7x has 32 8KW maps per task */
/* Concept 32/27 has 256 2KW maps per task */
/* Concept 32/X7 has 2048 2KW maps per task */
uint32          MAPC[1024];                 /* maps are 16bit entries on word bountries */
uint32          dummy=0;
uint32          pfault;                     /* page # of fault from read/write */
uint32          HIWM = 0;                   /* max maps loaded so far */
uint32          TLB[2048];                  /* Translated addresses for each map entry */
/* bits 0-4 are bits 0-4 from map entry */
/* bit 0 valid */
/* bit 1 p1 write access if set */
/* bit 2 p2 write access if set */
/* bit 3 p3 write access if set MM - memory modify */
/* bit 4 p4 write access if set MA - memory accessed */
/* bit 5 hit bit means entry is etup, even if not valid map */
/* if hit bit is set and entry not valid, we will do a page fault */
/* bit 6 dirty bit, set when written to, page update required */
/* bits 8-18 has map reg contents for this page (Map << 13) */
/* bit 19-31 is zero for page offset of zero */

uint32          modes;                      /* Operating modes, bits 0, 5, 6, 7 of PSD1 */
uint8           wait4int = 0;               /* waiting for interrupt if set */

/* define traps */
uint32          TRAPME = 0;                 /* trap to be executed */
uint32          attention_trap = 0;         /* set when trap is requested */

struct InstHistory
{
    uint32   opsd1;     /* original PSD1 */
    uint32   opsd2;     /* original PSD2 */
    uint32   npsd1;     /* new PSD1 after instruction */
    uint32   npsd2;     /* new PSD2 after instruction */
    uint32   oir;       /* the instruction itself */
    uint32   modes;     /* current cpu mode bits */
    uint32   reg[16];   /* regs/bregs for operation */
};

/* forward definitions */
t_stat cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw);
t_stat cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw);
t_stat cpu_reset(DEVICE * dptr);
t_stat cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist(FILE * st, UNIT * uptr, int32 val, CONST void *desc);
t_stat cpu_set_hist(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
uint32 cpu_cmd(UNIT * uptr, uint16 cmd, uint16 dev);
t_stat cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *cpu_description (DEVICE *dptr);
t_stat RealAddr(uint32 addr, uint32 *realaddr, uint32 *prot);
t_stat load_maps(uint32 thepsd[2]);
t_stat read_instruction(uint32 thepsd[2], uint32 *instr);
t_stat Mem_read(uint32 addr, uint32 *data);
t_stat Mem_write(uint32 addr, uint32 *data);

/* external definitions */
extern t_stat startxio(uint16 addr, uint32 *status);    /* XIO start in chan.c */
extern t_stat testxio(uint16 addr, uint32 *status);     /* XIO test in chan.c */
extern t_stat stopxio(uint16 addr, uint32 *status);     /* XIO stop in chan.c */
extern t_stat rschnlxio(uint16 addr, uint32 *status);   /* reset channel XIO */
extern t_stat haltxio(uint16 addr, uint32 *status);     /* halt XIO */
extern t_stat grabxio(uint16 addr, uint32 *status);     /* grab XIO n/u */
extern t_stat rsctlxio(uint16 addr, uint32 *status);    /* reset controller XIO */
extern t_stat chan_set_devs();                          /* set up the defined devices on the simulator */
extern uint32 scan_chan(void);                          /* go scan for I/O int pending */
extern uint16 loading;                                  /* set when doing IPL */
extern int fprint_inst(FILE *of, uint32 val, int32 sw); /* instruction print function */
extern int irq_pend;                                    /* go scan for pending interrupt */
extern void rtc_setup(uint32 ss, uint32 level);         /* tell rtc to start/stop */
extern void itm_setup(uint32 ss, uint32 level);         /* tell itm to start/stop */
extern int32 itm_rdwr(uint32 cmd, int32 cnt, uint32 level); /* read/write the interval timer */

/* floating point subroutines definitions */
extern uint32 s_fixw(uint32 val, uint32 *cc);
extern uint32 s_fltw(uint32 val, uint32 *cc);
extern t_uint64 s_fixd(t_uint64 val, uint32 *cc);
extern t_uint64 s_fltd(t_uint64 val, uint32 *cc);
extern uint32 s_nor(uint32 reg, uint32 *exp);
extern t_uint64 s_nord(t_uint64 reg, uint32 *exp);
extern uint32 s_adfw(uint32 reg, uint32 mem, uint32 *cc);
extern uint32 s_sufw(uint32 reg, uint32 mem, uint32 *cc);
extern t_uint64 s_adfd(t_uint64 reg, t_uint64 mem, uint32 *cc);
extern t_uint64 s_sufd(t_uint64 reg, t_uint64 mem, uint32 *cc);
extern uint32 s_mpfw(uint32 reg, uint32 mem, uint32 *cc);
extern t_uint64 s_dvfw(uint32 reg, uint32 mem, uint32 *cc);
extern uint32 s_mpfd(t_uint64 reg, t_uint64 mem, uint32 *cc);
extern t_uint64 s_dvfd(t_uint64 reg, t_uint64 mem, uint32 *cc);

/* History information */
int32               hst_p = 0;       /* History pointer */
int32               hst_lnt = 0;     /* History length */
struct InstHistory *hst = NULL;      /* History stack */

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT  cpu_unit =
    /* Unit data layout for CPU */
/*  { UDATA(rtc_srv, UNIT_BINK | MODEL(MODEL_27) | MEMAMOUNT(0), MAXMEMSIZE ), 120 }; */
    {
    NULL,               /* UNIT *next */             /* next active */
    NULL,               /* t_stat (*action) */       /* action routine */
    NULL,               /* char *filename */         /* open file name */
    NULL,               /* FILE *fileref */          /* file reference */
    NULL,               /* void *filebuf */          /* memory buffer */
    0,                  /* uint32 hwmark */          /* high water mark */
    0,                  /* int32 time */             /* time out */
//was    UNIT_BINK|MODEL(MODEL_27)|MEMAMOUNT(1), /* uint32 flags */ /* flags */
    UNIT_IDLE|UNIT_BINK|MODEL(MODEL_27)|MEMAMOUNT(4), /* uint32 flags */ /* flags */
    0,                  /* uint32 dynflags */        /* dynamic flags */
    MAXMEMSIZE,         /* t_addr capac */           /* capacity */
    0,                  /* t_addr pos */             /* file position */
    NULL,               /* void (*io_flush) */       /* io flush routine */
    0,                  /* uint32 iostarttime */     /* I/O start time */
    0,                  /* int32 buf */              /* buffer */
    80,                 /* int32 wait */             /* wait */
};

//UNIT cpu_unit = { UDATA (&rtc_srv, UNIT_BINK, MAXMEMSIZE) };

REG                 cpu_reg[] = {
    {HRDATAD(PC, PC, 24, "Program Counter"), REG_FIT},
    {BRDATAD(PSD, PSD, 16, 32, 2, "Progtam Status Doubleword"), REG_FIT},
    {BRDATAD(GPR, GPR, 16, 32, 8, "Index registers"), REG_FIT},
    {BRDATAD(BR, BR, 16, 32, 8, "Base registers"), REG_FIT},
    {BRDATAD(SPAD, SPAD, 16, 32, 256, "CPU Scratchpad memory"), REG_FIT},
    {BRDATAD(MAPC, MAPC, 16, 32, 1024, "CPU map cache"), REG_FIT},
    {BRDATAD(TLB, TLB, 16, 32, 2048, "CPU Translation Lookaside Buffer"), REG_FIT},
    {HRDATAD(CPUSTATUS, CPUSTATUS, 32, "CPU Status Word"), REG_FIT},
    {HRDATAD(TRAPSTATUS, TRAPSTATUS, 32, "TRAP Status Word"), REG_FIT},
    {HRDATAD(CC, CC, 32, "Condition Codes"), REG_FIT},
    {BRDATAD(INTS, INTS, 16, 32, 112, "Interrupt Status"), REG_FIT},
    {HRDATAD(CMCR, CMCR, 32, "Cache Memory Control Register"), REG_FIT},
    {HRDATAD(SMCR, SMCR, 32, "Shared Memory Control Register"), REG_FIT},
    {HRDATAD(CCW, CCW, 32, "Computer Configuration Word"), REG_FIT},
//    {ORDATAD(BASE, baseaddr, 16, "Relocation base"), REG_FIT},
    {NULL}
};

/* Modifier table layout (MTAB) - only extended entries have disp, reg, or flags */
MTAB cpu_mod[] = {
    {
    /* MTAB table layout for cpu type */
    /* {UNIT_MODEL, MODEL(MODEL_55), "32/55", "32/55", NULL, NULL, NULL, "Concept 32/55"}, */
    UNIT_MODEL,          /* uint32 mask */            /* mask */
    MODEL(MODEL_55),     /* uint32 match */           /* match */
    "32/55",             /* cchar  *pstring */        /* print string */
    "32/55",             /* cchar  *mstring */        /* match string */
    NULL,                /* t_stat (*valid) */        /* validation routine */
    NULL,                /* t_stat (*disp)  */        /* display routine */
    NULL,                /* void *desc      */        /* value descriptor, REG* if MTAB_VAL, int* if not */
    "Concept 32/55",     /* cchar *help     */        /* help string */
    },
    {UNIT_MODEL, MODEL(MODEL_75), "32/75", "32/75", NULL, NULL, NULL, "Concept 32/75"},
    {UNIT_MODEL, MODEL(MODEL_27), "32/27", "32/27", NULL, NULL, NULL, "Concept 32/27"},
    {UNIT_MODEL, MODEL(MODEL_67), "32/67", "32/67", NULL, NULL, NULL, "Concept 32/67"},
    {UNIT_MODEL, MODEL(MODEL_87), "32/87", "32/87", NULL, NULL, NULL, "Concept 32/87"},
    {UNIT_MODEL, MODEL(MODEL_97), "32/97", "32/97", NULL, NULL, NULL, "Concept 32/97"},
    {UNIT_MODEL, MODEL(MODEL_V6), "V6", "V6", NULL, NULL, NULL, "Concept V6"},
    {UNIT_MODEL, MODEL(MODEL_V9), "V9", "V9", NULL, NULL, NULL, "Concept V9"},
    {
    /* MTAB table layout for cpu memory size */
    /* {UNIT_MSIZE, MEMAMOUNT(0), "128K", "128K", &cpu_set_size}, */
    UNIT_MSIZE,          /* uint32 mask */            /* mask */
    MEMAMOUNT(0),        /* uint32 match */           /* match */
    "128K",              /* cchar  *pstring */        /* print string */
    "128K",              /* cchar  *mstring */        /* match string */
    &cpu_set_size,       /* t_stat (*valid) */        /* validation routine */
    NULL,                /* t_stat (*disp)  */        /* display routine */
    NULL,                /* void *desc      */        /* value descriptor, REG* if MTAB_VAL, int* if not */
    NULL,                /* cchar *help     */        /* help string */
    },
    {UNIT_MSIZE, MEMAMOUNT(1), "256K", "256K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(2), "512K", "512K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(3),   "1M",   "1M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(4),   "2M",   "2M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(5),   "3M",   "3M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(6),   "4M",   "4M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(7),   "6M",   "6M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(8),   "8M",   "8M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(9),  "12M",  "12M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(10), "16M",  "16M", &cpu_set_size},
    { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
    {MTAB_XTD | MTAB_VDV | MTAB_NMO | MTAB_SHP, 0, "HISTORY", "HISTORY",
     &cpu_set_hist, &cpu_show_hist},
    {0}
};

/* CPU device descriptor */
DEVICE cpu_dev = {
    /* "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 24, 1, 8, 32,
    &cpu_ex, &cpu_dep, &cpu_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description */
    "CPU",               /* cchar *name */            /* device name */
    &cpu_unit,           /* UNIT *units */            /* unit array */
    cpu_reg,             /* REG *registers */         /* register array */
    cpu_mod,             /* MTAB *modifiers */        /* modifier array */
    1,                   /* uint32 numunits */        /* number of units */
    16,                  /* uint32 aradix */          /* address radix */
    32,                  /* uint32 awidth */          /* address width */
    1,                   /* uint32 aincr */           /* address increment */
    16,                  /* uint32 dradix */          /* data radix */
    8,                   /* uint32 dwidth */          /* data width */
    &cpu_ex,             /* t_stat (*examine) */      /* examine routine */
    &cpu_dep,            /* t_stat (*deposit) */      /* deposit routine */
    &cpu_reset,          /* t_stat (*reset) */        /* reset routine */
    NULL,                /* t_stat (*boot) */         /* boot routine */
    NULL,                /* t_stat (*attach) */       /* attach routine */
    NULL,                /* t_stat (*detach) */       /* detach routine */
    NULL,                /* void *ctxt */             /* (context) device information block pointer */
    DEV_DEBUG,           /* uint32 flags */           /* device flags */
    0,                   /* uint32 dctrl */           /* debug control flags */
    dev_debug,           /* DEBTAB *debflags */       /* debug flag name array */
    NULL,                /* t_stat (*msize) */        /* memory size change routine */
    NULL,                /* char *lname */            /* logical device name */
    &cpu_help,           /* t_stat (*help) */         /* help function */
    NULL,                /* t_stat (*attach_help) */  /* attach help function */
    NULL,                /* void *help_ctx */         /* Context available to help routines */
    &cpu_description,    /* cchar *(*description) */  /* Device description */
    NULL,                /* BRKTYPTB *brk_types */    /* Breakpoint types */
};

/* CPU Instruction decode flags */
#define INV     0x0000            /* Instruction is invalid */
#define HLF     0x0001            /* Half word instruction */
#define ADR     0x0002            /* Normal addressing mode */
#define IMM     0x0004            /* Immediate mode */
#define WRD     0x0008            /* Word addressing, no index */
#define SCC     0x0010            /* Sets CC */
#define RR      0x0020            /* Read source register */
#define R1      0x0040            /* Read destination register */
#define RB      0x0080            /* Read base register into dest */
#define SD      0x0100            /* Stores into destination register */
#define RNX     0x0200            /* Reads memory without sign extend */
#define RM      0x0400            /* Reads memory */
#define SM      0x0800            /* Stores memory */
#define DBL     0x1000            /* Double word operation */
#define SB      0x2000            /* Store Base register */
#define BT      0x4000            /* Branch taken, no PC incr */
#define SF      0x8000            /* Special flag */

int nobase_mode[] = {
   /*    00            04             08             0C  */
   /*    00            ANR,           ORR,           EOR */ 
         HLF,        SCC|R1|RR|SD|HLF, SCC|R1|RR|SD|HLF, SCC|R1|RR|SD|HLF, 

   /*    10            14             18             1C */
   /*    CAR,          CMR,           SBR            ZBR */
         HLF,          HLF,           HLF,           HLF,

   /*    20            24             28             2C  */
   /*    ABR           TBR            REG            TRR  */
         HLF,          HLF,           HLF,           HLF, 

   /*    30            34             38             3C */
   /*    CALM          LA             ADR            SUR */
       HLF,            SD|ADR,        HLF,           HLF,

   /*    40            44             48             4C  */ 
   /*    MPR           DVR                             */
      SCC|SD|HLF,      HLF,           HLF|INV,       HLF|INV, 

   /*    50            54             58             5C */
   /*                                                 */
        HLF|INV,       HLF|INV,       HLF|INV,       HLF|INV,

   /*    60            64             68             6C   */
   /*    NOR           NORD           SCZ            SRA  */
         HLF,          HLF,           HLF,           HLF, 

   /*    70            74             78             7C */
   /*    SRL           SRC            SRAD           SRLD */ 
         HLF,          HLF,           HLF,           HLF,

   /*    80            84             88             8C   */
   /*    LEAR          ANM            ORM            EOM  */
       SD|ADR,  SD|RR|RNX|ADR,  SD|RR|RNX|ADR,  SD|RR|RNX|ADR,  

   /*    90            94             98             9C */
   /*    CAM           CMM            SBM            ZBM  */ 
      SCC|RR|RM|ADR,   RR|RM|ADR,     ADR,           ADR,

   /*    A0            A4             A8             AC  */
   /*    ABM           TBM            EXM            L    */
         ADR,          ADR,           ADR,        SCC|SD|RM|ADR,

   /*    B0            B4             B8             BC */
   /*    LM            LN             ADM            SUM  */ 
//   SCC|SD|RM|ADR,    SCC|SD|RM|ADR,  SCC|SD|RR|RM|ADR,  SCC|SD|RR|RM|ADR,
     SCC|SD|RM|ADR,    SCC|SD|RM|ADR,  SD|RR|RM|ADR,  SD|RR|RM|ADR,

   /*    C0            C4             C8             CC    */
   /*    MPM           DVM            IMM            LF  */
     SCC|SD|RM|ADR,    RM|ADR,        IMM,           ADR, 

   /*    D0            D4             D8             DC */
   /*    LEA           ST             STM            STF */ 
     SD|ADR,           RR|SM|ADR,     RR|SM|ADR,     ADR,  

   /*    E0            E4             E8             EC   */
   /*    ADF           MPF            ARM            BCT  */
     ADR,           ADR,      SM|RR|RNX|ADR,  ADR, 

   /*    F0            F4             F8             FC */
   /*    BCF           BI             MISC           IO */ 
        ADR,           RR|SD|WRD,     ADR,           IMM,  
};

int base_mode[] = {
   /* 00        04            08         0C      */
   /* 00        AND,          OR,        EOR  */
//   HLF,      SCC|R1|RR|SD|HLF,     SCC|R1|RR|SD|HLF,     SCC|R1|RR|SD|HLF,  
     HLF,      R1|RR|SD|HLF,     SCC|R1|RR|SD|HLF,     SCC|R1|RR|SD|HLF,  

   /* 10        14           18        1C  */
   /* SACZ      CMR         xBR         SRx */
     HLF,       HLF,        HLF,        HLF,

   /* 20        24            28         2C   */
   /* SRxD      SRC          REG        TRR      */
     HLF,       HLF,         HLF,       HLF,    

   /* 30        34          38           3C */
   /*           LA          FLRop        SUR */
    INV,        INV,       HLF,      HLF,

   /* 40        44            48         4C     */
   /*                                        */
      INV,      INV,        INV,       INV, 

    /* 50       54          58            5C */
   /*  LA       BASE        BASE          CALLM */ 
    SD|ADR,     SM|ADR,     SB|ADR,    RM|ADR,

   /* 60        64            68         6C     */
   /*                                         */
      INV,      INV,         INV,      INV,   

   /* 70       74           78           7C */
   /*                                          */ 
    INV,     INV,        INV,        INV,  

   /* LEAR      ANM          ORM        EOM   */
   /* 80        84            88         8C   */
    SD|ADR,    SD|RR|RNX|ADR, SD|RR|RNX|ADR, SD|RR|RNX|ADR, 

   /* CAM       CMM           SBM        ZBM  */ 
   /* 90        94            98         9C   */
    SCC|RR|RM|ADR, RR|RM|ADR,   ADR,     ADR,

   /* A0        A4            A8         AC   */
   /* ABM       TBM           EXM        L    */
      ADR,      ADR,          ADR,    SCC|SD|RM|ADR,

   /* B0        B4            B8         BC   */
   /* LM        LN            ADM        SUM  */ 
// SCC|SD|RM|ADR,   SCC|SD|RM|ADR,  SCC|SD|RR|RM|ADR, SCC|SD|RR|RM|ADR,
   SCC|SD|RM|ADR,   SCC|SD|RM|ADR,      SD|RR|RM|ADR,     SD|RR|RM|ADR,

   /* C0        C4            C8         CC   */
   /* MPM       DVM           IMM        LF   */
    SCC|SD|RM|ADR,  RM|ADR,       IMM,       ADR, 

   /*  D0       D4            D8         DC */
   /*  LEA      ST            STM        STFBR */ 
     INV,       RR|SM|ADR,    RR|SM|ADR,    ADR,  

   /* E0        E4            E8         EC     */
   /* ADF       MPF           ARM        BCT      */
    ADR,     ADR,     SM|RR|RNX|ADR,    ADR, 

   /* F0        F4            F8         FC */
  /*  BCF       BI            MISC       IO */ 
    ADR,        RR|SD|WRD,    ADR,       IMM,  
};

/* Map image descriptor 32/77 */
/* |--------------------------------------| */
/* |0|1|2|3 4 5 6|7 8  9 10 11 12 13 14 15| */
/* |N|V|P|  n/u  | 9 bit map block entry  | */
/* |U| | |       |      32kb/block        | */
/* |             |  32 8kb maps per task  | */
/* |             |   1 mb address space   | */
/* |--------------------------------------| */

/* Map image descriptor 32/27 */
/* |--------------------------------------| */
/* |0|1|2|3|4|5 6 7 8  9 10 11 12 13 14 15| */
/* |V|P|P|P|P|    11 bit map block entry  | */
/* | |1|2|3|4|           8kb/block        | */
/* |         |    256 8kb maps per task   | */
/* |         |      2 mb address space    | */
/* |--------------------------------------| */

/* Map image descriptor  32/67, 32/87, 32/97 */
/* |--------------------------------------| */
/* |0|1|2|3|4|5 6 7 8  9 10 11 12 13 14 15| */
/* |V|P|P|P|P|    11 bit map block entry  | */
/* | |1|2|3|4|           2kb/block        | */
/* |         |    2048 8kb maps per task  | */
/* |         |      16 mb address space   | */
/* |--------------------------------------| */
/* BIT 0 = 0    Invalid map block (page) entry */
/*       = 1    Valid map block (page) entry */
/*     1 = 0    000-7ff of 8kb page is not write protected */
/*       = 1    000-7ff of 8kb page is write protected */
/*     2 = 0    800-fff of 8kb page is not write protected */
/*       = 1    800-fff of 8kb page is write protected */
/*     3 = 0    1000-17ff of 8kb page is not write protected */
/*       = 1    1000-17ff of 8kb page is write protected */
/*     4 = 0    1800-1fff of 8kb page is not write protected */
/*       = 1    1800-1fff of 8kb page is write protected */
/*  5-15 =      11 most significant bits of the 24 bit real address for page */

/* Map image descriptor V6 & V9 */
/* |--------------------------------------| */
/* |0|1|2|3|4|5 6 7 8  9 10 11 12 13 14 15| */
/* |V|P|P|M|M|    11 bit map block entry  | */
/* | |1|2|M|A|           2kb/map          | */
/* |         |    2048 8kb maps per task  | */
/* |         |      16 mb address space   | */
/* |--------------------------------------| */
/* BIT 0 = 0    Invalid map block (page) entry */
/*       = 1    Valid map block (page) entry */
/* */
/* PSD 1 BIT 0 -  Map Bit 1 - Map Bit 2 - Access state */
/* Priv Bit */
/*     0              0           0     No access allowed to page */
/*     0              0           1     No access allowed to page */
/*     0              1           0     Read/Write/Execute access */
/*     0              1           1     Read/Execute access only */
/*     1              0           0     Read/Write/Execute access */
/*     1              0           1     Read/Execute access only */
/*     1              1           0     Read/Write/Execute access */
/*     1              1           1     Read/Execute access only */
/* */
/* BIT 3 = 0    (MM) A first write (modify) to the map block (page) has not occurred */
/*       = 1    (MM) A first write (modify) to the map block (page) has occurred */
/* BIT 4 = 0    (MA) A first read or write (access) to the map block (page) has not occurred */
/*       = 1    (MA) A first read or write (access) to the map block (page) has occurred */
/*  5-15 =      11 most significant bits of the 24 bit real address for page */
/* Note */
/* If a map is valid, a MAP (page) hit occurs and logical to physical translation occures */
/* If the map is not valid, a demand MAP (page) fault occures and the faulting page is provided */
/* P1 and P2 are used with Bit 0 of PSD to define the access rights */
/* A privilege violation trap occurres if access it denied */
/* Bits 5-15 contain the 11 most-significant bits of the physical address */
/* MSD 0 page limit is used to verify access to O/S pages */
/* CPIX page limit is used to verify access to user pages and page faults */
/* CPIX base address ss used for user address translation */
/* Access to pages outside the limit registers results in a map fault */

#define MAX32       32      /* 32/77 map limit */
#define MAX256      256     /* 32/27 and 32/87 map limit */
#define MAX2048     2048    /* 32/67, V6, and V9 map limit */
#define RMB(x) ((M[x>>2]>>(8*(7-(x&3))))&0xff)      /* read memory addressed byte */
#define RMH(x) (x&2?(M[x>>2]&RMASK):(M[x>>2]>>16)&RMASK)    /* read memory addressed halfword */
#define RMW(x) (M[x>>2])                            /* read memory addressed word */
#define WMW(x,y) (M[x>>2]=y)                        /* write memory addressed word */
/* write halfword to memory address */
#define WMH(x,y) (x&2?(M[x>>2]=(M[x>>]&LMASK)|(y&RMASK)):(M[>>2]=(M[x>>2]&RMASK)|(y<<16)))
/* write halfword map register MAP cache address */
#define WMR(x,y) (x&2?(MAPC[x>>2]=(MAPC[x>>2]&LMASK)|(y&RMASK)):(MAPC[x>>2]=(MAPC[x>>2]&RMASK)|(y<<16)))

/* set up the map registers for the current task in the cpu */
/* the PSD bpix and cpix are used to setup the maps */
/* return non-zero if mapping error */
t_stat load_maps(uint32 thepsd[2])
{
    uint32 num, sdc, spc;
    uint32 mpl, cpixmsdl, bpixmsdl, msdl, midl;
    uint32 cpix, bpix, i, j, map, osmidl;
    uint32 MAXMAP = MAX2048;                        /* default to 2048 maps */

    if (CPU_MODEL < MODEL_27) {
        MAXMAP = MAX32;                             /* 32 maps for 32/77 */
        /* 32/7x machine, 8KW maps 32 maps total */
        modes &= ~BASEBIT;                          /* no basemode on 7x */
        if ((thepsd[1] & 0xc0000000) == 0) {        /* mapped mode? */
            return ALLOK;                           /* no, all OK, no mapping required */
        }
        /* we are mapped, so load the maps for this task into the cpu map cache */
        cpix = (thepsd[1] >> 2) & 0xfff;            /* get cpix 12 bit offset from psd wd 2 */
        bpix = (thepsd[1] >> 18) & 0xfff;           /* get bpix 12 bit offset from psd wd 2 */
        num = 0;                                    /* working map number */
        /* master process list is in 0x83 of spad for 7x */
        mpl = SPAD[0x83] >> 2;                      /* get mpl from spad address */
        cpixmsdl = M[mpl + cpix];                   /* get msdl from mpl for given cpix */

        /* if bit zero of mpl entry is set, use bpix first to load maps */
        if (cpixmsdl & BIT0) {
            /* load bpix maps first */
            bpixmsdl = M[mpl + bpix];               /* get bpix msdl word address */
            sdc = (bpixmsdl >> 24) & 0x3f;          /* get 6 bit segment description count */
            msdl = (bpixmsdl >> 2) & 0x3fffff;      /* get 24 bit real address of msdl */
            for (i = 0; i < sdc; i++) {             /* loop through the msd's */
                spc = (M[msdl + i] >> 24) & 0xff;   /* get segment page count from msdl */
                midl = (M[msdl + i] >> 2) & 0x3fffff;   /* get 24 bit real word address of midl */

                for (j = 0; j < spc; j++, num++) {  /* loop throught the midl's */
                    if (num >= MAXMAP)
                        return MAPFLT;              /* map loading overflow, map fault error */
                    /* load 16 bit map descriptors */
                    map = (M[midl + (j / 2)]);      /* get 2 16 bit map entries */
                    if (j & 1)
                        map = (map & RMASK);        /* use right half word map entry */
                    else
                        map = ((map >> 16) & RMASK);    /* use left half word map entry */
                    /* the map register contents is now in right 16 bits */
                    /* now load a 32 bit word with both maps from memory  */
                    /* and or in the new map entry data */
                    /* num has the number of maps already loaded */
                    if (num & 1) {
                        /* entry going to rt hw, clean it first */
                        map = (MAPC[num/2] & LMASK) | map;  /* map is in rt hw */
                    }
                    else {
                        /* entry going to left hw, clean it first */
                        map = (MAPC[num/2] & RMASK) | (map << 16);  /* map is in left hw */
                    }
                    MAPC[num/2] = map;              /* store the map reg contents into cache */
                }
            }
        }
        /* now load cpix maps */
        cpixmsdl = M[mpl + cpix];                   /* get cpix msdl word address */
        sdc = (cpixmsdl >> 24) & 0x3f;              /* get 6 bit segment description count */
        msdl = (cpixmsdl >> 2) & 0x3fffff;          /* get 24 bit real address of msdl */
        for (i = 0; i < sdc; i++) {
            spc = (M[msdl + i] >> 24) & 0xff;       /* get segment page count from msdl */
            midl = (M[msdl + i] >> 2) & 0x3fffff;   /* get 24 bit real word address of midl */

            for (j = 0; j < spc; j++, num++) {      /* loop throught the midl's */
                if (num >= MAXMAP)
                    return MAPFLT;                  /* map loading overflow, map fault error */
                /* load 16 bit map descriptors */
                map = (M[midl + (j / 2)]);          /* get 2 16 bit map entries */
                if (j & 1)
                    map = (map & RMASK);            /* use right half word map entry */
                else
                    map = ((map >> 16) & RMASK);    /* use left half word map entry */
                /* the map register contents is now in right 16 bits */
                /* now load a 32 bit word with both maps from memory  */
                /* and or in the new map entry data */
                if (num & 1) {
                    /* entry going to rt hw, clean it first */
                    map = (MAPC[num/2] & LMASK) | map;  /* map is in rt hw */
                }
                else {
                    /* entry going to left hw, clean it first */
                    map = (MAPC[num/2] & RMASK) | (map << 16);  /* map is in left hw */
                }
                MAPC[num/2] = map;                  /* store the map reg contents into cache */
            }
        }
        /* if none loaded, map fault */
        if (num == 0)
            return MAPFLT;                          /* map fault error */
        if (num & 1) {                              /* clear rest of maps */
            /* left hw of map is good, zero right */
            map = (MAPC[num/2] & LMASK);            /* clean rt hw */
            MAPC[num++/2] = map;                    /* store the map reg contents into cache */
        }
        /* num should be even at this point, so zero 32 bit words for remaining maps */
        if ((num/2) > HIWM)                         /* largerst number of maps loaded so far */
            HIWM = num/2;                           /* yes, set new high water mark */
        for (i = num/2; i < HIWM; i++)              /* zero any remaining entries */
            MAPC[i] = 0;                            /* clear the map entry to make not valid */
        HIWM = num/2;                               /* set new high water mark */
        return ALLOK;                               /* all cache is loaded, return OK */
    }

    /* process a 32/27, 32/67, 32/87, 32/97, V6, or V9 here with 2KW (8kb) maps */
    /* 32/27 & 32/87 have 256 maps. Others have 2048 maps */
    /* Concept/32 machine, 2KW maps */
    if ((modes & MAPMODE) == 0) {                   /* mapped mode? */
        return ALLOK;                               /* no, all OK, no mapping required */
    }
    if ((CPU_MODEL == MODEL_27) || (CPU_MODEL == MODEL_87))
        MAXMAP = MAX256;                            /* only 256 2KW (8kb) maps */

    /* we are mapped, so calculate real address from map information */
    cpix = PSD2 & 0x3ff8;                           /* get cpix 11 bit offset from psd wd 2 */
    num = 0;                                        /* no maps loaded yet */
    /* master process list is in 0xf3 of spad for concept */
    mpl = SPAD[0xf3];                               /* get mpl from spad address */
    midl = RMW(mpl+cpix);                           /* get mpl entry wd 0 for given cpix */
    msdl = RMW(mpl+cpix+4);                         /* get mpl entry wd 1 for given cpix */

    /* load msd 0 maps first (O/S) */
    osmidl = RMW(mpl);                              /* get midl 0 word address */
//    fprintf(stderr, "loading maps MAXMAP %x cpix %x mpl %x midl %x msdl %x osmidl %x\n",
 //           MAXMAP, cpix, mpl, midl, msdl, osmidl);

    /* if bit zero of cpix mpl entry is set, use msd entry 0 first to load maps */
    /* This test must be made (cpix == bpix) to allow sysgen to run without using */
    /* a valid cpix */
    /* the cpix is zero indicating only load MSD 0 for the target system */
    /* bit 0 of msd 0 will be zero saying load the maps */
    if ((osmidl == midl) || (midl & BIT0)) {
        /* Do not load O/S if already loaded. Bit zero of O/S midl will be set by */
        /* swapper on startup */
        /* load msd 0 maps first (O/S) */
        spc = osmidl & MASK16;                      /* get 16 bit segment description count */
        if (osmidl & BIT0) {                        /* see if O/S already loaded */
            num = spc;                              /* set the number of o/s maps loaded */
            goto skipos;                            /* skip OS map loading */
        }
        midl = RMW(mpl+4) & MASK24;                 /* get 24 bit real address from mpl 0 wd2 */
        for (j = 0; j < spc; j++, num++) {          /* copy maps from midl to map cache */
            if (num > MAXMAP)
                return MAPFLT;                      /* map loading overflow, map fault error */
            /* load 16 bit map descriptors */
            map = RMH(midl+(j<<1));
            /* translate the map number to a real address */
            /* put this address in the LTB for later translation */
            /* copy the map status bits too and set hit bit */
            TLB[num] = ((map & 0x7ff) << 13) | ((map & 0xf800) << 16) | 0x04000000;
            WMR((num*2),map);                       /* store the map reg contents into cache */
        }
    }
skipos:
    /* sysgen in mpx does not have a valid cpix MPL entry, only a bpix entry */
    /* that entry uses 64 map entries to map between target/host systems */
    /* When cpix in instruction is zero, just load the O/S specified by MSD 0 */
    if (cpix == 0)
        goto skipcpix;                              /* only load maps specified by msd 0 */

    /* now load user maps specified by the cpix value */
    midl = RMW(mpl+cpix);                           /* get cpix midl word address */
    msdl = RMW(mpl+cpix+4);                         /* get 24 bit real word address of midl */
    spc = midl & RMASK;                             /* get segment page count from msdl */
    midl = RMW(mpl+cpix+4) & MASK24;                /* get 24 bit real word address of midl */
//    fprintf(stderr, "loading maps MAXMAP %x cpix %x spc %x midl %x msdl %x osmidl %x num %x\n",
//            MAXMAP, cpix, spc, midl, msdl, osmidl, num);
    for (j = 0; j < spc; j++, num++) {              /* copy maps from midl to map cache */
        if (num > MAXMAP)
            return MAPFLT;                          /* map loading overflow, map fault error */
        /* load 16 bit map descriptors */
        map = RMH(midl+(j<<1));                     /* get 16 bit map entry */
        /* translate the map number to a real address */
        /* put this address in the LTB for later translation */
        /* copy the map status bits too */
        TLB[num] = ((map & 0x7ff) << 13) | ((map & 0xf800) << 16) | 0x04000000;
        WMR((num*2),map);                           /* store the map reg contents into cache */
    }
    /* if none loaded, map fault */
    /* if we got here without a map block found, return map fault error */
    if (num == 0)
        return MAPFLT;                              /* map fault error */
skipcpix:
    if (num & 1) {
        /* last map was in left hw, zero right halfword */
        WMR((num*2),0);                             /* zero the map reg contents in cache */
        TLB[num++] = 0;                             /* zero the TLB entry too */
    }
//    fprintf(stderr, "loading maps done MAXMAP %x cpix %x spc %x midl %x msdl %x osmidl %x num %x HIWM %x\n",
//           MAXMAP, cpix, spc, midl, msdl, osmidl, num, HIWM);
    /* now clear any map entries left over from previous map */
    if ((num/2) < HIWM) {                           /* largest number of maps loaded so far */
        /* we need to zero the left over entries from previous map */
        /* num should be even at this point, so zero 32 bit words for remaining maps */
        for (i = num/2; i < HIWM; i++) {            /* zero any remaining entries */
            MAPC[i] = 0;                            /* clear the map entry to make not valid */
            TLB[i*2] = 0;                           /* zero the TLB entry */
            TLB[(i*2)+1] = 0;                       /* zero the TLB entry */
        }
    }
    HIWM = num/2;                                   /* set new high water mark */
    return ALLOK;                                   /* all cache is loaded, return OK */
}

/*
 * Return the real memory address from the logical address
 * Also return the protection status, 1 if write protected address
 * addr is byte address
 */
t_stat RealAddr(uint32 addr, uint32 *realaddr, uint32 *prot)
{
    uint32 word, index, map, mask, raddr;

    *prot = 0;      /* show unprotected memory as default */
                    /* unmapped mode is unprotected */

    /* see what machine we have */
    if (CPU_MODEL < MODEL_27)
    {
        /* 32/7x machine with 8KW maps */
        if (modes & EXTDBIT)
            word = addr & 0xfffff;              /* get 20 bit logical word address */
       else
            word = addr & 0x7ffff;              /* get 19 bit logical word address */
        if ((modes & MAPMODE) == 0) {
            /* check if valid real address */
            if (word >= (MEMSIZE*4))            /* see if address is within our memory */
                return NPMEM;                   /* no, none present memory error */
            *realaddr = word;                   /* return the real address */
            return ALLOK;                       /* all OK, return instruction */
        }
        /* we are mapped, so calculate real address from map information */
        /* 32/7x machine, 8KW maps */
        index = word >> 15;                     /* get 4 or 5 bit value */
        map = MAPC[index/2];                    /* get two hw map entries */
        if (index & 1)
            /* entry is in rt hw, clear left hw */
            map &= RMASK;                       /* map is in rt hw */
        else
            /* entry is in left hw, move to rt hw */
            map >>= 16;                         /* map is in left hw */
        /* see if map is valid */
        if (map & 0x4000) {
            /* required map is valid, get 9 bit address and merge with 15 bit page offset */
            word = ((map & 0x1ff) << 15) | (word & 0x7fff);
            /* check if valid real address */
            if (word >= (MEMSIZE*4))            /* see if address is within our memory */
                return NPMEM;                   /* no, none present memory error */
            if ((modes & PRIVBIT) == 0) {       /* see if we are in unprivileged mode */
                if (map & 0x2000)               /* check if protect bit is set in map entry */
                    *prot = 1;                  /* return memory write protection status */
            }
            *realaddr = word;                   /* return the real address */
            return ALLOK;                       /* all OK, return instruction */
        }
        /* map is invalid, so return map fault error */
        return MAPFLT;                          /* map fault error */
    }
    else
    if (CPU_MODEL < MODEL_V6) {
        /* 32/27, 32/67, 32/87, 32/97 2KW maps */
        /* Concept 32 machine, 2KW maps */
        if (modes & (BASEBIT | EXTDBIT))
            word = addr & 0xffffff;             /* get 24 bit address */
        else
            word = addr & 0x7ffff;              /* get 19 bit address */
        if ((modes & MAPMODE) == 0) {
            /* we are in unmapped mode, check if valid real address */
            if (word >= (MEMSIZE*4))            /* see if address is within our memory */
                return NPMEM;                   /* no, none present memory error */
            *realaddr = word;                   /* return the real address */
            return ALLOK;                       /* all OK, return instruction */
        }
        /* we are mapped, so calculate real address from map information */
        /* get 11 bit page number from address bits 8-18 */
        index = (word >> 13) & 0x7ff;           /* get 11 bit value */
        raddr = TLB[index];                     /* get the base address & bits */
        if (raddr == 0)                         /* see if valid address */
            return MAPFLT;                      /* no, map fault error */
        /* check if valid real address */
        if ((raddr & 0xffffff) >= (MEMSIZE*4))  /* see if address is within our memory */
            return NPMEM;                       /* no, none present memory error */
        word = (raddr & 0xffe000) | (word & 0x1fff);   /* combine map and offset */
        *realaddr = word;                       /* return the real address */
//    if (PSD2 & 0x80000000)
//    fprintf(stderr, "RealAddr page %x realaddr = %x raddr %x\n", index, word, raddr);
#ifndef LATER
        /* get protection status of map */
        index = (word >> 11) & 0x3;             /* see which 1/4 page we are in */
        if ((BIT1 >> index) & raddr) {          /* is 1/4 page write protected */
            *prot = 1;                          /* return memory write protection status */
        }
#endif
        return ALLOK;                           /* all OK, return instruction */
    }
    else
    {
        /* handle V6 & V9 here */
        /* Concept 32 machine, 2KW maps */
        if (modes & (BASEBIT | EXTDBIT))
            word = addr & 0xffffff;             /* get 24 bit address */
        else
            word = addr & 0x7ffff;              /* get 19 bit address */
        if ((modes & MAPMODE) == 0) {
            /* check if valid real address */
            if (word >= (MEMSIZE*4))            /* see if address is within our memory */
                return NPMEM;                   /* no, none present memory error */
            *realaddr = word;                   /* return the real address */
            return ALLOK;                       /* all OK, return instruction */
        }
        /* we are mapped, so calculate real address from map information */
        /* get 11 bit page number from address bits 8-18 */
        index = (word >> 13) & 0x7ff;           /* get 11 bit value */
        raddr = TLB[index];                     /* get the base address & bits */
        if (raddr == 0)                         /* see if valid address */
            return MAPFLT;                      /* no, map fault error */
        /* check if valid real address */
        if ((raddr & 0xffffff) >= (MEMSIZE*4))  /* see if address is within our memory */
            return NPMEM;                       /* no, none present memory error */
        word = (raddr & 0xffe000) | (word & 0x1fff);   /* combine map and offset */
        *realaddr = word;                       /* return the real address */
        /* get protection status bits in map, combine with priv bit in psd 1 */
        /* access bits in bits 24-26 and bit 31 set indicating status returned */
        *prot = (((PSD1 & BIT0) | (raddr & 0x60000000)) >> 24) | 1;
        return ALLOK;                           /* all OK, return instruction */
    }
}

/* fetch the current instruction from the PC address */
t_stat read_instruction(uint32 thepsd[2], uint32 *instr)
{
    uint32 addr, status;

    if (CPU_MODEL < MODEL_27)
    {
        /* 32/7x machine with 8KW maps */
        /* instruction must be in first 512KB of address space */
        addr = thepsd[0] & 0x7fffc;             /* get 19 bit logical word address */
    }
    else
    {
        /* 32/27, 32/67, 32/87, 32/97 2KW maps */
        /* Concept 32 machine, 2KW maps */
        if (thepsd[0] & BASEBIT) {              /* bit 6 is base mode? */
            addr = thepsd[0] & 0xfffffc;        /* get 24 bit address */
        }
        else
            addr = thepsd[0] & 0x7fffc;         /* get 19 bit address */
    }
    status = Mem_read(addr, instr);             /* get the instruction at the specified address */
    if (status == DMDPG) {                      /* demand page request */
        *instr |= 0x80000000;                   /* set instruction fetch paging error */
        pfault = *instr;                        /* save page number */
    }
    sim_debug(DEBUG_DETAIL, &cpu_dev, "read_instr status = %x\n", status);
    return status;                              /* return ALLOK or ERROR */
}

/*
 * Read a full word from memory
 * Return error type if failure, ALLOK if
 * success.  Addr is logical byte address.
 */
t_stat Mem_read(uint32 addr, uint32 *data)
{
    uint32 status, realaddr, prot, raddr, page;

    status = RealAddr(addr, &realaddr, &prot);  /* convert address to real physical address */
    sim_debug(DEBUG_DETAIL, &cpu_dev, "Mem_read status = %x\n", status);
    if (status == ALLOK) {
        *data = M[realaddr >> 2];               /* valid address, get physical address contents */
        if ((CPU_MODEL >= MODEL_V6) && (modes & MAPMODE)) {
            /* for v6 & v9, check if we have read access */
            if ((prot & 0xe0) == 0 || (prot & 0xe0) == 0x20) {
                /* user has no access, do protection violation */
                return MPVIOL;                  /* return memory protection violation */
            }
            /* everybody else has read access */
            page = (addr >> 13) & 0x7ff;        /* get 11 bit value */
            raddr = TLB[page];                  /* get the base address & bits */
            if ((raddr & BIT0) == 0) {          /* see if page is valid */
                /* not valid, but mapped, so do a demand page request */
                *data = page;                   /* return the page # */
                pfault = page;                  /* save page number */
                return DMDPG;                   /* demand page request */
            }
        }
        sim_debug(DEBUG_DETAIL, &cpu_dev, "Mem_read addr %.8x realaddr %.8x data %.8x prot %d\n",
            addr, realaddr, *data, prot);
    }
    return status;                              /* return ALLOK or ERROR status */
}

/*
 * Write a full word to memory, checking protection
 * and alignment restrictions. Return 1 if failure, 0 if
 * success.  Addr is logical byte address, data is 32bit word
 */
t_stat Mem_write(uint32 addr, uint32 *data)
{
    uint32 status, realaddr, prot, raddr, page;

    status = RealAddr(addr, &realaddr, &prot);  /* convert address to real physical address */
    if (prot)
//    fprintf(stderr, "Mem_write addr %.8x realaddr %.8x data %.8x prot %d\n",
//        addr, realaddr, *data, prot);
    sim_debug(DEBUG_DETAIL, &cpu_dev, "Mem_write addr %.8x realaddr %.8x data %.8x prot %d\n",
        addr, realaddr, *data, prot);
    if (status == ALLOK) {
        if ((CPU_MODEL >= MODEL_V6) && (modes & MAPMODE)) {
            /* for v6 & v9, check if we have write access */
            if (((prot & 0xe0) != 0x40) && ((prot & 0xe0) != 0x80) && ((prot & 0xe0) != 0xc0)){
                /* user has no write access, do protection violation */
                return MPVIOL;                  /* return memory protection violation */
            }
            /* everything else has read access */
            page = (addr >> 13) & 0x7ff;        /* get 11 bit value */
            raddr = TLB[page];                  /* get the base address & bits */
            if ((raddr & BIT0) == 0) {          /* see if page is valid */
                /* not valid, but mapped, so do a demand page request */
                *data = page;                   /* return the page # */
                pfault = page;                  /* save page number */
                return DMDPG;                   /* demand page request */
            }
        } else {
            if (prot)                           /* check for write protected memory */
                return MPVIOL;                  /* return memory protection violation */
        }
        M[realaddr >> 2] = *data;               /* valid address, put physical address contents */
    }
    return status;                              /* return ALLOK or ERROR */
}

/* function to set the CCs in PSD1 */
/* ovr is setting for CC1 */
void set_CCs(uint32 value, int ovr)
{
    PSD1 &= 0x87FFFFFE;                         /* clear the old CC's */
    if (ovr)
        CC = CC1BIT;                            /* CC1 value */
    else
        CC = 0;                                 /* CC1 off */
    if (value & FSIGN)
        CC |= CC3BIT;                           /* CC3 for neg */
    else if (value == 0)
        CC |= CC4BIT;                           /* CC4 for zero */
    else 
        CC |= CC2BIT;                           /* CC2 for greater than zero */
    PSD1 |= (CC & 0x78000000);                  /* update the CC's in the PSD */
}

/* Opcode definitions */
/* called from simulator */
t_stat sim_instr(void) {
    t_stat              reason = 0;       /* reason for stopping */
    t_uint64            dest;             /* Holds destination/source register */
    t_uint64            source;           /* Holds source or memory data */
    t_uint64            td;               /* Temporary */
    t_int64             int64a;           /* temp int */
    t_int64             int64b;           /* temp int */
    t_int64             int64c;           /* temp int */
    uint32              addr;             /* Holds address of last access */
    uint32              temp;             /* General holding place for stuff */
    uint32              IR;               /* Instruction register */
    uint32              i_flags;          /* Instruction description flags from table */
    uint32              t;                /* Temporary */
    uint32              temp2;            /* Temporary */
    uint32              bc;               /* Temporary bit count */
    uint16              opr;              /* Top half of Instruction register */
    uint16              OP;               /* Six bit instruction opcode */
    uint16              chan;             /* I/O channel address */
    uint16              lchan;            /* Logical I/O channel address */
    uint16              suba;             /* I/O subaddress */
    uint8               FC;               /* Current F&C bits */
    uint8               EXM_EXR=0;        /* PC Increment for EXM/EXR instructions */
    uint32              reg;              /* GPR or Base register bits 6-8 */
    uint32              sreg;             /* Source reg in from bits 9-11 reg-reg instructions */
    uint32              ix;               /* index register */
    uint32              dbl;              /* Double word */
    uint32              ovr;              /* Overflow flag */
    uint32              stopnext = 0;     /* Stop on next instruction */
    uint32              skipinstr = 0;    /* Skip test for interrupt on this instruction */
    uint32              drop_nop = 0;     /* Set if right hw instruction is a nop */
    uint32              int_icb;          /* interrupt context block address */
    uint32              OIR;              /* Original Instruction register */
    uint32              OPSD1;            /* Original PSD1 */
    uint32              OPSD2;            /* Original PSD2 */
    int32               int32a;           /* temp int */
    int32               int32b;           /* temp int */
    int32               int32c;           /* temp int */

wait_loop:
    while (reason == 0) {                       /* loop until halted */
        // wait_loop:
        if (sim_interval <= 0) {                /* event queue? */
            reason = sim_process_event();       /* process */
            if (reason != SCPE_OK) {
                if (reason == SCPE_STEP) {
//*FORSTEP*/           stopnext = 1;
                    break;
                } else
                    break;                      /* process */
            }
        }

        /* stop simulator if user break requested */
        if (sim_brk_summ && sim_brk_test(PC, SWMASK('E'))) {
            reason = STOP_IBKPT;
            break;
        }

        sim_interval--;                         /* count down */

#ifndef TRYTHIS_FOR_NONOP
#ifdef DO_NEW_NOP
        if (drop_nop) {                         /* need to drop a nop? */
            PSD1 = (PSD1 + 2) | (((PSD1 & 2) >> 1) & 1);    /* skip this instruction */
            drop_nop = 0;                       /* we dropped the nop */
        }
#else
        if (skipinstr) {                        /* need to skip interrupt test? */
//        if (skipinstr || drop_nop) {            /* need to skip interrupt test? */
            skipinstr = 2;                      /* skip only once, but test later */
            goto skipi;                         /* skip int test */
        }
#endif
#else
        if (skipinstr)                          /* need to skip interrupt test? */
            goto skipi;                         /* skip int test */
#endif

redo:
#ifdef DO_NEW_NOP
        if (skipinstr)                          /* need to skip interrupt test? */
            goto skipi;                         /* skip int test */
#endif
        /* process pending I/O interrupts */
        if (!loading && (wait4int || irq_pend)) {   /* see if ints are pending */
            int_icb = scan_chan();              /* no, go scan for I/O int pending */
            if (int_icb != 0) {                 /* was ICB returned for an I/O or interrupt */
                int il;
                /* find interrupt level for icb address */
                for (il=0; il<112; il++) {
                    /* get the address of the interrupt IVL table in main memory */
                    uint32 civl = SPAD[0xf1] + (il<<2); /* contents of spad f1 points to chan ivl in mem */
                    civl = M[civl >> 2];        /* get the interrupt context block addr in memory */
                    if (civl == int_icb)
                        break;
                }
                sim_debug(DEBUG_EXP, &cpu_dev, "Normal int scan return icb %x irq_pend %x wait4int %x\n",
                    int_icb, irq_pend, wait4int);
#ifdef TRME     /* set to 1 for traceme to work */
    if (traceme >= trstart) {
        fprintf(stderr, "Normal int scan return icb %x irq_pend %x wait4int %x\r\n",
                int_icb, irq_pend, wait4int);
    }
#endif
                /* take interrupt, store the PSD, fetch new PSD */
                bc = PSD2 & 0x3ffc;             /* get copy of cpix */
                M[int_icb>>2] = PSD1&0xfffffffe;    /* store PSD 1 */
                M[(int_icb>>2)+1] = PSD2;       /* store PSD 2 */
                PSD1 = M[(int_icb>>2)+2];       /* get new PSD 1 */
                PSD2 = (M[(int_icb>>2)+3] & ~0x3ffc) | bc;  /* get new PSD 2 w/old cpix */
                /* I/O status DW address will be in WD 6 */
                /* set new map mode and interrupt blocking state in CPUSTATUS */
                modes = PSD1 & 0x87000000;      /* extract bits 0, 5, 6, 7 from PSD 1 */
                if (PSD2 & MAPBIT) {
                    CPUSTATUS |= 0x00800000;    /* set bit 8 of cpu status */
                    modes |= MAPMODE;           /* set mapped mode */
                } else
                    CPUSTATUS &= 0xff7fffff;    /* reset bit 8 of cpu status */
                if ((PSD2 & 0x8000) == 0) {     /* is it retain blocking state */
                    if (PSD2 & 0x4000) {        /* no, is it set blocking state */
                        CPUSTATUS |= 0x80;      /* yes, set blk state in cpu status bit 24 */
                        t = SPAD[il+0x80];      /* get spad entry for interrupt */
                        /* Class F I/O spec says to reset interrupt active if user's */
                        /* interrupt service routine runs with interrupts blocked */
                        if ((t & 0x0f000000) == 0x0f000000) { /* if class F clear interrupt */
                            /* if this is F class I/O interrupt, clear the active level */
                            /* SPAD entries for interrupts begin at 0x80 */
                            INTS[il] &= ~INTS_ACT;      /* deactivate specified int level */
                            SPAD[il+0x80] &= ~SINT_ACT; /* deactivate in SPAD too */
                        }
                    }
                    else
                        CPUSTATUS &= ~0x80;     /* no, reset blk state in cpu status bit 24 */
                }
                PSD2 &= ~0x0000c000;            /* clear bit 48 & 49 to be unblocked */
                if (CPUSTATUS & 0x80)           /* see if old mode is blocked */
                    PSD2 |= 0x00004000;         /* set to blocked state */
                PSD2 &= ~RETMBIT;               /* turn off retain bit in PSD2 */
                SPAD[0xf5] = PSD2;              /* save the current PSD2 */
                sim_debug(DEBUG_INST, &cpu_dev,
                    "Interrupt %x OPSD1 %.8x OPSD2 %.8x NPSD1 %.8x NPSD2 %.8x ICBA %x\n",
                    il, M[int_icb>>2], M[(int_icb>>2)+1], PSD1, PSD2, int_icb);
#ifdef TRME     /* set to 1 for traceme to work */
    if (traceme >= trstart) {
        fprintf(stderr, "Interrupt %x OPSD1 %.8x OPSD2 %.8x NPSD1 %.8x NPSD2 %.8x ICBA %x\r\n",
                il, M[int_icb>>2], M[(int_icb>>2)+1], PSD1, PSD2, int_icb);
    }
#endif
                wait4int = 0;                   /* wait is over for int */
                skipinstr = 1;                  /* skip next interrupt test after this instruction */
/*DIAG*/        goto skipi;                     /* skip int test */
            }
            /* see if waiting at a wait instruction */
            if (wait4int || loading) {
                /* tell simh we will be waiting */
                sim_idle(TMR_RTC, 1);           /* wait for clock tick */
                goto wait_loop;                 /* continue waiting */
            }
        } else {
            if (loading) {
                uint32 chsa  = scan_chan();     /* go scan for load complete pending */
                if (chsa != 0) {                /* see if a boot channel/subaddress were returned */
                    /* take interrupt, store the PSD, fetch new PSD */
                    PSD1 = M[0>>2];             /* PSD1 from location 0 */
                    PSD2 = M[4>>2];             /* PSD2 from location 4 */
                    modes = PSD1 & 0x87000000;  /* extract bits 0, 5, 6, 7 from PSD 1 */
                    sim_debug(DEBUG_INST, &cpu_dev, "Boot Loading PSD1 %.8x PSD2 %.8x\n", PSD1, PSD2);
                    /* set interrupt blocking state in CPUSTATUS */
                    CPUSTATUS |= 0x80;          /* set blocked state in cpu status, bit 24 too */
                    PSD2 &= ~RETMBIT;           /* turn off retain bit in PSD2 */
                    SPAD[0xf5] = PSD2;          /* save the current PSD2 */
                    loading = 0;                /* we are done loading */
                    skipinstr = 1;              /* skip next interrupt test only once */
                }
                goto wait_loop;                 /* continue waiting */
            }
            /* see if in wait instruction */
            if (wait4int) {                     /* keep waiting */
                /* tell simh we will be waiting */
                sim_idle(TMR_RTC, 1);           /* wait for clock tick */
                goto wait_loop;                 /* continue waiting */
            }
        }

        /* Check for external interrupt here */
        /* see if we have an attention request from console */
        if (!skipinstr && attention_trap) {
            TRAPME = attention_trap;            /* get trap number */
            attention_trap = 0;                 /* do only once */
            sim_debug(DEBUG_DETAIL, &cpu_dev, "Attention TRAP %x\n", TRAPME);
            skipinstr = 1;                      /* skip next interrupt test only once */
            goto newpsd;                        /* got process trap */
        }

skipi:
#ifdef DO_NEW_NOP
        skipinstr = 0;                          /* skip only once */
#endif
        if (sim_brk_summ && sim_brk_test(PC, SWMASK('E'))) {
            reason = STOP_IBKPT;
            break;
        }

        /* fill IR from logical memory address */
        if ((TRAPME = read_instruction(PSD, &IR))) {
            sim_debug(DEBUG_INST, &cpu_dev, "read_instr TRAPME = %x\n", TRAPME);
#ifndef DO_NEW_NOP
            skipinstr = 0;                      /* only test this once */
#endif
            /* if paging error, IR has page number with bit 0 set */
            goto newpsd;                        /* got process trap */
        }

        if (PSD1 & 2) {                         /* see if executing right half */
            /* we have a rt hw instruction */
            IR <<= 16;                          /* put instruction in left hw */
#ifndef TRYTHIS_FOR_NONOP
//            if ((CPU_MODEL <= MODEL_27) || (CPU_MODEL == MODEL_87)
//                    || (CPU_MODEL == MODEL_97)) {
            if ((CPU_MODEL <= MODEL_27) || (CPU_MODEL == MODEL_87) ||
                    (CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9)) {
                skipinstr = 0;                  /* only test this once */
#ifdef DO_NEW_NOP
                drop_nop = 0;                   /* not dropping nop for these machines */
#endif
                goto exec;                      /* old machines did not drop nop instructions */
            }
            /* We have 67, V6 or V9 */
            if (IR == 0x00020000) {             /* is this a NOP from rt hw? */
                PSD1 = (PSD1 + 2) | (((PSD1 & 2) >> 1) & 1);    /* skip this instruction */
//                fprintf(stderr, "RIGHT HW skip NOP instr %x skip nop at %x\n", IR, PSD1);
                if (skipinstr == 2) {           /* last instr was lf hw and rt NOP, try ints again */
                    skipinstr = 0;              /* only test this once */
                    goto redo;                  /* check for ints now */
                }
                skipinstr = 0;                  /* only test this once */
                goto skipi;                     /* go read next instruction */
            }
            skipinstr = 0;                      /* only test this once */
#endif
        } else {
            /* we have a left hw or fullword instruction */
#ifndef DO_NEW_NOP
            skipinstr = 0;                      /* only test this once */
#endif
            /* see if we can drop a rt hw nop instruction */
            OP = (IR >> 24) & 0xFC;             /* this is a 32/67 or above, get OP */
//            if ((CPU_MODEL <= MODEL_27) || (CPU_MODEL == MODEL_87)
//                    || (CPU_MODEL == MODEL_97))
            if ((CPU_MODEL <= MODEL_27) || (CPU_MODEL == MODEL_87) ||
                    (CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                goto exec;                      /* old machines did not drop nop instructions */
            if (PSD1 & BASEBIT)
                i_flags = base_mode[OP>>2];     /* set the BM instruction processing flags */
            else
                i_flags = nobase_mode[OP>>2];   /* set the NBM instruction processing flags */
            if ((i_flags & 0xf) == HLF) {       /* this is left HW instruction */
                if ((IR & 0xffff) == 0x0002) {  /* see if rt hw is a nop */
                    /* treat this as a fw instruction */
#ifndef DO_NEW_NOP
                    skipinstr = 2;              /* show we need to skip nop next time */
#else
                    drop_nop = 1;               /* show we need to skip nop next time */
#endif
//                  fprintf(stderr, "LEFT HW skip NOP instr %x skip nop at %x\r\n", IR, PSD1);
                }
            }
        }

exec:
/*FIXME temp saves for debugging */
        OIR = IR;                               /* save the instruction */
        OPSD1 = PSD1;                           /* save the old PSD1 */
        OPSD2 = PSD2;                           /* save the old PSD2 */

        /* TODO Update history for this instruction */
        if (hst_lnt) {
            hst_p += 1;                         /* next history location */
            if (hst_p >= hst_lnt)               /* check for wrap */
                hst_p = 0;                      /* start over at beginning */
            hst[hst_p].opsd1 = OPSD1;           /* set original psd1 */ 
            hst[hst_p].opsd2 = OPSD2;           /* set original psd2 */ 
            hst[hst_p].oir = OIR;               /* set original instruction */ 
        }

        /* Split instruction into pieces */
        PC = PSD1 & 0xfffffe;                   /* get 24 bit addr from PSD1 */
        sim_debug(DEBUG_DATA, &cpu_dev, "-----Instr @ PC %x PSD1 %.8x PSD2 %.8x IR %.8x\n", PC, PSD1, PSD2, IR);
        opr = (IR >> 16) & MASK16;              /* use upper half of instruction */
        OP = (opr >> 8) & 0xFC;                 /* Get opcode (bits 0-5) left justified */
        FC =  ((IR & F_BIT) ? 0x4 : 0) | (IR & 3);  /* get F & C bits for addressing */
        reg = (opr >> 7) & 0x7;                 /* dest reg or xr on base mode */
        sreg = (opr >> 4) & 0x7;                /* src reg for reg-reg instructions or BR instr */
        dbl = 0;                                /* no doubleword instruction */
        ovr = 0;                                /* no overflow or arithmetic exception either */
        dest = (t_uint64)IR;                    /* assume memory address specified */
        CC = PSD1 & 0x78000000;                 /* save CC's if any */
        /* changed for diags 052719*/
        modes = PSD1 & 0x87000000;              /* extract bits 0, 5, 6, 7 from PSD 1 */
        if (PSD2 & MAPBIT)
            modes |= MAPMODE;                   /* set mapped mode */

        if (modes & BASEBIT) {
            i_flags = base_mode[OP>>2];         /* set the instruction processing flags */
            addr = IR & RMASK;                  /* get address offset from instruction */
            sim_debug(DEBUG_INST, &cpu_dev, "Base OP %x i_flags %x addr %.8x\n", OP, i_flags, addr);
            switch(i_flags & 0xf) {
            case HLF:
                source = GPR[sreg];             /* get the src reg from instruction */
                break;
            case IMM:
                if (PC & 02) {                  /* if pc is on HW boundry, bad address */
                    TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                    goto newpsd;                /* go execute the trap now */
                }
                break;
            case ADR:
                ix = (IR >> 20) & 7;            /* get index reg from instruction */
                if (ix != 0)
                    addr += GPR[ix];            /* if not zero, add in reg contents */
            case WRD:
                if (PC & 02) {                  /* if pc is on HW boundry, bad address */
                    TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                    goto newpsd;                /* go execute the trap now */
                }
                ix = (IR >> 16) & 7;            /* get base reg from instruction */
                if (ix != 0)     
                    addr += BR[ix];             /* if not zero, add to base reg contents */
                FC = ((IR & F_BIT) ? 4 : 0);    /* get F bit from original instruction */
                FC |= addr & 3;                 /* set new C bits to address from orig or regs */
                break;
            case INV:
                TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                goto newpsd;                    /* handle trap */
                break;
             }
        } else {
            i_flags = nobase_mode[OP>>2];       /* set the instruction processing flags */
            addr = IR & 0x7ffff;                /* get 19 bit address from instruction */

            sim_debug(DEBUG_INST, &cpu_dev, "Non Based i_flags %x addr %.8x\n", i_flags, addr);
            /* non base mode instructions have bit 0 of the instruction set */
            /* for word length instructions and zero for halfword instructions */
            /* the LA (op=0x34) is the only exception.  So test for PC on a halfword */
            /* address and trap if word opcode is in right hw */
            if (PC & 02) {          /* if pc is on HW boundry, addr trap if bit zero set */
                if ((OP == 0x34) || (OP & 0x80)) {
                    i_flags |= HLF;             /* diags treats these as hw instructions */
                    TRAPME = UNDEFINSTR_TRAP;   /* Undefined Instruction Trap */
                    goto newpsd;                /* go execute the trap now */
                }
            }
            switch(i_flags & 0xf) {
            case HLF:                           /* halfword instruction */
                source = GPR[sreg];             /* get the src reg contents */
                break;

            case IMM:           /* Immediate mode */
                if (PC & 02) {                  /* if pc is on HW boundry, bad address */
                    TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                    goto newpsd;                /* go execute the trap now */
                }
                break;

            case ADR:           /* Normal addressing mode */
                ix = (IR >> 21) & 3;            /* get the index reg if specified */
                if (ix != 0) {
                    addr += GPR[ix];            /* if not zero, add in reg contents */
                    FC = ((IR & F_BIT) ? 4 : 0);    /* get F bit from original instruction */
                    FC |= addr & 3;             /* set new C bits to address from orig or regs */
                }

                /* wart alert! */
                /* the lea instruction requires special handling for indirection. */
                /* Bits 0,1 are set to 1 in result addr if indirect bit is zero in */
                /* instruction.  Bits 0 & 1 are set to the last word */
                /* or instruction in the chain bits 0 & 1 if indirect bit set */
                  /* if IX == 00 => dest = IR */
                  /* if IX == 0x => dest = IR + reg */
                  /* if IX == Ix => dest = ind + reg */

                /* fall through */
            case WRD:           /* Word addressing, no index */
                bc = 0xC0000000;                /* set bits 0, 1 for instruction if not indirect */
                t = IR;                         /* get current IR */
                while ((t & IND) != 0) {        /* process indirection */
                    if ((TRAPME = Mem_read(addr, &temp)))   /* get the word from memory */
                        goto newpsd;            /* memory read error or map fault */
                    bc = temp & 0xC0000000;     /* save new bits 0, 1 from indirect location */
                    CC = (temp & 0x78000000);   /* save CC's from the last indirect word */
                    /* process new X, I, ADDR fields */
                    addr = temp & MASK19;       /* get just the addr */
                    ix = (temp >> 21) & 3;      /* get the index reg from indirect word */
                    if (ix != 0)
                        addr += (GPR[ix] & MASK19); /* add the register to the address */
                    /* if no F or C bits set, use original, else new */
                    if ((temp & F_BIT) || (addr & 3)) 
                        FC = ((temp & F_BIT) ? 0x4 : 0) | (addr & 3);
                    else {
                        addr |= (IR & F_BIT);   /* copy F bit from instruction */
                        addr |= (FC & 3);       /* copy in last C bits */
                    }
                    t = temp;                   /* go process next indirect location */
                    temp &= MASK19;             /* go process next indirect location */
                    addr &= ~F_BIT;             /* turn off F bit */
                }
                dest = (t_uint64)addr;          /* make into 64 bit variable */
                break;
            case INV:                           /* Invalid instruction */
                if ((TRAPME = Mem_read(addr, &temp)))   /* get the word from memory */
                    goto newpsd;                /* memory read error or map fault */
                TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                goto newpsd;                    /* handle trap */
                break;
            }
        }

        /* Read memory operand */
        if (i_flags & RM) { 
            if ((TRAPME = Mem_read(addr, &temp))) { /* get the word from memory */
                goto newpsd;                    /* memory read error or map fault */
            }
            source = (t_uint64)temp;            /* make into 64 bit value */
            switch(FC) {
            case 0:                             /* word address, extend sign */
                source |= (source & MSIGN) ? D32LMASK : 0;
                break;
            case 1:                             /* left hw */
                source >>= 16;                  /* move left hw to right hw*/
                /* Fall through */
            case 3:                             /* right hw or right shifted left hw */
                source &= 0xffff;               /* use just the right hw */
                if (source & 0x8000) {          /* check sign of 16 bit value */
                    /* sign extend the value to leftmost 48 bits */
                    source = 0xFFFF0000 | (source & 0xFFFF);    /* extend low 32 bits */
                    source |= (D32LMASK);       /* extend hi bits */
                }
                break;
            case 2:                             /* double word address */
                if ((addr & 7) != 2) {          /* must be double word adddress */
                    TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                    goto newpsd;                /* go execute the trap now */
                }
                if ((TRAPME = Mem_read(addr+4, &temp))) {   /* get the 2nd word from memory */
                    goto newpsd;                /* memory read error or map fault */
                }
                source = (source << 32) | (t_uint64)temp;   /* merge in the low order 32 bits */
                dbl = 1;                        /* double word instruction */
                break;
            case 4:                             /* byte mode, byte 0 */
            case 5:                             /* byte mode, byte 1 */
            case 6:                             /* byte mode, byte 2 */
            case 7:                             /* byte mode, byte 3 */
                source = (source >> (8*(7-FC))) & 0xff; /* right justify addressed byte */
                break;
           }
        }

        /* Read memory operand without doing sign extend for EOMX/ANMX/ORMX/ARMX */
        if (i_flags & RNX) { 
            if ((TRAPME = Mem_read(addr, &temp))) { /* get the word from memory */
                goto newpsd;                    /* memory read error or map fault */
            }
            source = (t_uint64)temp;            /* make into 64 bit value */
            switch(FC) {
            case 0:                             /* word address and no sign extend */
                source &= D32RMASK;             /* just l/o 32 bits */
                break;
            case 1:                             /* left hw */
                source >>= 16;                  /* move left hw to right hw*/
                /* Fall through */
            case 3:                             /* right hw or right shifted left hw */
                source &= 0xffff;               /* use just the right hw */
                break;
            case 2:                             /* double word address */
                if ((addr & 7) != 2) {          /* must be double word adddress */
                    TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                    goto newpsd;                /* go execute the trap now */
                }
                if ((TRAPME = Mem_read(addr+4, &temp))) {   /* get the 2nd word from memory */
                    goto newpsd;                /* memory read error or map fault */
                }
                source = (source << 32) | (t_uint64)temp;   /* merge in the low order 32 bits */
                dbl = 1;                        /* double word instruction */
                break;
            case 4:                             /* byte mode, byte 0 */
            case 5:                             /* byte mode, byte 1 */
            case 6:                             /* byte mode, byte 2 */
            case 7:                             /* byte mode, byte 3 */
                source = (source >> (8*(7-FC))) & 0xff; /* right justify addressed byte */
                break;
           }
        }

        /* Read in if from register */
        if (i_flags & RR) {
            if (FC == 2 && (i_flags & HLF) == 0)    /* double dest? */
                dbl = 1;                            /* src must be dbl for dbl dest */
            dest = (t_uint64)GPR[reg];              /* get the register content */
            if (dbl) {                              /* is it double regs */
                if (reg & 1) {                      /* check for odd reg load */
                    TRAPME = ADDRSPEC_TRAP;         /* bad address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                /* merge the regs into the 64bit value */
                dest = (((t_uint64)dest) << 32) | ((t_uint64)GPR[reg+1]);
            } else {
                /* sign extend the data value */
                dest |= (dest & MSIGN) ? D32LMASK : 0;
            }
        }

        /* For Base mode */
        if (i_flags & RB) {
            dest = (t_uint64)BR[reg];               /* get base reg contents */
        }

        /* For register instructions */
        if (i_flags & R1) {
            source = (t_uint64)GPR[sreg];
            if (dbl) {
                if (sreg & 1) {
                    TRAPME = ADDRSPEC_TRAP;         /* bad address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                /* merge the regs into the 64bit value */
                source = (source << 32) | ((t_uint64)GPR[reg+1]);
            } else {
                /* sign extend the data value */
                source |= (source & MSIGN) ? ((t_uint64)MASK32) << 32: 0;
            }
        }

        sim_debug(DEBUG_INST, &cpu_dev, "SW OP %x Non Based i_flags %x addr %.8x\n", OP, i_flags, addr);
        switch (OP>>2) {
/*
 *        For op-codes=00,04,08,0c,10,14,28,2c,38,3c,40,44,60,64,68
 */
        /* Reg - Reg instruction Format (16 bit) */
        /* |--------------------------------------| */
        /* |0 1 2 3 4 5|6 7 8 |9 10 11|12 13 14 15| */
        /* | Op Code   | DReg | SReg  | Aug Code  | */
        /* |--------------------------------------| */
        case 0x00>>2:               /* HLF - HLF */ /* CPU General operations */
                switch(opr & 0xF) {                 /* switch on aug code */
                case 0x0:   /* HALT */
                        if ((modes & PRIVBIT) == 0) {   /* must be privileged to halt */
                            TRAPME = PRIVVIOL_TRAP; /* set the trap to take */
                            goto newpsd;            /* Privlege violation trap */
                        }
                        if (CPUSTATUS & 0x00000100) {   /* Priv mode halt must be enabled */
                            TRAPME = PRIVHALT_TRAP; /* set the trap to take */
                            goto newpsd;            /* Privlege mode halt trap */
                        }
                        /*FIXME*/
                        reason = STOP_HALT;         /* do halt for now */
                        /*
                    fprintf(stdout, "[][][][][][][][][][] HALT [][][][][][][][][][]\r\n");
                    fprintf(stdout, "PSD1 %.8x PSD2 %.8x TRAPME %.4x\r\n", PSD1, PSD2, TRAPME);
                    for (ix=0; ix<8; ix+=2) {
                        fprintf(stdout, "GPR[%d] %.8x GPR[%d] %.8x\r\n", ix, GPR[ix], ix+1, GPR[ix+1]);
                    }
                    fprintf(stdout, "[][][][][][][][][][] HALT [][][][][][][][][][]\r\n");
                        */
                        return STOP_HALT;           /* exit to simh for halt */
                        break;
                case 0x1:   /* WAIT */
                        if ((modes & PRIVBIT) == 0) { /* must be privileged to wait */
                            TRAPME = PRIVVIOL_TRAP; /* set the trap to take */
                            goto newpsd;            /* Privlege violation trap */
                        }
                        if (wait4int == 0) {
                            time_t result = time(NULL);
                            sim_debug(DEBUG_CMD, &cpu_dev, "Starting WAIT mode %x\n", (uint32)result);
                        }
                        wait4int = 1;               /* show we are waiting for interrupt */
                        /* tell simh we will be waiting */
                        sim_idle(TMR_RTC, 0);       /* wait for next pending device event */
                        i_flags |= BT;              /* keep PC from being incremented while waiting */
                        break;
                case 0x2:   /* NOP */
                        break;
                case 0x3:   /* LCS */
                        /* get console switches from memory loc 0x780 */
                        if ((TRAPME = Mem_read(0x780, &GPR[reg]))) /* get the word from memory */
                            goto newpsd;            /* memory read error or map fault */
                        set_CCs(GPR[reg], 0);       /* set the CC's, CC1 = 0 */
                        break;
                case 0x4:   /* ES */
                        if (reg & 1) {              /* see if odd reg specified */
                            TRAPME = ADDRSPEC_TRAP; /* bad reg address, error */
                            goto newpsd;            /* go execute the trap now */
                        }
                        /* reg is reg to extend sign into from reg+1 */
                        GPR[reg] = (GPR[reg+1] & FSIGN) ? FMASK : 0;
                        set_CCs(GPR[reg], 0);       /* set CCs, CC2 & CC3 */
                        break;
                case 0x5:   /* RND */
                        if (reg & 1) {              /* see if odd reg specified */
                            TRAPME = ADDRSPEC_TRAP; /* bad reg address, error */
                            goto newpsd;            /* go execute the trap now */
                        }
                        temp = GPR[reg];            /* save the current contents of specified reg */
                        t = (temp & FSIGN) != 0;    /* set flag for sign bit not set in temp value */
                        bc = 1;
                        t |= ((bc & FSIGN) != 0) ? 2 : 0; /* ditto for the bit value */
                        if (GPR[reg+1] & FSIGN) {   /* if sign of R+1 is set, incr R by 1 */
                            temp += bc;             /* add the bit value to the reg */
                            /* if both signs are neg and result sign is positive, overflow */
                            /* if both signs are pos and result sign is negative, overflow */
                            if ((t == 3 && (temp & FSIGN) == 0) ||
                                (t == 0 && (temp & FSIGN) != 0)) {
                                ovr = 1;            /* we have an overflow */
                            }
                            GPR[reg] = temp;        /* update the R value */
                        } else
                            ovr = 0;
                        set_CCs(temp, ovr);         /* set the CC's, CC1 = ovr */
                        /* the arithmetic exception will be handled */
                        /* after instruction is completed */
                        /* check for arithmetic exception trap enabled */
                        if (ovr && (modes & AEXPBIT)) {
                            TRAPME = AEXPCEPT_TRAP; /* set the trap type */
                            goto newpsd;            /* handle trap */
                        }
                        break;
                case 0x6:   /* BEI */
                        if ((modes & PRIVBIT) == 0) {   /* must be privileged to BEI */
                            TRAPME = PRIVVIOL_TRAP; /* set the trap to take */
                            goto newpsd;            /* Privlege violation trap */
                        }
                        CPUSTATUS |= 0x80;          /* into status word bit 24 too */
                        PSD2 &= ~0x0000c000;        /* clear bit 48 & 49 */
                        PSD2 |= 0x00004000;         /* set bit 49 only */
                        SPAD[0xf5] = PSD2;          /* save the current PSD2 */
                        break;
                case 0x7:   /* UEI */
                        if ((modes & PRIVBIT) == 0) {   /* must be privileged to UEI */
                            TRAPME = PRIVVIOL_TRAP; /* set the trap to take */
                            goto newpsd;            /* Privlege violation trap */
                        }
                        if (CPUSTATUS & 0x80)       /* see if old mode is blocked */
                            irq_pend = 1;           /* start scanning interrupts again */
                        CPUSTATUS &= ~0x80;         /* into status word bit 24 too */
                        PSD2 &= ~0x0000c000;        /* clear bit 48 & 49 to be unblocked */
                        SPAD[0xf5] = PSD2;          /* save the current PSD2 */
/*052619                irq_pend = 1;                  start scanning interrupts again */
                        break;
                case 0x8:   /* EAE */
                        PSD1 |= AEXPBIT;            /* set the enable AEXP flag in PSD */
                        CPUSTATUS |= AEXPBIT;       /* into status word too */
                        modes |= AEXPBIT;           /* ensable arithmetic exception in modes & PSD */
                        break;
                case 0x9:   /* RDSTS */
#ifdef TRME     /* set to 1 for traceme to work */
    traceme++;  /* start trace (maybe) if traceme >= trstart */
//  traceme = trstart;  /* start trace */
    fprintf(stderr, "Got RDSTS traceme = %x trstart = %x\n", traceme, trstart);
#endif
                        GPR[reg] = CPUSTATUS;       /* get CPU status word */
                        break;
                case 0xA:   /* SIPU */              /* ignore for now */
                        break;
                case 0xB:   /* RWCS */              /* RWCS ignore for now */
                        /* reg = specifies reg containing the ACS/WCS address */
                        /* sreg = specifies the ACS/WCS address */
                        /* if the WCS option is not present, address spec error */
                        /* if the mem addr is not a DW, address spec error */
                        /* If 0<-Rs<=fff and Rs bit 0=0, then PROM address */
                        /* If 0<-Rs<=fff and Rs bit 0=1, then ACS address */
#ifdef TRME     /* set to 1 for traceme to work */
//  traceme = trstart;  /* start trace */
    fprintf(stderr, "Got RWCS traceme = %x trstart = %x\n", traceme, trstart);
#endif
                        /* if bit 20 set, WCS enables, else addr spec error */
                        if ((CPUSTATUS & 0x00000800) == 0) {
                            TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                            goto newpsd;                /* go execute the trap now */
                        }
                        /* Maybe TODO copy something */
                        break;
                case 0xC:   /* WWCS */              /* WWCS ignore for now */
                        /* reg = specifies the logical address in memory that */
                        /* is to receive the ACS/WCS contents */
                        /* sreg = specifies the ACS/WCS address */
#ifdef TRME     /* set to 1 for traceme to work */
//  traceme = trstart;  /* start trace */
    fprintf(stderr, "Got WWCS traceme = %x trstart = %x\n", traceme, trstart);
#endif
                        /* bit 20 of cpu stat must be set=1 to to write to ACS or WCS */
                        /* bit 21 of CPU stat must be 0 to write to ACS */
                        /* if bit 20 set, WCS enables, else addr spec error */
                        if ((CPUSTATUS & 0x00000800) == 0) {
                            TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                            goto newpsd;                /* go execute the trap now */
                        }
                        /* Maybe TODO copy something */
                        break;
                case 0xD:   /* SEA */
                        if (modes & BASEBIT)        /* see if based */
                            goto inv;               /* invalid instruction in based mode */
                        modes |= EXTDBIT;           /* set new extended flag (bit 5) in modes & PSD */
                        PSD1 |= EXTDBIT;            /* set the enable AEXP flag in PSD */
                        CPUSTATUS |= EXTDBIT;       /* into status word too */
                        break;
                case 0xE:   /* DAE */
                        modes &= ~AEXPBIT;          /* disable arithmetic exception in modes & PSD */
                        PSD1 &= ~AEXPBIT;           /* disable AEXP flag in PSD */
                        CPUSTATUS &= ~AEXPBIT;      /* into status word too */
                        break;

                case 0xF:   /* CEA */
                        if (modes & BASEBIT)        /* see if based */
                            goto inv;               /* invalid instruction in based mode */
                        modes &= ~EXTDBIT;          /* disable extended mode in modes and PSD */
                        PSD1 &= ~EXTDBIT;           /* disable extended mode (bit 5) flag in PSD */
                        CPUSTATUS &= ~EXTDBIT;      /* into status word too */
                        break;
                }
                break;
        case 0x04>>2:               /* 0x04 RR|R1|SD|HLF - SD|HLF */ /* ANR, SMC, CMC, RPSWT */
                switch(opr & 0xF) {
                case 0x0:   /* ANR */
                    dest &= source;                 /* just an and reg to reg */
                    if (dest & MSIGN)
                        dest |= D32LMASK;           /* force upper word to all ones */
                    i_flags |=  SCC;                /* make sure we set CC's for dest value */
                    break; 

                case 0xA:   /* CMC */        /* Cache Memory Control - Diag use only */
                    /* Cache memory control bit assignments for reg */
                    /* 0-22 reserved, must be zero */
                    /* 23 - Initialize Instruction Cache Bank 0 On = 1 Off = 0 */
                    /* 24 - Initialize Instruction Cache Bank 1 On = 1 Off = 0 */
                    /* 25 - Initialize Operand Cache Bank 0 On = 1 Off = 0 */
                    /* 26 - Initialize Operand Cache Bank 1 On = 1 Off = 0 */
                    /* 27 - Enable Instruction Cache Bank 0 On = 1 Off = 0 */
                    /* 28 - Enable Instruction Cache Bank 1 On = 1 Off = 0 */
                    /* 29 - Enable Operand Cache Bank 0 On = 1 Off = 0 */
                    /* 30 - Enable Operand Cache Bank 1 On = 1 Off = 0 */
                    /* 31 - Bypass Instruction Cache Bank 1 On = 1 Off = 0 */
//                    fprintf(stderr, "CMC GPR[%x] = %x CMCR %x\n", reg, GPR[reg], CMCR);
                    CMCR = GPR[reg];                /* write reg bits 23-31 to cache memory controller */
                    i_flags &= ~SD;                 /* turn off store dest for this instruction */
                    break;

                case 0x7:   /* SMC */               /* Shared Memory Control - Diag use only */
                    /* Shared memory control bit assignments for reg */
                    /*    0 - Reserved */
                    /*    1 - Shared Memory Enabled (=1)/Disabled (=0) */
                    /*  2-6 - Upper Bound of Shared Memory */
                    /*    7 - Read & Lock Enabled (=1)/Disabled (=0) */
                    /* 8-12 - Lower Bound of Shared Memory */
                    /* 3-31 - Reserved and must be zero */
//                    fprintf(stderr, "SMC GPR[%x] = %x SMCR %x\n", reg, GPR[reg], SMCR);
                    SMCR = GPR[reg];                /* write reg bits 0-12 to shared memory controller */
                    i_flags &= ~SD;                 /* turn off store dest for this instruction */
                    break;

/* Computer Configuration Word */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00|01|02 03 04 05 06|07|08 09 10 11 12|13 14 15|16|17|18|19|20 21 22 23 24 25 26|27|28|29|30|31| */
/* |  | S| Upper Bound  |RL| Lower Bound  |Reserved|4k|8k|SM|P2|      Reserved      |I0|I1|D0|D1|BY| */
/* | 0| x| x  x  x  x  x| x| x  x  x  x  x| 0  0  0| x| x| x| x| 0  0  0  0  0  0  0| x| x| x| x| x| */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */
/* Bits:    0   Reserved */
/*          1   Shared Memory Enabled (=1)/Disabled (=0) */
/*        2-6   Upper Bound of Shared Memory */
/*          7   Read & Lock Enabled (=1)/Disabled (=0) */
/*       8-12   Lower Bound of Shared Memory */
/*      13-15   Reserved */
/*         16   4K WCS Option Present (=1)/Not Present (=0) */
/*         17   8K WCS Option Present (=1)/Not Present (=0) */
/*         18   Firmware Control Store Mode ROMSIM (=1)/PROM (=0) */
/*         19   IPU Present (=1)/Not Present (=0) */
/*      20-26   Reserved */
/*         27   Instruction Cache Bank 0 on (=1)/Off (=0) */
/*         28   Instruction Cache Bank 1 on (=1)/Off (=0) */
/*         29   Data Cache Bank 0 on (=1)/Off (=0) */
/*         30   Data Cache Bank 1 on (=1)/Off (=0) */
/*         31   Instruction Cache Enabled (=1)/Disabled (=0) */
/* */
                case 0xB:   /* RPSWT */             /* Read Processor Status Word 2 (PSD2) */
#ifdef TRMEX     /* set to 1 for traceme to work */
//  traceme = trstart;  /* start trace */
  traceme++;  /* start trace (maybe) if traceme >= trstart */
#endif
                    if (GPR[reg] & 0x80000000) {
                        /* if bit 0 of reg set, return (default 0) CPU Configuration Word */
//        fprintf(stderr, "RPSWT READ CCW GPR[%x] = %x CCW %x SPAD %x PSD2 %x\n", reg, GPR[reg], CCW, SPAD[0xf5], PSD2);
                        dest = CCW;                 /* no cache or shared memory */
                        dest = 0x0000c000;          /* set SIM bit for DIAGS */
                    } else {
                        /* if bit 0 of reg not set, return PSD2 */
//        fprintf(stderr, "RPSWT READ PSD2 GPR[%x] = %x CCW %x SPAD %x PSD2 %x\n", reg, GPR[reg], CCW, SPAD[0xf5], PSD2);
                        dest = SPAD[0xf5];          /* get PSD2 for user from SPAD 0xf5 */
                    }
                    break;

                case 0x08:  /* INV */
                    /* HACK HACK HACK for DIAGS */
                    if (CPU_MODEL <= MODEL_27) {    /* DIAG error for 32/27 only */
                        if ((PSD1 & 2) == 0)        /* if lf hw instruction */
                            i_flags |= HLF;         /* if nop in rt hw, bump pc a word */
                    }
                    /* drop through */
                default:    /* INV */               /* everything else is invalid instruction */
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    goto newpsd;                    /* handle trap */
                    break;
                }
                break;

        case 0x08>>2:               /* 0x08 SCC|RR|R1|SD|HLF - */ /* ORR or ORRM */
                dest |= source;                     /* or the regs into dest reg */
                switch(opr & 0x0f) {
                case 0x8:                           /* this is ORRM op */
                     dest &= GPR[4];                /* mask with reg 4 contents */
                     /* drop thru */
                case 0x0:                           /* this is ORR op */
                    if (dest & MSIGN)               /* see if we need to sign extend */
                        dest |= D32LMASK;           /* force upper word to all ones */
                    break;
                default:    /* INV */               /* everything else is invalid instruction */
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    goto newpsd;                    /* handle trap */
                }
                break;

        case 0x0C>>2:               /* 0x0c SCC|RR|R1|SD|HLF - SCC|SD|HLF */ /* EOR or EORM */
                dest ^= source;                     /* exclusive or the regs into dest reg */
                switch(opr & 0x0f) {
                case 0x8:                           /* this is EORM op */
                     dest &= GPR[4];                /* mask with reg 4 contents */
                     /* drop thru */
                case 0x0:                           /* this is EOR op */
                    if (dest & MSIGN)               /* see if we need to sign extend */
                        dest |= D32LMASK;           /* force upper word to all ones */
                    break;
                default:    /* INV */               /* everything else is invalid instruction */
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    goto newpsd;                    /* handle trap */
                }
                break;

        case 0x10>>2:               /* 0x10 HLF - HLF */ /* CAR or (basemode SACZ ) */
                if ((opr & 0xF) == 0) {             /* see if CAR instruction */
                    /* handle non basemode/basemode CAR instr */
                    if ((int32)GPR[reg] < (int32)GPR[sreg])
                        CC = CC3BIT;                /* Rd < Rs; negative */
                    else
                    if (GPR[reg] == GPR[sreg])
                        CC = CC4BIT;                /* Rd == Rs; zero */
                    else
                        CC = CC2BIT;                /* Rd > Rs; positive */
                    PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                    PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                } else {
                    if ((modes & BASEBIT) == 0) {   /* if not basemode, error */
                        TRAPME = UNDEFINSTR_TRAP;   /* Undefined Instruction Trap */
                        goto newpsd;                /* handle trap */
                    }
                    /* handle basemode SACZ instruction */
sacz:               /* non basemode SCZ enters here */
                    temp = GPR[reg];                /* get destination reg contents to shift */
                    CC = 0;                         /* zero the CC's */
                    t = 0;                          /* start with zero shift count */
                    if (temp == 0) {
                        CC = CC4BIT;                /* set CC4 showing dest is zero & cnt is zero too */
                    }
#ifdef NOT_IN_DIAG
                    /* The doc says the reg is not shifted if bit 0 is set on entry. */
                    /* diags says it does, so that is what we will do */
                    /* set count to zero, but shift reg 1 left */
                    else
                    if (temp & BIT0) {
                        CC = 0;                     /* clear CC4 & set count to zero */
                    }
#endif
                    else
                    if (temp != 0) {                /* shift non zero values */
                        while ((temp & FSIGN) == 0) {   /* shift the reg until bit 0 is set */
                            temp <<= 1;             /* shift left 1 bit */
                            t++;                    /* increment shift count */
                        }
                        temp <<= 1;                 /* shift the sign bit out */
                    }
                    GPR[reg] = temp;                /* save the shifted values */
                    GPR[sreg] = t;                  /* set the shift cnt into the src reg */
                    PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                    PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                }
                break;

        case 0x14>>2:               /* 0x14 HLF - HLF */ /* CMR compare masked with reg */
                if (opr & 0xf) {                    /* any subop not zero is error */
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    goto newpsd;                    /* handle trap */
                }
                temp = GPR[reg] ^ GPR[sreg];        /* exclusive or src and destination values */
                temp &= GPR[4];                     /* and with mask reg (GPR 4) */
                CC = 0;                             /* set all CCs zero */
                if (temp == 0)                      /* if result is zero, set CC4 */
                    CC = CC4BIT;                    /* set CC4 to show result 0 */
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                break;

        case 0x18>>2:               /* 0x18 HLF - HLF */ /* SBR, (basemode ZBR, ABR, TBR */
                if (modes & BASEBIT) {              /* handle basemode ZBR, ABR, TBR */
                    if ((opr & 0xC) == 0x0)         /* SBR instruction */
                        goto sbr;                   /* use nonbase SBR code */
                    if ((opr & 0xC) == 0x4)         /* ZBR instruction */
                        goto zbr;                   /* use nonbase ZBR code */
                    if ((opr & 0xC) == 0x8)         /* ABR instruction */
                        goto abr;                   /* use nonbase ABR code */
                    if ((opr & 0xC) == 0xC)         /* TBR instruction */
                        goto tbr;                   /* use nonbase TBR code */
inv:
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    goto newpsd;                    /* handle trap */

                } else {                            /* handle non basemode SBR */
                    if (opr & 0xc) {                /* any subop not zero is error */
                        TRAPME = UNDEFINSTR_TRAP;   /* Undefined Instruction Trap */
                        goto newpsd;                /* handle trap */
                    }
sbr:                                                /* handle basemode too */
                    /* move the byte field bits 14-15 to bits 27-28 */
                    /* or in the bit# from dest reg field bits 6-8 into bit 29-31 */
                    bc = (((opr << 3) & 0x18) | reg);   /* get # bits to shift right */
                    bc = BIT0 >> bc;                /* make a bit mask of bit number */
                    t = (PSD1 & 0x70000000) >> 1;   /* get old CC bits 1-3 into CCs 2-4*/
                    PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                    if (GPR[sreg] & bc)             /* test the bit in src reg */
                        t |= CC1BIT;                /* set CC1 to the bit value */
                    GPR[sreg] |=  bc;               /* set the bit in src reg */
                    PSD1 |= t;                      /* update the CC's in the PSD */
                }
                break;

        case 0x1C>>2:               /* 0x1C HLF - HLF */ /* ZBR (basemode SRA, SRL, SLA, SLL) */
                if (modes & BASEBIT) {          /* handle basemode SRA, SRL, SLA, SLL */
                    bc = opr & 0x1f;            /* get bit shift count */
                    if ((opr & 0x60) == 0x00) { /* SRA instruction */
                        temp = GPR[reg];        /* get reg value to shift */
                        t = temp & FSIGN;       /* sign value */
                        for (ix=0; ix<bc; ix++) {
                            temp >>= 1;         /* shift bit 0 right one bit */
                            temp |= t;          /* restore original sign bit */
                        }
                        GPR[reg] = temp;        /* save the new value */
                        break;
                    }
                    if ((opr & 0x60) == 0x20) { /* SRL instruction */
                        GPR[reg] >>= bc;        /* value to be output */
                        break;
                    }
                    if ((opr & 0x60) == 0x40) { /* SLA instruction */
                        temp = GPR[reg];        /* get reg value to shift */
                        t = temp & FSIGN;       /* sign value */
                        ovr = 0;                /* set ovr off */
                        for (ix=0; ix<bc; ix++) {
                            temp <<= 1;         /* shift bit into sign position */
                            if ((temp & FSIGN) ^ t) /* see if sign bit changed */
                                ovr = 1;        /* set arithmetic exception flag */
                        }
                        temp &= ~BIT0;          /* clear sign bit */
                        temp |= t;              /* restore original sign bit */
                        GPR[reg] = temp;        /* save the new value */
                        PSD1 &= 0x87FFFFFE;     /* clear the old CC's */
                        if (ovr)
                            PSD1 |= BIT1;       /* CC1 in PSD */
                        /* the arithmetic exception will be handled */
                        /* after instruction is completed */
                        /* check for arithmetic exception trap enabled */
                        if (ovr && (modes & AEXPBIT)) {
                            TRAPME = AEXPCEPT_TRAP; /* set the trap type */
                            goto newpsd;        /* go execute the trap now */
                        }
                        break;
                    }
                    if ((opr & 0x60) == 0x60) { /* SLL instruction */
                        GPR[reg] <<= bc;        /* value to be output */
                        break;
                    }
                    break;
                } else {                            /* handle nonbase ZBR */
                    if (opr & 0xc) {                /* any subop not zero is error */
                        TRAPME = UNDEFINSTR_TRAP;   /* Undefined Instruction Trap */
                        goto newpsd;                /* handle trap */
                    }
zbr:                /* handle basemode too */
                    /* move the byte field bits 14-15 to bits 27-28 */
                    /* or in the bit# from dest reg field bits 6-8 into bit 29-31 */
                    bc = (((opr << 3) & 0x18) | reg);   /* get # bits to shift right */
                    bc = BIT0 >> bc;            /* make a bit mask of bit number */
                    t = (PSD1 & 0x70000000) >> 1;   /* get old CC bits 1-3 into CCs 2-4*/
                    PSD1 &= 0x87FFFFFE;         /* clear the old CC's */
                    if (GPR[sreg] & bc)         /* test the bit in src reg */
                        t |= CC1BIT;            /* set CC1 to the bit value */
                    GPR[sreg] &= ~bc;           /* reset the bit in src reg */
                    PSD1 |= t;                  /* update the CC's in the PSD */
                }
                break;

        case 0x20>>2:               /* 0x20 HLF - HLF */    /* ABR (basemode SRAD, SRLD, SLAD, SLLD) */
                if (modes & BASEBIT) {              /* handle basemode SRAD, SRLD, SLAD, SLLD */
                    if (reg & 1) {                  /* see if odd reg specified */
                        TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                        goto newpsd;                /* go execute the trap now */
                    }
                    dest = (t_uint64)GPR[reg+1];    /* get low order reg value */
                    dest |= (((t_uint64)GPR[reg]) << 32);   /* insert upper reg value */
                    bc = opr & 0x1f;                /* get bit shift count */
                    source = dest & DMSIGN;         /* 64 bit sign value */
                    switch (opr & 0x60) {
                    case 0x00:                      /* SRAD instruction */
                        for (ix=0; ix<bc; ix++) {
                            dest >>= 1;             /* shift bit 0 right one bit */
                            dest |= source;         /* restore original sign bit */
                        }
                        break;

                    case 0x20:                      /* SRLD */
                        dest >>= bc;                /* shift right #bits */
                        break;

                    case 0x40:                      /* SLAD instruction */
                        ovr = 0;                    /* set ovr off */
                        for (ix=0; ix<bc; ix++) {
                            dest <<= 1;             /* shift bit into sign position */
                            if ((dest & DMSIGN) ^ source)   /* see if sign bit changed */
                                ovr = 1;            /* set arithmetic exception flag */
                        }
                        dest &= ~DMSIGN;            /* clear sign bit */
                        dest |= source;             /* restore original sign bit */
                        GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                        GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                        PSD1 &= 0x87FFFFFE;         /* clear the old CC's */
                        if (ovr)
                            PSD1 |= BIT1;           /* CC1 in PSD */
                        /* the arithmetic exception will be handled */
                        /* after instruction is completed */
                        /* check for arithmetic exception trap enabled */
                        if (ovr && (modes & AEXPBIT)) {
                            TRAPME = AEXPCEPT_TRAP; /* set the trap type */
                            goto newpsd;            /* go execute the trap now */
                        }
                        break;

                    case 0x60:                      /* SLLD */
                        dest <<= bc;                /* shift left #bits */
                        break;
                    }
                    GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                    GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                    break;

                } else {                            /* handle nonbase mode ABR */
                    if (opr & 0xc) {                /* any subop not zero is error */
                        TRAPME = UNDEFINSTR_TRAP;   /* Undefined Instruction Trap */
                        goto newpsd;                /* handle trap */
                    }
abr:                                                /* basemode ABR too */
                    /* move the byte field bits 14-15 to bits 27-28 */
                    /* or in the bit# from dest reg field bits 6-8 into bit 29-31 */
                    bc = (((opr << 3) & 0x18) | reg);   /* get # bits to shift right */
                    bc = BIT0 >> bc;                /* make a bit mask of bit number */
                    temp = GPR[sreg];               /* get reg value to add bit to */
                    t = (temp & FSIGN) != 0;        /* set flag for sign bit not set in temp value */
                    t |= ((bc & FSIGN) != 0) ? 2 : 0; /* ditto for the bit value */
                    temp += bc;                     /* add the bit value to the reg */
                    /* if both signs are neg and result sign is positive, overflow */
                    /* if both signs are pos and result sign is negative, overflow */
                    if ((t == 3 && (temp & FSIGN) == 0) ||
                        (t == 0 && (temp & FSIGN) != 0)) {
                        ovr = 1;                    /* we have an overflow */
                    }
                    GPR[sreg] = temp;               /* save the new value */
                    set_CCs(temp, ovr);             /* set the CC's, CC1 = ovr */
                    /* the arithmetic exception will be handled */
                    /* after instruction is completed */
                    /* check for arithmetic exception trap enabled */
                    if (ovr && (modes & AEXPBIT)) {
                        TRAPME = AEXPCEPT_TRAP;     /* set the trap type */
                        goto newpsd;                /* handle trap */
                    }
                }
                break;

                case 0x24>>2:               /* 0x24 HLF - HLF */ /* TBR (basemode SRC)  */
                if (modes & BASEBIT) {              /* handle SRC basemode */
                    bc = opr & 0x1f;                /* get bit shift count */
                    temp = GPR[reg];                /* get reg value to shift */
                    if ((opr & 0x60) == 0x40) {     /* SLC instruction */
                        for (ix=0; ix<bc; ix++) {
                            t = temp & BIT0;        /* get sign bit status */
                            temp <<= 1;             /* shift the bit out */
                            if (t)
                                temp |= 1;          /* the sign bit status */
                        }
                    } else {                        /* this is SRC */
                        for (ix=0; ix<bc; ix++) {
                            t = temp & 1;           /* get bit 31 status */
                            temp >>= 1;             /* shift the bit out */
                            if (t)
                                temp |= BIT0;       /* put in new sign bit */
                        }
                    }
                    GPR[reg] = temp;                /* shift result */
                } else {                            /* handle TBR non basemode */
                    if (opr & 0xc) {                /* any subop not zero is error */
                        TRAPME = UNDEFINSTR_TRAP;   /* Undefined Instruction Trap */
                        goto newpsd;                /* handle trap */
                    }
tbr:                                                /* handle basemode TBR too */
                    /* move the byte field bits 14-15 to bits 27-28 */
                    /* or in the bit# from dest reg field bits 6-8 into bit 29-31 */
                    bc = (((opr << 3) & 0x18) | reg);   /* get # bits to shift right */
                    bc = BIT0 >> bc;                /* make a bit mask of bit number */
                    t = (PSD1 & 0x70000000) >> 1;   /* get old CC bits 1-3 into CCs 2-4*/
                    PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                    if (GPR[sreg] & bc)             /* test the bit in src reg */
                        t |= CC1BIT;                /* set CC1 to the bit value */
                    PSD1 |= t;                      /* update the CC's in the PSD */
                }
                break;

        case 0x28>>2:               /* 0x28 HLF - HLF */ /* Misc OP REG instructions */
                switch(opr & 0xF) {

                case 0x0:       /* TRSW */
                    if (modes & BASEBIT)
                        temp = 0x78FFFFFE;          /* bits 1-4 and 24 bit addr for based mode */
                    else
                        temp = 0x7807FFFE;          /* bits 1-4 and 19 bit addr for non based mode */
                    addr = GPR[reg];                /* get reg value */
                    /* we are returning to the addr in reg, set CC's from reg */
                    /* update the PSD with new address from reg */
                    PSD1 &= ~temp;                  /* clean the bits to be changed */
                    PSD1 |= (addr & temp);          /* insert the CC's and address */
                    i_flags |= BT;                  /* we branched, so no PC update */
                    break;

                case 0x2:           /* XCBR */  /* Exchange base registers */
                    if ((modes & BASEBIT) == 0)     /* see if nonbased */
                        goto inv;                   /* invalid instruction in nonbased mode */
                    temp = BR[reg];                 /* get dest reg value */
                    BR[reg] = BR[sreg];             /* put source reg value int dest reg */
                    BR[sreg] = temp;                /* put dest reg value into src reg */
                    break;

                case 0x4:           /* TCCR */  /* Transfer condition codes to GPR bits 28-31 */
                    if ((modes & BASEBIT) == 0)     /* see if nonbased */
                        goto inv;                   /* invalid instruction in nonbased mode */
                    temp = CC >> 27;                /* right justify CC's in reg */
                    GPR[reg] = temp;                /* put dest reg value into src reg */
                    break;

                case 0x5:           /* TRCC */  /* Transfer GPR bits 28-31 to condition codes */
                    if ((modes & BASEBIT) == 0)     /* see if nonbased */
                        goto inv;                   /* invalid instruction in nonbased mode */
                    PSD1 = ((PSD1 & 0x87fffffe) | ((GPR[reg] & 0xf) << 27));    /* insert CCs from reg */
                    break;

                case 0x8:           /* BSUB */  /* Procedure call */
                    if ((modes & BASEBIT) == 0)         /* see if nonbased */
                        goto inv;                       /* invalid instruction in nonbased mode */

                    /* if Rd field is 0 (reg is b6-b8), this is a BSUB instruction */
                    /* otherwise it is a CALL instruction (Rd != 0) */
                    if (reg == 0) {
                        /* BSUB instruction */
                        uint32 cfp = BR[2];             /* get dword bounded frame pointer from BR2 */
                        if ((BR[2] & 0x7) != 0)  {
                            /* Fault, must be dw bounded address */
                            TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                            goto newpsd;                /* go execute the trap now */
                        }
                        cfp = BR[2] & 0x00fffff8;       /* clean the cfp address to 24 bit dw */

                        M[cfp>>2] = (PSD1 + 2) & 0x01fffffe; /* save AEXP bit and PC into frame */
                        M[(cfp>>2)+1] = 0x80000000;     /* show frame created by BSUB instr */
                        BR[1] = BR[sreg] & MASK24;      /* Rs reg to BR 1 */
                        PSD1 = (PSD1 & 0xff000000) | (BR[1] & MASK24); /* New PSD address */
                        BR[3] = GPR[0];                 /* GPR 0 to BR 3 (AP) */
                        BR[0] = cfp;                    /* set frame pointer from BR 2 into BR 0 */
                        i_flags |= BT;                  /* we changed the PC, so no PC update */
                    } else

                    {
                        /* CALL instruction */
                        /* get frame pointer from BR2-16 words & make it a dword addr */
                        uint32 cfp = ((BR[2]-0x40) & 0x00fffff8);

                        /* if cfp and cfp+15w are in different maps, then addr exception error */
                        if ((cfp & 0xffe000) != ((cfp+0x3f) & 0xffe000)) {
                            TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                            goto newpsd;                /* go execute the trap now */
                        }

                        temp = (PSD1+2) & 0x01fffffe;   /* save AEXP bit and PC from PSD1 in to frame */
                        if (TRAPME = Mem_write(cfp, &temp)) { /* Save the PSD into memory */
                            goto newpsd;                /* memory write error or map fault */
                        }

                        temp = 0x00000000;              /* show frame created by CALL instr */
                        if (TRAPME = Mem_write(cfp+4, &temp)) { /* Save zero into memory */
                            goto newpsd;                /* memory write error or map fault */
                        }

                        /* Save BR 0-7 to stack */
                        for (ix=0; ix<8; ix++) {
                            if (TRAPME = Mem_write(cfp+(4*ix)+8, &BR[ix])) { /* Save into memory */
                                goto newpsd;            /* memory write error or map fault */
                            }
                        }

                        /* save GPR 2-8 to stack */
                        for (ix=2; ix<8; ix++) {
                            if (TRAPME = Mem_write(cfp+(4*ix)+32, &GPR[ix])) { /* Save into memory */
                                goto newpsd;            /* memory write error or map fault */
                            }
                        }

                        /* keep bits 0-7 from old PSD */ 
                        PSD1 = (PSD1 & 0xff000000) | ((BR[sreg]) & MASK24); /* New PSD address */
                        BR[1] = BR[sreg];               /* Rs reg to BR 1 */
                        BR[3] = GPR[reg];               /* Rd to BR 3 (AP) */
                        BR[0] = cfp;                    /* set current frame pointer into BR[0] */
                        BR[2] = cfp;                    /* set current frame pointer into BR[2] */
                        i_flags |= BT;                  /* we changed the PC, so no PC update */
                    }
                    break;

                case 0xC:           /* TPCBR */ /* Transfer program Counter to Base Register */
                    if ((modes & BASEBIT) == 0)     /* see if nonbased */
                        goto inv;                   /* invalid instruction in nonbased mode */
                    BR[reg] = PSD1 & 0xfffffe;      /* save PC from PSD1 into BR */
                    break;

                case 0xE:           /* RETURN */    /* procedure return for basemode calls */
                    if ((modes & BASEBIT) == 0)     /* see if nonbased */
                        goto inv;                   /* invalid instruction in nonbased mode */
                    t = BR[0];                      /* get frame pointer from BR[0] */
//                    temp = M[(t>>2)+1];             /* get 2nd word of stack frame */
                    if ((TRAPME = Mem_read(t+4, &temp)))   /* get the word from memory */
                        goto newpsd;            /* memory read error or map fault */
                    /* if Bit0 set, restore all saved regs, else restore only BRs */
                    if ((temp & BIT0) == 0) {       /* see if GPRs are to be restored */
                        /* Bit 0 is not set, so restore all GPRs */
                        for (ix=2; ix<8; ix++)
//                            GPR[ix] = M[(t>>2)+ix+8];   /* restore GPRs 2-7 from call frame */
                            if ((TRAPME = Mem_read(t+ix*4+32, &GPR[ix])))   /* get the word from memory */
                                goto newpsd;        /* memory read error or map fault */
                    }
                    for (ix=0; ix<8; ix++)
//                        BR[ix] = M[(t>>2)+ix+2];    /* restore BRs 0-7 from call frame */
                        if ((TRAPME = Mem_read(t+ix*4+8, &BR[ix])))   /* get the word from memory */
                            goto newpsd;            /* memory read error or map fault */
                    PSD1 &= ~0x1fffffe;             /* leave everything except AEXP bit and PC */
//                    PSD1 |= (M[t>>2] & 0x01fffffe); /* restore AEXP bit and PC from call frame */
                    if ((TRAPME = Mem_read(t, &temp)))   /* get the word from memory */
                        goto newpsd;                /* memory read error or map fault */
                    PSD1 |= (temp & 0x01fffffe);    /* restore AEXP bit and PC from call frame */
                    i_flags |= BT;                  /* we changed the PC, so no PC update */
                    break;

                case 0x1:           /* INV */
                case 0x3:           /* INV */
                case 0x6:           /* INV */
                case 0x7:           /* INV */
                case 0x9:           /* INV */
                case 0xA:           /* INV */
                case 0xB:           /* INV */
                case 0xD:           /* INV */
                case 0xF:           /* INV */
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    goto newpsd;                    /* handle trap */
                    break;
                }
                break;

            case 0x2C>>2:               /* 0x2C HLF - HLF */    /* Reg-Reg instructions */
                temp = GPR[reg];                    /* reg contents specified by Rd */
                addr = GPR[sreg];                   /* reg contents specified by Rs */
                bc = 0;

                switch(opr & 0xF) {
                case 0x0:           /* TRR */       /* SCC|SD|R1 */
                    temp = addr;                    /* set value to go to GPR[reg] */
                    bc = 1;                         /* set CC's at end */
                    break;

                case 0x1:           /* TRBR */      /* Transfer GPR to BR  */
                    if ((modes & BASEBIT) == 0)     /* see if nonbased */
                        goto inv;                   /* invalid instruction in nonbased mode */
                    BR[reg] = GPR[sreg];            /* copy GPR to BR */
                    break;

                case 0x2:           /* TBRR */      /* transfer BR to GPR */
                    if ((modes & BASEBIT) == 0)     /* see if nonbased */
                        goto inv;                   /* invalid instruction in nonbased mode */
                    temp = BR[sreg];                /* set base reg value */
                    bc = 1;                         /* set CC's at end */
                    break;

                case 0x3:           /* TRC */       /* Transfer register complement */
                    temp = addr ^ FMASK;            /* complement Rs */
                    bc = 1;                         /* set CC's at end */
                    break;

                case 0x4:           /* TRN */       /* Transfer register negative */
                    temp = NEGATE32(addr);          /* negate Rs value */
                    if (temp == addr)               /* overflow if nothing changed */
                        ovr = 1;                    /* set overflow flag */
                    /* reset ovr if val == 0, not set for DIAGS */
                    if ((temp == 0) & ovr)
                        ovr = 0;
                    bc = 1;                         /* set the CC's */
                    break;

                case 0x5:           /* XCR */       /* exchange registers Rd & Rs */
                    GPR[sreg] = temp;               /* Rd to Rs */
                    set_CCs(temp, ovr);             /* set the CC's from original Rd */
                    temp = addr;                    /* save the Rs value to Rd reg */
                    break;

                case 0x6:           /* INV */
                    goto inv;
                    break;

                case 0x7:           /* LMAP */      /* Load map reg - Diags only */
                    if ((modes & PRIVBIT) == 0) {   /* must be privileged */
                        TRAPME = PRIVVIOL_TRAP;     /* set the trap to take */
                        goto newpsd;                /* handle trap */
                    }
                    if (modes & MAPMODE) {          /* must be unmapped cpu */
                        TRAPME = MAPFAULT_TRAP;     /* Map Fault Trap */
                        goto newpsd;                /* handle trap */
                    }
                    /* TODO add this instruction code */
                    goto inv;
                    break;

                case 0x8:           /* TRRM */      /* SCC|SD|R1 */
                    temp = addr & GPR[4];           /* transfer reg-reg masked */
                    bc = 1;                         /* set CC's at end */
                    break;

                /* CPUSTATUS bits */
                /* Bits 0-19 reserved */
                /* Bit 20   =0 Write to writable control store is disabled */
                /*          =1 Write to writable control store is enabled */
                /* Bit 21   =0 Enable PROM mode */
                /*          =1 Enable Alterable Control Store Mode */
                /* Bit 22   =0 Enable High Speed Floating Point Accelerator */
                /*          =1 Disable High Speed Floating Point Accelerator */
                /* Bit 23   =0 Disable privileged mode halt trap */
                /*          =1 Enable privileged mode halt trap */
                /* Bit 24 is reserved */
                /* bit 25   =0 Disable software trap handling (enable automatic trap handling) */
                /*          =1 Enable software trap handling */
                /* Bits 26-31 reserved */
                case 0x9:           /* SETCPU */
                    if ((modes & PRIVBIT) == 0) {   /* must be privileged */
                        TRAPME = PRIVVIOL_TRAP;     /* set the trap to take */
                        goto newpsd;                /* handle trap */
                    }
                    CPUSTATUS &= 0xfffff0bf;        /* zero bits that can change */
                    CPUSTATUS |= (temp & 0x0f40);   /* or in the new status bits */
                    break;

                case 0xA:           /* TMAPR */     /* Transfer map to Reg - Diags only */
                    if ((modes & PRIVBIT) == 0) {   /* must be privileged */
                        TRAPME = PRIVVIOL_TRAP;     /* set the trap to take */
                        goto newpsd;                /* handle trap */
                    }
                    if (modes & MAPMODE) {          /* must be unmapped cpu */
                        TRAPME = MAPFAULT_TRAP;     /* Map Fault Trap */
                        goto newpsd;                /* handle trap */
                    }
                    /* TODO add this instruction code */
                    goto inv;                       /* not used */
                    break;

                case 0xB:           /* TRCM */      /* Transfer register complemented masked */
                    temp = (addr ^ FMASK) & GPR[4]; /* compliment & mask */
                    bc = 1;                         /* set the CC's */
                    break;

                case 0xC:           /* TRNM */      /* Transfer register negative masked */
                    temp = NEGATE32(addr);          /* complement GPR[reg] */
                    if (temp == addr)               /* check for overflow */
                        ovr = 1;                    /* overflow */
                    /* reset ovr if val == 0, not set for DIAGS */
                    if ((temp == 0) & ovr)
                        ovr = 0;
                    temp &= GPR[4];                 /* and with negative reg */
                    bc = 1;                         /* set the CC's */
                    break;

                case 0xD:           /* XCRM */      /* Exchange registers masked */
                    addr &= GPR[4];                 /* and Rs with mask reg */
                    temp &= GPR[4];                 /* and Rd with mask reg */
                    GPR[sreg] = temp;               /* Rs to get Rd masked value */
                    set_CCs(temp, ovr);             /* set the CC's from original Rd */
                    temp = addr;                    /* save the Rs value to Rd reg */
                    break;

                case 0xE:           /* TRSC */      /* transfer reg to SPAD */
                    if ((modes & PRIVBIT) == 0) {   /* must be privileged */
                        TRAPME = PRIVVIOL_TRAP;     /* set the trap to take */
                        goto newpsd;                /* handle trap */
                    }
                    t = (GPR[reg] >> 16) & 0xff;    /* get SPAD address from Rd (6-8) */
                    temp2 = SPAD[t];                /* get old SPAD data */
                    SPAD[t] = GPR[sreg];            /* store Rs into SPAD */
                    break;

                case 0xF:           /* TSCR */      /* Transfer scratchpad to register */
                    if ((modes & PRIVBIT) == 0) {   /* must be privileged */
                        TRAPME = PRIVVIOL_TRAP;     /* set the trap to take */
                        goto newpsd;                /* handle trap */
                    }
                    t = (GPR[sreg] >> 16) & 0xff;   /* get SPAD address from Rs (9-11) */
                    temp = SPAD[t];                 /* get SPAD data into Rd (6-8) */
                    break;
                }
                GPR[reg] = temp;                    /* save the temp value to Rd reg */
                if (bc)                             /* set cc's if bc set */
                    set_CCs(temp, ovr);             /* set the CC's */
                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (ovr && (modes & AEXPBIT)) {
                    TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                    goto newpsd;                    /* handle trap */
                }
                break;

/*TODO*/    case 0x30>>2:               /* 0x30 */ /* CALM */
//              fprintf(stderr, "ERROR - CALM called\r\n");
//              fflush(stderr);
                goto inv;           /* TODO */
                break;

            case 0x34>>2:               /* 0x34 SD|ADR - inv */ /* LA non-basemode */
                if (modes & BASEBIT)                /* see if based */
                    goto inv;                       /* invalid instruction in based mode */
                if (modes & EXTDBIT) {              /* see if extended mode */
                    dest = (t_uint64)addr;          /* just pure 24 bit address */
                } else {                            /* use bits 13-31 */
                    dest = (t_uint64)(addr | ((FC & 4) << 17));     /* F bit to bit 12 */
                }
                break;

        case 0x38>>2:                   /* 0x38 HLF - HLF */   /* REG - REG floating point */
                switch(opr & 0xF) {
                    case 0x0:   /* ADR */
                        temp = GPR[reg];                /* reg contents specified by Rd */
                        addr = GPR[sreg];               /* reg contents specified by Rs */
                        t = (temp & FSIGN) != 0;        /* set flag for sign bit not set in temp value */
                        t |= ((addr & FSIGN) != 0) ? 2 : 0; /* ditto for the reg value */
                        temp = temp + addr;             /* add the values */
                        /* if both signs are neg and result sign is positive, overflow */
                        /* if both signs are pos and result sign is negative, overflow */
                        if ((t == 3 && (temp & FSIGN) == 0) ||
                            (t == 0 && (temp & FSIGN) != 0)) {
                            ovr = 1;                    /* we have an overflow */
                        }
                        i_flags |= SF;                  /* special processing */
                        break;

                    case 0x1:   /* ADRFW */
                    case 0x3:   /* SURFW */
                        /* TODO not on 32/27 */
                        temp = GPR[reg];                /* reg contents specified by Rd */
                        addr = GPR[sreg];               /* reg contents specified by Rs */
                        /* temp has Rd (GPR[reg]), addr has Rs (GPR[sreg]) */
                        if ((opr & 0xF) == 0x3)
                            addr = NEGATE32(addr);      /* subtract, so negate source */
                        temp2 = s_adfw(temp, addr, &CC);    /* all add float numbers */
                        PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                        PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                        if (CC & CC1BIT) {              /* check for arithmetic exception */
                            ovr = 1;                    /* exception */
                            /* leave Rd & Rs unchanged if AEXPBIT is set */
                            if (modes & AEXPBIT) {
                                TRAPME = AEXPCEPT_TRAP; /* trap the system now */
                                goto newpsd;            /* process the trap */
                            }
                        }
                        /* AEXPBIT not set, so save the fixed return value */
                        /* return result to destination reg */
                        GPR[reg] = temp2;               /* dest - reg contents specified by Rd */
                        break;

                    case 0x2:   /* MPRBR */
                        /* TODO not on 32/27 */
                        if ((modes & BASEBIT) == 0)     /* see if nonbased */
                            goto inv;                   /* invalid instruction in nonbased mode */
                        if (reg & 1) {
                            /* Spec fault if not even reg */
                            TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                            goto newpsd;                /* go execute the trap now */
                        }
                        temp = GPR[reg+1];              /* get multiplicand */
                        addr = GPR[sreg];               /* multiplier */

                        /* change value into a 64 bit value */
                        dest = ((t_uint64)(addr & FMASK)) | ((addr & FSIGN) ? D32LMASK : 0);
                        source = ((t_uint64)(temp & FMASK)) | ((temp & FSIGN) ? D32LMASK : 0);
                        dest = dest * source;           /* do the multiply */
                        i_flags |= (SD|SCC);            /* save dest reg and set CC's */
                        dbl = 1;                        /* double reg save */
                        break;

                    case 0x4:   /* DVRFW */
                        /* TODO not on 32/27 */
                        temp = GPR[reg];                /* reg contents specified by Rd */
                        addr = GPR[sreg];               /* reg contents specified by Rs */
                        /* temp has Rd (GPR[reg]), addr has Rs (GPR[sreg]) */
                        temp2 = (uint32)s_dvfw(temp, addr, &CC);    /* divide reg by sreg */
                        PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                        PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                        if (CC & CC1BIT) {              /* check for arithmetic exception */
                            ovr = 1;                    /* exception */
                            /* leave Rd & Rs unchanged if AEXPBIT is set */
                            if (modes & AEXPBIT) {
                                TRAPME = AEXPCEPT_TRAP; /* trap the system now */
                                goto newpsd;            /* process the trap */
                            }
                        }
                        /* AEXPBIT not set, so save the fixed return value */
                        /* return result to destination reg */
                        GPR[reg] = temp2;               /* dest - reg contents specified by Rd */
                        break;

                    case 0x5:   /* FIXW */
                        /* TODO not on 32/27 */
                        /* convert from 32 bit float to 32 bit fixed */
                        addr = GPR[sreg];               /* reg contents specified by Rs */
                        temp2 = s_fixw(addr, &CC);      /* do conversion */
                        PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                        PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                        if (CC & CC1BIT) {              /* check for arithmetic exception */
                            ovr = 1;                    /* exception */
                            /* leave Rd & Rs unchanged if AEXPBIT is set */
                            if (modes & AEXPBIT) {
                                TRAPME = AEXPCEPT_TRAP; /* trap the system now */
                                goto newpsd;            /* process the trap */
                            }
                        }
                        /* AEXPBIT not set, so save the fixed return value */
                        /* return result to destination reg */
                        GPR[reg] = temp2;               /* dest - reg contents specified by Rd */
                        break;                          /* go set CC's */

                    case 0x6:   /* MPRFW */
                        /* TODO not on 32/27 */
                        temp = GPR[reg];                /* reg contents specified by Rd */
                        addr = GPR[sreg];               /* reg contents specified by Rs */
                        /* temp has Rd (GPR[reg]), addr has Rs (GPR[sreg]) */
                        temp2 = s_mpfw(temp, addr, &CC);    /* mult reg by sreg */
                        PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                        PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                        if (CC & CC1BIT) {              /* check for arithmetic exception */
                            ovr = 1;                    /* exception */
                            /* leave Rd & Rs unchanged if AEXPBIT is set */
                            if (modes & AEXPBIT) {
                                TRAPME = AEXPCEPT_TRAP; /* trap the system now */
                                goto newpsd;            /* process the trap */
                            }
                        }
                        /* AEXPBIT not set, so save the fixed return value */
                        /* return result to destination reg */
                        GPR[reg] = temp2;               /* dest - reg contents specified by Rd */
                        break;

                    case 0x7:   /* FLTW */
                        /* TODO not on 32/27 */
                        /* convert from 32 bit integer to 32 bit float */
                        addr = GPR[sreg];               /* reg contents specified by Rs */
                        GPR[reg] = s_fltw(addr, &CC);   /* do conversion & set CC's */
                        PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                        PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                        break;

                    case 0x8:   /* ADRM */
                        temp = GPR[reg];                /* reg contents specified by Rd */
                        addr = GPR[sreg];               /* reg contents specified by Rs */
                        t = (temp & FSIGN) != 0;    /* set flag for sign bit not set in temp value */
                        t |= ((addr & FSIGN) != 0) ? 2 : 0; /* ditto for the reg value */
                        temp = temp + addr;             /* add the values */
                        /* if both signs are neg and result sign is positive, overflow */
                        /* if both signs are pos and result sign is negative, overflow */
                        if ((t == 3 && (temp & FSIGN) == 0) ||
                            (t == 0 && (temp & FSIGN) != 0))
                            ovr = 1;                    /* we have an overflow */
                        temp &= GPR[4];                 /* mask the destination reg */
                        i_flags |= SF;                  /* special processing */
                        break;

                    case 0x9:   /* ADRFD */
                    case 0xB:   /* SURFD */
                        /* TODO not on 32/27 */
                        if ((reg & 1) || (sreg & 1)) {  /* see if any odd reg specified */
                            TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                            goto newpsd;                /* go execute the trap now */
                        }
                        td = (((t_uint64)GPR[reg]) << 32);  /* get upper reg value */
                        td |= (t_uint64)GPR[reg+1];         /* insert low order reg value */
                        source = (((t_uint64)GPR[sreg]) << 32); /* get upper reg value */
                        source |= (t_uint64)GPR[sreg+1];        /* insert low order reg value */
                        if ((opr & 0xF) == 0x9)
                            dest = s_adfd(td, source, &CC); /* add */
                        else                        
                            dest = s_sufd(td, source, &CC); /* subtract */
                        PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                        PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                        if (CC & CC1BIT) {              /* check for arithmetic exception */
                            ovr = 1;                    /* exception */
                            /* leave Rd & Rs unchanged if AEXPBIT is set */
                            if (modes & AEXPBIT) {
                                TRAPME = AEXPCEPT_TRAP; /* trap the system now */
                                goto newpsd;            /* process the trap */
                            }
                        }
                        /* AEXPBIT not set, so save the fixed return value */
                        /* return result to destination reg */
                        GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                        GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                        break;

                    case 0xA:   /* DVRBR */
                        /* TODO not on 32/27 */
                        if ((modes & BASEBIT) == 0)     /* see if nonbased */
                            goto inv;                   /* invalid instruction in nonbased mode */
                        if (reg & 1) {
                            /* Spec fault if not even reg */
                            TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                            goto newpsd;                /* go execute the trap now */
                        }
                        /* get Rs divisor value */
                        source = (t_uint64)(GPR[sreg]) | ((GPR[sreg] & FSIGN) ? D32LMASK : 0);
                        /* merge the dividend regs into the 64bit value */
                        dest = (((t_uint64)GPR[reg]) << 32) | ((t_uint64)GPR[reg+1]);
                        if (source == 0) {
                            goto doovr4;
                            break;
                        }
                        td = (t_int64)dest % (t_int64)source;   /* remainder */
                        dbl = (td < 0);                 /* double reg is neg remainder */
                        if (((td & DMSIGN) ^ (dest & DMSIGN)) != 0) /* Fix sign if needed */
                            td = NEGATE32(td);          /* dividend and remainder must be same sign */
                        dest = (t_int64)dest / (t_int64)source; /* now do the divide */
                        /* test for overflow */
                        if ((dest & D32LMASK) != 0 && (dest & D32LMASK) != D32LMASK) {
doovr4:
                            ovr = 1;                    /* the quotient exceeds 31 bit, overflow */
                            /* the arithmetic exception will be handled */
                            /* after instruction is completed */
                            /* check for arithmetic exception trap enabled */
                            if (ovr && (modes & AEXPBIT)) {
                                TRAPME = AEXPCEPT_TRAP; /* set the trap type */
                            }
                            /* the original regs must be returned unchanged if aexp */
                            set_CCs(temp, ovr);         /* set the CC's */
                        } else {
                            GPR[reg] = (uint32)(td & FMASK);    /* reg gets remainder, reg+1 quotient */
                            GPR[reg+1] = (uint32)(dest & FMASK);    /* store quotient in reg+1 */
                            set_CCs(GPR[reg+1], ovr);   /* set the CC's, CC1 = ovr */
                        }
                        break;

                    case 0xC:   /* DVRFD */
                        /* TODO not on 32/27 */
                        if ((reg & 1) || (sreg & 1)) {  /* see if any odd reg specified */
                            TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                            goto newpsd;                /* go execute the trap now */
                        }
                        td = (((t_uint64)GPR[reg]) << 32);  /* get upper reg value */
                        td |= (t_uint64)GPR[reg+1];         /* insert low order reg value */
                        source = (((t_uint64)GPR[sreg]) << 32); /* get upper reg value */
                        source |= (t_uint64)GPR[sreg+1];        /* insert low order reg value */
                        dest = s_dvfd(td, source, &CC); /* divide double values */
                        PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                        PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                        if (CC & CC1BIT) {              /* check for arithmetic exception */
                            ovr = 1;                    /* exception */
                            /* leave Rd & Rs unchanged if AEXPBIT is set */
                            if (modes & AEXPBIT) {
                                TRAPME = AEXPCEPT_TRAP; /* trap the system now */
                                goto newpsd;            /* process the trap */
                            }
                        }
                        /* AEXPBIT not set, so save the fixed return value */
                        /* return result to destination reg */
                        GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                        GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                        break;

                    case 0xD:   /* FIXD */
                        /* dest - reg contents specified by Rd & Rd+1 */
                        /* source - reg contents specified by Rs & Rs+1 */
                        if (sreg & 1) {
                            TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                            goto newpsd;                /* go execute the trap now */
                        }
                        /* merge the sregs into the 64bit value */
                        source = (((t_uint64)GPR[sreg]) << 32) | ((t_uint64)GPR[sreg+1]);
                        /* convert from 64 bit double to 64 bit int */
                        dest = s_fixd(addr, &CC);
                        PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                        PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                        if (CC & CC1BIT) {              /* check for arithmetic exception */
                            ovr = 1;                    /* exception */
                            /* leave Rd & Rs unchanged if AEXPBIT is set */
                            if (modes & AEXPBIT) {
                                TRAPME = AEXPCEPT_TRAP; /* trap the system now */
                                goto newpsd;            /* process the trap */
                            }
                        }
                        /* AEXPBIT not set, so save the fixed return value */
                        /* return result to destination reg */
                        GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                        GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                        break;

                    case 0xE:   /* MPRFD */
                        /* TODO not on 32/27 */
                        if ((reg & 1) || (sreg & 1)) {  /* see if any odd reg specified */
                            TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                            goto newpsd;                /* go execute the trap now */
                        }
                        td = (((t_uint64)GPR[reg]) << 32);  /* get upper reg value */
                        td |= (t_uint64)GPR[reg+1];         /* insert low order reg value */
                        source = (((t_uint64)GPR[sreg]) << 32); /* get upper reg value */
                        source |= (t_uint64)GPR[sreg+1];        /* insert low order reg value */
                        dest = s_mpfd(td, source, &CC); /* multiply double values */
                        PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                        PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                        if (CC & CC1BIT) {              /* check for arithmetic exception */
                            ovr = 1;                    /* exception */
                            /* leave Rd & Rs unchanged if AEXPBIT is set */
                            if (modes & AEXPBIT) {
                                TRAPME = AEXPCEPT_TRAP; /* trap the system now */
                                goto newpsd;            /* process the trap */
                            }
                        }
                        /* AEXPBIT not set, so save the fixed return value */
                        /* return result to destination reg */
                        GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                        GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                        break;

                    case 0xF:   /* FLTD */
                        /* TODO not on 32/27 */
                        /* convert from 64 bit integer to 64 bit float */
                        if ((reg & 1) || (sreg & 1)) {  /* see if any odd reg specified */
                            TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                            goto newpsd;                /* go execute the trap now */
                        }
                        source = (((t_uint64)GPR[sreg]) << 32); /* get upper reg value */
                        source |= (t_uint64)GPR[sreg+1];        /* insert low order reg value */
                        dest = s_fltd(source, &CC);     /* do conversion & set CC's */
                        PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                        PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                        GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                        GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                        break;
                }
                if (i_flags & SF) {                     /* see if special processing */
                    GPR[reg] = temp;                    /* temp has destination reg value */
                    set_CCs(temp, ovr);                 /* set the CC's */
                    /* the arithmetic exception will be handled */
                    /* after instruction is completed */
                    /* check for arithmetic exception trap enabled */
                    if (ovr && (modes & AEXPBIT)) {
                        TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                        goto newpsd;                    /* go execute the trap now */
                    }
                }
                break;

            case 0x3C>>2:               /* 0x3C HLF - HLF */    /* SUR and SURM */
                temp = GPR[reg];                        /* get negative value to add */
                addr = NEGATE32(GPR[sreg]);             /* reg contents specified by Rs */
                switch(opr & 0xF) {
                case 0x0:       /* SUR */
                    t = (temp & FSIGN) != 0;            /* set flag for sign bit not set in temp value */
                    t |= ((addr & FSIGN) != 0) ? 2 : 0; /* ditto for the reg value */
                    temp = temp + addr;                 /* add the values */
                    /* if both signs are neg and result sign is positive, overflow */
                    /* if both signs are pos and result sign is negative, overflow */
                    if ((t == 3 && (temp & FSIGN) == 0) ||
                        (t == 0 && (temp & FSIGN) != 0))
                        ovr = 1;                        /* we have an overflow */
                    break;

                case 0x8:       /* SURM */
                    t = (temp & FSIGN) != 0;            /* set flag for sign bit not set in temp value */
                    t |= ((addr & FSIGN) != 0) ? 2 : 0; /* ditto for the reg value */
                    temp = temp + addr;                 /* add the values */
                    /* if both signs are neg and result sign is positive, overflow */
                    /* if both signs are pos and result sign is negative, overflow */
                    if ((t == 3 && (temp & FSIGN) == 0) ||
                        (t == 0 && (temp & FSIGN) != 0))
                        ovr = 1;                        /* we have an overflow */
                    temp &= GPR[4];                     /* mask the destination reg */
                    break;
                default:
                    TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                    goto newpsd;                        /* handle trap */
                    break;
                }
                GPR[reg] = temp;                        /* save the result */
                set_CCs(temp, ovr);                     /* set CCs for result */
                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (ovr && (modes & AEXPBIT)) {
                    TRAPME = AEXPCEPT_TRAP;             /* set the trap type */
                    goto newpsd;                        /* go execute the trap now */
                }
                break;

        case 0x40>>2:               /* 0x40 SCC|SD|HLF - INV */ /* MPR */
                if (modes & BASEBIT) 
                    goto inv;                           /* invalid instruction in basemode */
                if (reg & 1) {                          /* odd reg specified? */
                    /* Spec fault */
                    /* HACK HACK HACK for DIAGS */
                    if (CPU_MODEL <= MODEL_27) {        /* DIAG error for 32/27 only */
                        if ((PSD1 & 2) == 0)            /* if lf hw instruction */
                            i_flags &= ~HLF;            /* if nop in rt hw, bump pc a word */
                        else
                            PSD1 &= ~3;                 /* fake out 32/27 diag error */
                    }
                    TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                    goto newpsd;                        /* go execute the trap now */
                }
                if (opr & 0xf) {                        /* any subop not zero is error */
                    TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                    goto newpsd;                        /* handle trap */
                }
                temp = GPR[reg+1];                      /* get multiplicand */
                addr = GPR[sreg];                       /* multiplier */

                /* change immediate value into a 64 bit value */
                dest = ((t_uint64)(addr & FMASK)) | ((addr & FSIGN) ? D32LMASK : 0);
                source = ((t_uint64)(temp & FMASK)) | ((temp & FSIGN) ? D32LMASK : 0);
                dest = dest * source;                   /* do the multiply */
                dbl = 1;                                /* double reg save */
                break;

        case 0x44>>2:               /* 0x44 ADR - ADR */ /* DVR */
                /* sreg has Rs */
                if (reg & 1) {
                    /* Spec fault */
                    /* HACK HACK HACK for DIAGS */
                    if (CPU_MODEL <= MODEL_27) {        /* DIAG error for 32/27 only */
                        if ((PSD1 & 2) == 0)            /* if lf hw instruction */
                            i_flags &= ~HLF;            /* if nop in rt hw, bump pc a word */
                        else
                            PSD1 &= ~3;                 /* fake out 32/27 diag error */
                    }
                    TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                    goto newpsd;                        /* go execute the trap now */
                }
                if (opr & 0xf) {                        /* any subop not zero is error */
                    TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                    goto newpsd;                        /* handle trap */
                }
                /* get Rs divisor value */
                source = (t_uint64)(GPR[sreg]) | ((GPR[sreg] & FSIGN) ? D32LMASK : 0);
                /* merge the dividend regs into the 64bit value */
                dest = (((t_uint64)GPR[reg]) << 32) | ((t_uint64)GPR[reg+1]);
                if (source == 0)
                    goto doovr3;
                td = (t_int64)dest % (t_int64)source;   /* remainder */
                if (((td & DMSIGN) ^ (dest & DMSIGN)) != 0) /* Fix sign if needed */
                    td = NEGATE32(td);                  /* dividend and remainder must be same sign */
                dest = (t_int64)dest / (t_int64)source; /* now do the divide */
                int64a = dest;
                if (int64a < 0)
                    int64a = -int64a;
                if (int64a > 0x7fffffff)                /* if more than 31 bits, we have an error */
                    goto doovr3;
                if (((dest & D32LMASK) != 0 && (dest & D32LMASK) != D32LMASK) ||
                    ((dest & D32LMASK) == D32LMASK) && ((dest & D32RMASK) == 0)) {  /* test for overflow */
doovr3:
                    dest = (((t_uint64)GPR[reg]) << 32);/* insert upper reg value */
                    dest |= (t_uint64)GPR[reg+1];       /* get low order reg value */
                    ovr = 1;                            /* the quotient exceeds 31 bit, overflow */
                    /* the arithmetic exception will be handled */
                    /* after instruction is completed */
                    /* check for arithmetic exception trap enabled */
                    if (ovr && (modes & AEXPBIT)) {
                        TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                    }
                    /* the original regs must be returned unchanged if aexp */
                    CC = CC1BIT;                        /* set ovr CC bit */
                    if (dest == 0)
                        CC |= CC4BIT;                   /* dw is zero, so CC4 */
                    else
                    if (dest & DMSIGN)
                        CC |= CC3BIT;                   /* it is neg dw, so CC3  */
                    else
                        CC |= CC2BIT;                   /* then dest > 0, so CC2 */
                    PSD1 &= 0x87FFFFFE;                 /* clear the old CC's from PSD1 */
                    PSD1 |= CC;                         /* update the CC's in the PSD */
                } else {
                    GPR[reg] = (uint32)(td & FMASK);    /* reg gets remainder, reg+1 quotient */
                    GPR[reg+1] = (uint32)(dest & FMASK);    /* store quotient in reg+1 */
                    set_CCs(GPR[reg+1], ovr);           /* set the CC's, CC1 = ovr */
                }
                break;

        case 0x48>>2:               /* 0x48 INV - INV */    /* unused opcodes */
        case 0x4C>>2:               /* 0x4C INV - INV */    /* unused opcodes */
        default:
                TRAPME = UNDEFINSTR_TRAP;               /* Undefined Instruction Trap */
                goto newpsd;                            /* handle trap */
                break;

        case 0x50>>2:               /* 0x50 INV - SD|ADR */ /* LA basemode */
                if ((modes & BASEBIT) == 0)             /* see if nonbased */
                    goto inv;                           /* invalid instruction in nonbased mode */
                dest = (t_uint64)addr;                  /* just pure 24 bit address */
                break;

        case 0x54>>2:               /* 0x54 SM|ADR - INV */ /* (basemode STWBR) */
                if ((modes & BASEBIT) == 0)             /* see if nonbased */
                    goto inv;                           /* invalid instruction in nonbased mode */
                if (FC != 0) {                          /* word address only */
                    TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                    goto newpsd;                        /* go execute the trap now */
                }
                dest = BR[reg];                         /* save the BR to memory */
                break;

        case 0x58>>2:               /* 0x58 SB|ADR - INV */ /* (basemode SUABR and LABR) */
                if ((modes & BASEBIT) == 0)             /* see if nonbased */
                    goto inv;                           /* invalid instruction in nonbased mode */
                if ((FC & 4) == 0) {                    /* see if SUABR F=0 0x5800 */
#ifdef TRME     /* set to 1 for traceme to work */
//    traceme++;  /* start trace (maybe) if traceme >= trstart */
#endif
                     dest = BR[reg] - addr;             /* subtract addr from the BR and store back to BR */
                  } else {                              /* LABR if F=1  0x5808 */
                     dest = addr;                       /* addr goes to specified BR */
                  }
                  break;
        case 0x5C>>2:               /* 0x5C RM|ADR - INV */  /* (basemode LWBR and BSUBM) */
                if ((modes & BASEBIT) == 0)             /* see if nonbased */
                    goto inv;                           /* invalid instruction in nonbased mode */
                if ((FC & 3) != 0) {                    /* word address only */
                    TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                    goto newpsd;                        /* go execute the trap now */
                }
                if ((FC & 0x4) == 0) {                  /* this is a LWBR 0x5C00 instruction */
                    BR[reg] = (uint32)source;           /* load memory location into BR */
#ifdef TRME     /* set to 1 for traceme to work */
//  traceme++;  /* start trace (maybe) if traceme >= trstart */
#endif
                } else

                {                                       /* this is a CALLM/BSUBM instruction */
                    /* if Rd field is 0 (reg is b6-8), this is a BSUBM instruction */
                    /* otherwise it is a CALLM instruction (Rd != 0) */
                    if (reg == 0) {
                        /* BSUBM instruction */
                        uint32 cfp = BR[2];             /* get dword bounded frame pointer from BR2 */

#ifdef TRME     /* set to 1 for traceme to work */
//  traceme = trstart;  /* start trace */
#endif
                        if ((BR[2] & 0x7) != 0)  {
                            /* Fault, must be dw bounded address */
                            TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                            goto newpsd;                /* go execute the trap now */
                        }

                        temp = (PSD1+4) & 0x01fffffe;   /* save AEXP bit and PC from PSD1 into frame */
                        if (TRAPME = Mem_write(cfp, &temp)) { /* Save the PSD into memory */
                            goto newpsd;                /* memory write error or map fault */
                        }

                        temp = 0x80000000;              /* show frame created by BSUBM instr */
                        if (TRAPME = Mem_write(cfp+4, &temp)) { /* Save zero into memory */
                            goto newpsd;                /* memory write error or map fault */
                        }

                        temp = addr & 0xfffffe;         /* CALL memory address */
                        if ((temp & 0x3) != 0) {        /* check for word aligned */
                            /* Fault, must be word bounded address */
                            TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                            goto newpsd;                /* go execute the trap now */
                        }

                        if (TRAPME = Mem_read(temp, &addr))   /* get the word from memory */
                            goto newpsd;                /* memory read error or map fault */

                        BR[1] = addr;                   /* effective address contents to BR 1 */
                        /* keep bits 0-7 from old PSD */ 
                        PSD1 = ((PSD1 & 0xff000000) | (BR[1] & 0x01fffffe)); /* New PSD address */
                        BR[3] = GPR[0];                 /* GPR[0] to BR[3] (AP) */
                        BR[0] = cfp;                    /* set current frame pointer into BR[0] */
                        i_flags |= BT;                  /* we changed the PC, so no PC update */
                    } else {
                        /* CALLM instruction */

                        /* get frame pointer from BR2 - 16 words & make it a dword addr */
                        uint32 cfp = ((BR[2]-0x40) & 0x00fffff8);

                        /* if cfp and cfp+15w are in different maps, then addr exception error */
                        if ((cfp & 0xffe000) != ((cfp+0x3f) & 0xffe000)) {
                            TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                            goto newpsd;                /* go execute the trap now */
                        }

                        temp = (PSD1+4) & 0x01fffffe;   /* save AEXP bit and PC from PSD1 in to frame */
                        if (TRAPME = Mem_write(cfp, &temp)) { /* Save the PSD into memory */
                            goto newpsd;                /* memory write error or map fault */
                        }

                        temp = 0x00000000;              /* show frame created by CALL instr */
                        if (TRAPME = Mem_write(cfp+4, &temp)) { /* Save zero into memory */
                            goto newpsd;                /* memory write error or map fault */
                        }

                        /* save the BRs 0-7 on stack */
                        for (ix=0; ix<8; ix++) {
                            if (TRAPME = Mem_write(cfp+(4*ix)+8, &BR[ix])) { /* Save into memory */
                                goto newpsd;            /* memory write error or map fault */
                            }
                        }

                        /* save GPRs 2-7 on stack */
                        for (ix=2; ix<8; ix++) {
                            if (TRAPME = Mem_write(cfp+(4*ix)+32, &GPR[ix])) { /* Save into memory */
                                goto newpsd;            /* memory write error or map fault */
                            }
                        }

                        temp = addr & 0xfffffe;         /* CALL memory address */
                        if ((temp & 0x3) != 0) {        /* check for word aligned */
                            /* Fault, must be word bounded address */
                            TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                            goto newpsd;                /* go execute the trap now */
                        }

                        if (TRAPME = Mem_read(temp, &addr))   /* get the word from memory */
                            goto newpsd;                /* memory read error or map fault */

                        BR[1] = addr;                   /* effective address contents to BR 1 */
                        /* keep bits 0-6 from old PSD */ 
                        PSD1 = (PSD1 & 0xff000000) | ((BR[1]) & 0x01fffffe); /* New PSD address */
                        BR[3] = GPR[reg];               /* Rd to BR 3 (AP) */
                        BR[0] = cfp;                    /* set current frame pointer into BR[0] */
                        BR[2] = cfp;                    /* set current frame pointer into BR[2] */
                        i_flags |= BT;                  /* we changed the PC, so no PC update */
                    }
                }
                break;

        case 0x60>>2:               /* 0x60 HLF - INV */ /* NOR Rd,Rs */
                if ((modes & BASEBIT)) {                /* only for nonbased mode */
                    TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                    goto newpsd;                        /* handle trap */
                }
                if (opr & 0xf) {                        /* any subop not zero is error */
                    TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                    goto newpsd;                        /* handle trap */
                }
                /* exponent must not be zero or all 1's */
                /* normalize the value Rd in GPR[reg] and put exponent into Rs GPR[sreg] */
                GPR[reg] = s_nor(GPR[reg], &GPR[sreg]);
                break;

        case 0x64>>2:               /* 0x64 SD|HLF - INV */ /* NORD */
                if ((modes & BASEBIT)) {                /* only for nonbased mode */
                    TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                    goto newpsd;                        /* handle trap */
                }
                if (reg & 1) {                          /* see if odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                    goto newpsd;                        /* go execute the trap now */
                }
                if (opr & 0xf) {                        /* any subop not zero is error */
                    TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                    goto newpsd;                        /* handle trap */
                }
                /* shift until upper 5 bits are neither 0 or all 1's */
                /* merge the GPR[reg] & GPR[reg+1] into a 64bit value */
                dest = (((t_uint64)GPR[reg]) << 32) | ((t_uint64)GPR[reg+1]);
                /* normalize the value Rd in GPR[reg] and put exponent into Rs GPR[sreg] */
                dest = s_nord(dest, &GPR[sreg]);
                GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                break;

        case 0x68>>2:           /* 0x68 HLF - INV */ /* non basemode SCZ */
                if (modes & BASEBIT) 
                    goto inv;                           /* invalid instruction */
                if (opr & 0xf) {                        /* any subop not zero is error */
                    TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                    goto newpsd;                        /* handle trap */
                }
                goto sacz;                              /* use basemode sacz instruction */

        case 0x6C>>2:           /* 0x6C HLF - INV */ /* non basemode SRA & SLA */
                if (modes & BASEBIT) 
                    goto inv;                       /* invalid instruction */
                bc = opr & 0x1f;                    /* get bit shift count */
                temp = GPR[reg];                    /* get reg value to shift */
                t = temp & FSIGN;                   /* sign value */
                if (opr & 0x0040) {                 /* is this SLA */
                    ovr = 0;                        /* set ovr off */
                    for (ix=0; ix<bc; ix++) {
                        temp <<= 1;                 /* shift bit into sign position */
                        if ((temp & FSIGN) ^ t)     /* see if sign bit changed */
                            ovr = 1;                /* set arithmetic exception flag */
                    }
                    temp &= ~BIT0;                  /* clear sign bit */
                    temp |= t;                      /* restore original sign bit */
                    GPR[reg] = temp;                /* save the new value */
                    PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                    if (ovr)
                        PSD1 |= BIT1;               /* CC1 in PSD */
                    /* the arithmetic exception will be handled */
                    /* after instruction is completed */
                    /* check for arithmetic exception trap enabled */
                    if (ovr && (modes & AEXPBIT)) {
                        TRAPME = AEXPCEPT_TRAP;     /* set the trap type */
                        goto newpsd;                /* go execute the trap now */
                    }
                } else {                            /* this is a SRA */
                    for (ix=0; ix<bc; ix++) {
                        temp >>= 1;                 /* shift bit 0 right one bit */
                        temp |= t;                  /* restore original sign bit */
                    }
                    GPR[reg] = temp;                /* save the new value */
                }
                break;

       case 0x70>>2:             /* 0x70 SD|HLF - INV */ /* non-basemode SRL & SLL */
                if (modes & BASEBIT) 
                    goto inv;                       /* invalid instruction in basemode */
                bc = opr & 0x1f;                    /* get bit shift count */
                if (opr & 0x0040)                   /* is this SLL, bit 9 set */
                    GPR[reg] <<= bc;                /* shift left #bits */
                else
                    GPR[reg] >>= bc;                /* shift right #bits */
                break;

       case 0x74>>2:             /* 0x74 SD|HLF - INV */ /* non-basemode SRC & SLC */
                if (modes & BASEBIT) 
                    goto inv;                       /* invalid instruction in basemode */
                bc = opr & 0x1f;                    /* get bit shift count */
                temp = GPR[reg];                    /* get reg value to shift */
                if (opr & 0x0040) {                 /* is this SLC, bit 9 set */
                    for (ix=0; ix<bc; ix++) {
                        t = temp & BIT0;            /* get sign bit status */
                        temp <<= 1;                 /* shift the bit out */
                        if (t)
                            temp |= 1;              /* the sign bit status */
                    }
                } else {                            /* this is SRC, bit 9 not set */
                    for (ix=0; ix<bc; ix++) {
                        t = temp & 1;               /* get bit 31 status */
                        temp >>= 1;                 /* shift the bit out */
                        if (t)
                            temp |= BIT0;           /* put in new sign bit */
                    }
                }
                GPR[reg] = temp;                    /* shift result */
                break;

        case 0x78>>2:               /* 0x78 HLF - INV */ /* non-basemode SRAD & SLAD */
                if (modes & BASEBIT)                /* Base mode? */
                    goto inv;                       /* invalid instruction in basemode */
                if (reg & 1) {                      /* see if odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                bc = opr & 0x1f;                    /* get bit shift count */
                dest = (t_uint64)GPR[reg+1];        /* get low order reg value */
                dest |= (((t_uint64)GPR[reg]) << 32);   /* insert upper reg value */
                source = dest & DMSIGN;             /* 64 bit sign value */
                if (opr & 0x0040) {                 /* is this SLAD */
                    ovr = 0;                        /* set ovr off */
                    for (ix=0; ix<bc; ix++) {
                        dest <<= 1;                 /* shift bit into sign position */
                        if ((dest & DMSIGN) ^ source)   /* see if sign bit changed */
                            ovr = 1;                /* set arithmetic exception flag */
                    }
                    dest &= ~DMSIGN;                /* clear sign bit */
                    dest |= source;                 /* restore original sign bit */
                    GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                    GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                    PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                    if (ovr)
                        PSD1 |= BIT1;               /* CC1 in PSD */
                    /* the arithmetic exception will be handled */
                    /* after instruction is completed */
                    /* check for arithmetic exception trap enabled */
                    if (ovr && (modes & AEXPBIT)) {
                        TRAPME = AEXPCEPT_TRAP;     /* set the trap type */
                        goto newpsd;                /* go execute the trap now */
                    }
                } else {                            /* this is a SRAD */
                    for (ix=0; ix<bc; ix++) {
                        dest >>= 1;                 /* shift bit 0 right one bit */
                        dest |= source;             /* restore original sign bit */
                    }
                    GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                    GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                }
                break;

        case 0x7C>>2:               /* 0x7C HLF - INV */ /* non-basemode SRLD & SLLD */
                if (modes & BASEBIT) 
                    goto inv;                       /* invalid instruction in basemode */
                if (reg & 1) {                      /* see if odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                dest = (t_uint64)GPR[reg+1];        /* get low order reg value */
                dest |= (((t_uint64)GPR[reg]) << 32);   /* insert upper reg value */
                bc = opr & 0x1f;                    /* get bit shift count */
                if (opr & 0x0040)                   /* is this SLL, bit 9 set */
                    dest <<= bc;                    /* shift left #bits */
                else
                    dest >>= bc;                    /* shift right #bits */
                GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                break;

        case 0x80>>2:           /* 0x80 SD|ADR - SD|ADR */ /* LEAR */
                /* convert address to real physical address */
                TRAPME = RealAddr(addr, &temp, &t);
                if (TRAPME != ALLOK)
                    goto newpsd;                    /* memory read error or map fault */
                /* OS code says F bit is not transferred, so just ignore it */
                /* DIAGS needs it, so put it back */
                if (FC & 4)                         /* see if F bit was set */
                    temp |= 0x01000000;             /* set bit 7 of address */
                dest = temp;                        /* put in dest to go out */
                break;

        case 0x84>>2:               /* 0x84 SD|RR|RNX|ADR - SD|RNX|ADR */ /* ANMx */
                td = dest & source;                 /* DO ANMX */
                CC = 0;
                switch(FC) {                        /* adjust for hw or bytes */
                case 4: case 5: case 6: case 7:     /* byte address */
                    /* ANMB */
                    td &= 0xff;                     /* mask out right most byte */
                    dest &= 0xffffff00;             /* make place for byte */
                    if (td == 0)
                        CC |= CC4BIT;               /* byte is zero, so CC4 */
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                    break;
                case 1:                             /* left halfword addr */
                case 3:                             /* right halfword addr */
                    /* ANMH */
                    td &= RMASK;                    /* mask out right most 16 bits */
                    dest &= LMASK;                  /* make place for halfword */
                    if (td == 0)
                        CC |= CC4BIT;               /* hw is zero, so CC4 */
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                    break;
                case 0:                             /* 32 bit word */
                    /* ANMW */
                    td &= D32RMASK;                 /* mask out right most 32 bits */
                    dest = 0;                       /* make place for 64 bits */
                    if (td == 0)
                        CC |= CC4BIT;               /* word is zero, so CC4 */
                    else
                    if (td & 0x80000000)
                        CC |= CC3BIT;               /* it is neg wd, so CC3  */
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                    break;
                case 2:                             /* 64 bit double */
                    /* ANMD */
                    dest = 0;                       /* make place for 64 bits */
                    if (td == 0)
                        CC |= CC4BIT;               /* dw is zero, so CC4 */
                    else
                    if (td & DMSIGN)
                        CC |= CC3BIT;               /* it is neg dw, so CC3  */
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                    break;
                }
                dest |= td;                         /* insert result into dest */
                if (FC != 2)                        /* do not sign extend DW */
                    if (dest & 0x80000000)          /* see if we need to sign extend */
                        dest |= D32LMASK;           /* force upper word to all ones */
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's from PSD1 */
                PSD1 |= CC;                         /* update the CC's in the PSD */
                break;

        case 0x88>>2:               /* 0x88 SD|RR|RNX|ADR - SD|RNX|ADR */ /* ORMx */
                td = dest | source;                 /* DO ORMX */
meoa:           /* merge point for eor, and, or */
                CC = 0;
                switch(FC) {                        /* adjust for hw or bytes */
                case 4: case 5: case 6: case 7:     /* byte address */
                    /* ORMB */
                    td &= 0xff;                     /* mask out right most byte */
                    dest &= 0xffffff00;             /* make place for byte */
                    dest |= td;                     /* insert result into dest */
                    if (dest == 0)
                        CC |= CC4BIT;               /* byte is zero, so CC4 */
                    else
                    if (dest & MSIGN) {
                        CC |= CC3BIT;               /* assume negative */
                        dest |= D32LMASK;           /* force upper word to all ones */
                    }
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                    break;
                case 1:                             /* left halfword addr */
                case 3:                             /* right halfword addr */
                    /* ORMH */
                    td &= RMASK;                    /* mask out right most 16 bits */
                    dest &= LMASK;                  /* make place for halfword */
                    dest |= td;                     /* insert result into dest */
                    if (dest == 0)
                        CC |= CC4BIT;               /* byte is zero, so CC4 */
                    else
                    if (dest & MSIGN) {
                        CC |= CC3BIT;               /* assume negative */
                        dest |= D32LMASK;           /* force upper word to all ones */
                    }
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                    break;
                case 0:                             /* 32 bit word */
                    /* ORMW */
                    td &= D32RMASK;                 /* mask out right most 32 bits */
                    dest = 0;                       /* make place for 64 bits */
                    dest |= td;                     /* insert result into dest */
                    if (dest == 0)
                        CC |= CC4BIT;               /* byte is zero, so CC4 */
                    else
                    if (dest & MSIGN) {
                        CC |= CC3BIT;               /* assume negative */
                        dest |= D32LMASK;           /* force upper word to all ones */
                    }
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                    break;
                case 2:                             /* 64 bit double */
                    /* ORMD */
                    dest = 0;                       /* make place for 64 bits */
                    dest |= td;                     /* insert result into dest */
                    if (dest == 0)
                        CC |= CC4BIT;               /* byte is zero, so CC4 */
                    else
                    if (dest & DMSIGN)
                        CC |= CC3BIT;               /* assume negative */
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                    break;
                }
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's from PSD1 */
                PSD1 |= CC;                         /* update the CC's in the PSD */
                break;

        case 0x8C>>2:               /* 0x8C  SD|RR|RNX|ADR - SD|RNX|ADR */ /* EOMx */
                /* must special handle because we are getting bit difference */
                /* for word, halfword, & byte zero the upper 32 bits of dest */
                /* Diags require CC's to be set on result value of byte, hw, wd, or dw */
                td = dest ^ source;                 /* DO EOMX */
                goto meoa;
                break;

        case 0x90>>2:               /* 0x90 SCC|RR|RM|ADR - RM|ADR */ /* CAMx */
                if (dbl == 0) {
                    int32a = dest & D32RMASK;       /* mask out right most 32 bits */
                    int32b = source & D32RMASK;     /* mask out right most 32 bits */
                    int32c = int32a - int32b;       /* signed diff */
                    td = int32c;
                    if (int32a > int32b) dest = 1;
                    else if (int32a == int32b) dest = 0;
                    else dest = -1;
                } else {
                    int64a = dest;                  /* mask out right most 32 bits */
                    int64b = source;                /* mask out right most 32 bits */
                    int64c = int64a - int64b;       /* signed diff */
                    td = int64c;
                    if (int64a > int64b) dest = 1;
                    else if (int64a == int64b) dest = 0;
                    else dest = -1;
                }
                break;

        case 0x94>>2:               /* 0x94 RR|RM|ADR - RM|ADR */ /* CMMx */
                /* CMMD needs both regs to be masked with R4 */
                if (dbl) {
                    /* we need to and both regs with R4 */
                    t_uint64 nm = (((t_uint64)GPR[4]) << 32) | (((t_uint64)GPR[4]) & D32RMASK);
                    td = dest;              /* save dest */
                    dest ^= source;
                    dest &= nm;             /* mask both regs with reg 4 contents */
                } else {
                    td = dest;              /* save dest */
                    dest ^= source;         /* <= 32 bits, so just do lower 32 bits */
                    dest &= (((t_uint64)GPR[4]) & D32RMASK);        /* mask with reg 4 contents */
                }           
                CC = 0;
                if (dest == 0ll)
                    CC |= CC4BIT;
                PSD1 &= 0x87FFFFFE;     /* clear the old CC's from PSD1 */
                PSD1 |= CC;             /* update the CC's in the PSD */
                break;

        case 0x98>>2:               /* 0x98 ADR - ADR */ /* SBM */
                if ((FC & 04) == 0)  {
                    /* Fault, f-bit must be set for SBM instruction */
                    TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                    goto newpsd;                /* go execute the trap now */
                }
                if ((TRAPME = Mem_read(addr, &temp)))   /* get the word from memory */
                    goto newpsd;                /* memory read error or map fault */

                t = (PSD1 & 0x70000000) >> 1;   /* get old CC bits 1-3 into CCs 2-4*/
                /* use C bits and bits 6-8 (reg) to generate shift bit count */
                bc = ((FC & 3) << 3) | reg;     /* get # bits to shift right */
                bc = BIT0 >> bc;                /* make a bit mask of bit number */
                PSD1 &= 0x87FFFFFE;             /* clear the old CC's from PSD1 */
                if (temp & bc)                  /* test the bit in memory */
                    t |= CC1BIT;                /* set CC1 to the bit value */
                PSD1 |= t;                      /* update the CC's in the PSD */
                temp |= bc;                     /* set the bit in temp */
                if ((TRAPME = Mem_write(addr, &temp)))  /* put word back into memory */
                    goto newpsd;                /* memory write error or map fault */
                break;
                  
        case 0x9C>>2:               /* 0x9C ADR - ADR */ /* ZBM */
                if ((FC & 04) == 0)  {
                    /* Fault, byte address not allowed */
                    TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                    goto newpsd;                /* go execute the trap now */
                }
                if ((TRAPME = Mem_read(addr, &temp)))   /* get the word from memory */
                    goto newpsd;                /* memory read error or map fault */

                t = (PSD1 & 0x70000000) >> 1;   /* get old CC bits 1-3 into CCs 2-4*/
                /* use C bits and bits 6-8 (reg) to generate shift bit count */
                bc = ((FC & 3) << 3) | reg;     /* get # bits to shift right */
                bc = BIT0 >> bc;                /* make a bit mask of bit number */
                PSD1 &= 0x87FFFFFE;             /* clear the old CC's from PSD1 */
                if (temp & bc)                  /* test the bit in memory */
                    t |= CC1BIT;                /* set CC1 to the bit value */
                PSD1 |= t;                      /* update the CC's in the PSD */
                temp &= ~bc;                    /* reset the bit in temp */
                if ((TRAPME = Mem_write(addr, &temp)))  /* put word into memory */
                    goto newpsd;                /* memory write error or map fault */
                break;

        case 0xA0>>2:               /* 0xA0 ADR - ADR */ /* ABM */
                if ((FC & 04) == 0)  {
                    /* Fault, byte address not allowed */
                    TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                    goto newpsd;                /* go execute the trap now */
                }
                if ((TRAPME = Mem_read(addr, &temp)))   /* get the word from memory */
                    goto newpsd;                /* memory read error or map fault */

                /* use C bits and bits 6-8 (reg) to generate shift bit count */
                bc = ((FC & 3) << 3) | reg;     /* get # bits to shift right */
                bc = BIT0 >> bc;                /* make a bit mask of bit number */
                t = (temp & FSIGN) != 0;        /* set flag for sign bit not set in temp value */
                t |= ((bc & FSIGN) != 0) ? 2 : 0; /* ditto for the bit value */
                temp += bc;                     /* add the bit value to the reg */
                /* if both signs are neg and result sign is positive, overflow */
                /* if both signs are pos and result sign is negative, overflow */
                if ((t == 3 && (temp & FSIGN) == 0) ||
                    (t == 0 && (temp & FSIGN) != 0)) {
                    ovr = 1;                    /* we have an overflow */
                }
                set_CCs(temp, ovr);             /* set the CC's, CC1 = ovr */
                if ((TRAPME = Mem_write(addr, &temp)))  /* put word into memory */
                    goto newpsd;                /* memory write error or map fault */
                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (ovr && (modes & AEXPBIT)) {
                    TRAPME = AEXPCEPT_TRAP;     /* set the trap type */
                    goto newpsd;                /* handle trap */
                }
                break;

        case 0xA4>>2:               /* 0xA4 ADR - ADR */ /* TBM */
                if ((FC & 04) == 0)  {
                    /* Fault, byte address not allowed */
                    TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                    goto newpsd;                /* go execute the trap now */
                }
                if ((TRAPME = Mem_read(addr, &temp)))   /* get the word from memory */
                    goto newpsd;                /* memory read error or map fault */

                t = (PSD1 & 0x70000000) >> 1;   /* get old CC bits 1-3 into CCs 2-4*/
                /* use C bits and bits 6-8 (reg) to generate shift bit count */
                bc = ((FC & 3) << 3) | reg;     /* get # bits to shift right */
                bc = BIT0 >> bc;                /* make a bit mask of bit number */
                PSD1 &= 0x87FFFFFE;             /* clear the old CC's from PSD1 */
                if (temp & bc)                  /* test the bit in memory */
                    t |= CC1BIT;                /* set CC1 to the bit value */
                PSD1 |= t;                      /* update the CC's in the PSD */
                break;

        case 0xA8>>2:               /* 0xA8 RM|ADR - RM|ADR */ /* EXM */
                if ((FC & 04) != 0 || FC == 2) {    /* can not be byte or doubleword */
                    /* Fault */
                    TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                    goto newpsd;                /* go execute the trap now */
                }
                if ((TRAPME = Mem_read(addr, &temp)))   /* get the word from memory */
                    goto newpsd;                /* memory read error or map fault */

                IR = temp;                      /* get instruction from memory */
                if (FC == 3)                    /* see if right halfword specified */
                    IR <<= 16;                  /* move over the HW instruction */
                if ((IR & 0xFC7F0000) == 0xC8070000 ||
                    (IR & 0xFF800000) == 0xA8000000 ||
                    (IR & 0xFC000000) == 0x80000000) {
                    /* Fault, attempt to execute another EXR, EXRR, EXM, or LEAR  */
                    goto inv;                   /* invalid instruction */
                }
                EXM_EXR = 4;                    /* set PC increment for EXM */
                OPSD1 &= 0x87FFFFFE;            /* clear the old PSD CC's */
                OPSD1 |= PSD1 & 0x78000000;     /* update the CC's in the old PSD */
                /* TODO Update other history information for this instruction */
                if (hst_lnt) {
                    hst[hst_p].opsd1 = OPSD1;   /* update the CC in opsd1 */
                    hst[hst_p].npsd1 = PSD1;    /* save new psd1 */
                    hst[hst_p].npsd2 = PSD2;    /* save new psd2 */
                    hst[hst_p].modes = modes;   /* save current mode bits */
                    for (ix=0; ix<8; ix++) {
                        hst[hst_p].reg[ix] = GPR[ix];   /* save reg */
                        hst[hst_p].reg[ix+8] = BR[ix];  /* save breg */
                    }
                }

#ifdef TRME     /* set to 1 for traceme to work */
        /* no trap, so continue with next instruction */
    if (traceme >= trstart) {
        OPSD1 &= 0x87FFFFFE;                    /* clear the old CC's */
        OPSD1 |= PSD1 & 0x78000000;             /* update the CC's in the PSD */
        if (modes & MAPMODE)
            fprintf(stderr, "M%.8x %.8x ", OPSD1, OIR);
        else
            fprintf(stderr, "U%.8x %.8x ", OPSD1, OIR);
        fprint_inst(stderr, OIR, 0);            /* display instruction */
        fprintf(stderr, "\r\n\tR0=%.8x R1=%.8x R2=%.8x R3=%.8x", GPR[0], GPR[1], GPR[2], GPR[3]);
        fprintf(stderr, " R4=%.8x R5=%.8x R6=%.8x R7=%.8x", GPR[4], GPR[5], GPR[6], GPR[7]);
        fprintf(stderr, "\r\n");
    }
#endif
                goto exec;                      /* go execute the instruction */
                break;
 
        case 0xAC>>2:               /* 0xAC SCC|SD|RM|ADR - SCC|SD|RM|ADR */ /* Lx */
                dest = source;                  /* set value to load into reg */
                break;

        case 0xB0>>2:               /* 0xB0 SCC|SD|RM|ADR - SCC|SD|RM|ADR */ /* LMx */
                /* LMD needs both regs to be masked with R4 */
                if (dbl) {
                    /* we need to and both regs with R4 */
                    t_uint64 nm = (((t_uint64)GPR[4]) << 32) | (((t_uint64)GPR[4]) & D32RMASK);
                    dest = source & nm;         /* mask both regs with reg 4 contents */
                } else {
                    dest = source;              /* <= 32 bits, so just do lower 32 bits */
                    dest &= (((t_uint64)GPR[4]) & D32RMASK);        /* mask with reg 4 contents */
                    if (dest & 0x80000000)      /* see if we need to sign extend */
                        dest |= D32LMASK;       /* force upper word to all ones */
                }           
                break;
 
        case 0xB4>>2:               /* 0xB4 SCC|SD|RM|ADR - SCC|SD|RM|ADR */ /* LNx */
                dest = NEGATE32(source);        /* set the value to load into reg */
                td = dest;
                if (dest != 0 && (dest == source || dest == 0x80000000))
                    ovr = 1;                    /* set arithmetic exception status */
                if (FC != 2)                    /* do not sign extend DW */
                    if (dest & 0x80000000)      /* see if we need to sign extend */
                        dest |= D32LMASK;       /* force upper word to all ones */
                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (dest != 0 && ovr && (modes & AEXPBIT)) {
                    TRAPME = AEXPCEPT_TRAP;     /* set the trap type */
                }
                break;

        case 0xBC>>2:           /* 0xBC SD|RR|RM|ADR - SD|RR|RM|ADR */ /* SUMx */
                source = NEGATE32(source);
                /* Fall through */

        case 0xB8>>2:           /* 0xB8 SD|RR|RM|ADR - SD|RR|RM|ADR */ /* ADMx */
                ovr = 0;
                CC = 0;
                /* DIAG fixs */
                if (dbl == 0) {
                    source &= D32RMASK;             /* just 32 bits */
                    dest &= D32RMASK;               /* just 32 bits */
                    t = (source & MSIGN) != 0;
                    t |= ((dest & MSIGN) != 0) ? 2 : 0;
                    td = dest + source;             /* DO ADMx*/
                    td &= D32RMASK;                 /* mask out right most 32 bits */
                    dest = 0;                       /* make place for 64 bits */
                    dest |= td;                     /* insert 32 bit result into dest */
                    /* if both signs are neg and result sign is positive, overflow */
                    /* if both signs are pos and result sign is negative, overflow */
                    if (((t == 3) && ((dest & MSIGN) == 0)) || 
                        ((t == 0) && ((dest & MSIGN) != 0)))
                        ovr = 1;
                    if ((td == 0) && ((source & MSIGN) == MSIGN) && ovr)
                        ovr = 0;                    /* Diags want 0 and no ovr on MSIGN - MSIGN */
                    if (dest & MSIGN)
                        dest = (D32LMASK | dest);   /* sign extend */
                    else
                        dest = (D32RMASK & dest);   /* zero fill */
                    if (td == 0)
                        CC |= CC4BIT;               /* word is zero, so CC4 */
                    else
                    if (td & 0x80000000)
                        CC |= CC3BIT;               /* it is neg wd, so CC3  */
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                } else {
                    /* ADMD */
                    t = (source & DMSIGN) != 0;
                    t |= ((dest & DMSIGN) != 0) ? 2 : 0;
                    td = dest + source;             /* get sum */
                    dest = td;                      /* insert 64 bit result into dest */
                    /* if both signs are neg and result sign is positive, overflow */
                    /* if both signs are pos and result sign is negative, overflow */
                    if (((t == 3) && ((dest & DMSIGN) == 0)) || 
                        ((t == 0) && ((dest & DMSIGN) != 0)))
                        ovr = 1;
                    if (td == 0)
                        CC |= CC4BIT;               /* word is zero, so CC4 */
                    else
                    if (td & DMSIGN)
                        CC |= CC3BIT;               /* it is neg wd, so CC3  */
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                }
                if (ovr)
                    CC |= CC1BIT;                   /* set overflow CC */
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's from PSD1 */
                PSD1 |= CC;                         /* update the CC's in the PSD */

                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (ovr && (modes & AEXPBIT)) {
                    TRAPME = AEXPCEPT_TRAP;             /* set the trap type */
                }
                break;

        case 0xC0>>2:               /* 0xC0 SCC|SD|RM|ADR - SCC|SD|RM|ADR */ /* MPMx */
                if (reg & 1) {                          /* see if odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                    goto newpsd;                        /* go execute the trap now */
                }
                if (FC == 2) {                          /* must not be double word adddress */
                    TRAPME = ADDRSPEC_TRAP;             /* bad address, error */
                    goto newpsd;                        /* go execute the trap now */
                }
                td = dest;
                dest = GPR[reg+1];                      /* get low order reg value */
                if (dest & MSIGN)
                    dest = (D32LMASK | dest);           /* sign extend */
                dest = (t_uint64)((t_int64)dest * (t_int64)source);
                dbl = 1;
                break;

        case 0xC4>>2:               /* 0xC4 RM|ADR - RM|ADR */ /* DVMx */
                if (reg & 1) {                          /* see if odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                    goto newpsd;                        /* go execute the trap now */
                }
                if (FC == 2) {                          /* must not be double word adddress */
                    TRAPME = ADDRSPEC_TRAP;             /* bad address, error */
                    goto newpsd;                        /* go execute the trap now */
                }
                if (source == 0)
                    goto doovr;                         /* we have div by zero */
                dest = (((t_uint64)GPR[reg]) << 32);    /* insert upper reg value */
                dest |= (t_uint64)GPR[reg+1];           /* get low order reg value */
                td = ((t_int64)dest % (t_int64)source); /* remainder */
                if (((td & DMSIGN) ^ (dest & DMSIGN)) != 0) /* Fix sign if needed */
                    td = NEGATE32(td);                  /* dividend and remainder must be same sign */
                dest = (t_int64)dest / (t_int64)source; /* now do the divide */
                int64a = dest;
                if (int64a < 0)
                    int64a = -int64a;
                if (int64a > 0x7fffffff)                /* if more than 31 bits, we have an error */
                    goto doovr;
                if (((dest & D32LMASK) != 0 && (dest & D32LMASK) != D32LMASK) ||
                    ((dest & D32LMASK) == D32LMASK) && ((dest & D32RMASK) == 0)) {  /* test for overflow */
doovr:
                    dest = (((t_uint64)GPR[reg]) << 32);/* insert upper reg value */
                    dest |= (t_uint64)GPR[reg+1];           /* get low order reg value */
                    ovr = 1;                            /* the quotient exceeds 31 bit, overflow */
                    /* the original regs must be returned unchanged if aexp */
                    CC = CC1BIT;                        /* set ovr CC bit */
                    if (dest == 0)
                        CC |= CC4BIT;                   /* dw is zero, so CC4 */
                    else
                    if (dest & DMSIGN)
                        CC |= CC3BIT;                   /* it is neg dw, so CC3  */
                    else
                        CC |= CC2BIT;                   /* then dest > 0, so CC2 */
                    PSD1 &= 0x87FFFFFE;                 /* clear the old CC's from PSD1 */
                    PSD1 |= CC;                         /* update the CC's in the PSD */
                    /* the arithmetic exception will be handled */
                    /* after instruction is completed */
                    /* check for arithmetic exception trap enabled */
                    if (modes & AEXPBIT)
                        TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                } else {
                    GPR[reg] = (uint32)(td & FMASK);    /* reg gets remainder, reg+1 quotient */
                    GPR[reg+1] = (uint32)(dest & FMASK);    /* store quotient in reg+1 */
                    set_CCs(GPR[reg+1], ovr);           /* set the CC's, CC1 = ovr */
                }
                break;

        case 0xC8>>2:               /* 0xC8 IMM - IMM */ /* Immedate */
                temp = GPR[reg];                        /* get reg contents */
                addr = IR & RMASK;                      /* sign extend 16 bit imm value from IR */
                if (addr & 0x8000)                      /* negative */
                    addr |= LMASK;                      /* extend sign */

                switch(opr & 0xF) {                     /* switch on aug code */
                case 0x0:       /* LI */  /* SCC | SD */
                    GPR[reg] = addr;                    /* put immediate value into reg */
                    set_CCs(addr, ovr);                 /* set the CC's, CC1 = ovr */
                    break;

                case 0x2:       /* SUI */
                    addr = NEGATE32(addr);              /* just make value a negative add */
                    /* drop through */
                case 0x1:       /* ADI */
                    t = (temp & FSIGN) != 0;            /* set flag for sign bit not set in reg value */
                    t |= ((addr & FSIGN) != 0) ? 2 : 0; /* ditto for the extended immediate value */
                    temp = temp + addr;                 /* now add the numbers */
                    /* if both signs are neg and result sign is positive, overflow */
                    /* if both signs are pos and result sign is negative, overflow */
                    if ((t == 3 && (temp & FSIGN) == 0) ||
                        (t == 0 && (temp & FSIGN) != 0))
                        ovr = 1;                        /* we have an overflow */
                    GPR[reg] = temp;                    /* save the result */
                    set_CCs(temp, ovr);                 /* set the CC's, CC1 = ovr */
                    /* the arithmetic exception will be handled */
                    /* after instruction is completed */
                    /* check for arithmetic exception trap enabled */
                    if (ovr && (modes & AEXPBIT)) {
                        TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                        goto newpsd;                    /* go execute the trap now */
                    }
                    break;

                case 0x3:       /* MPI */
                    if (reg & 1) {              /* see if odd reg specified */
                        TRAPME = ADDRSPEC_TRAP; /* bad reg address, error */
                        goto newpsd;            /* go execute the trap now */
                    }
                    /* change immediate value into a 64 bit value */
                    source = ((t_uint64)(addr & FMASK)) | ((addr & FSIGN) ? D32LMASK : 0);
                    temp = GPR[reg+1];          /* get reg multiplier */
                    dest = ((t_uint64)(temp & FMASK)) | ((temp & FSIGN) ? D32LMASK : 0);
                    dest = dest * source;       /* do the multiply */
                    i_flags |= (SD|SCC);        /* save regs and set CC's */
                    dbl = 1;                    /* double reg save */
                    break;

                case 0x4:       /* DVI */
                    if (reg & 1) {                      /* see if odd reg specified */
                        TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                        goto newpsd;                    /* go execute the trap now */
                    }
                    /* change immediate value into a 64 bit value */
                    source = ((t_uint64)(addr & FMASK)) | ((addr & FSIGN) ? D32LMASK : 0);
                    if (source == 0) {
                        goto doovr2;
                    }
                    dest = (((t_uint64)GPR[reg]) << 32);    /* get upper reg value */
                    dest |= (t_uint64)GPR[reg+1];       /* insert low order reg value */
                    td = ((t_int64)dest % (t_int64)source); /* remainder */
//                    dbl = (td < 0);                   /* double reg if neg remainder */
                    if (((td & DMSIGN) ^ (dest & DMSIGN)) != 0) /* Fix sign if needed */
                        td = NEGATE32(td);              /* dividend and remainder must be same sign */
                    dest = (t_int64)dest / (t_int64)source; /* now do the divide */
                    int64a = dest;
                    if (int64a < 0)
                        int64a = -int64a;
                    if (int64a > 0x7fffffff)            /* if more than 31 bits, we have an error */
                        goto doovr2;
                    if ((dest & D32LMASK) != 0 && (dest & D32LMASK) != D32LMASK) {  /* test for overflow */
doovr2:
                        dest = (((t_uint64)GPR[reg]) << 32);    /* get upper reg value */
                        dest |= (t_uint64)GPR[reg+1];   /* insert low order reg value */
                        ovr = 1;                        /* the quotient exceeds 31 bit, overflow */
                        /* the arithmetic exception will be handled */
                        /* after instruction is completed */
                        /* check for arithmetic exception trap enabled */
                        if (modes & AEXPBIT)
                            TRAPME = AEXPCEPT_TRAP;     /* set the trap type */
                        /* the original regs must be returned unchanged if aexp */
                        /* put reg values back in dest for CC test */
                        CC = CC1BIT;                    /* set ovr CC bit */
                        if (dest == 0)
                            CC |= CC4BIT;               /* dw is zero, so CC4 */
                        else
                        if (dest & DMSIGN)
                            CC |= CC3BIT;               /* it is neg dw, so CC3  */
                        else
                            CC |= CC2BIT;               /* then dest > 0, so CC2 */
                        PSD1 &= 0x87FFFFFE;             /* clear the old CC's from PSD1 */
                        PSD1 |= CC;                     /* update the CC's in the PSD */
//                        set_CCs(GPR[reg+1], ovr);       /* set the CC's, CC1 = ovr */
                    } else {
                        GPR[reg] = (uint32)(td & FMASK);    /* reg gets remainder, reg+1 quotient */
                        GPR[reg+1] = (uint32)(dest & FMASK);    /* store quotient in reg+1 */
                        set_CCs(GPR[reg+1], ovr);       /* set the CC's, CC1 = ovr */
                    }
                    break;

                case 0x5:           /* CI */    /* SCC */
                    temp = ((int)temp - (int)addr);     /* subtract imm value from reg value */
                    set_CCs(temp, ovr);                 /* set the CC's, CC1 = ovr */
                    break;

/* SVC instruction format C806 */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03 04 05|06 07 08|09 10 11|12 13 14 15|16 17 18 19|20 21 22 23 24 25 26 27 28 29 30 31| */
/* |     Op Code     |   N/U  |  N/U   |   Aug     | SVC Index |        SVC Call Number            | */
/* | 1  1  0  0  1  0| 0  0  0| 0  0  0| 0  1  1  0| x  x  x  x| x  x  x  x  x  x  x  x  x  x  x  x| */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */
                case 0x6: {           /* SVC  none - none */  /* Supervisor Call Trap */
#ifdef TRMEMPX    /* set to 1 for traceme to work */
                    /* get current MPX task name */
                    int j;
                    char n[9];
                    uint32 dqe = M[0x8e8>>2];       /* get DQE of current task */
                    for (j=0; j<8; j++) {           /* get the task name */
                        n[j] = (M[((dqe+0x18)>>2)+(j/4)] >> ((3-(j&7))*8)) & 0xff;
                        if (n[j] == 0)
                            n[j] = 0x20;
                    }
                    n[8] = 0;
#endif
                    addr = SPAD[0xf0];              /* get trap table memory address from SPAD (def 80) */
                    if (addr == 0 || addr == 0xffffffff) {  /* see if secondary vector table set up */
                        TRAPME = ADDRSPEC_TRAP;     /* Not setup, error */
                        goto newpsd;                /* program error */
                    }
                    addr = addr + (0x06 << 2);      /* addr has mem addr of SVC trap vector (def 98) */
                    temp = M[addr >> 2];            /* get the secondary trap table address from memory */
                    if (temp == 0 || temp == 0xffffffff) {  /* see if ICB set up */
                        TRAPME = ADDRSPEC_TRAP;     /* Not setup, error */
                        goto newpsd;                /* program error */
                    }
                    temp2 = ((IR>>12) & 0x0f) << 2;     /* get SVC index from IR */
                    t = M[(temp+temp2)>>2];         /* get secondary trap vector address ICB address */
                    if (t == 0 || t == 0xffffffff) {    /* see if ICB set up */
                        TRAPME = ADDRSPEC_TRAP;     /* Not setup, error */
                        goto newpsd;                /* program error */
                    }
                    bc = PSD2 & 0x3ffc;             /* get copy of cpix */
                    M[t>>2] = (PSD1+4) & 0xfffffffe;    /* store PSD 1 + 1W to point to next instruction */
                    M[(t>>2)+1] = PSD2;             /* store PSD 2 */
                    PSD1 = M[(t>>2)+2];             /* get new PSD 1 */
                    PSD2 = (M[(t>>2)+3] & ~0x3ffc) | bc;    /* get new PSD 2 w/old cpix */
                    M[(t>>2)+4] = IR&0xFFF;         /* store call number */
#ifdef TRMEMPX    /* set to 1 for traceme to work */
    fprintf(stderr, "SVC @ %.8x SVC %x,%x PSD1 %.8x PSD2 %.8x SPAD PSD@ %x C.CURR %x LMN %s\r\n",
        OPSD1, temp2>>2, IR&0xFFF, PSD1, PSD2, SPAD[0xf5], dqe, n);
    fprintf(stderr, "\r\n\tR0=%.8x R1=%.8x R2=%.8x R3=%.8x", GPR[0], GPR[1], GPR[2], GPR[3]);
    fprintf(stderr, " R4=%.8x R5=%.8x R6=%.8x R7=%.8x", GPR[4], GPR[5], GPR[6], GPR[7]);
    fprintf(stderr, "\r\n");
    if (((temp2>>2) == 1) && ((IR&0xfff) == 0x75))
        fprintf(stderr, "SVC %x,%x GPR[6] %x GPR[6] %x\r\n", temp2>>2, IR&0xfff, GPR[6], GPR[7]);
#endif
                    /* set the mode bits and CCs from the new PSD */
                    CC = PSD1 & 0x78000000;         /* extract bits 1-4 from PSD1 */
                    modes = PSD1 & 0x87000000;      /* extract bits 0, 5, 6, 7 from PSD 1 */
                    /* set new map mode and interrupt blocking state in CPUSTATUS */
                    if (PSD2 & MAPBIT) {
                        CPUSTATUS |= 0x00800000;    /* set bit 8 of cpu status */
                        modes |= MAPMODE;           /* set mapped mode */
                    } else
                        CPUSTATUS &= 0xff7fffff;    /* reset bit 8 of cpu status */
                    /* set interrupt blocking state */
                    if ((PSD2 & 0x8000) == 0) {     /* is it retain blocking state */
                        if (PSD2 & 0x4000)          /* no, is it set blocking state */
                            CPUSTATUS |= 0x80;      /* yes, set blk state in cpu status bit 24 */
                        else {
                            CPUSTATUS &= ~0x80;     /* no, reset blk state in cpu status bit 24 */
                            irq_pend = 1;           /* start scanning interrupts again */
                        }
                    }
                    PSD2 &= ~0x0000c000;            /* clear bit 48 & 49 to be unblocked */
                    if (CPUSTATUS & 0x80)           /* see if old mode is blocked */
                        PSD2 |= 0x00004000;         /* set to blocked state */

                    PSD2 &= ~RETMBIT;               /* turn off retain bit in PSD2 */
                    SPAD[0xf5] = PSD2;              /* save the current PSD2 */
                    goto newpsd;                    /* new psd loaded */
                }
                    break;

                case 0x7:           /* EXR */
                    IR = temp;                      /* get instruction to execute */
                    /* if bit 30 set, instruction is in right hw, do EXRR */
                    if (addr & 2)
                        IR <<= 16;                  /* move instruction to left HW */
                    if ((IR & 0xFC7F0000) == 0xC8070000 ||
                        (IR & 0xFF800000) == 0xA8000000) {
                        /* Fault, attempt to execute another EXR, EXRR, or EXM  */
                        goto inv;                   /* invalid instruction */
                    }
                    EXM_EXR = 4;                    /* set PC increment for EXR */
                    OPSD1 &= 0x87FFFFFE;            /* clear the old CC's */
                    OPSD1 |= PSD1 & 0x78000000;     /* update the CC's in the PSD */
                    /* TODO Update other history information for this instruction */
                    if (hst_lnt) {
                        hst[hst_p].opsd1 = OPSD1;   /* update the CC in opsd1 */
                        hst[hst_p].npsd1 = PSD1;    /* save new psd1 */
                        hst[hst_p].npsd2 = PSD2;    /* save new psd2 */
                        hst[hst_p].modes = modes;   /* save current mode bits */
                        for (ix=0; ix<8; ix++) {
                            hst[hst_p].reg[ix] = GPR[ix];   /* save reg */
                            hst[hst_p].reg[ix+8] = BR[ix];  /* save breg */
                        }
                    }

#ifdef TRME     /* set to 1 for traceme to work */
    if (traceme >= trstart) {
        OPSD1 &= 0x87FFFFFE;            /* clear the old CC's */
        OPSD1 |= PSD1 & 0x78000000;     /* update the CC's in the PSD */
        if (modes & MAPMODE)
            fprintf(stderr, "M%.8x %.8x ", OPSD1, OIR);
        else
            fprintf(stderr, "U%.8x %.8x ", OPSD1, OIR);
        fprint_inst(stderr, OIR, 0);    /* display instruction */
        fprintf(stderr, "\r\n\tR0=%.8x R1=%.8x R2=%.8x R3=%.8x", GPR[0], GPR[1], GPR[2], GPR[3]);
        fprintf(stderr, " R4=%.8x R5=%.8x R6=%.8x R7=%.8x", GPR[4], GPR[5], GPR[6], GPR[7]);
        fprintf(stderr, "\r\n");
    }
#endif
                    goto exec;          /* go execute the instruction */
                    break;

                /* these instruction were never used by MPX, only diags */
                /* diags treat them as invalid halfword instructions */
                /* so set the HLF flag to get proper PC increment */
                case 0x8:                           /* SEM */
                case 0x9:                           /* LEM */
                case 0xA:                           /* CEMA */
                case 0xB:                           /* INV */
                case 0xC:                           /* INV */
                case 0xD:                           /* INV */
                case 0xE:                           /* INV */
                case 0xF:                           /* INV */
                default:
//                    i_flags |= HLF;                 /* handle as if hw */
                    goto inv;                       /* invalid instruction */
                    break;
                }
                break;

        case 0xCC>>2:               /* 0xCC ADR - ADR */ /* LF */
                /* For machines with Base mode 0xCC08 stores base registers */
                if ((FC & 3) != 0) {                /* must be word address */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                bc = addr & 0x20;                   /* bit 26 initial value */
                while (reg < 8) {
                    if (bc != (addr & 0x20)) {      /* test for crossing file boundry */
                        if (CPU_MODEL < MODEL_27) {
                            TRAPME = ADDRSPEC_TRAP; /* bad reg address, error */
                            goto newpsd;            /* go execute the trap now */
                        }
                    }
                    if (FC & 0x4)                   /* LFBR? 0xCC08 */
                        TRAPME = Mem_read(addr, &BR[reg]);  /* read the base reg */
                    else                            /* LF? 0xCC00 */
                        TRAPME = Mem_read(addr, &GPR[reg]); /* read the GPR reg */
                    if (TRAPME)                     /* TRAPME has error */
                        goto newpsd;                /* go execute the trap now */
                    reg++;                          /* next reg to write */
                    addr += 4;                      /* next addr */
                }
                break;

       case 0xD0>>2:             /* 0xD0 SD|ADR - INV */ /* LEA  none basemode only */
                if (modes & BASEBIT) 
                    goto inv;                       /* invalid instruction in basemode */
                /* bc has last bits 0,1 for indirect addr of both 1 for no indirection */
                addr &= 0x3fffffff;                 /* clear bits 0-1 */
                addr |= bc;                         /* insert bits 0,1 values into address */
                if (FC & 0x4)
                    addr |= F_BIT;                  /* copy F bit from instruction */
                dest = (t_uint64)(addr);
                break;

        case 0xD4>>2:               /* 0xD4 RR|SM|ADR - RR|SM|ADR */ /* STx */
                break;

        case 0xD8>>2:               /* 0xD8 RR|SM|ADR - RR|SM|ADR */ /* STMx */
                /* STMD needs both regs to be masked with R4 */
                if (dbl) {
                    /* we need to and both regs */
                    t_uint64 nm = (((t_uint64)GPR[4]) << 32) | (((t_uint64)GPR[4]) & D32RMASK);
                    dest &= nm;                         /* mask both regs with reg 4 contents */
                } else {
                    dest &= (((t_uint64)GPR[4]) & D32RMASK);    /* mask with reg 4 contents */
                }           
                break;

        case 0xDC>>2:               /* 0xDC INV - ADR */    /* INV nonbasemode (STFx basemode) */
                /* DC00 STF */ /* DC08 STFBR */
                if ((FC & 0x4) && (CPU_MODEL <= MODEL_27))  {
                    /* basemode undefined for 32/7x & 32/27 */ /* TODO check this */
                    TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                    goto newpsd;                        /* handle trap */
                }
                /* For machines with Base mode 0xDC08 stores base registers */
                if ((FC & 3) != 0) {                    /* must be word address */
                    TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                    goto newpsd;                        /* go execute the trap now */
                }
                bc = addr & 0x20;                       /* bit 26 initial value */
                while (reg < 8) {
                    if (bc != (addr & 0x20)) {          /* test for crossing file boundry */
                        if (CPU_MODEL < MODEL_27) {
                            TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                            goto newpsd;                /* go execute the trap now */
                        }
                    }
                    if (FC & 0x4)                       /* STFBR? */
                        TRAPME = Mem_write(addr, &BR[reg]);     /* store the base reg */
                    else                                /* STF */
                        TRAPME = Mem_write(addr, &GPR[reg]);    /* store the GPR reg */
                    if (TRAPME)                         /* TRAPME has error */
                        goto newpsd;                    /* go execute the trap now */
                    reg++;                              /* next reg to write */
                    addr += 4;                          /* next addr */
                }
                break;

        case 0xE0>>2:               /* 0xE0 ADR - ADR */ /* ADFx, SUFx */
                if ((TRAPME = Mem_read(addr, &temp))) { /* get the word from memory */
                    goto newpsd;                        /* memory read error or map fault */
                }
                source = (t_uint64)temp;                /* make into 64 bit value */
                if (FC & 2) {                           /* see if double word addr */
                    if ((TRAPME = Mem_read(addr+4, &temp))) {   /* get the 2nd word from memory */
                        goto newpsd;                    /* memory read error or map fault */
                    }
                    source = (source << 32) | (t_uint64)temp;   /* merge in the low order 32 bits */
                    dbl = 1;                            /* double word instruction */
                } else {
                    source |= (source & MSIGN) ? D32LMASK : 0;
                    dbl = 0;                            /* not double wd */
                }
                PSD1 &= 0x87FFFFFE;                     /* clear the old CC's */
                CC = 0;                                 /* clear the CC'ss */
                /* handle float or double add/sub instructions */
                if (dbl == 0) {
                    /* do ADFW or SUFW instructions */
                    temp2 = GPR[reg];                   /* dest - reg contents specified by Rd */
                    addr = (uint32)(source & D32RMASK); /* get 32 bits from source memory */
                    if (opr & 0x0008) {                 /* Was it ADFW? */
                        temp = s_adfw(temp2, addr, &CC);    /* do ADFW */
                    } else {
                        /* s_sufw will negate the value before calling add */
                        temp = s_sufw(temp2, addr, &CC);    /* do SUFW */
                    }
                    ovr = 0;
                    if (CC & CC1BIT)
                        ovr = 1;
                    PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                    /* check if we had an arithmetic exception on the last instruction*/
                    if (ovr && (modes & AEXPBIT)) {
                        /* leave regs unchanged */
                        TRAPME = AEXPCEPT_TRAP;         /* trap the system now */
                        goto newpsd;                    /* process the trap */
                    }
                    /* AEXP not enabled, so apply fix here */
                    /* return temp to destination reg */
                    GPR[reg] = temp;                    /* dest - reg contents specified by Rd */
                } else {
                    /* handle ADFD or SUFD */
                    if (reg & 1) {                      /* see if odd reg specified */
                        TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                        goto newpsd;                    /* go execute the trap now */
                    }
                    /* do ADFD or SUFD instructions */
                    td = (((t_uint64)GPR[reg]) << 32);  /* get upper reg value */
                    td |= (t_uint64)GPR[reg+1];         /* insert low order reg value */
                    /* source has 64 bit memory data */
                    if (opr & 0x0008) {                 /* Was it ADFD? */
                        dest = s_adfd(td, source, &CC); /* do ADFW */
                    } else {
                        /* s_sufd will negate the memory value before calling add */
                        dest = s_sufd(td, source, &CC); /* do SUFD */
                    }
                    ovr = 0;
                    if (CC & CC1BIT)                    /* test for overflow detection */
                        ovr = 1;
                    PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                    /* check if we had an arithmetic exception on the last instruction */
                    if (ovr && (modes & AEXPBIT)) {
                        /* leave regs unchanged */
                        TRAPME = AEXPCEPT_TRAP;         /* trap the system now */
                        goto newpsd;                    /* process the trap */
                    }
                    /* dest will be returned to destination regs */
                    /* if AEXP not enabled, apply fix here */
                    /* return dest to destination reg */
                    GPR[reg] = (uint32)((dest & D32LMASK) >> 32);   /* get upper reg value */
                    GPR[reg+1] = (uint32)(dest & D32RMASK);     /* get lower reg value */
                }
                break;

        case 0xE4>>2:               /* 0xE4 ADR - ADR */ /* MPFx, DVFx */
                if ((TRAPME = Mem_read(addr, &temp))) { /* get the word from memory */
                    goto newpsd;                        /* memory read error or map fault */
                }
                source = (t_uint64)temp;                /* make into 64 bit value */
                if (FC & 2) {                           /* see if double word addr */
                    if ((TRAPME = Mem_read(addr+4, &temp))) {   /* get the 2nd word from memory */
                        goto newpsd;                    /* memory read error or map fault */
                    }
                    source = (source << 32) | (t_uint64)temp;   /* merge in the low order 32 bits */
                    dbl = 1;                            /* double word instruction */
                } else {
                    source |= (source & MSIGN) ? D32LMASK : 0;
                    dbl = 0;                            /* not double wd */
                }
                PSD1 &= 0x87FFFFFE;                     /* clear the old CC's */
                CC = 0;                                 /* clear the CC'ss */
                /* handle float or double mul/div instructions */
                if (dbl == 0) {
                    /* do MPFW or DIVW instructions */
                    temp2 = GPR[reg];                   /* dest - reg contents specified by Rd */
                    addr = (uint32)(source & D32RMASK); /* get 32 bits from source memory */
                    if ((opr & 0xf) == 0x8) {           /* Was it MPFW? */
                        temp = s_mpfw(temp2, addr, &CC);    /* do MPFW */
                    } else {
                        temp = (uint32)s_dvfw(temp2, addr, &CC);    /* do DVFW */
                    }
                    if (CC & CC1BIT)
                        ovr = 1;
                    PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                    /* check if we had an arithmetic exception on the last instruction*/
                    if (ovr && (modes & AEXPBIT)) {
                        /* leave regs unchanged */
                        TRAPME = AEXPCEPT_TRAP;         /* trap the system now */
                        goto newpsd;                    /* process the trap */
                    }
                    /* if AEXP not enabled, apply fix here */
                    /* return temp to destination reg */
                    GPR[reg] = temp;                    /* dest - reg contents specified by Rd */
                } else {
                    /* handle MPFD or DVFD */
                    if (reg & 1) {                      /* see if odd reg specified */
                        TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                        goto newpsd;                    /* go execute the trap now */
                    }
                    /* do MPFD or DVFD instructions */
                    td = (((t_uint64)GPR[reg]) << 32);  /* get upper reg value */
                    td |= (t_uint64)GPR[reg+1];         /* insert low order reg value */
                    /* source has 64 bit memory data */
                    if ((opr & 0xf) == 0x8) {           /* Was it MPFD? */
                        dest = s_mpfd(td, source, &CC); /* do MPFD */
                    } else {
                        dest = s_sufd(td, source, &CC); /* do DVFD */
                    }
                    if (CC & CC1BIT)                    /* test for overflow detection */
                        ovr = 1;
                    PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                    /* check if we had an arithmetic exception on the last instruction*/
                    if (ovr && (modes & AEXPBIT)) {
                        /* leave regs unchanged */
                        TRAPME = AEXPCEPT_TRAP;         /* trap the system now */
                        goto newpsd;                    /* process the trap */
                    }
                    /* dest will be returned to destination regs */
                    /* if AEXP not enabled, apply fix here */
                    /* return dest to destination reg */
                    GPR[reg] = (uint32)((dest & D32LMASK) >> 32);   /* get upper reg value */
                    GPR[reg+1] = (uint32)(dest & D32RMASK);         /* get lower reg value */
                }
                break;

        case 0xE8>>2:               /* 0xE8 SM|RR|RNX|ADR - SM|RM|ADR */ /* ARMx */
                ovr = 0;
                CC = 0;
                switch(FC) {                        /* adjust for hw or bytes */
                case 4: case 5: case 6: case 7:     /* byte address */
                    /* ARMB */
                    td = dest + source;             /* DO ARMB */
                    td &= 0xff;                     /* mask out right most byte */
                    dest &= 0xffffff00;             /* make place for byte */
                    dest |= td;                     /* insert result into dest */
                    if (td == 0)
                        CC |= CC4BIT;               /* byte is zero, so CC4 */
                    break;
                case 1:                             /* left halfword addr */
                case 3:                             /* right halfword addr */
                    /* ARMH */
                    td = dest + source;             /* DO ARMH */
                    td &= RMASK;                    /* mask out right most 16 bits */
                    dest &= LMASK;                  /* make place for halfword */
                    dest |= td;                     /* insert result into dest */
                    if (td == 0)
                        CC |= CC4BIT;               /* hw is zero, so CC4 */
                    break;
                case 0:                             /* 32 bit word */
                    /* ARMW */
                    /* dest and source are really 32 bit values */
                    t = (source & MSIGN) != 0;
                    t |= ((dest & MSIGN) != 0) ? 2 : 0;
                    td = dest + source;             /* DO ARMW */
                    td &= D32RMASK;                 /* mask out right most 32 bits */
                    dest = 0;                       /* make place for 64 bits */
                    dest |= td;                     /* insert result into dest */
                    /* if both signs are neg and result sign is positive, overflow */
                    /* if both signs are pos and result sign is negative, overflow */
                    if (((t == 3) && ((dest & MSIGN) == 0)) || 
                        ((t == 0) && ((dest & MSIGN) != 0)))
                        ovr = 1;
                    if (dest & MSIGN)
                        dest = (D32LMASK | dest);   /* sign extend */
                    else
                        dest = (D32RMASK & dest);   /* zero fill */
                    if (td == 0)
                        CC |= CC4BIT;               /* word is zero, so CC4 */
                    else
                    if (td & 0x80000000)
                        CC |= CC3BIT;               /* it is neg wd, so CC3  */
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                    break;
                case 2:                             /* 64 bit double */
                    /* ARMD */
                    t = (source & DMSIGN) != 0;
                    t |= ((dest & DMSIGN) != 0) ? 2 : 0;
                    td = dest + source;             /* DO ARMD */
                    dest = td;                      /* insert result into dest */
                    /* if both signs are neg and result sign is positive, overflow */
                    /* if both signs are pos and result sign is negative, overflow */
                    if (((t == 3) && ((dest & DMSIGN) == 0)) || 
                        ((t == 0) && ((dest & DMSIGN) != 0)))
                        ovr = 1;
                    if (td == 0)
                        CC |= CC4BIT;               /* dw is zero, so CC4 */
                    else
                    if (td & DMSIGN)
                        CC |= CC3BIT;               /* it is neg dw, so CC3  */
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                    break;
                }
                if (ovr)
                    CC |= CC1BIT;                   /* set overflow CC */
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's from PSD1 */
                PSD1 |= CC;                         /* update the CC's in the PSD */
                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (ovr && (modes & AEXPBIT)) {
                    TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                }
                break;

        case 0xEC>>2:               /* 0xEC ADR - ADR */ /* Branch unconditional or Branch True */
#ifdef TRME     /* set to 1 for traceme to work */
//                if (IR == 0xEE806ACD)
//  traceme = trstart;  /* start trace */
#endif
                /* GOOF alert, the assembler sets bit 31 to 1 so this test will fail*/
                /* so just test for F bit and go on */
                /* if ((FC & 5) != 0) { */
                if ((FC & 4) != 0) {
                    TRAPME = ADDRSPEC_TRAP;             /* bad address, error */
                    goto newpsd;                        /* go execute the trap now */
                }
                temp2 = CC;                             /* save the old CC's */
                CC = PSD1 & 0x78000000;                 /* get CC's if any */
                switch(reg) {
                case 0:     t = 1; break;
                case 1:     t = (CC & CC1BIT) != 0; break;
                case 2:     t = (CC & CC2BIT) != 0; break;
                case 3:     t = (CC & CC3BIT) != 0; break;
                case 4:     t = (CC & CC4BIT) != 0; break;
                case 5:     t = (CC & (CC2BIT|CC4BIT)) != 0; break;
                case 6:     t = (CC & (CC3BIT|CC4BIT)) != 0; break;
                case 7:     t = (CC & (CC1BIT|CC2BIT|CC3BIT|CC4BIT)) != 0; break;
                }
                if (t) {                                /* see if we are going to branch */
                    /* we are taking the branch, set CC's if indirect, else leave'm */
                    /* update the PSD with new address */
                    PSD1 = (PSD1 & 0xff000000) | (addr & 0xfffffe); /* set new PC */
                    i_flags |= BT;                      /* we branched, so no PC update */
                    if (((modes & BASEBIT) == 0) && (IR & IND)) /* see if CCs from last indirect are wanted */
                        PSD1 = (PSD1 & 0x87fffffe) | temp2;    /* insert last indirect CCs */
                }
                /* branch not taken, go do next instruction */
                break;

        case 0xF0>>2:               /* 0xF0 ADR - ADR */ /* Branch False or Branch Function True BFT */
                /* GOOF alert, the assembler sets bit 31 to 1 so this test will fail*/
                /* so just test for F bit and go on */
                /* if ((FC & 5) != 0) { */
                if ((FC & 4) != 0) {
                    TRAPME = ADDRSPEC_TRAP;             /* bad address, error */
                    goto newpsd;                        /* go execute the trap now */
                }
                temp2 = CC;                             /* save the old CC's */
                CC = PSD1 & 0x78000000;                 /* get CC's if any */
                switch(reg) {
                case 0:     t = (GPR[4] & (0x8000 >> ((CC >> 27) & 0xf))) != 0; break;
                case 1:     t = (CC & CC1BIT) == 0; break;
                case 2:     t = (CC & CC2BIT) == 0; break;
                case 3:     t = (CC & CC3BIT) == 0; break;
                case 4:     t = (CC & CC4BIT) == 0; break;
                case 5:     t = (CC & (CC2BIT|CC4BIT)) == 0; break;
                case 6:     t = (CC & (CC3BIT|CC4BIT)) == 0; break;
                case 7:     t = (CC & (CC1BIT|CC2BIT|CC3BIT|CC4BIT)) == 0; break;
                }
                if (t) {                                /* see if we are going to branch */
                    /* we are taking the branch, set CC's if indirect, else leave'm */
                    /* update the PSD with new address */
                    PSD1 = (PSD1 & 0xff000000) | (addr & 0xfffffe); /* set new PC */
                    i_flags |= BT;                      /* we branched, so no PC update */
                    if (((modes & BASEBIT) == 0) && (IR & IND)) /* see if CCs from last indirect are wanted */
                        PSD1 = (PSD1 & 0x87fffffe) | temp2;    /* insert last indirect CCs */
                }
                break;

        case 0xF4>>2:               /* 0xF4 RR|SD|ADR - RR|SB|WRD */ /* Branch increment */
                dest += ((t_uint64)1) << ((IR >> 21) & 3);/* use bits 9 & 10 to incr reg */
                if (dest != 0) {                        /* if reg is not 0, take the branch */
                    /* we are taking the branch, set CC's if indirect, else leave'm */
                    /* update the PSD with new address */
#if 0  /* set to 1 to stop branch to self, for now */
///* FIXME */         if (PC == (addr & 0x7FFFC)) {       /* BIB to current PC, bump branch addr */
/* FIXME */         if (PC == (addr & 0xFFFFFC)) {       /* BIB to current PC, bump branch addr */
                        addr += 4;
//                      fprintf(stderr, "BI? stopping BIB $ addr %x PC %x\r\n", addr, PC);
                        dest = 0;                       /* force reg to zero */
                    }
#endif
                    PSD1 = (PSD1 & 0xff000000) | (addr & 0xfffffe); /* set new PC */
                    if (((modes & BASEBIT) == 0) && (IR & IND)) /* see if CCs from last indirect are wanted */
                        PSD1 = (PSD1 & 0x87fffffe) | CC;    /* insert last CCs */
                    i_flags |= BT;                      /* we branched, so no PC update */
                }
                break;

        case 0xF8>>2:               /* 0xF8 SM|ADR - SM|ADR */ /* ZMx, BL, BRI, LPSD, LPSDCM, TPR, TRP */
                switch((opr >> 7) & 0x7) {              /* use bits 6-8 to determine instruction */
                case 0x0:       /* ZMx F80x */  /* SM */
                    dest = 0;                           /* destination value is zero */
                    i_flags |= SM;                      /* SM not set so set it to store value */
                    break;
                case 0x1:       /* BL F880 */
                    /* copy CC's from instruction and PC incremented by 4 */
                    GPR[0] = ((PSD1 & 0xff000000) | ((PSD1 + 4) & 0xfffffe));
                    if (((modes & BASEBIT) == 0) && (IR & IND)) /* see if CCs from last indirect are wanted */
                        PSD1 = (PSD1 & 0x87fffffe) | CC;    /* insert last CCs */
                    /* update the PSD with new address */
                    if (modes & BASEBIT) 
                        PSD1 = (PSD1 & 0xff000000) | (addr & 0xfffffe); /* bit 8-30 */
                    else
                        PSD1 = (PSD1 & 0xff000000) | (addr & 0x07fffe); /* bit 13-30 */
                    i_flags |= BT;                      /* we branched, so no PC update */
                    break;

                case 0x3:       /* LPSD F980 */
                    /* fall through */;
                case 0x5:       /* LPSDCM FA80 */
                    if ((modes & PRIVBIT) == 0) {       /* must be privileged */
                        TRAPME = PRIVVIOL_TRAP;         /* set the trap to take */
                        goto newpsd;                    /* Privlege violation trap */
                    }
                    CPUSTATUS |= 0x40;                  /* enable software traps */
                                                        /* this will allow attn and */
                                                        /* power fail traps */
                    if ((FC & 04) != 0 || FC == 2) {    /* can not be byte or doubleword */
                        /* Fault */
                        TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                        goto newpsd;                    /* go execute the trap now */
                    }
                    if ((TRAPME = Mem_read(addr, &temp))) { /* get PSD1 from memory */
                        goto newpsd;                    /* memory read error or map fault */
                    }
                    if (opr & 0x0200) {                 /* Was it LPSDCM? */
                        if ((TRAPME = Mem_read(addr+4, &temp2))) {   /* get PSD2 from memory */
                            goto newpsd;                /* memory read error or map fault */
                        }
                        PSD2 = temp2;                   /* PSD2 access good, so save it */
                    }
                    else {
                        if ((TRAPME = Mem_read(addr+4, &temp2))) {   /* get PSD2 from memory */
                            goto newpsd;                /* memory read error or map fault */
                        }
                        /* lpsd can not change cpix, so keep it */
                        PSD2 = ((PSD2 & 0x3fff) | (temp2 & 0xffffc000)); /* use current cpix */
                    }
                    PSD1 = temp;                        /* PSD1 good, so set it */
                    /* set the mode bits and CCs from the new PSD */
                    CC = PSD1 & 0x78000000;             /* extract bits 1-4 from PSD1 */
                    modes = PSD1 & 0x87000000;          /* extract bits 0, 5, 6, 7 from PSD 1 */
                    /* set new arithmetic trap state in CPUSTATUS */
                    if (PSD1 & AEXPBIT) {
                        CPUSTATUS |= AEXPBIT;           /* set bit 7 of cpu status */
                        modes |= AEXPBIT;               /* set arithmetic exception mode */
                    } else
                        CPUSTATUS &= ~AEXPBIT;          /* reset bit 7 of cpu status */
                    /* set new extended state in CPUSTATUS */
                    if (PSD1 & EXTDBIT) {
                        CPUSTATUS |= EXTDBIT;           /* set bit 5 of cpu status */
                        modes |= EXTDBIT;               /* set extended mode */
                    } else
                        CPUSTATUS &= ~EXTDBIT;          /* reset bit 5 of cpu status */
                    /* set new map mode and interrupt blocking state in CPUSTATUS */
                    if (PSD2 & MAPBIT) {
                        CPUSTATUS |= 0x00800000;        /* set bit 8 of cpu status */
                        modes |= MAPMODE;               /* set mapped mode */
                    } else
                        CPUSTATUS &= 0xff7fffff;        /* reset bit 8 of cpu status */
                    /* set interrupt blocking state */
                    if ((PSD2 & 0x8000) == 0) {         /* is it retain blocking state */
                        if (PSD2 & 0x4000)              /* no, is it set blocking state */
                            CPUSTATUS |= 0x80;          /* yes, set blk state in cpu status bit 24 */
                        else {
                            CPUSTATUS &= ~0x80;         /* no, reset blk state in cpu status bit 24 */
                            irq_pend = 1;               /* start scanning interrupts again */
                        }
                    }
                    PSD2 &= ~0x0000c000;                /* clear bit 48 & 49 to be unblocked */
                    if (CPUSTATUS & 0x80)               /* see if old mode is blocked */
                        PSD2 |= 0x00004000;             /* set to blocked state */

                    if (opr & 0x0200) {                 /* Was it LPSDCM? */
                        /* map bit must be on to load maps */
                        if (PSD2 & MAPBIT) {
                            /* set mapped mode in cpu status */
                            CPUSTATUS |= 0x00800000;    /* set bit 8 of cpu status */
                            /* we need to load the new maps */
                            TRAPME = load_maps(&PSD1);  /* load maps for new PSD */
                            PSD2 &= ~RETMBIT;           /* turn off retain bit in PSD2 */
                            SPAD[0xf5] = PSD2;          /* save the current PSD2 */
                            sim_debug(DEBUG_EXP, &cpu_dev,
                                "LPSDCM MAPS LOADED TRAPME = %x PSD1 %x PSD2 %x CPUSTATUS %x\n",
                                TRAPME, PSD1, PSD2, CPUSTATUS);
                        }
                        PSD2 &= ~RETMBIT;               /* turn off retain bit in PSD2 */
                    } else {
                        /* LPSD */
                        /* if cpix is zero, copy cpix from PSD2 in SPAD[0xf5] */
                        if ((PSD2 & 0x3fff) == 0) {
                            PSD2 |= (SPAD[0xf5] & 0x3fff);  /* use new cpix */
                        }
                    }
                    /* TRAPME can be error from LPSDCM or OK here */
                    skipinstr = 1;                      /* skip next instruction */
                    goto newpsd;                        /* load the new psd */
                    break;

                case 0x4:   /* JWCS */                  /* not used in simulator */
#ifdef TRME     /* set to 1 for traceme to work */
  traceme = trstart;  /* start trace */
    fprintf(stderr, "Got JWCS traceme = %x trstart = %x\n", traceme, trstart);
#endif
                    break;
                case 0x2:   /* BRI */                   /* TODO - only for 32/55 or 32/7X in PSW mode */
                case 0x6:   /* TRP */
                case 0x7:   /* TPR */
                    TRAPME = UNDEFINSTR_TRAP;           /* trap condition */
                    goto newpsd;                        /* undefined instruction trap */
                    break;
                }
                break;

/* F Class I/O device instruction format */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03 04 05|06 07 08|09 10 11 12|13 14 15|16|17 18 19 20 21 22 23|24 25 26 27 28 29 30 31| */
/* |     Op Code     |  Reg   |  I/O type |  Aug   |0 |   Channel Address  |  Device Sub-address   | */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */
/* E Class I/O device instruction format */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03 04 05|06 07 08 09 10 11 12|13 14 15|16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31| */
/* |     Op Code     |     Device Number  |  Aug   |                  Command Code                 | */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */
        case 0xFC>>2:               /* 0xFC IMM - IMM */ /* XIO, CD, TD, Interrupt Control */
                if ((modes & PRIVBIT) == 0) {           /* must be privileged to do I/O */
                    TRAPME = PRIVVIOL_TRAP;             /* set the trap to take */
                    TRAPSTATUS |= 0x1000;               /* set Bit 19 of Trap Status word */
                    goto newpsd;                        /* Privlege violation trap */
                }
                if ((opr & 0x7) != 0x07) {              /* aug is 111 for XIO instruction */
                    /* Process Non-XIO instructions */
                    uint32 status = 0;                  /* status returned from device */
                    uint32 device = (opr >> 3) & 0x7f;  /* get device code */
                    uint32 prior = device;              /* interrupt priority */
                    t = SPAD[prior+0x80];               /* get spad entry for interrupt */
                    addr = SPAD[0xf1] + (prior<<2);     /* vector address in SPAD */
                    addr = M[addr>>2];                  /* get the interrupt context block addr */

                    switch(opr & 0x7) {                 /* use bits 13-15 to determine instruction */
                    case 0x0:       /* EI  FC00  Enable Interrupt */
                        prior = (opr >> 3) & 0x7f;      /* get priority level */
                        /* SPAD entries for interrupts begin at 0x80 */
                        t = SPAD[prior+0x80];           /* get spad entry for interrupt */
                        if (t == 0 || t == 0xffffffff)  /* if not set up, die */
                            goto syscheck;              /* system check */
                        INTS[prior] |= INTS_ENAB;       /* enable specified int level */
                        SPAD[prior+0x80] |= SINT_ENAB;  /* enable in SPAD too */
                        irq_pend = 1;                   /* start scanning interrupts again */
                        /* test for clock at address 0x7f06 and interrupt level 0x18 */
                        if ((SPAD[prior+0x80] & 0x0f00ffff) == 0x7f06)
//                        if (prior == 0x18)              /* is this the clock starting */
                            rtc_setup(1, prior);        /* tell clock to start */
                        if ((SPAD[prior+0x80] & 0x0f00ffff) == 0x7f04)
//                        if (prior == 0x5f)              /* is this the interval timer starting */
                            itm_setup(1, prior);        /* tell timer to start */
#ifdef NOT_NOW
                        {
                            /* output current status of ints */
                            unsigned int nx;
//                            for (nx=0; nx<=prior; nx++) {
                            for (nx=0; nx<112; nx++) {
                                if (INTS[nx] & INTS_ACT)
                                    fprintf(stderr, "EI %x Interrupt level %x active\n", prior, nx);
                                if (INTS[nx] & INTS_REQ)
                                    fprintf(stderr, "EI %x Interrupt level %x requesting\n", prior, nx);
                                if (INTS[nx] & INTS_ENAB)
                                    fprintf(stderr, "EI %x Interrupt level %x enabled\n", prior, nx);
                            }
                        }
#endif
                        break;

                    case 0x1:       /* DI FC01 */
                        prior = (opr >> 3) & 0x7f;      /* get priority level */
                        if (prior > 0x6f)               /* ignore for invalid levels */
//                            goto syscheck;              /* system check */
                            break;
                        /* SPAD entries for interrupts begin at 0x80 */
                        t = SPAD[prior+0x80];           /* get spad entry for interrupt */
                        if (t != 0 && t != 0xffffffff)  /* if not set up, not class F */
                        if ((t & 0x0f000000) == 0x0f000000) /* if class F ignore instruction */
                            break;
                        /* active state is left alone */
                        INTS[prior] &= ~INTS_ENAB;      /* disable specified int level */
                        INTS[prior] &= ~INTS_REQ;       /* clears any requests also */
                        SPAD[prior+0x80] &= ~SINT_ENAB; /* disable in SPAD too */
                        /* test for clock at address 0x7f06 and interrupt level 0x18 */
                        if ((SPAD[prior+0x80] & 0x0f00ffff) == 0x7f06)
//                        if (prior == 0x18)              /* is this the clock stopping */
                            rtc_setup(0, prior);        /* tell clock to stop */
                        if ((SPAD[prior+0x80] & 0x0f00ffff) == 0x7f04)
//                        if (prior == 0x5f)              /* is this the interval timer stopping */
                            itm_setup(0, prior);        /* tell timer to stop */
#ifdef NOT_NOW
                        {
                            /* output current status of ints */
                            unsigned int nx;
//                            for (nx=0; nx<=prior; nx++) {
                            for (nx=0; nx<112; nx++) {
                                if (INTS[nx] & INTS_ACT)
                                    fprintf(stderr, "DI %x Interrupt level %x active\n", prior, nx);
                                if (INTS[nx] & INTS_REQ)
                                    fprintf(stderr, "DI %x Interrupt level %x requesting\n", prior, nx);
                                if (INTS[nx] & INTS_ENAB)
                                    fprintf(stderr, "DI %x Interrupt level %x enabled\n", prior, nx);
                            }
                        }
#endif
                        break;

                    case 0x2:       /* RI FC02 */
                        prior = (opr >> 3) & 0x7f;      /* get priority level */
                        if (prior > 0x6f)               /* ignore for invalid levels */
//                            goto syscheck;              /* system check */
                            break;
                        /* SPAD entries for interrupts begin at 0x80 */
                        t = SPAD[prior+0x80];           /* get spad entry for interrupt */
                        if (t != 0 && t != 0xffffffff)  /* if not set up, not class F */
                        if ((t & 0x0f000000) == 0x0f000000) /* if class F ignore instruction */
                            break;
                        INTS[prior] |= INTS_REQ;        /* set the request flag for this level */
                        irq_pend = 1;                   /* start scanning interrupts again */
#ifdef NOT_NOW
                        {
                            /* output current status of ints */
                            unsigned int nx;
//                            for (nx=0; nx<=prior; nx++) {
                            for (nx=0; nx<112; nx++) {
                                if (INTS[nx] & INTS_ACT)
                                    fprintf(stderr, "RI %x Interrupt level %x active\n", prior, nx);
                                if (INTS[nx] & INTS_REQ)
                                    fprintf(stderr, "RI %x Interrupt level %x requesting\n", prior, nx);
                                if (INTS[nx] & INTS_ENAB)
                                    fprintf(stderr, "RI %x Interrupt level %x enabled\n", prior, nx);
                            }
                        }
#endif
                        break;

                    case 0x3:       /* AI FC03 */
                        prior = (opr >> 3) & 0x7f;      /* get priority level */
                        if (prior > 0x6f)               /* ignore for invalid levels */
//                            goto syscheck;              /* system check */
                            break;
                        /* SPAD entries for interrupts begin at 0x80 */
                        t = SPAD[prior+0x80];           /* get spad entry for interrupt */
                        if (t != 0 && t != 0xffffffff)  /* if not set up, not class F */
                        if ((t & 0x0f000000) == 0x0f000000) /* if class F ignore instruction */
                            break;
                        INTS[prior] |= INTS_ACT;        /* activate specified int level */
                        SPAD[prior+0x80] |= SINT_ACT;   /* activate in SPAD too */
                        irq_pend = 1;                   /* start scanning interrupts again */
#ifdef NOT_NOW
                        {
                            /* output current status od ints */
                            unsigned int nx;
//                            for (nx=0; nx<=prior; nx++) {
                            for (nx=0; nx<112; nx++) {
                                if (INTS[nx] & INTS_ACT)
                                    fprintf(stderr, "AI %x Interrupt level %x active\n", prior, nx);
                                if (INTS[nx] & INTS_REQ)
                                    fprintf(stderr, "AI %x Interrupt level %x requesting\n", prior, nx);
                                if (INTS[nx] & INTS_ENAB)
                                    fprintf(stderr, "AI %x Interrupt level %x enabled\n", prior, nx);
                            }
                        }
#endif
                        break;

                    case 0x4:       /* DAI FC04 */
                        prior = (opr >> 3) & 0x7f;      /* get priority level */
                        if (prior > 0x6f)               /* ignore for invalid levels */
//                            goto syscheck;              /* system check */
                            break;
                        /* SPAD entries for interrupts begin at 0x80 */
                        t = SPAD[prior+0x80];           /* get spad entry for interrupt */
                        if (t != 0 && t != 0xffffffff)  /* if not set up, not class F */
                        if ((t & 0x0f000000) == 0x0f000000) /* if class F ignore instruction */
                            break;
                        INTS[prior] &= ~INTS_ACT;       /* deactivate specified int level */
                        SPAD[prior+0x80] &= ~SINT_ACT;  /* deactivate in SPAD too */
                        irq_pend = 1;                   /* start scanning interrupts again */
                        /* instruction following a DAI can not be interrupted */
                        /* skip tests for interrupts if this is the case */
                        skipinstr = 1;                  /* skip interrupt test */
#ifdef MAYBE_BAD
                        if (prior == 0x18) {            /* is this the clock stopping */
//                            rtc_setup(0, prior);        /* tell clock to stop */
                            INTS[prior] &= ~INTS_REQ;       /* clears any requests also */
                            INTS[prior] &= ~INTS_ENAB;      /* clears any requests also */
                            SPAD[prior+0x80] &= ~SINT_ENAB; /* disable in SPAD too */
                        }
#endif
#ifdef NOT_NOW
                        {
                            /* output current status of ints */
                            unsigned int nx;
//                            for (nx=0; nx<=prior; nx++) {
                            for (nx=0; nx<112; nx++) {
                                if (INTS[nx] & INTS_ACT)
                                    fprintf(stderr, "DAI %x Interrupt level %x active\n", prior, nx);
                                if (INTS[nx] & INTS_REQ)
                                    fprintf(stderr, "DAI %x Interrupt level %x requesting\n", prior, nx);
                                if (INTS[nx] & INTS_ENAB)
                                    fprintf(stderr, "DAI %x Interrupt level %x enabled\n", prior, nx);
                            }
                        }
#endif
                        break;

                    case 0x5:       /* TD FC05 */       /* bits 13-15 is test code type */
                    case 0x6:       /* CD FC06 */
                        /* If CD or TD, make sure device is not F class device */
                        /* the channel must be defined as a non class F I/O channel in SPAD */
                        /* if class F, the system will generate a system check trap */
                        t = SPAD[device];               /* get spad entry for channel */
                        if (t == 0 || t == 0xffffffff)  /* if not set up, die */
                            goto syscheck;              /* system check */
                        if ((t & 0x0f000000) == 0x0f000000) {   /* class in bits 4-7 */
syscheck:
                            TRAPME = SYSTEMCHK_TRAP;    /* trap condition if F class */
                            TRAPSTATUS &= ~BIT0;        /* class E error bit */
                            TRAPSTATUS &= ~BIT1;        /* I/O processing error */
                            goto newpsd;                /* machine check trap */
                        }
                        if (opr & 0x1) {                /* see if CD or TD */
                            /* TODO process a TD */
//                          if ((TRAPME = testEIO(device, testcode, &status)))
//                              goto newpsd;            /* error returned, trap cpu */
                            /* return status has new CC's in bits 1-4 of status word */
                            PSD1 = ((PSD1 & 0x87fffffe) | (status & 0x78000000));   /* insert status CCs */
//fprintf(stderr, "EIO TD chan %.4x spad %.8x\r\n", device, t);
                            goto inv;                   /* invalid instruction until I fix it */
                        } else {
                            /* TODO process a CD */
//                          if ((TRAPME = startEIO(device, &status)))
//                              goto newpsd;            /* error returned, trap cpu */
//                          t = SPAD[device];           /* get spad entry for channel */
                            /* t has spad entry for device */
                            /* get the 1's comp of interrupt address from bits 9-15 SPAD entry */
                            ix = (t & 0x007f0000) >> 16;/* get the 1's comp of int level */
                            ix = 127 - ix;              /* get positive number for interrupt */
                            temp = (IR & 0x7f);         /* get cmd from instruction */
                            if (device == 0x7f) {
                                status = itm_rdwr(temp, GPR[0], ix);    /* read/write the interval timer */
                                /* see if thes cmd does not return value */
                                if ((temp != 0x39) && (temp != 0x3d) && (temp != 0x20)) 
                                    GPR[0] = status;    /* return count in reg 0 */
                                /* No CC's going out */
                            } else {
                                goto inv;               /* invalid instruction until I fix it */
                            }
                        }
                        break;
                    case 0x7:       /* XIO FC07*/       /* should never get here */
                        break;
                    }
                    break;          /* skip over XIO code */
                }

                /* Process XIO instructions */
                /* if reg is non-zero, add reg to 15 bits from instruction */
                if (reg)
                    temp2 = (IR & 0x7fff) + (GPR[reg] & 0x7fff);    /* set new chan/suba into IR */
                else
                    temp2 = (IR & 0x7fff);              /* set new chan/suba into IR */
                lchan = (temp2 & 0x7F00) >> 8;          /* get 7 bit logical channel address */
                suba = temp2 & 0xFF;                    /* get 8 bit subaddress */
                /* the channel must be defined as a class F I/O channel in SPAD */
                /* if not class F, the system will generate a system check trap */
                t = SPAD[lchan];                        /* get spad entry for channel */
#ifdef UTXBUG
//    fprintf(stderr, "XIO step 1 lchan = %x, spad[%x] %x mem[0] %x\n", lchan, lchan, t, M[0]);
#endif
                if (t == 0 || t == 0xffffffff)          /* if not set up, die */
                    goto syscheck;                      /* machine check */
                /* sim_debug(DEBUG_EXP, &cpu_dev, "$$ XIO lchan %x sa %x spad %.8x\n", lchan, suba, t); */
                if ((t & 0x0f000000) != 0x0f000000) {   /* class in bits 4-7 */
mcheck:
//fprintf(stderr, "MCHECK XIO PSD1 %x PSD2 %x lchan %x sa %x spad %.8x\n", PSD1, PSD2, lchan, suba, t);
                    TRAPME = MACHINECHK_TRAP;           /* trap condition */
                    TRAPSTATUS |= BIT0;                 /* class F error bit */
                    TRAPSTATUS &= ~BIT1;                /* I/O processing error */
                    goto newpsd;                        /* machine check trap */
                }
                /* get real channel from spad device entry */
                chan = (t & 0x7f00) >> 8;               /* real channel */
                /* get the 1's comp of interrupt address from bits 9-15 SPAD entry */
                ix = (t & 0x007f0000) >> 16;            /* get the 1's comp of int level */
                ix = 127 - ix;                          /* get positive number for interrupt */
                bc = SPAD[ix+0x80];                     /* get interrupt entry for channel */
                /* SPAD address F1 has interrupt table address */
                temp = SPAD[0xf1] + (ix<<2);            /* vector address in SPAD */
                if ((TRAPME = Mem_read(temp, &addr))) { /* get interrupt context block addr */
//fprintf(stderr, "MCHECK2 XIO PSD1 %x PSD2 %x chan %x temp %x addr %.8x\n", PSD1, PSD2, chan, temp, addr);
                    goto mcheck;                        /* machine check if not there */
                }
                                                        /* the context block contains the old PSD, */
                                                        /* new PSD, IOCL address, and I/O status address */
                if ((addr == 0) || (addr == 0xffffffff)) {  /* must be initialized address */
//fprintf(stderr, "MCHECK3 XIO PSD1 %x PSD2 %x chan %x temp %x addr %.8x\n", PSD1, PSD2, chan, temp, addr);
                    goto mcheck;                        /* bad int icb address */
                }
                if ((TRAPME = Mem_read(addr+16, &temp))) { /* get iocl address from icb wd 4 */
//fprintf(stderr, "MCHECK4 XIO PSD1 %x PSD2 %x chan %x temp %x addr %.8x\n", PSD1, PSD2, chan, temp, addr);
                    goto mcheck;                        /* machine check if not there */
                }
                /* iocla must be valid addr if it is a SIO instruction */
                if (((temp & MASK24) == 0) && (((opr >> 2) & 0xf) == 2))  {
//fprintf(stderr, "MCHECK5 XIO PSD1 %x PSD2 %x chan %x temp %x addr %.8x\n", PSD1, PSD2, chan, temp, addr);
                    goto mcheck;                        /* bad iocl address */
                }

        sim_debug(DEBUG_EXP, &cpu_dev, "XIO ready chan %x intr %x icb %x iocla %x iocd1 %.8x iocd2 %.8x\n",
            chan, ix, addr, addr+16, M[temp>>2], M[(temp+4)>>2]);
                /* at this point, the channel has a valid SPAD channel entry */
                /* t is SPAD entry contents for chan device */
                /* temp2 has IR + reg contents if reg != 0 */
                /* lchan - logical channel address */
                /* chan - channel address */
                /* suba - channel device subaddress */
                /* ix - positive interrupt level */
                /* addr - ICB for specified interrupt level, points to 6 wd block */
                /* temp - First IOCD address */
        sim_debug(DEBUG_EXP, &cpu_dev, "XIO switch %x lchan %x, chan %x intr %x chsa %x IOCDa %.8x\n",
            ((opr>>3)&0x0f), lchan, chan, ix, (chan<<8)|suba, temp);
                switch((opr >> 3) & 0xf) {              /* use bits 9-12 to determine I/O instruction */
                    uint32 status;                      /* status returned by various functions */
                    uint16 chsa;                        /* logical device address */

                    case 0x00:                          /* Unassigned */
                    case 0x01:                          /* Unassigned */
                    case 0x0A:                          /* Unassigned */
                        TRAPME = UNDEFINSTR_TRAP;       /* trap condition */
                        goto newpsd;                    /* undefined instruction trap */
                        break;

                    case 0x09:                          /* Enable write channel ECWCS */
                    case 0x0B:                          /* Write channel WCS WCWCS */
                        /* TODO, provide support code */
                        /* for now or maybe forever, return unsupported transaction */
                        PSD1 = ((PSD1 & 0x87fffffe) | (CC2BIT|CC4BIT)); /* insert status 5 */
                        /* just give unsupported transaction */
#ifdef NOT_REALLY_CORRECT
                        TRAPME = UNDEFINSTR_TRAP;       /* trap condition */
                        goto newpsd;                    /* undefined instruction trap */
#endif
                        break;

                    case 0x02:      /* Start I/O SIO */
                        chsa = temp2 & 0x7FFF;          /* get logical device address */
                        if ((TRAPME = startxio(chsa, &status)))
                            goto newpsd;                /* error returned, trap cpu */
                        PSD1 = ((PSD1 & 0x87fffffe) | (status & 0x78000000));   /* insert status */
                        sim_debug(DEBUG_EXP, &cpu_dev, "XIO SIO ret chan %x chsa %x status %x M[0] %x\n",
                            chan, (chan<<8)|suba, status, M[0]);
                        break;
                            
                    case 0x03:      /* Test I/O TIO */
                        chsa = temp2 & 0x7FFF;          /* get logical device address */
                        if ((TRAPME = testxio(chsa, &status))) {
// fprintf(stderr, "XIO TIO ret PSD1 %x chan %x chsa %x status %x\n", PSD1, chan, (chan<<8)|suba, status);
                            goto newpsd;                /* error returned, trap cpu */
                        }
                        PSD1 = ((PSD1 & 0x87fffffe) | (status & 0x78000000));   /* insert status */
                        sim_debug(DEBUG_EXP, &cpu_dev, "XIO TIO ret chan %x chsa %x status %x\n",
                            chan, (chan<<8)|suba, status);
                        break;
                            
                    case 0x04:      /* Stop I/O STPIO */
                        chsa = temp2 & 0x7FFF;          /* get logical device address */
                        if ((TRAPME = stopxio(chsa, &status)))
                            goto newpsd;                /* error returned, trap cpu */
                        PSD1 = ((PSD1 & 0x87fffffe) | (status & 0x78000000));   /* insert status */
                        sim_debug(DEBUG_EXP, &cpu_dev, "XIO STPIO ret chan %x chsa %x status %x\n",
                            chan, (chan<<8)|suba, status);
                        break;

                    /* TODO Finish XIO */
                    case 0x05:      /* Reset channel RSCHNL */
#ifdef TRME     /* set to 1 for traceme to work */
//  traceme = trstart;  /* start trace */
#endif
                        chsa = temp2 & 0x7FFF;          /* get logical device address */
                        /* SPAD entries for interrupts begin at 0x80 */
                        INTS[ix] &= ~INTS_REQ;          /* clears any requests */
                        INTS[ix] &= ~INTS_ACT;          /* deactivate specified int level */
                        SPAD[ix+0x80] &= ~SINT_ACT;     /* deactivate in SPAD too */
                        /* TODO Maybe we need to disable int too???? */
                        if ((TRAPME = rschnlxio(chsa, &status)))
                            goto newpsd;                /* error returned, trap cpu */
                        PSD1 = ((PSD1 & 0x87fffffe) | (status & 0x78000000));   /* insert status */
//fprintf(stderr, "XIO RSCHNL ret chan %x chsa %x status %x\n", chan, (chan<<8)|suba, status);
                        sim_debug(DEBUG_EXP, &cpu_dev, "XIO RSCHNL ret chan %x chsa %x status %x\n",
                            chan, (chan<<8)|suba, status);
                        break;

                    case 0x06:      /* Halt I/O HIO */
                        chsa = temp2 & 0x7FFF;          /* get logical device address */
                        if ((TRAPME = haltxio(chsa, &status)))
                            goto newpsd;                /* error returned, trap cpu */
                        PSD1 = ((PSD1 & 0x87fffffe) | (status & 0x78000000));   /* insert status */
                        sim_debug(DEBUG_EXP, &cpu_dev, "HIO HALTXIO ret chan %x chsa %x status %x\n",
                            chan, (chan<<8)|suba, status);
                        break;

                    case 0x07:      /* Grab controller GRIO n/u */
                        chsa = temp2 & 0x7FFF;          /* get logical device address */
                        if ((TRAPME = grabxio(chsa, &status)))
                            goto newpsd;                /* error returned, trap cpu */
                        PSD1 = ((PSD1 & 0x87fffffe) | (status & 0x78000000));   /* insert status */
                        sim_debug(DEBUG_EXP, &cpu_dev, "XIO GRIO ret chan %x chsa %x status %x\n",
                            chan, (chan<<8)|suba, status);
                        break;

                    case 0x08:      /* Reset controller RSCTL */
                        chsa = temp2 & 0x7FFF;          /* get logical device address */
                        if ((TRAPME = stopxio(chsa, &status)))
                            goto newpsd;                /* error returned, trap cpu */
                        PSD1 = ((PSD1 & 0x87fffffe) | (status & 0x78000000));   /* insert status */
                        sim_debug(DEBUG_EXP, &cpu_dev, "XIO RSCTL ret chan %x chsa %x status %x\n",
                            chan, (chan<<8)|suba, status);
                        break;

                    /* TODO Finish XIO interrupts */
                    case 0x0C:      /* Enable channel interrupt ECI */
                        chsa = temp2 & 0x7FFF;          /* get logical device address */
                        sim_debug(DEBUG_EXP, &cpu_dev, "XIO ECI chan %x sa %x spad %.8x\n", chan, suba, t);
                        /* SPAD entries for interrupts begin at 0x80 */
                        INTS[ix] |= INTS_ENAB;          /* enable specified int level */
                        SPAD[ix+0x80] |= SINT_ENAB;     /* enable in SPAD too */
                        irq_pend = 1;                   /* start scanning interrupts again */
                        PSD1 = ((PSD1 & 0x87fffffe) | (0x40000000 & 0x78000000));   /* insert cc1 status */
                        break;

                    case 0x0D:      /* Disable channel interrupt DCI */
                        chsa = temp2 & 0x7FFF;          /* get logical device address */
                        sim_debug(DEBUG_EXP, &cpu_dev, "XIO DCI chan %x sa %x spad %.8x\n", chan, suba, t);
                        /* SPAD entries for interrupts begin at 0x80 */
                        INTS[ix] &= ~INTS_ENAB;         /* disable specified int level */
                        SPAD[ix+0x80] &= ~SINT_ENAB;    /* disable in SPAD too */
                        break;

                    case 0x0E:      /* Activate channel interrupt ACI */
                        chsa = temp2 & 0x7FFF;          /* get logical device address */
                        sim_debug(DEBUG_EXP, &cpu_dev, "XIO ACI chan %x sa %x spad %.8x\n", chan, suba, t);
                        /* SPAD entries for interrupts begin at 0x80 */
                        INTS[ix] |= INTS_ACT;           /* activate specified int level */
                        SPAD[ix+0x80] |= SINT_ACT;      /* enable in SPAD too */
                        INTS[ix] &= ~INTS_REQ;          /* clears any requests also */
                        break;

                    case 0x0F:      /* Deactivate channel interrupt DACI */
                                    /* Note, instruction following DACI is not interruptable */
                        chsa = temp2 & 0x7FFF;          /* get logical device address */
                        sim_debug(DEBUG_EXP, &cpu_dev, "XIO DACI chan %x sa %x spad %.8x\n", chan, suba, t);
                        /* SPAD entries for interrupts begin at 0x80 */
                        INTS[ix] &= ~INTS_ACT;          /* deactivate specified int level */
                        SPAD[ix+0x80] &= ~SINT_ACT;     /* deactivate in SPAD too */
                        irq_pend = 1;                   /* start scanning interrupts again */
                        skipinstr = 1;                  /* skip interrupt test */
                        /* NOTE CC must be returned */
                        break;
                }                   /* end of XIO switch */
                break;
        }                   /* End of Instruction Switch */
        /* [][][][][][][][][][][][][][][][][][][][][][][][][][][][][][][][][][][][] */

        /* any instruction with an arithmetic exception will still end up here */
        /* after the instruction is done and before incrementing the PC, */
        /* we will trap the cpu if ovl is set nonzero by an instruction */

        /* Store result to register */
        if (i_flags & SD) {
            if (dbl) {                              /* if double reg, store 2nd reg */
                if (reg & 1) {                      /* is it double regs into odd reg */
                    TRAPME = ADDRSPEC_TRAP;         /* bad address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
            } else {
                GPR[reg] = (uint32)(dest & FMASK);  /* save the reg */
            }
        }

        /* Store result to base register */
        if (i_flags & SB) {
            if (dbl)  {                             /* no dbl wd store to base regs */
                TRAPME = ADDRSPEC_TRAP;             /* bad address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            BR[reg] = (uint32)(dest & FMASK);       /* save the base reg */
        }

        /* Store result to memory */
        if (i_flags & SM) {
            /* Check if byte of half word */
            if (((FC & 04) || (FC & 5) == 1)) {     /* hw or byte requires read first */
                if ((TRAPME = Mem_read(addr, &temp))) { /* get the word from memory */
                    goto newpsd;                    /* memory read error or map fault */
                }
            }
            switch(FC) {
            case 2:         /* double word store */
                if ((addr & 7) != 2) {
                    TRAPME = ADDRSPEC_TRAP;         /* address not on dbl wd boundry, error */
//fprintf(stderr, "SM AD02 DBL WD opr %x FC %x addr = %x dest %lx\r\n", opr, FC, addr, dest);
                    goto newpsd;                    /* go execute the trap now */
                }
                temp = (uint32)(dest & MASK32);/* get lo 32 bit */
                if ((TRAPME = Mem_write(addr + 4, &temp)))
                    goto newpsd;                    /* memory write error or map fault */
                temp = (uint32)(dest >> 32);        /* move upper 32 bits to lo 32 bits */
                break;

            case 0:     /* word store */
                temp = (uint32)(dest & FMASK);      /* mask 32 bit of reg */
                if ((addr & 3) != 0) {
                    /* Address fault */
                    TRAPME = ADDRSPEC_TRAP;         /* address not on wd boundry, error */
//fprintf(stderr, "SM AD02 WD opr %x FC %x addr = %x dest %x\r\n", opr, FC, addr, temp);
                    goto newpsd;                    /* go execute the trap now */
                }
                break;

            case 1:     /* left halfword write */
                temp &= RMASK;                      /* mask out 16 left most bits */
                temp |= (uint32)(dest & RMASK) << 16;   /* put into left most 16 bits */
                if ((addr & 1) != 1) {
                    /* Address fault */
                    TRAPME = ADDRSPEC_TRAP;         /* address not on hw boundry, error */
//fprintf(stderr, "SM AD02 LHWD opr %x FC %x addr = %x dest %x\r\n", opr, FC, addr, temp);
                    goto newpsd;                    /* go execute the trap now */
                }
                break;

            case 3:     /* right halfword write */
                temp &= LMASK;                      /* mask out 16 right most bits */
                temp |= (uint32)(dest & RMASK);     /* put into right most 16 bits */
                if ((addr & 3) != 3) {
                    TRAPME = ADDRSPEC_TRAP;         /* address not on hw boundry, error */
//fprintf(stderr, "SM AD02 RHWD opr %x FC %x addr = %x dest %x\r\n", opr, FC, addr, temp);
                    goto newpsd;                    /* go execute the trap now */
                }
                break;

            case 4:
            case 5:
            case 6:
            case 7:     /* byte store operation */
                temp &= ~(0xFF << (8 * (7 - FC)));  /* clear the byte to store */
                temp |= (uint32)(dest & 0xFF) << (8 * (7 - FC));    /* insert new byte */
                break;
            }
            /* store back the modified memory location */
            if ((TRAPME = Mem_write(addr, &temp)))  /* store back to memory */
                goto newpsd;                        /* memory write error or map fault */
        }

        /* Update condition code registers */
        if (i_flags & SCC) {
            PSD1 &= 0x87FFFFFE;                     /* clear the old CC's */
            if (ovr)                                /* if overflow, set CC1 */
                CC = CC1BIT;                        /* show we had AEXP */
            else
                CC = 0;                             /* no CC's yet */   
            if (dest & DMSIGN)                      /* if neg, set CC3 */
                CC |= CC3BIT;                       /* if neg, set CC3 */
            else if (dest == 0)
                CC |= CC4BIT;                       /* if zero, set CC4 */
            else
                CC |= CC2BIT;                       /* if gtr than zero, set CC2 */
            PSD1 |= CC & 0x78000000;                /* update the CC's in the PSD */
        }

        /* check if we had an arithmetic exception on the last instruction*/
        if (ovr && (modes & AEXPBIT)) {
#ifdef TRME
//    fprintf(stderr, "Calling newpsd for ovr = %x at end\n", ovr);
#endif
            TRAPME = AEXPCEPT_TRAP;                 /* trap the system now */
            goto newpsd;                            /* process the trap */
        }

        /* Update instruction pointer to next instruction */
        if ((i_flags & BT) == 0) {                  /* see if PSD was replaced on a branch instruction */
            /* branch not taken, so update the PC */
            if (EXM_EXR != 0) {                     /* special handling for EXM, EXR, EXRR */
                PSD1 = (PSD1 + 4) | (((PSD1 & 2) >> 1) & 1);
                EXM_EXR = 0;                        /* reset PC increment for EXR */
            } else
            if (i_flags & HLF) {
                PSD1 = (PSD1 + 2) | (((PSD1 & 2) >> 1) & 1);
            } else {
                PSD1 = (PSD1 + 4) | (((PSD1 & 2) >> 1) & 1);
            }
        } else {
            EXM_EXR = 0;                            /* reset PC increment for EXR */
#ifdef DO_NEW_NOP
            drop_nop = 0;
#endif
        }

        OPSD1 &= 0x87FFFFFE;                        /* clear the old CC's */
        OPSD1 |= PSD1 & 0x78000000;                 /* update the CC's in the PSD */
        /* TODO Update other history information for this instruction */
        if (hst_lnt) {
            hst[hst_p].opsd1 = OPSD1;               /* update the CC in opsd1 */
            hst[hst_p].npsd1 = PSD1;                /* save new psd1 */
            hst[hst_p].npsd2 = PSD2;                /* save new psd2 */
            hst[hst_p].modes = modes;               /* save current mode bits */
            for (ix=0; ix<8; ix++) {
                hst[hst_p].reg[ix] = GPR[ix];       /* save reg */
                hst[hst_p].reg[ix+8] = BR[ix];      /* save breg */
            }
        }

#ifdef TRME     /* set to 1 for traceme to work */
    if (traceme >= trstart) {
        OPSD1 &= 0x87FFFFFE;                        /* clear the old CC's */
        OPSD1 |= PSD1 & 0x78000000;                 /* update the CC's in the PSD */
        if (modes & MAPMODE)
            fprintf(stderr, "M%.8x %.8x ", OPSD1, OIR);
        else
            fprintf(stderr, "U%.8x %.8x ", OPSD1, OIR);
        fprint_inst(stderr, OIR, 0);                /* display instruction */
        fprintf(stderr, "\r\n");
        fprintf(stderr, "\tR0=%.8x R1=%.8x R2=%.8x R3=%.8x", GPR[0], GPR[1], GPR[2], GPR[3]);
        fprintf(stderr, " R4=%.8x R5=%.8x R6=%.8x R7=%.8x", GPR[4], GPR[5], GPR[6], GPR[7]);
        fprintf(stderr, "\r\n");
        if (modes & BASEBIT) {
        fprintf(stderr, "\tB0=%.8x B1=%.8x B2=%.8x B3=%.8x", BR[0], BR[1], BR[2], BR[3]);
        fprintf(stderr, " B4=%.8x B5=%.8x B6=%.8x B7=%.8x", BR[4], BR[5], BR[6], BR[7]);
        fprintf(stderr, "\r\n");
        fflush(stderr);
        }
    }
#endif
sim_debug(DEBUG_DATA, &cpu_dev, "R0=%08x R1=%08x R2=%08x R3=%08x\n", GPR[0], GPR[1], GPR[2], GPR[3]);
sim_debug(DEBUG_DATA, &cpu_dev, "R4=%08x R5=%08x R6=%08x R7=%08x\n", GPR[4], GPR[5], GPR[6], GPR[7]);
        continue;   /* keep running */
//      break;      /* quit for now after each instruction */

newpsd:
        /* Trap Context Block - 6 words */
        /* WD1  Old PSD Wd 1 */
        /* WD2  Old PSD Wd 2 */
        /* WD3  New PSD WD 1 */
        /* WD4  New PSD Wd 2 */
            /* WD5  Multi Use */     /* N/U for Interrupts */
            /* WD6  Multi Use */     /* N/U for Interrupts */

            /* WD5  Multi Use */     /* IOCL address for I/O */
            /* WD6  Multi Use */     /* Status address for I/O */

            /* WD5  Multi Use */     /* Secondary vector table for SVC */
            /* WD6  Multi Use */     /* N/U for SVC */

            /* WD5  Multi Use */     /* Trap status word for traps */
            /* WD6  Multi Use */     /* N/U for traps */

            /* WD5  Multi Use */     /* Trap status word for page faults */
            /* WD6  Multi Use */     /* Page fault status word */
                /* Bit 0 = 0  The map fault was caused by an instruction fetch */
                /*       = 1  The mp fault was caused by an operand access */
                /* Bits 1-20  Always zero */
                /* Map register number (logical map block number) */

        /* we get here from a LPSD, LPSDCM, INTR, or TRAP */
        if (TRAPME) {
            /* SPAD location 0xf0 has trap vector base address */
            uint32 tta = SPAD[0xf0];                /* get trap table address in memory */
            uint32 tvl;                             /* trap vector location */
            if (tta == 0 || tta == 0xffffffff)
                tta = 0x80;                         /* if not set, assume 0x80 FIXME */
            /* Trap Table Address in memory is pointed to by SPAD 0xF0 */
            /* TODO update cpu status and trap status words with reason too */
            switch(TRAPME) {
            case POWERFAIL_TRAP:                    /* 0x80 power fail trap */
            case POWERON_TRAP:                      /* 0x84 Power-On trap */
            case MEMPARITY_TRAP:                    /* 0x88 Memory Parity Error trap */
            case NONPRESMEM_TRAP:                   /* 0x8C Non Present Memory trap */
            case UNDEFINSTR_TRAP:                   /* 0x90 Undefined Instruction Trap */
            case PRIVVIOL_TRAP:                     /* 0x94 Privlege Violation Trap */
//MOVED     case SVCCALL_TRAP:                      /* 0x98 Supervisor Call Trap */
            case MACHINECHK_TRAP:                   /* 0x9C Machine Check Trap */
            case SYSTEMCHK_TRAP:                    /* 0xA0 System Check Trap */
            case MAPFAULT_TRAP:                     /* 0xA4 Map Fault Trap */
            case IPUUNDEFI_TRAP:                    /* 0xA8 IPU Undefined Instruction Trap */
            case SIGNALIPU_TRAP:                    /* 0xAC Signal IPU/CPU Trap */
            case ADDRSPEC_TRAP:                     /* 0xB0 Address Specification Trap */
            case CONSOLEATN_TRAP:                   /* 0xB4 Console Attention Trap */
            case PRIVHALT_TRAP:                     /* 0xB8 Privlege Mode Halt Trap */
            case AEXPCEPT_TRAP:                     /* 0xBC Arithmetic Exception Trap */
            case CACHEERR_TRAP:                     /* 0xC0 Cache Error Trap (V9 Only) */
                /* drop through */
            default:
#ifdef TRME     /* set to 1 for traceme to work */
    if (traceme >= trstart) {
        fprintf(stderr, "##TRAPME %x page# %x LOAD MAPS PSD1 %x PSD2 %x CPUSTATUS %x\r\n",
                TRAPME, pfault, PSD1, PSD2, CPUSTATUS);
    }
#endif
                /* adjust PSD1 to next instruction */
                /* Update instruction pointer to next instruction */
                if ((i_flags & BT) == 0) {          /* see if PSD was replaced on a branch instruction */
                    /* branch not taken, so update the PC */
                    if (EXM_EXR != 0) {             /* special handling for EXM, EXR, EXRR */
                        PSD1 = (PSD1 + 4) | (((PSD1 & 2) >> 1) & 1);
                        EXM_EXR = 0;                /* reset PC increment for EXR */
                    } else
                    if (i_flags & HLF) {            /* if nop in rt hw, bump pc a word */
#ifdef DO_NEW_NOP
//                        if ((drop_nop) && ((CPU_MODEL == MODEL_67) || (CPU_MODEL >= MODEL_V6)))
                        if ((drop_nop) && ((CPU_MODEL == MODEL_67) || (CPU_MODEL == MODEL_V6)))
#else
//                        if ((skipinstr == 2) && ((CPU_MODEL == MODEL_67) || (CPU_MODEL >= MODEL_V6)))
                        if ((skipinstr == 2) && ((CPU_MODEL == MODEL_67) || (CPU_MODEL == MODEL_V6)))
#endif
                            PSD1 = (PSD1 + 4) | (((PSD1 & 2) >> 1) & 1);
                        else
                            PSD1 = (PSD1 + 2) | (((PSD1 & 2) >> 1) & 1);
                    } else {
                        PSD1 = (PSD1 + 4) | (((PSD1 & 2) >> 1) & 1);
                    }
                } else {
                    EXM_EXR = 0;                    /* reset PC increment for EXR */
#ifdef DO_NEW_NOP
                    drop_nop = 0;
#endif
                }
                /* do not update pc for page fault */
            case DEMANDPG_TRAP:                     /* 0xC4 Demand Page Fault Trap (V6&V9 Only) */
                if (TRAPME == DEMANDPG_TRAP) {      /* 0xC4 Demand Page Fault Trap (V6&V9 Only) */
                    /* Set map number */
                    /* pfault will have 11 bit page number and bit 0 set if op fetch */
        fprintf(stderr, "##PAGEFAULT TRAPS %x page# %x LOAD MAPS PSD1 %x PSD2 %x CPUSTATUS %x\r\n",
                TRAPME, pfault, PSD1, PSD2, CPUSTATUS);
                }

#ifdef DO_NEW_NOP
                sim_debug(DEBUG_EXP, &cpu_dev, "TRAP PSD1 %x PSD2 %x CPUSTATUS %x drop_nop %x\n",
                        PSD1, PSD2, CPUSTATUS, drop_nop);
#else
                sim_debug(DEBUG_EXP, &cpu_dev, "TRAP PSD1 %x PSD2 %x CPUSTATUS %x skipinstr %x\n",
                        PSD1, PSD2, CPUSTATUS, skipinstr);
#endif
#ifdef TRME     /* set to 1 for traceme to work */
    if (traceme >= trstart) {
#ifdef DO_NEW_NOP
    fprintf(stderr, "At TRAP %x IR %x PSD1 %x PSD2 %x CPUSTATUS %x ovr %d drop_nop %x\r\n",
            TRAPME, IR, PSD1, PSD2, CPUSTATUS, ovr, drop_nop);
#else
    fprintf(stderr, "At TRAP %x IR %x PSD1 %x PSD2 %x CPUSTATUS %x ovr %d skipinstr %x\r\n",
            TRAPME, IR, PSD1, PSD2, CPUSTATUS, ovr, skipinstr);
#endif
    fprintf(stderr, "tvl %.8x, tta %.8x status %.8x\r\n", tvl, tta, CPUSTATUS);
    }
#endif
                tta = tta + (TRAPME - 0x80);        /* tta has mem addr of trap vector */
                if (modes & BASEBIT)
                    tvl = M[tta>>2] & 0xFFFFFC;     /* get 24 bit trap vector address from trap vector loc */
                else
                    tvl = M[tta>>2] & 0x7FFFC;      /* get 19 bit trap vector address from trap vector loc */
//                if (tvl == 0 || tvl == 0x7FFFC || (CPUSTATUS & 0x40) == 0) {
                if (tvl == 0 || (CPUSTATUS & 0x40) == 0) {
                    /* vector is zero or software has not enabled traps yet */
                    /* execute a trap halt */
                    /* set the PSD to trap vector location */
                    PSD1 = 0x80000000 + TRAPME;     /* just priv and PC to trap vector */
                    PSD2 = 0x00004000;              /* unmapped, blocked interrupts mode */
                    M[0x680>>2] = PSD1;             /* store PSD 1 */
                    M[0x684>>2] = PSD2;             /* store PSD 2 */
                    M[0x688>>2] = TRAPSTATUS;       /* store trap status */
                    M[0x68C>>2] = 0;                /* This will be device table entry later TODO */
                    fprintf(stderr, "[][][][][][][][][][] HALT TRAP [][][][][][][][][][]\r\n");
                    fprintf(stderr, "PSD1 %.8x PSD2 %.8x TRAPME %.4x\r\n", PSD1, PSD2, TRAPME);
                    for (ix=0; ix<8; ix+=2) {
                        fprintf(stderr, "GPR[%d] %.8x GPR[%d] %.8x\r\n", ix, GPR[ix], ix+1, GPR[ix+1]);
                    }
                    if (modes & BASEBIT) {
                    for (ix=0; ix<8; ix+=2) {
                        fprintf(stderr, "BR[%d] %.8x BR[%d] %.8x\r\n", ix, BR[ix], ix+1, BR[ix+1]);
                    }
                    }
                    fprintf(stderr, "[][][][][][][][][][] HALT TRAP [][][][][][][][][][]\r\n");
                    return STOP_HALT;               /* exit to simh for halt */
                } else {
                    /* valid vector, so store the PSD, fetch new PSD */
                    bc = PSD2 & 0x3ffc;             /* get copy of cpix */
//DIAG                    M[tvl>>2] = PSD1 & 0xfffffffe;  /* store PSD 1 */
                    if ((TRAPME == PRIVHALT_TRAP) && (CPU_MODEL <= MODEL_27))
                        /* Privlege Mode Halt Trap on 27 has bit 31 reset */
                        M[tvl>>2] = PSD1 & 0xfffffffe;  /* store PSD 1 */
                    else
                        M[tvl>>2] = PSD1 & 0xffffffff;  /* store PSD 1 */
                    M[(tvl>>2)+1] = PSD2;           /* store PSD 2 */
                    PSD1 = M[(tvl>>2)+2];           /* get new PSD 1 */
                    PSD2 = (M[(tvl>>2)+3] & ~0x3ffc) | bc;  /* get new PSD 2 w/old cpix */
                    M[(tvl>>2)+4] = TRAPSTATUS;     /* store trap status */
                    if (TRAPME == DEMANDPG_TRAP)    /* 0xC4 Demand Page Fault Trap (V6&V9 Only) */
                        M[(tvl>>2)+5] = pfault;     /* store page fault number */

                    /* set the mode bits and CCs from the new PSD */
                    CC = PSD1 & 0x78000000;         /* extract bits 1-4 from PSD1 */
                    modes = PSD1 & 0x87000000;      /* extract bits 0, 5, 6, 7 from PSD 1 */
                    /* set new map mode and interrupt blocking state in CPUSTATUS */
                    if (PSD2 & MAPBIT) {
                        CPUSTATUS |= 0x00800000;    /* set bit 8 of cpu status */
                        modes |= MAPMODE;           /* set mapped mode */
                    } else
                        CPUSTATUS &= 0xff7fffff;    /* reset bit 8 of cpu status */
                    /* set interrupt blocking state */
                    if ((PSD2 & 0x8000) == 0) {     /* is it retain blocking state */
                        if (PSD2 & 0x4000)          /* no, is it set blocking state */
                            CPUSTATUS |= 0x80;      /* yes, set blk state in cpu status bit 24 */
                        else
                            CPUSTATUS &= ~0x80;     /* no, reset blk state in cpu status bit 24 */
                    }
                    PSD2 &= ~0x0000c000;            /* clear bit 48 & 49 to be unblocked */
                    if (CPUSTATUS & 0x80)           /* see if old mode is blocked */
                        PSD2 |= 0x00004000;         /* set to blocked state */

                    PSD2 &= ~RETMBIT;               /* turn off retain bit in PSD2 */
                    SPAD[0xf5] = PSD2;              /* save the current PSD2 */
                    /* TODO provide page fault data to word 6 */
                    if (TRAPME == DEMANDPG_TRAP) {  /* 0xC4 Demand Page Fault Trap (V6&V9 Only) */
                        /* Set map number */
                        /* pfault will have 11 bit page number and bit 0 set if op fetch */
        fprintf(stderr, "PAGE TRAP %x TSTATUS %x LOAD MAPS PSD1 %x PSD2 %x CPUSTATUS %x pfault %x\r\n",
                TRAPME, TRAPSTATUS, PSD1, PSD2, CPUSTATUS, pfault);
                    }

#ifdef NOTNOW
    if ((TRAPME == DEMANDPG_TRAP) || 
        (TRAPME == UNDEFINSTR_TRAP) || (TRAPME == MAPFAULT_TRAP)) {
        fprintf(stderr, "TRAPS %x TSTATUS %x LOAD MAPS PSD1 %x PSD2 %x CPUSTATUS %x\r\n",
                TRAPME, TRAPSTATUS, PSD1, PSD2, CPUSTATUS);
        goto dumpi;
    }
#endif
#ifdef TRME     /* set to 1 for traceme to work */
    if (TRAPME == PRIVVIOL_TRAP || TRAPME == SYSTEMCHK_TRAP) {
//  if (TRAPME == UNDEFINSTR_TRAP || TRAPME == MAPFAULT_TRAP) {
//  if (TRAPME == MAPFAULT_TRAP) {
//  if (TRAPME == ADDRSPEC_TRAP) {
/// if (TRAPME == AEXPCEPT_TRAP) {
//  if (TRAPME == PRIVVIOL_TRAP) {
traceme = trstart;
        sim_debug(DEBUG_EXP, &cpu_dev, "TRAP PSD1 %x PSD2 %x CPUSTATUS %x\r\n", PSD1, PSD2, CPUSTATUS);
        fprintf(stderr, "TRAPS %x LOAD MAPS PSD1 %x PSD2 %x CPUSTATUS %x\r\n", TRAPME, PSD1, PSD2, CPUSTATUS);
        goto dumpi;
    }
#endif
                    break;                  /* Go execute the trap */
                }
                break;
            }
        }
        skipinstr = 1;                      /* skip next instruction */
        /* we have a new PSD loaded via a LPSD or LPSDCM */
        /* TODO finish instruction history, then continue */
        /* update cpu status word too */
//DIAG        OPSD1 &= 0x87FFFFFE;                /* clear the old CC's */
        OPSD1 &= 0x87FFFFFF;                /* clear the old CC's */
        OPSD1 |= PSD1 & 0x78000000;         /* update the CC's in the PSD */
        /* TODO Update other history information for this instruction */
        if (hst_lnt) {
            hst[hst_p].opsd1 = OPSD1;       /* update the CC in opsd1 */
            hst[hst_p].npsd1 = PSD1;        /* save new psd1 */
            hst[hst_p].npsd2 = PSD2;        /* save new psd2 */
            hst[hst_p].modes = modes;       /* save current mode bits */
            for (ix=0; ix<8; ix++) {
                hst[hst_p].reg[ix] = GPR[ix];   /* save reg */
                hst[hst_p].reg[ix+8] = BR[ix];  /* save breg */
            }
        }

#ifdef TRME     /* set to 1 for traceme to work */
    if (traceme >= trstart) {
dumpi:
//DIAG        OPSD1 &= 0x87FFFFFE;                /* clear the old CC's */
        OPSD1 &= 0x87FFFFFF;                /* clear the old CC's */
        OPSD1 |= PSD1 & 0x78000000;         /* update the CC's in the PSD */
        if (modes & MAPMODE)
            fprintf(stderr, "M%.8x %.8x ", OPSD1, OIR);
        else
            fprintf(stderr, "U%.8x %.8x ", OPSD1, OIR);
        fprint_inst(stderr, OIR, 0);        /* display instruction */
        fprintf(stderr, "\r\n");
        fprintf(stderr, "\tR0=%.8x R1=%.8x R2=%.8x R3=%.8x", GPR[0], GPR[1], GPR[2], GPR[3]);
        fprintf(stderr, " R4=%.8x R5=%.8x R6=%.8x R7=%.8x", GPR[4], GPR[5], GPR[6], GPR[7]);
        fprintf(stderr, "\r\n");
        if (modes & BASEBIT) {
        fprintf(stderr, "\tB0=%.8x B1=%.8x B2=%.8x B3=%.8x", BR[0], BR[1], BR[2], BR[3]);
        fprintf(stderr, " B4=%.8x B5=%.8x B6=%.8x B7=%.8x", BR[4], BR[5], BR[6], BR[7]);
        fprintf(stderr, "\r\n");
        }
        fflush(stderr);
#if 0
        fprintf(stderr, "Current MAPC PSD2 %x modes %x\r\n", PSD2, modes);
        for (ix=0; ix<16; ix++) {
            fprintf(stderr, "MAP %x MPC %x\r\n", ix/2, MAPC[ix]);
        }
//      fflush(stderr);
#endif
//  if (TRAPME == UNDEFINSTR_TRAP || TRAPME == MAPFAULT_TRAP)
    if (TRAPME == MAPFAULT_TRAP)
        return STOP_HALT;                   /* exit to simh for halt */
    }
#endif
sim_debug(DEBUG_DATA, &cpu_dev, "R0=%08x R1=%08x R2=%08x R3=%08x\n", GPR[0], GPR[1], GPR[2], GPR[3]);
sim_debug(DEBUG_DATA, &cpu_dev, "R4=%08x R5=%08x R6=%08x R7=%08x\n", GPR[4], GPR[5], GPR[6], GPR[7]);
        continue;                           /* single step cpu just for now */
//      break;                              /* quit for now after each instruction */
    }   /* end while */

    /* Simulation halted */
    return reason;
}

/* these are the default ipl devices defined by the CPU jumpers */
/* they can be overridden by specifying IPL device at ipl time */
uint32 def_disk = 0x0800;               /* disk channel 8, device 0 */
uint32 def_tape = 0x1000;               /* tape device 10, device 0 */
uint32 def_floppy = 0x7ef0;             /* IOP floppy disk channel 7e, device f0 */

/* Reset routine */
/* do any one time initialization here for cpu */
t_stat cpu_reset(DEVICE * dptr)
{
    int i;

    /* leave regs alone so values can be passed to boot code */
    PSD1 = 0x80000000;                  /* privileged, non mapped, non extended, address 0 */
    PSD2 = 0x00004000;                  /* blocked interrupts mode */
    modes = (PRIVBIT | BLKMODE);        /* set modes to privileged and blocked interrupts */
    CC = 0;                             /* no CCs too */
    CPUSTATUS = CPU_MODEL;              /* clear all cpu status except cpu type */
    CPUSTATUS |= 0x80000000;            /* set privleged state bit 0 */
    CPUSTATUS |= 0x00000080;            /* set blocked mode state bit 24 */
    TRAPSTATUS = CPU_MODEL;             /* clear all trap status except cpu type */
    CMCR = 0;                           /* No Cache Enabled */
    SMCR = 0;                           /* No Shared Memory Enabled */
    CCW = 0;                            /* No Computer Configuration Enabled */

    chan_set_devs();                    /* set up the defined devices on the simulator */

    /* set default breaks to execution tracing */
    sim_brk_types = sim_brk_dflt = SWMASK('E');
    /* zero regs */
    for (i = 0; i < 8; i++) {
        GPR[i] = 0;                     /* clear the registers */
        BR[i] = 0;                      /* clear the registers */
    }
    GPR[7] = 0x40;                      /* set RE_VERBOSE bit for UTX tape boot */
    /* zero interrupt status words */
    for (i = 0; i < 112; i++)
        INTS[i] = 0;                    /* clear interrupt status flags */

    /* add code here to initialize the SEL32 cpu scratchpad on initial start */
    /* see if spad setup by software, if yes, leave spad alone */
    /* otherwise set the default values into the spad */
    /* CPU key is 0xECDAB897, IPU key is 0x13254768 */
    /* Keys are loaded by the O/S software during the boot loading sequence */
    if (SPAD[0xf7] != 0xecdab897)
    {
        int ival = 0;                   /* init value for concept 32 */

        if (CPU_MODEL < MODEL_27)
            ival = 0xfffffff;           /* init value for 32/7x int and dev entries */
        for (i = 0; i < 1024; i++)
            MAPC[i] = 0;                /* clear 2048 halfword map cache */
        for (i = 0; i < 224; i++)
            SPAD[i] = ival;             /* init 128 devices and 96 ints in the spad */
        for (i = 224; i < 256; i++)     /* clear the last 32 extries */
            SPAD[i] = 0;                /* clear the spad */
        SPAD[0xf0] = 0x80;              /* default Trap Table Address (TTA) */
        SPAD[0xf1] = 0x100;             /* Interrupt Table Address (ITA) */
        SPAD[0Xf2] = 0x700;             /* IOCD Base Address */
        SPAD[0xf3] = 0x788;             /* Master Process List (MPL) table address */
        SPAD[0xf4] = def_tape;          /* Default IPL address from console IPL command or jumper */
        SPAD[0xf5] = 0x00004000;        /* current PSD2 defaults to blocked */
        SPAD[0xf6] = 0;                 /* reserved (PSD1 ??) */
        SPAD[0xf7] = 0;                 /* make sure key is zero */
        SPAD[0xf8] = 0x0000f000;        /* set DRT to class f (anything else is E) */
        SPAD[0xf9] = CPU_MODEL;         /* set default cpu type in cpu status word */
        SPAD[0xff] = 0x00ffffff;        /* interrupt level 7f 1's complament */
    }
    /* set low memory bootstrap code */
    M[0] = 0x02000000;                  /* 0x00 IOCD 1 read into address 0 */
    M[1] = 0x60000078;                  /* 0x04 IOCD 1 CMD Chain, Suppress incor length, 120 bytes */
    M[2] = 0x53000000;                  /* 0x08 IOCD 2 BKSR or RZR to re-read boot code */
    M[3] = 0x60000001;                  /* 0x0C IOCD 2 CMD chain,Supress incor length, 1 byte */
    M[4] = 0x02000000;                  /* 0x10 IOCD 3 Read into address 0 */
    M[5] = 0x000006EC;                  /* 0x14 IOCD 3 Read 0x6EC bytes */
    loading = 0;                        /* not loading yet */
    /* we are good to go */
    return SCPE_OK;
}

/* Memory examine */
/* examine a 32bit memory location and return a byte */
t_stat cpu_ex(t_value *vptr, t_addr baddr, UNIT *uptr, int32 sw)
{
    uint32 addr = (baddr & 0xfffffc) >> 2;  /* make 24 bit byte address into word address */

    /* MSIZE is in 32 bit words */
    if (addr >= MEMSIZE)                /* see if address is within our memory */
        return SCPE_NXM;                /* no, none existant memory error */
    if (vptr == NULL)                   /* any address specified by user */
        return SCPE_OK;                 /* no, just ignore the request */
    *vptr = (M[addr] >> (8 * (3 - (baddr & 0x3))));  /* return memory contents */
    return SCPE_OK;                     /* we are all ok */
}

/* Memory deposit */
/* modify a byte specified by a 32bit memory location */
/* address is byte address with bits 30,31 = 0 */
t_stat cpu_dep(t_value val, t_addr baddr, UNIT *uptr, int32 sw)
{
    uint32 addr = (baddr & 0xfffffc) >> 2;  /* make 24 bit byte address into word address */
    static const uint32 bmasks[4] = {0x00FFFFFF, 0xFF00FFFF, 0xFFFF00FF, 0xFFFFFF00};

    /* MSIZE is in 32 bit words */
    if (addr >= MEMSIZE)                /* see if address is within our memory */
        return SCPE_NXM;                /* no, none existant memory error */
    val = (M[addr] & bmasks[baddr & 0x3]) | (val << (8 * (3 - (baddr & 0x3))));
    M[addr] = val;                      /* set new value */
    return SCPE_OK;                     /* all OK */
}

/* set the CPU memory size */
/* table values are in words, not bytes */
uint32 memwds [] = {
    0x008000,   /* size index 0 - 128KB =  32KW */
    0x010000,   /*            1 - 256KB =  64KW */
    0x020000,   /*            2 - 512KB = 128KW */
    0x040000,   /*            3 -   1MB = 256KW */
    0x080000,   /*            4 -   2MB = 512KW */
    0x0c0000,   /*            5 -   3MB = 768KW */
    0x100000,   /*            6 -   4MB =   1MW */
    0x180000,   /*            7 -   6MB = 1.5MW */
    0x200000,   /*            8 -   8MB =   2MW */
    0x300000,   /*            9 -  12MB =   3MW */
    0x400000,   /*           10 -  16MB =   4MW */
};

t_stat cpu_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_uint64            mc = 0;
    uint32              i;

    cpu_unit.flags &= ~UNIT_MSIZE;      /* clear old size value 0-31 */
    cpu_unit.flags |= val;              /* set new memory size index value (0-31) */
    val >>= UNIT_V_MSIZE;               /* shift index right 19 bits */
    val = memwds[val];                  /* (128KB/4) << index == memory size in KW */
    if ((val < 0) || (val > MAXMEMSIZE))    /* is size valid */
        return SCPE_ARG;                /* nope, argument error */
    for (i = val; i < MEMSIZE; i++)     /* see if memory contains anything */
        mc |= M[i];                     /* or in any bits in memory */
    if ((mc != 0) && (!get_yn("Really truncate memory [N]?", FALSE)))
        return SCPE_OK;                 /* return OK if user says no */
    MEMSIZE = val;                      /* set new size in words */
    for (i = MEMSIZE; i < MAXMEMSIZE; i++)
        M[i] = 0;                       /* zero all of the new memory */
    return SCPE_OK;                     /* we done */
}

/* Handle execute history */

/* Set history */
t_stat
cpu_set_hist(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32               i, lnt;
    t_stat              r;

    if (cptr == NULL) {                 /* check for any user options */
        for (i = 0; i < hst_lnt; i++)   /* none, so just zero the history */
            hst[i].opsd1 = 0;           /* just psd1 for now */
        hst_p = 0;                      /* start at the beginning */
        return SCPE_OK;                 /* all OK */
    }
    /* the user has specified options, process them */
    lnt = (int32)get_uint(cptr, 10, HIST_MAX, &r);
    if ((r != SCPE_OK) || (lnt && (lnt < HIST_MIN)))
        return SCPE_ARG;                /* arg error for bad input or too small a value */
    hst_p = 0;                          /* start at beginning */
    if (hst_lnt) {                      /* if a new length was input, resize history buffer */
        free(hst);                      /* out with the old */
        hst_lnt = 0;                    /* no length anymore */
        hst = NULL;                     /* and no pointer either */
    }
    if (lnt) {                          /* see if new size specified, if so get new resized buffer */
        hst = (struct InstHistory *)calloc(sizeof(struct InstHistory), lnt);
        if (hst == NULL)
            return SCPE_MEM;            /* allocation error, so tell user */
        hst_lnt = lnt;                  /* set new length */
    }
    return SCPE_OK;                     /* we are good to go */
}

/* Show history */
t_stat cpu_show_hist(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int32               k, di, lnt;
    char               *cptr = (char *) desc;
    t_stat              r;
    struct InstHistory *h;

    if (hst_lnt == 0)                   /* see if show history is enabled */
        return SCPE_NOFNC;              /* no, so are out of here */
    if (cptr) {                         /* see if user provided a display count */
        lnt = (int32)get_uint(cptr, 10, hst_lnt, &r);   /* get the count */
        if ((r != SCPE_OK) || (lnt == 0))   /* if error or 0 count */
            return SCPE_ARG;            /* report argument error */
    } else
        lnt = hst_lnt;                  /* dump all the entries */
    di = hst_p - lnt;                   /* work forward */
    if (di < 0)
        di = di + hst_lnt;              /* wrap */
    for (k = 0; k < lnt; k++) {         /* print specified entries */
        h = &hst[(++di) % hst_lnt];     /* entry pointer */
        /* display the instruction and results */
        if (h->modes & MAPMODE)
            fprintf(st, "M%.8x %.8x %.8x ", h->opsd1, h->npsd2, h->oir);
        else
            fprintf(st, "U%.8x %.8x %.8x ", h->opsd1, h->npsd2, h->oir);
        if (h->modes & BASEBIT)
            fprint_inst(st, h->oir, SWMASK('M')); /* display basemode instruction */
        else
            fprint_inst(st, h->oir, 0); /* display non basemode instruction */
        fprintf(st, "\n");
        fprintf(st, "\tR0=%.8x R1=%.8x R2=%.8x R3=%.8x", h->reg[0], h->reg[1], h->reg[2], h->reg[3]);
        fprintf(st, " R4=%.8x R5=%.8x R6=%.8x R7=%.8x", h->reg[4], h->reg[5], h->reg[6], h->reg[7]);
        if (h->modes & BASEBIT) {
            fprintf(st, "\n");
            fprintf(st, "\tB0=%.8x B1=%.8x B2=%.8x B3=%.8x", h->reg[8], h->reg[9], h->reg[10], h->reg[11]);
            fprintf(st, " B4=%.8x B5=%.8x B6=%.8x B7=%.8x", h->reg[12], h->reg[13], h->reg[14], h->reg[15]);
        }
        fprintf(st, "\n");
    }                                   /* end for */
    return SCPE_OK;                     /* all is good */
}

/* return description for the specified device */
const char *cpu_description (DEVICE *dptr) 
{
    return "SEL 32 CPU";                /* return description */
}

t_stat cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "The CPU can maintain a history of the most recently executed instructions.\n");
    fprintf(st, "This is controlled by the SET CPU HISTORY and SHOW CPU HISTORY commands:\n\n");
    fprintf(st, "   sim> SET CPU HISTORY                 clear history buffer\n");
    fprintf(st, "   sim> SET CPU HISTORY=0               disable history\n");
    fprintf(st, "   sim> SET CPU HISTORY=n{:file}        enable history, length = n\n");
    fprintf(st, "   sim> SHOW CPU HISTORY                print CPU history\n");
    return SCPE_OK;
}
