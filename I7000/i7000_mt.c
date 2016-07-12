/* i7090_mt.c: IBM 7090 Magnetic tape controller

   Copyright (c) 2005-2016, Richard Cornwell

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

#include "i7000_defs.h"
#include "sim_tape.h"

#ifndef NUM_DEVS_MT
#define NUM_DEVS_MT 0
#endif

#if (NUM_DEVS_MT > 0) || defined(MT_CHANNEL_ZERO)

#define BUFFSIZE        (MAXMEMSIZE * CHARSPERWORD)
#define UNIT_MT(x)      UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE | \
                        UNIT_S_CHAN(x)
#define MTUF_LDN        (1 << MTUF_V_UF)
#define MTUF_ONLINE     (1 << UNIT_V_UF_31)
#define LT              66      /* Time per char low density */
#define HT              16      /* Time per char high density */

/* in u3 is device address */
/* in u4 is current buffer position */
/* in u5 */
#define MT_RDS          1
#define MT_RDSB         2
#define MT_WRS          3
#define MT_WRSB         4
#define MT_WEF          5
#define MT_BSR          6
#define MT_BSF          7
#define MT_REW          8
#define MT_SDN          9
#define MT_RUN          10
#define MT_SKIP         11      /* Do skip to end of record */
#define MT_WRITE        12      /* Actual transfer operation */
#define MT_SKR          13      
#define MT_ERG          14      
#define MT_RDB          15      
#define MT_CMDMSK   000017      /* Command being run */
#define MT_RDY      000020      /* Device is ready for command */
#define MT_IDLE     000040      /* Tape still in motion */
#define MT_MARK     000100      /* Hit tape mark */
#define MT_EOT      000200      /* At End Of Tape */
#define MT_RM       000400      /* Hit a record mark character */
#define MT_EOR      001000      /* Set EOR on next record */

#define MTC_SEL     0020        /* Controller executing read/write */
#define MTC_BSY     0040        /* Controller is busy - executing cmd */
#define MTC_UNIT    0017        /* device Channel is on */

