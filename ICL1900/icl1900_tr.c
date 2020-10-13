/* icl1900_tr.c: ICL1900 Paper Tape Reader simulator

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

#ifndef NUM_DEVS_PTR
#define NUM_DEVS_PTR 0
#endif

#if (NUM_DEVS_PTR > 0)

#define PP_V_MODE        (UNIT_V_UF + 0)
#define PP_M_MODE        (3 << PP_V_MODE)
#define PP_MODE(x)       ((PP_M_MODE & (x)) >> PP_V_MODE)
#define UNIT_V_TYPE      (UNIT_V_UF + 2)
#define UNIT_TYPE        (0xf << UNIT_V_TYPE)
#define GET_TYPE(x)      ((UNIT_TYPE & (x)) >> UNIT_V_TYPE)
#define SET_TYPE(x)      (UNIT_TYPE & ((x) << UNIT_V_TYPE))

#define NSI_TYPE(x)      ((GET_TYPE(x) & 1) == 0)
#define  SI_TYPE(x)      ((GET_TYPE(x) & 1) != 0)
#define  PP_MODE_7B   0
#define  PP_MODE_7P   1
#define  PP_MODE_7X   2

#define CMD          u3
#define STATUS       u4
#define HOLD         u5

#define ALPHA_MODE   0001
#define BETA_MODE    0000
#define BIN_MODE     0002
#define IGN_BLNK     0004
#define DISC         0010
#define BUSY         0020
#define DELTA_MODE   0040
#define STOP_CHAR    0100

#define TERMINATE    001
#define OPAT         002
#define ERROR        004
#define ACCEPT       020

#define ALPHA_SHIFT   074
#define BETA_SHIFT    075
#define DELTA_SHIFT   076

#define T1915_1               0
#define T1915_2               1
#define T1916_1               2
#define T1916_2               3

#define UNIT_PTR(x)      UNIT_ADDR(x)|SET_TYPE(T1915_2)|UNIT_ATTABLE|\
                              UNIT_DISABLE|UNIT_RO|TT_MODE_7B

/*
 * Character translation.
 *
 * Alpha shift 074
 * Beta shift  075
 * Delta shift 076
 *
 * p000xxxx    Delta + 01xxxx
 * p001xxxx    Delta + 00xxxx
 * p10111xx    Delta + 1101xx
 * p11111xx    Delta + 1110xx
 * p010xxxx            01xxxx
 * p011xxxx            00xxxx
 * p100xxxx    Alpha + 10xxxx
 * p101xxxx    Alpha + 11xxxx xxxx < 4
 * p110xxxx    Beta  + 10xxxx
 * p111xxxx    Beta  + 11xxxx xxxx < 4
 *
 * Two modes Alpha and Beta. Delta is always output.
 *
 * Graphics mode translation.
 *
 * p010xxxx    01xxxx
 * p011xxxx    00xxxx
 * p100xxxx    10xxxx
 * p101xxxx    11xxxx
 * p110xxxx    10xxxx
 * p111xxxx    11xxxx
 *
 */

void ptr_cmd (uint32 dev, uint32 cmd, uint32 *resp);
void ptr_nsi_cmd (uint32 dev, uint32 cmd);
void ptr_nsi_status (uint32 dev, uint32 *resp);
t_stat ptr_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unit_num, DEVICE * dptr);
t_stat ptr_attach(UNIT *, CONST char *);
t_stat ptr_detach(UNIT *);
t_stat ptr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
CONST char *ptr_description (DEVICE *dptr);

DIB ptr_dib = {  CHAR_DEV, &ptr_cmd, &ptr_nsi_cmd, &ptr_nsi_status };

UNIT ptr_unit[] = {
    { UDATA (&ptr_svc, UNIT_PTR(4), 0), 10000 },
    { UDATA (&ptr_svc, UNIT_PTR(5), 0), 10000 },
    };


