/* ibm360_cdp.c: IBM 360 Card Punch

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
#define UNIT_CDP       UNIT_ATTABLE | UNIT_DISABLE | UNIT_SEQ | MODE_029


#define CHN_SNS        0x04       /* Sense command */

/* Device status information stored in u3 */
#define CDR_RD         0x02       /* Read command */
#define CDR_FEED       0x03       /* Feed next card */
#define CDP_CMDMSK     0x27       /* Mask command part. */
#define CDR_MODE       0x20       /* Mode operation */
#define CDR_STKMSK     0xC0       /* Mask for stacker */
#define CDP_WR         0x01       /* Punch command */
#define CDP_CARD       0x100      /* Unit has card in buffer */

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

   cdp_dev       Card Punch device descriptor
   cdp_unit      Card Punch unit descriptor
   cdp_reg       Card Punch register list
   cdp_mod       Card Punch modifiers list
*/

uint8               cdp_startio(UNIT *uptr);
uint8               cdp_startcmd(UNIT *,  uint8);
void                cdp_ini(UNIT *, t_bool);
t_stat              cdp_srv(UNIT *);
t_stat              cdp_reset(DEVICE *);
t_stat              cdp_attach(UNIT *, CONST char *);
t_stat              cdp_detach(UNIT *);
t_stat              cdp_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cdp_description(DEVICE *);

UNIT                cdp_unit[] = {
    {UDATA(cdp_srv, UNIT_CDP, 0), 600, UNIT_ADDR(0x00D) },
#if NUM_DEVS_CDP > 1
    {UDATA(cdp_srv, UNIT_CDP | UNIT_DIS, 0), 600, UNIT_ADDR(0x01D)},
#if NUM_DEVS_CDP > 2
    {UDATA(cdp_srv, UNIT_CDP | UNIT_DIS, 0), 600, UNIT_ADDR(0x40D)},
#if NUM_DEVS_CDP > 3
    {UDATA(cdp_srv, UNIT_CDP | UNIT_DIS, 0), 600, UNIT_ADDR(0x41D)},
#endif
#endif
#endif
};


MTAB                cdp_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {0}
};

struct dib cdp_dib = { 0xFF, 1, cdp_startio, cdp_startcmd, NULL, cdp_unit, NULL};

DEVICE              cdp_dev = {
    "CDP", cdp_unit, NULL, cdp_mod,
    NUM_DEVS_CDP, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &cdp_attach, &cdp_detach,
    &cdp_dib, DEV_UADDR | DEV_DISABLE | DEV_DEBUG | DEV_CARD, 0, crd_debug,
    NULL, NULL, &cdp_help, NULL, NULL, &cdp_description
};



/*
 * Check if device ready to start commands.
 */

uint8  cdp_startio(UNIT *uptr) {
    DEVICE         *dptr = find_dev_from_unit(uptr);

    /* Check if unit is free */
    if ((uptr->CMD & (CDP_CARD|CDP_CMDMSK)) != 0) {
        return SNS_BSY;
    }
    sim_debug(DEBUG_CMD, dptr, "start io\n");
    return 0;
}

/*
 * Start the card punch to punch one card.
 */

uint8  cdp_startcmd(UNIT *uptr,  uint8 cmd) {
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);

    if ((uptr->CMD & (CDP_CARD|CDP_CMDMSK)) != 0)
        return SNS_BSY;

    sim_debug(DEBUG_CMD, dptr, "CMD unit=%d %x\n", unit, cmd);
    switch (cmd & 0x7) {
    case 1:              /* Write command */
         uptr->CMD &= ~(CDP_CMDMSK);
         uptr->CMD |= (cmd & CDP_CMDMSK);
         sim_activate(uptr, 100);       /* Start unit off */
         uptr->COL = 0;
         uptr->SNS = 0;
         return 0;

    case 3:
         if (cmd != 0x3) {
             uptr->SNS |= SNS_CMDREJ;
             return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
         }
         return SNS_CHNEND|SNS_DEVEND;

    case 0:                /* Status */
         break;

    case 4:                /* Sense */
         uptr->CMD &= ~(CDP_CMDMSK);
         uptr->CMD |= (cmd & CDP_CMDMSK);
         sim_activate(uptr, 100);
         return 0;

    default:              /* invalid command */
         uptr->SNS |= SNS_CMDREJ;
         break;
    }
    if (uptr->SNS & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}


/* Handle transfer of data for card punch */
t_stat
cdp_srv(UNIT *uptr) {
    int       u = uptr-cdp_unit;
    uint16   *image = (uint16 *)(uptr->up7);
    uint16    addr = GET_UADDR(uptr->CMD);

    /* Handle sense */
    if ((uptr->CMD & CDP_CMDMSK) == 0x4) {
         uint8 ch = uptr->SNS;
         uptr->CMD &= ~(CDP_CMDMSK);
         chan_write_byte(addr, &ch);
         chan_end(addr, SNS_DEVEND|SNS_CHNEND);
         return SCPE_OK;
    }

    if (uptr->CMD & CDP_CARD) {
        /* Done waiting, punch card */
        uptr->CMD &= ~CDP_CARD;
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
    if (uptr->COL < 80) {
        uint8               ch = 0;

        if (chan_read_byte(addr, &ch)) {
            uptr->CMD |= CDP_CARD;
        } else {
            sim_debug(DEBUG_DATA, &cdp_dev, "%d: Char < %02x\n", u, ch);
            image[uptr->COL++] = sim_ebcdic_to_hol(ch);
            if (uptr->COL == 80) {
                uptr->CMD |= CDP_CARD;
            }
        }
        if (uptr->CMD & CDP_CARD) {
            uptr->CMD &= ~(CDP_CMDMSK);
            chan_end(addr, SNS_CHNEND);
            sim_activate(uptr, 80000);
        } else
            sim_activate(uptr, 100);
    }
    return SCPE_OK;
}


t_stat
cdp_attach(UNIT * uptr, CONST char *file)
{
    if (uptr->up7 == 0) {
        uptr->up7 = calloc(80, sizeof(uint16));
        uptr->SNS = 0;
    }
    return sim_card_attach(uptr, file);
}

t_stat
cdp_detach(UNIT * uptr)
{
    uint16   *image = (uint16 *)(uptr->up7);

    if (uptr->SNS & CDP_CARD)
        sim_punch_card(uptr, image);
    if (uptr->up7 != 0)
        free(uptr->up7);
    uptr->up7 = 0;
    return sim_card_detach(uptr);
}

t_stat
cdp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "2540P Card Punch\n\n");
   sim_card_attach_help(st, dptr, uptr, flag, cptr);
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cdp_description(DEVICE *dptr)
{
   return "2540P Card Punch";
}
#endif