uint32              mt_cmd(UNIT *, uint16, uint16);
t_stat              mt_srv(UNIT *);
t_stat              mt_boot(int32, DEVICE *);
void                mt_ini(UNIT *, t_bool);
t_stat              mt_reset(DEVICE *);
t_stat              mt_attach(UNIT *, CONST char *);
t_stat              mt_detach(UNIT *);
t_stat              mt_rew(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat              mt_tape_density(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat              mt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *mt_description (DEVICE *dptr);
extern t_stat       chan_boot(int32, DEVICE *);
#ifdef I7010
extern uint8        chan_io_status[NUM_CHAN];   /* Channel status */
#endif

#ifdef MT_CHANNEL_ZERO
#define NUM_DEVS (NUM_DEVS_MT + 1)
#else
#define NUM_DEVS (NUM_DEVS_MT)
#endif

/* Channel level activity */
uint8               mt_chan[NUM_DEVS];

/* One buffer per channel */
uint8               mt_buffer[NUM_DEVS][BUFFSIZE];

UNIT                mta_unit[] = {
/* Controller 1 */
#if (NUM_DEVS_MT > 0)
    {UDATA(&mt_srv, UNIT_MT(1), 0), 0}, /* 0 */
    {UDATA(&mt_srv, UNIT_MT(1), 0), 0}, /* 1 */
    {UDATA(&mt_srv, UNIT_MT(1), 0), 0}, /* 2 */
    {UDATA(&mt_srv, UNIT_MT(1), 0), 0}, /* 3 */
    {UDATA(&mt_srv, UNIT_MT(1), 0), 0}, /* 4 */
    {UDATA(&mt_srv, UNIT_MT(1), 0), 0}, /* 5 */
    {UDATA(&mt_srv, UNIT_MT(1), 0), 0}, /* 6 */
    {UDATA(&mt_srv, UNIT_MT(1), 0), 0}, /* 7 */
    {UDATA(&mt_srv, UNIT_MT(1), 0), 0}, /* 8 */
    {UDATA(&mt_srv, UNIT_MT(1), 0), 0}, /* 9 */
#if (NUM_DEVS_MT > 1)
/* Controller 2 */
    {UDATA(&mt_srv, UNIT_MT(2), 0), 0}, /* 0 */
    {UDATA(&mt_srv, UNIT_MT(2), 0), 0}, /* 1 */
    {UDATA(&mt_srv, UNIT_MT(2), 0), 0}, /* 2 */
    {UDATA(&mt_srv, UNIT_MT(2), 0), 0}, /* 3 */
    {UDATA(&mt_srv, UNIT_MT(2), 0), 0}, /* 4 */
    {UDATA(&mt_srv, UNIT_MT(2), 0), 0}, /* 5 */
    {UDATA(&mt_srv, UNIT_MT(2), 0), 0}, /* 6 */
    {UDATA(&mt_srv, UNIT_MT(2), 0), 0}, /* 7 */
    {UDATA(&mt_srv, UNIT_MT(2), 0), 0}, /* 8 */
    {UDATA(&mt_srv, UNIT_MT(2), 0), 0}, /* 9 */
#if (NUM_DEVS_MT > 2)
/* Controller 3 */
    {UDATA(&mt_srv, UNIT_MT(3), 0), 0}, /* 0 */
    {UDATA(&mt_srv, UNIT_MT(3), 0), 0}, /* 1 */
    {UDATA(&mt_srv, UNIT_MT(3), 0), 0}, /* 2 */
    {UDATA(&mt_srv, UNIT_MT(3), 0), 0}, /* 3 */
    {UDATA(&mt_srv, UNIT_MT(3), 0), 0}, /* 4 */
    {UDATA(&mt_srv, UNIT_MT(3), 0), 0}, /* 5 */
    {UDATA(&mt_srv, UNIT_MT(3), 0), 0}, /* 6 */
    {UDATA(&mt_srv, UNIT_MT(3), 0), 0}, /* 7 */
    {UDATA(&mt_srv, UNIT_MT(3), 0), 0}, /* 8 */
    {UDATA(&mt_srv, UNIT_MT(3), 0), 0}, /* 9 */
#if (NUM_DEVS_MT > 3)
/* Controller 4 */
    {UDATA(&mt_srv, UNIT_MT(4), 0), 0}, /* 0 */
    {UDATA(&mt_srv, UNIT_MT(4), 0), 0}, /* 1 */
    {UDATA(&mt_srv, UNIT_MT(4), 0), 0}, /* 2 */
    {UDATA(&mt_srv, UNIT_MT(4), 0), 0}, /* 3 */
    {UDATA(&mt_srv, UNIT_MT(4), 0), 0}, /* 4 */
    {UDATA(&mt_srv, UNIT_MT(4), 0), 0}, /* 5 */
    {UDATA(&mt_srv, UNIT_MT(4), 0), 0}, /* 6 */
    {UDATA(&mt_srv, UNIT_MT(4), 0), 0}, /* 7 */
    {UDATA(&mt_srv, UNIT_MT(4), 0), 0}, /* 8 */
    {UDATA(&mt_srv, UNIT_MT(4), 0), 0}, /* 9 */
#if (NUM_DEVS_MT > 4)
/* Controller 5 */
    {UDATA(&mt_srv, UNIT_MT(5), 0), 0}, /* 0 */
    {UDATA(&mt_srv, UNIT_MT(5), 0), 0}, /* 1 */
    {UDATA(&mt_srv, UNIT_MT(5), 0), 0}, /* 2 */
    {UDATA(&mt_srv, UNIT_MT(5), 0), 0}, /* 3 */
    {UDATA(&mt_srv, UNIT_MT(5), 0), 0}, /* 4 */
    {UDATA(&mt_srv, UNIT_MT(5), 0), 0}, /* 5 */
    {UDATA(&mt_srv, UNIT_MT(5), 0), 0}, /* 6 */
    {UDATA(&mt_srv, UNIT_MT(5), 0), 0}, /* 7 */
    {UDATA(&mt_srv, UNIT_MT(5), 0), 0}, /* 8 */
    {UDATA(&mt_srv, UNIT_MT(5), 0), 0}, /* 9 */
#if (NUM_DEVS_MT > 5)
/* Controller 6 */
    {UDATA(&mt_srv, UNIT_MT(6), 0), 0}, /* 0 */
    {UDATA(&mt_srv, UNIT_MT(6), 0), 0}, /* 1 */
    {UDATA(&mt_srv, UNIT_MT(6), 0), 0}, /* 2 */
    {UDATA(&mt_srv, UNIT_MT(6), 0), 0}, /* 3 */
    {UDATA(&mt_srv, UNIT_MT(6), 0), 0}, /* 4 */
    {UDATA(&mt_srv, UNIT_MT(6), 0), 0}, /* 5 */
    {UDATA(&mt_srv, UNIT_MT(6), 0), 0}, /* 6 */
    {UDATA(&mt_srv, UNIT_MT(6), 0), 0}, /* 7 */
    {UDATA(&mt_srv, UNIT_MT(6), 0), 0}, /* 8 */
    {UDATA(&mt_srv, UNIT_MT(6), 0), 0}, /* 9 */
#endif
#endif
#endif
#endif
#endif
#endif
#ifdef MT_CHANNEL_ZERO
/* Controller 7 */
    {UDATA(&mt_srv, UNIT_MT(0), 0), 0}, /* 0 */
    {UDATA(&mt_srv, UNIT_MT(0), 0), 0}, /* 1 */
    {UDATA(&mt_srv, UNIT_MT(0), 0), 0}, /* 2 */
    {UDATA(&mt_srv, UNIT_MT(0), 0), 0}, /* 3 */
    {UDATA(&mt_srv, UNIT_MT(0), 0), 0}, /* 4 */
    {UDATA(&mt_srv, UNIT_MT(0), 0), 0}, /* 5 */
    {UDATA(&mt_srv, UNIT_MT(0), 0), 0}, /* 6 */
    {UDATA(&mt_srv, UNIT_MT(0), 0), 0}, /* 7 */
    {UDATA(&mt_srv, UNIT_MT(0), 0), 0}, /* 8 */
    {UDATA(&mt_srv, UNIT_MT(0), 0), 0}, /* 9 */
#endif
};

MTAB                mt_mod[] = {
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL, NULL, NULL,
       "Write ring in place"},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL, NULL, NULL,
       "No write ring in place"},
    {MTUF_LDN, 0, "high density", "HIGH", &mt_tape_density, NULL, NULL,
        "556 BPI"},
    {MTUF_LDN, MTUF_LDN, "low density", "LOW", &mt_tape_density, NULL, NULL,
        "200 BPI"},
#ifdef I7090
    {MTUF_ONLINE, 0, "offline", "OFFLINE", NULL, NULL, NULL,
        "Tape offline"},
    {MTUF_ONLINE, MTUF_ONLINE, "online", "ONLINE", NULL, NULL, NULL,
        "Tape Online"},
#endif
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL, 
       "Set/Display tape format (SIMH, E11, TPC, P7B)"},
   {MTAB_XTD | MTAB_VUN, 0, "LENGTH", "LENGTH",
     &sim_tape_set_capac, &sim_tape_show_capac, NULL,
      "Set unit n capacity to arg MB (0 = unlimited)" },
    {MTAB_XTD | MTAB_VUN, 0, NULL, "REWIND",
     &mt_rew, NULL, NULL, "Rewind tape"
    },
#ifdef I7090
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan, &get_chan,
     NULL, "Device Channel"},
#endif
    {0}
};

#ifdef MT_CHANNEL_ZERO
DEVICE              mtz_dev = {
    "MT", &mta_unit[NUM_DEVS_MT * 10], NULL, mt_mod,
    NUM_UNITS_MT, 8, 15, 1, 8, 8,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mt_dib, DEV_BUF_NUM(NUM_DEVS_MT) | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL, &mt_description
};
#endif

#if (NUM_DEVS_MT > 0)
DEVICE              mta_dev = {
    "MTA", mta_unit, NULL, mt_mod,
    NUM_UNITS_MT, 8, 15, 1, 8, 8,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mt_dib, DEV_BUF_NUM(0) | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL, &mt_description
};

#if (NUM_DEVS_MT > 1)
DEVICE              mtb_dev = {
    "MTB", &mta_unit[10], NULL, mt_mod,
    NUM_UNITS_MT, 8, 15, 1, 8, 8,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mt_dib, DEV_BUF_NUM(1) | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL, &mt_description
};

#if (NUM_DEVS_MT > 2)
DEVICE              mtc_dev = {
    "MTC", &mta_unit[20], NULL, mt_mod,
    NUM_UNITS_MT, 8, 15, 1, 8, 8,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mt_dib, DEV_BUF_NUM(2) | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL, &mt_description
};

#if (NUM_DEVS_MT > 3)
DEVICE              mtd_dev = {
    "MTD", &mta_unit[30], NULL, mt_mod,
    NUM_UNITS_MT, 8, 15, 1, 8, 36,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mt_dib, DEV_BUF_NUM(3) | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL, &mt_description
};