MTAB ptr_mod[] = {
    { PP_M_MODE, PP_MODE_7B << PP_V_MODE, "7b", "7B", NULL },
    { PP_M_MODE, PP_MODE_7P << PP_V_MODE, "7p", "7P", NULL },
    { PP_M_MODE, PP_MODE_7X << PP_V_MODE, "7x", "7X", NULL },
    { UNIT_TYPE, SET_TYPE(T1915_1), "1915/1", "1915/1", NULL, NULL, NULL, "ICL 1915/1 NSI 300CPM reader."},
    { UNIT_TYPE, SET_TYPE(T1915_2), "1915/2", "1915/2", NULL, NULL, NULL, "ICL 1912/2 SI 300CPM reader."},
    { UNIT_TYPE, SET_TYPE(T1916_1), "1916/1", "1916/1", NULL, NULL, NULL, "ICL 1916/1 NSI 1000CPM reader."},
    { UNIT_TYPE, SET_TYPE(T1916_2), "1916/2", "1916/2", NULL, NULL, NULL, "ICL 1916/2 SI 1000CPM reader."},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_chan, &get_chan, NULL, "Device Number"},
    { 0 }
    };

DEVICE ptr_dev = {
    "TR", ptr_unit, NULL, ptr_mod,
    NUM_DEVS_PTR, 8, 22, 1, 8, 22,
    NULL, NULL, &ptr_reset, &ptr_boot, &ptr_attach, &ptr_detach,
    &ptr_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &ptr_help, NULL, NULL, &ptr_description
    };

/*
 * Command codes
 *
 * 001xxx      Read
 * 001xx0          Start in current shift
 * 001xx1          Start in alpha shift
 * 001x1x          Graphics
 * 001x0x          BCD.
 * 0011xx          Ignore blank tape and erase
 * 0010xx          Read Blank and erase
 * 010000          Send Q.
 * 010100          Send P.
 * 011110          Disconnect.
 */


