/* sel32_mt.c: SEL 32 2400 Magnetic tape controller

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

extern t_stat     set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat     show_dev_addr(FILE *st, UNIT *uptr, int32 v, CONST void *desc);
extern void       chan_end(uint16 chan, uint8 flags);
extern int        chan_read_byte(uint16 chan, uint8 *data);
extern int        chan_write_byte(uint16 chan, uint8 *data);
extern void       set_devattn(uint16 addr, uint8 flags);
extern t_stat     chan_boot(uint16 addr, DEVICE *dptr);
extern uint32     SPAD[];           /* cpu SPAD */

#ifdef NUM_DEVS_MT
#define BUFFSIZE        (64 * 1024)
#define UNIT_MT         UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE 
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
#define MT_NOP              0x03       /* Control command */
#define MT_SENSE            0x04       /* Sense command */
#define MT_RDBK             0x0c       /* Read Backward */
#define MT_RDCMP            0x13       /* Read and compare command */
#define MT_REW              0x23       /* Rewind command */
#define MT_RUN              0x33       /* Rewind and unload */
#define MT_FSR              0x43       /* Advance record */
#define MT_BSR              0x53       /* Backspace record */
#define MT_FSF              0x63       /* Advance filemark  */
#define MT_BSF              0x73       /* Backspace filemark */
#define MT_SETM             0x83       /* Set Mode command */
#define MT_WTM              0x93       /* Write Tape filemark */
#define MT_ERG              0xA3       /* Erase 3.5 of tape */
#define MT_MODEMSK          0xFF       /* Mode Mask */

/* set mode bits for BTP (MT_SETM) */
#define MT_MODE_AUTO        0x80       /* =0 Perform auto error recodery on read */
#define MT_MODE_FORCE       0x80       /* =1 Read regardless if error recovery fails */
#define MT_MDEN_800         0x40       /* =0 select 800 BPI NRZI mode 9 track only */
#define MT_MDEN_1600        0x40       /* =1 select 1600 BPI PE mode 9 track only */
#define MT_MDEN_6250        0x20       /* =0 Use mode from bit one for NRZI/PE */
#define MT_MDEN_6250        0x20       /* =1 6250 BPI GCR mode 9 track only */ 
#define MT_MDEN_SCATGR      0x01       /* =1 HSTP scatter/gather mode */
#define MT_MDEN_MSK         0xc0       /* Density mask */

#define MT_CTL_MSK          0x38       /* Mask for control flags */
#define MT_CTL_NOP          0x00       /* Nop control mode */
#define MT_CTL_NRZI         0x08       /* 9 track 800 bpi mode */
#define MT_CTL_RST          0x10       /* Set density, odd, convert on, trans off */
#define MT_CTL_NOP2         0x18       /* 9 track 1600 NRZI mode */

/* in u3 is device command code and status */
#define MT_CMDMSK            0x00ff       /* Command being run */
#define MT_READDONE          0x0400       /* Read finished, end channel */
#define MT_MARK              0x0800       /* Sensed tape mark in move command */
#define MT_ODD               0x1000       /* Odd parity */
#define MT_TRANS             0x2000       /* Translation turned on ignored 9 track  */
#define MT_CONV              0x4000       /* Data converter on ignored 9 track  */
#define MT_BUSY              0x8000       /* Flag to send a CUE */

/* in u4 is current buffer position */

/* in u5 packs sense byte 0, 1, 2 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ       0x80000000       /* Command reject */
#define SNS_INTVENT      0x40000000       /* Unit intervention required */
#define SNS_SPARE1       0x20000000       /* Spare */
#define SNS_EQUCHK       0x10000000       /* Equipment check */
#define SNS_DATCHK       0x08000000       /* Data Check */
#define SNS_OVRRUN       0x04000000       /* Data overrun */
#define SNS_SPARE2       0x02000000       /* Spare */
#define SNS_LOOKER       0x01000000       /* lookahead error */

/* Sense byte 1 */
#define SNS_PEMODER      0x800000       /* PE tape mode error */
#define SNS_TPECHK       0x400000       /* Tape PE mode check */
#define SNS_FMRKDT       0x200000       /* File mark detected EOF */
#define SNS_CORERR       0x100000       /* Corrected Error */
#define SNS_HARDER       0x080000       /* Hard Error */
#define SNS_MRLDER       0x040000       /* Mode register load error */
#define SNS_DATAWR       0x020000       /* Data written */
#define SNS_SPARE3       0x010000       /* Spare */

/* Sense byte 2 mode bits */
#define SNS_MREG0        0x8000       /* Mode register bit 0 */
#define SNS_MREG1        0x4000       /* Mode register bit 1 */
#define SNS_MREG2        0x2000       /* Mode register bit 2 */
#define SNS_MREG3        0x1000       /* Mode register bit 3 */
#define SNS_MREG4        0x0800       /* Mode register bit 4 */
#define SNS_MREG5        0x0400       /* Mode register bit 5 */
#define SNS_MREG6        0x0200       /* Mode register bit 6 */
#define SNS_MREG7        0x0100       /* Mode register bit 7 */

/* Sense byte 3 */
#define SNS_RDY          0x80       /* Drive Ready */
#define SNS_ONLN         0x40       /* Drive Online */
#define SNS_WRP          0x20       /* Drive is file protected (write ring missing) */
#define SNS_NRZI         0x10       /* Drive is NRZI */
#define SNS_SPARE4       0x08       /* Spare */
#define SNS_LOAD         0x04       /* Drive is at load point */
#define SNS_EOT          0x02       /* Drive is at EOT */
#define SNS_SPARE5       0x01       /* Spare */

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
t_stat              mt_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *mt_description(DEVICE *);

