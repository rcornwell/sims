/* icl1900_cr.c: ICL1900 Punch Card Reader simulator

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
#include "sim_card.h"

#ifndef NUM_DEVS_CDP
#define NUM_DEVS_CDP 0
#endif

#define UNIT_V_TYPE      (UNIT_V_UF + 7)
#define UNIT_TYPE        (0xf << UNIT_V_TYPE)
#define GET_TYPE(x)      ((UNIT_TYPE & (x)) >> UNIT_V_TYPE)
#define SET_TYPE(x)      (UNIT_TYPE & ((x) << UNIT_V_TYPE))

#define NSI_TYPE(x)      ((GET_TYPE(x) & 1) == 0)
#define  SI_TYPE(x)      ((GET_TYPE(x) & 1) != 0)


#define STATUS       u3

#define TERMINATE    000001
#define OPAT         000002
#define STOPPED      000030
#define ERROR        000004
#define DISC         010000
#define BUSY         020000


#if (NUM_DEVS_CDP > 0)

#define T1920_1               0
#define T1920_2               1

#define UNIT_CDP(x)      UNIT_ADDR(x)|SET_TYPE(T1920_2)|UNIT_ATTABLE|UNIT_DISABLE| \
                          MODE_029


void cdp_cmd (uint32 dev, uint32 cmd, uint32 *resp);
void cdp_nsi_cmd (uint32 dev, uint32 cmd);
void cdp_nsi_status (uint32 dev, uint32 *resp);
t_stat cdp_svc (UNIT *uptr);
t_stat cdp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
CONST char *cdp_description (DEVICE *dptr);

DIB cdp_dib = {  CHAR_DEV, &cdp_cmd, &cdp_nsi_cmd, &cdp_nsi_status };

UNIT cdp_unit[] = {
    { UDATA (&cdp_svc, UNIT_CDP(12), 0), 10000 },
    { UDATA (&cdp_svc, UNIT_CDP(13), 0), 10000 },
    };


MTAB cdp_mod[] = {
    { UNIT_TYPE, SET_TYPE(T1920_1), "1920/1", "1920/1", NULL, NULL, NULL, "ICL 1920/1 NSI card punch."},
    { UNIT_TYPE, SET_TYPE(T1920_2), "1920/2", "1920/2", NULL, NULL, NULL, "ICL 1920/2 SI card punch."},
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_chan, &get_chan, NULL, "Device Number"},
    { 0 }
    };

DEVICE cdp_dev = {
    "CP", cdp_unit, NULL, cdp_mod,
    NUM_DEVS_CDP, 8, 22, 1, 8, 22,
    NULL, NULL, NULL, NULL, &sim_card_attach, &sim_card_detach,
    &cdp_dib, DEV_DISABLE | DEV_CARD | DEV_DEBUG, 0, card_debug,
    NULL, NULL, &cdp_help, NULL, NULL, &cdp_description
    };

/*
 * Command codes
 *
 * 011010          Punch
 * 010000          Send Q.
 * 010100          Send P.
 * 011110          Disconnect.
 */


void cdp_cmd(uint32 dev, uint32 cmd, uint32 *resp) {
   uint32   i;
   UNIT    *uptr = NULL;

   *resp = 0;
   /* Find the unit from dev */
   for (i = 0; i < cdp_dev.numunits; i++) {
       if (GET_UADDR(cdp_unit[i].flags) == dev) {
           uptr = &cdp_unit[i];
           break;
       }
   }

   /* Should not happen, but just in case */
   if (uptr == NULL)
       return;

   /* Ignore this command if not a SI device */
   if (NSI_TYPE(uptr->flags))
       return;

   if ((uptr->flags & UNIT_ATT) == 0)
        return;
   if (cmd == 020) {    /* Send Q */
        *resp = uptr->STATUS & TERMINATE; /* TERMINATE */
        if ((uptr->flags & UNIT_ATT) == 0 || (uptr->STATUS & 07700) == 0)
            *resp |= 040;
	if ((uptr->flags & BUSY) == 0)
            *resp |= STOPPED;
        sim_debug(DEBUG_STATUS, &cdp_dev, "STATUS: %02o %02o\n", cmd, *resp);
        uptr->STATUS &= ~TERMINATE;
        chan_clr_done(dev);
   } else if (cmd == 024) {  /* Send P */
        *resp = uptr->STATUS & 016;  /* IMAGE, ERROR, OPAT */
        if ((uptr->flags & UNIT_ATT) != 0)
            *resp |= 1;
        uptr->STATUS &= (BUSY|DISC);
        sim_debug(DEBUG_STATUS, &cdp_dev, "STATUS: %02o %02o\n", cmd, *resp);
   } else if (cmd == 032) {
        if ((uptr->flags & UNIT_ATT) == 0)
            return;
        if (uptr->STATUS & BUSY) {
            *resp = 3;
            return;
        }
        sim_debug(DEBUG_CMD, &cdp_dev, "CMD: %02o %08o\n", cmd, uptr->STATUS);
        uptr->STATUS = BUSY;
        sim_activate(uptr, uptr->wait);
        chan_clr_done(dev);
        *resp = 5;
   } else if (cmd == 036) {  /* Disconnect */
        uptr->STATUS |= DISC;
        *resp = 5;
   }
}

