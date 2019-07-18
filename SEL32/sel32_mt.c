/* sel32_mt.c: SEL 32 2400 Magnetic tape controller

   Copyright (c) 2016, Richard Cornwell

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

   Magnetic tapes are represented as a series of variable records
   of the form:

        32b byte count
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32b byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0.
*/

#include "sel32_defs.h"
#include "sim_tape.h"

extern t_stat set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_dev_addr(FILE *st, UNIT * uptr, int32 v, CONST void *desc);
extern void chan_end(uint16 chan, uint8 flags);
extern int  chan_read_byte(uint16 chan, uint8 *data);
extern int  chan_write_byte(uint16 chan, uint8 *data);
extern void set_devattn(uint16 addr, uint8 flags);
extern t_stat chan_boot(uint16 addr, DEVICE *dptr);

#ifdef NUM_DEVS_MT
#define BUFFSIZE        (64 * 1024)
#define MTUF_9TR        (1 << MTUF_V_UF)
#define UNIT_MT         UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE | MTUF_9TR
#define DEV_BUF_NUM(x)  (((x) & 07) << DEV_V_UF)
#define GET_DEV_BUF(x)  (((x) >> DEV_V_UF) & 07)

#if 0
CMNDCODE EQU       $-1B
/*           IOCD cmd bits 0-7       OP */
         DATAB     X'23'           1 REW
         DATAB     X'02'           2 READ
         DATAB     X'01'           3 WRITE
         DATAB     X'93'           4 WEOF
         DATAB     X'FF'           5 XCHANP
         DATAB     X'43'           6 ADVR
         DATAB     X'63'           7 ADVF
         DATAB     X'53'           8 BKSR
         DATAB     X'73'           9 BKXF
         DATAB     X'01'           A UPSPACE  (REALLY A WRITE)
         DATAB     X'A3'           B ERASE
         SPACE     3
*        TIMER TABLE VALUES
*
*        ENTRIES CORRESPOND ONE-FOR-ONE WITH ENTRIES IN OTAB
*        AND REPRESENT THE MAXIMUM NUMBER OF SECONDS WHICH THE
*    CORRESPONDING FUNCTION WOULD REQUIRE TO COMPLETE ON A 25 IPS
*    TAPE DRIVE ON WHICH WAS MOUNTED A 2400 ft REEL OF TAPE.
         BOUND     1W
TIMETBL  DATAH     0               OPEN
         DATAH     2               REWIND
         DATAH     128             READ  ASSUME TRANSFER OF 128K
         DATAH     128             WRITE ASSUME TRANSFER OF 128K
         DATAH     2               WRITE END-OF-FILE
         DATAH     0               EXECUTE CHANNEL PROGRAM
         DATAH     2               ADVANCE RECORD
         DATAH     1152            SPACE FORWARD TO END-OF-FILE
         DATAH     2               BACKSPACE RECORD
         DATAH     1152            SPACE BACKWARD TO END-OF-FILE
         DATAH     2               UPSPACE
         DATAH     2               ERASE
         DATAH     0               EJECT
         DATAH     0               CLOSE
         DATAH     0               TERM
         DATAH     0               TEST
TOENTS   EQU       $-TIMETBL/2     NUMBER OF ENTRIES IN TABLE
         SPACE     2
*
*    HANDLER OP CODE VECTOR TABLE
*
         BOUND     1W
OTAB     EQU       $
         ACH      OPEN            0 OPEN
         ACH      RWND            1 RWND
         ACH      READ            2 READ
         ACH      WRITE           3 WRITE
         ACH      WEOF            4 WEOF
         ACH      XCHANP          5 EXECUTE CHANNEL PROGRAM
         ACH      ADVR            6 ADVR
         ACH      ADVF            7 ADVF
         ACH      BKSR            8 BKSR
         ACH      BKSF            9 BKSF
         ACH      UPSP            A UPSP
         ACH      ERASE           B ERASE
         ACH      EJCT            C EJCT
         ACH      CLOSE           D CLOSE
         ACH      TERM            E TERM
         ACH      TEST            F TEST
         SPACE     3
#endif

/* BTP tape commands */
#define MT_INCH             0x00       /* Initialize channel command */
#define MT_WRITE            0x01       /* Write command */
#define MT_READ             0x02       /* Read command */
#define MT_RDBK             0x0c       /* Read Backward */
#define MT_SENSE            0x04       /* Sense command */
#define MT_REW              0x23       /* Rewind command */
#define MT_RUN              0x33       /* Rewind and unload */
#define MT_ERG              0x17       /* Erase 3.5 of tape (maybe 0xA3) */
#define MT_WTM              0x93       /* Write Tape filemark */
#define MT_BSR              0x53       /* Backspace record */
#define MT_BSF              0x73       /* Backspace filemark */
#define MT_FSR              0x43       /* Advance record */
#define MT_FSF              0x63       /* Advance filemark  */
#define MT_MODE             0x83       /* Mode command */
#define MT_MODEMSK          0xC3       /* Mode Mask */

/* FIXME */ /* set correct bits for BTP */
#define MT_MDEN_200         0x00       /* 200 BPI mode 7 track only */
#define MT_MDEN_556         0x40       /* 556 BPI mode 7 track only */
#define MT_MDEN_800         0x80       /* 800 BPI mode 7 track only */
#define MT_MDEN_1600        0xc0       /* 1600 BPI mode 9 track only */
#define MT_MDEN_6250        0x10       /* 6250 BPI mode 9 track only */ 
#define MT_MDEN_MSK         0xc0       /* Density mask */

#define MT_CTL_MSK          0x38       /* Mask for control flags */
#define MT_CTL_NOP          0x00       /* Nop control mode */
#define MT_CTL_NRZI         0x08       /* 9 track 800 bpi mode */
#define MT_CTL_RST          0x10       /* Set density, odd, convert on, trans off */
#define MT_CTL_NOP2         0x18       /* 9 track 1600 NRZI mode */
#define MT_CTL_MD0          0x20       /* Set density, even, convert off, trans off */
#define MT_CTL_MD1          0x28       /* Set density, even, convert off, trans on */
#define MT_CTL_MD2          0x30       /* Set density, odd, convert off, trans off */
#define MT_CTL_MD3          0x38       /* Set density, odd, convert off, trans on */

/* in u3 is device command code and status */
#define MT_CMDMSK            0x00ff       /* Command being run */
#define MT_READDONE          0x0400       /* Read finished, end channel */
#define MT_MARK              0x0800       /* Sensed tape mark in move command */
#define MT_ODD               0x1000       /* Odd parity */
#define MT_TRANS             0x2000       /* Translation turned on ignored 9 track  */
#define MT_CONV              0x4000       /* Data converter on ignored 9 track  */
#define MT_BUSY              0x8000       /* Flag to send a CUE */

/* in u4 is current buffer position */

