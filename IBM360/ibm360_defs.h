/* ibm360_defs.h: IBM 360 simulator definitions

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "sim_defs.h"                     /* simulator defns */

/* Simulator stop codes */

#define STOP_IONRDY       1               /* I/O dev not ready */
#define STOP_HALT         2               /* HALT */
#define STOP_IBKPT        3               /* breakpoint */
#define STOP_UUO          4               /* invalid opcode */
#define STOP_INVINS       5               /* invalid instr */
#define STOP_INVIOP       6               /* invalid I/O op */
#define STOP_INDLIM       7               /* indirect limit */
#define STOP_XECLIM       8               /* XEC limit */
#define STOP_IOCHECK      9               /* IOCHECK */
#define STOP_MMTRP       10               /* mm in trap */
#define STOP_TRPINS      11               /* trap inst not BRM */
#define STOP_RTCINS      12               /* rtc inst not MIN/SKR */
#define STOP_ILLVEC      13               /* zero vector */
#define STOP_CCT         14               /* runaway CCT */

/* Conditional error returns */

/* Memory */

#define MAXMEMSIZE        (16*1024*1024)      /* max memory size */
#define PAMASK            (MAXMEMSIZE - 1)    /* physical addr mask */
#define MEMSIZE           (cpu_unit.capac)    /* actual memory size */
#define MEM_ADDR_OK(x)    (((x)) < MEMSIZE)

/* Device information block */
typedef struct dib {
        uint8             mask;               /* Device mask */
        uint8             numunits;           /* Number of units */
                          /* Start I/O */
        uint8            (*start_io)(UNIT *uptr, uint16 chan);
                          /* Start a command */
        uint8            (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd);
                          /* Stop I/O */
        uint8            (*halt_io)(UNIT *uptr);
        UNIT             *units;                /* Pointer to units structure */
        void             (*dev_ini)(UNIT *, t_bool);

} DIB;

#define DEV_V_ADDR        DEV_V_UF        /* Pointer to device address */
#define DEV_ADDR_MASK     (0x7ff << DEV_V_ADDR)
#define DEV_V_UADDR       (DEV_V_UF + 12)        /* Device address in Unit */
#define DEV_UADDR         (1 << DEV_V_UADDR)
#define GET_DADDR(x)      (0x7ff & ((x) >> DEV_V_ADDR))
#define DEV_ADDR(x)       ((x) << DEV_V_ADDR)

#define UNIT_V_ADDR       21
#define UNIT_ADDR_MASK    (0x7ff << UNIT_V_ADDR)
#define GET_UADDR(x)      ((UNIT_ADDR_MASK & x) >> UNIT_V_ADDR)
#define UNIT_ADDR(x)      ((x) << UNIT_V_ADDR)

#define PROTECT_V         UNIT_V_UF+12
#define PROTECT           (1 << PROTECT_V)

/* Arithmetic */

/* Instruction format */

/* Globally visible flags */

/* I/O routine functions */


/* low addresses */
#define IPSW              0x00        /* IPSW */
#define ICCW1             0x08        /* ICCW1 */
#define ICCW2             0x10        /* ICCW2 */
#define OEPSW             0x18        /* Exteranl old PSW */
#define OSPSW             0x20        /* Supervisior call old PSW */
#define OPPSW             0x28        /* Program old PSW */
#define OMPSW             0x30        /* Machine check PSW */
#define OIOPSW            0x38        /* IO old PSW */
#define CSW               0x40        /* CSW */
#define CAW               0x48        /* CAW */
#define TIMER             0x50        /* timer */
#define NEPSW             0x58        /* External new PSW */
#define NSPSW             0x60        /* SVC new PSW */
#define NPPSW             0x68        /* Program new PSW */
#define NMPSW             0x70        /* Machine Check PSW */
#define NIOPSW            0x78        /* IOPSW */
#define DIAGAREA          0x80        /* Diag scan area. */

