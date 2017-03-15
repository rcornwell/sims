/* ka10_cr.c: PDP10 Card reader.

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

   This is the standard card reader.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "ka10_defs.h"
#include "sim_card.h"
#include "sim_defs.h"
#if (NUM_DEVS_CR > 0)

#define UNIT_CDR        UNIT_ATTABLE | UNIT_RO | UNIT_DISABLE | \
                         UNIT_ROABLE | MODE_029

#define CR_DEVNUM        0150


/* std devices. data structures

   cr_dev      Card Reader device descriptor
   cr_unit     Card Reader unit descriptor
   cr_reg      Card Reader register list
   cr_mod      Card Reader modifiers list
*/

/* CONO Bits */
#define PIA             0000007
#define CLR_DRDY        0000010    /* Clear data ready */
#define CLR_END_CARD    0000020    /* Clear end of card */
#define CLR_EOF         0000040    /* Clear end of File Flag */
#define EN_READY        0000100    /* Enable ready irq */
#define CLR_DATA_MISS   0000200    /* Clear data miss */
#define EN_TROUBLE      0000400    /* Enable trouble IRQ */
#define READ_CARD       0001000    /* Read card */
#define OFFSET_CARD     0004000
#define CLR_READER      0010000    /* Clear reader */
/* CONI Bits */
#define DATA_RDY        00000010    /* Data ready */
#define END_CARD        00000020    /* End of card */
#define END_FILE        00000040    /* End of file */
#define RDY_READ        00000100    /* Ready to read */
#define DATA_MISS       00000200    /* Data missed */
#define TROUBLE         00000400    /* Trouble */
#define READING         00001000    /* Reading card */
#define HOPPER_EMPTY    00002000
#define CARD_IN_READ    00004000    /* Card in reader */
#define STOP            00010000
#define MOTION_ERROR    00020000
#define CELL_ERROR      00040000
#define PICK_ERROR      00100000
#define RDY_READ_EN     00200000
#define TROUBLE_EN      00400000

t_stat              cr_devio(uint32 dev, uint64 *data);
t_stat              cr_srv(UNIT *);
t_stat              cr_reset(DEVICE *);
t_stat              cr_attach(UNIT *, CONST char *);
t_stat              cr_detach(UNIT *);
t_stat              cr_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cr_description(DEVICE *dptr);

DIB cr_dib = { CR_DEVNUM, 1, cr_devio, NULL};

UNIT                cr_unit = {
   UDATA(cr_srv, UNIT_CDR, 0), 300,
};

MTAB                cr_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},
    {0}
};

DEVICE              cr_dev = {
    "CR", &cr_unit, NULL, cr_mod,
    NUM_DEVS_CR, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &cr_attach, &sim_card_detach,
    &cr_dib, DEV_DISABLE | DEV_DEBUG, 0, crd_debug,
    NULL, NULL, &cr_help, NULL, NULL, &cr_description
};


/*
 * Device entry points for card reader.
 */
