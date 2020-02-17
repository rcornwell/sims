/* sel32_con.c: SEL 32 Class F IOP processor console.

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

   This is the standard console interface.  It is subchannel of the IOP 7e00.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as ASCII characters.

   Change History:
   12/10/2018 - force input chars to upper case if lower case
   07/18/2019 - generate interrupt for INCH/NOP commands for UTX

*/

#include "sel32_defs.h"
#include "sim_tmxr.h"

#ifdef NUM_DEVS_CON

extern  t_stat  set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern  t_stat  show_dev_addr(FILE *st, UNIT * uptr, int32 v, CONST void *desc);
extern  void    chan_end(uint16 chan, uint8 flags);
extern  int     chan_read_byte(uint16 chan, uint8 *data);
extern  int     chan_write_byte(uint16 chan, uint8 *data);
extern  void    set_devattn(uint16 addr, uint8 flags);
extern  void    post_extirq(void);
extern  uint32  attention_trap;             /* set when trap is requested */
extern  void    set_devwake(uint16 addr, uint8 flags);
extern  DEVICE *get_dev(UNIT *uptr);
extern  t_stat  set_inch(UNIT *uptr, uint32 inch_addr); /* set channel inch address */
extern  CHANP  *find_chanp_ptr(uint16 chsa);             /* find chanp pointer */

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

/* Input buffer pointer held in u4 */

#define SNS     u5
/* in u5 packs sense byte 0,1 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ  0x80000000    /* Command reject */
#define SNS_INTVENT 0x40000000    /* Unit intervention required */
/* sense byte 3 */
#define SNS_RDY     0x80        /* device ready */
#define SNS_ONLN    0x40        /* device online */
//#define SNS_DSR       0x04        /* data set ready */
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

/* forward definitions */
uint8   con_preio(UNIT *uptr, uint16 chan);
uint8   con_startcmd(UNIT*, uint16, uint8);
void    con_ini(UNIT*, t_bool);
t_stat  con_srvi(UNIT*);
t_stat  con_srvo(UNIT*);
uint8   con_haltio(UNIT *);
t_stat  con_poll(UNIT *);
t_stat  con_reset(DEVICE *);

/* channel program information */
CHANP           con_chp[NUM_UNITS_CON] = {0};

MTAB    con_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr, &show_dev_addr, NULL},
    {0}
};

UNIT            con_unit[] = {
    {UDATA(&con_srvi, UNIT_IDLE, 0), 0, UNIT_ADDR(0x7EFC)},   /* Input */
    {UDATA(&con_srvo, UNIT_IDLE, 0), 0, UNIT_ADDR(0x7EFD)},   /* Output */
};

//DIB               con_dib = {NULL, con_startcmd, NULL, NULL, NULL, con_ini, con_unit, con_chp, NUM_UNITS_CON, 0xf, 0x7e00, 0, 0, 0};
DIB             con_dib = {
    con_preio,      /* uint8 (*pre_io)(UNIT *uptr, uint16 chan)*/       /* Start I/O */
    con_startcmd,   /* uint8 (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start a command */
    con_haltio,     /* uint8 (*halt_io)(UNIT *uptr) */          /* Stop I/O */
    NULL,           /* uint8 (*test_io)(UNIT *uptr) */          /* Test I/O */
    NULL,           /* uint8 (*post_io)(UNIT *uptr) */          /* Post I/O */
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
    &con_dib, DEV_UADDR|DEV_DISABLE|DEV_DEBUG, 0, dev_debug
};

/*
 * Console print routines.
 */
/* initialize the console chan/unit */
void con_ini(UNIT *uptr, t_bool f) {
    int     unit = (uptr - con_unit);   /* unit 0 */
//  DEVICE *dptr = get_dev(uptr);

    uptr->u4 = 0;                       /* no input cpunt */
    con_data[unit].incnt = 0;           /* no input data */
//  con_data[0].incnt = 0;              /* no input data */
//  con_data[1].incnt = 0;              /* no output data */
    uptr->SNS = SNS_RDY|SNS_ONLN;       /* status is online & ready */
    sim_activate(uptr, 1000);           /* time increment */
}

