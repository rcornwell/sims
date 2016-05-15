/* ka10_cpu.c: PDP-10 CPU simulator

   Copyright (c) 2013, Richard Cornwell

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   cpu          KA10 central processor


   The 36b system family had six different implementions: PDP-6, KA10, KI10,
   L10, KL10 extended, and KS10. 

   The register state for the KS10 is:

   AC[16]                       accumulators
   PC                           program counter
   flags<0:11>                  state flags
   pi_enb<1:7>                  enabled PI levels
   pi_act<1:7>                  active PI levels
   pi_prq<1:7>                  program PI requests
   apr_enb<0:7>                 enabled system flags
   apr_flg<0:7>                 system flags

   The PDP-10 had just two instruction formats: memory reference
   and I/O.

    000000000 0111 1 1111 112222222222333333
    012345678 9012 3 4567 890123456789012345
   +---------+----+-+----+------------------+
   |  opcode | ac |i| idx|     address      | memory reference
   +---------+----+-+----+------------------+

    000 0000000 111 1 1111 112222222222333333
    012 3456789 012 3 4567 890123456789012345
   +---+-------+---+-+----+------------------+
   |111|device |iop|i| idx|     address      | I/O
   +---+-------+---+-+----+------------------+

   This routine is the instruction decode routine for the PDP-10.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until an abort occurs.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        MUUO instruction in executive mode
        pager error in interrupt sequence
        invalid vector table in interrupt sequence
        illegal instruction in interrupt sequence
        breakpoint encountered
        nested indirects exceeding limit
        nested XCT's exceeding limit
        I/O error in I/O simulator

   2. Interrupts.  PDP-10's have a seven level priority interrupt
      system.  Interrupt requests can come from internal sources,
      such as APR program requests, or external sources, such as
      I/O devices.  The requests are stored in pi_prq for program
      requests, pi_apr for other internal flags, and pi_ioq for
      I/O device flags.  Internal and device (but not program)
      interrupts must be enabled on a level by level basis.  When
      an interrupt is granted on a level, interrupts at that level
      and below are masked until the interrupt is dismissed.

      The I/O device interrupt system is taken from the PDP-11.
      int_req stores the interrupt requests for Unibus I/O devices.
      Routines in the Unibus adapter map requests in int_req to
      PDP-10 levels.  The Unibus adapter also calculates which
      device to get a vector from when a PDP-10 interrupt is granted.

   3. Arithmetic.  The PDP-10 is a 2's complement system.

   4. Adding I/O devices.  These modules must be modified:

        pdp10_defs.h    add device address and interrupt definitions
        pdp10_sys.c     add sim_devices table entry

   A note on ITS 1-proceed.  The simulator follows the implementation
   on the KS10, keeping 1-proceed as a side flag (its_1pr) rather than
   as flags<8>.  This simplifies the flag saving instructions, which
   don't have to clear flags<8> before saving it.  Instead, the page
   fail and interrupt code must restore flags<8> from its_1pr.  Unlike
   the KS10, the simulator will not lose the 1-proceed trap if the
   1-proceeded instructions clears 1-proceed.
*/

#include "ka10_defs.h"
#include "sim_timer.h"
#include <time.h>

#define HIST_PC         0x40000000
#define HIST_MIN        64
#define HIST_MAX        65536
#define TMR_RTC         1

#define UNIT_V_MSIZE    (UNIT_V_UF + 0)
#if KI10
#define UNIT_MSIZE      (0177 << UNIT_V_MSIZE)
#else
#define UNIT_MSIZE      (017 << UNIT_V_MSIZE)
#endif
#define UNIT_V_TWOSEG   (UNIT_V_MSIZE + 8)
#define UNIT_TWOSEG     (1 << UNIT_V_TWOSEG)

typedef struct {
    uint32      pc;
    uint32      ea;
    uint64      ir;
    uint64      ac;
    uint32      flags;
    uint64      mb;
    uint64      fmb;
    } InstHistory;
uint64  M[MAXMEMSIZE];
uint64  AR, MQ, BR, AD, MB, ARX, BRX;
uint32  AB, PC, IR, FLAGS, AC;
#if KI
uint64  FM[64];
uint64  BRX, ARX, ADX;
uint32  ub_ptr, eb_ptr;
uint8   fm_blk, fm_sel, small_user, user_addr_cmp, page_enable, reg_stack;
uint32  ac_stack, pag_reload, inout_fail;
#else
uint64  FM[16];
#define get_reg(reg)    FM[(reg) & 017]
#define set_reg(reg, value)     FM[(reg) & 017] = value;
#define fm_blk  0
#endif

int     BYF5;
int     uuo_cycle, sac_inh;
int     SC, SCAD, FE;
int     Pl, Ph, Rl, Rh, Pflag;
char    push_ovf, mem_prot, nxm_flag, clk_flg;
char    PIR, PIH, PIE, pi_enable, parity_irq;
char    dev_irq[128];
int     pi_pending;
int     pi_req, pi_enc;
int     apr_irq, clk_pri;
int     ov_irq,fov_irq,clk_en,clk_irq, xctf;
int     pi_restore, pi_hold;
t_stat  (*dev_tab[128])(uint32 dev, uint64 *data);
t_stat  rtc_srv(UNIT * uptr);
int32   rtc_tps = 60;
int32   tmxr_poll = 10000;


int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
InstHistory *hst = NULL;                                /* instruction history */

/* Forward and external declarations */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                     const char *cptr);
const char          *cpu_description (DEVICE *dptr);
void set_ac_display (uint64 *acbase);

t_bool build_dev_tab (void);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = { UDATA (&rtc_srv, UNIT_FIX|UNIT_TWOSEG, MAXMEMSIZE) };

REG cpu_reg[] = {
    { ORDATA (PC, PC, 18) },
    { ORDATA (FLAGS, FLAGS, 18) },
    { ORDATA (FM0, FM[00], 36) },                       /* addr in memory */
    { ORDATA (FM1, FM[01], 36) },                       /* modified at exit */
    { ORDATA (FM2, FM[02], 36) },                       /* to SCP */
    { ORDATA (FM3, FM[03], 36) },
    { ORDATA (FM4, FM[04], 36) },
    { ORDATA (FM5, FM[05], 36) },
    { ORDATA (FM6, FM[06], 36) },
    { ORDATA (FM7, FM[07], 36) },
    { ORDATA (FM10, FM[010], 36) },
    { ORDATA (FM11, FM[011], 36) },
    { ORDATA (FM12, FM[012], 36) },
    { ORDATA (FM13, FM[013], 36) },
    { ORDATA (FM14, FM[014], 36) },
    { ORDATA (FM15, FM[015], 36) },
    { ORDATA (FM16, FM[016], 36) },
    { ORDATA (FM17, FM[017], 36) },
    { ORDATA (PIENB, pi_enable, 7) },
    { BRDATA (REG, FM, 8, 36, 017) },
    { NULL }
    };

MTAB cpu_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
    { UNIT_MSIZE, 1, "16K", "16K", &cpu_set_size },
    { UNIT_MSIZE, 2, "32K", "32K", &cpu_set_size },
    { UNIT_MSIZE, 4, "64K", "64K", &cpu_set_size },
    { UNIT_MSIZE, 8, "128K", "128K", &cpu_set_size },
    { UNIT_MSIZE, 12, "196K", "196K", &cpu_set_size },
    { UNIT_MSIZE, 16, "256K", "256K", &cpu_set_size },
#if KI
    { UNIT_MSIZE, 32, "512K", "512K", &cpu_set_size },
    { UNIT_MSIZE, 64, "1024K", "1024K", &cpu_set_size },
    { UNIT_MSIZE, 128, "2048K", "2048K", &cpu_set_size },
#endif
#if !KI
    { UNIT_TWOSEG, 0, "ONESEG", "ONESEG", NULL, NULL, NULL},
    { UNIT_TWOSEG, UNIT_TWOSEG, "TWOSEG", "TWOSEG", NULL, NULL, NULL},
#endif
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 18, 1, 8, 36,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL, NULL, 0, 0, NULL,
    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description
    };

/* Data arrays */
#define FCE     00001   /* Fetch memory into AR */
#define FCEPSE  00002   /* Fetch and store memory into AR */
#define SCE     00004   /* Save AR into memory */
#define FAC     00010   /* Fetch AC into AR */
#define FAC2    00020   /* Fetch AC+1 into MQ */
#define FALT    00040   /* Not used */
#define SAC     00100   /* Save AC into AR */
#define SACZ    00200   /* Save AC into AR if AC not 0 */
#define SAC2    00400   /* Save MQ into AC+1 */
#define MBR     01000   /* Load Mem to BR, AC to AR */
#define SWAR    02000   /* Swap AR */