/* in u5 packs sense byte 0,1 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ       0x80       /* Command reject */
#define SNS_INTVENT      0x40       /* Unit intervention required */
#define SNS_BUSCHK       0x20       /* Parity error on bus */
#define SNS_EQUCHK       0x10       /* Equipment check */
#define SNS_DATCHK       0x08       /* Data Check */
#define SNS_OVRRUN       0x04       /* Data overrun */
#define SNS_WCZERO       0x02       /* Write with no data */
#define SNS_CVTCHK       0x01       /* Data conversion error */

/* Sense byte 1 */
#define SNS_NOISE        0x80       /* Noise record */
#define SNS_TUASTA       0x40       /* Selected and ready */
#define SNS_TUBSTA       0x20       /* Not ready, rewinding. */
#define SNS_7TRACK       0x10       /* Seven track unit */
#define SNS_LOAD         0x08       /* Load Point */
#define SNS_WR           0x04       /* Unit write */
#define SNS_WRP          0x02       /* No write ring */
#define SNS_DENS         0x01       /* Density error 9tr only */

/* Sense byte 2 */
#define SNS_BYTE2        0xc0       /* Not supported feature */

/* Sense byte 3 */
#define SNS_VRC          0x80       /* Veritical parity error */
#define SNS_LRCR         0x40       /* Logituntial parity error */
#define SNS_SKEW         0x20       /* Skew */
#define SNS_CRC          0x10       /* CRC error. 9t only */
#define SNS_SKEWVRC      0x08       /* VRC Skew */
#define SNS_PE           0x04       /* Phase encoding */
#define SNS_BACK         0x01       /* tape in backward status */

#define SNS_BYTE4        0x00       /* Hardware errors not supported */
#define SNS_BYTE5        0x00       /* Hardware errors not supported */

#define MT_CONV1         0x40
#define MT_CONV2         0x80
#define MT_CONV3         0xc0

/* u6 holds the packed characters and unpack counter */
#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark =  0xFFFFFFFF

uint8               mt_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) ;
t_stat              mt_srv(UNIT *);
t_stat              mt_boot(int32, DEVICE *);
void                mt_ini(UNIT *, t_bool);
t_stat              mt_reset(DEVICE *);
t_stat              mt_attach(UNIT *, CONST char *);
t_stat              mt_detach(UNIT *);
t_stat              mt_boot(int32, DEVICE *);

/* One buffer per channel */
uint8               mt_buffer[NUM_DEVS_MT][BUFFSIZE];
uint8               mt_busy[NUM_DEVS_MT];

/* Gould Buffered Tape Processor (BTP) - Model 8051 */
/* Integrated channel controller */

/* Class F MT BTP I/O device status responce in IOCD address pointer location */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03|04 05 06 07|08|09 10 11 12|13 14 15|16|17 18 19 20 21 22 23|24 25 26 27 28 29 30 31| */
/* | Cond Code | 0  0  0 0 |                 Address of status doubleword or zero                  | */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */
/* BIts 0-3 - Condition codes */
/* 0000 - operation accepted with echo */
/* 0001 - channel busy */
/* 0010 - channel inop or undefined */
/* 0011 - subchannel busy */
/* 0100 - status stored */
/* 0101 - unsupported transaction */
/* 1000 - Operation accepted */

/* Status Doubleword */
/* Word 1 */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03 04 05 06 07|08|09 10 11 12|13 14 15|16|17 18 19 20 21 22 23|24 25 26 27 28 29 30 31| */
/* |      Sub Address      |                          24 bit IOCD address                          | */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* Word 2 */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15|16|17 18 19 20 21 22 23 24 25 26 27 28 29 30 31| */
/* |                  16 bit of status             |             Residual Byte Count               | */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */

/* Status Bits */
/* Bit 00 - ECHO    Halt I/O and Stop I/O function */
/* Bit 01 - PCI     Program Controlled Interrupt */
/* Bit 02 - IL      Incorrect Length */
/* Bit 03 - CPC     Channel Program Check */
/* Bit 04 - CDC     Channel Data Check */
/* Bit 05 - CCC     Channel Control Check */
/* Bit 06 - IC      Interface Check */
/* Bit 07 - CHC     Chaining Check */
/* Bit 08 - DB      Device Busy */
/* Bit 09 - SM      Status Modifier */
/* Bit 10 - CNTE    Controller End */
/* Bit 11 - ATTN    Attention */
/* Bit 12 - CE      Channel End */
/* Bit 13 - DE      Device End */
/* Bit 14 - UC      Unit Check */
/* Bit 15 - UE      Unit Exception */

/* 41 Word Main memory channel buffer provided by INCH command */
/* when software is initializing the hannel */
/*  Word 01 - Status Doubleword 1 - Word 1 */
/*  Word 02 - Status Doubleword 1 - Word 2 */
/*  Word 03 - Status Doubleword 2 - Word 1 */
/*  Word 04 - Status Doubleword 2 - Word 2 */
/*  Word 05 - BTP Error Recovery IOCD Address */
/*  Word 06 - Queue Command List Doubleword - Word 1 */
/*  Word 07 - Queue Command List Doubleword - Word 2 */
/*  Word 08 - 16 but Logical Q-pointer | 16 bit Physical Q-pointer */
/*  Word 09 - 16 Active Retry Count | 16 bit Constant Retry Count */
/*  Word 10 - Accumulated Write Count - Drive 0 */
/*  Word 11 - Accumulated Read Count - Drive 0 */
/*  Word 12 - Write Error Count - Drive 0 */
/*  Word 13 - Read Error Count - Drive 0 */
/*  Word 14 - Accumulated Write Count - Drive 1 */
/*  Word 15 - Accumulated Read Count - Drive 1 */
/*  Word 16 - Write Error Count - Drive 1 */
/*  Word 17 - Read Error Count - Drive 1 */
/*  Word 18 - Accumulated Write Count - Drive 2 */
/*  Word 19 - Accumulated Read Count - Drive 2 */
/*  Word 20 - Write Error Count - Drive 2 */
/*  Word 21 - Read Error Count - Drive 2 */
/*  Word 22 - Accumulated Write Count - Drive 3 */
/*  Word 23 - Accumulated Read Count - Drive 3 */
/*  Word 24 - Write Error Count - Drive 3 */
/*  Word 25 - Read Error Count - Drive 3 */
/*  Word 26 - Accumulated Write Count - Drive 4 */
/*  Word 27 - Accumulated Read Count - Drive 4 */
/*  Word 28 - Write Error Count - Drive 4 */
/*  Word 29 - Read Error Count - Drive 4 */
/*  Word 30 - Accumulated Write Count - Drive 5 */
/*  Word 31 - Accumulated Read Count - Drive 5 */
/*  Word 32 - Write Error Count - Drive 5 */
/*  Word 33 - Read Error Count - Drive 5 */
/*  Word 34 - Accumulated Write Count - Drive 6 */
/*  Word 35 - Accumulated Read Count - Drive 6 */
/*  Word 36 - Write Error Count - Drive 6 */
/*  Word 37 - Read Error Count - Drive 6 */
/*  Word 38 - Accumulated Write Count - Drive 7 */
/*  Word 39 - Accumulated Read Count - Drive 7 */
/*  Word 40 - Write Error Count - Drive 7 */
/*  Word 41 - Read Error Count - Drive 7 */