#if (NUM_DEVS_MT > 4)
DEVICE              mte_dev = {
    "MTE", &mta_unit[40], NULL, mt_mod,
    NUM_UNITS_MT, 8, 15, 1, 8, 8,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mt_dib, DEV_BUF_NUM(4) | DEV_DIS | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL, &mt_description
};

#if (NUM_DEVS_MT > 5)
DEVICE              mtf_dev = {
    "MTF", &mta_unit[50], NULL, mt_mod,
    NUM_UNITS_MT, 8, 15, 1, 8, 8,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mt_dib, DEV_BUF_NUM(5) | DEV_DIS | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL, &mt_description
};
#endif
#endif
#endif
#endif
#endif
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

/* Rewind tape drive */
t_stat
mt_rew(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    /* If drive is offline or not attached return not ready */
    if ((uptr->flags & (UNIT_ATT | MTUF_ONLINE)) == 0)
        return SCPE_NOATT;
    /* Check if drive is ready to recieve a command */
    if ((uptr->u5 & MT_RDY) == 0)
        return STOP_IOCHECK;
    return sim_tape_rewind(uptr);
}

/* Start off a mag tape command */
uint32 mt_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 time = us_to_ticks(100);
    int                 unit = dev & 017;

    unit -= mt_dib.addr & 017;  /* Adjust to origin zero */
    /* Make sure valid drive number */
    if (unit > NUM_UNITS_MT || unit < 0)
        return SCPE_NODEV;
    uptr += unit;
    /* If unit disabled return error */
    if (uptr->flags & UNIT_DIS) {
        /* 
        fprintf(stderr, "Attempt to access disconnected unit %s%d\n",
                dptr->name, unit); */
        return SCPE_NODEV;
    }

    /* Check status of the drive */

    /* Can't do nothing if controller is busy */
    if (mt_chan[chan] & MTC_BSY)
        return SCPE_BUSY;
    /* If drive is offline or not attached return not ready */
    if ((uptr->flags & (UNIT_ATT | MTUF_ONLINE)) !=
        (UNIT_ATT | MTUF_ONLINE)) {
        fprintf(stderr, "Attempt to access offline unit %s%d\n",
                dptr->name, unit);
        return SCPE_IOERR;
    }
    /* Check if drive is ready to recieve a command */
    if ((uptr->u5 & MT_RDY) == 0) {
        /* Return indication if not ready and doing TRS */
        if (cmd == IO_TRS) {
            return SCPE_IOERR;
        } else
            return SCPE_BUSY;
    }
    uptr->u5 &= ~(MT_CMDMSK | MT_RDY);
    switch (cmd) {
    case IO_RDS:
        if (mt_chan[chan] & MTC_SEL) {
            uptr->u5 |= MT_RDY;
            return SCPE_BUSY;
        }

#ifdef I701
        uptr->u5 |= MT_RDSB;
#else
        if (dev & 020)
            uptr->u5 |= MT_RDSB;
        else
            uptr->u5 |= MT_RDS;
#endif
        time = us_to_ticks(3000);
        if ((uptr->u5 & MT_IDLE) == 0)
            time = us_to_ticks(4500);
        if (sim_tape_bot(uptr))
            time = us_to_ticks(21000);
        chan_set_sel(chan, 0);
        chan_clear_status(chan);
        mt_chan[chan] &= MTC_BSY;
        mt_chan[chan] |= MTC_SEL | unit;
#ifdef I7010
        uptr->u5 &= ~(MT_RM);
#else /* TEST */
        uptr->u5 &= ~(MT_RM|MT_EOR);
#endif /* TEST */
        uptr->u6 = -1;
        uptr->hwmark = -1;
        sim_debug(DEBUG_CMD, dptr, "RDS %s unit=%d %d\n", 
                  ((uptr->u5 & MT_CMDMSK) == MT_RDS) ? "BCD" : "Binary",
                         unit, dev);
        break;
    case IO_WRS:
        if (mt_chan[chan] & MTC_SEL) {
            uptr->u5 |= MT_RDY;
            return SCPE_BUSY;
        }
        if (sim_tape_wrp(uptr)) {
            sim_debug(DEBUG_EXP, dptr,
                      "WRS %d attempted on locked tape\n", unit);
            uptr->u5 |= MT_RDY;
            return SCPE_IOERR;
        }
#ifdef I701
        uptr->u5 |= MT_WRSB;
#else
        if (dev & 020)
            uptr->u5 |= MT_WRSB;
        else
            uptr->u5 |= MT_WRS;
#endif
        uptr->u6 = 0;
        uptr->hwmark = 0;
        chan_set_sel(chan, 1);
        chan_clear_status(chan);
        mt_chan[chan] &= MTC_BSY;
        mt_chan[chan] |= MTC_SEL | unit;
        uptr->u5 &= ~(MT_MARK | MT_EOT);
        time = us_to_ticks(6500);
        if ((uptr->u5 & MT_IDLE) == 0)
            time = us_to_ticks(10000);
        if (sim_tape_bot(uptr))
            time = us_to_ticks(41000);
        sim_debug(DEBUG_CMD, dptr, "WRS %s unit=%d %d\n",
                  ((uptr->u5 & MT_CMDMSK) == MT_WRS) ? "BCD" : "Binary",
                   unit, dev);
        break;
    case IO_RDB:
        if (mt_chan[chan] & MTC_SEL) {
            uptr->u5 |= MT_RDY;
            return SCPE_BUSY;
        }

        uptr->u5 |= MT_RDB;
        time = us_to_ticks(3000);
        if ((uptr->u5 & MT_IDLE) == 0)
            time = us_to_ticks(4500);
        if (sim_tape_bot(uptr))
            time = us_to_ticks(20000);
        chan_set_sel(chan, 0);
        chan_clear_status(chan);
        mt_chan[chan] &= MTC_BSY;
        mt_chan[chan] |= MTC_SEL | unit;
        uptr->u5 &= ~(MT_RM);
        uptr->u6 = -1;
        uptr->hwmark = -1;
        sim_debug(DEBUG_CMD, dptr, "RDB unit=%d %d\n", unit, dev);
        break;

    case IO_WEF:
        uptr->u5 &= ~(MT_EOT|MT_MARK);
        if (sim_tape_wrp(uptr)) {
            sim_debug(DEBUG_EXP, dptr,
                      "WRS %d attempted on locked tape\n", unit);
            uptr->u5 |= MT_RDY;
            return SCPE_IOERR;
        }
        if ((uptr->u5 & MT_IDLE) == 0)
            time = us_to_ticks(2700);
        uptr->u5 |= MT_WEF;
        mt_chan[chan] |= MTC_BSY;
        sim_debug(DEBUG_CMD, dptr, "WEF unit=%d\n", unit);
        break;

    case IO_BSR:
        uptr->u5 &= ~(MT_MARK);
        /* Check if at load point, quick return if so */
        if (sim_tape_bot(uptr)) {
            sim_debug(DEBUG_CMD, dptr, "BSR unit=%d at BOT\n", unit);
            uptr->u5 |= MT_RDY;
            chan_set(chan, CHS_BOT);
            return SCPE_OK;
        }
        uptr->u5 |= MT_BSR;
        mt_chan[chan] |= MTC_BSY;
        sim_debug(DEBUG_CMD, dptr, "BSR unit=%d\n", unit);
        break;
    case IO_BSF:
        uptr->u5 &= ~(MT_MARK);
        /* Check if at load point, quick return if so */
        if (sim_tape_bot(uptr)) {
            sim_debug(DEBUG_CMD, dptr, "BSF unit=%d at BOT\n", unit);
            uptr->u5 |= MT_RDY;
            chan_set(chan, CHS_BOT);
            return SCPE_OK;
        }
        uptr->u5 |= MT_BSF;
        mt_chan[chan] |= MTC_BSY;
        sim_debug(DEBUG_CMD, dptr, "BSF unit=%d\n", unit);
        break;
    case IO_SKR:
        uptr->u5 &= ~(MT_MARK);
        uptr->u5 |= MT_SKR;
#ifndef I7010
        mt_chan[chan] |= MTC_BSY;
#endif 
        sim_debug(DEBUG_CMD, dptr, "SKR unit=%d\n", unit);
        break;
    case IO_ERG:
        uptr->u5 &= ~(MT_MARK);
        uptr->u5 |= MT_ERG;
        mt_chan[chan] |= MTC_BSY;
        sim_debug(DEBUG_CMD, dptr, "ERG unit=%d\n", unit);
        break;
    case IO_REW:
        uptr->u5 &= ~(MT_EOT|MT_MARK);
        /* Check if at load point, quick return if so */
        if (sim_tape_bot(uptr)) {
            sim_debug(DEBUG_CMD, dptr, "REW unit=%d at BOT\n", unit);
            uptr->u5 |= MT_RDY;
#ifdef I7010
            chan_set(chan, CHS_BOT);
#endif
            return SCPE_OK;
        }
        uptr->u5 |= MT_REW;
        mt_chan[chan] |= MTC_BSY;
        sim_debug(DEBUG_CMD, dptr, "REW unit=%d\n", unit);
        break;
    case IO_RUN:
        uptr->u5 &= ~(MT_EOT|MT_MARK);
        chan_clear_status(chan);
        uptr->u5 |= MT_RUN;
        mt_chan[chan] |= MTC_BSY;
        sim_debug(DEBUG_CMD, dptr, "RUN unit=%d\n", unit);
        break;
    case IO_SDL:
        uptr->u5 |= MT_RDY;     /* Command is quick */
        uptr->flags |= MTUF_LDN;
        sim_debug(DEBUG_CMD, dptr, "SDN unit=%d low\n", unit);
        return SCPE_OK;
    case IO_SDH:
        uptr->u5 |= MT_RDY;     /* Command is quick */
        uptr->flags &= ~MTUF_LDN;
        sim_debug(DEBUG_CMD, dptr, "SDN unit=%d high\n", unit);
        return SCPE_OK;
    case IO_DRS:
        uptr->flags &= ~MTUF_ONLINE;
        uptr->u5 |= MT_RDY;     /* Command is quick */
        sim_debug(DEBUG_CMD, dptr, "DRS unit=%d\n", unit);
        return SCPE_OK;
    case IO_TRS:
        uptr->u5 |= MT_RDY;     /* Get here we are ready */
        sim_debug(DEBUG_CMD, dptr, "TRS unit=%d\n", unit);
        return SCPE_OK;
    }
    sim_cancel(uptr);
    sim_activate(uptr, time);
