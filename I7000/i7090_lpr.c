/* i7090_lpr.c: IBM 7090 Standard line printer.

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

   This is the standard line printer that all 70xx systems have.

   For WRS read next 24 words and fill print buffer.
      Row 9, 8, 7, 6, 5, 4, 3, 2, 1, 10, 11, 12
   For RDS read rows 9, 8, 7, 6, 5, 4, 3, 2, 1,
          Echo 8|4
          read row 10
          Echo 8|3
          read row 11
          Echo 9
          read row 12
          Echo 8, 7, 6, 5, 4, 3, 2, 1

*/

#include "i7090_defs.h"
#include "sim_console.h"
#include "sim_card.h"

#ifdef NUM_DEVS_LPR     

#define UNIT_LPR        UNIT_ATTABLE | UNIT_DISABLE
#define ECHO            (1 << UNIT_V_LOCAL)


/* std devices. data structures

   chan_dev     Channel device descriptor
   chan_unit    Channel unit descriptor
   chan_reg     Channel register list
   chan_mod     Channel modifiers list
*/

#define LPRSTA_READ     0x00000001      /* Unit is in read */
#define LPRSTA_WRITE    0x00000002      /* Unit is in write */
#define LPRSTA_ON       0x00000004      /* Unit is running */
#define LPRSTA_EOF      0x00000008      /* Hit end of file */
#define LPRSTA_EOR      0x00000010      /* Hit end of record */
#define LPRSTA_IDLE     0x00000020      /* Unit between operation */
#define LPRSTA_CMD      0x00000040      /* Unit has recieved a cmd */
#define LPRSTA_RCMD     0x00000080      /* Restart with read */
#define LPRSTA_WCMD     0x00000100      /* Restart with write */
#define LPRSTA_POSMASK  0x0007f000      /* Postion data */
#define LPRSTA_POSSHIFT 12
#define LPRSTA_BINMODE  0x00000200      /* Line printer started in bin mode */
#define LPRSTA_CHANGE   0x00000400      /* Turn DEV_WRITE on */
#define LPRSTA_COLMASK  0xff000000      /* Mask to last column printed */
#define LPRSTA_COLSHIFT 24

struct _lpr_data
{
    t_uint64            wbuff[24];      /* Line buffer */
    char                lbuff[144];     /* Output line buffer */
}
lpr_data[NUM_DEVS_LPR];

uint32              lpr_cmd(UNIT *, uint16, uint16);
t_stat              lpr_srv(UNIT *);
void                lpr_ini(UNIT *, t_bool);
t_stat              lpr_reset(DEVICE *);
t_stat              lpr_attach(UNIT *, CONST char *);
t_stat              lpr_detach(UNIT *);
t_stat              lpr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *lpr_description (DEVICE *dptr);

extern char         six_to_ascii[64];

UNIT                lpr_unit[] = {
#if NUM_DEVS_LPR > 1
    {UDATA(&lpr_srv, UNIT_S_CHAN(CHAN_A) | UNIT_LPR | ECHO, 0)},        /* A */
#endif
#if NUM_DEVS_LPR > 2
    {UDATA(&lpr_srv, UNIT_S_CHAN(CHAN_C) | UNIT_LPR, 0)},       /* B */
#endif
#if NUM_DEVS_LPR > 3
    {UDATA(&lpr_srv, UNIT_S_CHAN(CHAN_E) | UNIT_LPR | UNIT_DIS, 0)},    /* C */
#endif
    {UDATA(&lpr_srv, UNIT_S_CHAN(CHAN_CHPIO) | UNIT_LPR, 0)},   /* 704 */
};

MTAB                lpr_mod[] = {
    {ECHO, 0,     NULL, "NOECHO", NULL, NULL, NULL},
    {ECHO, ECHO, "ECHO", "ECHO", NULL, NULL, NULL},
#if NUM_CHAN != 1
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan,
     &get_chan, NULL},
#endif
    {0}
};

DEVICE              lpr_dev = {
    "LP", lpr_unit, NULL, lpr_mod,
    NUM_DEVS_LPR, 8, 15, 1, 8, 36,
    NULL, NULL, &lpr_reset, NULL, &lpr_attach, &lpr_detach,
    &lpr_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lpr_help, NULL, NULL, &lpr_description
};

/* Line printer routines
*/

