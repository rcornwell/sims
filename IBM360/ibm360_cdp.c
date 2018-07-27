/* ibm360_cdp.c: IBM 360 Card Punch

   Copyright (c) 2017, Richard Cornwell

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

   This is the standard card punch.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "ibm360_defs.h"
#include "sim_defs.h"
#include "sim_card.h"

#ifdef NUM_DEVS_CDP
#define UNIT_CDP       UNIT_ATTABLE | UNIT_DISABLE | MODE_029


#define CHN_SNS        0x04       /* Sense command */

/* Device status information stored in u3 */
#define CDR_RD         0x02       /* Read command */
#define CDR_FEED       0x03       /* Feed next card */
#define CDP_CMDMSK     0x27       /* Mask command part. */
#define CDR_MODE       0x20       /* Mode operation */
#define CDR_STKMSK     0xC0       /* Mask for stacker */
#define CDP_WR         0x01       /* Punch command */
#define CDP_CARD       0x100      /* Unit has card in buffer */


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



/* std devices. data structures

   cdp_dev       Card Punch device descriptor
   cdp_unit      Card Punch unit descriptor
   cdp_reg       Card Punch register list
   cdp_mod       Card Punch modifiers list
*/

uint8               cdp_startcmd(UNIT *, uint16,  uint8);
void                cdp_ini(UNIT *, t_bool);
t_stat              cdp_srv(UNIT *);
t_stat              cdp_reset(DEVICE *);
t_stat              cdp_attach(UNIT *, CONST char *);
t_stat              cdp_detach(UNIT *);

UNIT                cdp_unit[] = {
    {UDATA(cdp_srv, UNIT_CDP, 0), 600, UNIT_ADDR(0x00D) },       /* A */
#if NUM_DEVS_CDP > 1
    {UDATA(cdp_srv, UNIT_CDP, 0), 600, UNIT_ADDR(0x01D)},       /* A */
#endif
};


MTAB                cdp_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {0}
};

struct dib cdp_dib = { 0xFF, 1, NULL, cdp_startcmd, NULL, cdp_unit, NULL};

DEVICE              cdp_dev = {
    "CDP", cdp_unit, NULL, cdp_mod,
    NUM_DEVS_CDP, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &cdp_attach, &cdp_detach,
    &cdp_dib, DEV_UADDR | DEV_DISABLE | DEV_DEBUG | DEV_CARD, 0, crd_debug
};




/* Card punch routine

   Modifiers have been checked by the caller
   C modifier is recognized (column binary is implemented)
*/


uint8  cdp_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) {
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);
    uint8   ch;

    if ((uptr->u3 & (CDP_CARD|CDP_CMDMSK)) != 0) {
        if ((uptr->flags & UNIT_ATT) != 0)
            return SNS_BSY;
        return SNS_DEVEND|SNS_UNITCHK;
    }

    sim_debug(DEBUG_CMD, dptr, "CMD unit=%d %x\n", unit, cmd);
    switch (cmd & 0x7) {
    case 1:              /* Write command */
         uptr->u3 &= ~(CDP_CMDMSK);
         uptr->u3 |= (cmd & CDP_CMDMSK);
         sim_activate(uptr, 10);       /* Start unit off */
         uptr->u4 = 0;
         uptr->u5 = 0;
         return 0;

    case 3:
         if (cmd != 0x3) {
             uptr->u5 |= SNS_CMDREJ;
             return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
         }
         return SNS_CHNEND|SNS_DEVEND;

    case 0:                /* Status */
         break;

    case 4:                /* Sense */
         uptr->u3 &= ~(CDP_CMDMSK);
         uptr->u3 |= (cmd & CDP_CMDMSK);
         sim_activate(uptr, 10);
         return 0;

    default:              /* invalid command */
         uptr->u5 |= SNS_CMDREJ;
         break;
    }
    if (uptr->u5 & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}


/* Handle transfer of data for card punch */
t_stat
cdp_srv(UNIT *uptr) {
    int       u = uptr-cdp_unit;
    uint16   *image = (uint16 *)(uptr->up7);
    uint16    addr = GET_UADDR(uptr->u3);

    /* Handle sense */
    if ((uptr->u3 & CDP_CMDMSK) == 0x4) {
         uint8 ch = uptr->u5;
         chan_write_byte(addr, &ch);
         chan_end(addr, SNS_DEVEND|SNS_CHNEND);
         return SCPE_OK;
    }

    if (uptr->u3 & CDP_CARD) {
        /* Done waiting, punch card */
        uptr->u3 &= ~CDP_CARD;
        sim_debug(DEBUG_DETAIL, &cdp_dev, "unit=%d:punch\n", u);
        switch(sim_punch_card(uptr, image)) {
        /* If we get here, something is wrong */
        default:
        sim_debug(DEBUG_DETAIL, &cdp_dev, "unit=%d:punch error\n", u);
             set_devattn(addr, SNS_DEVEND|SNS_UNITCHK);
             break;
        case CDSE_OK:
             set_devattn(addr, SNS_DEVEND);
             break;
        }
        return SCPE_OK;
    }

    /* Copy next column over */
    if (uptr->u4 < 80) {
        uint8               ch = 0;

        if (chan_read_byte(addr, &ch)) {
            uptr->u3 |= CDP_CARD;
        } else {
            sim_debug(DEBUG_DATA, &cdp_dev, "%d: Char < %02o\n", u, ch);
            image[uptr->u4++] = sim_ebcdic_to_hol(ch);
            if (uptr->u4 == 80) {
                uptr->u3 |= CDP_CARD;
            }
        }
        if (uptr->u3 & CDP_CARD) {
            uptr->u3 &= ~(CDP_CMDMSK);
            chan_end(addr, SNS_CHNEND);
            sim_activate(uptr, 1000);
        } else
            sim_activate(uptr, 10);
    }
    return SCPE_OK;
}


t_stat
cdp_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
       return r;
    if (uptr->up7 == 0) {
        uptr->up7 = calloc(80, sizeof(uint16));
        uptr->u5 = 0;
    }
    return SCPE_OK;
}

t_stat
cdp_detach(UNIT * uptr)
{
    uint16   *image = (uint16 *)(uptr->up7);

    if (uptr->u5 & CDP_CARD)
        sim_punch_card(uptr, image);
    if (uptr->up7 != 0)
        free(uptr->up7);
    uptr->up7 = 0;
    return sim_card_detach(uptr);
}
#endif