#ifdef I7080
    chan_set(chan, STA_TWAIT);
#endif
#ifdef I7010
    chan_set(chan, STA_TWAIT);
#endif
    return SCPE_OK;
}


#if I7090 | I704 | I701
/* Read a word from tape, used during boot read */
int
mt_read_buff(UNIT * uptr, int cmd, DEVICE * dptr, t_value *word)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 bufnum = GET_DEV_BUF(dptr->flags);
    int                 i;
    uint8               ch;
    int                 mode = 0;
    int                 mark = 1;
    int                 parity = 0;

    uptr->u5 &= ~MT_MARK;
    if (cmd == MT_RDS)
        mode = 0100;
        
    *word = 0;
    for(i = CHARSPERWORD-1; i >= 0 && uptr->u6 < (int32)uptr->hwmark; i--) {
        ch = mt_buffer[bufnum][uptr->u6++];
        /* Do BCD translation */
        if ((parity_table[ch & 077] ^ (ch & 0100) ^ mode) == 0) {
                parity = 1;
        }
        ch &= 077;
        /* Not needed on decimal machines */
        if (mode) {
            /* Map BCD to internal format */
            ch ^= (ch & 020) << 1;
            if (ch == 012)
                ch = 0;
            if (ch == 017 && mark) {
                chan_set_error(chan);   /* Force CRC error. */
                ch = 0;
                mark = 0;
                uptr->u6++;     /* Skip next character */
                i--;
            }
        }
        *word |= ((t_value) ch) << (6 * i);
    }

    if (parity) {
        chan_set_error(chan);   /* Force redundency error */
        return 0;
    }
    return 1;
}
#endif

/* Map simH errors into machine errors */
t_stat mt_error(UNIT * uptr, int chan, t_stat r, DEVICE * dptr)
{
    switch (r) {
    case MTSE_OK:               /* no error */
        break;

    case MTSE_TMK:              /* tape mark */
        sim_debug(DEBUG_EXP, dptr, "MARK ");
        chan_set_eof(chan);
        break;

    case MTSE_WRP:              /* write protected */
    case MTSE_UNATT:            /* unattached */
        sim_debug(DEBUG_EXP, dptr, "ATTENTION %d ", r);
        chan_set_attn(chan);
        break;

    case MTSE_IOERR:            /* IO error */
    case MTSE_FMT:              /* invalid format */
    case MTSE_RECE:             /* error in record */
        chan_set_error(chan);   /* Force redundency error */
        chan_set_attn(chan);    /* Set error */
        sim_debug(DEBUG_EXP, dptr, "ERROR %d ", r);
        break;
    case MTSE_BOT:              /* beginning of tape */
        chan_set(chan, CHS_BOT);        /* Set flag */
        sim_debug(DEBUG_EXP, dptr, "BOT ");
        break;
    case MTSE_INVRL:            /* invalid rec lnt */
    case MTSE_EOM:              /* end of medium */
        uptr->u5 |= MT_EOT;
        sim_debug(DEBUG_EXP, dptr, "EOT ");
        break;
    }
    return SCPE_OK;
}