MTAB                mt_mod[] = {
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL},
    {MTUF_9TR, 0, "7 track", "7T", NULL},
    {MTUF_9TR, MTUF_9TR, "9 track", "9T", NULL},
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {0}
};

#ifdef DEFINED_IN_SIM_DEFS_H
/* Unit data structure from sim_defs.h

   Parts of the unit structure are device specific, that is, they are
   not referenced by the simulator control package and can be freely
   used by device simulators.  Fields starting with 'buf', and flags
   starting with 'UF', are device specific.  The definitions given here
   are for a typical sequential device.
*/

struct UNIT {
    UNIT                *next;                          /* next active */
    t_stat              (*action)(UNIT *up);            /* action routine */
    char                *filename;                      /* open file name */
    FILE                *fileref;                       /* file reference */
    void                *filebuf;                       /* memory buffer */
    uint32              hwmark;                         /* high water mark */
    int32               time;                           /* time out */
    uint32              flags;                          /* flags */
    uint32              dynflags;                       /* dynamic flags */
    t_addr              capac;                          /* capacity */
    t_addr              pos;                            /* file position */
    void                (*io_flush)(UNIT *up);          /* io flush routine */
    uint32              iostarttime;                    /* I/O start time */
    int32               buf;                            /* buffer */
    int32               wait;                           /* wait */
    int32               u3;                             /* device specific */
    int32               u4;                             /* device specific */
    int32               u5;                             /* device specific */
    int32               u6;                             /* device specific */
    void                *up7;                           /* device specific */
    void                *up8;                           /* device specific */
    uint16              us9;                            /* device specific */
    uint16              us10;                           /* device specific */
    void                *tmxr;                          /* TMXR linkage */
    t_bool              (*cancel)(UNIT *);
    double              usecs_remaining;                /* time balance for long delays */
    char                *uname;                         /* Unit name */
#ifdef SIM_ASYNCH_IO
    void                (*a_check_completion)(UNIT *);
    t_bool              (*a_is_active)(UNIT *);
    UNIT                *a_next;                        /* next asynch active */
    int32               a_event_time;
    ACTIVATE_API        a_activate_call;
    /* Asynchronous Polling control */
    /* These fields should only be referenced when holding the sim_tmxr_poll_lock */
    t_bool              a_polling_now;                  /* polling active flag */
    int32               a_poll_waiter_count;            /* count of polling threads */
                                                        /* waiting for this unit */
    /* Asynchronous Timer control */
    double              a_due_time;                     /* due time for timer event */
    double              a_due_gtime;                    /* due time (in instructions) for timer event */
    double              a_usec_delay;                   /* time delay for timer event */
#endif /* SIM_ASYNCH_IO */
    };

/* Unit flags */

#define UNIT_V_UF_31    12              /* dev spec, V3.1 */
#define UNIT_V_UF       16              /* device specific */
#define UNIT_V_RSV      31              /* reserved!! */

#define UNIT_ATTABLE    0000001         /* attachable */
#define UNIT_RO         0000002         /* read only */
#define UNIT_FIX        0000004         /* fixed capacity */
#define UNIT_SEQ        0000010         /* sequential */
#define UNIT_ATT        0000020         /* attached */
#define UNIT_BINK       0000040         /* K = power of 2 */
#define UNIT_BUFABLE    0000100         /* bufferable */
#define UNIT_MUSTBUF    0000200         /* must buffer */
#define UNIT_BUF        0000400         /* buffered */
#define UNIT_ROABLE     0001000         /* read only ok */
#define UNIT_DISABLE    0002000         /* disable-able */
#define UNIT_DIS        0004000         /* disabled */
#define UNIT_IDLE       0040000         /* idle eligible */
#endif /* DEFINED_IN_SIM_DEFS_H */

UNIT                mta_unit[] = {
    /* Unit data layout for MT devices */
//  {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1000)},       /* 0 */
    {
    NULL,               /* UNIT *next */             /* next active */
    mt_srv,             /* t_stat (*action) */       /* action routine */
    NULL,               /* char *filename */         /* open file name */
    NULL,               /* FILE *fileref */          /* file reference */
    NULL,               /* void *filebuf */          /* memory buffer */
    0,                  /* uint32 hwmark */          /* high water mark */
    0,                  /* int32 time */             /* time out */
    UNIT_MT,            /* uint32 flags */           /* flags */
    0,                  /* uint32 dynflags */        /* dynamic flags */
    0,                  /* t_addr capac */           /* capacity */
    0,                  /* t_addr pos */             /* file position */
    NULL,               /* void (*io_flush) */       /* io flush routine */
    0,                  /* uint32 iostarttime */     /* I/O start time */
    0,                  /* int32 buf */              /* buffer */
    0,                  /* int32 wait */             /* wait */
    UNIT_ADDR(0x1000),  /* int32 u3 */               /* unit address */
    0,                  /* int32 u4 */               /* currrent buffer position */
    0,                  /* int32 u5 */               /* pack sense bytes 0, 1 and 3 */
    0,                  /* int32 u6 */               /* packed chars and unpack count */
    NULL,               /* void *up7 */              /* device specific */
    NULL,               /* void *up8 */              /* device specific */
    0,                  /* uint16 us9 */             /* device specific */
    0,                  /* uint16 us10 */            /* device specific */
    NULL,               /* void *tmxr */             /* TMXR linkage */
    NULL,               /* t_bool(*cancel)(UNIT *) *//* Cancel I/O routine */
    0,                  /* double usecs_remaining */ /* time balance for long delays */
    NULL,               /* char *uname */            /* Unit name */
    },
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1001)},       /* 1 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1002)},       /* 2 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1003)},       /* 3 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1004)},       /* 4 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1005)},       /* 5 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1006)},       /* 6 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1007)},       /* 7 */
};

#if DEFINED_IN_SEL32_DEFS_H
/* Device information block */
typedef struct dib {
        uint8       mask;               /* device mask */
        uint8       numunits;           /* number of units */
        /* Start I/O */
        uint8       (*start_io)(UNIT *uptr, uint16 chan);
        /* Start a command */
        uint8       (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd);
        /* Stop I/O */
        uint8       (*halt_io)(UNIT *uptr);
        UNIT        *units;             /* Pointer to units structure */
        void        (*dev_ini)(UNIT *, t_bool); /* init function */
        uint8       dev_addr;           /* Device address */
        uint8       dev_class;          /* Device class */
} DIB;

/* CHAN 0x7F000000 UNIT 0x00ff0000 */
#define DEV_V_ADDR        DEV_V_UF              /* Pointer to device address (16) */
#define DEV_V_DADDR       (DEV_V_UF + 8)        /* Device address */
#define DEV_ADDR_MASK     (0x7f << DEV_V_DADDR) /* 24 bits shift */
#define DEV_V_UADDR       (DEV_V_UF)            /* Device address in Unit */
#define DEV_UADDR         (1 << DEV_V_UADDR)
#define GET_DADDR(x)      (0x7f & ((x) >> DEV_V_ADDR))
#define DEV_ADDR(x)       ((x) << DEV_V_ADDR)

