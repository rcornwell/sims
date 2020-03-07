/* sel32_mt.c: SEL-32 8051 Buffered Tape Processor

   Copyright (c) 2018-2020, James C. Bevier
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
   of junk.  File marks are represented by a byte count of 0. EOT is
   represented as 0xffffffff (-1) byte count.
*/

#include "sel32_defs.h"
#include "sim_tape.h"

extern  t_stat  set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern  t_stat  show_dev_addr(FILE *st, UNIT *uptr, int32 v, CONST void *desc);
extern  void    chan_end(uint16 chan, uint8 flags);
extern  int     chan_read_byte(uint16 chan, uint8 *data);
extern  int     chan_write_byte(uint16 chan, uint8 *data);
extern  void    set_devattn(uint16 addr, uint8 flags);
extern  t_stat  chan_boot(uint16 addr, DEVICE *dptr);
extern  DEVICE *get_dev(UNIT *uptr);
extern  t_stat  set_inch(UNIT *uptr, uint32 inch_addr); /* set channel inch address */
extern  CHANP  *find_chanp_ptr(uint16 chsa);             /* find chanp pointer */

extern  uint32  M[];                            /* our memory */
extern  uint32  SPAD[];                         /* cpu SPAD */

#ifdef NUM_DEVS_MT
#define BUFFSIZE        (64 * 1024)
#define UNIT_MT         UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE
#define DEV_BUF_NUM(x)  (((x) & 07) << DEV_V_UF)
#define GET_DEV_BUF(x)  (((x) >> DEV_V_UF) & 07)

#define CMD      u3
/* BTP tape commands */
#define MT_INCH             0x00        /* Initialize channel command */
#define MT_WRITE            0x01        /* Write command */
#define MT_READ             0x02        /* Read command */
#define MT_NOP              0x03        /* Control command */
#define MT_SENSE            0x04        /* Sense command */
#define MT_RDBK             0x0c        /* Read Backward */
#define MT_RDCMP            0x13        /* Read and compare command */
#define MT_REW              0x23        /* Rewind command */
#define MT_RUN              0x33        /* Rewind and unload */
#define MT_FSR              0x43        /* Advance record */
#define MT_BSR              0x53        /* Backspace record */
#define MT_FSF              0x63        /* Advance filemark  */
#define MT_BSF              0x73        /* Backspace filemark */
#define MT_SETM             0x83        /* Set Mode command */
#define MT_WTM              0x93        /* Write Tape filemark */
#define MT_ERG              0xA3        /* Erase 3.5 of tape */
#define MT_MODEMSK          0xFF        /* Mode Mask */

/* set mode bits for BTP (MT_SETM) */
#define MT_MODE_AUTO        0x80        /* =0 Perform auto error recovery on read */
#define MT_MODE_FORCE       0x80        /* =1 Read regardless if error recovery fails */
#define MT_MDEN_800         0x40        /* =0 select 800 BPI NRZI mode 9 track only */
#define MT_MDEN_1600        0x40        /* =1 select 1600 BPI PE mode 9 track only */
#define MT_MDEN_6250        0x02        /* =0 Use mode from bit one for NRZI/PE */
#define MT_MDEN_6250        0x02        /* =1 6250 BPI GCR mode 9 track only */
#define MT_MDEN_SCATGR      0x01        /* =1 HSTP scatter/gather mode */
#define MT_MDEN_MSK         0xc0        /* Density mask */

#define MT_CTL_MSK          0x38        /* Mask for control flags */
#define MT_CTL_NOP          0x00        /* Nop control mode */
#define MT_CTL_NRZI         0x08        /* 9 track 800 bpi mode */
#define MT_CTL_RST          0x10        /* Set density, odd, convert on, trans off */
#define MT_CTL_NOP2         0x18        /* 9 track 1600 NRZI mode */

/* in u3 is device command code and status */
#define MT_CMDMSK           0x00ff      /* Command being run */
#define MT_READDONE         0x0400      /* Read finished, end channel */
#define MT_MARK             0x0800      /* Sensed tape mark in move command */
#define MT_ODD              0x1000      /* Odd parity */
#define MT_TRANS            0x2000      /* Translation turned on ignored 9 track  */
#define MT_CONV             0x4000      /* Data converter on ignored 9 track  */
#define MT_BUSY             0x8000      /* Flag to send a CUE */

#define POS      u4
/* in u4 is current buffer position */

#define SNS      u5
/* in u5 packs sense byte 0, 1, 2 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ       0x80000000     /* Command reject */
#define SNS_INTVENT      0x40000000     /* Unit intervention required */
#define SNS_SPARE1       0x20000000     /* Spare */
#define SNS_EQUCHK       0x10000000     /* Equipment check */
#define SNS_DATCHK       0x08000000     /* Data Check */
#define SNS_OVRRUN       0x04000000     /* Data overrun */
#define SNS_SPARE2       0x02000000     /* Spare */
#define SNS_LOOKER       0x01000000     /* lookahead error */

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
#define SNS_MREG0        0x8000         /* Mode register bit 0 */
#define SNS_MREG1        0x4000         /* Mode register bit 1 */
#define SNS_MREG2        0x2000         /* Mode register bit 2 */
#define SNS_MREG3        0x1000         /* Mode register bit 3 */
#define SNS_MREG4        0x0800         /* Mode register bit 4 */
#define SNS_MREG5        0x0400         /* Mode register bit 5 */
#define SNS_MREG6        0x0200         /* Mode register bit 6 */
#define SNS_MREG7        0x0100         /* Mode register bit 7 */

/* Sense byte 3 */
#define SNS_RDY          0x80           /* Drive Ready */
#define SNS_ONLN         0x40           /* Drive Online */
#define SNS_WRP          0x20           /* Drive is file protected (write ring missing) */
#define SNS_NRZI         0x10           /* Drive is NRZI */
#define SNS_SPARE4       0x08           /* Spare */
#define SNS_LOAD         0x04           /* Drive is at load point */
#define SNS_EOT          0x02           /* Drive is at EOT */
#define SNS_SPARE5       0x01           /* Spare */

