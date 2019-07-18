/* sel32_sys.c: SEL 32 Gould Concept/32 (orignal SEL32) Simulator system interface.

   Copyright (c) 2018, Richard Cornwell

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

*/

#include "sel32_defs.h"
#include <ctype.h>

extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern uint32 M[MAXMEMSIZE];

/* SCP data structures and interface routines

   sim_name            simulator name string
   sim_PC              pointer to saved PC register descriptor
   sim_emax            number of words for examine
   sim_devices         array of pointers to simulated devices
   sim_stop_messages   array of pointers to stop messages
   sim_load            binary loader
*/

char sim_name[] = "SEL 32";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 16;

DEVICE *sim_devices[] = {
       &cpu_dev,
#ifdef NUM_DEVS_CON
        &con_dev,
#endif
#ifdef NUM_DEVS_CDR
        &cdr_dev,
#endif
#ifdef NUM_DEVS_CDP
        &cdp_dev,
#endif
#ifdef NUM_DEVS_LPR
        &lpr_dev,
#endif
#ifdef NUM_DEVS_MT
        &mta_dev,
#if NUM_DEVS_MT > 1
        &mtb_dev,
#endif
#endif
#ifdef NUM_DEVS_DASD
        &dda_dev,
#if NUM_DEVS_DASD > 1
        &ddb_dev,
#endif
#endif
#ifdef NUM_DEVS_COM
        &com_dev,
#endif
       NULL };


/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"INST", DEBUG_INST, "Show instruction execution"},
    {0, 0}
};


const char *sim_stop_messages[] = {
        };



/* Load a card image file into memory.  */

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
return SCPE_NOFNC;
}

/* Symbol tables */

/*
 * The SEL 32 supports the following instruction formats.
 * 
 * TYPE         Format   Normal          Base Mode
 *  A           ADR      d,[*]o,x        d,o[(b)],x  FC = extra
 *  B           BRA      [*]o,x          o[(b)],x
 *  C           IMM      d,o             d,o
 *  D           BIT      d,[*]o,x        d,o[(b)],x
 *  E           ADR      [*]o,x          o[(b)],x  FC = extra
 *  F           REG      s,d             s,d           Half Word
 *  G           RG1      s               s
 *  H           HLF
 *  I           SHF      d,v             d,v
 *  K           RBT      d,b             d,b
 *  L           EXR      s               s
 *  M           IOP      n,b             n,b  
 */

#define TYPE_A          0
#define TYPE_B          1 
#define TYPE_C          2
#define TYPE_D          3 
#define TYPE_E          4
#define TYPE_F          5 
#define TYPE_G          6
#define TYPE_H          7 
#define TYPE_I          8 
#define TYPE_K          9 
#define TYPE_L          10 
#define TYPE_M          11 
#define H               0x10

typedef struct _opcode {
       uint16   opbase;
       uint16   mask;
       uint8    type;
       char     *name;
} t_opcode;