/* Opcode definitions */
#define OP_SPM            0x04
#define OP_BALR           0x05
#define OP_BCTR           0x06
#define OP_BCR            0x07
#define OP_SSK            0x08
#define OP_ISK            0x09
#define OP_SVC            0x0A
#define OP_LPR            0x10
#define OP_LNR            0x11
#define OP_LTR            0x12
#define OP_LCR            0x13
#define OP_NR             0x14
#define OP_CLR            0x15
#define OP_OR             0x16
#define OP_XR             0x17
#define OP_CR             0x19
#define OP_LR             0x18
#define OP_AR             0x1A
#define OP_SR             0x1B
#define OP_MR             0x1C
#define OP_DR             0x1D
#define OP_ALR            0x1E
#define OP_SLR            0x1F
#define OP_LPDR           0x20
#define OP_LNDR           0x21
#define OP_LTDR           0x22
#define OP_LCDR           0x23
#define OP_HDR            0x24
#define OP_LRDR           0x25
#define OP_MXR            0x26
#define OP_MXDR           0x27
#define OP_LDR            0x28
#define OP_CDR            0x29
#define OP_ADR            0x2A
#define OP_SDR            0x2B
#define OP_MDR            0x2C
#define OP_DDR            0x2D
#define OP_AWR            0x2E
#define OP_SWR            0x2F
#define OP_LPER           0x30
#define OP_LNER           0x31
#define OP_LTER           0x32
#define OP_LCER           0x33
#define OP_HER            0x34
#define OP_LRER           0x35
#define OP_AXR            0x36
#define OP_SXR            0x37
#define OP_LER            0x38
#define OP_CER            0x39
#define OP_AER            0x3A
#define OP_SER            0x3B
#define OP_MER            0x3C
#define OP_DER            0x3D
#define OP_AUR            0x3E
#define OP_SUR            0x3F
#define OP_STH            0x40
#define OP_LA             0x41
#define OP_STC            0x42
#define OP_IC             0x43
#define OP_EX             0x44
#define OP_BAL            0x45
#define OP_BCT            0x46
#define OP_BC             0x47
#define OP_LH             0x48
#define OP_CH             0x49
#define OP_AH             0x4A
#define OP_SH             0x4B
#define OP_MH             0x4C
#define OP_CVD            0x4E
#define OP_CVB            0x4F
#define OP_ST             0x50
#define OP_N              0x54
#define OP_CL             0x55
#define OP_O              0x56
#define OP_X              0x57
#define OP_L              0x58
#define OP_C              0x59
#define OP_A              0x5A
#define OP_S              0x5B
#define OP_M              0x5C
#define OP_D              0x5D
#define OP_AL             0x5E
#define OP_SL             0x5F
#define OP_STD            0x60
#define OP_MXD            0x67
#define OP_LD             0x68
#define OP_CD             0x69
#define OP_AD             0x6A
#define OP_SD             0x6B
#define OP_MD             0x6C
#define OP_DD             0x6D
#define OP_AW             0x6E
#define OP_SW             0x6F
#define OP_STE            0x70
#define OP_LE             0x78
#define OP_CE             0x79
#define OP_AE             0x7A
#define OP_SE             0x7B
#define OP_ME             0x7C
#define OP_DE             0x7D
#define OP_AU             0x7E
#define OP_SU             0x7F
#define OP_SSM            0x80
#define OP_LPSW           0x82
#define OP_DIAG           0x83
#define OP_BXH            0x86
#define OP_BXLE           0x87
#define OP_SRL            0x88
#define OP_SLL            0x89
#define OP_SRA            0x8A
#define OP_SLA            0x8B
#define OP_SRDL           0x8C
#define OP_SLDL           0x8D
#define OP_SRDA           0x8E
#define OP_SLDA           0x8F
#define OP_STM            0x90
#define OP_TM             0x91
#define OP_MVI            0x92
#define OP_TS             0x93
#define OP_NI             0x94
#define OP_CLI            0x95
#define OP_OI             0x96
#define OP_XI             0x97
#define OP_LM             0x98
#define OP_SIO            0x9C
#define OP_TIO            0x9D
#define OP_HIO            0x9E
#define OP_TCH            0x9F
#define OP_MVN            0xD1
#define OP_MVC            0xD2
#define OP_MVZ            0xD3
#define OP_NC             0xD4
#define OP_CLC            0xD5
#define OP_OC             0xD6
#define OP_XC             0xD7
#define OP_TR             0xDC
#define OP_TRT            0xDD
#define OP_ED             0xDE
#define OP_EDMK           0xDF
#define OP_MVO            0xF1
#define OP_PACK           0xF2
#define OP_UNPK           0xF3
#define OP_ZAP            0xF8
#define OP_CP             0xF9
#define OP_AP             0xFA
#define OP_SP             0xFB
#define OP_MP             0xFC
#define OP_DP             0xFD

/* Channel sense bytes */
#define     SNS_ATTN      0x80                /* Unit attention */
#define     SNS_SMS       0x40                /* Status modifier */
#define     SNS_CTLEND    0x20                /* Control unit end */
#define     SNS_BSY       0x10                /* Unit Busy */
#define     SNS_CHNEND    0x08                /* Channel end */
#define     SNS_DEVEND    0x04                /* Device end */
#define     SNS_UNITCHK   0x02                /* Unit check */
#define     SNS_UNITEXP   0x01                /* Unit exception */