t_stat
print_line(UNIT * uptr, int chan, int unit)
{
/* Convert word record into column image */
/* Check output type, if auto or text, try and convert record to bcd first */
/* If failed and text report error and dump what we have */
/* Else if binary or not convertable, dump as image */

    uint16              buff[80];       /* Temp conversion buffer */
    int                 i;
    int                 outsel = uptr->u3;

    if ((uptr->flags & (UNIT_ATT | ECHO)) == 0)
        return SCPE_UNATT;      /* attached? */

    /* Try to convert to text */
    memset(buff, 0, sizeof(buff));
    /* Bit flip into temp buffer */
    for (i = 0; i < 24; i++) {
        int                 bit = 1 << (i / 2);
        t_uint64            mask = 1;
        t_uint64            wd = 0;
        int                 b = 36 * (i & 1);
        int                 col;

        wd = lpr_data[unit].wbuff[i];
        for (col = 35; col >= 0; mask <<= 1, col--) {
            if (wd & mask)
                buff[col + b] |= bit;
        }
        lpr_data[unit].wbuff[i] = 0;
    }

    /* Space printer */
    if (outsel == 0 || outsel & PRINT_2) {
        if (uptr->flags & UNIT_ATT)
            sim_fwrite("\n", 1, 1, uptr->fileref);
        if (uptr->flags & ECHO) {
            sim_putchar('\n');
            sim_putchar('\r');
        }
    }

    if (outsel & PRINT_1) {
        if (uptr->flags & UNIT_ATT)
            sim_fwrite("\f\n", 1, 2, uptr->fileref);
        if (uptr->flags & ECHO) {
            sim_putchar('\f');
            sim_putchar('\n');
            sim_putchar('\r');
        }
    }

    if (outsel & PRINT_3) {
        if (uptr->flags & UNIT_ATT)
            sim_fwrite("\n\n", 1, 2, uptr->fileref);
        if (uptr->flags & ECHO) {
            sim_putchar('\n');
            sim_putchar('\r');
            sim_putchar('\n');
        }
    }

    if (outsel & PRINT_4) {
        if (uptr->flags & UNIT_ATT)
            sim_fwrite("\n\n\n", 1, 3, uptr->fileref);
        if (uptr->flags & ECHO) {
            sim_putchar('\n');
            sim_putchar('\r');
            sim_putchar('\n');
            sim_putchar('\n');
        }
    }

    /* Scan each column */
    for (i = 0; i < 72;) {
        int                 bcd = sim_hol_to_bcd(buff[i]);

        if (bcd == 0x7f) 
            lpr_data[unit].lbuff[i++] = 0x7f;
        else {
            if (bcd == 020)
                bcd = 10;
            if (uptr->u5 & LPRSTA_BINMODE)
                lpr_data[unit].lbuff[i++] = (buff[i] != 0)?'1':' ';
            else
                lpr_data[unit].lbuff[i++] = sim_six_to_ascii[bcd];
        }
    }

    /* Trim trailing spaces */
    for (--i; i > 0 && lpr_data[unit].lbuff[i] == ' '; i--) ;

    /* Put output to column where we left off */
    if (outsel & PRINT_9) {
        int                 j =

            (uptr->u5 & LPRSTA_COLMASK) >> LPRSTA_COLSHIFT;
        uptr->u5 &= ~LPRSTA_COLMASK;

        if (j < 71) {
            if (uptr->flags & UNIT_ATT) {
                char                buffer[73];

                memset(buffer, ' ', 72);
                sim_fwrite(buffer, 1, 71 - j, uptr->fileref);
            }
            if (uptr->flags & ECHO) {
                while (j++ < 71)
                    sim_putchar(' ');
            }
        }
    } else {
        uptr->u5 &= ~LPRSTA_COLMASK;
        uptr->u5 |= (i << LPRSTA_COLSHIFT) & LPRSTA_COLMASK;
    }

    /* Print out buffer */
    if (uptr->flags & UNIT_ATT)
        sim_fwrite(lpr_data[unit].lbuff, 1, i + 1, uptr->fileref);
    if (uptr->flags & ECHO) {
        int                 j = 0;

        while (j <= i)
            sim_putchar(lpr_data[unit].lbuff[j++]);
    }
    return SCPE_OK;
}

