/* ka10_lp.c: PDP-10 line printer simulator

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
#include <ctype.h>

#ifndef NUM_DEVS_LP
#define NUM_DEVS_LP 0
#endif

#if (NUM_DEVS_LP > 0) 

#define LP_DEVNUM 0126
#define STATUS   u3
#define CHL      u4
#define CHR      u5

#define UNIT_V_UC    (UNIT_V_UF + 0)
#define UNIT_UC      (1 << UNIT_V_UC)
#define PI_DONE  000007
#define PI_ERROR 000070
#define DONE_FLG 000100
#define BUSY_FLG 000200
#define ERR_FLG  000400
#define CLR_LPT  002000
#define C96      002000
#define C128     004000



DEVICE          lpt_dev;
t_stat          lpt_devio(uint32 dev, uint64 *data);
t_stat          lpt_svc (UNIT *uptr);
t_stat          lpt_reset (DEVICE *dptr);
t_stat          lpt_attach (UNIT *uptr, CONST char *cptr);
t_stat          lpt_detach (UNIT *uptr);
t_stat          lpt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, 
                         const char *cptr);
const char     *lpt_description (DEVICE *dptr);
int32           lpt_stopioe;

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptor
   lpt_reg      LPT register list
*/

DIB lpt_dib = { LP_DEVNUM, 1, &lpt_devio };

UNIT lpt_unit = {
    UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0), SERIAL_OUT_WAIT
    };

REG lpt_reg[] = {
    { DRDATA (STATUS, lpt_unit.STATUS, 18), PV_LEFT },
    { DRDATA (TIME, lpt_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, lpt_stopioe, 0) },
    { NULL }
};

MTAB lpt_mod[] = {
    {UNIT_UC, 0, "Lower case", "LC", NULL},
    {UNIT_UC, UNIT_UC, "Upper case", "UC", NULL},
    { 0 }
};

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &lpt_reset,
    NULL, &lpt_attach, &lpt_detach,
    &lpt_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lpt_help, NULL, NULL, &lpt_description
};

/* IOT routine */

t_stat lpt_devio(uint32 dev, uint64 *data) {
    UNIT *uptr = &lpt_unit;
    switch(dev & 3) {
    case CONI:
         *data = uptr->STATUS;
         if ((uptr->flags & UNIT_UC) == 0)
             *data |= C96;
         if ((uptr->flags & UNIT_ATT) == 0) 
             *data |= ERR_FLG;
         sim_debug(DEBUG_CONI, &lpt_dev, "LP CONI %012llo PC=%06o\n", *data, PC);
         break;

    case CONO:
         clr_interrupt(dev);
         sim_debug(DEBUG_CONO, &lpt_dev, "LP CONO %012llo PC=%06o\n", *data, PC);
         uptr->STATUS = ((PI_DONE|PI_ERROR|DONE_FLG|BUSY_FLG) & *data);
         if (*data & CLR_LPT) {
             uptr->CHR = 0;
             uptr->CHL = 0;
             uptr->STATUS |= BUSY_FLG;
             sim_activate (&lpt_unit, lpt_unit.wait);
         } 
         if ((uptr->flags & UNIT_ATT) == 0) {
             set_interrupt(dev, (uptr->STATUS >> 3));
         }
         if (uptr->STATUS & DONE_FLG)
             set_interrupt(dev, uptr->STATUS);
         break;

    case DATAO:
         if ((uptr->STATUS & BUSY_FLG) == 0) {
             uptr->CHL = *data >> 15;
             uptr->CHR = (*data >> 1) & 0037777;
             uptr->STATUS |= BUSY_FLG;
             uptr->STATUS &= ~DONE_FLG;
             clr_interrupt(dev);
             sim_activate (&lpt_unit, lpt_unit.wait);
        }
        sim_debug(DEBUG_DATAIO, &lpt_dev, "LP DATO %012llo, %06o %06o PC=%06o\n", 
                  *data, uptr->CHL, uptr->CHR, PC);
         break;
    case DATAI:
         *data = 0;
         break;
    }
    return SCPE_OK;
}


/* Unit service */
t_stat lpt_output(UNIT *uptr, char c) {
    if (c == 0)
       return SCPE_OK;
    if (uptr->flags & UNIT_UC) 
        c = toupper(c);
    fputc (c, uptr->fileref);                       /* print char */
    uptr->pos = ftell (uptr->fileref);
    if (ferror (uptr->fileref)) {                           /* error? */
        perror ("LPT I/O error");
        clearerr (uptr->fileref);
        uptr->STATUS |= ERR_FLG;
        set_interrupt(LP_DEVNUM, (uptr->STATUS >> 3));
        return SCPE_IOERR;
    }
    return SCPE_OK;
}
    
t_stat lpt_svc (UNIT *uptr)
{
    t_stat r;
    char    c;
    if ((uptr->flags & UNIT_ATT) == 0) {
        uptr->STATUS |= ERR_FLG;
        return SCPE_OK;
    }
    
    c = (uptr->CHL >> 14) & 0177;
    if ((r = lpt_output(uptr, c)) != SCPE_OK)
       return r;
    c = (uptr->CHL >> 7) & 0177;
    if ((r = lpt_output(uptr, c)) != SCPE_OK)
       return r;
    c = uptr->CHL  & 0177;
    if ((r = lpt_output(uptr, c)) != SCPE_OK)
       return r;
    c = (uptr->CHR >> 7) & 0177;
    if ((r = lpt_output(uptr, c)) != SCPE_OK)
       return r;
    c = uptr->CHR  & 0177;
    if ((r = lpt_output(uptr, c)) != SCPE_OK)
       return r;
    uptr->STATUS &= ~BUSY_FLG;
    uptr->STATUS |= DONE_FLG;
    set_interrupt(LP_DEVNUM, uptr->STATUS);
    //fprintf(stderr, "LP IRQ\n\r");
    return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
    UNIT *uptr = &lpt_unit;
    uptr->CHR = 0;
    uptr->CHL = 0;
    uptr->STATUS = 0;
    clr_interrupt(LP_DEVNUM);
    sim_cancel (&lpt_unit);                                 /* deactivate unit */
    return SCPE_OK;
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat reason;

    reason = attach_unit (uptr, cptr);
    uptr->STATUS &= ~ERR_FLG;
    clr_interrupt(LP_DEVNUM);
    return reason;
}

/* Detach routine */

t_stat lpt_detach (UNIT *uptr)
{
    uptr->STATUS |= ERR_FLG;
    set_interrupt(LP_DEVNUM, uptr->STATUS >> 3);
    return detach_unit (uptr);
}

t_stat lpt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Line Printer (LPT)\n\n");
fprintf (st, "The line printer (LPT) writes data to a disk file.  The POS register specifies\n");
fprintf (st, "the number of the next data item to be written.  Thus, by changing POS, the\n");
fprintf (st, "user can backspace or advance the printer.\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *lpt_description (DEVICE *dptr)
{
    return "LP10 line printer" ;
}

#endif