/* Command masks */
#define     CMD_TYPE      0x3                 /* Type mask */
#define     CMD_CHAN      0x0                 /* Channel command */
#define     CMD_WRITE     0x1                 /* Write command */
#define     CMD_READ      0x2                 /* Read command */
#define     CMD_CTL       0x3                 /* Control command */
#define     CMD_SENSE     0x4                 /* Sense channel command */
#define     CMD_TIC       0x8                 /* Transfer in channel */
#define     CMD_RDBWD     0xc                 /* Read backward */

#define     STATUS_ATTN   0x8000               /* Device raised attention */
#define     STATUS_MOD    0x4000               /* Status modifier */
#define     STATUS_CTLEND 0x2000               /* Control end */
#define     STATUS_BUSY   0x1000               /* Device busy */
#define     STATUS_CEND   0x0800               /* Channel end */
#define     STATUS_DEND   0x0400               /* Device end */
#define     STATUS_CHECK  0x0200               /* Unit check */
#define     STATUS_EXPT   0x0100               /* Unit excpetion */
#define     STATUS_PCI    0x0080               /* Program interupt */
#define     STATUS_LENGTH 0x0040               /* Incorrect lenght */
#define     STATUS_PCHK   0x0020               /* Program check */
#define     STATUS_PROT   0x0010               /* Protection check */
#define     STATUS_CDATA  0x0008               /* Channel data check */
#define     STATUS_CCNTL  0x0004               /* Channel control check */
#define     STATUS_INTER  0x0002               /* Channel interface check */
#define     STATUS_CHAIN  0x0001               /* Channel chain check */

/* channel:
        subchannels = 128
        0 - 7       0x80-0xff
        8 - 127     0x00-0x7f
        128 - +6    0x1xx - 0x6xx

        Devices on channel 0 below number of subchannels have there own
        virtual channel.
        Devices on channel 0 above the number of subchannels are mapped in
        groups of 16 into channels 0 to n.

        Channels 1-n run on channels virtual channels above subchannels.
*/

#define     MAX_CHAN        3
#define     SUB_CHANS       128

void post_extirq();

/* look up device to find subchannel device is on */
int  find_subchan(uint16 device);
int  chan_read_byte(uint16 chan, uint8 *data);
int  chan_write_byte(uint16 chan, uint8 *data);
void set_devattn(uint16 addr, uint8 flags);
void chan_end(uint16 chan, uint8 flags);
int  startio(uint16 addr) ;
int testio(uint16 addr);
int haltio(uint16 addr);
int testchan(uint16 channel);
uint16 scan_chan(uint8 mask);
t_stat chan_boot(uint16 addr, DEVICE *dptr);
t_stat chan_set_devs();
t_stat set_dev_addr(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_dev_addr(FILE * st, UNIT * uptr, int32 v, CONST void *desc);
uint16 loading;
int    irq_pend;

const char ascii_to_ebcdic[128];
const char ebcdic_to_ascii[256];

/* Debuging controls */
#define DEBUG_CMD       0x0000001       /* Show device commands */
#define DEBUG_DATA      0x0000002       /* Show data transfers */
#define DEBUG_DETAIL    0x0000004       /* Show details */
#define DEBUG_EXP       0x0000008       /* Show error conditions */
#define DEBUG_POS       0x0000010       /* Show dasd position data */
#define DEBUG_INST      0x0000020       /* Show instruction execution */
#define DEBUG_IRQ       0x0000100       /* Show IRQ requests */
#define DEBUG_CDATA     0x0000200       /* Show channel data */

extern DEBTAB dev_debug[];
extern DEBTAB crd_debug[];

extern DEVICE cpu_dev;
extern DEVICE cdp_dev;
extern DEVICE cdr_dev;
extern DEVICE lpr_dev;
extern DEVICE con_dev;
extern DEVICE mta_dev;
extern DEVICE mtb_dev;
extern DEVICE dda_dev;
extern DEVICE ddb_dev;
extern DEVICE ddc_dev;
extern DEVICE ddd_dev;
extern DEVICE com_dev;
extern UNIT cpu_unit;

#define NUM_DEVS_CDP        1
#define NUM_DEVS_CDR        1
#define NUM_DEVS_CON        1
#define NUM_DEVS_LPR        1
#define NUM_DEVS_MT         1
#define NUM_UNITS_MT        8
#define NUM_DEVS_DASD       4
#define NUM_UNITS_DASD      8
#define NUM_DEVS_COM        1
#define NUM_UNITS_COM       16

extern void fprint_inst(FILE *, uint16 *);
