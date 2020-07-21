/* sel32_con.c: SEL 32 Class F IOP processor console.

   Copyright (c) 2018-2020, James C. Bevier
   Portions provided by Richard Cornwell, Geert Rolf and other SIMH contributers

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

   This is the standard console interface.  It is subchannel of the IOP 7e00.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as ASCII characters.
*/

#include "sel32_defs.h"
#include "sim_tmxr.h"

#if NUM_DEVS_CON > 0

#define UNIT_CON   UNIT_IDLE | UNIT_DISABLE

#define CMD     u3
/* Held in u3 is the device command and status */
#define CON_INCH    0x00    /* Initialize channel command */
#define CON_INCH2   0xf0    /* Initialize channel command for processing */
#define CON_WR      0x01    /* Write console */
#define CON_RD      0x02    /* Read console */
#define CON_NOP     0x03    /* No op command */
#define CON_SNS     0x04    /* Sense command */
#define CON_ECHO    0x0a    /* Read with Echo */
#define CON_CON     0x1f    /* connect line */
#define CON_DIS     0x23    /* disconnect line */
#define CON_RWD     0x37    /* TOF and write line */

#define CON_MSK     0xff    /* Command mask */

/* Status held in u3 */
/* controller/unit address in upper 16 bits */
#define CON_INPUT   0x100   /* Input ready for unit */
#define CON_CR      0x200   /* Output at beginning of line */
#define CON_REQ     0x400   /* Request key pressed */
#define CON_EKO     0x800   /* Echo input character */
#define CON_OUTPUT  0x1000  /* Output ready for unit */
#define CON_READ    0x2000  /* Read mode selected */
#define CON_ATAT    0x4000  /* working on @@A input */

/* Input buffer pointer held in u4 */

#define SNS     u5
/* in u5 packs sense byte 0,1 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ  0x80000000    /* Command reject */
#define SNS_INTVENT 0x40000000    /* Unit intervention required */
/* sense byte 3 */
#define SNS_RDY     0x80        /* device ready */
#define SNS_ONLN    0x40        /* device online */
#define SNS_DSR     0x08        /* data set ready */
#define SNS_DCD     0x04        /* data carrier detect */

/* std devices. data structures
    con_dev     Console device descriptor
    con_unit    Console unit descriptor
    con_reg     Console register list
    con_mod     Console modifiers list
*/

struct _con_data
{
    uint8       incnt;          /* char count */
    uint8       ibuff[145];     /* Input line buffer */
}
con_data[NUM_UNITS_CON];

uint32  atbuf=0;                /* attention buffer */
uint32  outbusy = 0;            /* output waiting on timeout */
uint32  inbusy = 0;             /* input waiting on timeout */

/* forward definitions */
uint16  con_preio(UNIT *uptr, uint16 chan);
uint16  con_startcmd(UNIT*, uint16, uint8);
void    con_ini(UNIT*, t_bool);
t_stat  con_srvi(UNIT*);
t_stat  con_srvo(UNIT*);
uint16  con_haltio(UNIT *);
t_stat  con_poll(UNIT *);
t_stat  con_reset(DEVICE *);

/* channel program information */
CHANP           con_chp[NUM_UNITS_CON] = {0};

MTAB    con_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr, &show_dev_addr, NULL},
    {0}
};

UNIT            con_unit[] = {
    {UDATA(&con_srvi, UNIT_CON, 0), 0, UNIT_ADDR(0x7EFC)},   /* Input */
    {UDATA(&con_srvo, UNIT_CON, 0), 0, UNIT_ADDR(0x7EFD)},   /* Output */
};

