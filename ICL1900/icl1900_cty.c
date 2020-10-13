/* icl1900_cty.c: ICL1900 front end (console terminal) simulator

   Copyright (c) 2018, Richard Cornwell

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

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell

*/

#include "icl1900_defs.h"

extern int32 tmxr_poll;
void cty_cmd(uint32 dev, uint32 cmd);
void cty_status(uint32 dev, uint32 *resp);
t_stat ctyi_svc (UNIT *uptr);
t_stat ctyo_svc (UNIT *uptr);
t_stat cty_reset (DEVICE *dptr);
t_stat tty_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cty_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *cty_description (DEVICE *dptr);

DIB cty_dib = { CHAR_DEV, NULL, cty_cmd, cty_status};

#define CMD          u3
#define STATUS       u4
#define HOLD         u5

#define START        01
#define STOP         02
#define END          000001
#define BUSY         000040
#define INPUT        000100
#define CANCEL       000200
#define ACCEPT       000400
#define F1           001000
#define F2           002000
#define F3           004000
#define F4           010000
#define F5           020000

UNIT cty_unit[] = {
    { UDATA (&ctyo_svc, UNIT_ADDR(3)|TT_MODE_7P, 0), 10000 },
    { UDATA (&ctyi_svc, UNIT_ADDR(2)|TT_MODE_7P|UNIT_IDLE, 0), 0 },
    };


MTAB cty_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", &tty_set_mode },
    { TT_MODE, TT_MODE_7B, "7b", "7B", &tty_set_mode },
    { TT_MODE, TT_MODE_8B, "8b", "8B", &tty_set_mode },
    { TT_MODE, TT_MODE_7P, "7b", "7P", &tty_set_mode },
    { 0 }
    };

DEVICE cty_dev = {
    "CTY", cty_unit, NULL, cty_mod,
    2, 8, 22, 1, 8, 22,
    NULL, NULL, &cty_reset,
    NULL, NULL, NULL, &cty_dib, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &cty_help, NULL, NULL, &cty_description
    };

/*
 * Commands:
 *    Bit 0 = start.
 *    Bit 1 = stop.
 *
 * Responses:
 *    Bit  0 = End of Transfer
 *    Bit  5 = Busy
 *    Bit  6 = Input Button Pushed.
 *    Bit  7 = Cancel Button Pushed.
 *    Bit  8 = Accept Button Pushed.
 *    Bit  9 = F1
 *    Bit 10 = F2
 *    Bit 11 = F3
 *    Bit 12 = F4
 *    Bit 13 = F5
 *
 *    00xxxx -> 011xxxx
 *    01xxxx -> 010xxxx
 *    10xxxx -> 100xxxx
 *    11xxxx -> 101xxxx
 *
 */
void cty_cmd(uint32 dev, uint32 cmd) {
    int   u = 0;

    if (dev > 3)
       return;
    if (dev == 2)
       u++;
    if (cmd & START) {
        cty_unit[u].CMD = 1;
        cty_unit[u].STATUS = BUSY;
        if (!u)
           sim_activate(&cty_unit[u], cty_unit[u].wait);
        sim_debug(DEBUG_CMD, &cty_dev, "start %d\n", dev);
    }
    if (cmd & STOP) {
        cty_unit[u].STATUS &= ~BUSY;
        sim_debug(DEBUG_CMD, &cty_dev, "stop %d\n", dev);
    }
    chan_clr_done(GET_UADDR(cty_unit[u].flags));
}

void cty_status(uint32 dev, uint32 *resp) {
    int   u = 0;

    if (dev > 3)
       return;
    if (dev == 2)
       u++;
    *resp = cty_unit[u].STATUS;
    cty_unit[u].STATUS &= BUSY;
    chan_clr_done(GET_UADDR(cty_unit[u].flags));
}

t_stat ctyo_svc (UNIT *uptr)
{
    t_stat  r;
    uint8   ch;
    int     eor;

    /* Check if we had a held characteter */
    if (uptr->HOLD != 0) {
        if ((r = sim_putchar_s (uptr->HOLD)) == SCPE_STALL) {
            r = SCPE_OK;
        } else {
            if (uptr->HOLD == '\r')
                uptr->HOLD = '\n';
            else
                uptr->HOLD = 0;
        }
        sim_activate (uptr, uptr->wait);               /* try again */
        return r;
    }

    if (uptr->STATUS & BUSY) {
       eor = chan_output_char(GET_UADDR(uptr->flags), &ch, 0);
       switch (ch & 060) {
       case 000:   ch = 0060 | (ch & 017); break;
       case 020:   ch = 0040 | (ch & 017); break;
       case 040:   ch = 0100 | (ch & 017); break;
       case 060:   ch = 0120 | (ch & 017); break;
       }
       if (ch == 0137) {
           ch = '\r';
           uptr->HOLD = '\n';
       }
       ch = sim_tt_outcvt ( ch, TT_GET_MODE (uptr->flags)) ;
       if ((r = sim_putchar_s (ch)) == SCPE_STALL) {   /* output; error? */
           uptr->HOLD = ch;
           r = SCPE_OK;
       }
       if (eor) {
          uptr->CMD = 0;
          uptr->STATUS &= ~BUSY;
          uptr->STATUS |= END;
          chan_set_done(GET_UADDR(uptr->flags));
       }
       sim_activate (uptr, uptr->wait);               /* try again */
    }
    return SCPE_OK;
}