int opflags[] = {
        /* UUO00 */     /* LUUO01 */    /* LUUO02 */    /* LUUO03 */
        0,              0,              0,              0,
        /* LUUO04 */    /* LUUO05 */    /* LUUO06 */    /* LUUO07 */
        0,              0,              0,              0,
        /* LUUO10 */    /* LUUO11 */    /* LUUO12 */    /* LUUO13 */
        0,              0,              0,              0,
        /* LUUO14 */    /* LUUO15 */    /* LUUO16 */    /* LUUO17 */
        0,              0,              0,              0,
        /* LUUO20 */    /* LUUO21 */    /* LUUO22 */    /* LUUO23 */
        0,              0,              0,              0,
        /* LUUO24 */    /* LUUO25 */    /* LUUO26 */    /* LUUO27 */
        0,              0,              0,              0,
        /* LUUO30 */    /* LUUO31 */    /* LUUO32 */    /* LUUO33 */
        0,              0,              0,              0,
        /* LUUO34 */    /* LUUO35 */    /* LUUO36 */    /* LUUO37 */
        0,              0,              0,              0,
        /* MUUO40 */    /* MUUO41 */    /* MUUO42 */    /* MUUO43 */
        0,              0,              0,              0,
        /* MUUO44 */    /* MUUO45 */    /* MUUO46 */    /* MUUO47 */
        0,              0,              0,              0,
        /* MUUO50 */    /* MUUO51 */    /* MUUO52 */    /* MUUO53 */    
        0,              0,              0,              0,
        /* MUUO54 */    /* MUUO55 */    /* MUUO56 */    /* MUUO57 */
        0,              0,              0,              0,
        /* MUUO60 */    /* MUUO61 */    /* MUUO62 */    /* MUUO63 */
        0,              0,              0,              0,
        /* MUUO64 */    /* MUUO65 */    /* MUUO66 */    /* MUUO67 */
        0,              0,              0,              0,
        /* MUUO70 */    /* MUUO71 */    /* MUUO72 */    /* MUUO73 */
        0,              0,              0,              0,
        /* MUUO74 */    /* MUUO75 */    /* MUUO76 */    /* MUUO77 */
        0,              0,              0,              0,
        /* UJEN */      /* UUO101 */    /* GFAD */      /* GFSB */
        0,              0,              0,              0,
        /* JSYS */      /* ADJSP */     /* GFMP */      /*GFDV */       
        0,              0,              0,              0,
        /* DFAD */      /* DFSB */      /* DFMP */      /* DFDV */
        0,              0,              0,              0,
        /* DADD */      /* DSUB */      /* DMUL */      /* DDIV */
        0,              0,              0,              0,
#if KI
        /* DMOVE */     /* DMOVN */     /* FIX */       /* EXTEND */
        FCE|SAC|SAC2,   FCE|SAC|SAC2,   FCE|SAC,        0,
        /* DMOVEM */    /* DMOVNM */    /* FIXR */      /* FLTR */
        0,              0,              FCE|SAC,        FCE,
#else
        /* DMOVE */     /* DMOVN */     /* FIX */       /* EXTEND */
        0,              0,              0,              0,
        /* DMOVEM */    /* DMOVNM */    /* FIXR */      /* FLTR */
        0,              0,              0,              0,
#endif
        /* UFA */       /* DFN */       /* FSC */       /* IBP */       
        FCE,            FCE|MBR,        FAC|SAC,        FCEPSE,
        /* ILDB */      /* LDB */       /* IDPB */      /* DPB */
        FCEPSE,         FCE,            FCEPSE,         FCE,
        /* FAD */       /* FADL */      /* FADM */      /* FADB */
        SAC|FCE,        SAC2|SAC|FCE,   FCEPSE,         SAC|FCEPSE,     
        /* FADR */      /* FADRI */     /* FADRM */     /* FADRB */
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* FSB */       /* FSBL */      /* FSBM */      /* FSBB */
        SAC|FCE,        SAC2|SAC|FCE,   FCEPSE,         SAC|FCEPSE,             
        /* FSBR */      /* FSBRI */     /* FSBRM */     /* FSBRB */
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* FMP */       /* FMPL */      /* FMPM */      /* FMPB */      
        SAC|FCE,        SAC2|SAC|FCE,   FCEPSE,         SAC|FCEPSE,     
        /* FMPR */      /* FMPRI */     /* FMPRM */     /* FMPRB */
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* FDV */       /* FDVL */      /* FDVM */      /* FDVB */
        SAC|FCE,        SAC2|SAC|FCE,   FCEPSE,         SAC|FCEPSE,     
        /* FDVR */      /* FDVRI */     /* FDVRM */     /* FDVRB */
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,

        /* MOVE */      /* MOVEI */     /* MOVEM */     /* MOVES */     
        SAC|FCE,        SAC|0,          FAC|SCE,        SACZ|FCEPSE,
        /* MOVS */      /* MOVSI */     /* MOVSM */     /* MOVSS */
        SAC|FCE,        SAC|0,          FAC|SCE,        SACZ|FCEPSE,
        /* MOVN */      /* MOVNI */     /* MOVNM */     /* MOVNS */
        SAC|FCE,        SAC|0,          FAC|SCE,        SACZ|FCEPSE,
        /* MOVM */      /* MOVMI */     /* MOVMM */     /* MOVMS */
        SAC|FCE,        SAC|0,          FAC|SCE,        SACZ|FCEPSE,
        /* IMUL */      /* IMULI */     /* IMULM */     /* IMULB */     
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* MUL */       /* MULI */      /* MULM */      /* MULB */
        SAC2|SAC|FCE,   SAC2|SAC|0,     FCEPSE,         SAC2|SAC|FCEPSE,
        /* IDIV */      /* IDIVI */     /* IDIVM */     /* IDIVB */
        SAC2|SAC|FCE|MBR,SAC2|SAC|MBR,  FCEPSE|MBR,     SAC2|SAC|FCEPSE|MBR,
        /* DIV */       /* DIVI */      /* DIVM */      /* DIVB */
        SAC2|SAC|FCE|MBR,SAC2|SAC|MBR,  FCEPSE|MBR,     SAC2|SAC|FCEPSE|MBR,
        /* ASH */       /* ROT */       /* LSH */       /* JFFO */      
        SAC,            SAC,            SAC,            FAC,    
        /* ASHC */      /* ROTC */      /* LSHC */      /* UUO247 */
        SAC|SAC2|FAC2,  SAC|SAC2|FAC2,  SAC|SAC2|FAC2,  0,

        /* EXCH */      /* BLT */       /* AOBJP */     /* AOBJN */     
        FCEPSE,         0,              0,              0,
        /* JRST */      /* JFCL */      /* XCT */       /* MAP */       
        0,              0,              0,              0,
        /* PUSHJ */     /* PUSH */      /* POP */       /* POPJ */
        0,              FCE,            0,              0,
        /* JSR */       /* JSP */       /* JSA */       /* JRA */
        SCE,            SAC,            SCE,            0,
        /* ADD */       /* ADDI */      /* ADDM */      /* ADDB */      
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,     
        /* SUB */       /* SUBI */      /* SUBM */      /* SUBB */
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        
        /* CAI */       /* CAIL */      /* CAIE */      /* CAILE */     
        0,              0,              0,              0,
        /* CAIA */      /* CAIGE */     /* CAIN */      /* CAIG */
        0,              0,              0,              0,
        /* CAM */       /* CAML */      /* CAME */      /* CAMLE */     
        FCE,            FCE,            FCE,            FCE,    
        /* CAMA */      /* CAMGE */     /* CAMN */      /* CAMG */
        FCE,            FCE,            FCE,            FCE,
        /* JUMP */      /* JUMPL */     /* JUMPE */     /* JUMPLE */
        FAC,            FAC,            FAC,            FAC,
        /* JUMPA */     /* JUMPGE */    /* JUMPN */     /* JUMPG */
        FAC,            FAC,            FAC,            FAC,
        /* SKIP */      /* SKIPL */     /* SKIPE */     /* SKIPLE */    
        SACZ|FCE,       SACZ|FCE,       SACZ|FCE,       SACZ|FCE,
        /* SKIPA */     /* SKIPGE */    /* SKIPN */     /* SKIPG */
        SACZ|FCE,       SACZ|FCE,       SACZ|FCE,       SACZ|FCE,
        /* AOJ */       /* AOJL */      /* AOJE */      /* AOJLE */     
        SAC|FAC,        SAC|FAC,        SAC|FAC,        SAC|FAC,
        /* AOJA */      /* AOJGE */     /* AOJN */      /* AOJG */
        SAC|FAC,        SAC|FAC,        SAC|FAC,        SAC|FAC,
        /* AOS */       /* AOSL */      /* AOSE */      /* AOSLE */     
        SACZ|FCEPSE,    SACZ|FCEPSE,    SACZ|FCEPSE,    SACZ|FCEPSE,
        /* AOSA */      /* AOSGE */     /* AOSN */      /* AOSG */
        SACZ|FCEPSE,    SACZ|FCEPSE,    SACZ|FCEPSE,    SACZ|FCEPSE,
        /* SOJ */       /* SOJL */      /* SOJE */      /* SOJLE */     
        SAC|FAC,        SAC|FAC,        SAC|FAC,        SAC|FAC,
        /* SOJA */      /* SOJGE */     /* SOJN */      /* SOJG */
        SAC|FAC,        SAC|FAC,        SAC|FAC,        SAC|FAC,
        /* SOS */       /* SOSL */      /* SOSE */      /* SOSLE */
        SACZ|FCEPSE,    SACZ|FCEPSE,    SACZ|FCEPSE,    SACZ|FCEPSE,
        /* SOSA */      /* SOSGE */     /* SOSN */      /* SOSG */
        SACZ|FCEPSE,    SACZ|FCEPSE,    SACZ|FCEPSE,    SACZ|FCEPSE,
        
        /* SETZ */      /* SETZI */     /* SETZM */     /* SETZB */
        SAC,            SAC|0,          SCE,            SAC|SCE,
        /* AND */       /* ANDI */      /* ANDM */      /* ANDB */      
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* ANDCA */     /* ANDCAI */    /* ANDCAM */    /* ANDCAB */
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* SETM */      /* SETMI */     /* SETMM */     /* SETMB */
        SAC|FCE,        SAC,            0,              SAC|FCE,
        /* ANDCM */     /* ANDCMI */    /* ANDCMM */    /* ANDCMB */
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* SETA */      /* SETAI */     /* SETAM */     /* SETAB */
        SAC|0,          SAC|0,          SCE,            SAC|SCE,
        /* XOR */       /* XORI */      /* XORM */      /* XORB */
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* IOR */       /* IORI */      /* IORM */      /* IORB */              
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* ANDCB */     /* ANDCBI */    /* ANDCBM */    /* ANDCBB */
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* EQV */       /* EQVI */      /* EQVM */      /* EQVB */      
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* SETCA */     /* SETCAI */    /* SETCAM */    /* SETCAB */
        SAC|0,          SAC|0,          SCE,            SAC|SCE,
        /* ORCA */      /* ORCAI */     /* ORCAM */     /* ORCAB */
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* SETCM */     /* SETCMI */    /* SETCMM */    /* SETCMB */
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,     
        /* ORCM */      /* ORCMI */     /* ORCMM */     /* ORCMB */
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* ORCB */      /* ORCBI */     /* ORCBM */     /* ORCBB */     
        SAC|FCE,        SAC|0,          FCEPSE,         SAC|FCEPSE,
        /* SETO */      /* SETOI */     /* SETOM */     /* SETOB */
        SAC|0,          SAC|0,          SCE,            SAC|SCE,

        /* HLL */       /* HLLI */      /* HLLM */      /* HLLS */      
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HRL */       /* HRLI */      /* HRLM */      /* HRLS */      
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HLLZ */      /* HLLZI */     /* HLLZM */     /* HLLZS */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HRLZ */      /* HRLZI */     /* HRLZM */     /* HRLZS */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HLLO */      /* HLLOI */     /* HLLOM */     /* HLLOS */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HRLO */      /* HRLOI */     /* HRLOM */     /* HRLOS */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HLLE */      /* HLLEI */     /* HLLEM */     /* HLLES */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HRLE */      /* HRLEI */     /* HRLEM */     /* HRLES */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HRR */       /* HRRI */      /* HRRM */      /* HRRS */      
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HLR */       /* HLRI */      /* HLRM */      /* HLRS */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HRRZ */      /* HRRZI */     /* HRRZM */     /* HRRZS */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HLRZ */      /* HLRZI */     /* HLRZM */     /* HLRZS */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HRRO */      /* HRROI */     /* HRROM */     /* HRROS */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HLRO */      /* HLROI */     /* HLROM */     /* HLROS */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HRRE */      /* HRREI */     /* HRREM */     /* HRRES */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        /* HLRE */      /* HLREI */     /* HLREM */     /* HLRES */
        SAC|FCE,        SAC|0,          FCEPSE,         SACZ|FCEPSE,
        
        /* TRN */       /* TLN */       /* TRNE */      /* TLNE */
        0,              0,              0,              0,
        /* TRNA */      /* TLNA */      /* TRNN */      /* TLNN */
        0,              0,              0,              0,
        /* TDN */       /* TSN */       /* TDNE */      /* TSNE */
        FCE,            FCE,            FCE,            FCE,    
        /* TDNA */      /* TSNA */      /* TDNN */      /* TSNN */
        FCE,            FCE,            FCE,            FCE,
        /* TRZ */       /* TLZ */       /* TRZE */      /* TLZE */
        0,              0,              0,              0,
        /* TRZA */      /* TLZA */      /* TRZN */      /* TLZN */
        0,              0,              0,              0,
        /* TDZ */       /* TSZ */       /* TDZE */      /* TSZE */
        FCE,            FCE,            FCE,            FCE,    
        /* TDZA */      /* TSZA */      /* TDZN */      /* TSZN */
        FCE,            FCE,            FCE,            FCE,
        /* TRC */       /* TLC */       /* TRCE */      /* TLCE */
        0,              0,              0,              0,
        /* TRCA */      /* TLCA */      /* TRCN */      /* TLCN */
        0,              0,              0,              0,
        /* TDC */       /* TSC */       /* TDCE */      /* TSCE */
        FCE,            FCE,            FCE,            FCE,
        /* TDCA */      /* TSCA */      /* TDCN */      /* TSCN */
        FCE,            FCE,            FCE,            FCE,
        /* TRO */       /* TLO */       /* TROE */      /* TLOE */
        0,              0,              0,              0,
        /* TROA */      /* TLOA */      /* TRON */      /* TLON */
        0,              0,              0,              0,
        /* TDO */       /* TSO */       /* TDOE */      /* TSOE */
        FCE,            FCE,            FCE,            FCE,    
        /* TDOA */      /* TSOA */      /* TDON */      /* TSON */
        FCE,            FCE,            FCE,            FCE,
        /* IOT  Instructions */
        0,              0,              0,              0,
        0,              0,              0,              0,
        0,              0,              0,              0,
        0,              0,              0,              0,
        0,              0,              0,              0,      
        0,              0,              0,              0,
        0,              0,              0,              0,
        0,              0,              0,              0,
        0,              0,              0,              0,
        0,              0,              0,              0,
        0,              0,              0,              0,      
        0,              0,              0,              0,
        0,              0,              0,              0,      
        0,              0,              0,              0,
        0,              0,              0,              0,
        0,              0,              0,              0,
};

#define SWAP_AR         ((RMASK & AR) << 18) | ((AR >> 18) & RMASK)
#define SMEAR_SIGN(x)   x = ((x) & SMASK) ? (x) | EXPO : (x) & MANT
#define GET_EXPO(x)     ((((x) & SMASK) ? 0377 : 0 )  \
                                        ^ (((x) >> 27) & 0377))
    

void set_interrupt(int dev, int lvl) {
    if (lvl) {
       dev_irq[dev>>2] = 0200 >> lvl;
       pi_pending = 1;
//       if (dev != 4 && (dev & 0774) != 0120)
 //      fprintf(stderr, "set irq %o %o\n\r", dev & 0774, lvl);
    }
}

void clr_interrupt(int dev) {
    dev_irq[dev>>2] = 0;
}

void check_apr_irq() {
        int flg = 0;
        clr_interrupt(0);
        clr_interrupt(4);
        if (apr_irq) {
            flg |= ((FLAGS & OVR) != 0) & ov_irq;
            flg |= ((FLAGS & FLTOVR) != 0) & fov_irq;
#if KI
            flg |= clk_flg & clk_irq;
#endif
            flg |= nxm_flag | mem_prot | push_ovf;
            if (flg)
                set_interrupt(0, apr_irq);
        }
        if (clk_flg & clk_en)
            set_interrupt(4, clk_irq);
}

int check_irq_level() {
     int i, lvl;
     int pi_ok, pi_t;

     if (!pi_enable)
        return 0;

     pi_pending = 0;
     for(i = lvl = 0; i < 128; i++) 
        lvl |= dev_irq[i];
     PIR |= (lvl & PIE);
//     fprintf(stderr, "PIR=%o PIE=%o\n\r", PIR, PIE);
     /* Compute mask for pi_ok */
     pi_t = (~PIR & ~PIH) >> 1;
     pi_ok = 0100 & (PIR & ~PIH);
     if (!pi_ok) {
        /* None at level 1, check for lower level */
         lvl = 0040;
         for(i = 2; i <= 7; i++) {
            if (lvl & pi_t) {
                pi_ok |= lvl;
                lvl >>= 1;
            } else {
                break;
            }
         }
     }
     /* We have 1 bit for each non held interrupt. */
     pi_req = PIR & ~PIH & pi_ok;
     if (pi_req) {
        int pi_r = pi_req;
        for(lvl = i = 1; i<=7; i++, lvl++) {
           if (pi_r & 0100) 
              break;
           pi_r <<= 1;
        }
        pi_enc = lvl;
        return 1;
     }
     return 0;
}       