//DIB               con_dib = {NULL, con_startcmd, NULL, NULL, NULL, con_ini, con_unit, con_chp, NUM_UNITS_CON, 0xf, 0x7e00, 0, 0, 0};
DIB             con_dib = {
    con_preio,      /* uint16 (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Start I/O */
    con_startcmd,   /* uint16 (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    con_haltio,     /* uint16 (*halt_io)(UNIT *uptr) */          /* Stop I/O */
    NULL,           /* uint16 (*test_io)(UNIT *uptr) */          /* Test I/O */
    NULL,           /* uint16 (*post_io)(UNIT *uptr) */          /* Post I/O */
    con_ini,        /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    con_unit,       /* UNIT* units */                           /* Pointer to units structure */
    con_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NUM_UNITS_CON,  /* uint8 numunits */                        /* number of units defined */
    0x0f,           /* uint8 mask */                            /* 2 devices - device mask */
    0x7e00,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE  con_dev = {
    "CON", con_unit, NULL, con_mod,
    NUM_UNITS_CON, 8, 15, 1, 8, 8,
    NULL, NULL, &con_reset, NULL, NULL, NULL,
    &con_dib, DEV_DIS|DEV_DISABLE|DEV_DEBUG, 0, dev_debug
};

/*
 * Console print routines.
 */
/* initialize the console chan/unit */
void con_ini(UNIT *uptr, t_bool f) {
    int     unit = (uptr - con_unit);   /* unit 0 */

    uptr->u4 = 0;                       /* no input count */
    con_data[unit].incnt = 0;           /* no input data */
    uptr->CMD &= LMASK;                 /* leave only chsa */
    uptr->SNS = SNS_RDY|SNS_ONLN;       /* status is online & ready */
    sim_activate(uptr, 1000);           /* time increment */
}

/* start a console operation */
uint16 con_preio(UNIT *uptr, uint16 chan) {
    DEVICE         *dptr = get_dev(uptr);
    int            unit = (uptr - dptr->units);

    if ((uptr->CMD & CON_MSK) != 0) {   /* just return if busy */
        sim_debug(DEBUG_CMD, &con_dev, "con_preio unit=%02x BUSY\n", unit);
        return SNS_BSY;
    }

    sim_debug(DEBUG_CMD, &con_dev, "con_preio unit=%02x OK\n", unit);
    return 0;                           /* good to go */
}

/* start an I/O operation */
uint16 con_startcmd(UNIT *uptr, uint16 chan, uint8 cmd) {
    int     unit = (uptr - con_unit);   /* unit 0 is read, unit 1 is write */
    uint8   ch;

    if ((uptr->CMD & CON_MSK) != 0) {   /* is unit busy */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_startcmd unit %01x chan %02x cmd %02x BUSY cmd %02x\n",
            unit, chan, cmd, uptr->CMD);
        return SNS_BSY;                 /* yes, return busy */
    }

    sim_debug(DEBUG_CMD, &con_dev,
        "con_startcmd unit %01x chan %02x cmd %02x enter\n", unit, chan, cmd);
    /* process the commands */
    switch (cmd & 0xFF) {
    case CON_INCH:      /* 0x00 */      /* INCH command */
        sim_debug(DEBUG_CMD, &con_dev, "con_startcmd %04x: Cmd INCH\n", chan);
        uptr->CMD &= LMASK;             /* leave only chsa */
        uptr->CMD |= CON_INCH2;         /* save INCH command as 0xf0 */
        uptr->SNS = SNS_RDY|SNS_ONLN;   /* status is online & ready */
        if (unit == 1)
//720       sim_activate(uptr, 240);     /* start us off */
            sim_activate(uptr, 200);     /* start us off */
        return 0;                       /* no status change */
        break;

    case CON_RWD:       /* 0x37 */      /* TOF and write line */
    case CON_WR:        /* 0x01 */      /* Write command */
        /* if input requested for output device, give error */
        uptr->CMD &= LMASK;             /* leave only chsa */
        uptr->CMD |= (cmd & CON_MSK);   /* save command */
        uptr->SNS = SNS_RDY|SNS_ONLN;   /* status is online & ready */
        if (unit == 1)
//720       sim_activate(uptr, 240);     /* start us off */
            sim_activate(uptr, 200);     /* start us off */
        return 0;                       /* no status change */
        break;

    case CON_RD:        /* 0x02 */      /* Read command */
    case CON_ECHO:      /* 0x0a */      /* Read command w/ECHO */
        /* if output requested for input device, give error */
        uptr->CMD &= ~CON_MSK;          /* remove old CMD */
        uptr->CMD |= (cmd & CON_MSK);   /* save command */
        if (cmd == CON_ECHO)            /* echo command? */
            uptr->CMD |= CON_EKO;       /* save echo status */
        uptr->CMD |= CON_READ;          /* show read mode */
        atbuf = 0;                      /* reset attention buffer */
        uptr->SNS = SNS_RDY|SNS_ONLN;   /* status is online & ready */
        if (unit == 1)
//720       sim_activate(uptr, 240);     /* start us off */
            sim_activate(uptr, 200);     /* start us off */
        return 0;
        break;

    case CON_NOP:       /* 0x03 */      /* NOP has do nothing */
        uptr->SNS = SNS_RDY|SNS_ONLN;   /* status is online & ready */
        uptr->CMD &= ~CON_MSK;          /* remove old CMD */
        uptr->CMD |= (cmd & CON_MSK);   /* save command */
        if (unit == 1)
//720       sim_activate(uptr, 240);     /* start us off */
            sim_activate(uptr, 200);     /* start us off */
        return 0;                       /* no status change */
        break;

    case 0x0C:          /* 0x0C */      /* Unknown command */
        uptr->SNS = SNS_RDY|SNS_ONLN;   /* status is online & ready */
        uptr->CMD &= LMASK;             /* leave only chsa */
        uptr->CMD |= (cmd & CON_MSK);   /* save command */
        if (unit == 1)
//720       sim_activate(uptr, 240);     /* start us off */
            sim_activate(uptr, 200);     /* start us off */
        return 0;                       /* no status change */
        break;

    case CON_CON:       /* 0x1f */      /* Connect, return Data Set ready */
        sim_debug(DEBUG_CMD, &con_dev, "con_startcmd %04x: Cmd %02x CON\n", chan, cmd);
        uptr->SNS |= (SNS_DSR|SNS_DCD); /* Data set ready, Data Carrier detected */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case CON_DIS:       /* 0x23 */      /* Disconnect has do nothing */
        sim_debug(DEBUG_CMD, &con_dev, "con_startcmd %04x: Cmd %02x DIS\n", chan, cmd);
        uptr->SNS &= ~(SNS_DSR|SNS_DCD); /* Data set not ready */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case CON_SNS:       /* 0x04 */      /* Sense */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_startcmd %04x: Cmd Sense %02x\n", chan, uptr->SNS);
        /* value 4 is Data Set Ready */
        /* value 5 is Data carrier detected n/u */
        ch = uptr->SNS;                 /* status */
        chan_write_byte(GET_UADDR(uptr->CMD), &ch);  /* write status */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    default:                            /* invalid command */
        break;
    }
    /* invalid command */
    uptr->SNS |= SNS_CMDREJ;            /* command rejected */
    sim_debug(DEBUG_CMD, &con_dev,
        "con_startcmd %04x: Invalid command %02x Sense %02x\n",
        chan, cmd, uptr->SNS);
    return SNS_CHNEND|STATUS_PCHK;
}