/* start a console operation */
uint8  con_preio(UNIT *uptr, uint16 chan)
{
    DEVICE         *dptr = get_dev(uptr);
    int            unit = (uptr - dptr->units);

    if ((uptr->CMD & 0xff00) != 0) {    /* just return if busy */
        sim_debug(DEBUG_CMD, &con_dev, "con_preio unit=%02x BUSY\n", unit);
        return SNS_BSY;
    }

    sim_debug(DEBUG_CMD, &con_dev, "con_preio unit=%02x OK\n", unit);
    return 0;                           /* good to go */
}

/* start an I/O operation */
uint8  con_startcmd(UNIT *uptr, uint16 chan, uint8 cmd) {
    int     unit = (uptr - con_unit);   /* unit 0,1 */
    uint8   ch;

    if ((uptr->CMD & CON_MSK) != 0)     /* is unit busy */
        return SNS_BSY;                 /* yes, return busy */

    sim_debug(DEBUG_CMD, &con_dev,
        "con_startcmd unit %01x chan %02x cmd %02x enter\n", unit, chan, cmd);
    /* process the commands */
    switch (cmd & 0xFF) {
    case CON_INCH:      /* 0x00 */      /* INCH command */
        sim_debug(DEBUG_CMD, &con_dev, "con_startcmd %04x: Cmd INCH\n", chan);
        uptr->CMD &= LMASK;             /* leave only chsa */
        uptr->CMD |= CON_INCH2;         /* save INCH command as 0xf0 */
        uptr->SNS = SNS_RDY|SNS_ONLN;   /* status is online & ready */
        sim_activate(uptr, 10);         /* start us off */
        return 0;                       /* no status change */
        break;

    case CON_RWD:       /* 0x37 */      /* TOF and write line */
    case CON_WR:        /* 0x01 */      /* Write command */
        /* if input requested for output device, give error */
        uptr->CMD &= LMASK;             /* leave only chsa */
        uptr->CMD |= (cmd & CON_MSK);   /* save command */
        uptr->SNS = SNS_RDY|SNS_ONLN;   /* status is online & ready */
        sim_activate(uptr, 20);         /* start us off */
//TRIED sim_activate(uptr, 10);         /* start us off */
        return 0;                       /* no status change */
        break;

    case CON_RD:        /* 0x02 */      /* Read command */
    case CON_ECHO:      /* 0x0a */      /* Read command w/ECHO */
        /* if output requested for input device, give error */
        uptr->CMD &= LMASK;             /* leave only chsa */
        uptr->CMD |= (cmd & CON_MSK);   /* save command */
        if (cmd == CON_ECHO)            /* echo command? */
            uptr->CMD |= CON_EKO;       /* save echo status */
        uptr->CMD |= CON_READ;          /* show read mode */
        atbuf = 0;                      /* reset attention buffer */
        uptr->u4 = 0;                   /* no I/O yet */
        con_data[unit].incnt = 0;       /* clear any input data */
        uptr->SNS = SNS_RDY|SNS_ONLN;   /* status is online & ready */
        sim_activate(uptr, 20);         /* start us off */
        return 0;
        break;

    case CON_NOP:       /* 0x03 */      /* NOP has do nothing */
        uptr->SNS = SNS_RDY|SNS_ONLN;   /* status is online & ready */
        uptr->CMD &= LMASK;             /* leave only chsa */
        uptr->CMD |= (cmd & CON_MSK);   /* save command */
//      uptr->u4 = 0;                   /* no I/O yet */
//      con_data[unit].incnt = 0;       /* clear any input data */
        sim_activate(uptr, 10);         /* start us off */
        return 0;                       /* no status change */
        break;

#ifndef JUNK
    case 0x0C:          /* 0x0C */      /* Unknown command */
        uptr->SNS = SNS_RDY|SNS_ONLN;   /* status is online & ready */
        uptr->CMD &= LMASK;             /* leave only chsa */
        uptr->CMD |= (cmd & CON_MSK);   /* save command */
        sim_activate(uptr, 10);         /* start us off */
        return 0;                       /* no status change */
        break;
#endif

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
        uptr->SNS |= SNS_CMDREJ;        /* command rejected */
//      uptr->u4 = 0;                   /* no I/O yet */
//      con_data[unit].incnt = 0;       /* clear any input data */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_startcmd %04x: Invalid command %02x Sense %02x\n",
            chan, cmd, uptr->SNS);
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* unit check */
        break;
    }

    if (uptr->SNS & (~(SNS_RDY|SNS_ONLN|SNS_DSR)))
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle output transfers for console */
t_stat con_srvo(UNIT *uptr) {
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         unit = (uptr - con_unit);       /* unit 0 is read, unit 1 is write */
    int         cmd = uptr->CMD & CON_MSK;
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */
    uint8       ch, cp;

    sim_debug(DEBUG_DETAIL, &con_dev, "con_srvo enter chsa %04x cmd = %02x\n", chsa, cmd);

    /* if input tried from output device, error */
    if ((cmd == CON_RD) || (cmd == CON_ECHO) || (cmd == 0xC0)) {  /* check for output */
        /* CON_RD:      0x02 */      /* Read command */
        /* CON_ECHO:    0x0a */      /* Read command w/ECHO */
        /* if input requested for output device, give error */
        if (unit == 1) {
            uptr->SNS |= SNS_CMDREJ;            /* command rejected */
            uptr->CMD &= LMASK;                 /* nothing left, command complete */
//DIAG_TUE  chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);    /* unit check */
            chan_end(chsa, SNS_CHNEND|SNS_UNITCHK);    /* unit check */
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvo Read to output device chsa %04x cmd = %02x\n", chsa, cmd);
            return SCPE_OK;
        }
    }

    if ((cmd == CON_NOP) || (cmd == CON_INCH2)) {   /* NOP has to do nothing */
        uptr->CMD &= LMASK;                     /* nothing left, command complete */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_srvo INCH/NOP unit %02x: cmd %02x incnt %02x u4 %02x\n",
            unit, cmd, con_data[unit].incnt, uptr->u4);
        if (cmd == CON_INCH2) {                 /* Channel end only for INCH */
            int len = chp->ccw_count;           /* INCH command count */
            uint32 mema = chp->ccw_addr;        /* get inch or buffer addr */
//          int i = set_inch(uptr, mema);       /* new address */
            set_inch(uptr, mema);               /* new address */

            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvo INCH chsa %04x len %02x inch %06x\n", chsa, len, mema);
            chan_end(chsa, SNS_CHNEND);         /* INCH done */
        } else {
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvo NOP chsa %04x cmd = %02x\n", chsa, cmd);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
        }
        return SCPE_OK;
    }

    if ((cmd == CON_WR) || (cmd == CON_RWD)) {
        /* Write to device */
        if (chan_read_byte(chsa, &ch)) {    /* get byte from memory */
            uptr->CMD &= LMASK;             /* nothing left, command complete */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvo write %02x chsa %04x cmd %02x complete\n",
                ch, chsa, cmd);
        } else {
            /* HACK HACK HACK */
            ch &= 0x7f;                     /* make 7 bit w/o parity */
            /* simh stops outputting chars to debug file if it is passed a null????? */
            if (ch == 0)                    /* do not pass a null char */
//WAS           ch = '@';                   /* stop simh abort .... */
                ch = ' ';                   /* stop simh abort .... */
            if (((ch >= 0x20) && (ch <= 0x7e)) || (ch == '\r') || (ch == '\n'))
                cp = ch;
            else
                cp = '^';
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvo write %01x: putch 0x%02x %c\n", unit, ch, cp);
//WAS       sim_putchar(ch);                /* output next char to device */
            sim_putchar(cp);                /* output next char to device */
            sim_activate(uptr, 20);         /* keep going */
        }
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

    sim_debug(DEBUG_DETAIL, &con_dev, "con_srvi enter chsa %04x cmd = %02x\n", chsa, cmd);

    /* if output tried to input device, error */
    if ((cmd == CON_RWD) || (cmd == CON_WR) || (cmd == 0x0C)) {  /* check for output */
        /* CON_RWD: 0x37 */      /* TOF and write line */
        /* CON_WR:  0x01 */      /* Write command */
        /* if input requested for output device, give error */
        if (unit == 0) {
            uptr->SNS |= SNS_CMDREJ;            /* command rejected */
            uptr->CMD &= LMASK;                 /* nothing left, command complete */
//DIAGTUE   chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);    /* unit check */
            chan_end(chsa, SNS_CHNEND|SNS_UNITCHK);    /* unit check */
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvi Write to input device chsa %04x cmd = %02x\n", chsa, cmd);
//fall thru return SCPE_OK;
        }
    }