#define UNIT_V_ADDR       16
#define UNIT_ADDR_MASK    (0x7fff << UNIT_V_ADDR)
#define GET_UADDR(x)      ((UNIT_ADDR_MASK & x) >> UNIT_V_ADDR)
#define UNIT_ADDR(x)      ((x) << UNIT_V_ADDR)
#endif

//struct dib mta_dib = { 0xF8, NUM_UNITS_MT, NULL, mt_startcmd, NULL, mta_unit, mt_ini};

struct dib          mta_dib = {
        0xFF,           /* uint8 mask */                        /* 256 devices - device mask */
        NUM_UNITS_MT,   /* uint8 numunits */                    /* number of units defined */
        NULL,           /* uint8 (*start_io)(UNIT *uptr, uint16 chan)*/     /* Start I/O */
        mt_startcmd,    /* uint8 (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start a command */
        NULL,           /* uint8 (*halt_io)(UNIT *uptr) */      /* Stop I/O */
        mta_unit,       /* UNIT* units */                       /* Pointer to units structure */
        mt_ini,         /* void  (*dev_ini)(UNIT *, t_bool) */  /* init function */
        0,              /* uint8 dev_addr */                    /* Device address */
        0,              /* uint8 dev_class */                   /* Device class */
};

DEVICE              mta_dev = {
#if 0
    "MTA", mta_unit, NULL, mt_mod,
//    NUM_UNITS_MT, 8, 15, 1, 8, 8,
    NUM_UNITS_MT, 16, 24, 4, 16, 32,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mta_dib, DEV_BUF_NUM(0) | DEV_DISABLE | DEV_DEBUG, 0, dev_debug
#endif
    "MTA",               /* cchar *name */            /* device name */
    mta_unit,            /* UNIT *units */            /* unit array */
    NULL,                /* REG *registers */         /* register array */
    mt_mod,              /* MTAB *modifiers */        /* modifier array */
    NUM_UNITS_MT,        /* uint32 numunits */        /* number of units */
    16,                  /* uint32 aradix */          /* address radix */
    24,                  /* uint32 awidth */          /* address width */
    4,                   /* uint32 aincr */           /* address increment */
    16,                  /* uint32 dradix */          /* data radix */
    32,                  /* uint32 dwidth */          /* data width */
    NULL,                /* t_stat (*examine) */      /* examine routine */
    NULL,                /* t_stat (*deposit) */      /* deposit routine */
    &mt_reset,           /* t_stat (*reset) */        /* reset routine */
    &mt_boot,            /* t_stat (*boot) */         /* boot routine */
    &mt_attach,          /* t_stat (*attach) */       /* attach routine */
    &mt_detach,          /* t_stat (*detach) */       /* detach routine */
    &mta_dib,            /* void *ctxt */             /* (context) device information block pointer */
    DEV_BUF_NUM(0)|DEV_DISABLE|DEV_DEBUG,   /* uint32 flags */    /* device flags */
    0,                   /* uint32 dctrl */           /* debug control flags */
    dev_debug,           /* DEBTAB *debflags */       /* debug flag name array */
    NULL,                /* t_stat (*msize) */        /* memory size change routine */
    NULL,                /* char *lname */            /* logical device name */
    NULL,                /* t_stat (*help) */         /* help function */
    NULL,                /* t_stat (*attach_help) */  /* attach help function */
    NULL,                /* void *help_ctx */         /* Context available to help routines */
    NULL,                /* cchar *(*description) */  /* Device description */
    NULL,                /* BRKTYPTB *brk_types */    /* Breakpoint types */
};

#if NUM_DEVS_MT > 1
UNIT                mtb_unit[] = {
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1800)},       /* 0 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1801)},       /* 1 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1802)},       /* 2 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1803)},       /* 3 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1804)},       /* 4 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1805)},       /* 5 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1806)},       /* 6 */
    {UDATA(&mt_srv, UNIT_MT, 0), 0, UNIT_ADDR(0x1807)},       /* 7 */
};

struct dib mtb_dib = { 0xF8, NUM_UNITS_MT, NULL, mt_startcmd, NULL, mtb_unit, mt_ini};

DEVICE              mtb_dev = {
    "MTB", mtb_unit, NULL, mt_mod,
    NUM_UNITS_MT, 8, 15, 1, 8, 8,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mtb_dib, DEV_BUF_NUM(1) | DEV_DISABLE | DEV_DEBUG, 0, dev_debug
};
#endif

uint8               parity_table[64] = {
    /* 0    1    2    3    4    5    6    7 */
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000
};

uint8                  bcd_to_ebcdic[64] = {
     0x40, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
     0xf8, 0xf9, 0xf0, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
     0x7a, 0x61, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
     0xe8, 0xe9, 0xe0, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
     0x60, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
     0xd8, 0xd9, 0xd0, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
     0x50, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
     0xc8, 0xc9, 0xc0, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f};


/* start an I/O operation */
uint8  mt_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) {
    uint16         addr = GET_UADDR(uptr->u3);
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);
    uint8          ch;

fprintf(stderr, "mt_startcmd chan %0.4x cmd %x\r\n", chan, cmd);
    if (mt_busy[GET_DEV_BUF(dptr->flags)] != 0 || (uptr->u3 & MT_CMDMSK) != 0) {
        sim_debug(DEBUG_CMD, dptr, "CMD busy unit=%d %x\n", unit, cmd);
        uptr->flags |= MT_BUSY;   /* Flag we need to send CUE */
//        mt_busy[GET_DEV_BUF(dptr->flags)] |= 2;
fprintf(stderr, "mt_startcmd busy chan %0.4x cmd %x\r\n", chan, cmd);
        return SNS_BSY;
    }

