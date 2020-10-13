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

#ifndef NUM_DEVS_CDR
#define NUM_DEVS_CDR 0
#endif

#define UNIT_V_TYPE      (UNIT_V_UF + 7)
#define UNIT_TYPE        (0xf << UNIT_V_TYPE)
#define GET_TYPE(x)      ((UNIT_TYPE & (x)) >> UNIT_V_TYPE)
#define SET_TYPE(x)      (UNIT_TYPE & ((x) << UNIT_V_TYPE))

#define NSI_TYPE(x)      ((GET_TYPE(x) & 1) == 0)
#define  SI_TYPE(x)      ((GET_TYPE(x) & 1) != 0)


#define STATUS       u4


#define TERMINATE    0000001
#define STOPPED      0000030
#define OPAT         0000002
#define ERROR        0000004
#define IMAGE        0000010
#define BUSY         0020
#define DISC         0040


#if (NUM_DEVS_CDR > 0)

#define T1911_1               0
#define T1911_2               1
#define T1912_1               2
#define T1912_2               3

#define UNIT_CDR(x)      UNIT_ADDR(x)|SET_TYPE(T1912_2)|UNIT_ATTABLE|\
                              UNIT_DISABLE|UNIT_RO|MODE_029


void cdr_cmd (uint32 dev, uint32 cmd, uint32 *resp);
void cdr_nsi_cmd (uint32 dev, uint32 cmd);
void cdr_nsi_status (uint32 dev, uint32 *resp);
t_stat cdr_svc (UNIT *uptr);
t_stat cdr_boot (int32 unit_num, DEVICE * dptr);
t_stat cdr_reset (DEVICE *dptr);
t_stat cdr_attach(UNIT * uptr, CONST char *file);
t_stat cdr_detach(UNIT * uptr);
t_stat cdr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
CONST char *cdr_description (DEVICE *dptr);

DIB cdr_dib = {  CHAR_DEV, &cdr_cmd, &cdr_nsi_cmd, &cdr_nsi_status };

UNIT cdr_unit[] = {
    { UDATA (&cdr_svc, UNIT_CDR(10), 0), 10000 },
    { UDATA (&cdr_svc, UNIT_CDR(11), 0), 10000 },
    };


MTAB cdr_mod[] = {
    { UNIT_TYPE, SET_TYPE(T1911_1), "1911/1", "1911/1", NULL, NULL, NULL, "ICL 1911/1 NSI 900CPM reader."},
    { UNIT_TYPE, SET_TYPE(T1911_2), "1911/2", "1911/2", NULL, NULL, NULL, "ICL 1911/2 SI 900CPM reader."},
    { UNIT_TYPE, SET_TYPE(T1912_1), "1912/1", "1912/1", NULL, NULL, NULL, "ICL 1912/1 NSI 300CPM reader."},
    { UNIT_TYPE, SET_TYPE(T1912_2), "1912/2", "1912/2", NULL, NULL, NULL, "ICL 1912/2 SI 900CPM reader."},
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
               &sim_card_set_fmt, &sim_card_show_fmt, NULL},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_chan, &get_chan, NULL, "Device Number"},
    { 0 }
    };

DEVICE cdr_dev = {
    "CR", cdr_unit, NULL, cdr_mod,
    NUM_DEVS_CDR, 8, 22, 1, 8, 22,
    NULL, NULL, &cdr_reset, &cdr_boot, &cdr_attach, &cdr_detach,
    &cdr_dib, DEV_DISABLE | DEV_CARD | DEV_DEBUG, 0, card_debug,
    NULL, NULL, &cdr_help, NULL, NULL, &cdr_description
    };

/*
 * Command codes
 *
 * 011001          Read
 * 011011          Read in image mode.
 * 010000          Send Q.
 * 010100          Send P.
 * 011110          Disconnect.
 */


