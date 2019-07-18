/* sel32_con.c: SEL 32 Class F IOP processor console.

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

   This is the standard console interface.  It is subchannel of the IOP 7e00.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as ASCII characters.

*/

#include "sel32_defs.h"
#include "sim_defs.h"

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

/* Held in u3 is the device command and status */
#define CON_INCH    0x00    /* Initialize channel command */
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
    uint8       ibuff[145];     /* Input line buffer */
    uint8       incnt;          /* char count */
}
con_data[NUM_UNITS_CON];

uint32  atbuf=0;                /* attention buffer */

/* forward definitions */
uint8 con_startcmd(UNIT *, uint16,  uint8);
void    con_ini(UNIT *, t_bool);
t_stat  con_srvi(UNIT *);
t_stat  con_srvo(UNIT *);
t_stat  con_reset(DEVICE *);
t_stat  con_attach(UNIT *, char *);
t_stat  con_detach(UNIT *);

/* channel program information */
CHANP           con_chp[NUM_UNITS_CON] = {0};

MTAB    con_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr, &show_dev_addr, NULL},
    {0}
};

UNIT            con_unit[] = {
    {UDATA(con_srvi, UNIT_ATT, 0), 0, UNIT_ADDR(0x7EFC)},   /* Input */
    {UDATA(con_srvo, UNIT_ATT, 0), 0, UNIT_ADDR(0x7EFD)},   /* Output */
};

