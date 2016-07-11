/* ka10_pt.c: PDP-10 reader/punch simulator

   Copyright (c) 2011-2016, Richard Cornwell

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
   in this Software without prior written authorization from Richard Cornwell.
*/

#include "ka10_defs.h"

#ifndef NUM_DEVS_PT
#define NUM_DEVS_PT 0
#endif

#if (NUM_DEVS_PT > 0) 

#define PP_DEVNUM 0100
#define PR_DEVNUM 0104
#define STATUS   u3
#define CHR      u4
#define CHL      u5

#define PI_DONE     000007
#define DONE_FLG    000010
#define BUSY_FLG    000020
#define BIN_FLG     000040
#define NO_TAPE_PP  000100
#define TAPE_PR     000400


DEVICE         ptp_dev;
t_stat         ptp_devio(uint32 dev, uint64 *data);
t_stat         ptp_svc (UNIT *uptr);
t_stat         ptp_reset (DEVICE *dptr);
t_stat         ptp_attach (UNIT *uptr, CONST char *cptr);
t_stat         ptp_detach (UNIT *uptr);
t_stat         ptp_help (FILE *st, DEVICE *dptr, UNIT *uptr, 
                            int32 flag, const char *cptr);
const char    *ptp_description (DEVICE *dptr);
             
DEVICE         ptr_dev;
t_stat         ptr_devio(uint32 dev, uint64 *data);
t_stat         ptr_svc (UNIT *uptr);
t_stat         ptr_reset (DEVICE *dptr);
t_stat         ptr_attach (UNIT *uptr, CONST char *cptr);
t_stat         ptr_detach (UNIT *uptr);
t_stat         ptr_help (FILE *st, DEVICE *dptr, UNIT *uptr,
                             int32 flag, const char *cptr);
const char    *ptr_description (DEVICE *dptr);


DIB ptp_dib = { PP_DEVNUM, 1, &ptp_devio };

UNIT ptp_unit = {
    UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0), SERIAL_OUT_WAIT
    };

REG ptp_reg[] = {
    { DRDATA (STATUS, ptp_unit.STATUS, 18), PV_LEFT },
    { DRDATA (TIME, ptp_unit.wait, 24), PV_LEFT },
    { NULL }
    };

MTAB ptp_mod[] = {
    { 0 }
    };

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, ptp_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, &ptp_attach, &ptp_detach,
    &ptp_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &ptp_help, NULL, NULL, &ptp_description
    };

DIB ptr_dib = { PR_DEVNUM, 1, &ptr_devio };

UNIT ptr_unit = {
    UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0), SERIAL_OUT_WAIT
    };

REG ptr_reg[] = {
    { DRDATA (STATUS, ptr_unit.STATUS, 18), PV_LEFT },
    { DRDATA (TIME, ptr_unit.wait, 24), PV_LEFT },
    { NULL }
    };

MTAB ptr_mod[] = {
    { 0 }
    };

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, ptr_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptr_reset,
    NULL, &ptr_attach, &ptr_detach,
    &ptr_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &ptr_help, NULL, NULL, &ptr_description
    };

/* IOT routine */

t_stat ptp_devio(uint32 dev, uint64 *data) {
    UNIT *uptr = &ptp_unit;
    switch(dev & 3) {
    case CONI:
         *data = uptr->STATUS;
         sim_debug(DEBUG_CONI, &ptp_dev, "PP: CONI %012llo\n\r", *data);
         break;

    case CONO:
         clr_interrupt(dev);
         uptr->STATUS = (PI_DONE|DONE_FLG|BUSY_FLG|BIN_FLG) & *data;
         if ((uptr->flags & UNIT_ATT) == 0) 
             uptr->STATUS |= NO_TAPE_PP;
         if (uptr->STATUS & BUSY_FLG) {
             uptr->CHR = 0;
             sim_activate (&ptp_unit, ptp_unit.wait);
         }
         if (uptr->STATUS & DONE_FLG) 
             set_interrupt(dev, uptr->STATUS);
         sim_debug(DEBUG_CONO, &ptp_dev, "PP: CONO %012llo\n\r", *data);
         break;

    case DATAO:
         if ((uptr->STATUS & BUSY_FLG) == 0) {
             uptr->CHR = *data & 00377;
             if (uptr->STATUS & BIN_FLG) {
                uptr->CHR &= 077;
                uptr->CHR |= 0200;
             }
             uptr->STATUS |= BUSY_FLG;
             uptr->STATUS &= ~DONE_FLG;
             clr_interrupt(dev);
             sim_activate (&ptp_unit, ptp_unit.wait);
         }
         sim_debug(DEBUG_DATAIO, &ptp_dev, "PP: DATAO %012llo\n\r", *data);
         break;
    case DATAI:
         *data = 0;
         break;
    }
    return SCPE_OK;
}


