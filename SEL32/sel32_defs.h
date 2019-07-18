/* sel32_defs.h: Gould Concept/32 (orignal SEL32) simulator definitions 

   Copyright (c) 2018, Richard Cornwell
   Copyright (c) 2018, James C. Bevier

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
   RICHARD CORNWELL OR JAMES BEVIER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

*/

#include "sim_defs.h"					/* simulator defns */

/* Simulator stop codes */

#define STOP_IONRDY	1				/* I/O dev not ready */
#define STOP_HALT	2				/* HALT */
#define STOP_IBKPT	3				/* breakpoint */
#define STOP_UUO	4				/* invalid opcode */
#define STOP_INVINS	5				/* invalid instr */
#define STOP_INVIOP	6				/* invalid I/O op */
#define STOP_INDLIM	7				/* indirect limit */
#define STOP_XECLIM	8				/* XEC limit */
#define STOP_IOCHECK	9				/* IOCHECK */
#define STOP_MMTRP	10				/* mm in trap */
#define STOP_TRPINS	11				/* trap inst not BRM */
#define STOP_RTCINS	12				/* rtc inst not MIN/SKR */
#define STOP_ILLVEC	13				/* zero vector */
#define STOP_CCT	14				/* runaway CCT */

/* Conditional error returns */

/* Memory */

#define MAXMEMSIZE	((16*1024*1024)/4)		/* max memory size */
#define PAMASK		(MAXMEMSIZE - 1)		/* physical addr mask */
#define MEMSIZE		(cpu_unit.capac)		/* actual memory size */
#define MEM_ADDR_OK(x)	(((x)) < MEMSIZE)

/* Device information block */
typedef struct dib {
        uint8           dev_addr;       /* Device address */
	uint8		dev_class;	/* Device class */
/* Start I/O */
	uint8		(*start_io)(UNIT *uptr, uint16 chan);
/* Start a command */
	uint8		(*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd);
/* Stop I/O */
	uint8		(*halt_io)(UNIT *uptr);
} DIB;


