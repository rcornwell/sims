/* i7000_cdp.c: IBM 7000 Card Punch

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

   This is the standard card punch.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "i7000_defs.h"
#include "sim_card.h"
#include "sim_defs.h"
#ifdef NUM_DEVS_CDP

#define UNIT_CDP        UNIT_ATTABLE | UNIT_DISABLE | MODE_026


/* Flags for punch and reader. */
#define ATTENA          (1 << (UNIT_V_UF+7))
#define ATTENB          (1 << (UNIT_V_UF+14))


/* std devices. data structures

   cdp_dev      Card Punch device descriptor
   cdp_unit     Card Punch unit descriptor
   cdp_reg      Card Punch register list
   cdp_mod      Card Punch modifiers list
*/

/* Device status information stored in u5 */
#define URCSTA_EOF      0001    /* Hit end of file */
#define URCSTA_ERR      0002    /* Error reading record */
#define URCSTA_CARD     0004    /* Unit has card in buffer */
#define URCSTA_FULL     0004    /* Unit has full buffer */
#define URCSTA_BUSY     0010    /* Device is busy */
#define URCSTA_WDISCO   0020    /* Device is wait for disconnect */
#define URCSTA_READ     0040    /* Device is reading channel */
#define URCSTA_WRITE    0100    /* Device is reading channel */
#define URCSTA_INPUT    0200    /* Console fill buffer from keyboard */
#define URCSTA_WMKS     0400    /* Printer print WM as 1 */
#define URCSTA_SKIPAFT  01000   /* Skip to line after printing next line */
#define URCSTA_NOXFER   01000   /* Don't set up to transfer after feed */
#define URCSTA_LOAD     01000   /* Load flag for 7070 card reader */

uint32              cdp_cmd(UNIT *, uint16, uint16);
void                cdp_ini(UNIT *, t_bool);
t_stat              cdp_srv(UNIT *);
t_stat              cdp_reset(DEVICE *);
t_stat              cdp_attach(UNIT *, CONST char *);
t_stat              cdp_detach(UNIT *);
t_stat              cdp_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *cdp_description(DEVICE *dptr);

UNIT                cdp_unit[] = {
    {UDATA(cdp_srv, UNIT_S_CHAN(CHAN_CHUREC) | UNIT_CDP, 0), 600},      /* A */
#if NUM_DEVS_CDP > 1
    {UDATA(cdp_srv, UNIT_S_CHAN(CHAN_CHUREC+1) | UNIT_CDP, 0), 600},    /* B */
#endif
};

MTAB                cdp_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},    
#ifdef I7070
    {ATTENA|ATTENB, 0, NULL, "NOATTEN", NULL, NULL, NULL},
    {ATTENA|ATTENB, ATTENA, "ATTENA", "ATTENA", NULL, NULL, NULL},
    {ATTENA|ATTENB, ATTENB, "ATTENB", "ATTENB", NULL, NULL, NULL},
#endif
#ifdef I7010
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "CHAN", "CHAN", &set_chan,
        &get_chan, NULL},
#endif   
    {0}
};

DEVICE              cdp_dev = {
    "CP", cdp_unit, NULL, cdp_mod,
    NUM_DEVS_CDP, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &cdp_attach, &cdp_detach,
    &cdp_dib, DEV_DISABLE | DEV_DEBUG, 0, crd_debug,
    NULL, NULL, &cdp_help, NULL, NULL, &cdp_description
};

#ifdef STACK_DEV
UNIT stack_unit[] = {
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) },
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    { UDATA (NULL, UNIT_DIS, 0) },                      /* unused */
    };

DEVICE stack_dev = {
    "STKR", stack_unit, NULL, NULL,
    NUM_DEVS_CDP * 10, 10, 31, 1, 8, 7,
    NULL, NULL, NULL, NULL, &sim_card_attach, &sim_card_detach
    };
#endif



/* Card punch routine

   Modifiers have been checked by the caller
   C modifier is recognized (column binary is implemented)
*/