//DIB               con_dib = {NULL, con_startcmd, NULL, NULL, NULL, con_ini, con_unit, con_chp, NUM_UNITS_CON, 0xf, 0x7e00, 0, 0, 0};
DIB             con_dib = {
    NULL,           /* uint8 (*pre_io)(UNIT *uptr, uint16 chan)*/       /* Start I/O */
    con_startcmd,   /* uint8 (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start a command */
    NULL,           /* uint8 (*halt_io)(UNIT *uptr) */          /* Stop I/O */
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
    0,              /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE  con_dev = {
    "CON", con_unit, NULL, con_mod,
    NUM_UNITS_CON, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &con_dib, DEV_UADDR|DEV_DISABLE|DEV_DEBUG, 0, dev_debug
};

/*
 * Console print routines.
 */
/* initialize the console chan/unit */
void con_ini(UNIT *uptr, t_bool f) {
    int     unit = (uptr - con_unit);   /* unit 0 */
    DEVICE *dptr = find_dev_from_unit(uptr);

    con_data[unit].incnt = 0;   /* no input data */
    uptr->u5 = SNS_RDY|SNS_ONLN;    /* status is online & ready */
    sim_activate(uptr, 1000);   /* time increment */
}

/* start an I/O operation */
uint8  con_startcmd(UNIT *uptr, uint16 chan, uint8 cmd) {
    int     unit = (uptr - con_unit);   /* unit 0,1 */
    uint8   ch;

    if ((uptr->u3 & CON_MSK) != 0)  /* is unit busy */
        return SNS_BSY;             /* yes, return busy */

    /* process the commands */
    switch (cmd & 0xFF) {
    case CON_INCH:      /* 0x00 */      /* INCH command */
        sim_debug(DEBUG_CMD, &con_dev, "con_startcmd %x: Cmd INCH\n", chan);
        return SNS_CHNEND|SNS_DEVEND;   /* all is well */
        break;

    case CON_RWD:       /* 0x37 */      /* TOF and write line */
    case CON_WR:        /* 0x01 */      /* Write command */
        /* if input requested for output device, give error */
        if (unit == 0) {
            uptr->u5 |= SNS_CMDREJ;     /* command rejected */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* unit check */
        }
        uptr->u3 &= LMASK;              /* leave only chsa */
        uptr->u3 |= (cmd & CON_MSK);    /* save command */
        uptr->u5 = SNS_RDY|SNS_ONLN;    /* status is online & ready */
        sim_activate(uptr, 20);         /* TRY 06-09-18 */
        return 0;                       /* no status change */
        break;

    case CON_RD:                        /* Read command */
    case CON_ECHO:                      /* Read command w/ECHO */
        /* if output requested for input device, give error */
        if (unit == 1) {
            uptr->u5 |= SNS_CMDREJ;     /* command rejected */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* unit check */
        }
        uptr->u3 &= LMASK;              /* leave only chsa */
        uptr->u3 |= (cmd & CON_MSK);    /* save command */
        if (cmd == CON_ECHO)            /* echo command? */
            uptr->u3 |= CON_EKO;        /* save echo status */
        uptr->u3 |= CON_READ;           /* show read mode */
        atbuf = 0;                      /* reset attention buffer */
        uptr->u4 = 0;                   /* no I/O yet */
        uptr->u5 = SNS_RDY|SNS_ONLN;    /* status is online & ready */
        return 0;
        break;

    case CON_NOP:                       /* NOP has do nothing */
        uptr->u5 = SNS_RDY|SNS_ONLN;    /* status is online & ready */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case CON_CON:                       /* Connect, return Data Set ready */
        sim_debug(DEBUG_CMD, &con_dev, "con_startcmd %x: Cmd %x NOP\n", chan, cmd);
        uptr->u5 |= (SNS_DSR|SNS_DCD);  /* Data set ready, Data Carrier detected */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case CON_DIS:                       /* NOP has do nothing */
        sim_debug(DEBUG_CMD, &con_dev, "con_startcmd %x: Cmd %x NOP\n", chan, cmd);
        uptr->u5 &= ~(SNS_DSR|SNS_DCD); /* Data set not ready */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case CON_SNS:                       /* Sense */
        sim_debug(DEBUG_CMD, &con_dev, "con_startcmd %x: Cmd Sense %02x\n", chan, uptr->u5);
        /* value 4 is Data Set Ready */
        /* value 5 is Data carrier detected n/u */
        ch = uptr->u5;                  /* status */
        chan_write_byte(GET_UADDR(uptr->u3), &ch);  /* write status */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    default:                            /* invalid command */
        uptr->u5 |= SNS_CMDREJ;         /* command rejected */
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* unit check */
        break;
    }

    if (uptr->u5 & (~(SNS_RDY|SNS_ONLN|SNS_DSR)))
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle output transfers for console */
t_stat con_srvo(UNIT *uptr) {
    uint16      chsa = GET_UADDR(uptr->u3);
    int         unit = (uptr - con_unit);       /* unit 0 is read, unit 1 is write */
    int         cmd = uptr->u3 & CON_MSK;
    uint8       ch;

    if ((cmd == CON_WR) || (cmd == CON_RWD)) {
        /* Write to device */
        if (chan_read_byte(chsa, &ch)) {    /* get byte from memory */
            uptr->u3 &= LMASK;              /* nothing left, command complete */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
        } else {
            sim_debug(DEBUG_CMD, &con_dev, "con_srvo write %d: putch %0.2x %c\n", unit, ch, ch);
            sim_putchar(ch);            /* output next char to device */
            sim_activate(uptr, 20);     /* TRY 07-18-18 */
        }
    }
    return SCPE_OK;
}

/* Handle input transfers for console */
t_stat con_srvi(UNIT *uptr) {
    uint16      chsa = GET_UADDR(uptr->u3);
    int         unit = (uptr - con_unit);       /* unit 0 is read, unit 1 is write */
    int         cmd = uptr->u3 & CON_MSK;
    t_stat      r = SCPE_ARG;       /* Force error if not set */
    uint8       ch;
    int         i;

    switch (cmd) {

    case CON_RD:            /* read from device */
    case CON_ECHO:          /* read from device w/ECHO */
        if (uptr->u3 & CON_INPUT) {             /* input waiting? */
            ch = con_data[unit].ibuff[uptr->u4++];  /* get char from read buffer */
            sim_debug(DEBUG_CMD, &con_dev, "con_srvi %d: read %02x\n", unit, ch);
            if (chan_write_byte(chsa, &ch)) {   /* write byte to memory */
                con_data[unit].incnt = 0;       /* buffer empty */
                cmd = 0;                        /* no cmd either */
                uptr->u3 &= LMASK;              /* nothing left, command complete */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
            } else {
                if (uptr->u4 == con_data[unit].incnt) { /* read completed */
                    con_data[unit].incnt = 0;   /* buffer is empty */
                    cmd = 0;                    /* no cmd either */
                    uptr->u3 &= LMASK;          /* nothing left, command complete */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                }
            }
        }
        break;
    }

    /* poll for next input if reading or @@A sequence */
    r = sim_poll_kbd();         /* poll for ready */
    if (r & SCPE_KFLAG) {       /* got a char */
        ch = r & 0377;          /* drop any extra bits */
        if ((cmd == CON_RD) || (cmd == CON_ECHO)) {     /* looking for input */
            atbuf = 0;          /* reset attention buffer */
            if (ch == '\n')     /* convert newline */
                ch = '\r';      /* make newline into carriage return */ 
            /* Handle end of buffer */
            switch (ch) {

            case 0x7f:      /* Delete */
            case '\b':      /* backspace */
                if (con_data[unit].incnt != 0) {
                    con_data[unit].incnt--;
                    sim_putchar('\b');
                    sim_putchar(' ');
                    sim_putchar('\b');
                }
                break;
            case 03:  /* ^C */
            case 025: /* ^U clear line */
                for (i = con_data[unit].incnt; i> 0; i--) {
                    sim_putchar('\b');
                    sim_putchar(' ');
                    sim_putchar('\b');
                }
                con_data[unit].incnt = 0;
                break;

            case '\r':      /* return */
            case '\n':      /* newline */
                uptr->u3 |= CON_CR;         /* C/R received */
                /* fall through */
            default:
                if (con_data[unit].incnt < sizeof(con_data[unit].ibuff)) {
                    if (uptr->u3 & CON_EKO)     /* ECHO requested */
                        sim_putchar(ch);        /* ECHO the char */
                    con_data[unit].ibuff[con_data[unit].incnt++] = ch;
                    uptr->u3 |= CON_INPUT;      /* we have a char available */
                }
                break;
            }
        } else {
            /* look for attention sequence '@@A' */
            if (ch == '@' || ch == 'A' || ch == 'a') {
                if (ch == 'a')
                    ch = 'A';
                atbuf = (atbuf|ch)<<8;
                sim_putchar(ch);        /* ECHO the char */
                if (atbuf == 0x40404100) {
                    attention_trap = CONSOLEATN_TRAP;   /* console attn (0xb4) */
                    atbuf = 0;          /* reset attention buffer */
                    sim_putchar('\r');  /* return char */
                    sim_putchar('\n');  /* line feed char */
                }
            } else {
                if (ch == '?') {
                    int chan = ((chsa >> 8) & 0x7f);        /* get the channel number */
                    /* set ring bit? */
                    set_devwake(chsa, SNS_ATTN|SNS_DEVEND|SNS_CHNEND);  /* tell user */
                }
            }
        }
    }
    if ((cmd == CON_RD) || (cmd == CON_ECHO))       /* looking for input */
        sim_activate(uptr, 200);        /* keep going */
    else
        sim_activate(uptr, 400);        /* keep going */
    return SCPE_OK;
}

#endif