/* Opcode definitions */
#define OP_HALT         0x0000          /* Halt # * */
#define OP_WAIT         0x0001          /* Wait # * */
#define OP_NOP          0x0002          /* Nop # */
#define OP_LCS          0x0003          /* Load Control Switches */
#define OP_ES           0x0004          /* Extend Sign # */
#define OP_RND          0x0005          /* Round Register # */
#define OP_BEI          0x0006          /* Block External Interrupts # */
#define OP_UEI          0x0007          /* Unblock External Interrupts # */
#define OP_EAE          0x0008          /* Enable Arithmetic Exception Trap # */
#define OP_RDSTS        0x0009          /* Read CPU Status Word * */
#define OP_SIPU         0x000A          /* Signal IPU # */
#define OP_SEA          0x000D          /* Set Extended Addressing # NBR */
#define OP_DAE          0x000E          /* Disable Arithmetic Exception Trap # */
#define OP_CEA          0x000F          /* Clear Extended Addressing # NBR */
#define OP_ANR          0x0400          /* And Register # */
#define OP_CMC          0x040A          /* Cache Memory Control # */
#define OP_SMC          0x0407          /* Shared Memory Control # */
#define OP_RPSWT        0x040B          /* Read Processor Status Word two # */
#define OP_ORR          0x0800          /* Or Register # */
#define OP_ORRM         0x0808          /* Or Register Masked # */
#define OP_ZR           0x0C00          /* Zero Register # */
#define OP_EOR          0x0C00          /* Exclusive Or Register # */
#define OP_EORM         0x0C08          /* Exclusive Or Register Masked # */
#define OP_CAR          0x1000          /* Compare Register # */
#define OP_SACZ         0x1008          /* Shift and Count Zeros # BR */
#define OP_CMR          0x1400          /* Compare masked with register */
#define OP_SBR          0x1800          /* Set Bit in Register # */
#define OP_ZBR          0x1804          /* Zero Bit In register # BR */
#define OP_ABR          0x1808          /* Add Bit In Register # BR */
#define OP_TBR          0x180C          /* Test Bit in Register # BR */
#define OP_SRABR        0x1C00          /* Shift Right Arithmetic # BR */
#define OP_SRLBR        0x1C20          /* Shift Right Logical # BR */
#define OP_SLABR        0x1C40          /* Shift Left Arithmetic # BR */
#define OP_SLLBR        0x1C60          /* Shift Left Logical # BR */
#define OP_SRADBR       0x2000          /* Shift Right Arithmetic Double # BR */
#define OP_SRLDBR       0x2020          /* Shift Left Logical Double # BR */
#define OP_SLADBR       0x2040          /* Shift Right Arithmetic Double # BR */
#define OP_SLLDBR       0x2060          /* Shift Left Logical Double # BR */
#define OP_SRCBR        0x2400          /* Shift Right Circular # BR */
#define OP_ZBM          0x1C00          /* Zero Bit in Register # NBR */
#define OP_ABR          0x2000          /* Add Bit in Register # NBR */
#define OP_TBR          0x2400          /* Test Bit in Register # NBR */
#define OP_TRSW         0x2800          /* Transfer GPR to PSD */
#define OP_TRBR         0x2801          /* Transfer GPR to BR # BR */
#define OP_XCBR         0x2802          /* Exchange Base Registers # BR */
#define OP_TCCR         0x2802          /* Transfer CC to GPR # BR */
#define OP_TRCC         0x2804          /* Transfer GPR to CC # BR */
#define OP_BSUB         0x2805          /* Branch Subroutine # BR */
#define OP_CALL         0x2808          /* Procedure Call # BR */
#define OP_TPCBR        0x280C          /* Transfer Program Counter to Base # BR */
#define OP_RETURN       0x280E          /* Procedure Return # BR */
#define OP_TRR          0x2C00          /* Transfer Register to Register # */
#define OP_TRDR         0x2C01          /* Transfer GPR to BR # */
#define OP_TBRR         0x2C02          /* Transfer BR to GPR BR # */
#define OP_TRC          0x2C03          /* Transfer Register Complement # */
#define OP_TRN          0x2C04          /* Transfer Register Negative # */
#define OP_XCR          0x2C05          /* Exchange Registers # */
#define OP_LMAP         0x2C07          /* Load MAP * */
#define OP_TRRM         0x2C08          /* Transfer Register to Register Masked # */
#define OP_SETCPU       0x2C09          /* Set CPU Mode # * */
#define OP_TMAPR        0x2C0A          /* Transfer MAP to Register # * */
#define OP_XCRM         0x2C0D          /* Exchange Registers Masked # */
#define OP_TRCM         0x2C0B          /* Transfer Register Complement Masked # */
#define OP_TRNM         0x2C0C          /* Transfer Register Negative Masked # */
#define OP_TRSC         0x2C0E          /* Transfer Register to Scratchpad # * */
#define OP_TSCR         0x2C0F          /* Transfer Scratchpad to Register # * */
#define OP_CALM         0x3000          /* Call Monitor # */
#define OP_LA           0x3400          /* Load Address NBR */
#define OP_ADR          0x3800          /* Add Register to Register # */
#define OP_ADRFW        0x3801          /* Add Floating Point to Register # BR? */
#define OP_MPRBR        0x3802          /* Multiply Register BR # */
#define OP_SURFW        0x3803          /* Subtract Floating Point Register BR? # */
#define OP_DVRFW        0x3804          /* Divide Floating Point Register BR? # */
#define OP_FIXW         0x3805          /* Fix Floating Point Register BR? # */
#define OP_MPRFW        0x3806          /* Multiply Floating Point Register BR? # */
#define OP_FLTW         0x3807          /* Float Floating Point Register BR? # */
#define OP_ADRM         0x3808          /* Add Register to Register Masked # */
#define OP_DVRBR        0x380A          /* Divide Register by Registier BR # */
#define OP_SURFD        0x380B          /* Subtract Floating Point Double # BR? */
#define OP_DVRFD        0x380C          /* Divide Floating Point Double # BR? */
#define OP_FIXD         0x380D          /* Fix Double Register # BR? */
#define OP_MPRFD        0x380E          /* Multiply Double Register # BR? */
#define OP_FLTD         0x380F          /* Float Double # BR? */
#define OP_SUR          0x3C00          /* Subtract Register to Register # */
#define OP_SURM         0x3C08          /* Subtract Register to Register Masked # */
#define OP_MPR          0x4000          /* Multiply Register to Register # NBR */
#define OP_DVR          0x4400          /* Divide Register to Register # NBR */
#define OP_LA           0x5000          /* Load Address BR */
#define OP_STWBR        0x5400          /* Store Base Register BR */
#define OP_SUABR        0x5800          /* Subtract Base Register BR */
#define OP_LABR         0x5808          /* Load Address Base Register BR */
#define OP_LWBR         0x5C00          /* Load Base Register BR */
#define OP_BSUBM        0x5C08          /* Branch Subroutine Memory BR */
#define OP_CALLM        0x5C08          /* Call Memory BR */
#define OP_NOR          0x6000          /* Normalize # NBR */
#define OP_NORD         0x6400          /* Normalize Double #  NBR*/
#define OP_SCZ          0x6800          /* Shift and Count Zeros # */
#define OP_SRA          0x6C00          /* Shift Right Arithmetic # NBR */
#define OP_SLA          0x6C40          /* Shift Left Arithmetic # NBR */
#define OP_SRL          0x7000          /* Shift Right Logical # NBR */
#define OP_SLL          0x7040          /* Shift Left Logical # NBR */
#define OP_SRC          0x7400          /* Shift Right Circular # NBR */
#define OP_SLC          0x7440          /* Shift Left Circular # NBR */
#define OP_SRAD         0x7800          /* Shift Right Arithmetic Double # NBR */
#define OP_SLAD         0x7840          /* Shift Left Arithmetic Double # NBR */
#define OP_SRLD         0x7C00          /* Shift Right Logical Double # NBR */
#define OP_SLLD         0x7C40          /* Shift Left Logical Double # NBR */
#define OP_LEAR         0x8000          /* Load Effective Address Real * */
#define OP_ANMx         0x8400          /* And Memory B,H,W,D */
#define OP_ORMx         0x8800          /* Or Memory B,H,W,D */
#define OP_EOMx         0x8C00          /* Exclusive Or Memory */
#define OP_CAMx         0x9000          /* Compare Arithmetic with Memory */
#define OP_CMMx         0x9400          /* Compare Masked with Memory */
#define OP_SBM          0x9800          /* Set Bit in Memory */
#define OP_ZBM          0x9C00          /* Zero Bit in Memory */
#define OP_ABM          0xA000          /* Add Bit in Memory */
#define OP_TBM          0xA400          /* Test Bit in Memory */
#define OP_EXM          0xA800          /* Execute Memory */
#define OP_Lx           0xAC00          /* Load B,H,W,D */
#define OP_LMx          0xB000          /* Load Masked B,H,W,D */
#define OP_LNx          0xB400          /* Load Negative B,H,W,D */
#define OP_ADMx         0xB800          /* Add Memory B,H,W,D */
#define OP_SUMx         0xBC00          /* Subtract Memory B,H,W,D */
#define OP_MPMx         0xC000          /* Multiply Memory B,H,W,D */
#define OP_DVMx         0xC400          /* Divide Memory B,H,W,D */
#define OP_LI           0xC800          /* Load Immediate */
#define OP_ADI          0xC801          /* Add Immediate */
#define OP_SUI          0xC802          /* Subtract Immediate */
#define OP_MPI          0xC803          /* Multiply Immediate */
#define OP_DVI          0xC804          /* Divide Immediate */
#define OP_CI           0xC805          /* Compare Immediate */
#define OP_SVC          0xC806          /* Supervisor Call */
#define OP_EXR          0xC807          /* Execute Register/ Right */
#define OP_SEM          0xC808          /* Store External Map * */
#define OP_LEM          0xC809          /* Load External Map * */
#define OP_CEMA         0xC80A          /* Convert External Map * */
#define OP_LF           0xCC00          /* Load File */
#define OP_LEA          0xD000          /* Load Effective Address */
#define OP_STx          0xD400          /* Store B,H,W,D */
#define OP_STMx         0xD800          /* Store Masked B,H,W,D */
#define OP_ADFx         0xE008          /* Add Floating Memory D,W */
#define OP_SUFx         0xE000          /* Subtract Floating Memory D,W */
#define OP_MPFx         0xE408          /* Multiply Floating Memory D,W */
#define OP_DVFx         0xE400          /* Divide Floating Memory D,W */
#define OP_ARMx         0xE800          /* Add Register to Memory B,H,W,D */
#define OP_BU           0xEC00          /* Branch Unconditional */
#define OP_BCT          0xEC00          /* Branch Condition True */
#define OP_BCF          0xF000          /* Branch Condition False */
#define OP_BIB          0xF400          /* Branch after Incrementing Byte */
#define OP_BIW          0xF420          /* Branch after Incrementing Word */
#define OP_BIH          0xF440          /* Branch after Incrementing Half */
#define OP_BID          0xF460          /* Branch after Incrementing Double */
#define OP_ZMx          0xF800          /* Zero Memory B,H,W,D */
#define OP_BL           0xF880          /* Branch and Link */
#define OP_BRI          0xF900          /* Branch and Reset Interrupt * */
#define OP_LPSD         0xF980          /* Load Program Status Double * */
#define OP_LPSDCM       0xFA80          /* LPSD and Change Map * */
#define OP_TPR          0xFB80          /* Transfer Protect Register to Register */
#define OP_TRP          0xFB00          /* Transfer Register to Protect Register */
#define OP_EI           0xFC00          /* Enable Interrupt */
#define OP_DI           0xFC01          /* Disable Interrupt */
#define OP_RI           0xFC02          /* Request Interrupt */
#define OP_AI           0xFC03          /* Activate Interrupt */
#define OP_DAI          0xFC04          /* Deactivate Interrupt */
#define OP_TD           0xFC05          /* Test Device */
#define OP_CD           0xFC06          /* Command Device */
#define OP_SIO          0xFC17          /* Start I/O */
#define OP_TIO          0xFC1F          /* Test I/O */
#define OP_STPIO        0xFC27          /* Stop I/O */
#define OP_RSCHNL       0xFC2F          /* Reset Channel */
#define OP_HIO          0xFC37          /* Halt I/O */
#define OP_GRIO         0xFC3F          /* Grab Controller */
#define OP_RSCTL        0xFC47          /* Reset Controller */
#define OP_ECI          0xFC67          /* Enable Channel Interrupt */
#define OP_DCI          0xFC6F          /* Disable Channel Interrupt */
#define OP_ACI          0xFC77          /* Activate Channel Interrupt */
#define OP_DACI         0xFC7F          /* Deactivate Channel Interrupt */