t_stat ctyi_svc (UNIT *uptr)
{
    int     dev = GET_UADDR(uptr->flags);
    t_stat  r;
    uint8   ch;
    int     eor;

    sim_clock_coschedule (uptr, tmxr_poll);

/* Handle input */

    r = sim_poll_kbd();
    if (r & SCPE_KFLAG) {
       ch = 0177 & sim_tt_inpcvt(r & 0377, TT_GET_MODE (uptr->flags));
       if (uptr->HOLD) {
           if (ch >= '1' && ch <= '5') {
               chan_set_done(dev);
               uptr->STATUS |= (F1 << (ch - '1'));
               uptr->CMD = 0;
           }  else {
               sim_putchar('\007');
           }
           uptr->HOLD = 0;
           return SCPE_OK;
       }
       if (uptr->CMD) {
          /* Handle end of buffer */
          switch (ch) {
          case '\r':
          case '\n':
                sim_debug(DEBUG_DATA, &cty_dev, ": ent\n");
                sim_putchar('\r');
                sim_putchar('\n');
                uptr->STATUS |= ACCEPT;
                uptr->CMD = 0;
                chan_set_done(dev);
                break;

          case 033: /* function key <escape>n key */
                uptr->HOLD = 1;
                break;

          case 030: /* ^X Post input interrupt */
                sim_debug(DEBUG_CMD, &cty_dev, ": inp\n");
                uptr->STATUS |= INPUT;
                uptr->HOLD = 0;
                uptr->CMD = 0;
                chan_set_done(dev);
                break;

          case 03:  /* ^C */
          case 025: /* ^U clear line */
                uptr->STATUS |= CANCEL;
                uptr->HOLD = 0;
                uptr->CMD = 0;
                chan_set_done(dev);
                break;

          default:
                sim_debug(DEBUG_DATA, &cty_dev, ": key '%c'\n", ch);
                if (ch >= 0140)
                    ch -= 040;
                if (ch >= 0100)
                    ch -= 040;
                else if (ch >= 060)
                    ch -= 060;
                else if (ch >= 040)
                    ch -= 020;
                else {
                    sim_putchar('\007');
                    break;
                }
                eor = chan_input_char(dev, &ch, 0);
                switch (ch & 060) {
                case 000:   ch = 0060 | (ch & 017); break;
                case 020:   ch = 0040 | (ch & 017); break;
                case 040:   ch = 0100 | (ch & 017); break;
                case 060:   ch = 0120 | (ch & 017); break;
                }
                ch = sim_tt_outcvt ( ch, TT_GET_MODE (uptr->flags)) ;
                sim_putchar (ch);
                if (eor) {
                    uptr->CMD = 0;
                    uptr->STATUS |= ACCEPT|END;
                    chan_set_done(dev);
                }
                break;
           }
        } else {
            /* Handle end of buffer */
            switch (ch) {
            case 033: /* function key <escape>n key */
                  uptr->HOLD = 1;
                  break;

            case 030: /* ^X Post input interrupt */
                  sim_debug(DEBUG_CMD, &cty_dev, ": inp\n");
                  uptr->STATUS |= INPUT;
                  uptr->HOLD = 0;
                  chan_set_done(dev);
                  break;

            case 03:  /* ^C */
            case 025: /* ^U clear line */
                  uptr->STATUS |= CANCEL;
                  uptr->HOLD = 0;
                  chan_set_done(dev);
                  break;

            default:
                  sim_debug(DEBUG_DATA, &cty_dev, ": ikey '%c'\n", ch);
                  sim_putchar('\007');
            }
        }
    }

    return SCPE_OK;
}

/* Reset */

t_stat cty_reset (DEVICE *dptr)
{
    cty_unit[0].CMD = 0;
    cty_unit[1].CMD = 0;
    cty_unit[0].STATUS = 0;
    cty_unit[1].STATUS = 0;
    chan_clr_done(GET_UADDR(cty_unit[0].flags));
    chan_clr_done(GET_UADDR(cty_unit[1].flags));
    sim_clock_coschedule (&cty_unit[1], tmxr_poll);

    return SCPE_OK;
}


t_stat tty_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    cty_unit[0].flags = (cty_unit[0].flags & ~TT_MODE) | val;
    cty_unit[1].flags = (cty_unit[1].flags & ~TT_MODE) | val;
    return SCPE_OK;
}

t_stat cty_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "The additional terminals can be set to one of four modes: UC, 7P, 7B, or 8B.\n\n");
fprintf (st, "  mode  input characters        output characters\n\n");
fprintf (st, "  UC    lower case converted    lower case converted to upper case,\n");
fprintf (st, "        to upper case,          high-order bit cleared,\n");
fprintf (st, "        high-order bit cleared  non-printing characters suppressed\n");
fprintf (st, "  7P    high-order bit cleared  high-order bit cleared,\n");
fprintf (st, "                                non-printing characters suppressed\n");
fprintf (st, "  7B    high-order bit cleared  high-order bit cleared\n");
fprintf (st, "  8B    no changes              no changes\n\n");
fprintf (st, "The default mode is 7P.  In addition, each line can be configured to\n");
fprintf (st, "behave as though it was attached to a dataset, or hardwired to a terminal:\n\n");
fprint_reg_help (st, &cty_dev);
return SCPE_OK;
}

const char *cty_description (DEVICE *dptr)
{
    return "Console TTY Line";

}