void restore_pi_hold() {
     int i, lvl;
     int pi_ok, pi_t;

     if (!pi_enable)
        return;
     /* Compute mask for pi_ok */
 //    printf("PI restore PIR=%o PIH=%o\n", PIR, PIH);
     lvl = 0100;
     /* None at level 1, check for lower level */
     for(i = 1; i <= 7; i++) {
        if (lvl & PIH) {
            PIR &= ~lvl;
            PIH &= ~lvl;
            break;
         }
         lvl >>= 1;
     }
     if (dev_irq[0])
        check_apr_irq();
     pi_pending = 1;
  //   printf("PI done PIR=%o PIH=%o\n", PIR, PIH);
}       

void set_pi_hold() {
     PIH |= 0200 >> pi_enc;
     PIR &= ~(0200 >> pi_enc);
}       

#if KI
static int      timer_irq, timer_flg;
static uint64   fault_data;


t_stat dev_pag(uint32 dev, uint64 *data) {
    uint64 res = 0;
    switch(dev & 03) {
    case CONI:
        /* Complement of vpn */
        *data = res;
        break;

     case CONO:
        /* Set Stack AC and Page Table Reload Counter */
        ac_stack = (*data >> 9) & 0760;
        pag_reload = *data & 037;
        break;

    case DATAO:
        res = *data;
        if (res & LSIGN) {
            eb_ptr = (res & 017777) << 9;
            page_enable = (res & 020000) != 0;
        }
        if (res & SMASK) {
            ub_ptr = ((res >> 18) & 017777) << 9;
            user_addr_cmp = (res & 00020000000000LL) != 0;
            small_user =  (res & 00040000000000LL) != 0;
            fm_sel = (res & 00300000000000LL) >> 29;
       }
       break;

    case DATAI:
        res = (eb_ptr >> 9);
        if (page_enable)
            res |= 020000;
        res |= ((uint64)(ub_ptr)) << 9;
        if (user_addr_cmp)
            res |= 00020000000000LL;
        if (small_user)
            res |= 00040000000000LL;
        res |= ((uint64)(fm_sel)) << 29;
        *data = res;
        break;
    return SCPE_OK;
    }
}

#endif

t_stat dev_apr(uint32 dev, uint64 *data) {
    uint64 res = 0;
    switch(dev & 03) {
    case CONI:
        /* Read trap conditions */
#if KI
        res = (apr_irq << 3) | clk_pri | (nxm_flag << 6);/* (((FLAGS & 010000) != 0) << 3);*/
        res |= (inout_fail << 7) | (clk_flg << 9) | (clk_irq << 10);
        res |= (timer_irq << 14) | (parity_irq << 15) | (timer_flg << 17);
#else
        res = apr_irq | (((FLAGS & OVR) != 0) << 3) | (ov_irq << 4) ;
        res |= (((FLAGS & FLTOVR) != 0) << 6) | (fov_irq << 7) ;
        res |= (clk_flg << 9) | (clk_en << 10) | (nxm_flag << 12);
        res |= (mem_prot << 13) | (((FLAGS & USERIO) != 0) << 15);
        res |= (push_ovf << 16);
#endif
        *data = res;
        break;

     case CONO:
        /* Set trap conditions */
        res = *data;
#if KI
        clk_pri = res & 07;
        apr_irq = (res >> 3) & 07;
        if (res & 0000100)
            nxm_flag = 0;
        if (res & 0000200)
            inout_fail = 0;
        if (res & 0001000) {
            clk_flg = 0;
            clr_interrupt(4);
        }
        if (res & 0002000)
            clk_irq = 1;
        if (res & 0004000)
            clk_irq = 0;
        if (res & 0040000)
            timer_irq = 1;
        if (res & 0100000)
            timer_irq = 0;
        if (res & 0400000)
            timer_flg = 0;
        printf("FLAGS = %06o\n", FLAGS);
#else
        clk_irq = apr_irq = res & 07;
        if (res & 010) 
            FLAGS &= ~OVR;
        if (res & 020) 
            ov_irq = 1;
        if (res & 040) 
            ov_irq = 0;
        if (res & 0100) 
            FLAGS &= ~FLTOVR;
        if (res & 0200) 
            fov_irq = 1;
        if (res & 0400) 
            fov_irq = 0;
        if (res & 01000) 
            clk_flg = 0;
        if (res & 02000) 
            clk_en = 1;
        if (res & 04000) 
            clk_en = 0;
        if (res & 010000) 
            nxm_flag = 0;
        if (res & 020000) 
            mem_prot = 0;
        if (res & 0200000)
            reset_all(1);
        if (res & 0400000) 
            push_ovf = 0;
        res = apr_irq | (((FLAGS & OVR) != 0) << 3) | (ov_irq << 4) ;
        res |= (((FLAGS & FLTOVR) != 0) << 6) | (fov_irq << 7) ;
        res |= (clk_flg << 9) | (clk_en << 10) | (nxm_flag << 12);
        res |= (mem_prot << 13) | (((FLAGS & USERIO) != 0) << 15);
        res |= (push_ovf << 16);
//      if (apr_irq != 0)
        //fprintf(stderr, "APR = %012llo\n\r", res);
#endif
        check_apr_irq();
        break;

    case DATAO:
#if !KI
        /* Set protection registers */
        Rh = 0377 & (*data >> 1);
        Rl = 0377 & (*data >> 10);
        Pflag = 01 & (*data >> 18);
        Ph = 0377 & (*data >> 19);
        Pl = 0377 & (*data >> 28);
//      printf("relocation [%o,%o] [%o,%o] %o\n", Pl,Rl, Ph,Rh, Pflag);
#endif
        break;

    case DATAI:
        /* Read switches */
        break;
    }
    return SCPE_OK;
}

#if KI
int page_lookup(int addr, int flag, int *loc, int wr) {
    uint64  data;
    int base;
    int page = addr >> 9;
    int pg;
    int uf = 0;
    if (!flag && (FLAGS & USER) != 0) {
        base = ub_ptr;
        uf = 1;
        if (small_user && (addr & 0340000) != 0) {
            fault_data = 2LL;
            fault_data = (((uint64)(page))<<18) | ((uint64)(uf) << 28);
            return 0;
        }
    } else {
        if ((addr & 0340000) == 0340000) {
            base = ub_ptr;
            page += 01000 - 0340;
        } else if (addr & 0400000) {
            base = eb_ptr;
        } else {
            *loc = addr;
            return 1;
        }
    }
    data = M[base + (page >> 1)];
    if (page & 1)
       data >>= 18;
    data &= RMASK;
    if ((data & LSIGN) == 0 || (wr & ((data & 0100000) != 0))) {
        fault_data = (((uint64)(page))<<18) | ((uint64)(uf) << 28) | 020LL;
        fault_data |= (data & 0100000) ? 04LL : 0LL;
        fault_data |= (data & 0040000) ? 02LL : 0LL;
        fault_data |= wr;
        return 0;
    }
    *loc = ((data & 037777) << 9) + (addr & 0777);
    return 1;
}

#else
int page_lookup(int addr, int flag, int *loc, int wr) {
      if (!flag && (FLAGS & USER) != 0) {
          if (addr <= ((Pl << 10) + 01777))
             *loc = (AB + (Rl << 10)) & RMASK;
          else if (cpu_unit.flags & UNIT_TWOSEG && 
                    (!Pflag & wr) == wr &&
                    (AB & 0400000) != 0 &&      
                    (addr <= ((Ph << 10) + 01777))) 
             *loc = (AB + (Rh << 10)) & RMASK;
          else {
            mem_prot = 1;
            set_interrupt(0, apr_irq);
            return 0;
          }
      } else {
         *loc = addr; 
      }
      return 1;
}
#endif

t_stat dev_pi(uint32 dev, uint64 *data) {
    uint64 res = 0;
    switch(dev & 3) {
    case CONO:
        /* Set PI flags */
        res = *data;
        if (res & 010000) {
           PIR = PIH = PIE = 0;
           pi_enable = 0;
           parity_irq = 0;
        }
        if (res & 0200)
           pi_enable = 1;
        if (res & 0400)
           pi_enable = 0;
        if (res & 01000)
           PIE &= ~(*data & 0177);
        if (res & 02000)
           PIE |= (*data & 0177);
        if (res & 04000) {
           PIR |= (*data & 0177);
           pi_pending = 1;
        }
        if (res & 040000)
           parity_irq = 1;
        if (res & 0100000)
           parity_irq = 0;
        break;
     case CONI:
        res = PIE;
        res |= (pi_enable << 7);
        res |= (PIH << 8);
#if KI
        res |= ((uint64)(PIR) << 18);
#endif
        res |= (parity_irq << 15);
        *data = res;
        break;
    case DATAO:
        /* Set lights */
    case DATAI:
        break;
    }
    return SCPE_OK;
}

t_stat null_dev(uint32 dev, uint64 *data) {
    switch(dev & 3) {
    case CONI:
    case DATAI:
         *data = 0;
         break;

    case CONO:
    case DATAO:
         break;
    }
    return SCPE_OK;
}

#if KI
uint64 get_reg(int reg) {
}

void   set_reg(int reg, uint64 value) {
}
#endif


int Mem_read(int flag) {
        if (AB < 020) {
            MB = FM[fm_blk|AB];
        } else {
            int addr;
            sim_interval--;
            if (!page_lookup(AB, flag, &addr, 0))
                return 1;
            if (addr > MEMSIZE) {
                nxm_flag = 1;
                set_interrupt(0, apr_irq);
                return 1;
            }
            MB = M[addr];
        }
 //   printf("Read C(%o) = %06o %06o\n\r", AB,
//         (uint18)((MB >> 18) & RMASK), (uint18)(MB & RMASK));
        return 0;
}

int Mem_write(int flag) {
        if (AB < 020)
                FM[fm_blk|AB] = MB;
        else {
            int addr;
            sim_interval--;
            if (!page_lookup(AB, flag, &addr, 1))
                return 1;
            if (addr > MEMSIZE) {
                nxm_flag = 1;
                set_interrupt(0, apr_irq);
                return 1;
            }
            M[addr] = MB;
        }
//    printf("Write C(%o) = %06o %06o\n\r", AB,
//         (uint18)((MB >> 18) & RMASK), (uint18)(MB & RMASK));
        return 0;
}