/* One buffer per channel */
uint8               mt_buffer[NUM_DEVS_MT][BUFFSIZE];
uint8               mt_busy[NUM_DEVS_MT];

/* Gould Buffered Tape Processor (BTP) - Model 8051 */
/* Integrated channel controller */

/* Class F MT BTP I/O device status responce in IOCD address pointer location */
/* |-------+-------+-------+-------+-------+-------+-------+-------| */
/* |0 0 0 0|0 0 0 0|0 0 1 1|1 1 1 1|1 1 1 1|2 2 2 2|2 2 2 2|2 2 3 3| */
/* |0 1 2 3|4 5 6 7|8 9 0 1|2 3 4 5|6 7 8 9|0 1 2 3|4 5 6 7|8 9 3 1| */
/* | Cond  |0 0 0 0|         Address of status doubleword or zero  | */
/* | Code                                                          | */
/* |-------+-------+-------+-------+-------+-------+-------+-------| */
/* */
/* Bits 0-3 - Condition codes */
/* 0000 - operation accepted will echo status not sent by the channel */
/* 0001 - channel busy */
/* 0010 - channel inop or undefined */
/* 0011 - subchannel busy */
/* 0100 - status stored */
/* 0101 - unsupported transaction */
/* 1000 - Operation accepted/queued, no echo status */

/* Status Doubleword */
/* Word 1 */
/* |-------+-------+-------+-------+-------+-------+-------+-------| */
/* |0 0 0 0|0 0 0 0|0 0 1 1|1 1 1 1|1 1 1 1|2 2 2 2|2 2 2 2|2 2 3 3| */
/* |0 1 2 3|4 5 6 7|8 9 0 1|2 3 4 5|6 7 8 9|0 1 2 3|4 5 6 7|8 9 3 1| */
/* |Sub Address    |                24 bit IOCD address            | */
/* |-------+-------+-------+-------+-------+-------+-------+-------| */
/* Word 2 */
/* |-------+-------+-------+-------+-------+-------+-------+-------| */
/* |0 0 0 0|0 0 0 0|0 0 1 1|1 1 1 1|1 1 1 1|2 2 2 2|2 2 2 2|2 2 3 3| */
/* |0 1 2 3|4 5 6 7|8 9 0 1|2 3 4 5|6 7 8 9|0 1 2 3|4 5 6 7|8 9 3 1| */
/* |        16 bit of status       |      Residual Byte Count      | */
/* |-------+-------+-------+-------+-------+-------+-------+-------| */

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
/* when software is initializing the channel */
/*  Word 01 - Status Doubleword 1 - Word 1 */
/*  Word 02 - Status Doubleword 1 - Word 2 */
/*  Word 03 - Status Doubleword 2 - Word 1 */
/*  Word 04 - Status Doubleword 2 - Word 2 */
/*  Word 05 - BTP Error Recovery IOCD Address */
/*  Word 06 - Queue Command List Doubleword - Word 1 */
/*  Word 07 - Queue Command List Doubleword - Word 2 */
/*  Word 08 - 16 bit Logical Q-pointer  | 16 bit Physical Q-pointer */
/*  Word 09 - 16 bit Active Retry Count | 16 bit Constant Retry Count */
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

int32               valid_dens = MT_800_VALID|MT_1600_VALID|MT_6250_VALID;
MTAB                mt_mod[] = {
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL, NULL, NULL,
       "Write ring in place"},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL, NULL, NULL,
       "No write ring in place"},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "DENSITY", "DENSITY", 
        &sim_tape_set_dens, &sim_tape_show_dens, &valid_dens, 
       "Set tape density"},
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL,
       "Set/Display tape format (SIMH, E11, TPC, P7B)"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL, "Device address"},
    {0}
};

UNIT                mta_unit[] = {
    /* Unit data layout for MT devices */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1000)},       /* 0 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1001)},       /* 1 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1002)},       /* 2 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1003)},       /* 3 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1004)},       /* 4 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1005)},       /* 5 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1006)},       /* 6 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1007)},       /* 7 */
};

/* channel program information */
CHANP           mta_chp[NUM_UNITS_MT] = {0};

DIB             mta_dib = {
    NULL,           /* uint8 (*pre_io)(UNIT *, uint16)*/     /* Pre Start I/O */
    mt_startcmd,    /* uint8 (*start_cmd)(UNIT *, uint16, uint8)*/ /* Start a command */
    NULL,           /* uint8 (*halt_io)(UNIT *) */           /* Stop I/O */
    NULL,           /* uint8 (*test_io)(UNIT *) */           /* Test I/O */
    NULL,           /* uint8 (*post_io)(UNIT *) */           /* Post I/O */
    mt_ini,         /* void  (*dev_ini)(UNIT *, t_bool) */   /* init function */
    mta_unit,       /* UNIT* units */                        /* Pointer to units structure */
    mta_chp,        /* CHANP* chan_prg */                    /* Pointer to chan_prg structure */
    NUM_UNITS_MT,   /* uint8 numunits */                     /* number of units defined */
    0xFF,           /* uint8 mask */                         /* 256 devices - device mask */
    0x1000,         /* uint16 chan_addr */                   /* parent channel address */
    0,              /* uint32 chan_fifo_in */                /* fifo input index */
    0,              /* uint32 chan_fifo_out */               /* fifo output index */
    0,              /* uint32 chan_fifo[FIFO_SIZE] */        /* interrupt status fifo for channel */
};

