/* icl1900_tp.c: ICL1900 Paper Tape Punch simulator

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

#ifndef NUM_DEVS_PTP
#define NUM_DEVS_PTP 0
#endif

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
#define PUN_BLNK     0004
#define DISC         0010
#define BUSY         0020
#define DELTA_MODE   0040

#define TERMINATE    001
#define OPAT         002
#define ERROR        004
#define ACCEPT       020

#define ALPHA_SHIFT   074
#define BETA_SHIFT    075
#define DELTA_SHIFT   076

#if (NUM_DEVS_PTP > 0)

#define T1925_1               0
#define T1925_2               1
#define T1926_1               2
#define T1926_2               3

#define UNIT_PTP(x)      UNIT_ADDR(x)|SET_TYPE(T1925_2)|UNIT_ATTABLE|\
                             UNIT_DISABLE|TT_MODE_7B

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

void ptp_cmd (uint32 dev, uint32 cmd, uint32 *resp);
void ptp_nsi_cmd (uint32 dev, uint32 cmd);
void ptp_nsi_status (uint32 dev, uint32 *resp);
t_stat ptp_svc (UNIT *uptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat ptp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
CONST char *ptp_description (DEVICE *dptr);

DIB ptp_dib = {  CHAR_DEV, &ptp_cmd, &ptp_nsi_cmd, &ptp_nsi_status };

UNIT ptp_unit[] = {
    { UDATA (&ptp_svc, UNIT_PTP(8), 0), 10000 },
    { UDATA (&ptp_svc, UNIT_PTP(9), 0), 10000 },
    };


MTAB ptp_mod[] = {
    { PP_M_MODE, PP_MODE_7B << PP_V_MODE, "7b", "7B", NULL },
    { PP_M_MODE, PP_MODE_7P << PP_V_MODE, "7p", "7P", NULL },
    { PP_M_MODE, PP_MODE_7X << PP_V_MODE, "7x", "7X", NULL },
    { UNIT_TYPE, SET_TYPE(T1925_1), "1925/1", "1925/1", NULL, NULL, NULL, "ICL 1925/1 NSI 300CPM punch."},
    { UNIT_TYPE, SET_TYPE(T1925_2), "1925/2", "1925/2", NULL, NULL, NULL, "ICL 1922/2 SI 300CPM punch."},
    { UNIT_TYPE, SET_TYPE(T1926_1), "1926/1", "1926/1", NULL, NULL, NULL, "ICL 1926/1 NSI 1000CPM punch."},
    { UNIT_TYPE, SET_TYPE(T1926_2), "1926/2", "1926/2", NULL, NULL, NULL, "ICL 1926/2 SI 1000CPM punch."},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_chan, &get_chan, NULL, "Device Number"},
    { 0 }
    };

DEVICE ptp_dev = {
    "TP", ptp_unit, NULL, ptp_mod,
    NUM_DEVS_PTP, 8, 22, 1, 8, 22,
    NULL, NULL, &ptp_reset, NULL, &attach_unit, &detach_unit,
    &ptp_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &ptp_help, NULL, NULL, &ptp_description
    };

/*
 * Command codes
 *
 * 001xxx      Write
 * 001xx0          Start in current shift
 * 001xx1          Start in alpha shift
 * 001x1x          Graphics
 * 001x0x          BCD.
 * 0011xx          Punch Blank characters.
 * 0010xx          Punch characters.
 * 010000          Send Q.
 * 010100          Send P.
 * 011110          Disconnect.
 */


