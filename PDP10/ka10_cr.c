/* ka10_cdr.c: PDP10 Card reader.

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
#ifdef NUM_DEVS_CDR

#define UNIT_CDR        UNIT_ATTABLE | UNIT_RO | UNIT_DISABLE | \
                         UNIT_ROABLE | MODE_026

#define CDR_DEVNUM        0150


/* std devices. data structures

   cdr_dev      Card Reader device descriptor
   cdr_unit     Card Reader unit descriptor
   cdr_reg      Card Reader register list
   cdr_mod      Card Reader modifiers list
*/

/* CONO Bits */
#define PIA             0000007
#define CLR_DRDY        0000010    /* Clear data ready */
#define CLR_END_CARD    0000020    /* Clear end of card */
#define CLR_EOF         0000040    /* Clear end of File Flag */
#define EN_READY        0000100    /* Enable ready irq */
#define CLR_DATA_MISS   0000200    /* Clear data miss */
#define EN_TROUBLE      0000400    /* Enable trouble IRQ */
#define READ_CARD       0001000    /* Read in card */
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
#define CARD_READ       00004000    /* Card in reader */
#define STOP            00010000
#define MOTION_ERROR    00020000
#define CELL_ERROR      00040000
#define PICK_ERROR      00100000
#define RDY_READ_EN     00200000
#define TROUBLE_EN      00400000
#define READ_CARD       01000000


/* Device status information stored in u5 */
#define URCSTA_EOF      0001    /* Hit end of file */
#define URCSTA_ERR      0002    /* Error reading record */
#define URCSTA_CARD     0004    /* Unit has card in buffer */
#define URCSTA_FULL     0004    /* Unit has full buffer */
#define URCSTA_BUSY     0010    /* Device is busy */

t_stat              cdr_devio(uint32 dev, uint64 *data);
t_stat              cdr_srv(UNIT *);
t_stat              cdr_reset(DEVICE *);
t_stat              cdr_attach(UNIT *, CONST char *);
t_stat              cdr_detach(UNIT *);
t_stat              cdr_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cdr_description(DEVICE *dptr);

DIB cdr_dib = { CDR_DEVNUM, 1, cdr_devio};

UNIT                cdr_unit[] = {
   {UDATA(cdr_srv, UNIT_S_CHAN(CHAN_CHUREC) | UNIT_CDR, 0), 300},       /* A */
};

MTAB                cdr_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},    
    {0}
};

DEVICE              cdr_dev = {
    "CR", cdr_unit, NULL, cdr_mod,
    NUM_DEVS_CDR, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &cdr_attach, &sim_card_detach,
    &cdr_dib, DEV_DISABLE | DEV_DEBUG, 0, crd_debug,
    NULL, NULL, &cdr_help, NULL, NULL, &cdr_description
};


/*
 * Device entry points for card reader.
 */
t_stat cdr_devio(uint32 dev, uint64 *data) {
    UNIT *uptr = &cdr_unit;
    switch(dev & 3) {
    case CONI:
         *data = uptr->STATUS;
         break;

    case CONO:
         clr_interrupt(dev);
         fprintf(stderr, "PT: CONO %012llo\n\r", *data);
         uptr->STATUS = (PI_DONE|DONE_FLG|BUSY_FLG|BIN_FLG) & *data;
         if ((uptr->flags & UNIT_ATT)) 
             uptr->STATUS |= TAPE_PR;
         if (uptr->STATUS & BUSY_FLG) {
             uptr->CHR = 0;
             uptr->CHL = 0;
             sim_activate (&ptr_unit, ptr_unit.wait);
         }
         if (uptr->STATUS & DONE_FLG) 
             set_interrupt(dev, uptr->STATUS);
         break;

    case DATAI:
         if ((uptr->STATUS & DONE_FLG)) {
             *data = ((uint64)uptr->CHL) << 18;
             *data |= ((uint64)uptr->CHR);
         fprintf(stderr, "PT: DATAI %012llo\n\r", *data);
             uptr->STATUS |= BUSY_FLG;
             uptr->STATUS &= ~DONE_FLG;
             clr_interrupt(dev);
             sim_activate (&ptr_unit, ptr_unit.wait);
         }
         break;
    case DATAO:
         break;
    }
    return SCPE_OK;
}

/* Handle transfer of data for card reader */
t_stat
cdr_srv(UNIT *uptr) {
    int                 u = (uptr - cdr_unit);
    struct _card_data   *data;

    data = (struct _card_data *)uptr->up7;

    if (uptr->u3 & URCSTA_BUSY) {
        uptr->u3 &= ~URCSTA_BUSY;
    }

    /* Check if new card requested. */
    if (uptr->u4 == 0 && 
           (uptr->u3 & (READ_CARD|CARD_READ)) == READ_CARD) {
        switch(sim_read_card(uptr)) {
        case SCPE_EOF:
             uptr->u3 |= END_FILE;
             return SCPE_OK;
        case SCPE_UNATT:
             return SCPE_OK;
        case SCPE_IOERR:
             uptr->u3 |= TROUBLE;
             return SCPE_OK;
        case SCPE_OK:   
             uptr->u3 |= CARD_READ;
             uptr->u3 &= ~READ_CARD;
             break;
        }
    }

    /* Copy next column over */
    if (uptr->u3 & CARD_READ) {
        if (uptr->u4 >= 80) {
             uptr->u3 |= END_CARD;
        }
        uptr->u5 = data->image[uptr->u4++];
        uptr->u3 |= DATA_RDY;
        sim_debug(DEBUG_DATA, &cdr_dev, "%d: Char > %03x\n", u, uptr->u5);
        sim_activate(uptr, uptr->wait);
    }
    return SCPE_OK;
}

t_stat
cdr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
        return r;
    uptr->u3 &= URCSTA_BUSY;
    uptr->u4 = 0;
    uptr->u6 = 0;
    return SCPE_OK;
}

t_stat
cdr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "Card Reader\n\n");
   fprintf (st, "The system supports one card reader.\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cdr_description(DEVICE *dptr)
{
   return "Card Reader";
}

#endif