/*
 * Command codes
 *
 * xxxx01     Start punch.
 * xxxx10     Stop punch.
 */
void cdp_nsi_cmd(uint32 dev, uint32 cmd) {
   uint32   i;
   UNIT    *uptr = NULL;

   /* Find the unit from dev */
   for (i = 0; i < cdp_dev.numunits; i++) {
       if (GET_UADDR(cdp_unit[i].flags) == dev) {
           uptr = &cdp_unit[i];
           break;
       }
   }

   /* Should not happen, but just in case */
   if (uptr == NULL)
       return;

   /* Ignore this command if not a SI device */
   if (SI_TYPE(uptr->flags))
       return;


   if (cmd & 02) {
       if (uptr->STATUS & BUSY)
           uptr->STATUS |= DISC;
       sim_debug(DEBUG_CMD, &cdp_dev, "Stop: %02o %08o\n", cmd, uptr->STATUS);
       return;
   }

   if (cmd & 01) {
       if (uptr->STATUS & BUSY || (uptr->flags & UNIT_ATT) == 0) {
           uptr->STATUS |= OPAT;
           chan_set_done(dev);
           return;
       }
       uptr->STATUS = BUSY;
       sim_debug(DEBUG_CMD, &cdp_dev, "Start: %02o %08o\n", cmd, uptr->STATUS);
       chan_clr_done(dev);
       sim_activate(uptr, uptr->wait);
   }
}

/*
 * NSI Status bits.
 *
 *  001   End.
 *  002   Opat.
 *  004   ERROR
 *  020   ACCEPT
 *  040   BUSY
 */
void cdp_nsi_status(uint32 dev, uint32 *resp) {
   uint32   i;
   UNIT    *uptr = NULL;

   *resp = 0;
   /* Find the unit from dev */
   for (i = 0; i < cdp_dev.numunits; i++) {
       if (GET_UADDR(cdp_unit[i].flags) == dev) {
           uptr = &cdp_unit[i];
           break;
       }
   }

   /* Should not happen, but just in case */
   if (uptr == NULL)
       return;

   /* Ignore this command if not a SI device */
   if (SI_TYPE(uptr->flags))
       return;

   *resp = uptr->STATUS & 3;
   if (uptr->STATUS & BUSY)
       *resp |= 040;
   if ((uptr->flags & UNIT_ATT) == 0) 
       *resp |= 2;
   uptr->STATUS &= BUSY|DISC;
   sim_debug(DEBUG_STATUS, &cdp_dev, "STATUS: %02o\n", *resp);
   chan_clr_done(dev);
}

t_stat cdp_svc (UNIT *uptr)
{
    int     dev = GET_UADDR(uptr->flags);
    uint16  image[80];
    uint8   ch;
    int     eor;
    int     i;

    /* Handle a disconnect request */
    if (uptr->STATUS & DISC) {
       uptr->STATUS |= TERMINATE;
       uptr->STATUS &= ~(BUSY|DISC);
       chan_set_done(dev);
       return SCPE_OK;
    }
    /* If not busy, false schedule, just exit */
    if ((uptr->STATUS & BUSY) == 0)
        return SCPE_OK;


    memset(&image, 80, sizeof(uint16));
    for (i = 0; i < 80; i++) {
        eor = chan_output_char(dev, &ch, 0);
        if (eor) {
           break;
        }
        image[i] = mem_to_hol[ch];
        sim_debug(DEBUG_DATA, &cdp_dev, "Data: %02o %04x\n", ch, image[i]);
    }
    switch(sim_punch_card(uptr, image)) {
    case CDSE_EMPTY:
    case CDSE_EOF:
    case CDSE_ERROR:
         uptr->STATUS |= OPAT;
         break;
    case CDSE_OK:
         break;
    }
    uptr->STATUS |= TERMINATE;
    uptr->STATUS &= ~(BUSY|DISC);
    chan_set_done(dev);
    return SCPE_OK;
}


t_stat cdp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "The Card Punch can be set to one of several types.\n\n");
    sim_card_attach_help(st, dptr, uptr, flag, cptr);
    fprintf (st, "The device number can be set with DEV=# command.\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);

    return SCPE_OK;
}

CONST char *cdp_description (DEVICE *dptr)
{
    return "CP"; 
}
#endif