/* Handle processing of tape requests. */
t_stat mt_srv(UNIT * uptr)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 unit = (uptr - dptr->units) & MTC_UNIT;
    int                 cmd = uptr->u5 & MT_CMDMSK;
    int                 bufnum = GET_DEV_BUF(dptr->flags);
    t_mtrlnt            reclen;
    t_stat              r = SCPE_ARG;   /* Force error if not set */
    uint8               ch;
    int                 mode = 0;
#ifdef I7010
    extern uint8        astmode;
#endif

    /* Call channel proccess to make sure data is ready */
    chan_proc();

    /* Channel has disconnected, abort current read. */
    if ((mt_chan[chan] & 037) == (MTC_SEL | unit) &&
                 chan_stat(chan, DEV_DISCO)) {
        uptr->u5 &= ~MT_CMDMSK;
#ifdef I7010
        if ((cmd == MT_WRS || cmd == MT_WRSB) && uptr->u6 > 0) {
            reclen = uptr->hwmark;
#else 
        if (cmd == MT_WRS || cmd == MT_WRSB) {
            if (uptr->u6 > 0) {
                 reclen = uptr->hwmark;
#endif
                 sim_debug(DEBUG_DETAIL, dptr, 
                        "Write flush unit=%d %s Block %d chars\n",
                         unit, (cmd == MT_WRS) ? "BCD" : "Binary", reclen);
                 r = sim_tape_wrrecf(uptr, &mt_buffer[bufnum][0], reclen);
                 mt_error(uptr, chan, r, dptr);      /* Record errors */
#ifndef I7010
            }
#endif 
            sim_activate(uptr, us_to_ticks(6000));
            mt_chan[chan] &= MTC_BSY;
            uptr->u5 |= MT_RDY;
        } else if (cmd == MT_RDS || cmd == MT_RDSB) {
            /* Keep moving until end of block */
            if (uptr->u6 < (int32)uptr->hwmark ) {
                int i = (uptr->hwmark-uptr->u6) *
                           ((uptr->flags & MTUF_LDN) ?LT:HT);
                uptr->u5 |= MT_SKIP;
                sim_activate(uptr, us_to_ticks(i));
            } else {
#ifndef I7010
                if (uptr->u5 & MT_MARK) {
                /* We hit tapemark, Back up so next read hits it */
                /* Or write starts just before it */
                /* This is due to SIMH returning mark after read */
                    (void) sim_tape_sprecr(uptr, &reclen);
                    uptr->u5 &= ~MT_MARK;
                } 
#endif 
                sim_activate(uptr, us_to_ticks(6000));
                uptr->u5 |= MT_RDY;
                mt_chan[chan] &= MTC_BSY;
            }
        } else {
            sim_activate(uptr, us_to_ticks(100));
#ifndef I7010
            uptr->u5 |= MT_RDY;
#endif 
            mt_chan[chan] &= MTC_BSY;
        }
        uptr->u6 = 0;
        uptr->hwmark = 0;
        sim_debug(DEBUG_CHAN, dptr, "Disconnect unit=%d\n", unit);
        uptr->u5 |= MT_IDLE;
        chan_clear(chan, DEV_DISCO | DEV_WEOR | DEV_SEL);
#ifdef I7080
        chan_clear(chan, STA_TWAIT);
#endif
        return SCPE_OK;
    }

    switch (cmd) {
    case 0:                     /* No command, stop tape */
        uptr->u5 &= ~MT_IDLE;
        uptr->u5 |= MT_RDY;     /* Ready since command is done */
#ifdef I7080
        chan_clear(chan, STA_TWAIT);
#endif
#ifdef I7010
        chan_clear(chan, STA_TWAIT);
#endif
        sim_debug(DEBUG_DETAIL, dptr, "Idle unit=%d\n", unit);
        return SCPE_OK;

    case MT_SKIP:               /* Record skip done, enable tape drive */
        uptr->u5 &= ~MT_CMDMSK;
        uptr->u5 |= MT_RDY | MT_IDLE;
#ifdef I7080
        chan_clear(chan, STA_TWAIT);
#endif
#ifndef I7010
        chan_clear(chan, DEV_SEL);
#endif /* TEST */
        mt_chan[chan] &= MTC_BSY;       /* Clear all but busy */
        sim_debug(DEBUG_DETAIL, dptr, "Skip unit=%d\n", unit);
        sim_activate(uptr, 
             (uptr->flags & MTUF_LDN) ? us_to_ticks(2500): us_to_ticks(4250));
        return SCPE_OK;

    case MT_RDS:
         mode = 0100;
    case MT_RDSB:
#ifndef I7010
        /* Post EOR */
        if (uptr->u5 & MT_EOR) {
            sim_debug(DEBUG_DETAIL, dptr, "Read unit=%d post EOR\n", unit);
            chan_set(chan, DEV_REOR);
            uptr->u5 &= ~ MT_EOR;
            sim_activate(uptr, (uptr->flags & MTUF_LDN) ?
                    us_to_ticks(4250): us_to_ticks(2500));
            return SCPE_OK;
        }
#endif 
        /* If tape mark pending, return it */
        if (chan_test(chan, DEV_FULL) == 0 && uptr->u5 & MT_MARK) {
            sim_debug(DEBUG_DETAIL, dptr, "Read unit=%d post ", unit);
            uptr->u5 &= ~(MT_CMDMSK|MT_MARK);
#ifdef I7010
            if (astmode) {
                ch = mode?017:054;
                chan_write_char(chan, &ch, DEV_REOR);
                if (mode) {
                   chan_clear(chan, STA_TWAIT);
                   sim_activate(uptr, us_to_ticks(100));
                   return SCPE_OK;
                }
            }
#endif
            chan_set_attn(chan);
            sim_activate(uptr, us_to_ticks(100));
            return mt_error(uptr, chan, MTSE_TMK, dptr);
        }
        /* If at end of record, fill buffer */
        if (uptr->u6 == uptr->hwmark) {
            sim_debug(DEBUG_DETAIL, dptr, "Read unit=%d ", unit);
            if ((r = sim_tape_rdrecf(uptr, &mt_buffer[bufnum][0], &reclen, 
                                BUFFSIZE)) != MTSE_OK) {
                sim_activate(uptr, us_to_ticks(100));
                if (r == MTSE_TMK && uptr->u6 != -1) {
                    sim_debug(DEBUG_DETAIL, dptr, "pend TM\n");
                    sim_activate(uptr, (uptr->flags & MTUF_LDN) ?
                            us_to_ticks(4250): us_to_ticks(2500));
                    uptr->u5 |= MT_MARK;
                    r = MTSE_OK;
                } else { 
                    uptr->u5 &= ~MT_CMDMSK;
#ifdef I7010
                    /* Translate TM characters for 7010 */
                    if (r == MTSE_TMK && astmode) {
                        sim_debug(DEBUG_DETAIL, dptr, "Read TM ");
                        ch = mode?017:054;
                        chan_write_char(chan, &ch, DEV_REOR);
                        chan_clear(chan, STA_TWAIT);
                        if (mode) {
                           sim_activate(uptr, us_to_ticks(100));
                           return SCPE_OK;
                        }
                    }
#endif
                    chan_set_attn(chan);
                }
                return mt_error(uptr, chan, r, dptr);
            }
            uptr->u6 = 0;
            uptr->hwmark = reclen;
            chan_clear(chan, CHS_EOF|CHS_ERR);
            sim_debug(DEBUG_DETAIL, dptr, "%s Block %d chars\n",
                      (cmd == MT_RDS) ? "BCD" : "Binary", reclen);
        }

        ch = mt_buffer[bufnum][uptr->u6++];
        /* Do BCD translation */
        if ((parity_table[ch & 077] ^ (ch & 0100) ^ mode) == 0) {
#ifdef I7010
            if (astmode) 
                ch = 054;
#endif
            chan_set_error(chan);
            chan_set_attn(chan);
        } 
#if I7090 | I704 | I701
        /* Not needed on decimal machines */
        if (mode) {
            /* Map BCD to internal format */
            ch ^= (ch & 020) << 1;
            if (ch == 012)
                ch = 0;
            if (ch == 017) {
                chan_set_error(chan);   /* Force CRC error. */
                if ((uptr->u5 & MT_RM) == 0) {
                     ch = 0;
                     uptr->u5 |= MT_RM;
                     mt_buffer[bufnum][uptr->u6] = 0;
                }
            } 
        }
#endif
        ch &= 077;

        /* Convert one word. */
            switch (chan_write_char(chan, &ch, 
#ifdef I7010
                                (uptr->u6 >= (int32)uptr->hwmark) ? DEV_REOR : 0)) {
#else 
                                /*(uptr->u6 >= (int32)uptr->hwmark) ? DEV_REOR :*/ 0)) {
#endif 
            case END_RECORD:
                sim_debug(DEBUG_DATA, dptr, "Read unit=%d EOR\n", unit);
                /* If not read whole record, skip till end */
#ifndef I7010
                uptr->u5 |= MT_EOR;
#endif 
                if (uptr->u6 < (int32)uptr->hwmark) {
#ifdef I7010
                    sim_activate(uptr, (uptr->hwmark-uptr->u6) * 20);
#else 
                    int i = (uptr->hwmark-uptr->u6) *
                           ((uptr->flags & MTUF_LDN) ?LT:HT);
                    i += (uptr->flags & MTUF_LDN) ? 100 : 50;
                    sim_activate(uptr, us_to_ticks(i));
#endif
#ifdef I7010
                    chan_set(chan, DEV_REOR);
#endif
                    uptr->u6 = uptr->hwmark;    /* Force read next record */
#ifdef I7010
                    break;
                }
#else 
                } else 
                    sim_activate(uptr, (uptr->flags & MTUF_LDN) ?
                            us_to_ticks(150): us_to_ticks(100));
                break;
#endif

            case DATA_OK:
                sim_debug(DEBUG_DATA, dptr, "Read data unit=%d %d %02o\n",
                          unit, uptr->u6, ch);
                if (uptr->u6 >= (int32)uptr->hwmark) { /* In IRG */
#ifndef I7010
                    uptr->u5 |= MT_EOR;
#endif
                    sim_activate(uptr, 
                      (uptr->flags & MTUF_LDN) ?
                            us_to_ticks(150): us_to_ticks(100));
                } else
                    sim_activate(uptr,
                      (uptr->flags & MTUF_LDN) ?
                          us_to_ticks(LT): us_to_ticks(HT));
                break;
    
            case TIME_ERROR:
                uptr->u5 &= ~MT_CMDMSK;
                uptr->u5 |= MT_SKIP;
                sim_activate(uptr, 
                    us_to_ticks(((uptr->flags & MTUF_LDN) ?4250:2500) +
                         ((uptr->hwmark-uptr->u6) *
                           ((uptr->flags & MTUF_LDN) ?LT:HT))));
                uptr->u6 = uptr->hwmark;        /* Force read next record */
                break;
            }
        return SCPE_OK;


    /* Check mode */
    case MT_WRS:
        mode = 0100;
    case MT_WRSB:
        switch (chan_read_char(chan, &ch,
                          (uptr->u6 > BUFFSIZE) ? DEV_WEOR : 0)) {
        case TIME_ERROR:
#ifdef I7010
            /* If no data was written, simulate a write gap */
            if (uptr->u6 == 0) {
                r = sim_tape_wrgap(uptr, 35);
            }
#endif 
            chan_set_attn(chan);
        case END_RECORD:
            if (uptr->u6 > 0) { /* Only if data in record */
                reclen = uptr->hwmark;
                sim_debug(DEBUG_DETAIL, dptr, 
                        "Write unit=%d %s Block %d chars\n",
                         unit, (cmd == MT_WRS) ? "BCD" : "Binary", reclen);
                r = sim_tape_wrrecf(uptr, &mt_buffer[bufnum][0], reclen);
                uptr->u6 = 0;
                uptr->hwmark = 0;
                mt_error(uptr, chan, r, dptr);  /* Record errors */
            }
            sim_activate(uptr, (uptr->flags & MTUF_LDN) ?
                    us_to_ticks(4250): us_to_ticks(2500));
            break;
        case DATA_OK:
            /* Copy data to buffer */
            ch &= 077;
#if I7090 | I701 | I704
            /* Not needed on decimal machines */
            if (mode) {
                /* Do BCD translation */
                ch ^= (ch & 020) << 1;
                if (ch == 0)
                    ch = 012;
            }
#endif
            ch |= mode ^ parity_table[ch] ^ 0100;
            mt_buffer[bufnum][uptr->u6++] = ch;
            sim_debug(DEBUG_DATA, dptr, "Write data unit=%d %d %02o\n",
                      unit, uptr->u6, ch);
            uptr->hwmark = uptr->u6;
            break;
        }
        sim_activate(uptr,
                (uptr->flags & MTUF_LDN) ? us_to_ticks(LT): us_to_ticks(HT));
        return SCPE_OK;

    case MT_RDB:
        /* If tape mark pending, return it */
        if (chan_test(chan, DEV_FULL) == 0 && uptr->u5 & MT_MARK) {
            sim_debug(DEBUG_DETAIL, dptr, "Read unit=%d post ", unit);
            uptr->u5 &= ~(MT_CMDMSK|MT_MARK);
            mt_chan[chan] &= MTC_BSY;
            chan_clear(chan, DEV_SEL);
            sim_activate(uptr, us_to_ticks(100));
            return mt_error(uptr, chan, MTSE_TMK, dptr);
        }
        /* If at end of record, fill buffer */
        if (uptr->u6 == uptr->hwmark) {
            sim_debug(DEBUG_DETAIL, dptr, "Read unit=%d ", unit);
            if ((r = sim_tape_rdrecr(uptr, &mt_buffer[bufnum][0], &reclen, 
                                BUFFSIZE)) != MTSE_OK) {
                sim_activate(uptr, us_to_ticks(100));
                if (r == MTSE_TMK && uptr->u6 != -1) {
                    sim_debug(DEBUG_DETAIL, dptr, "pend TM\n");
                    uptr->u5 |= MT_MARK;
                    r = MTSE_OK;
                } else { 
                    uptr->u5 &= ~MT_CMDMSK;
                    chan_set_attn(chan);
                    chan_clear(chan, DEV_SEL);
                    mt_chan[chan] &= MTC_BSY;
                }
                return mt_error(uptr, chan, r, dptr);
            }
            uptr->u6 = 0;
            uptr->hwmark = reclen;
            chan_clear(chan, CHS_EOF|CHS_ERR);
            sim_debug(DEBUG_DETAIL, dptr, "Binary Block %d chars\n", reclen);
        }

        ch = mt_buffer[bufnum][uptr->u6++];
        /* Do BCD translation */
        if ((parity_table[ch & 077] ^ (ch & 0100) ^ mode) == 0) {
            chan_set_error(chan);
            chan_set_attn(chan);
        fprintf(stderr, "Parity error %d: %03o\n", uptr->u6-1, ch);
        } 
        ch &= 077;

        /* Convert one word. */
            switch (chan_write_char(chan, &ch, 
                                (uptr->u6 >= (int32)uptr->hwmark) ? DEV_REOR : 0)) {
            case END_RECORD:
                sim_debug(DEBUG_DATA, dptr, "Read unit=%d EOR\n", unit);
                if (uptr->u6 >= (int32)uptr->hwmark) {
                    uptr->u5 &= ~MT_CMDMSK;
                    uptr->u5 |= MT_SKIP;
                    sim_activate(uptr, 
                        us_to_ticks(((uptr->hwmark-uptr->u6) *
                               ((uptr->flags & MTUF_LDN) ?LT:HT))));
                    chan_set(chan, DEV_REOR);
                    uptr->u6 = uptr->hwmark;    /* Force read next record */
                    break;
                }
            case DATA_OK:
                sim_debug(DEBUG_DATA, dptr, "Read data unit=%d %d %02o\n",
                          unit, uptr->u6, ch);
                if (uptr->u6 >= (int32)uptr->hwmark) { /* In IRG */
                    sim_activate(uptr, 
                      (uptr->flags & MTUF_LDN) ?
                            us_to_ticks(4250): us_to_ticks(2500));
                } else
                    sim_activate(uptr,
                      (uptr->flags & MTUF_LDN) ?
                          us_to_ticks(LT): us_to_ticks(HT));
                break;
    
            case TIME_ERROR:
                uptr->u5 &= ~MT_CMDMSK;
                uptr->u5 |= MT_SKIP;
                sim_activate(uptr, 
                        us_to_ticks(((uptr->hwmark-uptr->u6) *
                               ((uptr->flags & MTUF_LDN) ?LT:HT))));
                uptr->u6 = uptr->hwmark;        /* Force read next record */
                break;
            }
        return SCPE_OK;

    case MT_WEF:
        sim_debug(DEBUG_DETAIL, dptr, "Write Mark unit=%d\n", unit);
        uptr->u5 &= ~(MT_CMDMSK|MT_MARK);
        uptr->u5 |= (MT_RDY | MT_IDLE);
        r = sim_tape_wrtmk(uptr);
        mt_chan[chan] &= ~MTC_BSY;
        sim_activate(uptr, (uptr->flags & MTUF_LDN) ?
                            us_to_ticks(5000): us_to_ticks(3000));