DEVICE          mta_dev = {
    "MTA", mta_unit, NULL, mt_mod,
    NUM_UNITS_MT, 16, 24, 4, 16, 32,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mta_dib, DEV_BUF_NUM(0) | DEV_DISABLE | DEV_DEBUG | DEV_TAPE, 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL, &mt_description

};

#if NUM_DEVS_MT > 1
/* channel program information */
CHANP           mtb_chp[NUM_UNITS_MT] = {0};

UNIT            mtb_unit[] = {
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1800)},       /* 0 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1801)},       /* 1 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1802)},       /* 2 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1803)},       /* 3 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1804)},       /* 4 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1805)},       /* 5 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1806)},       /* 6 */
    {UDATA(&mt_srv, UNIT_MT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x1807)},       /* 7 */
};

/* device information block */
DIB             mtb_dib = {
    NULL,           /* uint8 (*pre_io)(UNIT *, uint16)*/   /* Pre Start I/O */
    mt_startcmd,    /* uint8 (*start_cmd)(UNIT *, uint16, uint8)*/ /* Start a command */
    NULL,           /* uint8 (*halt_io)(UNIT *) */          /* Stop I/O */
    NULL,           /* uint8 (*test_io)(UNIT *) */          /* Test I/O */
    NULL,           /* uint8 (*post_io)(UNIT *) */          /* Post I/O */
    mt_ini,         /* void  (*dev_ini)(UNIT *, t_bool) */  /* init function */
    mtb_unit,       /* UNIT* units */                       /* Pointer to units structure */
    mtb_chp,        /* CHANP* chan_prg */                   /* Pointer to chan_prg structure */
    NUM_UNITS_MT,   /* uint8 numunits */                    /* number of units defined */
    0xFF,           /* uint8 mask */                        /* 256 devices - device mask */
    0x1000,         /* uint16 chan_addr */                  /* parent channel address */
    0,              /* uint32 chan_fifo_in */               /* fifo input index */
    0,              /* uint32 chan_fifo_out */              /* fifo output index */
    0,              /* uint32 chan_fifo[FIFO_SIZE] */       /* interrupt status fifo for channel */
};

DEVICE          mtb_dev = {
    "MTB", mtb_unit, NULL, mt_mod,
    NUM_UNITS_MT, 8, 15, 1, 8, 8,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mtb_dib, DEV_BUF_NUM(1) | DEV_DISABLE | DEV_DEBUG | DEV_TAPE, 0, dev_debug
    NULL, NULL, &mt_help, NULL, NULL, &mt_description
};
#endif

/* start an I/O operation */
uint8  mt_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd)
{
    DEVICE      *dptr = find_dev_from_unit(uptr);
    int         unit = (uptr - dptr->units);

    sim_debug(DEBUG_EXP, &mta_dev, "mt_startcmd entry chan %x cmd %x\n", chan, cmd);
    if (mt_busy[GET_DEV_BUF(dptr->flags)] != 0 || (uptr->u3 & MT_CMDMSK) != 0) {
        sim_debug(DEBUG_EXP, &mta_dev, "mt_startcmd busy chan %x cmd %x\n", chan, cmd);
        uptr->flags |= MT_BUSY;                     /* Flag we need to send CUE */
        return SNS_BSY;
    }

    sim_debug(DEBUG_EXP, &mta_dev, "mt_startcmd processing unit %x cmd %x\n", unit, cmd);

    switch (cmd & 0xF) {
    case 0x0:                                       /* INCH command */
        /* u4 has INCH buffer address and us9 the count */
        /* just return OK and channel software will use u4 as status buffer */
        sim_debug(DEBUG_DETAIL, &mta_dev, "mt_startcmd INCH done unit %x cmd %x\n", unit, cmd);
        /* UTX_needs_interrupt */
        cmd = MT_CMDMSK;                            /* insert INCH cmd as 0xff */
        /* fall through */
    case 0x3:                                       /* Tape motion commands */
        /* UTX_needs_interrupt */
        /* fall through */
    case 0x1:                                       /* Write command */
    case 0x2:                                       /* Read command */
    case 0xc:                                       /* Read backward */
        if (cmd != 0x03)                            /* if this is a nop do not zero status */
            uptr->u5 = (uptr->u5 & 0x0000ff00);     /* clear all but byte 2 */
        uptr->u5 |= (SNS_RDY|SNS_ONLN);             /* set ready status */
        if (sim_tape_wrp(uptr))
            uptr->u5 |= (SNS_WRP);                  /* write protected */
        if (sim_tape_bot(uptr))
            uptr->u5 |= (SNS_LOAD);                 /* tape at load point */
        if (sim_tape_eot(uptr))
            uptr->u5 |= (SNS_EOT);                  /* tape at EOM */
        /* Fall through */

    case 0x4:              /* Sense */
        uptr->u3 &= ~(MT_CMDMSK);                   /* clear out last cmd */
        uptr->u3 |= cmd & MT_CMDMSK;                /* insert new cmd */
        CLR_BUF(uptr);                              /* buffer is empty */
        /* INCH cmd has iNCH buffer address in u4, so leave it */
        if (cmd != MT_CMDMSK)
            uptr->u4 = 0;                           /* reset buffer position pointer */
        sim_activate(uptr, 100);                    /* Start unit off */
        mt_busy[GET_DEV_BUF(dptr->flags)] = 1;      /* show we are busy */
        sim_debug(DEBUG_EXP, &mta_dev, "mt_startcmd sense return 0 chan %x cmd %x\n", chan, cmd);
        return 0;

    default:                                        /* invalid command */
        sim_debug(DEBUG_EXP, &mta_dev, "mt_startcmd CMDREJ return chan %d cmd %x\n", chan, cmd);
        uptr->u5 |= SNS_CMDREJ;
        break;
    }
    if (uptr->u5 & 0xff000000)                      /* errors? */
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    sim_debug(DEBUG_EXP, &mta_dev, "mt_startcmd ret CHNEND|DEVEND chan %d unit %x cmd %x\n",
                chan, unit, cmd);
    return SNS_CHNEND|SNS_DEVEND;
}