#define SNS_BYTE4        0x00           /* Hardware errors not supported */
#define SNS_BYTE5        0x00           /* Hardware errors not supported */

#define MT_CONV1         0x40
#define MT_CONV2         0x80
#define MT_CONV3         0xc0

/* u6 holds the packed characters and unpack counter */
#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark =  0xFFFFFFFF

uint8       mt_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) ;
t_stat      mt_srv(UNIT *uptr);
t_stat      mt_boot(int32 unitnum, DEVICE *dptr);
void        mt_ini(UNIT *uptr, t_bool);
t_stat      mt_reset(DEVICE *dptr);
t_stat      mt_attach(UNIT *uptr, CONST char *);
t_stat      mt_detach(UNIT *uptr);
t_stat      mt_help(FILE *, DEVICE *dptr, UNIT *uptr, int32, const char *);
const char  *mt_description(DEVICE *);

/* One buffer per channel */
uint8       mt_buffer[NUM_DEVS_MT][BUFFSIZE];
uint8       mt_busy[NUM_DEVS_MT];

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
    NULL,           /* Pre Start I/O */
    mt_startcmd,    /* Start a command */
    NULL,           /* Stop I/O */
    NULL,           /* Test I/O */
    NULL,           /* Post I/O */
    mt_ini,         /* init function */
    mta_unit,       /* Pointer to units structure */
    mta_chp,        /* Pointer to chan_prg structure */
    NUM_UNITS_MT,   /* number of units defined */
    0x07,           /* 8 devices - device mask */
    0x1000,         /* parent channel address */
    0,              /* fifo input index */
    0,              /* fifo output index */
    {0}             /* interrupt status fifo for channel */
};

DEVICE          mta_dev = {
    "MTA", mta_unit, NULL, mt_mod,
    NUM_UNITS_MT, 16, 24, 4, 16, 32,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    /* ctxt is the DIB pointer */
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
    NULL,             /* Pre Start I/O */
    mt_startcmd,      /* Start a command */
    NULL,             /* Stop I/O */
    NULL,             /* Test I/O */
    NULL,             /* Post I/O */
    mt_ini,           /* init function */
    mtb_unit,         /* Pointer to units structure */
    mtb_chp,          /* Pointer to chan_prg structure */
    NUM_UNITS_MT,     /* number of units defined */
    0x07,             /* 6 devices - device mask */
    0x1800,           /* parent channel address */
    0,                /* fifo input index */
    0,                /* fifo output index */
    0,                /* interrupt status fifo for channel */
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
    uint16      chsa = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int         unit = (uptr - dptr->units);
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    sim_debug(DEBUG_EXP, &mta_dev, "mt_startcmd entry chan %04x cmd %02x\n", chan, cmd);
    if (mt_busy[GET_DEV_BUF(dptr->flags)] != 0 || (uptr->CMD & MT_CMDMSK) != 0) {
        sim_debug(DEBUG_EXP, &mta_dev, "mt_startcmd busy chan %04x cmd %02x\n", chan, cmd);
        uptr->flags |= MT_BUSY;                     /* Flag we need to send CUE */
        return SNS_BSY;
    }

    sim_debug(DEBUG_EXP, &mta_dev, "mt_startcmd processing unit %01x cmd %02x\n", unit, cmd);

    switch (cmd & 0xFF) {
    case 0x00:                                      /* INCH command */
        sim_debug(DEBUG_CMD, dptr, "start INCH command\n");

        sim_debug(DEBUG_CMD, dptr,
            "mt_startcmd starting INCH cmd, chsa %04x MemBuf %08x cnt %04x\n",
            chsa, chp->ccw_addr, chp->ccw_count);

        /* UTX_needs_interrupt */
        cmd = MT_CMDMSK;                            /* insert INCH cmd as 0xff */
        /* fall through */
    case 0x03:                                      /* Tape motion commands or NOP */
    case 0x13:                                      /* Read and compare command */
    case 0x23:                                      /* Rewind command */
    case 0x33:                                      /* Rewind and unload */
    case 0x43:                                      /* Advance record */
    case 0x53:                                      /* Backspace record */
    case 0x63:                                      /* Advance filemark  */
    case 0x73:                                      /* Backspace filemark */
    case 0x83:                                      /* Set Mode command */
    case 0x93:                                      /* Write Tape filemark */
    case 0xA3:                                      /* Erase 3.5 of tape */
        /* UTX_needs_interrupt on NOP or INCH */
        /* fall through */
    case 0x01:                                      /* Write command */
    case 0x02:                                      /* Read command */
    case 0x0C:                                      /* Read backward */
        if (cmd != 0x03)                            /* if this is a nop do not zero status */
            uptr->SNS = (uptr->SNS & 0x0000ff00);   /* clear all but byte 2 */
        uptr->SNS |= (SNS_RDY|SNS_ONLN);            /* set ready status */
        /* Fall through */
        if (sim_tape_wrp(uptr))
            uptr->SNS |= (SNS_WRP);                 /* write protected */
        if (sim_tape_bot(uptr))
            uptr->SNS |= (SNS_LOAD);                /* tape at load point */
        if (sim_tape_eot(uptr))
            uptr->SNS |= (SNS_EOT);                 /* tape at EOM */
        /* Fall through */

    case 0x4:              /* Sense */
#ifndef FIX_DIAG
    case 0x80:                                      /* Unknown diag cmd with byte cnt of 0x0c */
#ifdef DO_DYNAMIC_DEBUG
        if ((cmd & 0xff) == 0x80) {
                /* start debugging */
                cpu_dev.dctrl |= (DEBUG_INST | DEBUG_CMD | DEBUG_EXP | DEBUG_IRQ);
        }
#endif
#endif
        uptr->CMD &= ~(MT_CMDMSK);                  /* clear out last cmd */
        uptr->CMD |= cmd & MT_CMDMSK;               /* insert new cmd */
        CLR_BUF(uptr);                              /* buffer is empty */
        uptr->POS = 0;                              /* reset buffer position pointer */
        mt_busy[GET_DEV_BUF(dptr->flags)] = 1;      /* show we are busy */
        sim_debug(DEBUG_EXP, &mta_dev, "mt_startcmd sense %08x return 0 chan %04x cmd %02x\n",
            uptr->SNS, chan, cmd);
        sim_activate(uptr, 100);                    /* Start unit off */
        return 0;

    default:                                        /* invalid command */
        sim_debug(DEBUG_EXP, &mta_dev, "mt_startcmd CMDREJ return chan %04x cmd %02x\n",
            chan, cmd);
        uptr->SNS |= SNS_CMDREJ;
        break;
    }
    if (uptr->SNS & 0xff000000)                     /* errors? */
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    sim_debug(DEBUG_EXP, &mta_dev,
        "mt_startcmd ret CHNEND|DEVEND chan %04x unit %04x cmd %02x\n", chan, unit, cmd);
    return SNS_CHNEND|SNS_DEVEND;
}