/* Debuging controls */
#define DEBUG_CMD       0x0000001       /* Show device commands */
#define DEBUG_DATA      0x0000002       /* Show data transfers */
#define DEBUG_DETAIL    0x0000004       /* Show details */
#define DEBUG_EXP       0x0000008       /* Show error conditions */
#define DEBUG_CONI      0x0000020       /* Show CONI instructions */
#define DEBUG_CONO      0x0000040       /* Show CONO instructions */
#define DEBUG_DATAIO    0x0000080       /* Show DATAI/O instructions */
#define DEBUG_IRQ       0x0000100       /* Show IRQ requests */

extern DEBTAB dev_debug[];

/* defines for all programs */
#define RMASK           0x0000FFFF      /* right hw 16 bit mask */
#define LMASK           0xFFFF0000      /* left hw 16 bit mask */
#define FMASK           0xFFFFFFFF      /* 32 bit mask */
#define DMASK           0xFFFFFFFFFFFFFFFFLL    /* 64 bit all bits mask */
#define D48LMASK        0xFFFFFFFFFFFF0000LL    /* 64 bit left 48 bits mask */
#define D32LMASK        0xFFFFFFFF00000000LL    /* 64 bit left 32 bits mask */
#define D32RMASK        0x00000000FFFFFFFFLL    /* 64 bit right 32 bits mask */
#define MSIGN           0x80000000      /* 32 bit minus sign */
#define DMSIGN          0x8000000000000000LL    /* 64 bit minus sign */
#define FSIGN           0x80000000      /* 32 bit minus sign */
/* sign extend 16 bit value to uint32 */
#define SEXT16(x)       (x&MSIGN?(uint32)(((uint32)x&RMASK)|LMASK):(uint32)x)
/* sign extend 16 bit value to uint64 */
#define DSEXT16(x)      (x&MSIGN?(l_uint64)(((l_uint64)x&RMASK)|D48LMASK):(t_uint64)x)
/* sign extend 32 bit value to uint64 */
#define DSEXT32(x)      (x&MSIGN?(l_uint64)(((l_uint64)x&D32RMASK)|D32LMASK):(t_uint64)x)

