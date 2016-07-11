/* ka10_cdp.c: PDP10 Card Punch

   Copyright (c) 2016, Richard Cornwell

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

#include "ka10_defs.h"
#include "sim_card.h"
#include "sim_defs.h"
#ifdef NUM_DEVS_CDP

#define UNIT_CDP        UNIT_ATTABLE | UNIT_DISABLE | MODE_026

#define CDP_DEVNUM        0110


/* std devices. data structures

   cdp_dev      Card Punch device descriptor
   cdp_unit     Card Punch unit descriptor
   cdp_reg      Card Punch register list
   cdp_mod      Card Punch modifiers list
*/

/* CONO Bits */
#define SET_DATA_REQ    0000010
#define CLR_DATA_REQ    0000020
#define SET_PUNCH_ON    0000040
#define CLR_END_CARD    0000100
#define EN_END_CARD     0000200
#define DIS_END_CARD    0000400
#define CLR_ERROR       0001000
#define EN_TROUBLE      0002000
#define DIS_TROUBLE     0004000
#define EJECT           0010000     /* Finish punch and eject */
#define OFFSET_CARD     0040000     /* Offset card stack */
#define CLR_PUNCH       0100000     /* Clear Trouble, Error, End */

/* CONI Bits */
#define PIA             0000007
#define DATA_REQ        0000010
#define PUNCH_ON        0000040
#define END_CARD        0000100    /* Eject or column 80 */
#define END_CARD_EN     0000200
#define CARD_IN_PUNCH   0000400    /* Card ready to punch */
#define ERROR           0001000    /* Punch error */
#define TROUBLE_EN      0002000
#define TROUBLE         0004000    /* Bit 18,22,23, or 21 */
#define EJECT_FAIL      0010000    /* Could not eject card 23 */
#define PICK_FAIL       0020000    /* Could not pick up card 22 */
#define NEED_OPR        0040000    /* Hopper empty, chip full 21 */
#define HOPPER_LOW      0100000    /* less 200 cards 20 */
#define TEST            0400000    /* In test mode 18 */

/* Device status information stored in u5 */
#define URCSTA_EOF      0001    /* Hit end of file */
#define URCSTA_ERR      0002    /* Error reading record */
#define URCSTA_CARD     0004    /* Unit has card in buffer */
#define URCSTA_FULL     0004    /* Unit has full buffer */
#define URCSTA_BUSY     0010    /* Device is busy */

t_stat              cdp_devio(uint32 dev, uint64 *data);
void                cdp_ini(UNIT *, t_bool);
t_stat              cdp_srv(UNIT *);
t_stat              cdp_reset(DEVICE *);
t_stat              cdp_attach(UNIT *, CONST char *);
t_stat              cdp_detach(UNIT *);
t_stat              cdp_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cdp_description(DEVICE *dptr);


DIB cdp_dib = { CDP_DEVNUM, 1, cdp_devio};

UNIT                cdp_unit[] = {
    {UDATA(cdp_srv, UNIT_S_CHAN(CHAN_CHUREC) | UNIT_CDP, 0), 600},      /* A */
};

MTAB                cdp_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},    
    {0}
};

DEVICE              cdp_dev = {
    "CP", cdp_unit, NULL, cdp_mod,
    NUM_DEVS_CDP, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &cdp_attach, &cdp_detach,
    &cdp_dib, DEV_DISABLE | DEV_DEBUG, 0, crd_debug,
    NULL, NULL, &cdp_help, NULL, NULL, &cdp_description
};




/* Card punch routine
*/

t_stat cdp_devio(uint32 dev, uint64 *data) {
     uint64              res;
     DEVICE              *dptr = &cdp_dev[0];
     UNIT                *uptr = &cdp_unit[0]; 
     struct _card_data   *dp;
#define PIA             0000007
#define DATA_REQ        0000010
#define PUNCH_ON        0000040
#define END_CARD        0000100    /* Eject or column 80 */
#define END_CARD_EN     0000200
#define CARD_IN_PUNCH   0000400    /* Card ready to punch */
#define ERROR           0001000    /* Punch error */
#define TROUBLE_EN      0002000
#define TROUBLE         0004000    /* Bit 18,22,23, or 21 */
#define EJECT_FAIL      0010000    /* Could not eject card 23 */
#define PICK_FAIL       0020000    /* Could not pick up card 22 */
#define NEED_OPR        0040000    /* Hopper empty, chip full 21 */
#define HOPPER_LOW      0100000    /* less 200 cards 20 */
#define TEST            0400000    /* In test mode 18 */

     switch(dev & 3) {
     case CONI:
        *data = uptr->u3;
        break;
     case CONO:
         uptr->u3 &= ~7;
         uptr->u3 |= *data & (PUNCH_ON|07);
         clr_interrupt(dev);
         if (*data & CLR_PUNCH) {
             uptr->u3 &= ~(TROUBLE|ERROR|END_CARD|END_CARD_EN|TROUBLE_EN);
         }
         if (
#define SET_DATA_REQ    0000010
#define CLR_DATA_REQ    0000020
#define SET_PUNCH_ON    0000040
#define CLR_END_CARD    0000100
#define EN_END_CARD     0000200
#define DIS_END_CARD    0000400
#define CLR_ERROR       0001000
#define EN_TROUBLE      0002000
#define DIS_TROUBLE     0004000
#define EJECT           0010000     /* Finish punch and eject */
#define OFFSET_CARD     0040000     /* Offset card stack */
#define CLR_PUNCH       0100000     /* Clear Trouble, Error, End */
         if (uptr->u3 & PUNCH_ON) {
             sim_activate(uptr, uptr->wait);
         }
         break;
     case DATAI:
         *data = 0;
         break;
    case DATAO:
         dp = (struct _card_data *)uptr->up7;
         dp->image[uptr->u4++] = *data & 0xfff;
         uptr->u3 &= ~DATA_REQ;
         clr_interrupt(dev);
         sim_activate(uptr, uptr->wait);
         break;
    }
    return SCPE_OK;
}

/* Handle transfer of data for card punch */
t_stat
cdp_srv(UNIT *uptr) {
    int                 u = (uptr - cdp_unit);


    if (uptr->u5 & URCSTA_BUSY) {
        /* Done waiting, punch card */
        if (uptr->u5 & URCSTA_FULL) {
              switch(sim_punch_card(uptr, NULL)) {
              case SCPE_EOF:
              case SCPE_UNATT:
                  chan_set_eof(chan);
                  break;
                 /* If we get here, something is wrong */
              case SCPE_IOERR:
                  chan_set_error(chan);
                  break;
              case SCPE_OK:     
                  break;
              }
              uptr->u5 &= ~URCSTA_FULL;
        }
        uptr->u5 &= ~URCSTA_BUSY;
    }

    /* Copy next column over */
    if (uptr->u5 & URCSTA_WRITE && uptr->u4 < 80) {
        struct _card_data   *data;
        uint8               ch = 0;

        data = (struct _card_data *)uptr->up7;

        sim_activate(uptr, uptr->wait);
    }
    return SCPE_OK;
}


void
cdp_ini(UNIT *uptr, t_bool f) {
}

t_stat
cdp_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    uptr->u5 = 0;
    return SCPE_OK;
}

t_stat
cdp_detach(UNIT * uptr)
{
    if (uptr->u5 & URCSTA_FULL) 
        sim_punch_card(uptr, NULL);
    return sim_card_detach(uptr);
}

t_stat
cdp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "Card Punch\n\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cdp_description(DEVICE *dptr)
{
   return "Card Punch";
}

#endif