uint32 cdp_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - cdp_unit);
    int                 stk = dev & 017;

    /* Are we currently tranfering? */
    if (uptr->u5 & URCSTA_WRITE)
        return SCPE_BUSY;

    if (stk == 10)
        stk = 0;
    if ((uptr->flags & UNIT_ATT) == 0) {
#ifdef STACK_DEV
        if ((stack_unit[stk * u].flags & UNIT_ATT) == 0)
#endif
        return SCPE_IOERR;
    }

    switch(cmd) {
    /* Test ready */
    case IO_TRS:
        sim_debug(DEBUG_CMD, &cdp_dev, "%d: Cmd TRS\n", u);
        return SCPE_OK;

    /* Suppress punch */
    case IO_RUN:
        uptr->u5 &= ~URCSTA_FULL;
        sim_debug(DEBUG_CMD, &cdp_dev, "%d: Cmd RUN\n", u);
        return SCPE_OK;

    /* Retieve data from CPU */
    case IO_WRS:
#ifdef STACK_DEV
        uptr->u5 &= ~0xF0000;
        uptr->u5 |= stk << 16;
#endif
        sim_debug(DEBUG_CMD, &cdp_dev, "%d: Cmd WRS\n", u);
        chan_set_sel(chan, 1);
        uptr->u5 |= URCSTA_WRITE;
        uptr->u4 = 0;
        if ((uptr->u5 & URCSTA_BUSY) == 0)
            sim_activate(uptr, 50);
        return SCPE_OK;
    }
    chan_set_attn(chan);
    return SCPE_IOERR;
}

/* Handle transfer of data for card punch */
t_stat
cdp_srv(UNIT *uptr) {
    int                 chan = UNIT_G_CHAN(uptr->flags);
    int                 u = (uptr - cdp_unit);
    /* Waiting for disconnect */
    if (uptr->u5 & URCSTA_WDISCO) {
        if (chan_stat(chan, DEV_DISCO)) {
            chan_clear(chan, DEV_SEL|DEV_WEOR);
            uptr->u5 &= ~ URCSTA_WDISCO;
        } else {
            /* No disco yet, try again in a bit */
            sim_activate(uptr, 50);
            return SCPE_OK;
        }
        /* If still busy, schedule another wait */
        if (uptr->u5 & URCSTA_BUSY)
             sim_activate(uptr, uptr->wait);
    }

    if (uptr->u5 & URCSTA_BUSY) {
        /* Done waiting, punch card */
        if (uptr->u5 & URCSTA_FULL) {
#ifdef STACK_DEV
              switch(sim_punch_card(uptr, 
                        &stack_unit[(u * 10) + ((uptr->u5 >> 16) & 0xf)])) {
#else
              switch(sim_punch_card(uptr, NULL)) {
#endif
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
#ifdef I7070
        switch(uptr->flags & (ATTENA|ATTENB)) {
        case ATTENA: chan_set_attn_a(chan); break;
        case ATTENB: chan_set_attn_b(chan); break;
        }
#endif
#ifdef I7010
        chan_set_attn_urec(chan, cdp_dib.addr);
#endif
    }

    /* Copy next column over */
    if (uptr->u5 & URCSTA_WRITE && uptr->u4 < 80) {
        struct _card_data   *data;
        uint8               ch = 0;

        data = (struct _card_data *)uptr->up7;

#ifdef I7080
        switch(chan_read_char(chan, &ch, 0)) {
#else
        switch(chan_read_char(chan, &ch,
                                (uptr->u4 == 79)? DEV_REOR: 0)) {
#endif
        case TIME_ERROR:
        case END_RECORD:
             uptr->u5 |= URCSTA_WDISCO|URCSTA_BUSY|URCSTA_FULL;
             uptr->u5 &= ~URCSTA_WRITE;
             break;
        case DATA_OK:
            sim_debug(DEBUG_DATA, &cdp_dev, "%d: Char < %02o\n", u, ch);
            data->image[uptr->u4++] = sim_bcd_to_hol(ch);
            break;
        }
        sim_activate(uptr, 10);
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
#ifdef STACK_DEV
        sim_punch_card(uptr, &stack_unit[
                ((uptr - cdp_unit) * 10) + ((uptr->u5 >> 16) & 0xf)]);
#else
        sim_punch_card(uptr, NULL);
#endif
    return sim_card_detach(uptr);
}

t_stat
cdp_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "%s\n\n", cdp_description(dptr));
   sim_card_attach_help(st, dptr, uptr, flag, cptr);
#ifdef STACK_DEV
   fprintf (st, "If the punch device is not attached and instead the %s", stack_dev.name);
   fprintf (st, "device is attached, the cards\n will be sent out to the");
   fprintf (st, "given stacker based on the flag set by the processor."); 
#endif
#ifdef I7070
   fprintf (st, "Unit record devices can be configured to interrupt the CPU on\n");
   fprintf (st, "one of two priority channels A or B, to set this\n\n");
   fprintf (st, "    sim> set cp attena     to set device to raise Atten A\n\n");
#endif
#ifdef I7010
   fprintf (st, "The card punch could be attached to either channel\n\n");
   fprintf (st, "    sim> set cp chan=1     to set the punch on channel 1\n\n");
#endif   
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
cdp_description(DEVICE *dptr)
{
#ifdef I7010
   return "1402 Card Punch";
#endif
#ifdef I7070
   return "7550 Card Punch";
#endif
#ifdef I7080
   return "721 Card Punch";
#endif
}

#endif