#ifdef JUNK
    if (cmd == 0x0C) {                          /* unknown has to do nothing */
        sim_debug(DEBUG_CMD, &con_dev, "con_srvi Unknown (0x0C) chsa %04x cmd = %02x\n", chsa, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
        return SCPE_OK;
    }
#endif

    if ((cmd == CON_NOP) || (cmd == CON_INCH2)) { /* NOP is do nothing */
        uptr->CMD &= LMASK;                     /* nothing left, command complete */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_srvi INCH/NOP unit %02x: cmd %02x incnt %02x u4 %02x\n",
            unit, cmd, con_data[unit].incnt, uptr->u4);
        if (cmd == CON_INCH2) {                 /* Channel end only for INCH */
            int len = chp->ccw_count;           /* INCH command count */
            uint32 mema = chp->ccw_addr;        /* get inch or buffer addr */
//          int i = set_inch(uptr, mema);       /* new address */
            set_inch(uptr, mema);               /* new address */

            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvi INCH chsa %04x len %02x inch %06x\n", chsa, len, mema);
            chan_end(chsa, SNS_CHNEND);         /* INCH done */
//          chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
        } else {
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvi NOP chsa %04x cmd = %02x\n", chsa, cmd);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* NOP done */
        }
        /* drop through to poll input */
    }

    switch (cmd) {

    case CON_RD:        /* 0x02 */              /* read from device */
    case CON_ECHO:      /* 0x0a */              /* read from device w/ECHO */
        if (uptr->CMD & CON_INPUT) {            /* input waiting? */
            int len = chp->ccw_count;           /* get command count */
            ch = con_data[unit].ibuff[uptr->u4++];  /* get char from read buffer */
            sim_debug(DEBUG_CMD, &con_dev,
                "con_srvi unit %02x: read %02x incnt %02x u4 %02x len %02x\n",
                unit, ch, con_data[unit].incnt, uptr->u4, len);

            if (chan_write_byte(chsa, &ch)) {   /* write byte to memory */
                con_data[unit].incnt = 0;       /* buffer empty */
                cmd = 0;                        /* no cmd either */
//              uptr->u4 = 0;                   /* no I/O yet */
                uptr->CMD &= LMASK;             /* nothing left, command complete */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
            } else {
//              len = chp->ccw_count;           /* INCH command count */
//              if ((len==0) && uptr->u4 == con_data[unit].incnt) { /* read completed */
                if (uptr->u4 == con_data[unit].incnt) { /* read completed */
                    con_data[unit].incnt = 0;   /* buffer is empty */
                    cmd = 0;                    /* no cmd either */
//                  uptr->u4 = 0;               /* no I/O yet */
                    uptr->CMD &= LMASK;         /* nothing left, command complete */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                }
            }
        }
//  default:
        break;
    }

    /* check for next input if reading or @@A sequence */
    r = sim_poll_kbd();                         /* poll for a char */
    if (r & SCPE_KFLAG) {                       /* got a char */
        ch = r & 0377;                          /* drop any extra bits */
#ifdef LEAVE_LOWER
        if ((ch >= 'a') && (ch <= 'z'))
            ch &= 0xdf;                         /* make upper case */
#endif
        if ((cmd == CON_RD) || (cmd == CON_ECHO)) { /* looking for input? */
            atbuf = 0;                          /* reset attention buffer */
            if (ch == '\n')                     /* convert newline */
                ch = '\r';                      /* make newline into carriage return */ 
            /* Handle end of buffer */
            switch (ch) {
#ifdef ONE_AT_A_TIME
            case 0x7f:                          /* Delete */
            case '\b':                          /* backspace 0x08 */
                if (con_data[unit].incnt != 0) {
                    con_data[unit].incnt--;
                    sim_putchar('\b');
                    sim_putchar(' ');
                    sim_putchar('\b');
                }
                break;
            case 03:                            /* ^C */
            case 025:                           /* ^U clear line */
                for (i = con_data[unit].incnt; i > 0; i--) {
                    sim_putchar('\b');
                    sim_putchar(' ');
                    sim_putchar('\b');
                }
                con_data[unit].incnt = 0;
                break;
#endif
            case '\r':                          /* return */
            case '\n':                          /* newline */
                uptr->CMD |= CON_CR;            /* C/R received */
                /* fall through */
            default:
                if (con_data[unit].incnt < sizeof(con_data[unit].ibuff)) {
                    if (uptr->CMD & CON_EKO)    /* ECHO requested */
                        sim_putchar(ch);        /* ECHO the char */
                    con_data[unit].ibuff[con_data[unit].incnt++] = ch;
                    uptr->CMD |= CON_INPUT;     /* we have a char available */
                }
                break;
            }
        } else {
            /* look for attention sequence '@@A' */
            if (ch == '@' || ch == 'A' || ch == 'a') {
                if (ch == 'a')
                    ch = 'A';
                atbuf = (atbuf|ch)<<8;
                sim_putchar(ch);                /* ECHO the char */
                if (atbuf == 0x40404100) {
                    attention_trap = CONSOLEATN_TRAP;   /* console attn (0xb4) */
                    atbuf = 0;                  /* reset attention buffer */
                    sim_putchar('\r');          /* return char */
                    sim_putchar('\n');          /* line feed char */
                    sim_debug(DEBUG_CMD, &con_dev,
                        "con_srvi unit %02x: read @@A Console Trap\n", unit);
                }
            } else {
                if (ch == '?') {
//                  int chan = ((chsa >> 8) & 0x7f);    /* get the channel number */
                    /* set ring bit? */
                    set_devwake(chsa, SNS_ATTN|SNS_DEVEND|SNS_CHNEND);  /* tell user */
                }
            }
        }
    }
    if ((r & SCPE_KFLAG) &&                     /* got something and */
        ((cmd == CON_RD) || (cmd == CON_ECHO))) /* looking for input */