void cdr_cmd(uint32 dev, uint32 cmd, uint32 *resp) {
   uint32   i;
   UNIT    *uptr = NULL;

   *resp = 0;
   /* Find the unit from dev */
   for (i = 0; i < cdr_dev.numunits; i++) {
       if (GET_UADDR(cdr_unit[i].flags) == dev) {
           uptr = &cdr_unit[i];
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
        *resp = uptr->STATUS & TERMINATE;  /* Terminate */
        if ((uptr->flags & UNIT_ATT) == 0 || (uptr->STATUS & 016) == 0)
            *resp |= 040;
        if ((uptr->STATUS & BUSY) == 0) 
            *resp |= STOPPED;
        sim_debug(DEBUG_STATUS, &cdr_dev, "STATUS: %02o %02o\n", cmd, *resp);
        uptr->STATUS &= ~TERMINATE;
        chan_clr_done(dev);
   } else if (cmd == 024) {  /* Send P */
        *resp = uptr->STATUS & 016;  /* IMAGE, ERROR, OPAT */
        if ((uptr->flags & UNIT_ATT) != 0)
            *resp |= 1;
        uptr->STATUS &= (IMAGE|BUSY|DISC);
        sim_debug(DEBUG_STATUS, &cdr_dev, "STATUS: %02o %02o\n", cmd, *resp);
   } else if (cmd == 031 || cmd == 033 || cmd == 037 ) {
        if ((uptr->flags & UNIT_ATT) == 0)
            return;
        if (uptr->STATUS & BUSY) {
            *resp = 3;
            return;
        }
        uptr->STATUS = BUSY;
        if (cmd & 02)
            uptr->STATUS |= IMAGE;
        sim_activate(uptr, uptr->wait);
        chan_clr_done(dev);
        sim_debug(DEBUG_CMD, &cdr_dev, "CMD: %02o %08o\n", cmd, uptr->STATUS);
        *resp = 5;
   } else if (cmd == 036) {  /* Disconnect */
        uptr->STATUS |= DISC;
        sim_debug(DEBUG_CMD, &cdr_dev, "CMD: %02o %08o\n", cmd, uptr->STATUS);
        *resp = 5;
   }
}

/*
 * Command codes
 *
 * xxxx01     Start reader.
 * xxxx10     Stop reader.
 */
void cdr_nsi_cmd(uint32 dev, uint32 cmd) {
   uint32   i;
   UNIT    *uptr = NULL;

   /* Find the unit from dev */
   for (i = 0; i < cdr_dev.numunits; i++) {
       if (GET_UADDR(cdr_unit[i].flags) == dev) {
           uptr = &cdr_unit[i];
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
       sim_debug(DEBUG_CMD, &cdr_dev, "STOP: %02o %08o\n", cmd, uptr->STATUS);
       return;
   }

   if (cmd & 01) {
       if (uptr->STATUS & BUSY || (uptr->flags & UNIT_ATT) == 0) {
           uptr->STATUS |= OPAT;
           chan_set_done(dev);
           return;
       }
       uptr->STATUS = BUSY;
       sim_activate(uptr, uptr->wait);
       chan_clr_done(dev);
       sim_debug(DEBUG_CMD, &cdr_dev, "START: %02o %08o\n", cmd, uptr->STATUS);
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
void cdr_nsi_status(uint32 dev, uint32 *resp) {
   uint32   i;
   UNIT    *uptr = NULL;

   *resp = 0;
   /* Find the unit from dev */
   for (i = 0; i < cdr_dev.numunits; i++) {
       if (GET_UADDR(cdr_unit[i].flags) == dev) {
           uptr = &cdr_unit[i];
           break;
       }
   }

   /* Should not happen, but just in case */
   if (uptr == NULL)
       return;

   /* Ignore this command if not a SI device */
   if (SI_TYPE(uptr->flags))
       return;

   *resp = uptr->STATUS & 7;
   if (uptr->STATUS & BUSY)
       *resp |= 040;
   uptr->STATUS &= BUSY|DISC|IMAGE;
   chan_clr_done(dev);
   sim_debug(DEBUG_STATUS, &cdr_dev, "STATUS: %02o\n", *resp);
}


t_stat cdr_svc (UNIT *uptr)
{
    uint16  image[80];
    int     dev = GET_UADDR(uptr->flags);
    uint8   ch;
    int     i;
    int     eor;

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

    switch(i = sim_read_card(uptr, image)) {
    default:
    case CDSE_EMPTY:
    case CDSE_EOF:
         sim_card_detach(uptr);
         uptr->STATUS |= OPAT;
         sim_debug(DEBUG_DATA, &cdr_dev, "EOF: %d\n", i);
         break;
    case CDSE_ERROR:
         uptr->STATUS |= OPAT;
         sim_debug(DEBUG_DATA, &cdr_dev, "Error: %d\n", i);
         break;
    case CDSE_OK:
         sim_debug(DEBUG_DATA, &cdr_dev, "ok: %d\n", i);
         for (i = 0; i < 80; i++) {
             if (uptr->STATUS & IMAGE) {
                 ch = (image[i] >> 6) & 077;
                 eor = chan_input_char(dev, &ch, 0);
                 if (eor)
                    break;
                 ch = image[i] & 077;
             } else {
                 ch = hol_to_mem[image[i]];
             sim_debug(DEBUG_DATA, &cdr_dev, "col: %04x %02o '%c'\n", image[i], ch, mem_to_ascii[ch]);
                 if (ch == 0xff) {
                    uptr->STATUS |= ERROR;
                    ch = 077;
                 }
             }
             sim_debug(DEBUG_DATA, &cdr_dev, "DATA: %03o\n", ch);
             eor = chan_input_char(dev, &ch, 0);
             if (eor)
                break;
         }
         break;
    }

    uptr->STATUS |= TERMINATE;
    uptr->STATUS &= ~(BUSY|DISC);
    chan_set_done(dev);
    return SCPE_OK;
}

t_stat
cdr_reset(DEVICE *dptr) 
{
    unsigned int i;

    memset(&hol_to_mem[0], 0xff, 4096);
    for(i = 0; i < (sizeof(mem_to_hol)/sizeof(uint16)); i++) {
         uint16          temp;
         temp = mem_to_hol[i];
         hol_to_mem[temp] = i;
    }
    return SCPE_OK;
}


/* Boot from given device */
t_stat
cdr_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT    *uptr = &dptr->units[unit_num];
    int      chan = GET_UADDR(uptr->flags);

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    M[64 + chan] = 0;
    M[256 + 4 * chan] = 0;
    M[257 + 4 * chan] = 0;
    loading = 1;
    uptr->STATUS = BUSY|IMAGE;
    sim_activate (uptr, uptr->wait);
    return SCPE_OK;
}

t_stat
cdr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = sim_card_attach(uptr, file)) != SCPE_OK)
       return r;
    uptr->STATUS = 0;
    chan_set_done(GET_UADDR(uptr->flags));
    return SCPE_OK;
}

t_stat
cdr_detach(UNIT * uptr)
{
    return sim_card_detach(uptr);
}



t_stat cdr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "The card reader can be set to one of several device types\n\n");
    sim_card_attach_help(st, dptr, uptr, flag, cptr);
    fprintf (st, "The device number can be set with DEV=# command.\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);

    return SCPE_OK;
}

CONST char *cdr_description (DEVICE *dptr)
{
    return "CR";

}
#endif
