/* ibm360_cdr.c: IBM 360 Card Reader.

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

   cdr_dev       Card Reader device descriptor
   cdr_unit      Card Reader unit descriptor
   cdr_reg       Card Reader register list
   cdr_mod       Card Reader modifiers list
*/




uint8               cdr_startcmd(UNIT *, uint16,  uint8);
t_stat              cdr_boot(int32, DEVICE *);
t_stat              cdr_srv(UNIT *);
t_stat              cdr_reset(DEVICE *);
t_stat              cdr_attach(UNIT *, CONST char *);
t_stat              cdr_detach(UNIT *);



UNIT                cdr_unit[] = {
   {UDATA(cdr_srv, UNIT_CDR, 0), 300, UNIT_ADDR(0x0C)},       /* A */
#if NUM_DEVS_CDR > 1
   {UDATA(cdr_srv, UNIT_CDR, 0), 300, UNIT_ADDR(0x1C)},       /* B */
#endif
};

MTAB                cdr_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {0}
};

struct dib cdr_dib = { 0xFF, 1, NULL, cdr_startcmd, NULL, cdr_unit};

DEVICE              cdr_dev = {
    "CDR", cdr_unit, NULL, cdr_mod,
    NUM_DEVS_CDR, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, &cdr_boot, &cdr_attach, &cdr_detach,
    &cdr_dib, DEV_UADDR | DEV_DISABLE | DEV_DEBUG | DEV_CARD, 0, crd_debug
};


uint8  cdr_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) {
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);
    uint8          ch;

    if ((uptr->u3 & CDR_CMDMSK) != 0) {
        if ((uptr->flags & UNIT_ATT) != 0)
            return SNS_BSY;
        return SNS_DEVEND;
    }

    sim_debug(DEBUG_CMD, dptr, "CMD unit=%d %x\n", unit, cmd);
    switch (cmd & 0x7) {
    case 2:              /* Read command */
         if ((cmd & 0xc0) != 0xc0)
             uptr->u3 &= ~CDR_CARD;
         uptr->u3 &= ~(CDR_CMDMSK);
         uptr->u3 |= (cmd & CDR_CMDMSK);
         sim_activate(uptr, 1000);       /* Start unit off */
         uptr->u4 = 0;
         uptr->u5 = 0;
         return 0;

    case 3:              /* Control */
         uptr->u5 = 0;
         uptr->u3 &= ~(CDR_CMDMSK|CDR_CARD);
         if (cmd == 0x3)
             return SNS_CHNEND|SNS_DEVEND;
         if ((cmd & 0x30) != 0x20 || (cmd & 0xc0) == 0xc0) {
             uptr->u5 |= SNS_CMDREJ;
             return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
         }
         uptr->u3 |= (cmd & CDR_CMDMSK);
         uptr->u4 = 0;
         sim_activate(uptr, 1000);       /* Start unit off */
         return 0;

    case 0:               /* Status */
         break;

    case 4:               /* Sense */
         uptr->u3 &= ~(CDR_CMDMSK);
         uptr->u3 |= (cmd & CDR_CMDMSK);
         sim_activate(uptr, 10);
         return 0;

    default:              /* invalid command */
         uptr->u5 |= SNS_CMDREJ;
         break;
    }

    if (uptr->u5)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle transfer of data for card reader */
t_stat
cdr_srv(UNIT *uptr) {
    int       addr = GET_UADDR(uptr->u3);
    uint16   *image = (uint16 *)(uptr->up7);

    if ((uptr->u3 & CDR_CMDMSK) == CHN_SNS) {
         uint8 ch = uptr->u5;
         chan_write_byte(addr, &ch);
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         uptr->u3 &= ~(CDR_CMDMSK);
         return SCPE_OK;
    }

    /* Check if new card requested. */
    if ((uptr->u3 & CDR_CARD) == 0) {
       switch(sim_read_card(uptr, image)) {
       case SCPE_EOF:
       case SCPE_UNATT:
            chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
            uptr->u5 = SNS_INTVENT;
            uptr->u3 &= ~CDR_CMDMSK;
            return SCPE_OK;
       case SCPE_IOERR:
            chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            uptr->u5 = SNS_INTVENT;
            uptr->u3 &= ~CDR_CMDMSK;
            return SCPE_OK;
       case SCPE_OK:
            uptr->u3 |= CDR_CARD;
            if ((uptr->u3 & CDR_CMDMSK) == CDR_FEED) {
                chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                uptr->u3 &= ~(CDR_CMDMSK);
                return SCPE_OK;
            }
            break;
       }
    }

    /* Copy next column over */
    if ((uptr->u3 & CDR_CMDMSK) == CDR_RD) {
        int                  u = uptr-cdr_unit;
        uint16               xlat;
        uint8                ch = 0;

        xlat = sim_hol_to_ebcdic(image[uptr->u4]);

        if (xlat == 0x100) {
            uptr->u5 |= SNS_DATCHK;
            ch = 0x00;
        } else
            ch = (uint8)(xlat&0xff);
        if (chan_write_byte(addr, &ch)) {
           uptr->u3 &= ~(CDR_CMDMSK);
           chan_end(addr, SNS_CHNEND|SNS_DEVEND|(uptr->u5 ? SNS_UNITCHK:0));
           return SCPE_OK;
       } else {
           uptr->u4++;
           sim_debug(DEBUG_DATA, &cdr_dev, "%d: Char > %02o\n", u, ch);
        }
        if (uptr->u4 == 80) {
            uptr->u3 &= ~(CDR_CMDMSK);
            chan_end(addr, SNS_CHNEND|SNS_DEVEND|(uptr->u5 ? SNS_UNITCHK:0));
        }
        sim_activate(uptr, 10);
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
cdr_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    t_stat              r;

    if ((uptr->flags & UNIT_ATT) == 0)
       return SCPE_UNATT;       /* attached? */
    return chan_boot(GET_UADDR(uptr->u3), dptr);
}

t_stat
cdr_attach(UNIT * uptr, CONST char *file)
{
    int                 addr = GET_UADDR(uptr->u3);
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
       return r;
    if (uptr->up7 == 0)
        uptr->up7 = malloc(sizeof(uint16)*80);
    set_devattn(addr, SNS_DEVEND);
    uptr->u3 &= ~(CDR_CARD);
    uptr->u4 = 0;
    uptr->u6 = 0;
    return SCPE_OK;
}

t_stat
cdr_detach(UNIT * uptr)
{
    if (uptr->up7 != 0)
        free(uptr->up7);
    uptr->up7 = 0;
    return sim_card_detach(uptr);
}


#endif