#ifdef I7080
        chan_clear(chan, STA_TWAIT);
#endif
        break;

    case MT_BSR:
        sim_debug(DEBUG_DETAIL, dptr, "Backspace rec unit=%d ", unit);
        /* Clear tape mark, command, idle since we will need to change dir */
        uptr->u5 &= ~(MT_CMDMSK | MT_EOT | MT_IDLE | MT_RDY);
        r = sim_tape_sprecr(uptr, &reclen);
        mt_chan[chan] &= ~MTC_BSY;
#ifdef I7080
        chan_clear(chan, STA_TWAIT);
#endif
        if (r == MTSE_TMK) {
#ifdef I7080
            chan_set_eof(chan);
#else
            /* We don't set EOF on BSR */
#endif
            sim_debug(DEBUG_DETAIL, dptr, "MARK\n");
            sim_activate(uptr, (uptr->flags & MTUF_LDN) ?
                        us_to_ticks(4250):us_to_ticks(2500));
            return SCPE_OK;
        }
        sim_debug(DEBUG_DETAIL, dptr, "%d \n", reclen);
        sim_activate(uptr, 
             us_to_ticks(((uptr->flags & MTUF_LDN) ?4250:2500) +
                 (reclen * ((uptr->flags & MTUF_LDN) ?LT:HT))));