/* Handle output transfers for console */
t_stat con_srvo(UNIT *uptr) {
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         unit = (uptr - con_unit);       /* unit 0 is read, unit 1 is write */
    int         cmd = uptr->CMD & CON_MSK;
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */
    uint8       ch;

    sim_debug(DEBUG_CMD, &con_dev,
        "con_srvo enter CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);

    /* if input tried from output device, error */
    if ((cmd == CON_RD) || (cmd == CON_ECHO) || (cmd == 0xC0)) {  /* check for output */
        /* CON_RD:      0x02 */      /* Read command */
        /* CON_ECHO:    0x0a */      /* Read command w/ECHO */
        /* if input requested for output device, give error */
        if (unit == 1) {
            uptr->SNS |= SNS_CMDREJ;            /* command rejected */
            uptr->CMD &= LMASK;                 /* nothing left, command complete */
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvo Read to output device CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);
            chan_end(chsa, SNS_CHNEND|SNS_UNITCHK);    /* unit check */
            return SCPE_OK;
        }
    }

    if ((cmd == CON_NOP) || (cmd == CON_INCH2)) {   /* NOP has to do nothing */
        uptr->CMD &= LMASK;                     /* nothing left, command complete */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_srvo INCH/NOP unit %02x: CMD %08x cmd %02x incnt %02x u4 %02x\n",
            unit, uptr->CMD, cmd, con_data[unit].incnt, uptr->u4);
        if (cmd == CON_INCH2) {                 /* Channel end only for INCH */
            int len = chp->ccw_count;           /* INCH command count */
            uint32 mema = chp->ccw_addr;        /* get inch or buffer addr */
            //FIXME - test error return for error
//          int i = set_inch(uptr, mema);       /* new address */
            set_inch(uptr, mema);               /* new address */

            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvo INCH CMD %08x chsa %04x len %02x inch %06x\n", uptr->CMD, chsa, len, mema);
            chan_end(chsa, SNS_CHNEND);         /* INCH done */
        } else {
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvo NOP CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
        }
        return SCPE_OK;
    }

    if ((cmd == CON_WR) || (cmd == CON_RWD)) {
        int cnt = 0;
        /* see if write complete */
        if (uptr->CMD & CON_OUTPUT) {
            /* write is complete, post status */
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvo write CMD %08x chsa %04x cmd %02x complete\n",
                uptr->CMD, chsa, cmd);
            uptr->CMD &= LMASK;             /* nothing left, command complete */
/*RTC*/     outbusy = 0;                    /* output done */

            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
            return SCPE_OK;
        }