/* Map simH errors into machine errors */
t_stat mt_error(UNIT *uptr, uint16 addr, t_stat r, DEVICE *dptr)
{
    sim_debug(DEBUG_CMD, &mta_dev, "mt_error status %x\n", r);
    mt_busy[GET_DEV_BUF(dptr->flags)] &= ~1;        /* not busy anymore */

    switch (r) {                                    /* switch on return value */
    case MTSE_OK:                                   /* no error */
        break;

    case MTSE_TMK:                                  /* tape mark */
        sim_debug(DEBUG_CMD, &mta_dev, "FILE MARK\n");
        uptr->u5 |= SNS_FMRKDT;                     /* file mark detected */
        chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
        break;

    case MTSE_WRP:                                  /* write protected */
        uptr->u5 |= SNS_WRP;                        /* write protected */
        sim_debug(DEBUG_CMD, &mta_dev, "WRITE PROTECT %d ", r); /* operator intervention */
        break;

    case MTSE_UNATT:                                /* unattached */
        uptr->u5 |= SNS_INTVENT;                    /* unit intervention required */
        sim_debug(DEBUG_CMD, &mta_dev, "ATTENTION %d ", r); /* operator intervention */
        chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
        break;

    case MTSE_IOERR:                                /* IO error */
    case MTSE_FMT:                                  /* invalid format */
    case MTSE_RECE:                                 /* error in record */
        sim_debug(DEBUG_CMD, &mta_dev, "ERROR %d ", r);
        break;

    case MTSE_BOT:                                  /* beginning of tape */
        uptr->u5 |= SNS_LOAD;                       /* tape at BOT */
        sim_debug(DEBUG_CMD, &mta_dev, "BOT ");
        break;

    case MTSE_INVRL:                                /* invalid rec lnt */
    case MTSE_EOM:                                  /* end of medium */
        uptr->u5 |= SNS_EOT;                        /* tape at EOT */
        sim_debug(DEBUG_CMD, &mta_dev, "EOT ");
        chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
        break;
    }
    chan_end(addr, SNS_CHNEND|SNS_DEVEND);          /* we are done with command */
    return SCPE_OK;
}