uint32 lpr_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - lpr_unit);
    int                 i;

    /* Check if valid */
    if ((dev & 03) == 0 || (dev & 03) == 3)
        return SCPE_NODEV;
    /* Check if still active */
    if (uptr->u5 & LPRSTA_CMD) 
        return SCPE_BUSY;
    /* Check if attached */
    if ((uptr->flags & (UNIT_ATT | ECHO)) == 0) {
        chan_set_error(chan);
        sim_debug(DEBUG_EXP, &lpr_dev, "unit=%d not ready\n", u);
        return SCPE_IOERR;
    }
    /* Ok, issue command if correct */
    if (cmd == IO_WRS || cmd == IO_RDS) {
        /* Start device */
        if (((uptr->u5 & (LPRSTA_ON | LPRSTA_IDLE)) ==
             (LPRSTA_ON | LPRSTA_IDLE)) && uptr->wait <= 30) {
            uptr->wait += 85;   /* Wait for next latch point */
        } else
            uptr->wait = 330;   /* Startup delay */
        for (i = 0; i < 24; lpr_data[u].wbuff[i++] = 0) ;
        uptr->u5 &=
            ~(LPRSTA_RCMD | LPRSTA_WCMD | LPRSTA_POSMASK | LPRSTA_WRITE |
              LPRSTA_READ);
        uptr->u3 = 0;
        if (cmd == IO_WRS) {
            sim_debug(DEBUG_CMD, &lpr_dev, "WRS %o unit=%d\n", dev, u);
            uptr->u5 |= LPRSTA_WCMD | LPRSTA_CMD | LPRSTA_WRITE;
        } else {
            sim_debug(DEBUG_CMD, &lpr_dev, "RDS %o unit=%d\n", dev, u);
            uptr->u5 |= LPRSTA_RCMD | LPRSTA_CMD | LPRSTA_READ;
        }
        if ((dev & 03) == 2)
            uptr->u5 |= LPRSTA_BINMODE;
        else
            uptr->u5 &= ~LPRSTA_BINMODE;
        chan_set_sel(chan, 1);
        chan_clear_status(chan);
        sim_activate(uptr, us_to_ticks(1000));  /* activate */
        return SCPE_OK;
    } else {
        chan_set_attn(chan);
    }
    return SCPE_IOERR;
}