/* Unit service */
t_stat ptp_svc (UNIT *uptr)
{
    t_stat r;
    char c;
    uptr->STATUS &= ~BUSY_FLG;
    uptr->STATUS |= DONE_FLG;
    set_interrupt(PP_DEVNUM, uptr->STATUS & 7);

    if ((uptr->flags & UNIT_ATT) == 0) {
        uptr->STATUS |= NO_TAPE_PP;
        return SCPE_UNATT;
    }
    fputc (uptr->CHR, uptr->fileref);                       /* print char */
    uptr->pos = ftell (uptr->fileref);
    if (ferror (uptr->fileref)) {                           /* error? */
        perror ("PTP I/O error");
        clearerr (uptr->fileref);
        return SCPE_IOERR;
    }
    return SCPE_OK;
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
    UNIT *uptr = &ptp_unit;
    uptr->CHR = 0;
    uptr->CHL = 0;
    uptr->STATUS = 0;
    clr_interrupt(PP_DEVNUM);
    sim_cancel (&ptp_unit);                                 /* deactivate unit */
    return SCPE_OK;
}

/* Attach routine */

t_stat ptp_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat reason;

    reason = attach_unit (uptr, cptr);
    uptr->STATUS &= ~NO_TAPE_PP;
    return reason;
}

/* Detach routine */

t_stat ptp_detach (UNIT *uptr)
{
    uptr->STATUS |= NO_TAPE_PP;
    return detach_unit (uptr);
}



t_stat ptr_devio(uint32 dev, uint64 *data) {
    UNIT *uptr = &ptr_unit;
    switch(dev & 3) {
    case CONI:
         *data = uptr->STATUS;
         sim_debug(DEBUG_CONI, &ptr_dev, "PT: CONI %012llo\n\r", *data);
         break;

    case CONO:
         clr_interrupt(dev);
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
         sim_debug(DEBUG_CONO, &ptr_dev, "PT: CONO %012llo\n\r", *data);
         break;

    case DATAI:
         if ((uptr->STATUS & DONE_FLG)) {
             *data = ((uint64)uptr->CHL) << 18;
             *data |= ((uint64)uptr->CHR);
             uptr->STATUS |= BUSY_FLG;
             uptr->STATUS &= ~DONE_FLG;
             clr_interrupt(dev);
             sim_activate (&ptr_unit, ptr_unit.wait);
         }
         sim_debug(DEBUG_DATAIO, &ptr_dev, "PT: DATAI %012llo\n\r", *data);
         break;
    case DATAO:
         break;
    }
    return SCPE_OK;
}

/* Unit service */
t_stat ptr_svc (UNIT *uptr)
{
    int32     temp;
    uint64    word;
    int       count = (uptr->STATUS & BIN_FLG) ? 6 : 1;

    uptr->STATUS &= ~BUSY_FLG;
    uptr->STATUS |= DONE_FLG;
    set_interrupt(PR_DEVNUM, uptr->STATUS);

    if ((ptr_unit.flags & UNIT_ATT) == 0)                   /* attached? */
        return SCPE_UNATT;
    word = 0;
    while (count > 0) {
        if ((temp = getc (ptr_unit.fileref)) == EOF) {
           if (feof (ptr_unit.fileref)) {
             uptr->STATUS &= ~TAPE_PR;
             break;
           }
        }
        if (uptr->STATUS & BIN_FLG) {
           if (temp & 0200) {
                word <<= 6;
                word |= (uint64)(temp & 077);
                count--;
           }
        } else {
           word |= (uint64)(temp);
           count--;
        } 
    }
    uptr->CHL = (word >> 18);
    uptr->CHR = word & 0777777;
    return SCPE_OK;
}


/* Reset routine */

t_stat ptr_reset (DEVICE *dptr)
{
    UNIT *uptr = &ptr_unit;
    uptr->CHR = 0;
    uptr->CHL = 0;
    uptr->STATUS = 0;
    clr_interrupt(PR_DEVNUM);
    sim_cancel (&ptr_unit);                                 /* deactivate unit */
    return SCPE_OK;
}

/* Attach routine */

t_stat ptr_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat reason;

    reason = attach_unit (uptr, cptr);
    uptr->STATUS |= TAPE_PR;
    return reason;
}

/* Detach routine */

t_stat ptr_detach (UNIT *uptr)
{
    uptr->STATUS &= ~TAPE_PR;
    return detach_unit (uptr);
}

t_stat ptr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Paper Tape Reader (PTR)\n\n");
fprintf (st, "The paper tape reader (PTR) reads data from a disk file.  The POS register\n");
fprintf (st, "specifies the number of the next data item to be read.  Thus, by changing\n");
fprintf (st, "POS, the user can backspace or advance the reader.\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *ptr_description (DEVICE *dptr)
{
return "paper tape reader";
}

t_stat ptp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Paper Tape Punch (PTP)\n\n");
fprintf (st, "The paper tape punch (PTP) writes data to a disk file.  The POS register\n");
fprintf (st, "specifies the number of the next data item to be written.  Thus, by changing\n");
fprintf (st, "POS, the user can backspace or advance the punch.\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *ptp_description (DEVICE *dptr)
{
return "paper tape punch";
}
#endif