//WAS   return sim_activate (uptr, 20);
        return sim_activate (uptr, 100);
    return sim_activate (uptr, 500);
//WAS  return tmxr_clock_coschedule_tmr (uptr, TMR_RTC, 1);    /* come back soon */
}

t_stat  con_reset(DEVICE *dptr) {
    tmxr_set_console_units (&con_unit[0], &con_unit[1]);
    return SCPE_OK;
}

/* Handle haltio transfers for console */
uint8   con_haltio(UNIT *uptr) {
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         cmd = uptr->CMD & CON_MSK;
    int         unit = (uptr - con_unit);       /* unit # 0 is read, 1 is write */
    uint8       ch;

    sim_debug(DEBUG_EXP, &con_dev, "con_haltio enter chsa %04x cmd = %02x\n", chsa, cmd);

#ifdef DO_DYNAMIC_DEBUG
    cpu_dev.dctrl |= (DEBUG_INST | DEBUG_CMD | DEBUG_EXP | DEBUG_IRQ);
    con_dev.dctrl |= (DEBUG_CMD | DEBUG_EXP | DEBUG_DETAIL);
#endif
    /* terminate any input command */
    if ((uptr->CMD & CON_MSK) != 0) {           /* is unit busy */
        sim_debug(DEBUG_CMD, &con_dev,
            "con_haltio HIO chsa %04x cmd = %02x\n", chsa, cmd);
        if (unit == 0) {
            if (chan_write_byte(chsa, &ch)) {   /* write byte to memory */
                con_data[unit].incnt = 0;       /* buffer empty */
                uptr->CMD &= LMASK;             /* nothing left, command complete */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                sim_debug(DEBUG_CMD, &con_dev,
                    "con_haltio HIO I/O stop chsa %04x cmd = %02x\n", chsa, cmd);
//              return SCPE_OK;                 /* not busy anymore */
                return SCPE_IOERR;
            }
        } else {
            if (chan_read_byte(chsa, &ch)) {    /* get byte from memory */
                uptr->CMD &= LMASK;             /* nothing left, command complete */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
                sim_debug(DEBUG_CMD, &con_dev,
                    "con_haltio HIO I/O stop chsa %04x cmd = %02x\n", chsa, cmd);
 //             return SCPE_OK;                 /* not busy anymore */
                return SCPE_IOERR;
            }
        }
        uptr->CMD &= LMASK;                     /* make non-busy */
        uptr->SNS = SNS_RDY|SNS_ONLN;           /* status is online & ready */
           return SCPE_OK;                         /* not busy */
//no work   chan_end(chsa, SNS_CHNEND|SNS_UNITCHK); /* write terminated */
//          chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);  /* write terminated */
//      chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);  /* done bit 15 */ /* bad status */
//      return SCPE_IOERR;
    }
    return SCPE_OK;                             /* not busy */
}
#endif