t_opcode  optab[] {
    {  OP_HALT,      0xFFFF,   H|TYPE_H,   "HALT", },     /* Halt # * */
    {  OP_WAIT,      0xFFFF,   H|TYPE_H,   "WAIT", },     /* Wait # * */
    {  OP_NOP,       0xFFFF,   H|TYPE_H,   "NOP", },      /* Nop # */
    {  OP_LCS,       0xFFFF,   H|TYPE_G,   "LCS", },      /* Load Control Switches */
    {  OP_ES,        0xFC0F,   H|TYPE_G,   "ES", },       /* Extend Sign # */
    {  OP_SIPU,      0xFFFF,   H|TYPE_H,   "SIPU", },     /* Signal IPU # */
    {  OP_RND,       0xFC0F,   H|TYPE_G,   "RND", },      /* Round Register # */
    {  OP_BEI,       0xFC0F,   H|TYPE_H,   "BEI", },      /* Block External Interrupts # */
    {  OP_UEI,       0xFC0F,   H|TYPE_H,   "UEI", },      /* Unblock External Interrupts # */
    {  OP_EAE,       0xFC0F,   H|TYPE_H,   "EAE", },      /* Enable Arithmetic Exception Trap # */
    {  OP_RDSTS,     0xFC0F,   H|TYPE_G,   "RDSTS", },    /* Read CPU Status Word * */
    {  OP_SEA,       0xFFFF,   H|TYPE_H,   "SEA", },      /* Set Extended Addressing # NBR */
    {  OP_DAE,       0xFC0F,   H|TYPE_H,   "DAE", },      /* Disable Arithmetic Exception Trap # */
    {  OP_CEA,       0xFFFF,   H|TYPE_H,   "CEA", },      /* Clear Extended Addressing # NBR */
    {  OP_CMC,       0xFC0F,   H|TYPE_G,   "CMC", },      /* Cache Memory Control # */
    {  OP_SMC,       0xFC0F,   H|TYPE_G,   "SMC", },      /* Shared Memory Control # */
    {  OP_ANR,       0xFC0F,   H|TYPE_F,   "ANR", },      /* And Register # */
    {  OP_RPSWT,     0xFC0F,   H|TYPE_G,   "RPSWT", },    /* Read Processor Status Word Two # */
    {  OP_ORR,       0xFC0F,   H|TYPE_F,   "ORR", },      /* Or Register # */
    {  OP_ORRM,      0xFC0F,   H|TYPE_F,   "ORRM", },     /* Or Register Masked # */
    {  OP_EOR,       0xFC0F,   H|TYPE_F,   "EOR", },      /* Exclusive Or Register # */
    {  OP_EORM,      0xFC0F,   H|TYPE_F,   "EORM", },     /* Exclusive Or Register Masked # */
    {  OP_CAR,       0xFC0F,   H|TYPE_F,   "CAR", },      /* Compare Register # */
    {  OP_CMR,       0xFC0F,   H|TYPE_F,   "CMR", },      /* Compare masked with register */
    {  OP_SACZ,      0xFC0F,   H|TYPE_F,   "SACZ", },     /* Shift and Count Zeros # BR */
    {  OP_SBR,       0xFC0F,   H|TYPE_K,   "SBR", },      /* Set Bit in Register # */
    {  OP_ZBR,       0xFC0F,   H|TYPE_K,   "ZBR", },      /* Zero Bit In register # BR */
    {  OP_ABR,       0xFC0F,   H|TYPE_K,   "ABR", },      /* Add Bit In Register # BR */
    {  OP_TBR,       0xFC0F,   H|TYPE_K,   "TBR", },      /* Test Bit in Register # BR */
    {  OP_SRABR,     0xFC0F,   H|TYPE_I,   "SRABR", },    /* Shift Right Arithmetic # BR */
    {  OP_SRLBR,     0xFC0F,   H|TYPE_I,   "SRLBR", },    /* Shift Right Logical # BR */
    {  OP_SLABR,     0xFC0F,   H|TYPE_I,   "SLABR", },    /* Shift Left Arithmetic # BR */
    {  OP_SLLBR,     0xFC0F,   H|TYPE_I,   "SLLBR", },    /* Shift Left Logical # BR */
    {  OP_SRADBR,    0xFC0F,   H|TYPE_I,   "SRADBR", },   /* Shift Right Arithmetic Double # BR */
    {  OP_SRLDBR,    0xFC0F,   H|TYPE_I,   "SRLDBR", },   /* Shift Left Logical Double # BR */
    {  OP_SLADBR,    0xFC0F,   H|TYPE_I,   "SLADBR", },   /* Shift Right Arithmetic Double # BR */
    {  OP_SLLDBR,    0xFC0F,   H|TYPE_I,   "SLLDBR", },   /* Shift Left Logical Double # BR */
    {  OP_SRCBR,     0xFC0F,   H|TYPE_I,   "SRCBR", },    /* Shift Right Circular # BR */
    {  OP_ZBR,       0xFC0F,   H|TYPE_K,   "ZBR", },      /* Zero Bit in Register # NBR */
    {  OP_ABR,       0xFC0F,   H|TYPE_K,   "ABR", },      /* Add Bit in Register # NBR */
    {  OP_TBR,       0xFC0F,   H|TYPE_K,   "TBR", },      /* Test Bit in Register # NBR */
    {  OP_TRSW,      0xFC0F,   H|TYPE_F,   "TRSW", },     /* Transfer GPR to PSD */
    {  OP_TRBR,      0xFC0F,   H|TYPE_F,   "TRBR", },     /* Transfer GPR to BR # BR */
    {  OP_XCBR,      0xFC0F,   H|TYPE_F,   "XCBR", },     /* Exchange Base Registers # BR */
    {  OP_TCCR,      0xFC0F,   H|TYPE_G,   "TCCR", },     /* Transfer CC to GPR # BR */
    {  OP_TRCC,      0xFC0F,   H|TYPE_G,   "TRCC", },     /* Transfer GPR to CC # BR */
    {  OP_BSUB,      0xFC0F,   H|TYPE_F,   "BSUB", },     /* Branch Subroutine # BR */
    {  OP_CALL,      0xFC0F,   H|TYPE_F,   "CALL", },     /* Procedure Call # BR */
    {  OP_TPCBR,     0xFC0F,   H|TYPE_G,   "TPCBR", },    /* Transfer Program Counter to Base # BR */
    {  OP_RETURN,    0xFC7F,   H|TYPE_G,   "RETURN", },   /* Procedure Return # BR */
    {  OP_TRR,       0xFC0F,   H|TYPE_F,   "TRR", },      /* Transfer Register to Register # */
    {  OP_TRDR,      0xFC0F,   H|TYPE_F,   "TRDR", },     /* Transfer GPR to BR # */
    {  OP_TBRR,      0xFC0F,   H|TYPE_A,   "TBRR", },     /* Transfer BR to GPR BR # */
    {  OP_TRC,       0xFC0F,   H|TYPE_F,   "TRC", },      /* Transfer Register Complement # */
    {  OP_TRN,       0xFC0F,   H|TYPE_F,   "TRN", },      /* Transfer Register Negative # */
    {  OP_XCR,       0xFC0F,   H|TYPE_F,   "XCR", },      /* Exchange Registers # */
    {  OP_LMAP,      0xFC0F,   H|TYPE_G,   "LMAP", },     /* Load MAP * */
    {  OP_TRRM,      0xFC0F,   H|TYPE_F,   "TRRM", },     /* Transfer Register to Register Masked # */
    {  OP_SETCPU,    0xFC0F,   H|TYPE_G,   "SETCPU", },   /* Set CPU Mode # * */
    {  OP_TMAPR,     0xFC0F,   H|TYPE_F,   "TMAPR", },    /* Transfer MAP to Register # * */
    {  OP_XCRM,      0xFC0F,   H|TYPE_F,   "XCRM", },     /* Exchange Registers Masked # */
    {  OP_TRCM,      0xFC0F,   H|TYPE_F,   "TRCM", },     /* Transfer Register Complement Masked # */
    {  OP_TRNM,      0xFC0F,   H|TYPE_F,   "TRNM", },     /* Transfer Register Negative Masked # */
    {  OP_TRSC,      0xFC0F,   H|TYPE_F,   "TRSC", },     /* Transfer Register to Scratchpad # * */
    {  OP_TSCR,      0xFC0F,   H|TYPE_F,   "TSCR", },     /* Transfer Scratchpad to Register # * */
    {  OP_CALM,      0xFC0F,   H|TYPE_F,   "CALM", },     /* Call Monitor # */
    {  OP_LA,        0xFC0F,   H|TYPE_F,   "LA", },       /* Load Address NBR */
    {  OP_ADR,       0xFC0F,   H|TYPE_F,   "ADR", },      /* Add Register to Register # */
    {  OP_ADRFW,     0xFC0F,   H|TYPE_F,   "ADRFW", },    /* Add Floating Point to Register # BR? */
    {  OP_MPRBR,     0xFC0F,   H|TYPE_F,   "MPRBR", },    /* Multiply Register BR # */
    {  OP_SURFW,     0xFC0F,   H|TYPE_F,   "SURFW", },    /* Subtract Floating Point Register BR? # */
    {  OP_DVRFW,     0xFC0F,   H|TYPE_F,   "DVRFW", },    /* Divide Floating Point Register BR? # */
    {  OP_FIXW,      0xFC0F,   H|TYPE_F,   "FIXW",        /* Fix Floating Point Register BR? # */
    {  OP_MPRFW,     0xFC0F,   H|TYPE_F,   "MPRFW", },    /* Multiply Floating Point Register BR? # */
    {  OP_FLTW,      0xFC0F,   H|TYPE_F,   "FLTW", },     /* Float Floating Point Register BR? # */
    {  OP_ADRM,      0xFC0F,   H|TYPE_F,   "ADRM",        /* Add Register to Register Masked # */
    {  OP_DVRBR,     0xFC0F,   H|TYPE_F,   "DVRBR",       /* Divide Register by Registier BR # */
    {  OP_SURFD,     0xFC0F,   H|TYPE_F,   "SURFD", },    /* Subtract Floating Point Double # BR? */
    {  OP_DVRFD,     0xFC0F,   H|TYPE_F,   "DVRFD", },    /* Divide Floating Point Double # BR? */
    {  OP_FIXD,      0xFC0F,   H|TYPE_F,   "FIXD", },     /* Fix Double Register # BR? */
    {  OP_MPRFD,     0xFC0F,   H|TYPE_F,   "MPRFD", },    /* Multiply Double Register # BR? */
    {  OP_FLTD,      0xFC0F,   H|TYPE_F,   "FLTD", },     /* Float Double # BR? */
    {  OP_SUR,       0xFC0F,   H|TYPE_F,   "SUR", },      /* Subtract Register to Register # */
    {  OP_SURM,      0xFC0F,   H|TYPE_F,   "SURM", },     /* Subtract Register to Register Masked # */
    {  OP_MPR,       0xFC0F,   H|TYPE_F,   "MPR", },      /* Multiply Register to Register # NBR */
    {  OP_DVR,       0xFC0F,   H|TYPE_F,   "DVR",         /* Divide Register to Register # NBR */
    {  OP_LA,        0xFC0F,   H|TYPE_F,   "LA", },       /* Load Address BR */
    {  OP_STWBR,     0xFC0F,   H|TYPE_F,   "STWBR", },    /* Store Base Register BR */
    {  OP_SUABR,     0xFC0F,   H|TYPE_F,   "SUABR", },    /* Subtract Base Register BR */
    {  OP_LABR,      0xFC0F,   H|TYPE_F,   "LABR", },     /* Load Address Base Register BR */
    {  OP_LWBR,      0xFC0F,   H|TYPE_F,   "WBR", },      /* Load Base Register BR */
    {  OP_BSUBM,     0xFC0F,   H|TYPE_F,   "BSUBM", },    /* Branch Subroutine Memory BR */
    {  OP_CALLM,     0xFC0F,   H|TYPE_F,   "CALLM", },    /* Call Memory BR */
    {  OP_NOR,       0xFC0F,   H|TYPE_F,   "NOR", },      /* Normalize # NBR */
    {  OP_NORD,      0xFC0F,   H|TYPE_F,   "NORD", },     /* Normalize Double #  NBR*/
    {  OP_SCZ,       0xFC0F,   H|TYPE_F,   "SCZ", },      /* Shift and Count Zeros # */
    {  OP_SRA,       0xFC0F,   H|TYPE_I,   "SRA", },      /* Shift Right Arithmetic # NBR */
    {  OP_SLA,       0xFC40,   H|TYPE_I,   "SLA", },      /* Shift Left Arithmetic # NBR */
    {  OP_SRL,       0xFC40,   H|TYPE_I,   "SRL", },      /* Shift Right Logical # NBR */
    {  OP_SLL,       0xFC40,   H|TYPE_I,   "SLL", },      /* Shift Left Logical # NBR */
    {  OP_SRC,       0xFC40,   H|TYPE_I,   "SRC", },      /* Shift Right Circular # NBR */
    {  OP_SLC,       0xFC40,   H|TYPE_I,   "SLC", },      /* Shift Left Circular # NBR */
    {  OP_SRAD,      0xFC40,   H|TYPE_I,   "SRAD", },     /* Shift Right Arithmetic Double # NBR */
    {  OP_SLAD,      0xFC40,   H|TYPE_I,   "SLAD", },     /* Shift Left Arithmetic Double # NBR */
    {  OP_SRLD,      0xFC40,   H|TYPE_I,   "SRLD", },     /* Shift Right Logical Double # NBR */
    {  OP_SLLD,      0xFC40,   H|TYPE_I,   "SLLD", },     /* Shift Left Logical Double # NBR */
    {  OP_LEAR,      0xFC00,   TYPE_A,     "LEAR", },     /* Load Effective Address Real * */
    {  OP_ANMx,      0xFC00,   TYPE_A,     "ANM", },      /* And Memory B,H,W,D */
    {  OP_ORMx,      0xFC00,   TYPE_A,     "ORM", },      /* Or Memory B,H,W,D */
    {  OP_EOMx,      0xFC00,   TYPE_A,     "EOM", },      /* Exclusive Or Memory */
    {  OP_CAMx,      0xFC00,   TYPE_A,     "CAM", },      /* Compare Arithmetic with Memory */
    {  OP_CMMx,      0xFC00,   TYPE_A,     "CMM", },      /* Compare Masked with Memory */
    {  OP_SBM,       0xFC00,   TYPE_A,     "SBM", },      /* Set Bit in Memory */
    {  OP_ZBM,       0xFC00,   TYPE_A,     "ZBM", },      /* Zero Bit in Memory */
    {  OP_ABM,       0xFC00,   TYPE_A,     "ABM", },      /* Add Bit in Memory */
    {  OP_TBM,       0xFC00,   TYPE_A,     "TBM", },      /* Test Bit in Memory */
    {  OP_EXM,       0xFC00,   TYPE_B,     "EXM", },      /* Execute Memory */
    {  OP_Lx,        0xFC00,   TYPE_A,     "L", },        /* Load B,H,W,D */
    {  OP_LMx,       0xFC00,   TYPE_A,     "LM", },       /* Load Masked B,H,W,D */
    {  OP_LNx,       0xFC00,   TYPE_A,     "LN", },       /* Load Negative B,H,W,D */
    {  OP_ADMx,      0xFC00,   TYPE_A,     "ADM", },      /* Add Memory B,H,W,D */
    {  OP_SUMx,      0xFC00,   TYPE_A,     "SUM", },      /* Subtract Memory B,H,W,D */
    {  OP_MPMx,      0xFC00,   TYPE_A,     "MPM", },      /* Multiply Memory B,H,W,D */
    {  OP_DVMx,      0xFC00,   TYPE_A,     "DVM", },      /* Divide Memory B,H,W,D */
    {  OP_LI,        0xFC0F,   TYPE_C,     "LI", },       /* Load Immediate */
    {  OP_ADI,       0xFC0F,   TYPE_C,     "ADI", },      /* Add Immediate */
    {  OP_SUI,       0xFC0F,   TYPE_C,     "SUI", },      /* Subtract Immediate */
    {  OP_MPI,       0xFC0F,   TYPE_C,     "MPI", },      /* Multiply Immediate */
    {  OP_DVI,       0xFC0F,   TYPE_C,     "DVI", },      /* Divide Immediate */
    {  OP_CI,        0xFC0F,   TYPE_C,     "CI", },       /* Compare Immediate */
    {  OP_SVC,       0xFC0F,   TYPE_C,     "SVC", },      /* Supervisor Call */
    {  OP_EXR,       0xFC0F,   TYPE_L,     "EXR", },      /* Execute Register/ Right */
    {  OP_SEM,       0xFC0F,   TYPE_A,     "SEM", },      /* Store External Map * */
    {  OP_LEM,       0xFC0F,   TYPE_A,     "LEM", },      /* Load External Map * */
    {  OP_CEMA,      0xFC0F,   TYPE_A,     "CEMA", },     /* Convert External Map * */
    {  OP_LF,        0xFC00,   TYPE_A,     "LF", },       /* Load File */
    {  OP_LEA,       0xFC00,   TYPE_A,     "LEA", },      /* Load Effective Address */
    {  OP_STx,       0xFC00,   TYPE_A,     "ST", },       /* Store B,H,W,D */
    {  OP_STMx,      0xFC00,   TYPE_A,     "STM", },      /* Store Masked B,H,W,D */
    {  OP_ADFx,      0xFC0F,   TYPE_A,     "ADF", },      /* Add Floating Memory D,W */
    {  OP_SUFx,      0xFC0F,   TYPE_A,     "SUF", },      /* Subtract Floating Memory D,W */
    {  OP_MPFx,      0xFC0F,   TYPE_A,     "MPF", },      /* Multiply Floating Memory D,W */
    {  OP_DVFx,      0xFC0F,   TYPE_A,     "DVF", },      /* Divide Floating Memory D,W */
    {  OP_ARMx,      0xFC00,   TYPE_A,     "ARM", },      /* Add Register to Memory B,H,W,D */
    {  OP_BU,        0xFC00,   TYPE_F,     "BU", },       /* Branch Unconditional */
    {  0xF000,       0XFF80,   TYPE_B,     "BFT", },      /* Branch Function True */
    {  0xEC80,       0xFF80,   TYPE_B,     "BS", },       /* Branch Condition True CC1 = 1 */
    {  0xED00,       0xFF80,   TYPE_B,     "BGT", },      /* Branch Condition True CC2 = 1 */
    {  0xED80,       0xFF80,   TYPE_B,     "BLT", },      /* Branch Condition True CC3 = 1 */
    {  0xEE00,       0xFF80,   TYPE_B,     "BEQ", },      /* Branch Condition True CC4 = 1 */
    {  0xEE80,       0xFF80,   TYPE_B,     "BGE", },      /* Branch Condition True CC2|CC4 = 1 */
    {  0xEF00,       0xFF80,   TYPE_B,     "BLE", },      /* Branch Condition True CC3|CC4 = 1*/
    {  0xEF80,       0xFF80,   TYPE_B,     "BANY", },     /* Branch Condition True CC1|CC2|CC3|CC4 */
    {  0xF080,       0xFF80,   TYPE_B,     "BNS", },      /* Branch Condition False CC1 = 0 */
    {  0xF100,       0xFF80,   TYPE_B,     "BNP", },      /* Branch Condition False CC2 = 0 */
    {  0xF180,       0xFF80,   TYPE_B,     "BNN", },      /* Branch Condition False CC3 = 0 */
    {  0xF200,       0xFF80,   TYPE_B,     "BNE", },      /* Branch Condition False CC4 = 0 */
    {  0xF380,       0xFF80,   TYPE_B,     "BAZ", },      /* Branch Condition False CC1|CC2|CC3|CC4=0*/
    {  OP_BCT,       0xFC00,   TYPE_A,     "BCT", },      /* Branch Condition True CC1 == 1 */
    {  OP_BCF,       0xFC00,   TYPE_A,     "BCF", },      /* Branch Condition False */
    {  OP_BIB,       0xFC70,   TYPE_D,     "BIB", },      /* Branch after Incrementing Byte */
    {  OP_BIW,       0xFC70,   TYPE_D,     "BIW", },      /* Branch after Incrementing Word */
    {  OP_BIH,       0xFC70,   TYPE_D,     "BIH", },      /* Branch after Incrementing Half */
    {  OP_BID,       0xFC70,   TYPE_D,     "BID", },      /* Branch after Incrementing Double */
    {  OP_ZMx,       0xFCC0,   TYPE_E,     "ZM", },       /* Zero Memory B,H,W,D */
    {  OP_BL,        0xFF80,   TYPE_B,     "BL", },       /* Branch and Link */
    {  OP_BRI,       0xFCC0,   TYPE_A,     "BRI", },      /* Branch and Reset Interrupt * */
    {  OP_LPSD,      0xFCC0,   TYPE_A,     "LPSD", },     /* Load Program Status Double * */
    {  OP_LPSDCM,    0xFCC0,   TYPE_A,     "LPSDCM", },   /* LPSD and Change Map * */
    {  OP_TPR,       0xFCC0,   TYPE_A,     "TPR", },      /* Transfer Protect Register to Register */
    {  OP_TRP,       0xFCC0,   TYPE_A,     "TRP", },      /* Transfer Register to Protect Register */
    {  OP_EI,        0xFC0F,   TYPE_L,     "EI", },       /* Enable Interrupt */
    {  OP_DI,        0xFC0F,   TYPE_L,     "DI", },       /* Disable Interrupt */
    {  OP_RI,        0xFC0F,   TYPE_L,     "RI", },       /* Request Interrupt */
    {  OP_AI,        0xFC0F,   TYPE_L,     "AI", },       /* Activate Interrupt */
    {  OP_DAI,       0xFC0F,   TYPE_L,     "DAI", },      /* Deactivate Interrupt */
    {  OP_TD,        0xFC0F,   TYPE_M,     "TD", },       /* Test Device */
    {  OP_CD,        0xFC0F,   TYPE_M,     "CD", },       /* Command Device */
    {  OP_SIO,       0xFC7F,   TYPE_C,     "SIO", },      /* Start I/O */
    {  OP_TIO,       0xFC7F,   TYPE_C,     "TIO", },      /* Test I/O */
    {  OP_STPIO,     0xFC7F,   TYPE_C,     "STPIO", },    /* Stop I/O */
    {  OP_RSCHNL,    0xFC7F,   TYPE_C,     "RSCHNL", },   /* Reset Channel */
    {  OP_HIO,       0xFC7F,   TYPE_C,     "HIO", },      /* Halt I/O */
    {  OP_GRIO,      0xFC7F,   TYPE_C,     "GRIO", },     /* Grab Controller */
    {  OP_RSCTL,     0xFC7F,   TYPE_C,     "RSCTL", },    /* Reset Controller */
    {  OP_ECI,       0xFC7F,   TYPE_C,     "ECI", },      /* Enable Channel Interrupt */
    {  OP_DCI,       0xFC7F,   TYPE_C,     "DCI", },      /* Disable Channel Interrupt */
    {  OP_ACI,       0xFC7F,   TYPE_C,     "ACI", },      /* Activate Channel Interrupt */
    {  OP_DACI,      0xFC7F,   TYPE_C,     "DACI", },     /* Deactivate Channel Interrupt */
};


/* Register change decode

   Inputs:
    *of       =       output stream
    inst      =       mask bits
*/
char *fc_type = "WHHDBBBB";

int fprint_inst(FILE *of, uint32 val, int32 sw) {
uint16          inst = (val >> 16) & 0xFFFF;
uint32          v = 0;;
int             i;
int             l = 1;
int             mode = 0;


    if (sw & SWMASK ('M'))  /* Base mode printing */
       mode = 1;
    for (tab = optab; tab->name != NULL; tab++) {
       if (tab->opbase == (inst & tab->mask)) {
           fputs(tab->name, of);
           switch(tab->type & 0xF) {
           case TYPE_A:                 /* c r,[*]o[,x] or r,o[(b)][,x] */
           case TYPE_E:                 /* c [*]o[,x] or o[(b)][,x] */
                 i = (*val & 3) | ((inst >> 1) & 04);
                 fputc(fc_type[i], of);
                 /* Fall through */

           case TYPE_F:                 /* r,[*]o[,x] or r,o[(b)],[,x] */
                 fputc(' ', of);
                 if ((tab->type & 0xf) != TYPE_E) {
                     fputc('0'+((inst>>7) & 07), of);
                     fputc(',', of);
                 }
                 /* Fall through */

           case TYPE_B:                 /* [*]o[,x] or o[(b)],[,x] */
                 if (mode) {
                     fprint_val(of, *val, 16, 16, PV_RZRO);
                     if (inst & 07) {
                         fputc('(', of);
                         fputc('0' + (inst & 07));
                         fputc(')', of);
                     }
                     if (inst & 0x70) {
                          fputc(',', of);
                          fputc('0'+(inst >> 4) & 07);
                     }
                 } else {
                     if (inst & 0x10)
                          fputc('*', of);
                     fprint_val(of, *val, 16, 19, PV_RZRO);
                     if (inst & 0x60) {
                          fputc(',', of);
                          fputc('0'+(inst >> 5) & 03);
                     }
                 }
                 break;

           case TYPE_C:                 /* r,v */
                 i = (*val & 3) | ((inst >> 1) & 04);
                 fputc(fc_type[i], of);
                 fputc(' ', of);
                 fputc('0'+((inst>>7) & 07), of);
                 fputc(',', of);
                 fprint_val(of, *val, 16, 16, PV_RZRO);
                 break;

           case TYPE_D:                  /* r,r */
                 fputc(' ', of);
                 fputc('0'+((inst>>7) & 07), of);
                 fputc(',', of);
                 fputc('0'+((inst>>4) & 07), of);
                 break;

           case TYPE_G:                 /* r */
                 fputc(' ', of);
                 fputc('0'+((inst>>7) & 07), of);
                 break;

           case TYPE_H:                 /* empty */
                 break;

           case TYPE_I:                 /* r,b */
                 fputc(' ', of);
                 fputc('0'+((inst>>7) & 07), of);
                 fputc(',', of);
                 fprint_val(of, inst, 16, 5, PV_RZRO);
                 break; 

           case TYPE_K:                 /* r,rb */
                 fputc(' ', of);
                 fputc('0'+((inst>>4) & 07), of);
                 fputc(',', of);
                 i = ((inst & 3) << 3) | ((inst >> 7) & 07);
                 fprint_val(of, i, 16, 5, PV_RZRO);
                 break; 

           case TYPE_L:                  /* i */
                 fputc(' ', of);
                 fprint_val(of, inst>>3, 16, 7, PV_RZRO);
                 break;

           case TYPE_M:                  /* i,v */
                 fputc(' ', of);
                 fprint_val(of, inst>>3, 16, 7, PV_RZRO);
                 fputc(',', of);
                 fprint_val(of, *val, 16, 16, PV_RZRO);
                 break;

           }
           return (tab->type & H) ? 2 : 4;
      }
   }
}

/* Symbolic decode

   Inputs:
       *of      =       output stream
       addr     =       current PC
       *val     =       pointer to values
       *uptr    =       pointer to unit
       sw       =       switches
   Outputs:
       return   =       status code
*/

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
       UNIT *uptr, int32 sw)
{
int             i;
int             l = 1;
int             rdx = 16;
t_opcode        *tab;
uint32          num;

if (sw & SWMASK ('D')) 
     rdx = 10;
else if (sw & SWMASK ('O')) 
     rdx = 8;
else if (sw & SWMASK ('H')) 
     rdx = 16;
if (sw & SWMASK ('M')) {
     sw &= ~ SWMASK('F'); /* Can't do F and M at same time */
} else if (sw & SWMASK('F')) {
     l = 4;
} else if (sw & SWMASK('W')) {
     l = 2;
} else if (sw & SWMASK('B')) {
     l = 1;
}

if (sw & SWMASK ('C')) {
    fputc('\'', of);
    for(i = 0; i < l; i++) {
       char ch = val[i] & 0xff;
       if (ch >= 0x20 && ch <= 0x7f)
           fprintf(of, "%c", ch);
       else
           fputc('_', of);
    }
    fputc('\'', of);
} else if ((addr & 1) == 0 && sw & (SWMASK ('M')|SWMASK('N'))) { 
    num = 0;
    for (i = 0; i < 4; i++) 
       num |= (uint32)val[i] << ((3-i) * 8);
    l = fprint_inst(of, num, sw);
} else {
    num = 0;
    for (i = 0; i < l && i < 4; i++) 
       num |= (uint32)val[i] << ((l-i-1) * 8);
    fprint_val(of, num, rdx, l*8, PV_RZRO);
}

return -(l-1);
}

/* 
 * Collect offset in radix.
 */
t_stat get_off (CONST char *cptr, CONST char **tptr, uint32 radix, uint32 *val, char *m)
{
t_stat r;

r = SCPE_OK;
*m = 0;
*val = (uint32)strtotv (cptr, tptr, radix);
if (cptr == *tptr)
    r = SCPE_ARG;
else {
    cptr = *tptr;
    while (sim_isspace (*cptr)) cptr++;
    if (*cptr++ == '(') {
       *m = 1;
        while (sim_isspace (*cptr)) cptr++;
    }
    *tptr = cptr;
}
return r;
}

/* 
 * Collect immediate in radix.
 */
t_stat get_imm (CONST char *cptr, CONST char **tptr, uint32 radix, uint32 *val)
{
t_stat r;

r = SCPE_OK;
*val = (uint32)strtotv (cptr, tptr, radix);
if ((cptr == *tptr) || (*val > 0xffff))
    r = SCPE_ARG;
else {
    cptr = *tptr;
    while (sim_isspace (*cptr)) cptr++;
    *tptr = cptr;
}
return r;
}

/* Symbolic input

   Inputs:
       *cptr       =       pointer to input string
       addr       =       current PC
       uptr       =       pointer to unit
       *val       =       pointer to output values
       sw       =       switches
   Outputs:
       status       =       error status
*/

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int             i;
int             x;
int             l = 1;
int             rdx = 16;
char            mod = 0;
t_opcode        *tab;
t_stat          r;
uint32          num;
uint32          val;
uint32          max[5] = { 0, 0xff, 0xffff, 0, 0xffffffff };
CONST char      *tptr;
char            gbuf[CBUFSIZE];

if (sw & SWMASK ('D')) 
    rdx = 10;
else if (sw & SWMASK ('O')) 
    rdx = 8;
else if (sw & SWMASK ('H')) 
    rdx = 16;
if (sw & SWMASK('F')) {
     l = 4;
} else if (sw & SWMASK('W')) {
     l = 2;
}

if (sw & SWMASK ('C')) {
   cptr = get_glyph_quoted(cptr, gbuf, 0);   /* Get string */
   for(i = 0; gbuf[i] != 0; i++) {
      val[i] = gbuf[i];
   }
   return -(i - 1);
}

if (sw & SWMASK ('N')) {
    cptr = get_glyph(cptr, gbuf, 0);         /* Get opcode */
    l = strlen(gbuf);
    for (tab = optab; tab->name != NULL; tab++) {
       i = tab->type & 0xf;
       if (i == TYPE_A || i == TYPE_E) {
          if (sim_strncasecmp(tab->name, gbuf, l - 1) == 0)
             break;
       } else if (sim_strcasecmp(tab->name, gbuf) == 0) 
          break;
    }
    if (tab->name == NULL)
       return SCPE_ARG;
    num = tab->opbase;
    switch(i) {
    case TYPE_A:                 /* c r,[*]o[,x] */
    case TYPE_E:                 /* c [*]o[,x] */
          switch(gbuf[l]) {
          case 'B': num |= 0x80000; break;
          case 'H': num |= 0x00001; break;
          case 'W': num |= 0x00000; break;
          case 'D': num |= 0x00002; break;
          default:
              return SCPE_ARG;
          }
          /* Fall through */

    case TYPE_F:                 /* r,[*]o[,x] */
          while (sim_isspace (*cptr)) cptr++;
          if (i != TYPE_E) {
              if (*cptr >= '0' || *cptr <= '7') {
                   x = *cptr++ - '0';
                   while (sim_isspace (*cptr)) cptr++;
                   if (*cptr++ != ',')
                       return SCPE_ARG;
                   num |= x << 23;
              } else 
                   return SCPE_ARG;
          }
          /* Fall through */

    case TYPE_B:                 /* [*]o[,x] */
          if (*cptr == '*') {
              num |= 0x100000;
              cptr++;
              while (sim_isspace (*cptr)) cptr++;
          }
          if (r = get_off (cptr, &tptr, 16, &val, &mod))
               return r;
          cptr = tptr;
          if (val > 0x7FFFF)
               return SCPE_ARG;
          num |= val;
          if (mod) {
              return SCPE_ARG;
          }
          if (*cptr++ == ",") {
              if (*cptr >= '0' || *cptr <= '7') {
                   x = *cptr++ - '0';
                   num |= x << 20;
              } else 
                   return SCPE_ARG;
          }
          break;

    case TYPE_C:                 /* r,v */
          while (sim_isspace (*cptr)) cptr++;
          if (*cptr >= '0' || *cptr <= '7') {
               x = *cptr++ - '0';
               while (sim_isspace (*cptr)) cptr++;
               if (*cptr++ != ',')
                   return SCPE_ARG;
               num |= x << 23;
          } else 
               return SCPE_ARG;
          while (sim_isspace (*cptr)) cptr++;
          if (r = get_imm (cptr, &tptr, rd, &val))
               return r;
          num |= val;
          break;

    case TYPE_D:                  /* r,r */
          while (sim_isspace (*cptr)) cptr++;
          if (*cptr >= '0' || *cptr <= '7') {
               x = *cptr++ - '0';
               while (sim_isspace (*cptr)) cptr++;
               if (*cptr++ != ',')
                   return SCPE_ARG;
               num |= x << 23;
          } else 
               return SCPE_ARG;
          while (sim_isspace (*cptr)) cptr++;
          if (*cptr >= '0' || *cptr <= '7') {
               x = *cptr++ - '0';
               while (sim_isspace (*cptr)) cptr++;
               if (*cptr++ != ',')
                   return SCPE_ARG;
               num |= x << 20;
          } else 
               return SCPE_ARG;
          break;

    case TYPE_G:                 /* r */
          while (sim_isspace (*cptr)) cptr++;
          if (*cptr >= '0' || *cptr <= '7') {
               x = *cptr++ - '0';
               while (sim_isspace (*cptr)) cptr++;
               if (*cptr++ != ',')
                   return SCPE_ARG;
               num |= x << 23;
          } else 
               return SCPE_ARG;
          break;

    case TYPE_H:
          break;

    case TYPE_I:
          break; 

    case TYPE_K:
          break; 

    case TYPE_L:
          break;

    case TYPE_M:
          break;

    }
    return (tab->type & H) ? 2 : 4;
}
if (sw & SWMASK ('M')) {
    cptr = get_glyph(cptr, gbuf, 0);         /* Get opcode */
    l = strlen(gbuf);
    for (tab = optab; tab->name != NULL; tab++) {
       i = tab->type & 0xf;
       if (i == TYPE_A || i == TYPE_E) {
          if (sim_strncasecmp(tab->name, gbuf, l - 1) == 0)
             break;
       } else if (sim_strcasecmp(tab->name, gbuf) == 0) 
          break;
    }
    if (tab->name == NULL)
       return SCPE_ARG;
    num = tab->opbase << 16;
    switch(i) {
    case TYPE_A:                 /* c r,o[(b)][,x] */
    case TYPE_E:                 /* c o[(b)][,x] */
          switch(gbuf[l]) {
          case 'B': num |= 0x80000; break;
          case 'H': num |= 0x00001; break;
          case 'W': num |= 0x00000; break;
          case 'D': num |= 0x00002; break;
          default:
              return SCPE_ARG;
          }
          /* Fall through */

    case TYPE_F:                 /* r,o[(b)],[,x] */
          while (sim_isspace (*cptr)) cptr++;
          if (i != TYPE_E) {
              if (*cptr >= '0' || *cptr <= '7') {
                   x = *cptr++ - '0';
                   while (sim_isspace (*cptr)) cptr++;
                   if (*cptr++ != ',')
                       return SCPE_ARG;
                   num |= x << 23;
              } else 
                   return SCPE_ARG;
          }
          /* Fall through */

    case TYPE_B:                 /* o[(b)],[,x] */
          if (r = get_off (cptr, &tptr, 16, &val, &mod))
               return r;
          cptr = tptr;
          if (val > 0xFFFF)
               return SCPE_ARG;
          num |= val;
          if (mod) {
              if (*cptr >= '0' || *cptr <= '7') {
                   x = *cptr++ - '0';
                   while (sim_isspace (*cptr)) cptr++;
                   if (*cptr++ != ')')
                       return SCPE_ARG;
                   num |= x << 16;
              } else
                   return SCPE_ARG;
          }
          if (*cptr++ == ",") {
              if (*cptr >= '0' || *cptr <= '7') {
                   x = *cptr++ - '0';
                   num |= x << 20;
              } else 
                   return SCPE_ARG;
          }
          break;

    case TYPE_C:                 /* r,v */
          while (sim_isspace (*cptr)) cptr++;
          if (*cptr >= '0' || *cptr <= '7') {
               x = *cptr++ - '0';
               while (sim_isspace (*cptr)) cptr++;
               if (*cptr++ != ',')
                   return SCPE_ARG;
               num |= x << 23;
          } else 
               return SCPE_ARG;
          while (sim_isspace (*cptr)) cptr++;
          if (r = get_imm (cptr, &tptr, rd, &val))
               return r;
          num |= val;
          break;

    case TYPE_D:                  /* r,r */
          while (sim_isspace (*cptr)) cptr++;
          if (*cptr >= '0' || *cptr <= '7') {
               x = *cptr++ - '0';
               while (sim_isspace (*cptr)) cptr++;
               if (*cptr++ != ',')
                   return SCPE_ARG;
               num |= x << 23;
          } else 
               return SCPE_ARG;
          while (sim_isspace (*cptr)) cptr++;
          if (*cptr >= '0' || *cptr <= '7') {
               x = *cptr++ - '0';
               while (sim_isspace (*cptr)) cptr++;
               if (*cptr++ != ',')
                   return SCPE_ARG;
               num |= x << 20;
          } else 
               return SCPE_ARG;
          break;

    case TYPE_G:                 /* r */
          while (sim_isspace (*cptr)) cptr++;
          if (*cptr >= '0' || *cptr <= '7') {
               x = *cptr++ - '0';
               while (sim_isspace (*cptr)) cptr++;
               if (*cptr++ != ',')
                   return SCPE_ARG;
               num |= x << 23;
          } else 
               return SCPE_ARG;
          break;

    case TYPE_H:
          break;

    case TYPE_I:
          break; 

    case TYPE_K:
          break; 

    case TYPE_L:
          break;

    case TYPE_M:
          break;
    }
    return (tab->type & H) ? 2 : 4;
}
num = get_uint(cptr, rdx, max[l], &r);
for (i = 0; i < l && i < 4; i++) 
    val[i] = (num >> (i * 8)) & 0xff;
return -(l-1);
}

