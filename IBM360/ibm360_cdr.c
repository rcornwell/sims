/* ibm360_cdr.c: IBM 360 Card Reader.

   Copyright (c) 2017-2020, Richard Cornwell

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

   This is the standard card reader.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "ibm360_defs.h"
#include "sim_defs.h"
#include "sim_card.h"

#ifdef NUM_DEVS_CDR
#define UNIT_CDR       UNIT_ATTABLE | UNIT_RO | UNIT_DISABLE | MODE_029


#define CHN_SNS        0x04       /* Sense command */

/* Device status information stored in u3 */
#define CDR_RD         0x02       /* Read command */
#define CDR_FEED       0x03       /* Feed next card */
#define CDR_CMDMSK     0x27       /* Mask command part. */
#define CDR_MODE       0x20       /* Mode operation */
#define CDR_STKMSK     0xC0       /* Mask for stacker */
#define CDP_WR         0x09       /* Punch command */
#define CDR_CARD       0x100      /* Unit has card in buffer */
#define CDR_EOF        0x200      /* An end of file card was read */
#define CDR_ERR        0x400      /* Last card had an error */

/* Upper 11 bits of u3 hold the device address */

/* u4 holds current column, */

/* in u5 packs sense byte 0,1 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ     0x80       /* Command reject */
#define SNS_INTVENT    0x40       /* Unit intervention required */
#define SNS_BUSCHK     0x20       /* Parity error on bus */
#define SNS_EQUCHK     0x10       /* Equipment check */
#define SNS_DATCHK     0x08       /* Data Check */
#define SNS_OVRRUN     0x04       /* Data overrun */
#define SNS_SEQUENCE   0x02       /* Unusual sequence */
#define SNS_CHN9       0x01       /* Channel 9 on printer */

#define CMD    u3
#define COL    u4
#define SNS    u5


/* std devices. data structures

   cdr_dev       Card Reader device descriptor
   cdr_unit      Card Reader unit descriptor
   cdr_reg       Card Reader register list
   cdr_mod       Card Reader modifiers list
*/


uint8               cdr_startcmd(UNIT *,  uint8);
t_stat              cdr_boot(int32, DEVICE *);
t_stat              cdr_srv(UNIT *);
t_stat              cdr_reset(DEVICE *);
t_stat              cdr_attach(UNIT *, CONST char *);
t_stat              cdr_detach(UNIT *);
t_stat              cdr_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cdr_description(DEVICE *);


UNIT                cdr_unit[] = {
   {UDATA(cdr_srv, UNIT_CDR, 0), 300, UNIT_ADDR(0x0C)},
#if NUM_DEVS_CDR > 1
   {UDATA(cdr_srv, UNIT_CDR | UNIT_DIS, 0), 300, UNIT_ADDR(0x1C)},
#if NUM_DEVS_CDR > 2
   {UDATA(cdr_srv, UNIT_CDR | UNIT_DIS, 0), 300, UNIT_ADDR(0x40C)},
#if NUM_DEVS_CDR > 3
   {UDATA(cdr_srv, UNIT_CDR | UNIT_DIS, 0), 300, UNIT_ADDR(0x41C)},
#endif
#endif
#endif
};

MTAB                cdr_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL, 
               "Set defualt format for reading cards in"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL, "Set device address"},
    {0}
};

struct dib cdr_dib = { 0xFF, 1, NULL, cdr_startcmd, NULL, cdr_unit};

DEVICE              cdr_dev = {
    "CDR", cdr_unit, NULL, cdr_mod,
    NUM_DEVS_CDR, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, &cdr_boot, &cdr_attach, &cdr_detach,
    &cdr_dib, DEV_UADDR | DEV_DISABLE | DEV_DEBUG | DEV_CARD, 0, crd_debug
};


/*
 * Start card reader to read in one card.
 */
