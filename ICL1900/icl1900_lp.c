/* icl1900_lp.c: ICL1900 Line Printer simulator

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

#ifndef NUM_DEVS_LPR
#define NUM_DEVS_LPR 0
#endif

#define UNIT_V_TYPE      (UNIT_V_UF + 0)
#define UNIT_TYPE        (0x1f << UNIT_V_TYPE)
#define GET_TYPE(x)      ((UNIT_TYPE & (x)) >> UNIT_V_TYPE)
#define SET_TYPE(x)      (UNIT_TYPE & ((x) << UNIT_V_TYPE))

#define NSI_TYPE(x)      ((GET_TYPE(x) & 1) == 0)
#define  SI_TYPE(x)      ((GET_TYPE(x) & 1) != 0)
#define LW_96(x)         ((GET_TYPE(x) & 06) == 0)
#define LW_120(x)        ((GET_TYPE(x) & 06) == 2)
#define LW_160(x)        ((GET_TYPE(x) & 06) == 4)

#define CMD          u3
#define STATUS       u4
#define MOTION       u5

#define AUTO         00100
#define PRINT        00040
#define QUAL         00020
#define SPACE        00010

#define TERMINATE    0001
#define OPAT         0002
#define ERROR        0004
#define BUSY         0040
#define DISC         0100


#if (NUM_DEVS_LPR > 0)

#define T1930_1               0
#define T1930_2               2
#define T1931_1               1
#define T1931_2               3
#define T1932_1               1+8
#define T1932_2               3+8
#define T1933_1               1+12
#define T1933_2               3+12
#define T1933_3               5+12

#define UNIT_LPR(x)      UNIT_ADDR(x)|SET_TYPE(T1931_2)|UNIT_ATTABLE|UNIT_DISABLE


void lpr_cmd (uint32 dev, uint32 cmd, uint32 *resp);
void lpr_nsi_cmd (uint32 dev, uint32 cmd);
void lpr_nsi_status (uint32 dev, uint32 *resp);
t_stat lpr_svc (UNIT *uptr);
t_stat lpr_reset (DEVICE *dptr);
t_stat lpr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
CONST char *lpr_description (DEVICE *dptr);

DIB lpr_dib = {  CHAR_DEV, &lpr_cmd, &lpr_nsi_cmd, &lpr_nsi_status };

UNIT lpr_unit[] = {
    { UDATA (&lpr_svc, UNIT_LPR(14), 0), 10000 },
    { UDATA (&lpr_svc, UNIT_LPR(15), 0), 10000 },
    };


MTAB lpr_mod[] = {
    { UNIT_TYPE, SET_TYPE(T1930_1), "1930/1", "1930/1", NULL, NULL, NULL, "ICL 1930/1 NSI 1000LPM printer."},
    { UNIT_TYPE, SET_TYPE(T1930_2), "1930/2", "1930/2", NULL, NULL, NULL, "ICL 1930/2 NSI 1000LPM printer."},
    { UNIT_TYPE, SET_TYPE(T1931_1), "1931/1", "1931/1", NULL, NULL, NULL, "ICL 1931/1 SI 1000LPM printer."},
    { UNIT_TYPE, SET_TYPE(T1931_2), "1931/2", "1931/2", NULL, NULL, NULL, "ICL 1931/2 SI 1000LPM printer."},
    { UNIT_TYPE, SET_TYPE(T1932_1), "1932/1", "1932/1", NULL, NULL, NULL, "ICL 1932/1 SI 1000LPM printer."},
    { UNIT_TYPE, SET_TYPE(T1932_2), "1932/2", "1932/2", NULL, NULL, NULL, "ICL 1932/2 SI 1000LPM printer."},
    { UNIT_TYPE, SET_TYPE(T1933_1), "1933/1", "1933/1", NULL, NULL, NULL, "ICL 1933/1 SI 1000LPM printer."},
    { UNIT_TYPE, SET_TYPE(T1933_2), "1933/2", "1933/2", NULL, NULL, NULL, "ICL 1933/2 SI 1000LPM printer."},
    { UNIT_TYPE, SET_TYPE(T1933_3), "1933/3", "1933/3", NULL, NULL, NULL, "ICL 1933/3 SI 1000LPM printer."},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_chan, &get_chan, NULL, "Device Number"},
    { 0 }
    };

DEVICE lpr_dev = {
    "LP", lpr_unit, NULL, lpr_mod,
    NUM_DEVS_PTP, 8, 22, 1, 8, 22,
    NULL, NULL, &lpr_reset, NULL, &attach_unit, &detach_unit,
    &lpr_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lpr_help, NULL, NULL, &lpr_description
    };

/*
 * Command codes
 *
 * 011010      Write
 * 000010      AutoWrite
 * 010000      Send Q.
 * 010100      Send P.
 * 011110      Disconnect.
 */