/* Map simH errors into machine errors */
t_stat mt_error(UNIT *uptr, uint16 addr, t_stat r, DEVICE *dptr)
{
    sim_debug(DEBUG_CMD, &mta_dev, "mt_error status %08x\n", r);
    mt_busy[GET_DEV_BUF(dptr->flags)] &= ~1;        /* not busy anymore */

    switch (r) {                                    /* switch on return value */
    case MTSE_OK:                                   /* no error */
/*NEW*/ chan_end(addr, SNS_CHNEND|SNS_DEVEND);      /* we are done with command */
        break;

    case MTSE_TMK:                                  /* tape mark */
        sim_debug(DEBUG_CMD, &mta_dev, "FILE MARK\n");
        uptr->SNS |= SNS_FMRKDT;                    /* file mark detected */
        chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
        break;

    case MTSE_WRP:                                  /* write protected */
        uptr->SNS |= SNS_WRP;                       /* write protected */
        sim_debug(DEBUG_CMD, &mta_dev, "WRITE PROTECT %08x\n", r); /* operator intervention */
/*NEW*/ chan_end(addr, SNS_CHNEND|SNS_DEVEND);      /* we are done with command */
        break;

    case MTSE_UNATT:                                /* unattached */
        uptr->SNS |= SNS_INTVENT;                   /* unit intervention required */
        sim_debug(DEBUG_CMD, &mta_dev, "ATTENTION %08x\n", r); /* operator intervention */
        chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
        break;

    case MTSE_IOERR:                                /* IO error */
    case MTSE_FMT:                                  /* invalid format */
    case MTSE_RECE:                                 /* error in record */
        sim_debug(DEBUG_CMD, &mta_dev, "ERROR %08x\n", r);
/*NEW*/ chan_end(addr, SNS_CHNEND|SNS_DEVEND);      /* we are done with command */
        break;

    case MTSE_BOT:                                  /* beginning of tape */
        uptr->SNS |= SNS_LOAD;                      /* tape at BOT */
        sim_debug(DEBUG_CMD, &mta_dev, "BOT\n");
/*NEW*/ chan_end(addr, SNS_CHNEND|SNS_DEVEND);      /* we are done with command */
        break;

    case MTSE_INVRL:                                /* invalid rec lnt */
    case MTSE_EOM:                                  /* end of medium */
        uptr->SNS |= SNS_EOT;                       /* tape at EOT */
        sim_debug(DEBUG_CMD, &mta_dev, "EOT\n");
        chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
        break;
    }
//WAS    chan_end(addr, SNS_CHNEND|SNS_DEVEND);          /* we are done with command */
    return SCPE_OK;
}

