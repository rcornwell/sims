/* ka10_defs.h: PDP-10 simulator definitions

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
   in this Software without prior written authorization from Richard Cornwell.

*/

#ifndef _KA10_DEFS_H_
#define _KA10_DEFS_H_  0

#include "sim_defs.h"                                   /* simulator defns */

#if defined(USE_ADDR64)
#error "PDP-10 does not support 64b addresses!"
#endif

#ifndef KI
#define KI 0
#endif

#ifndef KA
#define KA 0
#endif

#ifndef KI_22BIT
#define KI_22BIT 0
#endif

/* Digital Equipment Corporation's 36b family had six implementations:

   name         mips    comments

   PDP-6        0.25    Original 36b implementation, 1964
   KA10         0.38    First PDP-10, flip chips, 1967
   KI10         0.72    First paging system, flip chip + MSI, 1972
   KL10         1.8     First ECL system, ECL 10K, 1975
   KL10B        1.8     Expanded addressing, ECL 10K, 1978
   KS10         0.3     Last 36b system, 2901 based, 1979

   In addition, it ran four major (incompatible) operating systems:

   name         company comments

   TOPS-10      DEC     Original timesharing system
   ITS          MIT     "Incompatible Timesharing System"
   TENEX        BBN     ARPA-sponsored, became
   TOPS-20      DEC     Commercial version of TENEX

   All of the implementations differ from one another, in instruction set,
   I/O structure, and memory management.  Further, each of the operating
   systems customized the microcode of the paging systems (KI10, KL10, KS10)
   for additional instructions and specialized memory management.  As a
   result, there is no "reference implementation" for the 36b family that
   will run all programs and all operating systems.  The conditionalization
   and generality needed to support the full matrix of models and operating
   systems, and to support 36b hardware on 32b data types, is beyond the
   scope of this project.

*/

/* Abort codes, used to sort out longjmp's back to the main loop
   Codes > 0 are simulator stop codes
   Codes < 0 are internal aborts
   Code  = 0 stops execution for an interrupt check
*/

#define STOP_HALT       1                               /* halted */
#define STOP_IBKPT      2                               /* breakpoint */

/* Debuging controls */
#define DEBUG_CMD       0x0000001       /* Show device commands */
#define DEBUG_DATA      0x0000002       /* Show data transfers */
#define DEBUG_DETAIL    0x0000004       /* Show details */
#define DEBUG_EXP       0x0000008       /* Show error conditions */
#define DEBUG_CONI      0x0000010       /* Show CONI instructions */
#define DEBUG_CONO      0x0000020       /* Show CONO instructions */
#define DEBUG_DATAIO    0x0000040       /* Show DATAI/O instructions */
#define DEBUG_IRQ       0x0000080       /* Show IRQ requests */

extern DEBTAB dev_debug[];

/* Operating system flags, kept in cpu_unit.flags */

#define Q_IDLE          (sim_idle_enab)

/* Device information block */
#define LMASK   00777777000000LL
#define RMASK   00000000777777LL
#define FMASK   00777777777777LL
#define CMASK   00377777777777LL
#define SMASK   00400000000000LL
#define C1      01000000000000LL
#define LSIGN   00000000400000LL
#define PMASK   00007777777777LL
#define XMASK   03777777777777LL
#define EMASK   00777000000000LL
#define MMASK   00000777777777LL
#define BIT1    00200000000000LL
#define BIT8    00001000000000LL
#define BIT9    00000400000000LL
#define BIT10_35 0000377777777LL
#define MANT    00000777777777LL
#define EXPO    00377000000000LL
#define DFMASK  01777777777777777777777LL
#define DSMASK  01000000000000000000000LL
#define DCMASK   0777777777777777777777LL
#define DNMASK   0400000000000000000000LL
#define DXMASK   0200000000000000000000LL
#define FPSMASK   040000000000000000000LL
#define FPNMASK    01000000000000000000LL
#define FPFMASK   077777777777777777777LL
#define FPCMASK   000777777777777777777LL

#define CM(x)   (FMASK ^ (x))