void lpr_cmd(uint32 dev, uint32 cmd, uint32 *resp) {
   uint32  i;
   UNIT    *uptr = NULL;

   *resp = 0;
   /* Find the unit from dev */
   for (i = 0; i < lpr_dev.numunits; i++) {
       if (GET_UADDR(lpr_unit[i].flags) == dev) {
           uptr = &lpr_unit[i];
           break;
       }
   }

   /* Should not happen, but just in case */
   if (uptr == NULL)
       return;

   /* Ignore this command if not a SI device */
   if (NSI_TYPE(uptr->flags))
       return;

   if (uptr->CMD & QUAL) {
       uptr->CMD |= cmd << 8;
       uptr->CMD &= ~QUAL;
       sim_debug(DEBUG_CMD, &lpr_dev, "QUAL: %03o %03o %03o\n", cmd, uptr->CMD, uptr->STATUS);
       *resp = 5;
       return;
   }
   if (cmd == 032 || cmd == 02) { /* Command */
       if (uptr->STATUS & BUSY) {
           *resp = 3;
           return;
       }
       uptr->CMD = (cmd == 02) ? AUTO: QUAL;
       uptr->STATUS = BUSY;
       sim_activate(uptr, uptr->wait);
       chan_clr_done(GET_UADDR(uptr->flags));
       *resp = 5;
   } else if (cmd == SEND_Q) {
       if ((uptr->flags & UNIT_ATT) == 0 || (uptr->STATUS & 06) == 0)
          *resp = 040;
       *resp |= uptr->STATUS & TERMINATE;
       uptr->STATUS &= ~1;
       if ((uptr->STATUS & BUSY) == 0)
           *resp |= 030;
   } else if (cmd == SEND_P) {  /* Send P */
      if ((uptr->flags & UNIT_ATT) != 0)
         *resp = (uptr->STATUS & ERROR) | 1;
      uptr->STATUS = 0;
      chan_clr_done(GET_UADDR(uptr->flags));
   } else if (cmd == DISCO) {  /* Disconnect */
      uptr->STATUS |= DISC;
      *resp = 5;
   }
   sim_debug(DEBUG_CMD, &lpr_dev, "CMD: %03o %03o %03o\n", cmd, uptr->CMD, uptr->STATUS);
}

/*
 * Command codes
 *
 * xxxx01     Start print.
 * xxxx10     Stop print.
 */