#ifdef I7010
        break;
#else /* TEST */
        return SCPE_OK;
#endif /* TEST */

    case MT_BSF:
        uptr->u5 &= ~(MT_IDLE | MT_RDY | MT_EOT);
        r = sim_tape_sprecr(uptr, &reclen);
        /* If we hit mark or end of tape */
        if (r == MTSE_TMK || r == MTSE_BOT) {
            sim_debug(DEBUG_DETAIL, dptr, "Backspace file unit=%d\n",
                      unit);
            uptr->u5 &= ~MT_CMDMSK;
            mt_chan[chan] &= ~MTC_BSY;
            sim_activate(uptr, (uptr->flags & MTUF_LDN) ?
                        us_to_ticks(4250):us_to_ticks(2500));
#ifdef I7080
        chan_clear(chan, STA_TWAIT);
#endif
        } else {
            sim_activate(uptr, 
                 us_to_ticks(((uptr->flags & MTUF_LDN) ?4250:2500) +
                       (reclen * ((uptr->flags & MTUF_LDN) ?LT:HT))));
        }
#ifdef I7010
        break;
#else /* TEST */
        return SCPE_OK;
#endif /* TEST */

    case MT_SKR:
        sim_debug(DEBUG_DETAIL, dptr, "Skip rec unit=%d ", unit);
        /* Clear tape mark, command, idle since we will need to change dir */
        uptr->u5 &= ~(MT_CMDMSK | MT_EOT | MT_IDLE | MT_RDY);