uint8  cdr_startcmd(UNIT *uptr,  uint8 cmd) {
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);

    if ((uptr->CMD & CDR_CMDMSK) != 0)
        return SNS_BSY;

    sim_debug(DEBUG_CMD, dptr, "CMD unit=%d %x\n", unit, cmd);

    /* If not attached and not sense, return error */
    if (cmd != 4 && (uptr->flags & UNIT_ATT) == 0) {
        uptr->SNS = SNS_INTVENT;
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    switch (cmd & 0x7) {
    case 2:              /* Read command */
         uptr->SNS = 0;
         uptr->COL = 0;
         /* Check if there was End of file */
         if ((uptr->CMD & CDR_EOF) != 0) {
             uint16   *image = (uint16 *)(uptr->up7);
             uptr->CMD &= ~(CDR_EOF|CDR_ERR);
             /* Attempt to read in another card if there is one */
             switch(sim_read_card(uptr, image)) {
             case CDSE_ERROR:
                  uptr->CMD |= CDR_ERR;
                  /* Fall through */
             case CDSE_OK:
                  uptr->CMD |= CDR_CARD;
                  break;
             case CDSE_EOF:
                  uptr->CMD |= CDR_EOF;
                  break;
             case CDSE_EMPTY:
                  break;
             }
             return SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP;
         }
         /* Check if no more cards left in deck */
         if ((uptr->CMD & CDR_CARD) == 0) {
             uptr->SNS = SNS_INTVENT;
             return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
         }
         uptr->CMD &= ~(0xff);
         uptr->CMD |= (cmd & 0xff);
         sim_activate(uptr, 100);       /* Start unit off */
         return 0;

    case 3:              /* Control */
         uptr->SNS = 0;
         uptr->CMD &= ~(0xff);
         if (cmd == 0x3)
             return SNS_CHNEND|SNS_DEVEND;
         if ((cmd & 0x30) != 0x20 || (cmd & 0xc0) == 0xc0) {
             uptr->SNS |= SNS_CMDREJ;
             return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
         }
         uptr->CMD &= ~(CDR_CARD|CDR_ERR);
         uptr->CMD |= (cmd & 0xff);
         uptr->COL = 0;
         sim_activate(uptr, 10000);       /* Start unit off */
         return SNS_CHNEND;

    case 0:               /* Status */
         return 0;

    case 4:               /* Sense */
         uptr->CMD &= ~(0xff);
         uptr->CMD |= (cmd & 0xff);
         sim_activate(uptr, 10);
         return 0;

    default:              /* invalid command */
         uptr->SNS |= SNS_CMDREJ;
         break;
    }

    if (uptr->SNS)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle transfer of data for card reader */
t_stat
cdr_srv(UNIT *uptr) {
    int       addr = GET_UADDR(uptr->CMD);
    uint16   *image = (uint16 *)(uptr->up7);
    int       fl = 0;

    if ((uptr->CMD & CDR_CMDMSK) == CHN_SNS) {
         uint8 ch = uptr->SNS;
         if (ch == 0 && (uptr->flags & UNIT_ATT) == 0)
             ch = SNS_INTVENT;
         chan_write_byte(addr, &ch);
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         uptr->CMD &= ~(CDR_CMDMSK);
         uptr->SNS = 0;
         return SCPE_OK;
    }

    /* Check if new card requested. */
    if ((uptr->CMD & CDR_CARD) == 0) {
       sim_debug(DEBUG_CMD, &cdr_dev, "read card =%x %02x\n", addr, uptr->CMD & CDR_CMDMSK);
       if ((uptr->CMD & CDR_ERR) != 0) {
           fl = SNS_UNITCHK;
       }
       uptr->CMD &= ~(CDR_EOF|CDR_ERR|CDR_CMDMSK);
       switch(sim_read_card(uptr, image)) {
       case CDSE_ERROR:
            uptr->CMD |= CDR_ERR;
            /* Fall through */
       case CDSE_OK:
            uptr->CMD |= CDR_CARD;
            /* Fall through */
       case CDSE_EMPTY:
            set_devattn(addr, SNS_DEVEND|fl);
            return SCPE_OK;
       case CDSE_EOF:
            uptr->CMD |= CDR_EOF ;
            set_devattn(addr, SNS_DEVEND|fl);
            return SCPE_OK;
       }
    }

    /* Copy next column over */
    if ((uptr->CMD & CDR_CMDMSK) == CDR_RD) {
        int                  u = uptr-cdr_unit;
        uint16               xlat;
        uint8                ch = 0;

        if ((uptr->CMD & CDR_ERR) != 0) {
            uptr->SNS = SNS_DATCHK;
            goto feed;
        }
        xlat = sim_hol_to_ebcdic(image[uptr->COL]);

        if (xlat == 0x100) {
            uptr->SNS |= SNS_DATCHK;
            ch = 0x00;
        } else
            ch = (uint8)(xlat&0xff);
        if (chan_write_byte(addr, &ch)) {
            goto feed;
        } else {
            uptr->COL++;
            sim_debug(DEBUG_DATA, &cdr_dev, "%d: Char > %02o\n", u, ch);
        }
        if (uptr->COL == 80) {
            goto feed;
        }
        sim_activate(uptr, 100);
    }

    return SCPE_OK;

feed:
    /* If feed given, request new card */
    if ((uptr->CMD & 0xc0) != 0xc0) {
        uptr->CMD &= ~(CDR_CARD);
        sim_debug(DEBUG_CMD, &cdr_dev, "read end col =%x %04x\n", addr, uptr->CMD);
        chan_end(addr, SNS_CHNEND);
        sim_activate(uptr, 10000);       /* Feed the card */
    } else {
        if ((uptr->CMD & CDR_ERR) != 0) {
            fl = SNS_UNITCHK;
        }
        uptr->CMD &= ~(0xff);
        chan_end(addr, SNS_CHNEND|SNS_DEVEND|fl);
        sim_debug(DEBUG_CMD, &cdr_dev, "read end col no feed =%x %04x\n", addr, uptr->CMD);
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
cdr_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];

    if ((uptr->flags & UNIT_ATT) == 0)
       return SCPE_UNATT;       /* attached? */
    return chan_boot(GET_UADDR(uptr->CMD), dptr);
}

t_stat
cdr_attach(UNIT * uptr, CONST char *file)
{
    int                 addr = GET_UADDR(uptr->CMD);
    t_stat              r;
    uint16             *image;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
       return r;
    if (uptr->up7 == NULL)
        uptr->up7 = malloc(sizeof(uint16)*80);
    if (uptr->CMD & CDR_CARD) {
        return SCPE_OK;
    }
    uptr->CMD &= ~(CDR_CARD|CDR_EOF|CDR_ERR);
    uptr->SNS = 0;
    uptr->COL = 0;
    uptr->u6 = 0;
    image = (uint16 *)(uptr->up7);
    switch(sim_read_card(uptr, image)) {
    case CDSE_ERROR:
         uptr->CMD |= CDR_ERR;
         /* Fall through */
    case CDSE_OK:
         uptr->CMD |= CDR_CARD;
         break;

    case CDSE_EMPTY:
    case CDSE_EOF:
         break;
    }
    set_devattn(addr, SNS_DEVEND);
    return SCPE_OK;
}

t_stat
cdr_detach(UNIT * uptr)
{
    if (uptr->up7 != 0)
        free(uptr->up7);
    uptr->up7 = 0;
    uptr->SNS = 0;
    uptr->CMD &= ~(CDR_CARD|CDR_EOF|CDR_ERR);
    return sim_card_detach(uptr);
}

t_stat
cdr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "2540R Card Reader\n\n");
   sim_card_attach_help(st, dptr, uptr, flag, cptr);
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cdr_description(DEVICE *dptr)
{
   return "2540R Card Reader";
}
#endif