/* Handle processing of tape requests. */
t_stat mt_srv(UNIT *uptr)
{
    uint16      addr = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int         unit = (uptr - dptr->units);
    int         cmd = uptr->CMD & MT_CMDMSK;
    int         bufnum = GET_DEV_BUF(dptr->flags);
    CHANP       *chp = find_chanp_ptr(addr);    /* find the chanp pointer */
    t_mtrlnt    reclen;
    t_stat      r = SCPE_ARG;                   /* Force error if not set */
    int         i;
    uint32      mema;
    uint16      len;
    uint8       ch;
    uint8       buf[1024];

    sim_debug(DEBUG_DETAIL, &mta_dev, "mt_srv unit %04x cmd %02x\n", unit, cmd);
    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        mt_busy[bufnum] &= ~1;                  /* make our buffer not busy */
        if (cmd != MT_SENSE) {                  /* we are completed with unit check status */
            chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            return SCPE_OK;
        }
    }

    switch (cmd) {
    case MT_CMDMSK:   /* 0x0ff for inch 0x00 */ /* INCH is for channel, nothing for us */
        len = chp->ccw_count;                   /* INCH command count */
        mema = chp->ccw_addr;                   /* get inch or buffer addr */
        sim_debug(DEBUG_CMD, dptr,
            "mt_srv starting INCH %06x cmd, chsa %04x MemBuf %06x cnt %04x\n",
            mema, addr, chp->ccw_addr, chp->ccw_count);
#ifdef DO_DYNAMIC_DEBUG
        if (mema == 0x149d8) {
                /* start debugging */
                cpu_dev.dctrl |= (DEBUG_INST | DEBUG_CMD | DEBUG_EXP | DEBUG_IRQ);
        }
#endif

        if (len == 0) {
                /* we have invalid count, error, bail out */
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
                chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
        }
        for (i=0; i < len; i++) {
            if (chan_read_byte(addr, &buf[i])) {
                /* we have error, bail out */
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
                chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            /* just dump data */
        }
        /* a BTP uses a 41 word INCH memory buffer */
//      for (i=0; i<41; i++) {
        for (i=0; i<9; i++) {
            int32 data = RMW(mema+(4*i));       /* get data word */
            sim_debug(DEBUG_CMD, dptr,
                "mt_srv INCH buffer addr %06x, wd %02x data %08x\n",
                mema+(4*i), 4*i, data);
            /* zero the data */
            if (i == 8)
                WMW(mema+(4*i),0x00050005);     /* show we are a BTP */
            else
                WMW(mema+(4*i),0);              /* zero work location */
        }
        /* the chp->ccw_addr location contains the inch address */
        /* call set_inch() to setup inch buffer */
        i = set_inch(uptr, mema);               /* new address */

#ifndef NOTYET
        if ((i == SCPE_MEM) || (i == SCPE_ARG)) {   /* any error */
            /* we have error, bail out */
            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
            chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
#endif
        /* set halfwords 16 & 17 to 5 as default retry count in inch data */
        /* UTX uses this value to see if the device is a buffered tape processor */
        /* they must be non-zero and equal to be BTP */
        WMH(mema+(16*2),5);                     /* write left HW with count */
        WMH(mema+(17*2),5);                     /* write right HW with count */
        sim_debug(DEBUG_CMD, dptr,
            "mt_srv cmd INCH chsa %04x addr %06x count %04x completed\n",
            addr, mema, chp->ccw_count);
        uptr->CMD &= ~MT_CMDMSK;                /* clear the cmd */
        mt_busy[bufnum] &= ~1;                  /* make our buffer not busy */
        chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* we are done dev|chan end */
        break;

#ifndef FIX_DIAG
    case 0x80:      /* other? */                    /* default to NOP */
        sim_debug(DEBUG_CMD, &mta_dev, "mt_srv cmd 80 DIAG unit=%04x SNS %08x\n", unit, uptr->SNS);
        ch = (uptr->SNS >> 24) & 0xff;              /* get sense byte 0 status */
        sim_debug(DEBUG_CMD, &mta_dev, "sense unit %02x byte 0 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 0 */
        ch = (uptr->SNS >> 16) & 0xff;              /* get sense byte 1 status */
        sim_debug(DEBUG_CMD, &mta_dev, "sense unit %02x byte 1 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 1 */
        ch = (uptr->SNS >> 8) & 0xff;               /* get sense byte 2 status */
        sim_debug(DEBUG_CMD, &mta_dev, "sense unit %02x byte 2 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 2 */
        ch = (uptr->SNS >> 0) & 0xff;               /* get sense byte 3 status */
        sim_debug(DEBUG_CMD, &mta_dev, "sense unit %02x byte 3 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 3 */
#ifdef TRYTHIS
        /* write zero extra status */
        for (ch=4; ch < 0xc; ch++) {
            uint8   zc = 0;
            chan_write_byte(addr, &zc);             /* write zero byte */
        }
#else
        /* write status 2 more times */
        ch = (uptr->SNS >> 24) & 0xff;              /* get sense byte 0 status */
        sim_debug(DEBUG_CMD, &mta_dev, "sense unit %02x byte 0 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 0 */
        ch = (uptr->SNS >> 16) & 0xff;              /* get sense byte 1 status */
        sim_debug(DEBUG_CMD, &mta_dev, "sense unit %02x byte 1 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 1 */
        ch = (uptr->SNS >> 8) & 0xff;               /* get sense byte 2 status */
        sim_debug(DEBUG_CMD, &mta_dev, "sense unit %02x byte 2 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 2 */
        ch = (uptr->SNS >> 0) & 0xff;               /* get sense byte 3 status */
        sim_debug(DEBUG_CMD, &mta_dev, "sense unit %02x byte 3 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 3 */
        ch = (uptr->SNS >> 24) & 0xff;              /* get sense byte 0 status */
        sim_debug(DEBUG_CMD, &mta_dev, "sense unit %02x byte 0 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 0 */
        ch = (uptr->SNS >> 16) & 0xff;              /* get sense byte 1 status */
        sim_debug(DEBUG_CMD, &mta_dev, "sense unit %02x byte 1 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 1 */
        ch = (uptr->SNS >> 8) & 0xff;               /* get sense byte 2 status */
        sim_debug(DEBUG_CMD, &mta_dev, "sense unit %02x byte 2 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 2 */
        ch = (uptr->SNS >> 0) & 0xff;               /* get sense byte 3 status */
        sim_debug(DEBUG_CMD, &mta_dev, "sense unit %02x byte 3 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 3 */
#endif
        uptr->CMD &= ~MT_CMDMSK;                    /* clear the cmd */
        mt_busy[bufnum] &= ~1;                      /* make our buffer not busy */
        sim_debug(DEBUG_CMD, &mta_dev, "mt_srv DIAG SNS %08x char complete unit=%02x\n",
            uptr->SNS, unit);
        chan_end(addr, SNS_CHNEND|SNS_DEVEND);      /* we are done dev|chan end */
        break;

#endif
    case MT_NOP:    /* 0x03 */                      /* NOP motion command */
        uptr->CMD &= ~MT_CMDMSK;                    /* clear the cmd */
        mt_busy[bufnum] &= ~1;                      /* make our buffer not busy */
        chan_end(addr, SNS_CHNEND|SNS_DEVEND);      /* we are done dev|chan end */
        break;

    case MT_SENSE:  /* 0x04 */                      /* get sense data */
        sim_debug(DEBUG_CMD, &mta_dev, "mt_srv cmd 4 SENSE %08x unit=%04x\n", uptr->SNS, unit);
        ch = (uptr->SNS >> 24) & 0xff;              /* get sense byte 0 status */
        sim_debug(DEBUG_DETAIL, &mta_dev, "sense unit %02x byte 0 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 0 */
        ch = (uptr->SNS >> 16) & 0xff;              /* get sense byte 1 status */
        sim_debug(DEBUG_DETAIL, &mta_dev, "sense unit %02x byte 1 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 1 */
        ch = (uptr->SNS >> 8) & 0xff;               /* get sense byte 2 status */
        sim_debug(DEBUG_DETAIL, &mta_dev, "sense unit %02x byte 2 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 2 */
        ch = (uptr->SNS >> 0) & 0xff;               /* get sense byte 3 status */
        sim_debug(DEBUG_DETAIL, &mta_dev, "sense unit %02x byte 3 %02x\n", unit, ch);
        chan_write_byte(addr, &ch);                 /* write byte 3 */
        ch = 4;
        uptr->CMD &= ~MT_CMDMSK;                    /* clear the cmd */
        mt_busy[bufnum] &= ~1;                      /* make our buffer not busy */
        sim_debug(DEBUG_CMD, &mta_dev, "mt_srv SENSE %08x char complete unit=%02x\n",
            uptr->SNS, unit);
        chan_end(addr, SNS_CHNEND|SNS_DEVEND);      /* we are done dev|chan end */
        break;

    case MT_READ:   /* 0x02 */                      /* read a record from the device */
        sim_debug(DEBUG_DATA, &mta_dev, "mt_srv cmd 2 READ unit=%02x\n", unit);
        if (uptr->CMD & MT_READDONE) {              /* is the read complete */
            uptr->SNS &= ~(SNS_LOAD|SNS_EOT);       /* reset BOT & EOT */
            if (sim_tape_eot(uptr)) {               /* see if at EOM */
                uptr->SNS |= SNS_EOT;               /* set EOT status */
            }
            uptr->CMD &= ~(MT_CMDMSK|MT_READDONE);  /* clear all but readdone & cmd */
            uptr->CMD &= ~MT_CMDMSK;                /* clear the cmd */
            mt_busy[bufnum] &= ~1;                  /* not busy anymore */
            sim_debug(DEBUG_CMD, &mta_dev,
                "mt_srv READ %04x char complete unit=%02x sense %08x\n",
                uptr->POS, unit, uptr->SNS);
            chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* set chan end, dev end status */
            break;
        }
        /* read is not completed, get an input char */
        /* If empty buffer, fill */
        if (BUF_EMPTY(uptr)) {
            /* buffer is empty, so fill it with next record data */
            if ((r = sim_tape_rdrecf(uptr, &mt_buffer[bufnum][0], &reclen, BUFFSIZE)) != MTSE_OK) {
                sim_debug(DEBUG_CMD, &mta_dev, "mt_srv READ fill buffer unit=%02x\n", unit);
                uptr->CMD &= ~(MT_CMDMSK|MT_READDONE);   /* clear all but readdone & cmd */
                return mt_error(uptr, addr, r, dptr);   /* process any error & return status */
            }
            uptr->SNS &= ~(SNS_LOAD|SNS_EOT);       /* reset BOT & EOT */
            uptr->POS = 0;                          /* reset buffer position */
            uptr->hwmark = reclen;                  /* set buffer chars read in */
            sim_debug(DEBUG_DETAIL, &mta_dev, "mt_srv READ fill buffer complete count %04x\n", reclen);
        }
        /* get a char from the buffer */
        ch = mt_buffer[bufnum][uptr->POS++];

        /* Send character over to channel */
        if (chan_write_byte(addr, &ch)) {
            sim_debug(DEBUG_CMD, &mta_dev, "Read unit %02x EOR cnt %04x\n", unit, uptr->POS);
            /* If not read whole record, skip till end */
            if ((uint32)uptr->POS < uptr->hwmark) {
                /* Send dummy character to force SLI */
                chan_write_byte(addr, &ch);         /* write the byte */
                sim_debug(DEBUG_CMD, &mta_dev, "Read unit %02x send dump SLI\n", unit);
                sim_activate(uptr, (uptr->hwmark-uptr->POS) * 10); /* wait again */
                uptr->CMD |= MT_READDONE;           /* read is done */
                break;
            }
            sim_debug(DEBUG_CMD, &mta_dev,
                "Read data @1 unit %02x cnt %04x ch %02x hwm %04x\n",
                    unit, uptr->POS, ch, uptr->hwmark);
            uptr->CMD &= ~MT_CMDMSK;                /* clear the cmd */
            mt_busy[bufnum] &= ~1;                  /* set not busy */
            chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* return end status */
        } else {
            sim_debug(DEBUG_DATA, &mta_dev,
                "Read data @2 unit %02x cnt %04x ch %02x hwm %04x\n", unit, uptr->POS, ch,
                          uptr->hwmark);
            if ((uint32)uptr->POS >= uptr->hwmark) { /* In IRG */
                /* Handle end of data record */
                sim_debug(DEBUG_CMD, &mta_dev,
                        "Read data out of data unit %02x cnt %04x ch %02x hwm %04x\n",
                        unit, uptr->POS, ch, uptr->hwmark);
#ifdef UTX_EOF_CHANGE
                uptr->CMD &= ~MT_CMDMSK;            /* clear the cmd */
                mt_busy[bufnum] &= ~1;              /* set not busy */
                chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* return end status */
#else
                uptr->CMD |= MT_READDONE;           /* read is done */
                sim_activate(uptr, 20);             /* wait again */
#endif
            } else
                sim_activate(uptr, 20);             /* wait again */
        }
        break;

    case MT_SETM:   /* 0x83 */                      /* set mode byte */
        sim_debug(DEBUG_CMD, &mta_dev, "mt_srv cmd 0x83 SETM unit=%02x\n", unit);
        /* Grab data until channel has no more */
        if (chan_read_byte(addr, &ch)) {
            if (uptr->POS > 0) {                    /* Only if data in record */
                reclen = uptr->hwmark;              /* set record length */
                ch = mt_buffer[bufnum][0];          /* get the first byte read */
                sim_debug(DEBUG_CMD, &mta_dev,
                    "Write mode data done unit %02x chars %04x char %02x\n", unit, reclen, ch);
                /* put mode bits into byte 2 of SNS */
                uptr->SNS = (uptr->SNS & 0xffff00ff) | (ch << 8);
                uptr->POS = 0;                      /* no bytes anymore */
                uptr->CMD &= ~MT_CMDMSK;            /* no cmd to do */
                mt_busy[bufnum] &= ~1;              /* set not busy */
                chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* return end status */
            }
        } else {
            mt_buffer[bufnum][uptr->POS++] = ch;    /* save the character read in */
            sim_debug(DEBUG_CMD, &mta_dev, "Write mode data in unit %02x POS %04x ch %02x\n",
                  unit, uptr->POS, ch);
            uptr->hwmark = uptr->POS;               /* set high water mark */
            sim_activate(uptr, 20);                 /* wait time */
        }
        break;

    case MT_WRITE:  /* 0x01 */                      /* write record */
        /* Check if write protected */
        if (sim_tape_wrp(uptr)) {
            uptr->SNS |= SNS_CMDREJ;
            uptr->CMD &= ~MT_CMDMSK;
            mt_busy[bufnum] &= ~1;
            sim_debug(DEBUG_CMD, &mta_dev, "Write write protected unit=%02x\n", unit);
            chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }

        /* Grab data until channel has no more */
        if (chan_read_byte(addr, &ch)) {
            if (uptr->POS > 0) {                   /* Only if data in record */
                reclen = uptr->hwmark;
                sim_debug(DEBUG_CMD, &mta_dev, "Write unit=%02x Block %04x chars\n",
                    unit, reclen);
                r = sim_tape_wrrecf(uptr, &mt_buffer[bufnum][0], reclen);
                uptr->POS = 0;
                uptr->CMD &= ~MT_CMDMSK;
                mt_error(uptr, addr, r, dptr);     /* Record errors */
            }
        } else {
            mt_buffer[bufnum][uptr->POS++] = ch;
            sim_debug(DEBUG_DATA, &mta_dev, "Write data unit=%02x %04x %02x\n",
                unit, uptr->POS, ch);
            uptr->hwmark = uptr->POS;
        }
        sim_activate(uptr, 20);
        break;

    case MT_RDBK:   /* 0x0C */                      /* Read Backwards */
        if (uptr->CMD & MT_READDONE) {
            uptr->CMD &= ~(MT_CMDMSK|MT_READDONE);
            mt_busy[bufnum] &= ~1;
            chan_end(addr, SNS_CHNEND|SNS_DEVEND);
            return SCPE_OK;
        }

        /* If at end of record, fill buffer */
        if (BUF_EMPTY(uptr)) {
            if (sim_tape_bot(uptr)) {
                uptr->CMD &= ~MT_CMDMSK;
                mt_busy[GET_DEV_BUF(dptr->flags)] &= ~1;
                chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
            }
            sim_debug(DEBUG_CMD, &mta_dev, "Read backward unit=%02x\n", unit);
            if ((r = sim_tape_rdrecr(uptr, &mt_buffer[bufnum][0], &reclen, BUFFSIZE)) != MTSE_OK) {
                uptr->CMD &= ~(MT_CMDMSK|MT_READDONE);
                return mt_error(uptr, addr, r, dptr);
            }
            uptr->POS = reclen;
            uptr->hwmark = reclen;
            sim_debug(DEBUG_CMD, &mta_dev, "Binary Block %04x chars\n", reclen);
        }

        ch = mt_buffer[bufnum][--uptr->POS];

        if (chan_write_byte(addr, &ch)) {
            sim_debug(DEBUG_CMD, &mta_dev, "Read unit=%02x EOR cnt %04x\n",
                unit, uptr->POS);
             /* If not read whole record, skip till end */
             if (uptr->POS >= 0) {
                 sim_activate(uptr, (uptr->POS) * 20);
                 uptr->CMD |= MT_READDONE;
                 return SCPE_OK;
             }
             uptr->CMD &= ~MT_CMDMSK;
             mt_busy[bufnum] &= ~1;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND);
        } else {
            sim_debug(DEBUG_DATA, &mta_dev, "Read data unit=%02x %04x %02x\n",
                unit, uptr->POS, ch);
            if (uptr->POS == 0) {      /* In IRG */
                uptr->CMD &= ~MT_CMDMSK;
                mt_busy[bufnum] &= ~1;
                chan_end(addr, SNS_CHNEND|SNS_DEVEND);
            } else
                sim_activate(uptr, 20);
        }
        break;

    case MT_WTM:    /* 0x93 */                      /* Write tape filemark */
        if (uptr->POS == 0) {
            if (sim_tape_wrp(uptr)) {
                uptr->SNS |= SNS_CMDREJ;
                uptr->CMD &= ~MT_CMDMSK;
                mt_busy[GET_DEV_BUF(dptr->flags)] &= ~1;
                chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
            }
            uptr->POS ++;
            sim_activate(uptr, 500);
        } else {
            sim_debug(DEBUG_CMD, &mta_dev, "Write Mark unit=%02x\n", unit);
            uptr->CMD &= ~(MT_CMDMSK);
            r = sim_tape_wrtmk(uptr);
            chan_end(addr, SNS_DEVEND); //NEW         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
            mt_busy[bufnum] &= ~1;
        }
        break;

    case MT_BSR:    /* 0x53 */                      /* Backspace record */
        sim_debug(DEBUG_CMD, &mta_dev, "mt_srv cmd 0x53 BSR unit %02x POS %04x\n",
            unit, uptr->POS);
        switch (uptr->POS ) {
        case 0:
            if (sim_tape_bot(uptr)) {
                uptr->CMD &= ~MT_CMDMSK;
                 mt_busy[GET_DEV_BUF(dptr->flags)] &= ~1;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 return SCPE_OK;
            }
            uptr->POS++;
            sim_activate(uptr, 50);
            break;
        case 1:
            uptr->POS++;
            sim_debug(DEBUG_CMD, &mta_dev, "Backspace rec unit %02x POS %04x\n",
                unit, uptr->POS);
            r = sim_tape_sprecr(uptr, &reclen);
            /* We don't set EOF on BSR */
            if (r == MTSE_TMK) {
                uptr->POS++;
                sim_debug(DEBUG_CMD, &mta_dev, "MARK\n");
                sim_activate(uptr, 50);
            } else {
                sim_debug(DEBUG_CMD, &mta_dev, "Backspace reclen %04x\n", reclen);
                sim_activate(uptr, 50);
            }
            break;
        case 2:
            uptr->CMD &= ~(MT_CMDMSK);
            mt_busy[bufnum] &= ~1;
            chan_end(addr, SNS_CHNEND|SNS_DEVEND);
            break;
        case 3:
            uptr->CMD &= ~(MT_CMDMSK);
            mt_busy[bufnum] &= ~1;
            chan_end(addr, SNS_DEVEND|SNS_UNITEXP);
            break;
        }
        break;

    case MT_BSF:    /* 0x73 */          /* Backspace file */
        sim_debug(DEBUG_CMD, &mta_dev, "mt_srv cmd 0x73 BSF unit %02x\n", unit);
        switch(uptr->POS) {
        case 0:
            if (sim_tape_bot(uptr)) {
                uptr->CMD &= ~MT_CMDMSK;
                 mt_busy[bufnum] &= ~1;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
            }
            uptr->POS++;
            sim_activate(uptr, 500);
            break;
        case 1:
            sim_debug(DEBUG_CMD, &mta_dev, "Backspace file unit=%02x\n", unit);
            r = sim_tape_sprecr(uptr, &reclen);
            if (r == MTSE_TMK) {
                uptr->POS++;
                sim_debug(DEBUG_DETAIL, &mta_dev, "MARK\n");
                sim_activate(uptr, 50);
            } else if (r == MTSE_BOT) {
                uptr->POS+= 2;
                sim_activate(uptr, 50);
            } else {
                sim_activate(uptr, 20);
            }
            break;
        case 2:        /* File Mark */
            uptr->CMD &= ~(MT_CMDMSK);
            mt_busy[bufnum] &= ~1;
            chan_end(addr, SNS_DEVEND); //NEW         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
            break;
        case 3:        /* BOT */
            uptr->CMD &= ~(MT_CMDMSK);
            mt_busy[bufnum] &= ~1;
            chan_end(addr, SNS_DEVEND); //NEW         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
            break;
        }
        break;

    case MT_FSR:    /* 0x43 */          /* Advance record */
        switch(uptr->POS) {
        case 0:
            sim_debug(DEBUG_CMD, &mta_dev, "Skip rec entry unit=%02x ", unit);
            uptr->POS++;
            sim_activate(uptr, 50);
            break;
        case 1:
            uptr->POS++;
            sim_debug(DEBUG_CMD, &mta_dev, "Skip rec unit=%02x ", unit);
            r = sim_tape_sprecf(uptr, &reclen);
            if (r == MTSE_TMK) {
                uptr->POS = 3;
                uptr->SNS |= SNS_FMRKDT;            /* file mark detected */
                sim_debug(DEBUG_CMD, &mta_dev, "FSR MARK\n");
                sim_activate(uptr, 50);
            } else if (r == MTSE_EOM) {
                uptr->POS = 4;
                uptr->SNS |= SNS_EOT;               /* set EOT status */
                sim_activate(uptr, 50);
            } else {
                sim_debug(DEBUG_CMD, &mta_dev, "FSR skipped %04x byte record\n",
                    reclen);
                sim_activate(uptr, 10 + (10 * reclen));
            }
            break;
        case 2:
            uptr->CMD &= ~(MT_CMDMSK);
            mt_busy[bufnum] &= ~1;
            sim_debug(DEBUG_CMD, &mta_dev, "Skip record Completed\n");
            chan_end(addr, SNS_DEVEND); //NEW         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
            break;
        case 3:
            uptr->CMD &= ~(MT_CMDMSK);
            mt_busy[bufnum] &= ~1;
            sim_debug(DEBUG_CMD, &mta_dev, "Skip record at EOF\n");
            chan_end(addr, SNS_DEVEND|SNS_UNITEXP); //NEW chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
            break;
        case 4:
            uptr->CMD &= ~(MT_CMDMSK);
            mt_busy[bufnum] &= ~1;
            sim_debug(DEBUG_CMD, &mta_dev, "Skip record at EOT\n");
//BAD       chan_end(addr, SNS_DEVEND|SNS_UNITCHK);
            chan_end(addr, SNS_DEVEND|SNS_UNITEXP);
            break;
        }
        break;

    case MT_FSF:    /* 0x63 */          /* advance filemark */
        switch(uptr->POS) {
        case 0:
            sim_debug(DEBUG_CMD, &mta_dev, "Skip file entry sense %08x unit %02x\n", uptr->SNS, unit);
            uptr->POS++;
            sim_activate(uptr, 50);
            break;
        case 1:
            sim_debug(DEBUG_CMD, &mta_dev, "Skip file unit=%02x\n", unit);
            r = sim_tape_sprecf(uptr, &reclen);
            if (r == MTSE_TMK) {
                uptr->POS++;
                uptr->SNS |= SNS_FMRKDT;            /* file mark detected */
                sim_debug(DEBUG_CMD, &mta_dev, "FSF EOF MARK sense %08x\n", uptr->SNS);
                sim_activate(uptr, 50);
            } else if (r == MTSE_EOM) {
                uptr->SNS |= SNS_EOT;               /* set EOT status */
                sim_debug(DEBUG_CMD, &mta_dev, "FSF EOT sense %08x\n", uptr->SNS);
                uptr->POS+= 2;
                sim_activate(uptr, 50);
            } else {
                sim_debug(DEBUG_CMD, &mta_dev, "FSF skipped %04x byte record\n", reclen);
                sim_activate(uptr, 50);
            }
            break;
        case 2:
            uptr->CMD &= ~(MT_CMDMSK);
            mt_busy[bufnum] &= ~1;
            uptr->SNS &= ~SNS_LOAD;                 /* reset BOT */
            sim_debug(DEBUG_CMD, &mta_dev, "Skip file done sense %08x unit %02x\n", uptr->SNS, unit);
            chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* we are done dev|chan end */
            break;
        case 3:
            uptr->CMD &= ~(MT_CMDMSK);
            uptr->SNS &= ~SNS_LOAD;                 /* reset BOT */
            mt_busy[bufnum] &= ~1;
            sim_debug(DEBUG_CMD, &mta_dev, "Skip file got EOT sense %08x unit %02x\n", uptr->SNS, unit);
//BAD       chan_end(addr, SNS_DEVEND|SNS_UNITCHK);
            chan_end(addr, SNS_DEVEND|SNS_UNITEXP);
            break;
        }
        break;

    case MT_ERG:    /* 0xA3 */                      /* Erace 3.5 in tape */
        switch (uptr->POS) {
        case 0:
            if (sim_tape_wrp(uptr)) {
                uptr->SNS |= SNS_CMDREJ;
                uptr->CMD &= ~MT_CMDMSK;
                mt_busy[bufnum] &= ~1;
//BAD           chan_end(addr, SNS_DEVEND|SNS_UNITCHK);
                chan_end(addr, SNS_DEVEND|SNS_UNITEXP);
            } else {
                uptr->POS ++;
                sim_activate(uptr, 500);
            }
            break;
        case 1:
            sim_debug(DEBUG_CMD, &mta_dev, "Erase unit=%02x\n", unit);
            r = sim_tape_wrgap(uptr, 35);
            sim_activate(uptr, 5000);
            uptr->POS++;
            break;
        case 2:
            uptr->CMD &= ~(MT_CMDMSK);
            mt_busy[bufnum] &= ~1;
            /* we are done dev|chan end */
            chan_end(addr, SNS_DEVEND);
            break;
        }
        break;

    case MT_REW:    /* 0x23 */                      /* rewind tape */
        if (uptr->POS == 0) {
            uptr->POS++;
            sim_debug(DEBUG_CMD, &mta_dev, "Start rewind unit %02x\n", unit);
            sim_activate(uptr, 1500);
        } else {
            sim_debug(DEBUG_CMD, &mta_dev, "Rewind complete unit %02x\n", unit);
            uptr->CMD &= ~(MT_CMDMSK);
            r = sim_tape_rewind(uptr);
            uptr->SNS |= SNS_LOAD;                  /* set BOT */
            mt_busy[bufnum] &= ~1;
            chan_end(addr, SNS_CHNEND|SNS_DEVEND);  /* we are done dev|chan end */
         }
         break;

    case MT_RUN:    /* 0x33 */                      /* Rewind and unload tape */
         if (uptr->POS == 0) {
             uptr->POS++;
             mt_busy[bufnum] &= ~1;
             sim_activate(uptr, 30000);
         } else {
             sim_debug(DEBUG_CMD, &mta_dev, "Unload unit=%02x\n", unit);
             uptr->CMD &= ~(MT_CMDMSK);
             r = sim_tape_detach(uptr);
         }
         break;
    }
    return SCPE_OK;
}

/* initialize the tape chan/unit */
void mt_ini(UNIT *uptr, t_bool f)
{
    DEVICE *dptr = get_dev(uptr);
    if (MT_DENS(uptr->dynflags) == 0)
        uptr->dynflags |= MT_DENS_6250 << UNIT_S_DF_TAPE;

    uptr->CMD &= ~0xffff;                           /* clear out the flags but leave ch/sa */
    uptr->SNS = 0;                                  /* clear sense data */
    uptr->SNS |= (SNS_RDY|SNS_ONLN|SNS_LOAD);       /* set initial status */
    mt_busy[GET_DEV_BUF(dptr->flags)] = 0;          /* set not busy */
    sim_debug(DEBUG_EXP, dptr, "MT init device %s unit %02x\n",
        dptr->name, GET_UADDR(uptr->CMD));
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
    uint16         addr = GET_UADDR(uptr->CMD);     /* get address of mt device */
    t_stat         r;

    /* mount the specified file to the MT */
    if ((r = sim_tape_attach(uptr, file)) != SCPE_OK) {
       sim_debug(DEBUG_EXP, &mta_dev, "mt_attach ERROR filename %s status %08x\n", file, r);
       return r;                                    /* report any error */
    }
    sim_debug(DEBUG_EXP, &mta_dev, "mt_attach complete filename %s\n", file);
    uptr->CMD &= ~0xffff;                           /* clear out the flags but leave ch/sa */
    uptr->POS = 0;                                  /* clear position data */
    uptr->SNS = 0;                                  /* clear sense data */
    set_devattn(addr, SNS_DEVEND);                  /* ready int???? */
    return SCPE_OK;                                 /* return good status */
}

/* detach the MT device and unload any tape */
t_stat mt_detach(UNIT *uptr)
{
    sim_debug(DEBUG_EXP, &mta_dev, "mt_detach\n");
    uptr->CMD &= ~0xffff;                           /* clear out the flags but leave ch/sa */
    uptr->POS = 0;                                  /* clear position data */
    uptr->SNS = 0;                                  /* clear sense data */
    return sim_tape_detach(uptr);
}

/* boot from the specified tape unit */
t_stat mt_boot(int32 unit_num, DEVICE *dptr)
{
    UNIT    *uptr = &dptr->units[unit_num];         /* find tape unit pointer */

    sim_debug(DEBUG_EXP, &mta_dev, "MT Boot dev/unit %04x\n", GET_UADDR(uptr->CMD));
    if ((uptr->flags & UNIT_ATT) == 0) {            /* Is MT device already attached? */
        sim_debug(DEBUG_EXP, &mta_dev,
            "MT Boot attach error dev/unit %04x\n", GET_UADDR(uptr->CMD));
        return SCPE_UNATT;                          /* not attached, return error */
    }
    SPAD[0xf4] = GET_UADDR(uptr->CMD);              /* put boot device chan/sa into spad */
    SPAD[0xf8] = 0xF000;                            /* show as F class device */

    uptr->CMD &= ~0xffff;                           /* clear out old status */
    return chan_boot(GET_UADDR(uptr->CMD), dptr);   /* boot the ch/sa */
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
   return "8051 Buffered Tape Processor";
}

#endif /* NUM_DEVS_MT */