#define UNIT_V_MODEL    (UNIT_V_UF + 0)
#define UNIT_MODEL      (7 << UNIT_V_MODEL)
#define MODEL(x)        (x << UNIT_V_MODEL)
#define UNIT_V_MSIZE    (UNIT_V_MODEL + 3)
#define UNIT_MSIZE      (0x1F << UNIT_V_MSIZE)
#define MEMAMOUNT(x)    (x << UNIT_V_MSIZE)
#define CPU_MODEL       ((cpu_unit.flags >> UNIT_V_MODEL) & 0x7)    /* cpu model 0-7 */

#define MODEL_55        0                     /* 512K Mode Only */
#define MODEL_75        1                     /* Extended */
#define MODEL_27        2                     /* */
#define MODEL_67        3                     /* */
#define MODEL_87        4                     /* */
#define MODEL_97        5                     /* */
#define MODEL_V6        6                     /* V6 CPU */
#define MODEL_V9        7                     /* V9 CPU */

 DEVICEC1      0x40                       /* CC1 in CC */
#define CC2      0x20                       /* CC2 in CC */
#define CC3      0x10                       /* CC3 in CC */
#define CC4      0x08                       /* CC4 in CC */

#define CC1BIT   0x40000000                 /* CC1 in PSD1 */
#define CC2BIT   0x20000000                 /* CC2 in PSD1 */
#define CC3BIT   0x10000000                 /* CC3 in PSD1 */
#define CC4BIT   0x08000000                 /* CC4 in PSD1 */