void lpr_nsi_cmd(uint32 dev, uint32 cmd) {
   uint32  i;
   UNIT    *uptr = NULL;

   /* Find the unit from dev */
   for (i = 0; i < NUM_DEVS_PTP; i++) {
       if (GET_UADDR(lpr_unit[i].flags) == dev) {
           uptr = &lpr_unit[i];
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
       return;
   }

   if (cmd & 01) {
       if (uptr->CMD & BUSY || (uptr->flags & UNIT_ATT) == 0) {
           uptr->STATUS |= OPAT;
           chan_set_done(GET_UADDR(uptr->flags));
           return;
       }
       uptr->CMD |= AUTO;
       uptr->STATUS = BUSY;
       sim_activate(uptr, uptr->wait);
       chan_clr_done(GET_UADDR(uptr->flags));
       sim_debug(DEBUG_CMD, &lpr_dev, "CMD: %03o %03o %03o\n", cmd, uptr->CMD, uptr->STATUS);
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
void lpr_nsi_status(uint32 dev, uint32 *resp) {
   uint32  i;
   UNIT    *uptr = NULL;

   *resp = 0;
   /* Find the unit from dev */
   for (i = 0; i < NUM_DEVS_PTP; i++) {
       if (GET_UADDR(lpr_unit[i].flags) == dev) {
           uptr = &lpr_unit[i];
           break;
       }
   }

   /* Should not happen, but just in case */
   if (uptr == NULL)
       return;

   /* Ignore this command if not a SI device */
   if (SI_TYPE(uptr->flags))
       return;

   *resp = uptr->STATUS & 077;
   uptr->STATUS &= BUSY|DISCO ;
   chan_clr_done(GET_UADDR(uptr->flags));
   sim_debug(DEBUG_CMD, &lpr_dev, "ST: %08o %03o %03o\n", *resp, uptr->CMD, uptr->STATUS);
}



t_stat lpr_svc (UNIT *uptr)
{
    uint8   ch;
    char    buffer[200];
    int     i;
    int     len;
    int     eor = 0;

    /* Handle a disconnect request */
    if (uptr->STATUS & DISC) {
       uptr->STATUS &= ~(BUSY|DISC);
       uptr->STATUS |= TERMINATE;
       chan_set_done(GET_UADDR(uptr->flags));
       return SCPE_OK;
    }
    /* If not busy, false schedule, just exit */
    if ((uptr->STATUS & BUSY) == 0)
       return SCPE_OK;

    /* Check if attached */
    if ((uptr->flags & UNIT_ATT) == 0) {
       uptr->STATUS = ERROR|TERMINATE;
       chan_set_done(GET_UADDR(uptr->flags));
       return SCPE_OK;
    }

    if (uptr->CMD & QUAL) {
       sim_activate(uptr, uptr->wait);
       return SCPE_OK;
    }

    len = 96;
    if (LW_120(uptr->flags))
        len = 120;
    else if (LW_160(uptr->flags))
        len = 160;
    i = 0;
    while (eor == 0 && i < len && i < (int)(sizeof(buffer)-4)) {
        eor = chan_output_char(GET_UADDR(uptr->flags), &ch, 0);
        if (uptr->CMD & AUTO) {
            uptr->CMD |= (int32)ch << 8;
            uptr->CMD &= ~AUTO;
        } else {
            sim_debug(DEBUG_DATA, &lpr_dev, "DATA: %03o\n", ch);
            buffer[i++] = mem_to_ascii[ch];
        }
    }
    buffer[i++] = '\r';
    buffer[i++] = '\n';
    buffer[i] = '\0';

    sim_fwrite(&buffer, 1, i, uptr->fileref);
    uptr->pos += i;
    /* Check if Done */
    if (eor) {
        uptr->STATUS |= TERMINATE;
       uptr->STATUS &= ~(BUSY|DISC);
        chan_set_done(GET_UADDR(uptr->flags));
        return SCPE_OK;
    }
    return SCPE_OK;
}


/* Reset */

t_stat lpr_reset (DEVICE *dptr)
{
    UNIT *uptr = dptr->units;
    int unit;

    for (unit = 0; unit < NUM_DEVS_PTP; unit++, uptr++) {
       uptr->STATUS = 0;
       chan_clr_done(GET_UADDR(uptr->flags));
    }
    return SCPE_OK;
}


t_stat lpr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "The Paper Tape Punch can be set to one of two modes: 7P, or 7B.\n\n");
fprintf (st, "  mode \n");
fprintf (st, "  7P    Process even parity input tapes. \n");
fprintf (st, "  7B    Ignore parity of input data.\n");
fprintf (st, "The default mode is 7B.\n");
return SCPE_OK;
}

CONST char *lpr_description (DEVICE *dptr)
{
    return "PTP";

}
#endif