#ifndef I7010
        uptr->u5 |= MT_SKIP;
#endif /* TEST */
        r = sim_tape_sprecf(uptr, &reclen);
#ifdef I7010
        mt_chan[chan] &= ~MTC_BSY;
#else 
        /* We are like read that transfers nothing */
        chan_set(chan, DEV_REOR);
#endif 
        /* We don't set EOF on SKR */
        if (r == MTSE_TMK) {
            sim_debug(DEBUG_DETAIL, dptr, "MARK\n");
            sim_activate(uptr, (uptr->flags & MTUF_LDN) ?
                        us_to_ticks(4250):us_to_ticks(2500));
            return SCPE_OK;
        }
        if (r != MTSE_OK)
           reclen = 10;
        sim_debug(DEBUG_DETAIL, dptr, "%d\n", reclen);
        sim_activate(uptr, 
                 us_to_ticks(((uptr->flags & MTUF_LDN) ?4250:2500) +
                       (reclen * ((uptr->flags & MTUF_LDN) ?LT:HT))));
        break;

    case MT_ERG:
        sim_debug(DEBUG_DETAIL, dptr, "Erase unit=%d\n", unit);
        uptr->u5 &= ~(MT_CMDMSK|MT_MARK);
#ifdef I7010
        uptr->u5 |= (MT_RDY | MT_IDLE);
#else 
        uptr->u5 |= MT_SKIP;
#endif 
        r = sim_tape_wrgap(uptr, 35);
        mt_chan[chan] &= ~MTC_BSY;
        sim_activate(uptr, (uptr->flags & MTUF_LDN) ?
                        us_to_ticks(4250):us_to_ticks(2500));
        break;

    case MT_REW:
        sim_debug(DEBUG_DETAIL, dptr, "Rewind unit=%d\n", unit);
        uptr->u5 &= ~(MT_CMDMSK | MT_IDLE | MT_RDY);
        r = sim_tape_rewind(uptr);
        sim_activate(uptr, 30000);
        mt_chan[chan] &= ~MTC_BSY;
#ifdef I7080
        chan_clear(chan, STA_TWAIT);
#endif
        break;

    case MT_RUN:
        sim_debug(DEBUG_DETAIL, dptr, "Unload unit=%d\n", unit);
        uptr->u5 &= ~(MT_CMDMSK | MT_IDLE | MT_RDY);
#ifdef I7010
        chan_clear(chan, STA_TWAIT);
#endif
        r = sim_tape_detach(uptr);
        mt_chan[chan] &= ~MTC_BSY;
#ifdef I7080
        chan_clear(chan, STA_TWAIT);
#endif
        break;

    }
    return mt_error(uptr, chan, r, dptr);
}

/* Boot from given device */
t_stat
mt_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    uint16              dev = unit_num + 020 + mt_dib.addr;
#if I7090 | I704 | I701
    t_mtrlnt            reclen;
    t_stat              r;
#endif

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    /* Start a read. */
    if (mt_cmd(dptr->units, IO_RDS, dev) != SCPE_OK) 
        return STOP_IONRDY;

#if I7090 | I704 | I701
    r = sim_tape_rdrecf(uptr, &mt_buffer[GET_DEV_BUF(dptr->flags)][0], &reclen, BUFFSIZE);
    if (r != SCPE_OK)
        return r;
    uptr->u6 = 0;
    uptr->hwmark = reclen;

    /* Copy first three records. */
    mt_read_buff(uptr, MT_RDSB, dptr, &M[0]);
    mt_read_buff(uptr, MT_RDSB, dptr, &M[1]);
    if (UNIT_G_CHAN(uptr->flags) != 0)
        mt_read_buff(uptr, MT_RDSB, dptr, &M[2]);
    /* Make sure channel is set to start reading rest. */
#endif
    return chan_boot(unit_num, dptr);
}

void
mt_ini(UNIT * uptr, t_bool f)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);

    if (uptr->flags & UNIT_ATT)
        uptr->u5 = MT_RDY;
    else
        uptr->u5 = 0;
    mt_chan[chan] = 0;
}

t_stat
mt_reset(DEVICE * dptr)
{
    UNIT        *uptr = dptr->units;
    uint32       i;
    for (i = 0; i < dptr->numunits; i++) {
       sim_tape_set_dens (uptr, 
              ((uptr->flags & MTUF_LDN) ? MT_DENS_200 : MT_DENS_556),
              NULL, NULL);
       uptr++;
    }
    return SCPE_OK;
}

t_stat
mt_tape_density(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    return SCPE_OK;
}

t_stat
mt_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_tape_attach(uptr, file)) != SCPE_OK)
        return r;
    uptr->u5 |= MT_RDY;
    uptr->flags |= MTUF_ONLINE;
    uptr->dynflags = MT_200_VALID | MT_556_VALID |
           (((uptr->flags & MTUF_LDN) ? MT_556_VALID : MT_200_VALID) < UNIT_V_DF_TAPE);
    return SCPE_OK;
}

t_stat
mt_detach(UNIT * uptr)
{
    uptr->u5 = 0;
    uptr->flags &= ~MTUF_ONLINE;
    return sim_tape_detach(uptr);
}

t_stat
mt_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "IBM 729 Magnetic tape unit\n\n");
   fprintf (st, "The magnetic tape controller assumes that all tapes are 7 track\n");
   fprintf (st, "with valid parity. Tapes are assumed to be 555.5 characters per\n");
   fprintf (st, "inch. To simulate a standard 2400foot tape, do:\n");
   fprintf (st, "    sim> SET MTn LENGTH 15\n\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
mt_description(DEVICE *dptr)
{
   return "IBM 729 Magnetic tape unit";
}

#endif