/* PSD mode bits in 'modes' variable */
#define PRIV     0x80                       /* Privileged mode  PSD 1 bit 0 */
#define EXTD     0x04                       /* Extended Addressing PSD 1 bit 5 */
#define BASE     0x02                       /* Base Mode PSD 1 bit 6 */
#define AEXP     0x01                       /* Arithmetic exception PSD 1 bit 7 */

#define MAP      0x40                       /* Map mode, PSD 2 bit 0 */
#define RET      0x20                       /* Retain current maps, PSD 2 bit 15 */
#define BLKED    0x10                       /* Set blocked mode, PSD 2 bit 17 */
#define BLKRET   0x08                       /* Set retain blocked mode, PSD 2 bit 16 */

/* PSD mode bits in PSD words 1&2 variable */
#define PRIVBIT  0x80000000                 /* Privileged mode  PSD 1 bit 0 */
#define EXTDBIT  0x04000000                 /* Extended Addressing PSD 1 bit 5 */
#define BASEBIT  0x02000000                 /* Base Mode PSD 1 bit 6 */
#define AEXPBIT  0x01000000                 /* Arithmetic exception PSD 1 bit 7 */

#define BLKEDBIT 0x00004000                 /* Set blocked mode, PSD 2 bit 17 */
#define RETBIT   0x00010000                 /* Retain current maps, PSD 2 bit 15 */
#define MAPBIT   0x80000000                 /* Map mode, PSD 2 bit 0 */

/* Trap Table Address in memory is pointed to by SPAD 0xF0 */
#define POWERFAIL_TRAP  0x80                /* Power fail trap */
#define POWERON_TRAP    0x84                /* Power-On trap */
#define MEMPARITY_TRAP  0x88                /* Memory Parity Error trap */
#define NONPRESMEM_TRAP 0x8C                /* Non Present Memory trap */
#define UNDEFINSTR_TRAP 0x90                /* Undefined Instruction Trap */
#define PRIVVIOL_TRAP   0x94                /* Privlege Violation Trap */
#define SVCCALL_TRAP    0x98                /* Supervisor Call Trap */
#define MACHINECHK_TRAP 0x9C                /* Machine Check Trap */
#define SYSTEMCHK_TRAP  0xA0                /* System Check Trap */
#define MAPFAULT_TRAP   0xA4                /* Map Fault Trap */
#define IPUUNDEFI_TRAP  0xA8                /* IPU Undefined Instruction Trap */
#define SIGNALIPU_TRAP  0xAC                /* Signal IPU/CPU Trap */
#define ADDRSPEC_TRAP   0xB0                /* Address Specification Trap */
#define CONSOLEATN_TRAP 0xB4                /* Console Attention Trap */
#define PRIVHALT_TRAP   0xB8                /* Privlege Mode Halt Trap */
#define AEXPCEPT_TRAP   0xBC                /* Arithmetic Exception Trap */


/* Errors returned from various functions */
#define ALLOK   0x0000                      /* no error, all is OK */
#define MAPFLT  MAPFAULT_TRAP               /* map fault error */
#define NPMEM   NONPRESMEM_TRAP             /* non present memory */
#define MPVIOL  PRIVVIOL_TRAP               /* memory protection violation */

/* general instruction decode equates */
#define IND     0x00100000                  /* indirect bit in instruction, bit 11 */
#define F_BIT   0x00080000                  /* byte flag addressing bit 11 in instruction */
#define C_BITS  0x00000003                  /* byte number or hw, dw, dw flags bits 20 & 31 */
#define BIT0    0x80000000                  /* general use for bit 0 testing */
#define BIT1    0x40000000                  /* general use for bit 1 testing */
#define MASK16  0x0000FFFF                  /* 16 bit address mask */
#define MASK19  0x0007FFFF                  /* 19 bit address mask */
#define MASK20  0x000FFFFF                  /* 20 bit address mask */
#define MASK24  0x00FFFFFF                  /* 24 bit address mask */
#define MASK32  0xFFFFFFFF                  /* 32 bit address mask */