#define INST_V_OP       27                              /* opcode */
#define INST_M_OP       0777
#define INST_V_DEV      26
#define INST_M_DEV      0177                            /* device */
#define INST_V_AC       23                              /* AC */
#define INST_M_AC       017
#define INST_V_IND      22                              /* indirect */
#define INST_IND        (1 << INST_V_IND)
#define INST_V_XR       18                              /* index */
#define INST_M_XR       017
#define OP_JRST         0254                            /* JRST */
#define AC_XPCW         07                              /* XPCW */
#define OP_JSR          0264                            /* JSR */
#define GET_OP(x)       ((int32) (((x) >> INST_V_OP) & INST_M_OP))
#define GET_DEV(x)      ((int32) (((x) >> INST_V_DEV) & INST_M_DEV))
#define GET_AC(x)       ((int32) (((x) >> INST_V_AC) & INST_M_AC))
#define TST_IND(x)      ((x) & INST_IND)
#define GET_XR(x)       ((int32) (((x) >> INST_V_XR) & INST_M_XR))
#define GET_ADDR(x)     ((uint32) ((x) & RMASK))
#define LRZ(x)          (((x) >> 18) & RMASK)


#define NODIV   000001
#define FLTUND  000002
#if KI
#define TRP1    000004
#define TRP2    000010
#define ADRFLT  000020
#define PUBLIC  000040
#else
#define TRP1    000000
#define TRP2    000000
#define ADRFLT  000000
#define PUBLIC  000000
#endif
#define USERIO  000100
#define USER    000200
#define BYTI    000400
#define FLTOVR  001000
#define CRY1    002000
#define CRY0    004000
#define OVR     010000

#define DATAI   00
#define DATAO   01
#define CONI    02
#define CONO    03

#define CTY_SWITCH      030

#if KI_22BIT
#define MAXMEMSIZE      4096 * 1024
#else
#define MAXMEMSIZE      256 * 1024
#endif
#define MEMSIZE         (cpu_unit.capac)

#define ICWA            0000000000776
#if KI_22BIT
#define AMASK           0000037777777
#define WMASK           017777
#define CSHIFT          22
#else
#define AMASK           RMASK
#define WMASK           RMASK
#define CSHIFT          18
#endif

#define API_MASK        0000000007
#define PI_ENABLE       0000000010      /* Clear DONE */
#define BUSY            0000000020      /* STOP */
#define CCW_COMP        0000000040      /* Write Final CCW */

#if KI
#define DEF_SERIAL      514             /* Default dec test machine */
#endif


typedef unsigned long long int uint64;
typedef unsigned int uint18;

extern uint64   M[]; 
extern uint18   PC;
extern uint32   FLAGS;


extern void set_interrupt(int dev, int lvl);
extern void clr_interrupt(int dev);
extern void check_apr_irq();
extern int check_irq_level();
extern void restore_pi_hold();
extern void set_pi_hold();
extern UNIT     cpu_unit;
extern DEVICE   cpu_dev;
extern DEVICE   cty_dev;
extern DEVICE   mt_dev;
extern DEVICE   dpa_dev;
extern DEVICE   dpb_dev;
extern DEVICE   dpc_dev;
extern DEVICE   dpd_dev;
extern DEVICE   rpa_dev;
extern DEVICE   rpb_dev;
extern DEVICE   rpc_dev;
extern DEVICE   rpd_dev;
extern DEVICE   lpt_dev;
extern DEVICE   ptp_dev;
extern DEVICE   ptr_dev;
extern DEVICE   rca_dev;
extern DEVICE   rcb_dev;
extern DEVICE   dc_dev;
extern DEVICE   dt_dev;
extern DEVICE   dk_dev;

extern t_stat (*dev_tab[128])(uint32 dev, uint64 *data);

#define VEC_DEVMAX      8                               /* max device vec */

struct pdp_dib {
    int                 dev_num;                        /* device address */
    int                 num_devs;                       /* length */
    t_stat              (*io)(uint32 dev, uint64 *data);
};

typedef struct pdp_dib DIB;


/* DF10 Interface */
struct df10 {
        uint32  status;
        uint32  cia;
        uint32  ccw;
        uint32  wcr;
        uint32  cda;
        uint32  devnum;
        uint64  buf;
        uint8   nxmerr;
        uint8   ccw_comp;
} ;


void df10_setirq(struct df10 *df) ;
void df10_writecw(struct df10 *df) ;
void df10_finish_op(struct df10 *df, int flags) ;
void df10_setup(struct df10 *df, uint32 addr);
int  df10_fetch(struct df10 *df);
int  df10_read(struct df10 *df);
int  df10_write(struct df10 *df);


/* I/O system parameters */
#define NUM_DEVS_MT     1
#define NUM_DEVS_DP     2
#define NUM_DEVS_LP     1
#define NUM_DEVS_PT     1
#define NUM_DEVS_DC     1
#define NUM_DEVS_RC     1
#define NUM_DEVS_DT     1
#define NUM_DEVS_DK     1
#define NUM_DEVS_RP     1
#define NUM_DEVS_TU     1
/* Global data */

extern t_bool sim_idle_enab;

#endif