void ptr_cmd(uint32 dev, uint32 cmd, uint32 *resp) {
   uint32  i;
   UNIT    *uptr = NULL;

   *resp = 0;
   /* Find the unit from dev */
   for (i = 0; i < ptr_dev.numunits; i++) {
       if (GET_UADDR(ptr_unit[i].flags) == dev) {
           uptr = &ptr_unit[i];
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
   cmd &= 077;
   switch(cmd & 070) {
   case 010: /* Command */
             if ((uptr->flags & UNIT_ATT) == 0)
                 break;
             if (uptr->CMD & BUSY) {
                 *resp = 3;
                 break;
             }
             sim_debug(DEBUG_CMD, &ptr_dev, "CMD: %03o %03o %03o\n", cmd, uptr->CMD, uptr->STATUS);
             uptr->CMD &= 1;
             uptr->CMD |= BUSY| (cmd & 07);
             uptr->STATUS = 0;
             sim_activate(uptr, uptr->wait);
             chan_clr_done(dev);
             *resp = 5;
             break;

   case 020: if (cmd == 020) {    /* Send Q */
                 *resp = uptr->STATUS & TERMINATE;
                 if ((uptr->flags & UNIT_ATT) == 0) {
                    *resp = 040;
                    if ((uptr->CMD & BUSY) == 0)
                       *resp |= 030;
                 }
                 if ((uptr->STATUS & ERROR) == 0)
                    *resp |= 040;
                 sim_debug(DEBUG_STATUS, &ptr_dev, "STATUS: %03o %03o\n", cmd, *resp);
                 uptr->STATUS &= ~TERMINATE;
             } else if (cmd == 024) {  /* Send P */
                 if ((uptr->flags & UNIT_ATT) != 0)
                    *resp = 1;
                 if ((uptr->STATUS & ERROR) != 0)
                    *resp |= 2;
                 sim_debug(DEBUG_STATUS, &ptr_dev, "STATUS: %03o %03o\n", cmd, *resp);
                 uptr->STATUS = 0;
                 chan_clr_done(dev);
             }
             break;

   case 030: if (cmd == 036) {  /* Disconnect */
              uptr->CMD |= DISC;
              *resp = 5;
             }
             break;

   default:
             break;
   }
}

/*
 * Command codes
 *
 * xxxx01     Start reader.
 * xxxx10     Stop reader.
 * xxx1xx     Stop on return, else only stop on count.
 * xx1xxx     Start in Previous shift, else alpha.
 * x1xxxx     Graphics mode.
 * 1xxxxx     All characters.
 */
void ptr_nsi_cmd(uint32 dev, uint32 cmd) {
   uint32  i;
   UNIT    *uptr = NULL;

   /* Find the unit from dev */
   for (i = 0; i < ptr_dev.numunits; i++) {
       if (GET_UADDR(ptr_unit[i].flags) == dev) {
           uptr = &ptr_unit[i];
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
       if (uptr->CMD & BUSY)
           uptr->CMD |= DISC;
       sim_debug(DEBUG_CMD, &ptr_dev, "Stop: %03o\n", cmd);
       return;
   }

   if (cmd & 01) {
       if (uptr->CMD & BUSY || (uptr->flags & UNIT_ATT) == 0) {
           uptr->STATUS |= OPAT;
           chan_set_done(dev);
           return;
       }
       if (cmd & 010)
           uptr->CMD &= ALPHA_MODE;
       else
           uptr->CMD = ALPHA_MODE;
       if (cmd & 004)
           uptr->CMD |= STOP_CHAR;
       if (cmd & 020)
           uptr->CMD |= BIN_MODE;
       if ((cmd & 040) == 0)
           uptr->CMD |= IGN_BLNK;
       uptr->CMD |= BUSY;
       uptr->STATUS = 0;
       sim_debug(DEBUG_CMD, &ptr_dev, "Start: %03o\n", cmd);
       sim_activate(uptr, uptr->wait);
       chan_clr_done(dev);
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
void ptr_nsi_status(uint32 dev, uint32 *resp) {
   uint32  i;
   UNIT    *uptr = NULL;

   *resp = 0;
   /* Find the unit from dev */
   for (i = 0; i < ptr_dev.numunits; i++) {
       if (GET_UADDR(ptr_unit[i].flags) == dev) {
           uptr = &ptr_unit[i];
           break;
       }
   }

   /* Should not happen, but just in case */
   if (uptr == NULL)
       return;

   /* Ignore this command if not a SI device */
   if (SI_TYPE(uptr->flags))
       return;

   *resp = uptr->STATUS;
   if (uptr->CMD & BUSY)
       *resp |= 040;
   sim_debug(DEBUG_STATUS, &ptr_dev, "STATUS: %03o\n", *resp);
   uptr->STATUS = 0;
   chan_clr_done(dev);
}

t_stat ptr_svc (UNIT *uptr)
{
    int     dev = GET_UADDR(uptr->flags);
    uint8   ch;
    uint8   shift = 0;
    int     data;
    int     eor;

    /* Handle a disconnect request */
    if (uptr->CMD & DISC) {
       uptr->CMD &= 1;
       chan_set_done(dev);
       return SCPE_OK;
    }
    /* If not busy, false schedule, just exit */
    if ((uptr->CMD & BUSY) == 0)
        return SCPE_OK;
    if (uptr->HOLD != 0) {
        ch = uptr->HOLD & 077;
        uptr->HOLD = 0;
        eor = chan_input_char(dev, &ch, 0);
        if (eor) {
            uptr->CMD &= 1;
            chan_set_done(dev);
            uptr->STATUS = TERMINATE;
            return SCPE_OK;
        }
        if ((uptr->CMD & STOP_CHAR) != 0 && uptr->HOLD == 032)
            uptr->STATUS |= TERMINATE;
    }

    /* Read next charater */
    if (feof(uptr->fileref) || (data = getc (uptr->fileref)) == EOF) {
       uptr->CMD &= 1;
       sim_debug(DEBUG_DETAIL, &ptr_dev, "Tape Empty\n");
       detach_unit(uptr);
       chan_set_done(dev);
       uptr->STATUS = TERMINATE|OPAT;
       return SCPE_OK;
    }

    sim_debug(DEBUG_DATA, &ptr_dev, "data: %03o\n", data);
    /* Check parity is even */
    if (PP_MODE(uptr->flags) == PP_MODE_7P) {
       ch = data ^ (data >> 4);
       ch = ch ^ (ch >> 2);
       ch = ch ^ (ch >> 1);
       if (ch != 0)
          uptr->STATUS = TERMINATE | ERROR;
       chan_set_done(dev);
    } else if (PP_MODE(uptr->flags) == PP_MODE_7X) {
       if (data == 0243) {
           data = 044;
       } else if (data == 044) {
           data = 0174;
       }
    }
    data &= 0177;
    if ((data == 0 || data == 0177) && (uptr->CMD & IGN_BLNK) != 0) {
       sim_activate (uptr, uptr->wait);               /* try again */
       return SCPE_OK;
    }

    ch = 0;
    if (uptr->CMD & BIN_MODE) {
       switch (data & 0160) {
       case 0000:
       case 0020:   /* Terminate */
               uptr->STATUS |= TERMINATE;
               chan_set_done(dev);
               uptr->CMD &= 1;
               return SCPE_OK;
       case 0040:
               ch = 020 | (data & 017);
               break;
       case 0060:
               ch = 000 | (data & 017);
               break;
       case 0100:
       case 0140:
               ch = 040 | (data & 017);
               break;
       case 0120:
       case 0160:
               ch = 060 | (data & 017);
               break;
       }
       sim_debug(DEBUG_DATA, &ptr_dev, "xlt: '%c' %03o\n", data, ch);
    } else {
       switch (data & 0160) {
       case 0000:
       case 0020:
               if ((uptr->CMD & STOP_CHAR) != 0 && data == 012)
                   uptr->STATUS |= TERMINATE;
               shift = DELTA_SHIFT;
               ch = (data & 017);
               break;

       case 0040:
               ch = 020 | (data & 017);
               break;

       case 0060:
               ch = 000 | (data & 017);
               break;

       case 0140:
               if ((data & 017) > 013) {
                  shift = DELTA_SHIFT;
                  ch = 070 | (data & 03);
                  break;
               }
               /* Fall Through */

       case 0100:
               if ((uptr->CMD & 1) == BETA_MODE)
                  shift = ALPHA_SHIFT;
               uptr->CMD |= ALPHA_MODE;
               ch = 040 | (data & 037);
               break;

       case 0160:
               if ((data & 017) > 013) {
                  shift = DELTA_SHIFT;
                  ch = 064 | (data & 03);
                  break;
               }
               /* Fall Through */

       case 0120:
               if ((uptr->CMD & 1) == ALPHA_MODE)
                  shift = BETA_SHIFT;
               uptr->CMD &= ~ALPHA_MODE;
               ch = 040 | (data & 037);
               break;
       }
    }
    /* Check if error */
    if (shift != 0) {
        eor = chan_input_char(dev, &shift, 0);
        if (eor && ch != 0) {
           uptr->STATUS |= TERMINATE;
           chan_set_done(dev);
           uptr->CMD &= 1;
           uptr->HOLD = 0100 | ch;
           return SCPE_OK;
        }
    }
    eor = chan_input_char(dev, &ch, 0);
    if (eor) {
        uptr->STATUS |= TERMINATE;
        chan_set_done(dev);
        uptr->CMD &= 1;
        return SCPE_OK;
    }
    if (uptr->STATUS & TERMINATE) {
        chan_set_done(dev);
        uptr->CMD &= 1;
        return SCPE_OK;
    }
    sim_activate (uptr, uptr->wait);               /* try again */
    return SCPE_OK;
}


/* Reset */

t_stat ptr_reset (DEVICE *dptr)
{
    UNIT *uptr = dptr->units;
    int unit;

    for (unit = 0; unit < NUM_DEVS_PTR; unit++, uptr++) {
       uptr->CMD = ALPHA_MODE;
       uptr->STATUS = 0;
       uptr->HOLD = 0;
       chan_clr_done(GET_UADDR(uptr->flags));
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
ptr_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT    *uptr = &dptr->units[unit_num];
    int      dev = GET_UADDR(uptr->flags);

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    M[64 + dev] = 0;
    M[256 + 4 * dev] = 0;
    M[257 + 4 * dev] = 0;
    loading = 1;
    uptr->CMD = BUSY|ALPHA_MODE|BIN_MODE|IGN_BLNK;
    sim_activate (uptr, uptr->wait);
    return SCPE_OK;
}

t_stat
ptr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
       return r;
    uptr->STATUS = 0;
    chan_set_done(GET_UADDR(uptr->flags));
    return SCPE_OK;
}

t_stat
ptr_detach(UNIT * uptr)
{
    return detach_unit(uptr);
}



t_stat ptr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "The Paper Tape Reader can be set to one of two modes: 7P, or 7B\n\n");
    fprintf (st, "  7P    Process even parity input tapes. \n");
    fprintf (st, "  7B    Ignore parity of input data.\n");
    fprintf (st, "  7X    Ignore parity and translate British Pound to correct character\n");
    fprintf (st, "The default mode is 7B.\n\n");
    fprintf (st, "The device number can be set with DEV=# command.\n");
    return SCPE_OK;
}

CONST char *ptr_description (DEVICE *dptr)
{
    return "PTR";

}
#endif