//Comment out clock flag 072020
//*RTC*/ outbusy = 1;                        /* tell clock output waiting */
        /* Write to device */
        while (chan_read_byte(chsa, &ch) == SCPE_OK) {  /* get byte from memory */
            /* HACK HACK HACK */
            ch &= 0x7f;                     /* make 7 bit w/o parity */
            sim_putchar(ch);                /* output next char to device */
            cnt++;                          /* count chars output */
        }
        uptr->CMD |= CON_OUTPUT;            /* output command complete */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_srvo write wait %03x CMD %08x chsa %04x cmd %02x to complete\n",
            19*cnt+23, uptr->CMD, chsa, cmd);
//      sim_activate(uptr, 19*cnt+23);      /* wait for a while */
//      sim_activate(uptr, 31*cnt+47);      /* wait for a while */
/*719*/ sim_activate(uptr, 41*cnt+47);      /* wait for a while */
//719   sim_activate(uptr, 81*cnt+87);      /* wait for a while */
    }
    return SCPE_OK;
}

/* Handle input transfers for console */
t_stat con_srvi(UNIT *uptr) {
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         unit = (uptr - con_unit);       /* unit 0 is read, unit 1 is write */
    int         cmd = uptr->CMD & CON_MSK;
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */
    uint8       ch;
    t_stat      r;

//  sim_clock_coschedule(uptr, tmxr_poll);      /* keep polling the input */
    sim_clock_coschedule(uptr, 10000);          /* keep polling the input */

    sim_debug(DEBUG_CMD, &con_dev,
        "con_srvi enter CMD %08x chsa %04x cmd %02x incnt %02x u4 %02x\n",
        uptr->CMD, chsa, cmd, con_data[unit].incnt, uptr->u4);

    /* if output tried to input device, error */
    if ((cmd == CON_RWD) || (cmd == CON_WR) || (cmd == 0x0C)) {  /* check for output */
        /* CON_RWD: 0x37 */                     /* TOF and write line */
        /* CON_WR:  0x01 */                     /* Write command */
        /* if input requested for output device, give error */
        if (unit == 0) {
            uptr->SNS |= SNS_CMDREJ;            /* command rejected */
            uptr->CMD &= LMASK;                 /* nothing left, command complete */
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvi Write to input device CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);
            chan_end(chsa, SNS_CHNEND|SNS_UNITCHK);    /* unit check */
            // fall thru return SCPE_OK;
        }
    }

    if ((cmd == CON_NOP) || (cmd == CON_INCH2)) { /* NOP is do nothing */
        uptr->CMD &= LMASK;                     /* nothing left, command complete */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_srvi INCH/NOP unit %02x: CMD %08x cmd %02x incnt %02x u4 %02x\n",
            unit, uptr->CMD, cmd, con_data[unit].incnt, uptr->u4);
        if (cmd == CON_INCH2) {                 /* Channel end only for INCH */
            int len = chp->ccw_count;           /* INCH command count */
            uint32 mema = chp->ccw_addr;        /* get inch or buffer addr */
            //FIXME add code to test return from set_inch
            set_inch(uptr, mema);               /* new address */

            con_data[unit].incnt = 0;           /* buffer empty */
            uptr->u4 = 0;                       /* no I/O yet */
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvi INCH CMD %08x chsa %04x len %02x inch %06x\n", uptr->CMD, chsa, len, mema);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
        } else {
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvi NOP CMD %08x chsa %04x cmd = %02x\n", uptr->CMD, chsa, cmd);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* NOP done */
        }
        /* drop through to poll input */
    }

    switch (cmd) {

    case CON_RD:        /* 0x02 */              /* read from device */
    case CON_ECHO:      /* 0x0a */              /* read from device w/ECHO */

        if ((uptr->u4 != con_data[unit].incnt) ||  /* input empty */
            (uptr->CMD & CON_INPUT)) {          /* input waiting? */
            ch = con_data[unit].ibuff[uptr->u4]; /* get char from read buffer */
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvi readbuf unit %02x: CMD %08x read %02x incnt %02x u4 %02x len %02x\n",
                unit, uptr->CMD, ch, con_data[unit].incnt, uptr->u4, chp->ccw_count);

            /* process any characters */
            if (uptr->u4 != con_data[unit].incnt) { /* input available */
                ch = con_data[unit].ibuff[uptr->u4];    /* get char from read buffer */
                if (chan_write_byte(chsa, &ch)) {   /* write byte to memory */
                    /* write error */
                    cmd = 0;                    /* no cmd now */
                    sim_debug(DEBUG_CMD, &con_dev,
                        "con_srvi write error unit %02x: CMD %08x read %02x u4 %02x ccw_count %02x\n",
                        unit, uptr->CMD, ch, uptr->u4, chp->ccw_count);
                    uptr->CMD &= LMASK;         /* nothing left, command complete */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                    break;
                }
                sim_debug(DEBUG_CMD, &con_dev,
                    "con_srvi write to mem unit %02x: CMD %08x read %02x u4 %02x incnt %02x\n",
                    unit, uptr->CMD, ch, uptr->u4, con_data[unit].incnt);

                /* character accepted, bump buffer pointer */
                uptr->u4++;                     /* next char position */
                /* see if at end of buffer */
                if (uptr->u4 >= sizeof(con_data[unit].ibuff))
                    uptr->u4 = 0;               /* reset pointer */

                /* user want more data? */
                if ((test_write_byte_end(chsa)) == 0) {
                    sim_debug(DEBUG_CMD, &con_dev,
                        "con_srvi need more unit %02x CMD %08x u4 %02x incnt %02x ccw_count %02x\n",
                        unit, uptr->CMD, uptr->u4, con_data[unit].incnt, chp->ccw_count);
                    /* user wants more, look next time */
                    if (uptr->u4 == con_data[unit].incnt) { /* input empty */
                        uptr->CMD &= ~CON_INPUT;    /* no input available */
                    }
                    break;
                }
                /* command is completed */
                cmd = 0;                        /* no cmd now */
                sim_debug(DEBUG_CMD, &con_dev,
                    "con_srvi read done unit %02x: CMD %08x read %02x u4 %02x incnt %02x ccw_count %02x\n",
                    unit, uptr->CMD, ch, uptr->u4, con_data[unit].incnt, chp->ccw_count);
                uptr->CMD &= LMASK;             /* nothing left, command complete */
                if (uptr->u4 != con_data[unit].incnt) { /* input empty */
                    uptr->CMD |= CON_INPUT;     /* input still available */
                }
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                break;
            }
        }
        break;
    }

    /* check for next input if reading or @@A sequence */
    r = sim_poll_kbd();                         /* poll for a char */
    if (r & SCPE_KFLAG) {                       /* got a char */
        ch = r & 0xff;                          /* drop any extra bits */
        if ((uptr->CMD & CON_INPUT) == 0) {     /* looking for input? */
            atbuf = 0;                          /* reset attention buffer */
            uptr->CMD &= ~CON_ATAT;             /* no @@A input */
            if (ch == '@') {                    /* maybe for console int */
                atbuf = (ch)<<8;                /* start anew */
                uptr->CMD |= CON_ATAT;          /* show getting @ */
            }
            if (ch == '\n')                     /* convert newline */
                ch = '\r';                      /* make newline into carriage return */ 
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvi handle readch unit %02x: CMD %08x read %02x u4 %02x incnt %02x\n",
                unit, uptr->CMD, ch, uptr->u4, con_data[unit].incnt);

            if (uptr->CMD & CON_EKO)            /* ECHO requested */
                sim_putchar(ch);                /* ECHO the char */

            /* put char in buffer */
            con_data[unit].ibuff[con_data[unit].incnt++] = ch;

            /* see if count at max, if so reset to start */
            if (con_data[unit].incnt >= sizeof(con_data[unit].ibuff))
                con_data[unit].incnt = 0;       /* reset buffer cnt */

            uptr->CMD |= CON_INPUT;             /* we have a char available */
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvi readch unit %02x: CMD %08x read %02x u4 %02x incnt %02x\n",
                unit, uptr->CMD, ch, uptr->u4, con_data[unit].incnt);