t_stat cr_devio(uint32 dev, uint64 *data) {
    UNIT *uptr = &cr_unit;
    switch(dev & 3) {
    case CONI:
         if (uptr->flags & UNIT_ATT &&
                (uptr->u3 & (READING|CARD_IN_READ|END_CARD)) == 0)
            uptr->u3 |= RDY_READ;
         *data = uptr->u3;
         if (uptr->u3 & RDY_READ_EN && uptr->u3 & RDY_READ)
             set_interrupt(dev, uptr->u3);
         sim_debug(DEBUG_CONI, &cr_dev, "CR: CONI %012llo\n", *data);
         break;

    case CONO:
         clr_interrupt(dev);
         sim_debug(DEBUG_CONO, &cr_dev, "CR: CONO %012llo\n", *data);
         if (*data & CLR_READER) {
            uptr->u3 = 0;
            sim_cancel(uptr);
            break;
         }
         uptr->u3 &= ~(PIA);
         uptr->u3 |= *data & PIA;
         uptr->u3 &= ~(*data & (CLR_DRDY|CLR_END_CARD|CLR_EOF|CLR_DATA_MISS));
         if (*data & EN_TROUBLE)
             uptr->u3 |= TROUBLE_EN;
         if (*data & EN_READY)
             uptr->u3 |= RDY_READ_EN;
         if (*data & READ_CARD) {
             uptr->u3 |= READING;
             uptr->u3 &= ~(CARD_IN_READ|RDY_READ|DATA_RDY);
             uptr->u4 = 0;
             sim_activate(uptr, uptr->wait);
         }
         if (uptr->flags & UNIT_ATT &&
                (uptr->u3 & (READING|CARD_IN_READ|END_CARD)) == 0)
            uptr->u3 |= RDY_READ;
         if (uptr->u3 & RDY_READ_EN && uptr->u3 & RDY_READ)
             set_interrupt(dev, uptr->u3);
         if (uptr->u3 & TROUBLE_EN &&
             (uptr->u3 & (END_CARD|END_FILE|DATA_MISS|TROUBLE)) != 0)
             set_interrupt(dev, uptr->u3);
         break;

    case DATAI:
         clr_interrupt(dev);
         if (uptr->u3 & DATA_RDY) {
             *data = uptr->u5;
             sim_debug(DEBUG_DATAIO, &cr_dev, "CR: DATAI %012llo\n", *data);
             uptr->u3 &= ~DATA_RDY;
         } else
             *data = 0;
         break;
    case DATAO:
         break;
    }
    return SCPE_OK;
}

/* Handle transfer of data for card reader */
t_stat
cr_srv(UNIT *uptr) {
    struct _card_data   *data;

    data = (struct _card_data *)uptr->up7;

    /* Check if new card requested. */
    if ((uptr->u3 & (READING|CARD_IN_READ)) == READING) {
        switch(sim_read_card(uptr)) {
        case SCPE_EOF:
             uptr->u3 |= END_FILE;
             if (uptr->u3 & TROUBLE_EN)
                 set_interrupt(CR_DEVNUM, uptr->u3);
             return SCPE_OK;
        case SCPE_UNATT:
             return SCPE_OK;
        case SCPE_IOERR:
             uptr->u3 |= TROUBLE;
             if (uptr->u3 & TROUBLE_EN)
                 set_interrupt(CR_DEVNUM, uptr->u3);
             return SCPE_OK;
        case SCPE_OK:
             uptr->u3 |= CARD_IN_READ;
             break;
        }
        uptr->u4 = 0;
        sim_activate(uptr, uptr->wait);
        return SCPE_OK;
    }

    /* Copy next column over */
    if (uptr->u3 & CARD_IN_READ) {
        if (uptr->u4 >= 80) {
             uptr->u3 &= ~(CARD_IN_READ|READING);
             uptr->u3 |= END_CARD;
             set_interrupt(CR_DEVNUM, uptr->u3);
             sim_activate(uptr, uptr->wait);
             return SCPE_OK;
        }
        uptr->u5 = data->image[uptr->u4++];
        if (uptr->u3 & DATA_RDY) {
            uptr->u3 |= DATA_MISS;
        }
        uptr->u3 |= DATA_RDY;
        sim_debug(DEBUG_DATA, &cr_dev, "CR Char > %d %03x\n", uptr->u4, uptr->u5);
        set_interrupt(CR_DEVNUM, uptr->u3);
        sim_activate(uptr, uptr->wait);
    }
    return SCPE_OK;
}

t_stat
cr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    uptr->u3 |= RDY_READ;
    return SCPE_OK;
}

t_stat
cr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "Card Reader\n\n");
   fprintf (st, "The system supports one card reader.\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cr_description(DEVICE *dptr)
{
   return "Card Reader";
}

#endif