fprintf(stderr, "mt_startcmd processing chan %0.4x cmd %x\r\n", chan, cmd);
    sim_debug(DEBUG_CMD, dptr, "CMD unit=%d %x\n", unit, cmd);

    switch (cmd & 0xF) {
    case 0x7:              /* Tape motion */
    case 0xf:              /* Tape motion */
    case 0x1:              /* Write command */
    case 0x2:              /* Read command */
    case 0xc:              /* Read backward */
         uptr->u5 = 0;
         uptr->u5 |= SNS_TUASTA << 8;
         if ((uptr->flags & MTUF_9TR) == 0)
             uptr->u5 |= (SNS_7TRACK << 8);
         if (sim_tape_wrp(uptr))
             uptr->u5 |= (SNS_WRP << 8);
         if (sim_tape_bot(uptr))
             uptr->u5 |= (SNS_LOAD << 8);
          /* Fall through */

    case 0x4:              /* Sense */
         uptr->u3 &= ~(MT_CMDMSK);
         uptr->u3 |= cmd & MT_CMDMSK;
         sim_activate(uptr, 1000);       /* Start unit off */
         CLR_BUF(uptr);
         uptr->u4 = 0;
         uptr->u6 = 0;
         mt_busy[GET_DEV_BUF(dptr->flags)] = 1;
         if ((cmd & 0x7) == 0x7) {         /* Quick end channel on control */
//             if ((cmd & 0x30) == 0)  {
 //              mt_busy[GET_DEV_BUF(dptr->flags)] = 0;
  //           }
fprintf(stderr, "mt_startcmd ret CHNEND chan %0.4x cmd %x\r\n", chan, cmd);
             return SNS_CHNEND;
         }
fprintf(stderr, "mt_startcmd ret 0 chan %0.4x cmd %x\r\n", chan, cmd);
         return 0;

    case 0x3:              /* Control */
    case 0xb:              /* Control */
         if ((uptr->flags & MTUF_9TR) == 0)  {
             uptr->u5 |= (SNS_7TRACK << 8);
             if ((cmd & 0xc0) == 0xc0) {
                 uptr->u5 |= SNS_CMDREJ;
                 return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
             } 
             switch((cmd >> 3) & 07) {
             case 0:      /* NOP */
             case 1:      /* Diagnostics */
             case 3:
                  return SNS_CHNEND|SNS_DEVEND ;
             case 2:      /* Reset condition */
                  uptr->u3 &= ~(MT_ODD|MT_TRANS|MT_CONV|MT_MDEN_MSK);
                  uptr->u3 |= (cmd & MT_MDEN_MSK) | MT_ODD | MT_CONV;
                  break;
             case 4:      
                  uptr->u3 &= ~(MT_ODD|MT_TRANS|MT_CONV|MT_MDEN_MSK);
                  uptr->u3 |= (cmd & MT_MDEN_MSK);
                  break;
             case 5:      
                  uptr->u3 &= ~(MT_ODD|MT_TRANS|MT_CONV|MT_MDEN_MSK);
                  uptr->u3 |= (cmd & MT_MDEN_MSK) | MT_TRANS;
                  break;
             case 6:      
                  uptr->u3 &= ~(MT_ODD|MT_TRANS|MT_CONV|MT_MDEN_MSK);
                  uptr->u3 |= (cmd & MT_MDEN_MSK) | MT_ODD;
                  break;
             case 7:      
                  uptr->u3 &= ~(MT_ODD|MT_TRANS|MT_CONV|MT_MDEN_MSK);
                  uptr->u3 |= (cmd & MT_MDEN_MSK) | MT_ODD | MT_TRANS;
                  break;
             }
         } else {
//             if ((cmd & 0xf0) != 0xc0) {
 //                uptr->u5 |= SNS_CMDREJ;
  //           }
             uptr->u3 &= ~MT_MDEN_MSK;
             if (cmd & 0x8)
                 uptr->u3 |= MT_MDEN_800;   /* NRZI */
             else
                 uptr->u3 |= MT_MDEN_1600;  /* PE */
/*           else  /* FIXME */
//                 uptr->u3 |= MT_MDEN_6250;    /* GCR */
         }
         uptr->u5 = 0;
         break;

    case 0x0:               /* INCH command */
         break;

    default:                /* invalid command */
         uptr->u5 |= SNS_CMDREJ;
         break;
    }
    if (uptr->u5 & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
fprintf(stderr, "mt_startcmd ret CHNEND DEVEND chan %0.4x cmd %x\r\n", chan, cmd);
    return SNS_CHNEND|SNS_DEVEND;
}

/* Map simH errors into machine errors */
t_stat mt_error(UNIT *uptr, uint16 addr, t_stat r, DEVICE *dptr)
{
fprintf(stderr, "mt_error status %x\r\n", r);
    mt_busy[GET_DEV_BUF(dptr->flags)] &= ~1;
    switch (r) {
    case MTSE_OK:              /* no error */
       break;

    case MTSE_TMK:              /* tape mark */
       sim_debug(DEBUG_EXP, dptr, "MARK ");
       chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
       return SCPE_OK;

    case MTSE_WRP:              /* write protected */
    case MTSE_UNATT:              /* unattached */
       sim_debug(DEBUG_EXP, dptr, "ATTENTION %d ", r);
       break;

    case MTSE_IOERR:              /* IO error */
    case MTSE_FMT:              /* invalid format */
    case MTSE_RECE:              /* error in record */
       sim_debug(DEBUG_EXP, dptr, "ERROR %d ", r);
       break;
    case MTSE_BOT:              /* beginning of tape */
       sim_debug(DEBUG_EXP, dptr, "BOT ");
       break;
    case MTSE_INVRL:              /* invalid rec lnt */
    case MTSE_EOM:              /* end of medium */
       sim_debug(DEBUG_EXP, dptr, "EOT ");
       break;
    }
    chan_end(addr, SNS_CHNEND|SNS_DEVEND);
    return SCPE_OK;
}