t_stat sim_instr (void)
{
t_stat reason;
int     f;
int     i_flags;
int     pi_rq;
int     pi_ov;
int     pi_cycle;
int     ind;
int     f_load_pc;
int     f_inst_fetch;
int     f_pc_inh;
int     nrf;
int     fxu_hold_set;
int     sac_inh;
int     flag1;
int     flag3;
/* Restore register state */

if ((reason = build_dev_tab ()) != SCPE_OK)            /* build, chk dib_tab */
    return reason;


/* Main instruction fetch/decode loop: check clock queue, intr, trap, bkpt */
   f_load_pc = 1;
   f_inst_fetch = 1;
   ind = 0;
   uuo_cycle = 0;
   push_ovf = mem_prot = nxm_flag = 0;
   pi_cycle = 0;
   pi_rq = 0;
   pi_ov = 0;
   BYF5 = 0;
//   sim_activate(&cpu_unit, 10000);


  while ( reason == 0) {                                /* loop until ABORT */
     if (sim_interval <= 0) {                           /* check clock queue */
          if (reason = sim_process_event () != SCPE_OK) {    /* error?  stop sim */
                if (reason != SCPE_STEP || !BYF5)
                   return reason;
          }
    }
        if (sim_brk_summ &&
                 sim_brk_test(PC, SWMASK('E'))) {
            reason = STOP_IBKPT;
            break;
        }


        if (f_load_pc) {
            AB = PC;
            uuo_cycle = 0;
        }
#if KI
        fm_blk = (FLAGS & USER) ? fm_sel : 0;
#endif
        if (f_inst_fetch) {
fetch:
           Mem_read(pi_cycle | uuo_cycle);
           IR = (MB >> 27) & 0777;
           AC = (MB >> 23) & 017;
           i_flags = opflags[IR];
           BYF5 = 0;
        }
        if (BYF5) {
            i_flags = FCE;
            AB = AR & RMASK;
        }
        if (hst_lnt && !BYF5) {
                int i;
                hst_p = hst_p + 1;
                if (hst_p >= hst_lnt)
                        hst_p = 0;
                hst[hst_p].pc = HIST_PC | AB;
                hst[hst_p].ea = AB;
                hst[hst_p].ir = MB;
                hst[hst_p].flags = (FLAGS << 4) |(clk_flg << 3) |(mem_prot << 2) | (nxm_flag << 1) | (push_ovf);
                hst[hst_p].ac = get_reg(AC);
        }
        do {
           if (pi_enable & !pi_cycle & pi_pending) {
              pi_rq = check_irq_level();
           }
           ind = (MB & 020000000) != 0;
           AR = MB;
           AB = MB & RMASK;
           if (MB &  017000000) {
               AR = MB = (AB + get_reg((MB >> 18) & 017)) & FMASK;
               AB = MB & RMASK;
           }
           if (IR != 0254)
               AR &= RMASK;
           if (ind & !pi_rq)
                Mem_read(pi_cycle | uuo_cycle);
        } while (ind & !pi_rq);

        if (hst_lnt) {
                hst[hst_p].ea = AB;
        }

        if (pi_rq) {
            set_pi_hold();
            pi_cycle = 1;
            pi_rq = 0;
            pi_hold = 0;
            pi_ov = 0;
            AB = 040 | (pi_enc << 1);
            goto fetch;
        }


fetch_opr:
        f_inst_fetch = 1;
        f_load_pc = 1;
        f_pc_inh = 0;
        nrf = 0;
        fxu_hold_set = 0;
        sac_inh = 0;
        if (i_flags & (FCEPSE|FCE)) {
            if (Mem_read(0)) 
                goto last;
            AR = MB;
        }

        if (i_flags & FAC) {
            AR = get_reg(AC);
        }

        if (i_flags & SWAR) {
            AR = SWAP_AR;
        }

        if (i_flags & MBR) {
            BR = AR;
            AR = get_reg(AC);
        }

        if (hst_lnt) {
            hst[hst_p].mb = AR;
        }

        if (i_flags & FAC2) {
            MQ = get_reg(AC + 1);
        } else if (!BYF5) {
            MQ = 0;
        }
        

        switch (IR & 0770) {
        case 0040:
        case 0050:
        case 0060:
        case 0070:
muuo:
                uuo_cycle = 1;
        case 0000:      /* UUO */
                if (IR == 0)
                   uuo_cycle = 1;
        case 0010:
        case 0020:
        case 0030:
                f_pc_inh = 1;
uuo:
                MB = ((uint64)(IR) << 27) | ((uint64)(AC) << 23) | (uint64)(AB);
#if KI
               if (IR == 0 || (IR & 040) != 0) {
                    AB = ub_ptr | 0424;
                    uuo_cycle = 1;
                    Mem_write(uuo_cycle);
                    AB |= 1;
                    MB = (FLAGS << 23) | ((PC + 1) & RMASK);
                    Mem_write(uuo_cycle);
                    AB = ub_ptr | 0430;
                    if (((FLAGS & (TRP1|TRP2)) != 0) /*|| page_fault*/)
                        AB |= 1;
                    if (FLAGS & USER)
                        AB |= 2;
                    if (FLAGS & PUBLIC)
                        AB |= 4;
                    Mem_read(uuo_cycle);
                    FLAGS |= (MB >> 23) & 017777;
                    PC = MB & RMASK;
                    f_pc_inh = 1;
                    break;
                }
                AB = ((FLAGS & USER) ? 0 : eb_ptr) | 040;
#else
                AB = 040;
#endif
//              uuo_cycle = ((IR & 0077) == 0) | ((IR & 0040) != 0);
                Mem_write(uuo_cycle);
                AB += 1;
                f_load_pc = 0;
                break;
#if KI
        case 0100: /* OPR */ /* MUUO */
                goto muuo;
        case 0110:
        case 0120:
                switch (IR & 07) {
                        case 3: /* UUO */
                                goto muuo;
                        case 0: /* DMOVE */
                        case 1: /* DMOVN */
                                /* AR High */
                                AB = (AB + 1) & RMASK;
                                if (Mem_read(0))
                                     break;
                                AD = MB;        /* Low */
                printf(" DMOV AD = %07o %06o AR = %06o %06o MQ = %06o %06o\n",
                        (uint18)((AD >> 18) & FMASK), (uint18)(AD & RMASK),
                        (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                        (uint18)((MQ >> 18) & RMASK), (uint18)(MQ & RMASK));
                                if (IR & 1) {   /* DMOVN */
                                     BR = AR;   /* Save High */
                                     AR = AD;
                                     AD = ((AR & CMASK) ^ CMASK) + 1;
                                     MQ = AD & CMASK;   /* Low */
                printf(" DMOV AD = %07o %06o AR = %06o %06o MQ = %06o %06o\n",
                        (uint18)((AD >> 18) & FMASK), (uint18)(AD & RMASK),
                        (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                        (uint18)((MQ >> 18) & RMASK), (uint18)(MQ & RMASK));
                                        /* High */
                                     AD = (BR ^ FMASK) + ((AD & SMASK) != 0);
                                     AR = AD & FMASK;
                                } else {        /* DMOVE */
                                     MQ = AD;
                                }
                printf(" DMOV AD = %07o %06o AR = %06o %06o MQ = %06o %06o\n",
                        (uint18)((AD >> 18) & FMASK), (uint18)(AD & RMASK),
                        (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                        (uint18)((MQ >> 18) & RMASK), (uint18)(MQ & RMASK));
                                break;
                        case 4: /* DMOVEM */
                        case 5: /* DMOVNM */
                                /* Handle each half as seperate instruction */
                                if ((FLAGS & BYTI) == 0 || pi_cycle) {
                                    if (IR & 1) {       /* DMOVN */
                                        AD = (FM[AC] ^ FMASK);
                                        BR = AR = AD;
                                        AD = (AR + 1);
                printf(" DMOVM0 AD = %07o %06o AR = %06o %06o MQ = %06o %06o\n",
                        (uint18)((AD >> 18) & FMASK), (uint18)(AD & RMASK),
                        (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                        (uint18)((MQ >> 18) & RMASK), (uint18)(MQ & RMASK));
                                        AR = AD & FMASK;
                                        MQ = FM[(AC + 1) & 017] & CMASK;
                                        AD = ((MQ ^ CMASK) + 1);
                                        if (AD & SMASK)
                                           BR = AR;
                                    } else {
                                        AR = FM[AC];
                                        BR = AR;
                                    }
                printf(" DMOVM1 AD = %07o %06o AR = %06o %06o MQ = %06o %06o\n",
                        (uint18)((AD >> 18) & FMASK), (uint18)(AD & RMASK),
                        (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                        (uint18)((MQ >> 18) & RMASK), (uint18)(MQ & RMASK));
                                    MB = BR;
                                    if (Mem_write(0))
                                        break;
                                    if (!pi_cycle) {
                                        FLAGS |= BYTI;
                                        f_pc_inh = 1;
                                        break;
                                     }
                                }
                                if ((FLAGS & BYTI) || pi_cycle) {
                                     if (!pi_cycle)
                                        FLAGS &= ~BYTI;
                                     if (IR & 1) {
                                         AD = (FM[(AC+1) & 017] ^ FMASK) + 1;
                                         AR = AD & CMASK;
                                     } else {
                                         AD = get_reg(AC+1);
                                         AR = AD;
                                     }
                                     AB = (AB + 1) & RMASK;
                                     MB = AR;
                                     if (Mem_write(0))
                                        break;
                printf(" DMOVM2 AD = %07o %06o AR = %06o %06o AB = %06o %06o\n",
                        (uint18)((AD >> 18) & FMASK), (uint18)(AD & RMASK),
                        (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                        (uint18)((AB >> 18) & RMASK), (uint18)(AB & RMASK));
                                }
                                break;
                        case 2: /* FIX */
                        case 6: /* FIXR */
                                MQ = 0;
                                SCAD = ((((AR & SMASK) ? 0377 : 0 )
                                        ^ ((AR >> 27) & 0377)) + 0600) & 0777;
                                FE = SC = ((SCAD) + 0744 + 1) & 0777;
                printf(" FIX1 AR = %06o %06o BR = %06o %06o SC = %o SCAD = %o\n",
                        (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                        (uint18)((MQ >> 18) & RMASK), (uint18)(MQ & RMASK), SC, SCAD);
                                SCAD = ((SC ^ 0777)  + 011) & 0777;
                                flag1 = 0;
                                if (((AR & SMASK) != 0) != ((AR & BIT1) != 0)) {
                                     if (AR & SMASK)
                                         AR |= 00377000000000LL;
                                     else
                                         AR &= 00000777777777LL;
                                     flag1 = 1;
                                }
                printf(" FIX2 AR = %06o %06o BR = %06o %06o SC = %o SCAD = %o flag1=%o\n",
                        (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                        (uint18)((MQ >> 18) & RMASK), (uint18)(MQ & RMASK), SC, SCAD, flag1);
                                /* N < -27 */
                                if (!flag1) {
                printf(" FIX3 N < -27 \n");
                                    set_reg(AC, 0);
                                    break;
                                } else
                                /* N > 8 */
                                if (((SC & 0400) == 0) & ((SCAD & 0400) != 0)) {
                printf(" FIX3 N > 8 \n");
                                    if (!pi_cycle)
                                        FLAGS |= OVR|TRP1;        /* OV & T1 */
                                    break;
                                } else
                                /* 0 < N < 8 */
                                if (((SC & 0400) == 0) & ((SCAD & 0400) == 0)) {
                printf(" FIX3 0 < N < 8 \n");
                                    SC = ((SC ^ 0777) + 1) & 0777;
                                    if ((SC & 0400) == 0) {

                                       set_reg(AC, AR);
                                        break;
                                    }
                                    while(SC & 0400) {
                                  /* SCT1 */
                                        SC = (SC + 1) & 0777;
                                  /* SCT3 */
                                        AR = (AR << 1) & FMASK;
        printf("  FIX4 AD = %07o %06o ",
                (uint18)((AD >> 18) & FMASK), (uint18)(AD & RMASK));
        printf(" AR = %06o %06o MQ = %06o %06o SC=%o flag=%o\n",
                (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                (uint18)((MQ >> 18) & RMASK), (uint18)(MQ & RMASK), SC, flag1);
                                        }
                                }  else
                                /* -27 < N < 0 */
                                if (flag1 & ((SC & 0400) != 0)) {
                printf(" FIX3 -27 < N < 0 \n");
                                     while(SC & 0400) {
                                  /* SCT1 */
                                           SC = (SC + 1) & 0777;
                                  /* SCT3 */
                                        AD = AR;
                                        AR = ((AD & FMASK) >> 1) | (AD & SMASK) ;
                                        MQ = (MQ & MMASK)>> 1 | ((AD & 1) ? BIT8 : 0) |
                                                (((MQ & EMASK) >> 1) & EMASK);
        printf("  FIX4 AD = %07o %06o ",
                (uint18)((AD >> 18) & FMASK), (uint18)(AD & RMASK));
        printf(" AR = %06o %06o MQ = %06o %06o SC=%o flag=%o\n",
                (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                (uint18)((MQ >> 18) & RMASK), (uint18)(MQ & RMASK), SC, flag1);
                                        }
                                }
                                AD = (AR + 1) & FMASK;
                                if (((IR & 4) != 0) & ((MQ & BIT8) != 0)) {
                                        AR = AD;
                                }
                                if (((IR & 4) == 0) & ((AR & SMASK) != 0) &
                                        (((MQ & BIT8) != 0) |
                                         ((MQ & (BIT9|BIT10_35)) != 0))) {
                                         AR = AD;
                                }
        printf("  FIX4 AD = %07o %06o ",
                (uint18)((AD >> 18) & FMASK), (uint18)(AD & RMASK));
        printf(" AR = %06o %06o MQ = %06o %06o SC=%o flag=%o\n",
                (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                (uint18)((MQ >> 18) & RMASK), (uint18)(MQ & RMASK), SC, flag1);
                                set_reg(AC, AR);
                                break;
                        case 7: /* FLTR */
                                MQ = 0;
                                SC = (0777 ^ 8);
                                while(SC != 0777) {
                                        uint64 tmq;
                                       tmq = ((MQ & (SMASK - 1)) >> 1) | ((AR & 1) ? BIT8: 0);
                                       AD = (AR >> 1) | (AR & SMASK);
                                       MQ = tmq;
                                       AR = AD;
                                       SC = (SC + 1) & 0777;
                printf(" FLTR AR = %06o %06o MQ = %06o %06o SC = %o\n",
                        (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                        (uint18)((MQ >> 18) & RMASK), (uint18)(MQ & RMASK), SC);
                                }
                                SC = 128 + 35;
                printf(" FLTR AR = %06o %06o MQ = %06o %06o SC = %o\n",
                        (uint18)((AR >> 18) & RMASK), (uint18)(AR & RMASK),
                        (uint18)((MQ >> 18) & RMASK), (uint18)(MQ & RMASK), SC);
                                goto fnorm;
                        }
                        break;
#else
        case 0100: /* OPR */ /* MUUO */
        case 0110:
        case 0120:
                MB = ((uint64)(IR) << 27) | ((uint64)(AC) << 23) | (uint64)(AB);
                AB = 060;
                uuo_cycle = 1;
                Mem_write(uuo_cycle);
                AB += 1;
                f_load_pc = 0;
                f_pc_inh = 1;
                break;
#endif
        case 0130:      /* Byte OPS */
                switch(IR & 07) {
                case 3: /* IBP/ADJBP */
                case 4: /* ILDB */
                case 6: /* IDPB */
                        if ((FLAGS & BYTI) == 0) {      /* BYF6 */
                            SC = (AR >> 24) & 077;
                            SCAD = (((AR >> 30) & 077) + (0777 ^ SC) + 1) & 0777;
                            if (SCAD & 0400) {
                                SC = ((0777 ^ ((AR >> 24) & 077)) + 044 + 1) & 0777;
#if KI
                                AR = (AR & LMASK) | ((AR + 1) & RMASK);
#else
                                AR = (AR + 1) & FMASK;
#endif
                            } else
                                SC = SCAD;
                            AR &= PMASK;
                            AR |= (uint64)(SC & 077) << 30;
                            if ((IR & 04) == 0) 
                                break;
                        }
                case 5: /* LDB */
                case 7: /* DPB */
                        if (((FLAGS & BYTI) == 0) | !BYF5) {
                            SC = (AR >> 30) & 077;
                            MQ = (uint64)(1) << ( 077 & (AR >> 24));
                            MQ -= 1;
                            SC = ((0777 ^ SC) + 1) & 0777;
                            f_load_pc = 0;
                            f_inst_fetch = 0;
                            f_pc_inh = 1;
                            FLAGS |= BYTI;      /* BYF6 */
                            BYF5 = 1;
                            break;
                        }
                        if ((IR & 06) == 4) {
                            AR = MB;
                            while(SC != 0) {
                                AR >>= 1;
                                SC = (SC + 1) & 0777;
                            }
                            AR &= MQ;
                            set_reg(AC, AR);
                        } else {
                            BR = MB;
                            AR = get_reg(AC) & MQ;
                            while(SC != 0) {
                                AR <<= 1;
                                MQ <<= 1;
                                SC = (SC + 1) & 0777;
                            }
                            BR &= CM(MQ);
                            BR |= AR & MQ;
                            MB = BR;
                            Mem_write(0);
                        }
                        FLAGS &= ~BYTI; /* BYF6 */
                        BYF5 = 0;
                        break;
                case 1: /* DFN */
                        AD = (CM(BR) + 1) & FMASK;
                        SC = (BR >> 27) & 0777;
                        BR = AR;
                        AR = AD;
                        AD = (CM(BR) + ((AD & MANT) == 0)) & FMASK;
                        AR &= MANT;
                        AR |= ((uint64)(SC & 0777)) << 27;
                        BR = AR;
                        AR = AD;
                        MB = BR;
                        if (Mem_write(0))
                           break;
                        set_reg(AC, AR);
                        break;
                case 2: /* FSC */
                        BR = AB;
                        SC = ((AB & 0400000) ? 0400 : 0) | (AB & 0377);
                        BR = AR; 
                        SCAD = GET_EXPO(BR);
                        SC = (SCAD + SC) & 0777;
                        /* Smear the signs */
                        SMEAR_SIGN(AR);
                        goto fnorm; 
                case 0: /* UFA */       
                        goto fadd;
                }
                break;
        case 0140:      /* FAD */
        case 0150:      /* FSB */
fadd:
                if ((IR & 07) == 05) 
                    AR = SWAP_AR;
                BR = AR;
                AR = get_reg(AC);
                if ((IR & 010) && (IR  != 0130)) {
                    AD = (CM(BR) + 1) & FMASK;
                    BR = AR;
                    AR = AD;
                }
                SC = ((AR >> 27) & 0777);
                if ((AR & SMASK) == (BR & SMASK)) {
                    SCAD = SC + (((BR >> 27) & 0777) ^ 0777) + 1;
                } else {
                    SCAD = SC + ((BR >> 27) & 0777);
                }
                SC = SCAD & 0777;
                if (((AR & SMASK) != 0) == ((SC & 0400) != 0)) {
                    AD = AR;
                    AR = BR;
                    BR = AD;
                }
                if ((SC & 0400) == 0) {
                   if ((AR & SMASK) == (BR & SMASK)) 
                        SC = ((SC ^ 0777) + 1) & 0777;
                   else
                        SC = (SC ^ 0777);
                } else {
                   if ((AR & SMASK) != (BR & SMASK)) 
                        SC = (SC + 1) & 0777;
                }
                
                /* Smear the signs */
                SMEAR_SIGN(AR);
                if (SC & 0400) {
                    if (((SC & 0200) != 0) || ((SC & 0100) != 0)) {
                        while (SC != 0) {
                            MQ = ((AR & 1) ? BIT8 : 0) 
                                    | (MQ >> 1);
                            AR = (AR & SMASK) | (AR >> 1);
                            SC = (SC + 1) & 0777;
                        }
                    } else {
                        AR = 0;
                    }
                }
                /* Get exponent */
                SC = GET_EXPO(BR);
                /* Smear the signs */
                SMEAR_SIGN(BR);
                AR = (AR + BR) & FMASK;
fnorm:
/* NRT0 */
                if ((AR != 0) || ((MQ & 00001777777777LL) != 0)) {
                    if ((((AR & SMASK) != 0) != 
                         ((AR & BIT8) != 0))  || 
                        ((AR & MMASK) == BIT8)) {
                        if ((IR & 070) != 070)  /* Not FDVx */
                            MQ = ((AR & 1) ? BIT8 : 0) 
                                | (MQ >> 1);
                        AR = (AR & SMASK) | (AR >> 1);
                        SC = (SC + 1) & 0777;
                        goto fnorm;
                    }
/* NRT1 */
                    if (!nrf & (((SC & 0400) != 0) ^ (((SC & 0200) != 0))))
                        fxu_hold_set = 1;
                    SC = SC ^ 0777;

                /* Skip on UFA */
                if (IR != 0130) {
/* NRT2 */
                    while (!((((AR & SMASK) != 0) != ((AR & BIT9) != 0))
                        | (((AR & MANT) == BIT9) & ((MQ & BIT8) == 0)))
                        ) { 
                        AR = (((MQ & BIT8) ? 1 : 0) 
                                | (AR << 1)) & FMASK;
                        if ((IR & 070) != 070)
                            MQ = (MQ & 00376000000000LL) |
                                 ((MQ << 1) & 00001777777777LL);
                        SC = (SC + 1) & 0777;
                    }
                 }
/* NRT3 */
                 SC = SC ^ 0777;
                    if (!nrf & ((IR & 04) != 0) & 
                        ((MQ &  BIT8) != 0) &
                          !(((MQ & 00000777777777LL) == 0) &
                            ((AR & SMASK) != 0))) {
                        AR = (AR + 1) & FMASK;
                        nrf = 1;
#if !KI
                        goto fnorm;
#endif
                    }
                    if (((SC & 0400) != 0)) {
                        FLAGS |= OVR|FLTOVR;
                        if (!fxu_hold_set) {
                            FLAGS |= FLTUND;
                        }
                        check_apr_irq();
                    }
                    SCAD = SC ^ ((AR & SMASK) ? 0377 : 0);
                    AR &=00400777777777LL;
                    AR |= ((uint64)(SCAD & 0377)) << 27;
                    if ((IR & 07) == 1 && (IR & 070) != 070) {
                        SC = (SC + (0777 ^  26)) & 0777;
                        if (MQ != 0) 
                            AD = (MQ & 00401777777777LL);
                        else
                            AD = 0;
                        MQ = AR;
                        AR = AD;
                        AR = (AR & SMASK) | (AR >> 1);
                        SCAD = SC ^ ((AR & SMASK) ? 0377 : 0);
                        if (AR != 0)
                            AR |= ((uint64)(SCAD & 0377)) << 27;
                        AD = MQ;
                        MQ = AR;
                        AR = AD;
                     }
                }

                if ((IR & 070) == 070) {        /* FDV */
                        AD = (CM(AR) + 1) & FMASK;
                        if (flag1 ^ ((BR & SMASK) != 0))
                                AR = AD;
                        if ((IR & 07) == 1) {   /* FDVL */
                                BR = AR;
                                AR = 0;
                                SC = 0;
                                AR = get_reg(AC);
                                SC = ((AR >> 27) & 0777) ^ 
                                        ((AR & SMASK) ? 0777 : 0);
                                SCAD = (flag3) ? 032 : 033;
                                SCAD = (SC + (0777 ^ SCAD) + 1) & 0777;
                                AR = MQ;
                                if ((((AR & SMASK) != 0) == 
                                     ((SCAD & 0400) != 0)) &
                                     ((AR & MANT) != 0)) {
                                    AR &=00400777777777LL;
                                    AR |= ((uint64)(SCAD & 0377)) << 27;
                                } else {
                                    AR = 0;
                                }
                                MQ = AR;
                                AR = BR;
                        }
                }
        
                /* Handle UFA */
                if (IR == 0130) {
                    set_reg(AC + 1, AR);
                    break;
                }
        
        
                break;
        case 0160:      /* FMP */
                if ((IR & 07) == 05) 
                     AR = SWAP_AR;
                BR = AR;
                AR = get_reg(AC);
                /* FPT0 */
                SC = (AR >> 27) & 0777;
                SCAD = ((AR & SMASK) ? 0777 : 0) ^ SC;
                /* FPT1 */
                SC = SCAD;
                SCAD = SC + (((BR & SMASK) ? 0777 : 0  ) ^
                                ((BR >> 27) & 0777));
                /* FPT2 */
                SC = SCAD & 0777;
                SC = ((0 ^ SC) + 0600) & 0777;
                FE = SC;
                flag3 = (BR & AR & SMASK) != 0;
                /* Smear the signs */
                SMEAR_SIGN(AR);
                /* Smear the signs */
                SMEAR_SIGN(BR);
                MQ = BR;
                BR = AR;
                AR = 0;
                SC = 0745;
                flag1 = 1;
                if (MQ & 01) {
                   AD = CM(BR) + 1;
                } else {
                   AD = 0; 
                }
                /* SCT0 */
                AD = AD + AR;
                while(SC & 0400) {
                    uint64 b1;
                /* SCT1 */
                    SC = (SC + 1) & 0777;
                /* SCT3 */
                    b1 = AD & 1;
                    AR = ((AD & FMASK) >> 1) | (AD & SMASK) ;
                    switch(MQ & 3) {
                    case 3:     
                    case 0:     AD = AR;
                                break;
                    case 1:     AD = AR + BR; 
                                break;
                    case 2:     AD = AR + CM(BR) + 1;
                                break;
                    }
                    MQ = (MQ & MMASK)>> 1 | (b1 ? BIT8 : 0) |
                        (((MQ & EMASK) >> 1) & EMASK);
                } 
                AR = AD & FMASK;
                SC = FE;
                MQ &= ~1;
                goto fnorm;
        case 0170:      /* FDV */
                if ((IR & 07) == 05) 
                     AR = SWAP_AR;
                BR = AR;
                AR = get_reg(AC);
                flag1 = 0;
                if ((IR & 7) == 1) {    /* FDVL */
                     if (AR & SMASK) {
                         AD = (CM(MQ) + 1) & FMASK;
                         MQ = AR;
                         AR = AD;
                         AD = (CM(MQ)) & FMASK;
                         MQ = AR;
                         AR = AD;
                         if ((MQ & CMASK) == 0)
                             AR = (AR + 1) & FMASK;
                         flag1 = 1;
                     }
                     MQ = ((MQ << 1) & FMASK) ; 
                } else {                /* not FDVL */
                     if (AR & SMASK) {
                         AD = (CM(AR) + 1) & FMASK;
                         flag1 = 1;
                     } else {
                         AD = AR;
                     }
                     AR = AD;
                }     
                SC = (AR >> 27) & 0777;
                SCAD = ((AR & SMASK) ? 0777 : 0) ^ SC;
                SC = SCAD;
                SCAD = SC + (((BR & SMASK) ? 0 : 0777  ) ^
                                ((BR >> 27) & 0777)) + 1;
                SCAD &= 0777;
                SC = SCAD;
                SC = (SC + 0200) & 0777;
                FE = SC;
                flag3 = 0;
                /* Smear the signs */
                SMEAR_SIGN(AR);
                SMEAR_SIGN(BR);
                if (BR & SMASK) 
                     AD = (AR + BR) & FMASK;
                else
                     AD = (AR + CM(BR) + 1) & FMASK;
                if ((AD & SMASK) == 0) {
                    MQ = (MQ & MMASK)>> 1 | ((AR & 1) ? BIT8 : 0) |
                        (((MQ & 0376000000000LL ) >> 1) & EXPO);
                    AR = (AR >> 1) | (AR & SMASK);
                    FE = (SC + 01) & 0777;
                    flag3 = 1;
                }
                if (((SC & 0400) != 0) ^ (((SC & 0200) != 0)))
                    fxu_hold_set = 1;
                SC = ((IR & 04) == 0) ? 0745 : 0744;
                if (BR & SMASK) 
                     AD = (AR + BR) & FMASK;
                else
                     AD = (AR + CM(BR) + 1) & FMASK;
                if ((AD & SMASK) == 0) {
                    FLAGS |= OVR|NODIV|FLTOVR;  /* Overflow and No Divide */
                    check_apr_irq();
                    sac_inh = 1;
                    break;      /* Done */
                }

                while (SC != 0) {
                        AR = (AD << 1) | ((MQ & BIT8) ? 1 : 0);
                        AR &= FMASK;
                        MQ = ((MQ << 1 ) &  MMASK) | ((MQ & EMASK) << 1);
                        MQ |= (AD & BIT8) == 0;
                        MQ &= FMASK;
                        if (((BR & SMASK) != 0) ^ ((MQ & 01) != 0))
                             AD = (AR + CM(BR) + 1);
                        else
                             AD = (AR + BR);
                        SC = (SC + 1) & 0777;
                }
                AR = AD & FMASK;
                MQ = ((MQ << 1 ) &  MMASK) | ((MQ & EXPO) << 1);
                MQ |= (AD & SMASK) == 0;
                if (((BR & SMASK) != 0) ^ ((MQ & 01) != 0))
                     AD = (AR + CM(BR) + 1);
                else
                     AD = (AR + BR);
                if ((MQ & 01) == 0)
                    AR = AD & FMASK;
                if (flag1) 
                    AD = (CM(AR) + 1) & FMASK;
                else
                    AD = AR;
                AR = MQ;
                MQ = AD;
                if (IR & 04) {
                   nrf = 1;
                   AR = ((AR + 1) >> 1) | (AR & SMASK);
                }
                SC = FE;
                goto fnorm;
        case 0200: /* FWT */    /* MOVE, MOVS */
        case 0210:              /* MOVN, MOVM */
                switch(IR & 014) {
                case 000: AD = AR; break;               /* MOVE */
                case 004: AD = SWAP_AR; break;  /* MOVS */
                case 014: AD = AR;              /* MOVM */
                          if ((AR & SMASK) == 0)
                                break;
                case 010:                       /* MOVN */
                        { int t1,t2;
                        t1 = t2 = 0;
                        FLAGS &= 01777;
                        if ((((AR & CMASK) ^ CMASK) + 1) & SMASK) {
                            FLAGS |= CRY1;
                            t1 = 1;
                        }
                        AD = CM(AR) + 1;
                        if (AD & C1) {
                            FLAGS |= CRY0;
                            t2 = 1;
                        }
                        if (t1 != t2 && !pi_cycle) {
                            FLAGS |= OVR;
#if KI
                            FLAGS |= TRP1;
#endif
                            check_apr_irq();
                        }
#if KI
                        if (AR == SMASK & !pi_cycle)
                            FLAGS |= TRP1;
#endif

                        }
                        break;
                }
                AD &= FMASK;
                AR = AD;
                break;
        case 0220:      /* IMUL, MUL */
                AD = get_reg(AC);
                flag3 = (AD & AR & SMASK) != 0;
                BR = AR;
                MQ = AD;
                AR = 0;
                SC = 0735;
                flag1 = 1;
                if (MQ & 01) {
                   AD = CM(BR) + 1;
                } else {
                   AD = 0; 
                }
        /* SCT0 */
                AD = AD + AR;
                while(SC & 0400) {
                    uint64 b1;
        /* SCT1 */
                    SC = (SC + 1) & 0777;
        /* SCT3 */
                    b1 = AD & 1;
                    AR = ((AD & FMASK) >> 1) | (AD & SMASK) ;
                    switch(MQ & 3) {
                    case 3:     
                    case 0:     AD = AR;
                                break;
                    case 1:     AD = AR + BR; 
                                break;
                    case 2:     AD = AR + CM(BR) + 1;
                                break;
                    }
                    MQ = (MQ >> 1) | (b1 ? SMASK : 0);
                } 
                MQ = (MQ >> 1) | ((AD & 1) << 35);
                AR = AD & FMASK;
                if ((IR & 4) == 0) 
                    AD = ((AR & (SMASK >> 1)) ? FMASK : 0) ^ AR;
                if ((AR & SMASK) != 0 && flag3) {
                    FLAGS |= OVR;
                    check_apr_irq();
                }
                if ((AD & FMASK) != 0 && (IR & 4) == 0) {
                    FLAGS |= OVR;
                    check_apr_irq();
                }
                MQ = (MQ & ~SMASK) | (AR & SMASK);
                if ((IR & 4) == 0)
                    AR = MQ;
                break;
        case 0230:       /* IDIV, DIV */
                flag1 = 0;
                if (IR & 4) {   /* DIV */
                     MQ = get_reg(AC + 1);
                     if (AR & SMASK) {
                         AD = (CM(MQ) + 1) & FMASK;
                         MQ = AR;
                         AR = AD;
                         AD = (CM(MQ)) & FMASK;
                         MQ = AR;
                         AR = AD;
                         if ((MQ & CMASK) == 0)
                             AR = (AR + 1) & FMASK;
                         flag1 = 1;
                     }
                } else {                /* IDIV */
                     if (AR & SMASK) {
                         AD = (CM(AR) + 1) & FMASK;
                         flag1 = 1;
                     } else {
                         AD = AR;
                     }
                     AR = 0;// (AR & CMASK) == 0;
                     MQ = AD;
                }     
                if (BR & SMASK) 
                     AD = (AR + BR) & FMASK;
                else
                     AD = (AR + CM(BR) + 1) & FMASK;
                MQ = (MQ << 1) & FMASK;
                MQ |= (AD & SMASK) != 0;
                SC = ((0777 ^ 35) + 1) & 0777;
                if ((AD & SMASK) == 0) {
                    FLAGS |= OVR|NODIV; /* Overflow and No Divide */
                    sac_inh=1;
                    check_apr_irq();
                    break;      /* Done */
                }

                while (SC != 0) {
                        if (((BR & SMASK) != 0) ^ ((MQ & 01) != 0))
                             AD = (AR + CM(BR) + 1);
                        else
                             AD = (AR + BR);
                        AR = (AD << 1) | ((MQ & SMASK) ? 1 : 0);
                        AR &= FMASK;
                        MQ = (MQ << 1) & FMASK;
                        MQ |= (AD & SMASK) == 0;
                        SC = (SC + 1) & 0777;
                }
                if (((BR & SMASK) != 0) ^ ((MQ & 01) != 0))
                    AD = (AR + CM(BR) + 1);
                else
                    AD = (AR + BR);
                AR = AD & FMASK;
                MQ = (MQ << 1) & FMASK;
                MQ |= (AD & SMASK) == 0;
                if (AR & SMASK) {
                     if (BR & SMASK) 
                          AD = (AR + CM(BR) + 1) & FMASK;
                     else
                          AD = (AR + BR) & FMASK;
                     AR = AD;
                }
                    
                if (flag1)
                    AR = (CM(AR) + 1) & FMASK;
                if (flag1 ^ ((BR & SMASK) != 0)) {
                    AD = (CM(MQ) + 1) & FMASK;
                    MQ = AR;
                    AR = AD;
                } else {
                    AD = MQ;
                    MQ = AR;
                    AR = AD;
                }
                break;
        case 0240:      /* Shift */
                if ((IR & 03) == 03) {
                        /* NOP on KA10  or JFFO*/
                     if ((IR & 04) == 0) { // JFFO
                        SC = 0;
                        if (AR != 0) {
                            PC = AB;
                            f_pc_inh = 1;
                            if ((AR & LMASK) == 0) {
                                SC = 18;
                                AR = SWAP_AR; //AR << 18;
                            }
                            while ((AR & SMASK) == 0) {
                                SC++;
                                AR <<= 1;
                            }
                        } 
                        set_reg(AC + 1, SC);
                     }
                     break;
                }
                BR = AR;
                AR = get_reg(AC);
                /* Convert shift count to modulus 72 */
                SC = (0377 & AB) | ((AB & LSIGN) ? 0400 : 0);
                SCAD = ((((BR & LSIGN) ? 0777 : 0) ^ SC) + 
                        0220 + ((BR & LSIGN) ? 1 : 0)) & 0777;
                flag1 = 0;
                if (((SCAD & 0400) == 0) && ((SC & 0400) == 0)) {
                    SCAD = ((0777 ^ SC) + 0110 + 1) & 0777;
                } else {
                    int t = SCAD;
                    if (SC & 0400) 
                        SCAD = ((0777 ^ SC) + 0110 + 1) & 0777;
                    else
                        SCAD = (SC + 0110) & 0777;
                    if (SCAD & 0400) {
                        SC = t;
                        flag1 = 1;
                    }
                }
                if (((SCAD & 0400) == 0) && ((SC & 0400) == 0)) {
                    SC = ((0777 ^ SC) + 1) & 0777;
                } else if ((SCAD & 0400) == 0 && (SC & 0400) && ~flag1) {
                } else if ((((SCAD & 0400) != 0) || flag1) && ((IR & 0020) != 020)) {
                    SC = ((0777 ^ SC) + 0110 + 1) & 0777;
                } else  if ((SC & 0400) && (SCAD & 0400) && flag1) {
                    SC = ((0777 ^ SCAD) + 0110 + 1) & 0777;
                } else  if ((SCAD & 0400) && flag1) {
                    SC = ((0777 ^ SCAD) + 1) & 0777;
                } else {
                    SC = SCAD;
                } 
                while (SC != 0) {
                    uint64      tmq;
                    switch (IR & 07) {
                    case 04:            /* ASHC */
                               if (BR & LSIGN) {
                                    tmq = ((MQ & (SMASK - 1)) >> 1) | (AR & SMASK) | 
                                        ((AR & 1) ? SMASK>>1: 0);
                                    AD = (AR >> 1) | (AR & SMASK);
                               } else {
                                    tmq = ((MQ << 1) & ~SMASK) | 
                                                (AR & SMASK);
                                    AD = (AR & SMASK) | 
                                        ((AR << 1) & (SMASK - 1)) |
                                        ((MQ & (SMASK >> 1)) ? 1: 0);
                                    if ((AR ^ (AR << 1)) & SMASK) {
                                        FLAGS |= OVR;
                                        check_apr_irq();
                                    }
                               }
                               break;
                    case 00:            /* ASH */
                               if (BR & LSIGN)
                                    AD = (AR >> 1) | (AR & SMASK);
                               else {
                                    AD = (AR & SMASK) |
                                        ((AR << 1) & (SMASK - 1));
                                    if ((AR ^ (AR << 1)) & SMASK) {
                                        FLAGS |= OVR;
                                        check_apr_irq();
                                    }
                               }
                               break;
                    case 05:            /* ROTC */
                               if (BR & LSIGN) {
                                    tmq = (MQ >> 1) |
                                        ((AR & 1) ? SMASK : 0);
                                    AD = (AR >> 1) |
                                        ((MQ & 1) ? SMASK : 0);
                               } else {
                                    tmq = ((MQ << 1) & FMASK) | 
                                        ((AR & SMASK) ? 1 : 0);
                                    AD = ((AR << 1) & FMASK) |
                                        ((MQ & SMASK) ? 1: 0);
                               }
                               break;
                    case 01:            /* ROT */
                               if (BR & LSIGN) {
                                    AD = (AR >> 1) |
                                        ((AR & 1) ? SMASK : 0);
                               } else {
                                    AD = ((AR << 1) & FMASK) |
                                        ((AR & SMASK) ? 1: 0);
                               }
                               break;
                    case 02:            /* LSH */
                               if (BR & LSIGN) {
                                    AD = AR >> 1;
                               } else {
                                    AD = (AR << 1) & FMASK;
                               }
                               break;
                    case 06:            /* LSHC */
                               if (BR & LSIGN) {
                                    tmq = (MQ >> 1) | 
                                        ((AR & 1) ? SMASK : 0);
                                    AD = AR >> 1;
                               } else {
                                    tmq = (MQ << 1) & FMASK;
                                    AD = ((AR << 1) & FMASK) |
                                        ((MQ & SMASK) ? 1: 0);
                               }
                               break;
                    }
                    MQ = tmq;
                    AR = AD;
                    SC = (SC + 1) & 0777;
                }

                break;
        case 0250:      /* Branch */
                switch(IR & 07) {
                case 0: /* EXCH */
                        BR = AR;
                        AR = get_reg(AC);
                        set_reg(AC, BR);
                        break;
                case 1: /* BLT */
                        BR = AB;
                        AR = get_reg(AC);
                        do {
                           if (sim_interval <= 0) {  
                                sim_process_event ();
                           }
                           // Allow for interrupt
                           if (pi_pending) {
                                pi_rq = check_irq_level();
                                if (pi_rq) {
                                        f_pc_inh = 1;
                                        f_load_pc = 0;
                                        f_inst_fetch = 0;
                                        set_reg(AC, AR);
                                        break;
                                }
                           }
                           AB = (AR >> 18) & RMASK;
                           if (Mem_read(0))
                                break;
                           AB = (AR & RMASK);
                           if (Mem_write(0))
                                break;
                           AD = (AR & RMASK) + CM(BR) + 1;
                           AR = (AR + 01000001LL);
                        } while ((AD & C1) == 0);
                        break;
                case 2: /* AOBJP */
                        AR = get_reg(AC);
#if KI
                        AR = ((AR + 1) & RMASK) |
                                ((AR + 01000000) & LMASK);
#else
                        AR = (AR + 01000001LL);
#endif
                        set_reg(AC, AR & FMASK);
                        if ((AR & SMASK) == 0) {
                            PC = AB;
                            f_pc_inh = 1;
                        }
                        break;
                case 3: /* AOBJN */
                        AR = get_reg(AC);
#if KI
                        AR = ((AR + 1) & RMASK) |
                              ((AR + 01000000) & LMASK);
#else
                        AR = (AR + 01000001LL);
#endif
                        set_reg(AC, AR & FMASK);
                        if ((AR & SMASK) != 0) {
                            PC = AB;
                            f_pc_inh = 1;
                        }
                        break;
                case 4: /* JRST */      /* AR Frm PC */
                        PC = AR & RMASK;
                        if (uuo_cycle | pi_cycle) {
                           FLAGS &= ~USER; /* Clear USER */
                        }
                        /* JEN */
                        if (AC & 010) { // Restore interrupt level.
                           if ((FLAGS & (USER|USERIO)) == USER) {
                                goto uuo;
                                /*
                                MB = ((uint64)(IR) << 27) |
                                        ((uint64)(AC) << 23) | 
                                        (uint64)(AB);
                                AB = 040;
                                Mem_write(0);
                                AB += 1;
                                f_load_pc = 0;
                                */
                           } else {     
                                pi_restore = 1;
                           }
                        }
                        /* HALT */
                        if (AC & 04) {
                           if ((FLAGS & (USER|USERIO)) == USER) {
                                goto uuo;
                                /*
                                MB = ((uint64)(IR) << 27) |
                                        ((uint64)(AC) << 23) | 
                                        (uint64)(AB);
                                AB = 040;
                                Mem_write(0);
                                AB += 1;
                                f_load_pc = 0;
                                */
                           } else {     
                                reason = STOP_HALT;
                           }
                        }
                        /* JRSTF */
                        if (AC & 02) {
                           FLAGS &= ~(OVR|NODIV|FLTUND|BYTI|FLTOVR|CRY1|CRY0);
                           /* If executive mode, copy USER and UIO */
                           if ((FLAGS & USER) == 0) 
                              FLAGS |= (AR >> 23) & (USER|USERIO);
                           /* Can always clear UIO */
                           if (((AR >> 23) & 0100) == 0) 
                              FLAGS &= ~USERIO;
                           FLAGS |= (AR >> 23) & (OVR|NODIV|FLTUND|BYTI|FLTOVR|CRY1|CRY0);
                           check_apr_irq();

                        }
                        if (AC & 01) {  /* Enter User Mode */
                           FLAGS |= USER; 
#if KI
                           FLAGS &= ~PUBLIC;
#endif
                        }
                        f_pc_inh = 1;
                        break;
                case 5: /* JFCL */      
                        if ((FLAGS >> 9) & AC) {
                            PC = AR;
                            f_pc_inh = 1;
                        }
                        FLAGS &=  017777 ^ (AC << 9);
                        break;
                case 6: /* XCT */ 
                        f_load_pc = 0;
                        f_pc_inh = 1;
                        break;  

                case 7:  /* MAP */
#if KI
#endif
                        break;
                }
                break;
        case 0260:      /* Stack, JUMP */
                switch(IR & 07) {
                case 0: /* PUSHJ */     /* AR Frm PC */
                        BR = AB;
                        AD = get_reg(AC);
#if KI
                        AD = ((AD + 1) & RMASK) |
                                ((AD + 01000000) & (C1|LMASK));
#else
                        AD = AD + 01000001LL;
#endif
                        AB = AD & RMASK;
                        if (AD & C1) {
                           push_ovf = 1;
#if KI
                           FLAGS |= TRP2;
#else
                           check_apr_irq();
#endif
                        }
                        set_reg(AC, AD & FMASK);
                        AR = ((uint64)(FLAGS) << 23) | ((PC + !pi_cycle) & RMASK);
                        FLAGS &= ~ 0434;
                        if (uuo_cycle | pi_cycle) {
                           FLAGS &= ~USER; /* Clear USER */
                        }
                        MB = AR;
                        Mem_write(uuo_cycle | pi_cycle);
                        PC = BR & RMASK;
                        f_pc_inh = 1;
                        break;
                case 1: /* PUSH */
                        BR = AR;
                        AD = get_reg(AC);
#if KI
                        AD = ((AD + 1) & RMASK) |
                              ((AD + 01000000) & (C1|LMASK));
#else
                        AD = AD + 01000001LL;
#endif
                        AB = AD & RMASK;
                        if (AD & C1) {
                           push_ovf = 1;
#if KI
                           FLAGS |= TRP2;
#else
                           check_apr_irq();
#endif
                        }
                        set_reg(AC, AD & FMASK);
                        MB = BR;
                        Mem_write(0);
                        break;
                case 2: /* POP */
                        BR = AR;
                        AD = get_reg(AC);
                        AB = AD & RMASK;
                        if (Mem_read(0))
                                break;
#if KI
                        AD = ((AD + RMASK) & RMASK) |
                                ((AD + LMASK) & (C1|LMASK));
#else
                        AD = AD + 0777776777777LL;
#endif
                        AB = BR;
                        if (Mem_write(0))
                                break;
                        if ((AD & C1) == 0) {
                           push_ovf = 1;
#if KI
                           FLAGS |= TRP2;
#else
                           check_apr_irq();
#endif
                        }
                        set_reg(AC, AD & FMASK);
                        break;
                case 3: /* POPJ */
                        BR = AB;
                        AD = get_reg(AC);
                        AB = AD & RMASK;
                        if (Mem_read(0))
                                break;
                        PC = MB & RMASK;
#if KI
                        AD = ((AD + RMASK) & RMASK) |
                              ((AD + LMASK) & (C1|LMASK));
#else
                        AD = AD + 0777776777777LL;
#endif
                        if ((AD & C1) == 0) {
                           push_ovf = 1;
#if KI
                           FLAGS |= TRP2;
#else
                           check_apr_irq();
#endif
                        }
                        set_reg(AC, AD & FMASK);
                        f_pc_inh = 1;
                        break;
                case 4: /* JSR */       /* AR Frm PC */
                        AD = ((uint64)(FLAGS) << 23) | 
                                ((PC + !pi_cycle) & RMASK);
                        FLAGS &= ~ 0434;
                        if (uuo_cycle | pi_cycle) {
                           FLAGS &= ~USER; /* Clear USER */
                        }
                        PC = (AR + pi_cycle) & RMASK;
                        AR = AD;
                        break;
                case 5: /* JSP */       /* AR Frm PC */
                        AD = ((uint64)(FLAGS) << 23) | 
                                ((PC + !pi_cycle) & RMASK);
                        FLAGS &= ~ 0434;
                        if (uuo_cycle | pi_cycle) {
                           FLAGS &= ~USER; /* Clear USER */
                        }
                        PC = AR & RMASK;
                        AR = AD;
                        f_pc_inh = 1;
                        break;
                case 6: /* JSA */       /* AR Frm PC */
                        BR = get_reg(AC);
                        set_reg(AC, (AR << 18) | ((PC + 1) & RMASK));
                        if (uuo_cycle | pi_cycle) {
                           FLAGS &= ~USER; /* Clear USER */
                        }
                        PC = AR & RMASK;
                        AR = BR;
                        break;
                case 7: /* JRA */
                        AD = AB;        /* Not in hardware */
                        AB = (get_reg(AC) >> 18) & RMASK;
                        if (Mem_read(uuo_cycle | pi_cycle))
                             break;
                        set_reg(AC, MB);
                        PC = AD & RMASK;
                        f_pc_inh = 1;
                        break;
                }
                break;
        case 0270:      /* ADD, SUB */
                AD = get_reg(AC);
                if (IR & 04) {
                        int t1,t2;
                        t1 = t2 = 0;
                        FLAGS &= 01777;
                        if ((((AR & CMASK) ^ CMASK) + (AD & CMASK) + 1) & SMASK) {
                            FLAGS |= CRY1;
                            t1 = 1;
                        }
                        AD = CM(AR) + AD + 1;
                        if (AD & C1) {
                            FLAGS |= CRY0;
                            t2 = 1;
                        }
                        if (t1 != t2) {
                            FLAGS |= OVR;
                            check_apr_irq();
                        }
                } else {
                        int t1,t2;
                        t1 = t2 = 0;
                        FLAGS &= 01777;
                        if (((AR & CMASK) + (AD & CMASK)) & SMASK) {
                            FLAGS |= CRY1;
                            t1 = 1;
                        }
                        AD = AR + AD;
                        if (AD & C1) {
                            FLAGS |= CRY0;
                            t2 = 1;
                        }
                        if (t1 != t2) {
                            FLAGS |= OVR;
                            check_apr_irq();
                        }
                }
                AD &= FMASK;
                AR = AD;
                break;
        case 0300: /* SKIP */   /* CAM */
        case 0310:              /* CAI */
                f = 0;
                AD = (CM(AR) + get_reg(AC)) + 1;
                if (((get_reg(AC) & SMASK) != 0) && (AR & SMASK) == 0)
                   f = 1;
                if (((get_reg(AC) & SMASK) == (AR & SMASK)) &&
                        (AD & SMASK) != 0)
                   f = 1;
                goto skip_op;
        case 0320:      /* JUMP */
        case 0330:      /* SKIP */
                AD = AR;
                f = ((AD & SMASK) != 0);
                goto skip_op;                   /* JUMP, SKIP */
        case 0340:      /* AOJ */
        case 0350:      /* AOS */
        case 0360:      /* SOJ */
        case 0370:      /* SOS */
                {
                int t1,t2;
                t1 = t2 = 0;
                FLAGS &= 01777;
                AD = (IR & 020) ? FMASK : 1;
                if (((AR & CMASK) + (AD & CMASK)) & SMASK) {
                    FLAGS |= CRY1;
                    t1 = 1;
                }
                AD = AR + AD;
                if (AD & C1) {
                    FLAGS |= CRY0;
                    t2 = 1;
                }
                if (t1 != t2) {
                    FLAGS |= OVR;
                    check_apr_irq();
                }
                }
                f = ((AD & SMASK) != 0);
skip_op:
                AD &= FMASK; 
                AR = AD;
                f |= ((AD == 0) << 1);
                f = f & IR;
                if (((IR & 04) != 0) == (f == 0)) {
                        switch(IR & 070) {
                        case 000:
                        case 010:       
                        case 030:
                        case 050:
                        case 070:
                                PC = (PC + 1) & RMASK; break;
                        case 020:
                        case 040:
                        case 060:
                                PC = AB;
                                f_pc_inh = 1;
                                break;
                        }
                }
                break;
        case 0400: /* Bool */
        case 0410:
        case 0420:
        case 0430:
        case 0440:
        case 0450:
        case 0460:
        case 0470:
                BR = get_reg(AC);
                switch ((IR >> 2) & 017) {
                case 0: AR = 0; break;                  /* SETZ */
                case 1: AR = AR & BR; break;    /* AND */
                case 2: AR = AR & CM(BR); break;        /* ANDCA */
                case 3: break;                  /* SETM */
                case 4: AR = CM(AR) & BR; break;        /* ANDCM */
                case 5: AR = BR; break;                 /* SETA */
                case 6: AR = AR ^ BR; break;    /* XOR */
                case 7: AR = CM(CM(AR) & CM(BR)); break; /* IOR */
                case 8: AR = CM(AR) & CM(BR); break; /* ANDCB */
                case 9: AR = CM(AR ^ BR); break;        /* EQV */
                case 10: AR = CM(BR); break;    /* SETCA */
                case 11: AR = CM(CM(AR) & BR); break; /* ORCA */
                case 12: AR = CM(AR); break;            /* SETCM */
                case 13: AR = CM(AR & CM(BR)); break; /* ORCM */
                case 14: AR = CM(AR & BR); break;       /* ORCB */
                case 15: AR = FMASK; break;             /* SETO */
                }
                break;
        case 0500: /* HWT */
        case 0510:
        case 0520:
        case 0530:
        case 0540:
        case 0550:
        case 0560:
        case 0570:
                switch(IR & 03) {
                case 0:                 /* Blank */
                case 1:                 /* I */
                        BR = get_reg(AC);
                        break;
                case 2:                 /* M */
                        AR = get_reg(AC);
                case 3:                 /* S & M */
                        BR = MB;
                        break;
                }
                
                if ((IR & 04))
                    AR = SWAP_AR;

                switch (IR & 030) {
                case 000:   AD = BR; break;
                case 010:   AD = 0; break;
                case 020:   AD = FMASK; break;
                case 030:   AD = AR;
                            AD &= (IR & 040) ? LSIGN : SMASK;
                            if (AD != 0)
                                AD = FMASK;
                            break; 
                }
                if (IR & 040) 
                     AD = (AD & LMASK) | (AR & RMASK);
                else
                     AD = (AR & LMASK) | (AD & RMASK);
                AR = AD;
                break;                                                                                     
                    
        case 0600: /* Txx */
        case 0610:
                if (IR & 01)
                   AR = SWAP_AR; 
                BR = AR;                /* N */
                goto test_op;
        case 0620:
        case 0630:
                if (IR & 01)
                   AR = SWAP_AR; 
                BR = CM(AR) & get_reg(AC);  /* Z */
                goto test_op;
        case 0640:
        case 0650:
                if (IR & 01)
                   AR = SWAP_AR; 
                BR = AR ^ get_reg(AC);  /* C */
                goto test_op;
        case 0660:
        case 0670:
                if (IR & 01)
                   AR = SWAP_AR; 
                BR = AR | get_reg(AC);  /* O */
test_op:
                AR &= get_reg(AC);
                f = ((AR == 0) & ((IR >> 1) & 1)) ^ ((IR >> 2) & 1);
                if (f)
                    PC = (PC + 1) & RMASK;
                if ((IR & 060) != 0)
                    set_reg(AC, BR);
                break;
        case 0700: /* IOT */
        case 0710:
        case 0720:
        case 0730:
        case 0740:
        case 0750:
        case 0760:
        case 0770:
                if ((FLAGS & (USER|USERIO)) == USER && !pi_cycle) {
                                /* User and not User I/O */
                    goto muuo;
                    /*
                    MB = ((uint64)(IR) << 27) | ((uint64)(AC) << 23) | 
                                (uint64)(AB);
                    AB = 040;
                    Mem_write(1);
                    AB += 1;
                    uuo_cycle = 1;
                    f_load_pc = 0;
                    f_pc_inh = 1;
                    */
                    break;
                } else {
                    int d = ((IR & 077) << 1) | ((AC & 010) != 0);
                    switch(AC & 07) {
                    case 0: /* 00 BLKI */
                    case 2: /* 10 BLKO */
                            if (Mem_read(pi_cycle)) 
                                break;
                            AR = MB;
                            if (hst_lnt) {
                                    hst[hst_p].mb = AR;
                            }
                            AC |= 1;    /* Make into DATAI/DATAO */
                            f_load_pc = 0;
                            f_inst_fetch = 0;
                            AR = (AR + 01000001LL);
                            if (AR & C1) {
                                pi_ov = f_pc_inh = 1;
                            } else if (!pi_cycle) {
                                PC = (PC + 1) & RMASK; 
                            }
                            MB = AR & FMASK;
                            if (Mem_write(pi_cycle))
                                break;
                            AB = AR & RMASK;
                            goto fetch_opr;
                            break;
                    case 1:     /* 04 DATAI */
                //          if (dev_tab[d] != 0)
                                dev_tab[d](DATAI|(d<<2), &AR);
                //          else
                //              AR = 0;
                            MB = AR;
                            Mem_write(pi_cycle);
                            break;
                    case 3:     /* 14 DATAO */
                        if (Mem_read(pi_cycle)) 
                           break;
                        AR = MB;
                //      if (dev_tab[d] != 0)
                            dev_tab[d](DATAO|(d<<2), &AR);
                        break;
                    case 4:     /* 20 CONO */
                //      if (dev_tab[d] != 0) 
                            dev_tab[d](CONO|(d<<2), &AR);
                        break;
                    case 5: /* 24 CONI */
                    case 6: /* 30 CONSZ */
                    case 7: /* 34 CONSO */
                //      if (dev_tab[d] != 0) 
                            dev_tab[d](CONI|(d<<2), &AR);
                //      else
                //          AR = 0;
                        if (AC & 2) {
                            AR &= AB;
                            if ((AR != 0) == (AC & 1))
                                PC = (PC + 1) & RMASK;
                        } else {
                            MB = AR;
                            Mem_write(pi_cycle);
                        }
                        break;
                    }
                }
                break;
        }
        if (!sac_inh && (i_flags & (SCE|FCEPSE))) {
             MB = AR;
             if (Mem_write(0)) 
                goto last;
        } 
        if (!sac_inh && ((i_flags & SAC) || ((i_flags & SACZ) && AC != 0)))
            set_reg(AC, AR);    /* blank, I, B */

        if (!sac_inh && (i_flags & SAC2))
            set_reg(AC+1, MQ);  

        if (hst_lnt) {
            hst[hst_p].fmb = AR;
        }
             

last:
        if (!f_pc_inh & !pi_cycle) {
                PC = (PC + 1) & RMASK;
        }

        if (pi_cycle) {
           if ((IR & 0700) == 0700 && ((AC & 04) == 0)) {
               pi_hold = pi_ov;
               if (!pi_hold & f_inst_fetch) {
                    pi_restore = 1;
                    pi_cycle = 0;
               } else {
                    AB = 040 | (pi_enc << 1) | pi_ov;
                    pi_ov = 0;
                    pi_hold = 0;
                    goto fetch;
               }
           } else if (pi_hold) {
                AB = 040 | (pi_enc << 1) | pi_ov;
                pi_ov = 0;
                pi_hold = 0;
                goto fetch;
           } else {
                f_inst_fetch = 1;
                f_load_pc = 1;
                pi_cycle = 0;
           }
        }

        if (pi_restore) {
            restore_pi_hold();
            pi_restore = 0;
        }
        sim_interval--;
      }
/* Should never get here */

     return reason;
}

t_stat
rtc_srv(UNIT * uptr)
{ 
    int32 t;
    t = sim_rtcn_calb (rtc_tps, TMR_RTC);
    sim_activate_after(uptr, 1000000/rtc_tps);
    tmxr_poll = t/2;
    clk_flg = 1;
    if (clk_en) {
        set_interrupt(4, clk_irq);
    }
#if 0
    static uint32 last_ms = 0;
    static int milli_time = 0;
    static time_t last_sec = 0;
    int     ms = sim_os_msec();
    uint32  diff = ms - last_ms;
    time_t  nt;

    time(&nt);
    if (nt != last_sec) {
        //    fprintf(stderr, "%d clocks per second\n\r", milli_time);
        milli_time = 0;
        last_sec = nt;
    }
    while (diff > 16) {
       /* Stop updating it over 60 in this second */
       if (milli_time > 60) {
            last_ms = ms;
            diff = 0;
            break;
       }
       clk_flg = 1;
       if (clk_en) {
           set_interrupt(4, clk_irq);
        }
       diff -= 16;
       last_ms = ms;
       milli_time += 1;
    }
    sim_activate(&cpu_unit, 100000);
#endif
    return SCPE_OK;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
int     i;
BYF5 = uuo_cycle = 0;
Pl = Ph = Rl = Rh = Pflag = 0;
push_ovf = mem_prot = nxm_flag = clk_flg = 0;
PIR = PIH = PIE = pi_enable = parity_irq = 0;
pi_pending = pi_req = pi_enc = apr_irq = clk_pri = 0;
ov_irq =fov_irq =clk_en =clk_irq = xctf = 0;
pi_restore = pi_hold = 0;
#if KI
ub_ptr = eb_ptr = 0;
pag_reload = ac_stack = 0;
fm_blk = fm_sel = small_user = user_addr_cmp = page_enable = reg_stack = 0;
#endif
for(i=0; i < 128; dev_irq[i++] = 0);
sim_brk_types = sim_brk_dflt = SWMASK ('E');
sim_rtcn_init (cpu_unit.wait, TMR_RTC);
sim_activate(&cpu_unit, cpu_unit.wait);
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr ea, UNIT *uptr, int32 sw)
{
if (vptr == NULL)
    return SCPE_ARG;
if (ea < 020)
    *vptr = FM[ea] & FMASK;
else {
    if (sw & SWMASK ('V')) {
        if (ea >= MAXMEMSIZE)
            return SCPE_REL;
        }
    if (ea >= MEMSIZE)
        return SCPE_NXM;
    *vptr = M[ea] & FMASK;
    }
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr ea, UNIT *uptr, int32 sw)
{
if (ea < 020)
    FM[ea] = val & FMASK;
else {
    if (sw & SWMASK ('V')) {
        if (ea >= MAXMEMSIZE)
            return SCPE_REL;
        }
    if (ea >= MEMSIZE)
        return SCPE_NXM;
    M[ea] = val & FMASK;
    }
return SCPE_OK;
}

/* Memory size change */

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint64 mc = 0;
uint32 i;

if ((val <= 0) || ((val * 1024) > MAXMEMSIZE))
    return SCPE_ARG;
for (i = val; i < MEMSIZE; i++)
    mc = mc | M[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
    return SCPE_OK;
MEMSIZE = val * 16 * 1024;
for (i = MEMSIZE; i < MAXMEMSIZE; i++)
    M[i] = 0;
return SCPE_OK;
}

/* Build device dispatch table */
t_bool build_dev_tab (void) 
{
DEVICE *dptr;
DIB *dibp;
uint32 i, j;

for (i = 0; i < 128; i++) 
    dev_tab[i] = &null_dev;
dev_tab[0] = &dev_apr;
dev_tab[1] = &dev_pi;
#if KI
dev_tab[2] = &dev_pag;
#endif
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
    dibp = (DIB *) dptr->ctxt;
    if (dibp && !(dptr->flags & DEV_DIS)) {             /* enabled? */
         for (j = 0; j < dibp->num_devs; j++) {         /* loop thru disp */
              if (dibp->io) {                           /* any dispatch? */
                   if (dev_tab[(dibp->dev_num >> 2) + j] != &null_dev) {
                                                       /* already filled? */
                           printf ("%s device number conflict at %02o\n",
                              sim_dname (dptr), dibp->dev_num + j << 2);
                      if (sim_log)
                        fprintf (sim_log, "%s device number conflict at %02o\n",
                                   sim_dname (dptr), dibp->dev_num + j << 2);
                       return TRUE;
                   }
              dev_tab[(dibp->dev_num >> 2) + j] = dibp->io;  /* fill */
             }                                       /* end if dsp */
           }                                           /* end for j */
        }                                               /* end if enb */
    }                                                   /* end for i */
return FALSE;
}


/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 i, lnt;
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++)
        hst[i].pc = 0;
    hst_p = 0;
    return SCPE_OK;
    }
lnt = (int32) get_uint (cptr, 10, HIST_MAX, &r);
if ((r != SCPE_OK) || (lnt && (lnt < HIST_MIN)))
    return SCPE_ARG;
hst_p = 0;
if (hst_lnt) {
    free (hst);
    hst_lnt = 0;
    hst = NULL;
    }
if (lnt) {
    hst = (InstHistory *) calloc (lnt, sizeof (InstHistory));
    if (hst == NULL)
        return SCPE_MEM;
    hst_lnt = lnt;
    }
return SCPE_OK;
}

/* Show history */

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 k, di, lnt;
char *cptr = (char *) desc;
t_stat r;
int reg;
t_value sim_eval;
InstHistory *h;
//extern t_stat fprint_sym (FILE *ofile, t_addr addr, t_value *val,
 //   UNIT *uptr, int32 sw);

if (hst_lnt == 0)                                       /* enabled? */
    return SCPE_NOFNC;
if (cptr) {
    lnt = (int32) get_uint (cptr, 10, hst_lnt, &r);
    if ((r != SCPE_OK) || (lnt == 0))
        return SCPE_ARG;
    }
else lnt = hst_lnt;
di = hst_p - lnt;                                       /* work forward */
if (di < 0)
    di = di + hst_lnt;
fprintf (st, "PC      AC            EA      FLAGS IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    if (h->pc & HIST_PC) {                              /* instruction? */
        fprintf (st, "%06o  ", (uint32)(h->pc & RMASK));
        fprint_val (st, h->ac, 8, 36, PV_RZRO);
        fputs ("  ", st);
        fprintf (st, "%06o  ", h->ea);
        fputs ("  ", st);
        fprint_val (st, h->mb, 8, 36, PV_RZRO);
        fputs ("  ", st);
        fprint_val (st, h->fmb, 8, 36, PV_RZRO);
        fputs ("  ", st);
        fprintf (st, "%06o  ", h->flags);
        sim_eval = h->ir;
        if ((fprint_sym (st, h->pc & RMASK, &sim_eval, &cpu_unit, SWMASK ('M'))) > 0) {
            fputs ("(undefined) ", st);
            fprint_val (st, h->ir, 8, 36, PV_RZRO);
            }
//      for(reg = 0; reg < 020; reg++) {
//         fputc (' ', st);
//         fprint_val (st, h->fm[reg], 8, 36, PV_RZRO);
//      }
        fputc ('\n', st);                               /* end line */
        }                                               /* end else instruction */
    }                                                   /* end for */
return SCPE_OK;
}

t_stat
cpu_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
#if !KI10
    fprintf(st, "KA10 CPU\n\n");
#else
    fprintf(st, "KI10 CPU\n\n");
#endif
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *
cpu_description (DEVICE *dptr) 
{
#if !KI10
    return "KA10 CPU";
#else
    return "KI10 CPU";
#endif
}