/* Handle processing of tape requests. */
t_stat mt_srv(UNIT *uptr)
{
    uint16      addr = GET_UADDR(uptr->u3);
    DEVICE      *dptr = find_dev_from_unit(uptr);
    int         unit = (uptr - dptr->units);
    int         cmd = uptr->u3 & MT_CMDMSK;
    int         bufnum = GET_DEV_BUF(dptr->flags);
    t_mtrlnt    reclen;
    t_stat      r = SCPE_ARG;                       /* Force error if not set */
    uint8       ch;

    sim_debug(DEBUG_DATA, &mta_dev, "mt_srv unit %d cmd %x\n", unit, cmd);
    if ((uptr->flags & UNIT_ATT) == 0) {            /* unit attached status */
        uptr->u5 |= SNS_INTVENT;                    /* unit intervention required */
        mt_busy[bufnum] &= ~1;                      /* make our buffer not busy */
        if (cmd != MT_SENSE) {                      /* we are completed with unit check status */
            chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            return SCPE_OK;
        }
    }

    switch (cmd) {
    case MT_CMDMSK:   /* 0x0ff for inch 0x00 */     /* INCH is for channel, nothing for us */
        /* uptr->u4 has INCH buffer address, just leave it */
        sim_debug(DEBUG_CMD, &mta_dev, "mt_srv cmd 0 INCH unit=%d\n", unit);
        uptr->u3 &= ~MT_CMDMSK;                     /* clear the cmd */
        mt_busy[bufnum] &= ~1;                      /* make our buffer not busy */
        chan_end(addr, SNS_CHNEND|SNS_DEVEND);      /* we are done dev|chan end */
        break;

    case MT_NOP:    /* 0x03 */                      /* NOP motion command */
        uptr->u3 &= ~MT_CMDMSK;                     /* clear the cmd */
        mt_busy[bufnum] &= ~1;                      /* make our buffer not busy */
        chan_end(addr, SNS_CHNEND|SNS_DEVEND);      /* we are done dev|chan end */
        break;

    case MT_SENSE:  /* 0x04 */                      /* get sense data */
        sim_debug(DEBUG_CMD, &mta_dev, "mt_srv cmd 4 SENSE unit=%d\n", unit);
        ch = (uptr->u5 >> 24) & 0xff;               /* get sense byte 0 status */
        sim_debug(DEBUG_DETAIL, &mta_dev, "sense unit %d byte 0 %x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 0 */
        ch = (uptr->u5 >> 16) & 0xff;               /* get sense byte 1 status */
        sim_debug(DEBUG_DETAIL, &mta_dev, "sense unit %d byte 1 %x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 1 */
        ch = (uptr->u5 >> 8) & 0xff;                /* get sense byte 2 status */
        sim_debug(DEBUG_DETAIL, &mta_dev, "sense unit %d byte 2 %x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 2 */
        ch = (uptr->u5 >> 0) & 0xff;                /* get sense byte 3 status */
        sim_debug(DEBUG_DETAIL, &mta_dev, "sense unit %d byte 3 %x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 3 */
        ch = 4;
        uptr->u3 &= ~MT_CMDMSK;                     /* clear the cmd */
        mt_busy[bufnum] &= ~1;                      /* make our buffer not busy */
        chan_end(addr, SNS_CHNEND|SNS_DEVEND);      /* we are done dev|chan end */
        sim_debug(DEBUG_CMD, &mta_dev, "mt_srv SENSE %x char complete unit=%d\n", uptr->u4,
                         unit);
        break;

    case MT_READ:   /* 0x02 */                      /* read a record from the device */
        sim_debug(DEBUG_DATA, &mta_dev, "mt_srv cmd 2 READ unit=%d\n", unit);
        if (uptr->u3 & MT_READDONE) {               /* is the read complete */
            uptr->u5 &= ~(SNS_LOAD|SNS_EOT);        /* reset BOT & EOT */
            if (sim_tape_eot(uptr)) {               /* see if at EOM */
                uptr->u5 |= SNS_EOT;                /* set EOT status */
            }
            uptr->u3 &= ~(MT_CMDMSK|MT_READDONE);   /* clear all but readdone & cmd */
            uptr->u3 &= ~MT_CMDMSK;                 /* clear the cmd */
            mt_busy[bufnum] &= ~1;                  /* not busy anymore */
            chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* set chan end, dev end status */
            sim_debug(DEBUG_CMD, &mta_dev, "mt_srv READ %x char complete unit=%d sense %x\n",
                     uptr->u4, unit, uptr->u5);
            break;
        }
        /* read is not completed, get an input char */
        /* If empty buffer, fill */
        if (BUF_EMPTY(uptr)) {
            /* buffer is empty, so fill it with next record data */
            if ((r = sim_tape_rdrecf(uptr, &mt_buffer[bufnum][0], &reclen, BUFFSIZE)) != MTSE_OK) {
                sim_debug(DEBUG_DETAIL, &mta_dev, "mt_srv READ fill buffer unit=%d\n", unit);
                uptr->u3 &= ~(MT_CMDMSK|MT_READDONE);   /* clear all but readdone & cmd */
                return mt_error(uptr, addr, r, dptr);   /* process any error & return status */
            }
            uptr->u5 &= ~(SNS_LOAD|SNS_EOT);        /* reset BOT & EOT */
            uptr->u4 = 0;                           /* reset buffer position */
            uptr->hwmark = reclen;                  /* set buffer chars read in */
            sim_debug(DEBUG_DETAIL, &mta_dev, "mt_srv READ fill buffer complete count %x\n", reclen);
        }
        /* get a char from the buffer */
        ch = mt_buffer[bufnum][uptr->u4++];

        /* Send character over to channel */
        if (chan_write_byte(addr, &ch)) {
            sim_debug(DEBUG_CMD, &mta_dev, "Read unit %d EOR cnt %x\n", unit, uptr->u4);
            /* If not read whole record, skip till end */
            if ((uint32)uptr->u4 < uptr->hwmark) {
                /* Send dummy character to force SLI */
                chan_write_byte(addr, &ch);         /* write the byte */
                sim_debug(DEBUG_CMD, &mta_dev, "Read unit %d send dump SLI\n", unit);
                sim_activate(uptr, (uptr->hwmark-uptr->u4) * 10); /* wait again */
                uptr->u3 |= MT_READDONE;            /* read is done */
                break;
            }
            sim_debug(DEBUG_CMD, &mta_dev,
                "Read data @1 unit %d  cnt %x ch %02x hwm %x\n", unit, uptr->u4, ch, uptr->hwmark);
            uptr->u3 &= ~MT_CMDMSK;                 /* clear the cmd */
            mt_busy[bufnum] &= ~1;                  /* set not busy */
            chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* return end status */
        } else {
            sim_debug(DEBUG_DATA, &mta_dev,
                "Read data @2 unit %d  cnt %x ch %02x hwm %x\n", unit, uptr->u4, ch, uptr->hwmark);
            if ((uint32)uptr->u4 >= uptr->hwmark) { /* In IRG */
                /* Handle end of data record */
                sim_debug(DEBUG_CMD, &mta_dev, "Read data out of data unit %d cnt %x ch %02x hwm %x\n",
                        unit, uptr->u4, ch, uptr->hwmark);
#ifdef UTX_EOF_CHANGE
                uptr->u3 &= ~MT_CMDMSK;             /* clear the cmd */
                mt_busy[bufnum] &= ~1;              /* set not busy */
                chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* return end status */
#else
                uptr->u3 |= MT_READDONE;            /* read is done */
                sim_activate(uptr, 20);             /* wait again */
#endif
            } else
                sim_activate(uptr, 20);             /* wait again */
        }
        break;

    case MT_SETM:   /* 0x83 */                      /* set mode byte */
        sim_debug(DEBUG_CMD, &mta_dev, "mt_srv cmd 0x83 SETM unit=%d\n", unit);
        /* Grab data until channel has no more */
        if (chan_read_byte(addr, &ch)) {
            if (uptr->u4 > 0) {                     /* Only if data in record */
                reclen = uptr->hwmark;              /* set record length */
                ch = mt_buffer[bufnum][0];          /* get the first byte read */
                sim_debug(DEBUG_CMD, &mta_dev, "Write mode data done unit %d chars %d char %x\n", unit, reclen, ch);
                /* put mode bits into byte 2 of u5 */
                uptr->u5 = (uptr->u5 & 0xffff00ff) | (ch << 8);
                uptr->u4 = 0;                       /* no bytes anymore */
                uptr->u3 &= ~MT_CMDMSK;             /* no cmd to do */
                mt_busy[bufnum] &= ~1;              /* set not busy */
                chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* return end status */
            }
        } else {
            mt_buffer[bufnum][uptr->u4++] = ch;     /* save the character read in */
            sim_debug(DEBUG_CMD, &mta_dev, "Write mode data in unit %d u4 %d ch %0x\n", unit, uptr->u4, ch);
            uptr->hwmark = uptr->u4;                /* set high water mark */
            sim_activate(uptr, 20);                 /* wait time */
        }
        break;

    case MT_WRITE:  /* 0x01 */                      /* write record */
         /* Check if write protected */
         if (sim_tape_wrp(uptr)) {
             uptr->u5 |= SNS_CMDREJ;
             uptr->u3 &= ~MT_CMDMSK;
             mt_busy[bufnum] &= ~1;
             sim_debug(DEBUG_DETAIL, &mta_dev, "Write write protected unit=%d\n", unit);
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }

         /* Grab data until channel has no more */
         if (chan_read_byte(addr, &ch)) {
             if (uptr->u4 > 0) {                    /* Only if data in record */
                 reclen = uptr->hwmark;
                 sim_debug(DEBUG_DETAIL, &mta_dev, "Write unit=%d Block %d chars\n", unit, reclen);
                 r = sim_tape_wrrecf(uptr, &mt_buffer[bufnum][0], reclen);
                 uptr->u4 = 0;
                 uptr->u3 &= ~MT_CMDMSK;
                 mt_error(uptr, addr, r, dptr);     /* Record errors */
             }
         } else {
            mt_buffer[bufnum][uptr->u4++] = ch;
            sim_debug(DEBUG_DATA, &mta_dev, "Write data unit=%d %d %02x\n", unit, uptr->u4, ch);
            uptr->hwmark = uptr->u4;
         }
         sim_activate(uptr, 20);
         break;

    case MT_RDBK:   /* 0x0C */                      /* Read Backwards */
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
            sim_debug(DEBUG_DETAIL, &mta_dev, "Read backward unit=%d\n", unit);
            if ((r = sim_tape_rdrecr(uptr, &mt_buffer[bufnum][0], &reclen, BUFFSIZE)) != MTSE_OK) {
                uptr->u3 &= ~(MT_CMDMSK|MT_READDONE);
                return mt_error(uptr, addr, r, dptr);
            }
            uptr->u4 = reclen;
            uptr->hwmark = reclen;
            sim_debug(DEBUG_DETAIL, &mta_dev, "Binary Block %d chars\n", reclen);
         }

         ch = mt_buffer[bufnum][--uptr->u4];

         if (chan_write_byte(addr, &ch)) {
                sim_debug(DEBUG_DATA, &mta_dev, "Read unit=%d EOR cnt %x\n", unit, uptr->u4);
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
                sim_debug(DEBUG_DATA, &mta_dev, "Read data unit=%d %d %02o\n", unit, uptr->u4, ch);
              if (uptr->u4 == 0) {      /* In IRG */
                  uptr->u3 &= ~MT_CMDMSK;
                  mt_busy[bufnum] &= ~1;
                  chan_end(addr, SNS_CHNEND|SNS_DEVEND);
               } else
                  sim_activate(uptr, 20);
         }
         break;

    case MT_WTM:    /* 0x93 */                      /* Write tape filemark */
         if (uptr->u4 == 0) {
            if (sim_tape_wrp(uptr)) {
                uptr->u5 |= SNS_CMDREJ;
                uptr->u3 &= ~MT_CMDMSK;
                mt_busy[GET_DEV_BUF(dptr->flags)] &= ~1;
                chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
            }
            uptr->u4 ++;
            sim_activate(uptr, 500);
         } else {
            sim_debug(DEBUG_DETAIL, &mta_dev, "Write Mark unit=%d\n", unit);
            uptr->u3 &= ~(MT_CMDMSK);
            r = sim_tape_wrtmk(uptr);
            chan_end(addr, SNS_DEVEND);
            mt_busy[bufnum] &= ~1;
         }
         break;

    case MT_BSR:    /* 0x53 */                      /* Backspace record */
        sim_debug(DEBUG_DETAIL, &mta_dev, "mt_srv cmd 0x53 BSR unit %d u4 %x\n", unit, uptr->u4);
         switch (uptr->u4 ) {
         case 0:
              if (sim_tape_bot(uptr)) {
                  uptr->u3 &= ~MT_CMDMSK;
                  mt_busy[GET_DEV_BUF(dptr->flags)] &= ~1;
                  chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                  return SCPE_OK;
              }
              uptr->u4++;
              sim_activate(uptr, 50);
              break;
         case 1:
              uptr->u4++;
              sim_debug(DEBUG_DETAIL, &mta_dev, "Backspace rec unit %x u4 %x\n", unit, uptr->u4);
              r = sim_tape_sprecr(uptr, &reclen);
              /* We don't set EOF on BSR */
              if (r == MTSE_TMK) {
                  uptr->u4++;
                  sim_debug(DEBUG_DETAIL, &mta_dev, "MARK\n");
                  sim_activate(uptr, 50);
              } else {
                  sim_debug(DEBUG_DETAIL, &mta_dev, "Backspace reclen %x\n", reclen);
                  sim_activate(uptr, 50);
              }
              break;
         case 2:
              uptr->u3 &= ~(MT_CMDMSK);
              mt_busy[bufnum] &= ~1;
              chan_end(addr, SNS_CHNEND|SNS_DEVEND);
              break;
         case 3:
              uptr->u3 &= ~(MT_CMDMSK);
              mt_busy[bufnum] &= ~1;
              chan_end(addr, SNS_DEVEND|SNS_UNITEXP);
              break;
         }
         break;

    case MT_BSF:    /* 0x73 */          /* Backspace file */
         sim_debug(DEBUG_DETAIL, &mta_dev, "mt_srv cmd 0x73 BSF unit %d\n", unit);
         switch(uptr->u4) {
         case 0:
              if (sim_tape_bot(uptr)) {
                  uptr->u3 &= ~MT_CMDMSK;
                  mt_busy[bufnum] &= ~1;
                  chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                  break;
               }
               uptr->u4++;
               sim_activate(uptr, 500);
               break;
         case 1:
            sim_debug(DEBUG_DETAIL, &mta_dev, "Backspace file unit=%d\n", unit);
              r = sim_tape_sprecr(uptr, &reclen);
              if (r == MTSE_TMK) {
                  uptr->u4++;
                  sim_debug(DEBUG_DETAIL, &mta_dev, "MARK\n");
                  sim_activate(uptr, 50);
               } else if (r == MTSE_BOT) {
                  uptr->u4+= 2;
                  sim_activate(uptr, 50);
               } else {
                  sim_activate(uptr, 20);
               }
               break;
         case 2:        /* File Mark */
              uptr->u3 &= ~(MT_CMDMSK);
              mt_busy[bufnum] &= ~1;
              chan_end(addr, SNS_DEVEND);
              break;
         case 3:        /* BOT */
              uptr->u3 &= ~(MT_CMDMSK);
              mt_busy[bufnum] &= ~1;
              chan_end(addr, SNS_DEVEND);
              break;
         }
         break;

    case MT_FSR:    /* 0x43 */          /* Advance record */
         switch(uptr->u4) {
         case 0:
              sim_debug(DEBUG_DETAIL, &mta_dev, "Skip rec entry unit=%d ", unit);
              uptr->u4++;
              sim_activate(uptr, 50);
              break;
         case 1:
              uptr->u4++;
              sim_debug(DEBUG_DETAIL, &mta_dev, "Skip rec unit=%d ", unit);
              r = sim_tape_sprecf(uptr, &reclen);
              if (r == MTSE_TMK) {
                  uptr->u4 = 3;
                  sim_debug(DEBUG_DETAIL, &mta_dev, "FSR MARK\n");
                  sim_activate(uptr, 50);
              } else if (r == MTSE_EOM) {
                  uptr->u4 = 4;
                  sim_activate(uptr, 50);
              } else {
                  sim_debug(DEBUG_DETAIL, &mta_dev, "FSR skipped %d byte record\n", reclen);
                  sim_activate(uptr, 10 + (10 * reclen));
              }
              break;
         case 2:
              uptr->u3 &= ~(MT_CMDMSK);
              mt_busy[bufnum] &= ~1;
              chan_end(addr, SNS_DEVEND);
              break;
         case 3:
              uptr->u3 &= ~(MT_CMDMSK);
              mt_busy[bufnum] &= ~1;
              chan_end(addr, SNS_DEVEND|SNS_UNITEXP);
              sim_debug(DEBUG_DETAIL, &mta_dev, "Skip record Completed\n");
              break;
         case 4:
              uptr->u3 &= ~(MT_CMDMSK);
              mt_busy[bufnum] &= ~1;
              chan_end(addr, SNS_DEVEND|SNS_UNITCHK);
              break;
         }
         break;

    case MT_FSF:    /* 0x63 */          /* advance filemark */
        switch(uptr->u4) {
        case 0:
            sim_debug(DEBUG_DETAIL, &mta_dev, "Skip file entry unit=%d\n", unit);
            uptr->u4++;
            sim_activate(uptr, 50);
            break;
        case 1:
            sim_debug(DEBUG_DETAIL, &mta_dev, "Skip file unit=%d\n", unit);
            r = sim_tape_sprecf(uptr, &reclen);
            if (r == MTSE_TMK) {
                uptr->u4++;
                uptr->u5 |= SNS_FMRKDT;             /* file mark detected */
                sim_debug(DEBUG_DETAIL, &mta_dev, "FSF MARK\n");
                sim_activate(uptr, 50);
            } else if (r == MTSE_EOM) {
                uptr->u5 |= SNS_EOT;                /* set EOT status */
                uptr->u4+= 2;
                sim_activate(uptr, 50);
            } else {
                sim_debug(DEBUG_DETAIL, &mta_dev, "FSF skipped %d byte record\n", reclen);
                sim_activate(uptr, 50);
            }
            break;
        case 2:
            uptr->u3 &= ~(MT_CMDMSK);
            mt_busy[bufnum] &= ~1;
            chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* we are done dev|chan end */
            sim_debug(DEBUG_DETAIL, &mta_dev, "Skip file done unit=%d\n", unit);
            break;
        case 3:
            uptr->u3 &= ~(MT_CMDMSK);
            mt_busy[bufnum] &= ~1;
            chan_end(addr, SNS_DEVEND|SNS_UNITCHK|SNS_UNITCHK);
            break;
        }
        break;

    case MT_ERG:    /* 0xA3 */                      /* Erace 3.5 in tape */
         switch (uptr->u4) {
         case 0:
              if (sim_tape_wrp(uptr)) {
                  uptr->u5 |= SNS_CMDREJ;
                  uptr->u3 &= ~MT_CMDMSK;
                  mt_busy[bufnum] &= ~1;
                  chan_end(addr, SNS_DEVEND|SNS_UNITCHK);
              } else {
                  uptr->u4 ++;
                  sim_activate(uptr, 500);
              }
              break;
         case 1:
              sim_debug(DEBUG_DETAIL, &mta_dev, "Erase unit=%d\n", unit);
              r = sim_tape_wrgap(uptr, 35);
              sim_activate(uptr, 5000);
              uptr->u4++;
              break;
         case 2:
              uptr->u3 &= ~(MT_CMDMSK);
              mt_busy[bufnum] &= ~1;
              chan_end(addr, SNS_DEVEND);
         }
         break;

    case MT_REW:    /* 0x23 */                      /* rewind tape */
        if (uptr->u4 == 0) {
            uptr->u4++;
            sim_debug(DEBUG_DETAIL, &mta_dev, "Start rewind unit %d\n", unit);
            sim_activate(uptr, 1500);
        } else {
            sim_debug(DEBUG_DETAIL, &mta_dev, "Rewind complete unit %d\n", unit);
            uptr->u3 &= ~(MT_CMDMSK);
            r = sim_tape_rewind(uptr);
            uptr->u5 |= SNS_LOAD;                   /* set BOT */
            mt_busy[bufnum] &= ~1;
            chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* we are done dev|chan end */
         }
         break;

    case MT_RUN:    /* 0x33 */                      /* Rewind and unload tape */
         if (uptr->u4 == 0) {
             uptr->u4++;
             mt_busy[bufnum] &= ~1;
             sim_activate(uptr, 30000);
         } else {
             sim_debug(DEBUG_DETAIL, &mta_dev, "Unload unit=%d\n", unit);
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
    if (MT_DENS(uptr->dynflags) == 0)
        uptr->dynflags |= MT_DENS_6250 << UNIT_S_DF_TAPE;

    uptr->u3 &= ~0xffff;                            /* clear out the flags but leave ch/sa */
    uptr->u5 = 0;                                   /* clear sense data */
    uptr->u5 |= (SNS_RDY|SNS_ONLN|SNS_LOAD);        /* set initial status */
    mt_busy[GET_DEV_BUF(dptr->flags)] = 0;          /* set not busy */
    sim_debug(DEBUG_EXP, dptr, "MT init device %s unit %x\n", dptr->name, GET_UADDR(uptr->u3));
}

/* reset the mag tape */
t_stat mt_reset(DEVICE *dptr)
{
    /* nothing to do?? */
    sim_debug(DEBUG_EXP, &mta_dev, "MT reset name %s\n", dptr->name);
    return SCPE_OK;
}

/* attach the specified file to the tape device */
t_stat mt_attach(UNIT *uptr, CONST char *file)
{
    uint16              addr = GET_UADDR(uptr->u3);     /* get address of mt device */
    t_stat              r;

    if ((r = sim_tape_attach(uptr, file)) != SCPE_OK) { /* mount the specified file to the MT */
       sim_debug(DEBUG_EXP, &mta_dev, "mt_attach ERROR filename %s status %x\n", file, r);
       return r;                                        /* report any error */
    }
    sim_debug(DEBUG_EXP, &mta_dev, "mt_attach complete filename %s\n", file);
    set_devattn(addr, SNS_DEVEND);                      /* ready int???? */
    return SCPE_OK;                                     /* return good status */
}

/* detach the MT device and unload any tape */
t_stat mt_detach(UNIT *uptr)
{
    sim_debug(DEBUG_EXP, &mta_dev, "mt_detach\n");
    uptr->u3 = 0;
    return sim_tape_detach(uptr);
}

/* boot from the specified tape unit */
t_stat mt_boot(int32 unit_num, DEVICE *dptr)
{
    UNIT    *uptr = &dptr->units[unit_num];         /* find tape unit pointer */

    sim_debug(DEBUG_EXP, &mta_dev, "MT Boot dev/unit %x\n", GET_UADDR(uptr->u3));
    if ((uptr->flags & UNIT_ATT) == 0) {            /* Is MT device already attached? */
        sim_debug(DEBUG_EXP, &mta_dev, "MT Boot attach error dev/unit %x\n", GET_UADDR(uptr->u3));
        return SCPE_UNATT;                          /* not attached, return error */
    }
    SPAD[0xf4] = GET_UADDR(uptr->u3);               /* put boot device chan/sa into spad */
    SPAD[0xf8] = 0xF000;                            /* show as F class device */

//    if ((uptr->flags & MTUF_9TR) == 0) {          /* is tape a 9 track? */
    uptr->u3 &= ~0xffff;                            /* clear out old status */
//        uptr->u3 |= MT_ODD|MT_CONV|MT_MDEN_800;   /* set 800bpi & odd parity */
//    }
    return chan_boot(GET_UADDR(uptr->u3), dptr);    /* boot the ch/sa */
}

t_stat mt_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   char buffer[256];
   fprintf (st, "%s\n\n", mt_description(dptr));
   fprintf (st, "The mag tape drives support the BOOT command\n\n");
   (void)sim_tape_density_supported (buffer, sizeof(buffer), valid_dens);
   fprintf (st, " The density of the mag tape drive can be set with\n");
   fprintf (st, "    SET %s DENSITY=%s\n\n", dptr->name, buffer);
   sim_tape_attach_help (st, dptr, uptr, flag, cptr);
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *mt_description(DEVICE *dptr)
{
   return "2400 Magnetic tape unit";
}

#endif /* NUM_DEVS_MT */