t_stat lpr_srv(UNIT * uptr)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - lpr_unit);
    int                 pos;
    int                 r;
    int                 eor = 0;
    int                 action = 0;

    /* Channel has disconnected, abort current line. */
    if (uptr->u5 & LPRSTA_CMD && chan_stat(chan, DEV_DISCO)) {
        if ((uptr->u5 & LPRSTA_POSMASK) != 0)
            print_line(uptr, chan, u);
        uptr->u5 &= ~(LPRSTA_WRITE | LPRSTA_READ | LPRSTA_CMD | LPRSTA_POSMASK);
        chan_clear(chan, DEV_WEOR | DEV_SEL);
        sim_debug(DEBUG_CHAN, &lpr_dev, "unit=%d disconnect\n", u);
    }

    /* If change requested, do that first */
    if (uptr->u5 & LPRSTA_CHANGE) {
        /* Wait until word read by CPU or timeout */
        if (chan_test(chan, DEV_FULL)) {
            uptr->wait -= 50;
            if (uptr->wait == 50)
                uptr->u5 &= ~LPRSTA_CHANGE;
            sim_activate(uptr, us_to_ticks(100));
            return SCPE_OK;
        } else {
            chan_set(chan, DEV_WRITE);
            sim_activate(uptr, uptr->wait);
            uptr->u5 &= ~LPRSTA_CHANGE;
            uptr->wait = 0;
            return SCPE_OK;
        }
    }

    /* Check to see if we have timed out */
    if (uptr->wait != 0) {
        uptr->wait--;
        /* If at end of record and channel is still active, do another print */
        if (((uptr->u5 & (LPRSTA_IDLE|LPRSTA_CMD|LPRSTA_WRITE|LPRSTA_READ|
                 LPRSTA_ON)) == (LPRSTA_IDLE|LPRSTA_CMD|LPRSTA_ON))
            && uptr->wait > 30 && chan_test(chan, STA_ACTIVE)) {
            /* Restart same command */
            uptr->u5 |= (LPRSTA_WRITE | LPRSTA_READ) & (uptr->u5 >> 7);
            uptr->u5 &= ~(LPRSTA_POSMASK);
            chan_set(chan, DEV_WRITE);
            sim_debug(DEBUG_CHAN, &lpr_dev, "unit=%d restarting\n", u);
        }
        sim_activate(uptr, us_to_ticks(1000));  /* activate */
        return SCPE_OK;
    }

    /* If no request, go to idle mode */
    if ((uptr->u5 & (LPRSTA_READ | LPRSTA_WRITE)) == 0) {
        if ((uptr->u5 & (LPRSTA_IDLE | LPRSTA_ON)) ==
            (LPRSTA_IDLE | LPRSTA_ON)) {
            uptr->wait = 85;    /* Delay 85ms */
            uptr->u5 &= ~LPRSTA_IDLE;   /* Not running */
            sim_activate(uptr, us_to_ticks(1000));
        } else {
            uptr->wait = 330;   /* Delay 330ms */
            uptr->u5 &= ~LPRSTA_ON;     /* Turn motor off */
        }
        return SCPE_OK;
    }

    /* Motor is on and up to speed */
    uptr->u5 |= LPRSTA_ON;
    uptr->u5 &= ~LPRSTA_IDLE;
    pos = (uptr->u5 & LPRSTA_POSMASK) >> LPRSTA_POSSHIFT;

    uptr->u3 |= dev_pulse[chan] & PRINT_M;
    dev_pulse[chan] &= ~PRINT_M;
    if (uptr->u3 != 0)
        dev_pulse[chan] |= PRINT_I;

    /* Check if he write out last data */
    if (uptr->u5 & LPRSTA_READ) {
        int                 wrow = pos;
        t_uint64            wd = 0;

        /* Case 0: Read word from MF memory, DEV_WRITE=1 */
        /* Case 1: Read word from MF memory, write echo back */
        /* Case 2: Write echoback, after gone switch to read */
        /* Case 3: Write echoback */
        /* Case 4: No update, DEV_WRITE=1 */
        eor = (uptr->u5 & LPRSTA_BINMODE) ? 1 : 0;
        switch (pos) {
        case 0:
        case 1:         /* Row 9 */
        case 2:
        case 3:         /* Row 8 */
        case 4:
        case 5:         /* Row 7 */
        case 6:
        case 7:         /* Row 6 */
        case 8:
        case 9:         /* Row 5 */
        case 10:
        case 11:                /* Row 4 */
        case 12:
        case 13:                /* Row 3 */
        case 14:
        case 15:                /* Row 2 */
        case 16:                /* Row 1 */
            break;
        case 17:                /* Row 1L and start Echo */
            action = 1;
            break;
        case 18:                /* Echo 8-4 R */
            wd = lpr_data[u].wbuff[2];
            wd -= lpr_data[u].wbuff[10];
            /* I'm not sure how these are computed */
            /* But forcing to zero works */
            wd = 0;
            action = 2;
            break;
        case 19:                /* Echo 8-4 L */
            wd = lpr_data[u].wbuff[3];
            wd -= lpr_data[u].wbuff[11];
            /* I'm not sure how these are computed */
            /* But forcing to zero works */
            wd = 0;
            action = 3;
            break;
        case 20:                /* Row 10 R */
            wrow = 18;
            action = 0;
            break;
        case 21:                /* Row 10 L */
            wrow = 19;
            action = 1;
            break;
        case 22:                /* Echo 8-3 */
            /* Fill for echo back */
            wd = lpr_data[u].wbuff[2];
            wd -= lpr_data[u].wbuff[12];
            /* I'm not sure how these are computed */
            /* But forcing to zero works */
            wd = 0;
            action = 2;
            break;
        case 23:
            wd = lpr_data[u].wbuff[3];
            wd -= lpr_data[u].wbuff[13];
            /* I'm not sure how these are computed */
            /* But forcing to zero works */
            wd = 0;
            action = 3;
            break;
        case 24:
            wrow = 20;
            break;
        case 25:                /* Row 11 */
            wrow = 21;
            action = 1;
            break;
        case 26:                /* Echo 9 */
            action = 2;
            wd = lpr_data[u].wbuff[0];
            break;
        case 27:
            action = 3;
            wd = lpr_data[u].wbuff[1];
            break;
        case 28:
            wrow = 22;
            break;
        case 29:                /* Row 12 */
            wrow = 23;
            action = 1;
            break;
        case 45:                /* Echo 1 */
            eor = 1;
        case 30:
        case 31:                /* Echo 8 */
        case 32:
        case 33:                /* Echo 7 */
        case 34:
        case 35:                /* Echo 6 */
        case 36:
        case 37:                /* Echo 5 */
        case 38:
        case 39:                /* Echo 4 */
        case 40:
        case 41:                /* Echo 3 */
        case 42:
        case 43:                /* Echo 2 */
        case 44:                /* Echo 1 */
            wrow = pos - 28;
            wd = lpr_data[u].wbuff[wrow];
            action = 2;
            break;
        }

        if (action == 0 || action == 1) {
        /* If reading grab next word */
            r = chan_read(chan, &lpr_data[u].wbuff[wrow], 0);
            if (action == 1)
                chan_clear(chan, DEV_WRITE);
        } else { /* action == 2 || action == 3 */
        /* Place echo data in buffer */
            r = chan_write(chan, &wd, 0);
            /* Change back to reading */
            if (action == 3) {
                uptr->wait = 650;
                uptr->u5 &= ~(LPRSTA_POSMASK | LPRSTA_EOR);
                uptr->u5 |= (++pos << LPRSTA_POSSHIFT) & LPRSTA_POSMASK;
                uptr->u5 |= LPRSTA_CHANGE;
                sim_activate(uptr, us_to_ticks(100));
                return SCPE_OK;
            }
        }
    } else {
        eor = (pos == 23 || uptr->u5 & LPRSTA_BINMODE) ? 1 : 0;
        r = chan_read(chan, &lpr_data[u].wbuff[pos], 0);
    }
    switch (r) {
    case END_RECORD:
        if (pos != 0)
            print_line(uptr, chan, u);
        uptr->wait = 85;        /* Print wheel gap */
        uptr->u5 |= LPRSTA_EOR | LPRSTA_IDLE;
        uptr->u5 &= ~(LPRSTA_WRITE | LPRSTA_READ | LPRSTA_POSMASK);
        chan_set(chan, DEV_REOR);
        break;
    case DATA_OK:
        pos++;
        if (eor) {
            print_line(uptr, chan, u);
            uptr->wait = 85;    /* Print wheel gap */
            uptr->u5 |= LPRSTA_EOR | LPRSTA_IDLE;
            uptr->u5 &= ~(LPRSTA_WRITE | LPRSTA_READ | LPRSTA_POSMASK);
            chan_set(chan, DEV_REOR);
        } else {
            uptr->wait = 0;
            uptr->u5 &= ~(LPRSTA_POSMASK | LPRSTA_EOR);
            uptr->u5 |= (pos << LPRSTA_POSSHIFT) & LPRSTA_POSMASK;
            sim_activate(uptr, (pos & 1) ? us_to_ticks(300) : us_to_ticks(13000));
        }
        break;
    case TIME_ERROR:
        if (pos != 0)
            print_line(uptr, chan, u);
        chan_set_attn(chan);
        chan_set(chan, DEV_REOR);
        uptr->wait = 13 * (12 - (pos / 2)) + 85;
        uptr->u5 &= ~(LPRSTA_READ | LPRSTA_WRITE | LPRSTA_POSMASK);
        uptr->u5 |= LPRSTA_IDLE;
        break;
    }

    sim_activate(uptr, us_to_ticks(1000));
    return SCPE_OK;
}

void
lpr_ini(UNIT * uptr, t_bool f)
{
    int                 u = (uptr - lpr_unit);
    int                 i;

    uptr->u5 = 0;
    for (i = 0; i < 140; i++)
        lpr_data[u].lbuff[i] = ' ';
}

t_stat
lpr_reset(DEVICE * dptr)
{
    return SCPE_OK;
}

t_stat
lpr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;
    uptr->u5 = 0;
    return SCPE_OK;
}

t_stat
lpr_detach(UNIT * uptr)
{
    return detach_unit(uptr);
}

t_stat
lpr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "Line Printer\n\n");
   fprintf (st, "The system supports one line printer\n");
   fprintf (st, "by default. The Line printer can be configured to any number of\n");
   fprintf (st, "lines per page with the:\n");
   fprintf (st, "        sim> SET LPn LINESPERPAGE=n\n\n");
   fprintf (st, "The printer acted as the console printer therefore the default is\n"); 
   fprintf (st, "echo to the console\n");
   fprintf (st, "The default is 59 lines per page\n\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
lpr_description(DEVICE *dptr)
{
   return "Line Printer";
}


#endif