/* Handle processing of tape requests. */
t_stat mt_srv(UNIT * uptr)
{
    uint16              addr = GET_UADDR(uptr->u3);
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 unit = (uptr - dptr->units);
    int                 cmd = uptr->u3 & MT_CMDMSK;
    int                 bufnum = GET_DEV_BUF(dptr->flags);
    t_mtrlnt            reclen;
    t_stat              r = SCPE_ARG;       /* Force error if not set */
    uint8               ch;
    int                 mode = 0;

fprintf(stderr, "mt_srv unit %d\r\n", unit);
    if ((uptr->flags & UNIT_ATT) == 0) {
        uptr->u5 |= SNS_INTVENT;
        if (cmd != MT_SENSE)
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    switch (cmd) {
    case 0:                               /* No command, stop tape */
         sim_debug(DEBUG_DETAIL, dptr, "Idle unit=%d\n", unit);
         break;

    case MT_SENSE:
         ch = uptr->u5 & 0xff;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 1 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         ch = (uptr->u5 >> 8) & 0xff;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 2 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         ch = 0xc0;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 3 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         ch = (uptr->u5 >> 16) & 0xff;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 4 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         ch = 0;
         chan_write_byte(addr, &ch) ;
         chan_write_byte(addr, &ch);
         uptr->u3 &= ~MT_CMDMSK;
         mt_busy[bufnum] &= ~1;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case MT_READ:

         if (uptr->u3 & MT_READDONE) {
            uptr->u3 &= ~(MT_CMDMSK|MT_READDONE);
            mt_busy[bufnum] &= ~1;
            chan_end(addr, SNS_CHNEND|SNS_DEVEND);
            break;
         }

         /* If empty buffer, fill */
         if (BUF_EMPTY(uptr)) {
             sim_debug(DEBUG_DETAIL, dptr, "Read unit=%d ", unit);
             if ((r = sim_tape_rdrecf(uptr, &mt_buffer[bufnum][0], &reclen,
                              BUFFSIZE)) != MTSE_OK) {
            uptr->u3 &= ~(MT_CMDMSK|MT_READDONE);
                 return mt_error(uptr, addr, r, dptr);
             }
             uptr->u4 = 0;
             uptr->hwmark = reclen;
             sim_debug(DEBUG_DETAIL, dptr, "Block %d chars\n", reclen);
         }

         ch = mt_buffer[bufnum][uptr->u4++];
         /* if we are a 7track tape, handle conversion */
         if ((uptr->flags & MTUF_9TR) == 0) {
             mode = (uptr->u3 & MT_ODD) ? 0 : 0100;
             if ((parity_table[ch & 077] ^ (ch & 0100) ^ mode) == 0) {
                 sim_debug(DEBUG_DETAIL, dptr, "Parity error unit=%d %d %03o\n",
                       unit, uptr->u4-1, ch);
                 uptr->u5 |= (SNS_VRC << 16) | SNS_DATCHK;
             }
             ch &= 077;
             if (uptr->u3 & MT_TRANS)
                 ch = bcd_to_ebcdic[ch];
             if (uptr->u3 & MT_CONV) {
                 sim_debug(DEBUG_DATA, dptr, "Read raw data unit=%d %d %02x %02x\n",
                       unit, uptr->u4, ch, uptr->u6);
                 if (uptr->u6 == 0 && uptr->u4 < uptr->hwmark) {
                     uptr->u6 = MT_CONV1 | ch;
                     sim_activate(uptr, 20);
                     return SCPE_OK;
                 } else if ((uptr->u6 & 0xc0) == MT_CONV1) {
                     int t = uptr->u6 & 0x3F;
                     uptr->u6 = MT_CONV2 | ch;
                     ch = (t << 2) | ((ch >> 4) & 03);
                 } else if ((uptr->u6 & 0xc0) == MT_CONV2) {
                     int  t = uptr->u6 & 0xf;
                     uptr->u6 = MT_CONV3 | ch;
                     ch = (t << 4) | ((ch >> 2) & 0xf);
                 } else if ((uptr->u6 & 0xc0) == MT_CONV3) {
                     ch |= ((uptr->u6 & 0x3) << 6);
                     uptr->u6 = 0;
                 }
             }
         }

         /* Send character over to channel */
         if (chan_write_byte(addr, &ch)) {
             sim_debug(DEBUG_DATA, dptr, "Read unit=%d EOR\n\r", unit);
             /* If not read whole record, skip till end */
             if (uptr->u4 < uptr->hwmark) {
                 /* Send dummy character to force SLI */
                 chan_write_byte(addr, &ch);
                 sim_activate(uptr, (uptr->hwmark-uptr->u4) * 20);
                 uptr->u3 |= MT_READDONE;
                 break;
             }
             uptr->u3 &= ~MT_CMDMSK;
             mt_busy[bufnum] &= ~1;
             chan_end(addr, SNS_DEVEND);
         } else {
              sim_debug(DEBUG_DATA, dptr, "Read data unit=%d %d %02x\n\r",
                       unit, uptr->u4, ch);
              if (uptr->u4 >= uptr->hwmark) {       /* In IRG */
                  /* Handle end of record */
                  uptr->u3 &= ~MT_CMDMSK;
                  mt_busy[bufnum] &= ~1;
                  chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             } else
                  sim_activate(uptr, 20);
         }
         break;


    case MT_WRITE:
         /* Check if write protected */
         if (sim_tape_wrp(uptr)) {
             uptr->u5 |= SNS_CMDREJ;
             uptr->u3 &= ~MT_CMDMSK;
             mt_busy[bufnum] &= ~1;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }

         /* Grab data until channel has no more */
         if (chan_read_byte(addr, &ch)) {
             if (uptr->u4 > 0) {                      /* Only if data in record */
                 reclen = uptr->hwmark;
                 sim_debug(DEBUG_DETAIL, dptr, "Write unit=%d Block %d chars\n",
                          unit, reclen);
                 r = sim_tape_wrrecf(uptr, &mt_buffer[bufnum][0], reclen);
                 uptr->u4 = 0;
                 uptr->u3 &= ~MT_CMDMSK;
                 mt_error(uptr, addr, r, dptr);       /* Record errors */
             } else {
                 uptr->u5 |= SNS_WCZERO;              /* Write with no data */
             }
         } else {
             if ((uptr->flags & MTUF_9TR) == 0) {
                 mode = (uptr->u3 & MT_ODD) ? 0 : 0100;
                 if (uptr->u3 & MT_TRANS)
                     ch = (ch & 0xf) | ((ch & 0x30) ^ 0x30);
                 if (uptr->u3 & MT_CONV) {
                     if (uptr->u6 == 0) {
                         uptr->u6 = MT_CONV1 | (ch & 0x3);
                         ch >>= 2;
                     } else if ((uptr->u6 & 0xc0) == MT_CONV1) {
                         int t = uptr->u6 & 0x3;
                         uptr->u6 = MT_CONV2 | (ch & 0xf);
                         ch = (t << 4) | ((ch >> 4) & 0xf);
                    } else if ((uptr->u6 & 0xc0) == MT_CONV2) {
                         int  t = uptr->u6 & 0xf;
                         ch = (t << 2) | ((ch >> 6) & 0x3);
                         ch ^= parity_table[ch & 077] ^ mode;
                         mt_buffer[bufnum][uptr->u4++] = ch;
                         uptr->u6 = 0;
                    }
                }
                ch &= 077;
                ch |= parity_table[ch] ^ mode;
             }
             mt_buffer[bufnum][uptr->u4++] = ch;
             sim_debug(DEBUG_DATA, dptr, "Write data unit=%d %d %02o\n\r",
                      unit, uptr->u4, ch);
             uptr->hwmark = uptr->u4;
             break;
         }
         sim_activate(uptr, 20);
         break;

    case MT_RDBK:
         if (uptr->u3 & MT_READDONE) {
            uptr->u3 &= ~(MT_CMDMSK|MT_READDONE);
            mt_busy[bufnum] &= ~1;
            chan_end(addr, SNS_CHNEND|SNS_DEVEND);
            return SCPE_OK;
         }

         /* If at end of record, fill buffer */
         if (BUF_EMPTY(uptr)) {
              if (sim_tape_bot(uptr)) {
                  uptr->u3 &= ~MT_CMDMSK;
                  mt_busy[GET_DEV_BUF(dptr->flags)] &= ~1;
                  chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                  return SCPE_OK;
              }
             sim_debug(DEBUG_DETAIL, dptr, "Read backward unit=%d ", unit);
             if ((r = sim_tape_rdrecr(uptr, &mt_buffer[bufnum][0], &reclen,
                                BUFFSIZE)) != MTSE_OK) {
            uptr->u3 &= ~(MT_CMDMSK|MT_READDONE);
                  return mt_error(uptr, addr, r, dptr);
             }
             uptr->u4 = reclen;
             uptr->hwmark = reclen;
             sim_debug(DEBUG_DETAIL, dptr, "Binary Block %d chars\n", reclen);
         }

         ch = mt_buffer[bufnum][--uptr->u4];
         if ((uptr->flags & MTUF_9TR) == 0) {
             mode = (uptr->u3 & MT_ODD) ? 0 : 0100;
             ch &= 077;
             if ((parity_table[ch & 077] ^ (ch & 0100) ^ mode) == 0) {
                 uptr->u5 |= (SNS_VRC << 16) | SNS_DATCHK;
             }
             if (uptr->u3 & MT_TRANS)
                 ch = bcd_to_ebcdic[ch];
             if (uptr->u3 & MT_CONV) {
                 if (uptr->u6 == 0 && uptr->u4 < uptr->hwmark) {
                     uptr->u6 = MT_CONV1 | ch;
                     sim_activate(uptr, 20);
                     return SCPE_OK;
                 } else if ((uptr->u6 & 0xc0) == MT_CONV1) {
                     int t = uptr->u6 & 0x3F;
                     uptr->u6 = MT_CONV2 | ch;
                     ch = t | ((ch << 6) & 0xc0);
                 } else if ((uptr->u6 & 0xc0) == MT_CONV2) {
                     int  t = uptr->u6 & 0x3C;
                     uptr->u6 = MT_CONV3 | ch;
                     ch = (t >> 2) | ((ch << 4) & 0xf0);
                 } else if ((uptr->u6 & 0xc0) == MT_CONV3) {
                     ch |= ((uptr->u6 & 0x30) >> 4);
                     uptr->u6 = 0;
                 }
             }
         }

         if (chan_write_byte(addr, &ch)) {
                   sim_debug(DEBUG_DATA, dptr, "Read unit=%d EOR\n\r", unit);
              /* If not read whole record, skip till end */
              if (uptr->u4 >= 0) {
                  sim_activate(uptr, (uptr->u4) * 20);
                  uptr->u3 |= MT_READDONE;
                  return SCPE_OK;
              }
              uptr->u3 &= ~MT_CMDMSK;
              mt_busy[bufnum] &= ~1;
              chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         } else {
              sim_debug(DEBUG_DATA, dptr, "Read data unit=%d %d %02o\n\r",
                            unit, uptr->u4, ch);
              if (uptr->u4 == 0) {      /* In IRG */
                  uptr->u3 &= ~MT_CMDMSK;
                  mt_busy[bufnum] &= ~1;
                  chan_end(addr, SNS_CHNEND|SNS_DEVEND);
               } else
                  sim_activate(uptr, 20);
         }
         break;

    case MT_WTM:
         if (uptr->u4 == 0) {
            if (sim_tape_wrp(uptr)) {
                uptr->u5 |= SNS_CMDREJ;
                uptr->u3 &= ~MT_CMDMSK;
                mt_busy[GET_DEV_BUF(dptr->flags)] &= ~1;
                set_devattn(addr, SNS_DEVEND|SNS_UNITCHK);
//                chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
            }
            uptr->u4 ++;
            sim_activate(uptr, 500);
    //        chan_end(addr, SNS_CHNEND);
         } else {
            sim_debug(DEBUG_DETAIL, dptr, "Write Mark unit=%d\n", unit);
            uptr->u3 &= ~(MT_CMDMSK);
            r = sim_tape_wrtmk(uptr);
            set_devattn(addr, SNS_DEVEND);
            mt_busy[bufnum] &= ~1;
         }
         break;

    case MT_BSR:
         switch (uptr->u4 ) {
         case 0:
              if (sim_tape_bot(uptr)) {
                  uptr->u3 &= ~MT_CMDMSK;
                  mt_busy[GET_DEV_BUF(dptr->flags)] &= ~1;
                  set_devattn(addr, SNS_DEVEND|SNS_UNITCHK);
//                  chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                  return SCPE_OK;
              }
              uptr->u4 ++;
              sim_activate(uptr, 500);
//              chan_end(addr, SNS_CHNEND);
              break;
         case 1:
              uptr->u4++;
              sim_debug(DEBUG_DETAIL, dptr, "Backspace rec unit=%d ", unit);
              r = sim_tape_sprecr(uptr, &reclen);
              /* We don't set EOF on BSR */
              if (r == MTSE_TMK) {
                  uptr->u4++;
                  sim_debug(DEBUG_DETAIL, dptr, "MARK\n");
                  sim_activate(uptr, 50);
              } else {
                  sim_debug(DEBUG_DETAIL, dptr, "%d \n", reclen);
                  sim_activate(uptr, 10 + (10 * reclen));
              }
              break;
         case 2:
              uptr->u3 &= ~(MT_CMDMSK);
              set_devattn(addr, SNS_DEVEND);
              mt_busy[bufnum] &= ~1;
              break;
         case 3:
              uptr->u3 &= ~(MT_CMDMSK);
              set_devattn(addr, SNS_DEVEND|SNS_UNITEXP);
              mt_busy[bufnum] &= ~1;
              break;
         }
         break;

    case MT_BSF:
         switch(uptr->u4) {
         case 0:
              if (sim_tape_bot(uptr)) {
                  uptr->u3 &= ~MT_CMDMSK;
                  mt_busy[bufnum] &= ~1;
                  set_devattn(addr, SNS_DEVEND|SNS_UNITCHK);
//                  chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                  break;
               }
//               chan_end(addr, SNS_CHNEND);
               uptr->u4 ++;
               sim_activate(uptr, 500);
               break;
         case 1:
              sim_debug(DEBUG_DETAIL, dptr, "Backspace file unit=%d\n", unit);
              r = sim_tape_sprecr(uptr, &reclen);
              if (r == MTSE_TMK) {
                  uptr->u4++;
                  sim_debug(DEBUG_DETAIL, dptr, "MARK\n");
                  sim_activate(uptr, 50);
               } else if (r == MTSE_BOT) {
                  uptr->u4+= 2;
                  sim_activate(uptr, 50);
               } else {
                  sim_activate(uptr, 10 + (10 * reclen));
               }
               break;
         case 2:
              uptr->u3 &= ~(MT_CMDMSK);
              set_devattn(addr, SNS_DEVEND|SNS_UNITEXP);
              mt_busy[bufnum] &= ~1;
              break;
         case 3:
              uptr->u3 &= ~(MT_CMDMSK);
              set_devattn(addr, SNS_DEVEND|SNS_UNITCHK);
              mt_busy[bufnum] &= ~1;
              break;
         }
         break;

    case MT_FSR:
         switch(uptr->u4) {
         case 0:
              uptr->u4 ++;
              sim_activate(uptr, 500);
 //             chan_end(addr, SNS_CHNEND);
              break;
         case 1:
              uptr->u4++;
              sim_debug(DEBUG_DETAIL, dptr, "Skip rec unit=%d ", unit);
              r = sim_tape_sprecf(uptr, &reclen);
              if (r == MTSE_TMK) {
                  uptr->u4 = 3;
                  sim_debug(DEBUG_DETAIL, dptr, "MARK\n");
                  sim_activate(uptr, 50);
              } else if (r == MTSE_EOM) {
                  uptr->u4 = 4;
                  sim_activate(uptr, 50);
              } else {
                  sim_debug(DEBUG_DETAIL, dptr, "%d\n", reclen);
                  sim_activate(uptr, 10 + (10 * reclen));
              }
              break;
         case 2:
              uptr->u3 &= ~(MT_CMDMSK);
              set_devattn(addr, SNS_DEVEND);
              mt_busy[bufnum] &= ~1;
              break;
         case 3:
              uptr->u3 &= ~(MT_CMDMSK);
              set_devattn(addr, SNS_DEVEND|SNS_UNITEXP);
              mt_busy[bufnum] &= ~1;
              break;
         case 4:
              uptr->u3 &= ~(MT_CMDMSK);
              set_devattn(addr, SNS_DEVEND|SNS_UNITCHK);
              mt_busy[bufnum] &= ~1;
              break;
         }
         break;

    case MT_FSF:
         switch(uptr->u4) {
         case 0:
              uptr->u4 ++;
              sim_activate(uptr, 500);
//              chan_end(addr, SNS_CHNEND);
              break;
         case 1:
              sim_debug(DEBUG_DETAIL, dptr, "Skip rec unit=%d ", unit);
              r = sim_tape_sprecf(uptr, &reclen);
              if (r == MTSE_TMK) {
                  uptr->u4++;
                  sim_debug(DEBUG_DETAIL, dptr, "MARK\n");
                  sim_activate(uptr, 50);
              } else if (r == MTSE_EOM) {
                  uptr->u4+= 2;
                  sim_activate(uptr, 50);
              } else {
                  sim_debug(DEBUG_DETAIL, dptr, "%d\n", reclen);
                  sim_activate(uptr, 10 + (10 * reclen));
              }
              break;
         case 2:
              uptr->u3 &= ~(MT_CMDMSK);
              set_devattn(addr, SNS_DEVEND);
              mt_busy[bufnum] &= ~1;
              sim_debug(DEBUG_DETAIL, dptr, "Skip done unit=%d\n", unit);
              break;
         case 3:
              uptr->u3 &= ~(MT_CMDMSK);
              set_devattn(addr, SNS_DEVEND|SNS_UNITCHK);
              mt_busy[bufnum] &= ~1;
              break;
         }
         break;


    case MT_ERG:
         switch (uptr->u4) {
         case 0:
              if (sim_tape_wrp(uptr)) {
                  uptr->u5 |= SNS_CMDREJ;
                  uptr->u3 &= ~MT_CMDMSK;
                  mt_busy[bufnum] &= ~1;
//                  chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                  set_devattn(addr, SNS_DEVEND|SNS_UNITCHK);
              } else {
                  uptr->u4 ++;
                  sim_activate(uptr, 500);
//               chan_end(addr, SNS_CHNEND);
              }
              break;
         case 1:
              sim_debug(DEBUG_DETAIL, dptr, "Erase unit=%d\n", unit);
              r = sim_tape_wrgap(uptr, 35);
              sim_activate(uptr, 5000);
              uptr->u4++;
              break;
         case 2:
              uptr->u3 &= ~(MT_CMDMSK);
              set_devattn(addr, SNS_DEVEND);
              mt_busy[bufnum] &= ~1;
         }
         break;

    case MT_REW:
         if (uptr->u4 == 0) {
             uptr->u4 ++;
             sim_activate(uptr, 30000);
//               chan_end(addr, SNS_CHNEND);
             mt_busy[bufnum] &= ~1;
         } else {
             sim_debug(DEBUG_DETAIL, dptr, "Rewind unit=%d\n", unit);
             uptr->u3 &= ~(MT_CMDMSK);
             r = sim_tape_rewind(uptr);
             set_devattn(addr, SNS_DEVEND);
         }
         break;

    case MT_RUN:
         if (uptr->u4 == 0) {
             uptr->u4 ++;
             mt_busy[bufnum] &= ~1;
             sim_activate(uptr, 30000);
//               chan_end(addr, SNS_CHNEND);
         } else {
             sim_debug(DEBUG_DETAIL, dptr, "Unload unit=%d\n", unit);
             uptr->u3 &= ~(MT_CMDMSK);
             r = sim_tape_detach(uptr);
         }
         break;
    }
    return SCPE_OK;
}

/* initialize the tape chan/unit */
void mt_ini(UNIT *uptr, t_bool f)
{
    DEVICE *dptr = find_dev_from_unit(uptr);

    uptr->u3 &= ~0xffff;                        /* clear out the flags but leave ch/sa */
    if ((uptr->flags & MTUF_9TR) == 0)          /* if not 9 track tape, make 800 bpi */
        uptr->u3 |= MT_ODD|MT_CONV|MT_MDEN_800; /* set 800 bpi options */
    mt_busy[GET_DEV_BUF(dptr->flags)] = 0;      /* set not busy */
fprintf(stderr, "mt_int device %s unit %x\r\n", dptr->name, GET_UADDR(uptr->u3));
}

/* reset the mag tape */
t_stat mt_reset(DEVICE *dptr)
{
    /* nothing to do?? */
fprintf(stderr, "mt_reset name %s\r\n", dptr->name);
    return SCPE_OK;
}

/* attach the specified file to the tape device */
t_stat mt_attach(UNIT * uptr, CONST char *file)
{
    uint16              addr = GET_UADDR(uptr->u3);     /* get address of mt device */
    t_stat              r;

fprintf(stderr, "mt_attach1 filename %s\r\n", file);
    if ((r = sim_tape_attach(uptr, file)) != SCPE_OK)   /* mount the specified file to the MT */
       return r;                                        /* report any error */
fprintf(stderr, "mt_attach2 status %x\r\n", r);
    set_devattn(addr, SNS_DEVEND);                      /* ready int???? */
    return SCPE_OK;                                     /* return good status */
}

/* detach the MT device and unload any tape */
t_stat mt_detach(UNIT *uptr)
{
fprintf(stderr, "mt_detach MT\r\n");
    uptr->u3 = 0;
    return sim_tape_detach(uptr);
}

/* boot from the specified tape unit */
t_stat mt_boot(int32 unit_num, DEVICE *dptr)
{
    UNIT    *uptr = &dptr->units[unit_num]; /* find tape unit pointer */
    t_stat  r;

fprintf(stderr, "mt_boot MT Unit number %0.8x\r\n", unit_num);
    if ((uptr->flags & UNIT_ATT) == 0)      /* Is MT device already attached? */
        return SCPE_UNATT;                  /* not attached, return error */
    if ((uptr->flags & MTUF_9TR) == 0) {    /* is tape a 9 track? */
        uptr->u3 &= ~0xffff;                /* clear out old status */
        uptr->u3 |= MT_ODD|MT_CONV|MT_MDEN_800; /* set 800bpi & odd parity */
    }
    return chan_boot(GET_UADDR(uptr->u3), dptr);    /* boot the ch/sa */
}

#endif /* NUM_DEVS_MT */