void ptp_cmd(uint32 dev, uint32 cmd, uint32 *resp) {
   uint32  i;
   UNIT    *uptr = NULL;

   *resp = 0;
   /* Find the unit from dev */
   for (i = 0; i < ptp_dev.numunits; i++) {
       if (GET_UADDR(ptp_unit[i].flags) == dev) {
           uptr = &ptp_unit[i];
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
             if (cmd & 1)
                 uptr->CMD = 0;
             uptr->CMD &= DELTA_MODE|1;
             uptr->CMD |= BUSY | (cmd & 07);
             uptr->STATUS = 0;
             sim_activate(uptr, uptr->wait);
             chan_clr_done(dev);
             *resp = 5;
             break;

   case 020: if (cmd == 020) {    /* Send Q */
                 *resp = uptr->STATUS & TERMINATE;
                 if ((uptr->flags & UNIT_ATT) == 0) {
                    *resp = 040;
                    if ((uptr->CMD & BUSY) != 0)
                        *resp |= 030;
                 }
                 if ((uptr->STATUS & ERROR) == 0)
                    *resp |= 040;
             } else if (cmd == 024) {  /* Send P */
                 if ((uptr->flags & UNIT_ATT) != 0)
                    *resp = 1;
                 if ((uptr->STATUS & ERROR) != 0)
                    *resp |= 2;
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
 * xxxx01     Start punch.
 * xxxx10     Stop punch.
 * xx1xxx     Start in Previous shift, else alpha.
 * x1xxxx     Graphics mode.
 * 1xxxxx     Punch blanks.
 */
void ptp_nsi_cmd(uint32 dev, uint32 cmd) {
   uint32  i;
   UNIT    *uptr = NULL;

   /* Find the unit from dev */
   for (i = 0; i < ptp_dev.numunits; i++) {
       if (GET_UADDR(ptp_unit[i].flags) == dev) {
           uptr = &ptp_unit[i];
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
       if (cmd & 020)
           uptr->CMD |= BIN_MODE;
       if ((cmd & 040) == 0)
           uptr->CMD |= PUN_BLNK;
       uptr->CMD |= BUSY;
       uptr->STATUS = 0;
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
void ptp_nsi_status(uint32 dev, uint32 *resp) {
   uint32  i;
   UNIT    *uptr = NULL;

   *resp = 0;
   /* Find the unit from dev */
   for (i = 0; i < ptp_dev.numunits; i++) {
       if (GET_UADDR(ptp_unit[i].flags) == dev) {
           uptr = &ptp_unit[i];
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
   uptr->STATUS = 0;
   chan_clr_done(dev);
}



t_stat ptp_svc (UNIT *uptr)
{
    int     dev = GET_UADDR(uptr->flags);
    uint8   ch;
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

    /* Check if attached */
    if ((uptr->flags & UNIT_ATT) == 0) {
       uptr->CMD &= 1;
       uptr->STATUS = TERMINATE;
       chan_set_done(dev);
       return SCPE_OK;
    }

    eor = chan_output_char(dev, &ch, 0);
    if ((uptr->CMD & PUN_BLNK) != 0) {
       data = 0400;
    } else if (uptr->CMD & BIN_MODE) {
       data = ch & 017;
       switch (ch & 060) {
       case 0000: data |= 060; break;
       case 0020: data |= 040; break;
       case 0040: data |= 0100; break;
       case 0060: data |= 0120; break;
       }
    } else {
       data = 0;
       if (ch == ALPHA_SHIFT) {
            uptr->CMD &= BUSY|DISC|BIN_MODE|PUN_BLNK;
            uptr->CMD |= ALPHA_MODE;
       } else if (ch == BETA_SHIFT) {
            uptr->CMD &= BUSY|DISC|BIN_MODE|PUN_BLNK;
            uptr->CMD |= BETA_MODE;
       } else if (ch == DELTA_SHIFT) {
            uptr->CMD &= BUSY|DISC|BIN_MODE|PUN_BLNK|ALPHA_SHIFT;
            uptr->CMD |= DELTA_MODE;
       } else if (ch == 077) {
            uptr->CMD &= BUSY|DISC|BIN_MODE|PUN_BLNK|ALPHA_SHIFT;
       } else {
            if (uptr->CMD & DELTA_MODE) {
                uptr->CMD &= ~DELTA_MODE;
                data = ch;
                if (ch & 040)
                   data |= 0174 ^ ((ch & 020) << 1);
            } else if (uptr->CMD & ALPHA_MODE) {
                data = ch & 017;
                switch (ch & 060) {
                case 0000: data |= 060; break;
                case 0020: data |= 040; break;
                case 0040: data |= 0100; break;
                case 0060: data |= 0120; break;
                }
            } else {
                data = ch & 017;
                switch (ch & 060) {
                case 0000: data |= 060; break;
                case 0020: data |= 040; break;
                case 0040: data |= 0140; break;
                case 0060: data |= 0160; break;
                }
            }
        }
    }
    if (data != 0) {
    /* Check parity is even */
        if (PP_MODE(uptr->flags) == PP_MODE_7P) {
           data &= 0177;
           ch = data ^ (data << 4);
           ch = ch ^ (ch << 2);
           ch = ch ^ (ch << 1);
           data |= ch;
        } else if (PP_MODE(uptr->flags) == PP_MODE_7X) {
           if (data == 044) {
               data = 0243;
           } else if (data == 0174) {
               data = 044;
           }
        }

        fputc(data, uptr->fileref);
        uptr->pos = ftell(uptr->fileref);
        if (ferror (uptr->fileref)) {
            uptr->STATUS |= TERMINATE|ERROR;
            uptr->CMD &= DELTA_MODE|1;
            chan_set_done(dev);
            return SCPE_OK;
        }
    }
    /* Check if Done */
    if (eor) {
        uptr->STATUS |= TERMINATE;
        uptr->CMD &= DELTA_MODE|1;
        chan_set_done(dev);
        return SCPE_OK;
    }
    sim_activate (uptr, uptr->wait);               /* try again */
    return SCPE_OK;
}


/* Reset */

t_stat ptp_reset (DEVICE *dptr)
{
    UNIT *uptr = dptr->units;
    int unit;

    for (unit = 0; unit < NUM_DEVS_PTP; unit++, uptr++) {
       uptr->CMD = ALPHA_MODE;
       uptr->STATUS = 0;
       uptr->HOLD = 0;
       chan_clr_done(GET_UADDR(uptr->flags));
    }
    return SCPE_OK;
}


t_stat ptp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "The Paper Tape Punch can be set to one of two modes: 7P, or 7B\n\n");
    fprintf (st, "  7P    Generate even parity tapes.\n");
    fprintf (st, "  7B    Generate 7 bit tapes.\n");
    fprintf (st, "  7X    Generate translated 7 bit tapes\n");
    fprintf (st, "The default mode is 7B.\n\n");
    fprintf (st, "The device number can be set with DEV=# command.\n");

    return SCPE_OK;
}

CONST char *ptp_description (DEVICE *dptr)
{
    return "PTP";

}
#endif