//          return sim_activate (uptr, 30);     /* come back real soon */
            return SCPE_OK;
        }
        /* not looking for input, look for attn or wakeup */
        if (ch == '?') {
            /* set ring bit? */
            set_devwake(chsa, SNS_ATTN|SNS_DEVEND|SNS_CHNEND);  /* tell user */
        }     
        /* not wanting input, but we have a char, look for @@A */
        if (uptr->CMD & CON_ATAT) {            /* looking for @@A */
            /* we have at least one @, look for another */
            if (ch == '@' || ch == 'A' || ch == 'a') {
                uint8 cc = ch;
                if (cc == 'a')
                    cc = 'A';                   /* make uppercase */
                sim_putchar(ch);                /* ECHO the char */
                atbuf = (atbuf|cc)<<8;          /* merge new char */
                if (atbuf == 0x40404100) {
                    attention_trap = CONSOLEATN_TRAP;   /* console attn (0xb4) */
                    atbuf = 0;                  /* reset attention buffer */
                    uptr->CMD &= ~CON_ATAT;     /* no @@A input */
                    sim_putchar('\r');          /* return char */
                    sim_putchar('\n');          /* line feed char */
                    sim_debug(DEBUG_CMD, &con_dev,
                        "con_srvi unit %02x: CMD %08x read @@A Console Trap\n", unit, uptr->CMD);
                    uptr->u4 = 0;               /* no input count */
                    con_data[unit].incnt = 0;   /* no input data */
                }
                return SCPE_OK;
            }
            /* char not for us, so keep looking */
            atbuf = 0;                          /* reset attention buffer */
            uptr->CMD &= ~CON_ATAT;             /* no @@A input */
        }
        /* not looking for input, look for attn or wakeup */
        if (ch == '@') {
            atbuf = (atbuf|ch)<<8;              /* merge in char */
            uptr->CMD |= CON_ATAT;              /* show getting @ */
            sim_putchar(ch);                    /* ECHO the char */
        }
        /* assume it is for next read request, so save it */
        /* see if count at max, if so reset to start */
        if (con_data[unit].incnt >= sizeof(con_data[unit].ibuff))
            con_data[unit].incnt = 0;           /* reset buffer cnt */

        if (uptr->CMD & CON_EKO)                /* ECHO requested */
            sim_putchar(ch);                    /* ECHO the char */

        /* put char in buffer */
        con_data[unit].ibuff[con_data[unit].incnt++] = ch;

        uptr->CMD |= CON_INPUT;                 /* we have a char available */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_srvi readch2 unit %02x: CMD %08x read %02x u4 %02x incnt %02x\n",
            unit, uptr->CMD, ch, uptr->u4, con_data[unit].incnt);
    }
    return SCPE_OK;
}

t_stat  con_reset(DEVICE *dptr) {
    tmxr_set_console_units (&con_unit[0], &con_unit[1]);
    return SCPE_OK;
}

/* Handle haltio transfers for console */
uint16  con_haltio(UNIT *uptr) {
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         cmd = uptr->CMD & CON_MSK;
    int         unit = (uptr - con_unit);       /* unit # 0 is read, 1 is write */
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    sim_debug(DEBUG_EXP, &con_dev, "con_haltio enter chsa %04x cmd = %02x\n", chsa, cmd);

    /* terminate any input command */
    if ((uptr->CMD & CON_MSK) != 0) {       /* is unit busy */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_haltio HIO chsa %04x cmd = %02x ccw_count %02x\n", chsa, cmd, chp->ccw_count);
        // stop any I/O and post status and return error status */
        chp->chan_byte = BUFF_EMPTY;        /* there is no data to read/store */
        chp->ccw_flags = 0;                 /* stop any chaining */
        uptr->CMD &= LMASK;                 /* make non-busy */
        uptr->u4 = 0;                       /* no I/O yet */
        con_data[unit].incnt = 0;           /* no input data */
        uptr->SNS = SNS_RDY|SNS_ONLN;       /* status is online & ready */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_haltio HIO I/O stop chsa %04x cmd = %02x\n", chsa, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);  /* force error */
        return SCPE_IOERR;
    }
    uptr->u4 = 0;                           /* no I/O yet */
    con_data[unit].incnt = 0;               /* no input data */
    uptr->CMD &= LMASK;                     /* make non-busy */
    uptr->SNS = SNS_RDY|SNS_ONLN;           /* status is online & ready */
    return SCPE_OK;                         /* not busy */
}
#endif

