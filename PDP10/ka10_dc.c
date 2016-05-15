/* ka10_dc.c: PDP-10 DC10 communication server simulator

   Copyright (c) 1993-2011, Richard Cornwell

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

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell.

*/

#include "ka10_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#ifndef NUM_DEVS_DC
#define NUM_DEVS_DC 0
#endif

#if (NUM_DEVS_DC > 0) 

#define DC_DEVNUM 0240

#define DC10_LINES      8

#define STATUS   u3

#define DTS_LINE 007700         /* Scanner line number in STATUS */
#define PI_CHN   000007         /* IN STATUS. */
#define RCV_PI   000010         /* IN STATUS. */
#define XMT_PI   000020         /* IN STATUS. */
#define DTR_DIS  000040         /* DTR FLAG */
#define RST_SCN  000010         /* CONO */
#define DTR_SET  000020         /* CONO */
#define CLR_SCN  000040         /* CONO */

#define DATA     0000377
#define FLAG     0000400        /* Recieve data/ transmit disable */
#define LINE     0000077        /* Line number in Left */
#define LFLAG    0000100        /* Direct line number flag */

/* DC10E flags */
#define CTS      0000004        /* Clear to send */
#define RES_DET  0000002        /* Restrian detect RTS? */
#define DLO      0000040        /* (ACU) Data line occupied */
#define PND      0000020        /* (ACU) Present next digit */
#define ACR      0000010        /* (ACU) Abandon Call and retry */
#define CRQ      0000040        /* (ACU) Call Request */
#define DPR      0000020        /* (ACU) Digit Presented */
#define NB       0000017        /* (ACU) Number */
#define OFF_HOOK 0000100        /* Off Hook (CD) */
#define CAUSE_PI 0000200        /* Cause PI */

uint8 dcix_buf[DC10_LINES] = { 0 };             /* Input buffers */
uint8 dcox_buf[DC10_LINES] = { 0 };             /* Output buffers */
TMLN dc_ldsc[DC10_LINES] = { 0 };               /* Line descriptors */
TMXR dc_desc = { DC10_LINES, 0, 0, dc_ldsc };
uint32 tx_enable, rx_rdy;                               /* Flags */
uint32 rx_conn;                                         /* Connection flags */
extern int32 tmxr_poll;

DEVICE dc_dev;
t_stat dc_devio(uint32 dev, uint64 *data);
t_stat dc_svc (UNIT *uptr);
t_stat dc_doscan (UNIT *uptr);
t_stat dc_reset (DEVICE *dptr);
t_stat dc_attach (UNIT *uptr, char *cptr);
t_stat dc_detach (UNIT *uptr);
t_stat dc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
        const char *cptr);
const char *dc_description (DEVICE *dptr);

/* DC10 data structures

   dc_dev      DC10 device descriptor
   dc_unit     DC10 unit descriptor
   dc_reg      DC10 register list
*/

DIB dc_dib = { DC_DEVNUM, 1, &dc_devio };

UNIT dc_unit = {
    UDATA (&dc_svc, TT_MODE_7B+UNIT_IDLE+UNIT_ATTABLE, 0), KBD_POLL_WAIT 
    };

REG dc_reg[] = {
    { DRDATA (TIME, dc_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (STATUS, dc_unit.STATUS, 18), PV_LEFT },
    { NULL }
    };

MTAB dc_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", NULL },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  NULL },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  NULL },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  NULL },
    { UNIT_ATT, UNIT_ATT, "summary", NULL,
      NULL, &tmxr_show_summ, (void *) &dc_desc },
    { MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, (void *) &dc_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dc_desc },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL,
      NULL, &tmxr_show_cstat, (void *) &dc_desc },
    { 0 }
    };

DEVICE dc_dev = {
    "DC", &dc_unit, dc_reg, dc_mod,
    1, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &dc_reset,
    NULL, &dc_attach, &dc_detach,
    &dc_dib, DEV_NET | DEV_DISABLE, 0, NULL,
    NULL, NULL, &dc_help, NULL, NULL, &dc_description
    };



/* IOT routine */
t_stat dc_devio(uint32 dev, uint64 *data) {
    UNIT *uptr = &dc_unit;
    TMLN *lp;
    int   ln;

    switch(dev & 3) {
    case CONI:
         *data = uptr->STATUS & (PI_CHN|RCV_PI|XMT_PI);
         break;

    case CONO:
         clr_interrupt(dev);
         /* Set PI */
         uptr->STATUS &= ~PI_CHN;
         uptr->STATUS |= PI_CHN & *data;
         if (*data & RST_SCN) 
             uptr->STATUS &= ~ DTS_LINE;
         if (*data & DTR_SET)
             uptr->STATUS |= DTR_SET;
         if (*data & CLR_SCN) {
             uptr->STATUS &= PI_CHN;
             tx_enable = 0;
             rx_rdy = 0;                                /* Flags */
             rx_conn = 0;
         }

         sim_activate (&dc_unit, dc_unit.wait);
         if ((uptr->STATUS & (RCV_PI|XMT_PI)) == 0) {
             set_interrupt(dev, uptr->STATUS & 7);
         }
         break;

    case DATAO:
         if (*data & (LFLAG << 18)) 
             ln = (*data >> 18) & 077;
         else 
             ln = (uptr->STATUS & DTS_LINE) >> 6;
         if (ln >= DC10_LINES) { 
             lp = &dc_ldsc[ln - DC10_LINES];
             if (*data & CAUSE_PI) 
                rx_rdy |= (1 << ln);
             if ((*data & OFF_HOOK) == 0 && lp->conn) {
                tmxr_linemsg (lp, "\r\nLine Hangup\r\n");
                tmxr_reset_ln(lp);
             }
         } else {
             lp = &dc_ldsc[ln];
             if (*data & FLAG) 
                tx_enable &= ~(1 << ln);
             else {
                 int32 ch = *data & DATA;
                 ch = sim_tt_outcvt(ch, TT_GET_MODE (dc_unit.flags) | TTUF_KSR);
                 tmxr_putc_ln (lp, ch);
                 tx_enable |= (1 << ln);
             }
         }
         break;
    case DATAI:
         ln = (uptr->STATUS & DTS_LINE) >> 6;
         *data = (uint64)(ln) << 18;
         if (ln >= DC10_LINES) {
             lp = &dc_ldsc[ln - DC10_LINES];
             if (lp->conn)
                *data |= OFF_HOOK|CTS;
             rx_rdy &= ~(1 << ln);
         } else {
             /* Nothing happens if no recieve data, which is transmit ready */
             lp = &dc_ldsc[ln];
             if (tmxr_rqln (lp) > 0) {
                int32 ch = tmxr_getc_ln (lp);
                if (ch & SCPE_BREAK)                      /* break? */
                    ch = 0;
                else ch = sim_tt_inpcvt (ch, TTUF_MODE_7B | TTUF_KSR);
                *data |= FLAG | (uint64)(ch & DATA);
             }
             rx_rdy &= ~(1 << ln);
             if (tmxr_rqln (lp) > 0) {
                rx_rdy |= 1 << ln;
             }
         }
         break;
    }
    dc_doscan(uptr);
    return SCPE_OK;
}


/* Unit service */

t_stat dc_svc (UNIT *uptr)
{
int32 ln, c, temp;

    if ((uptr->flags & UNIT_ATT) == 0)                  /* attached? */
        return SCPE_OK;
    sim_clock_coschedule(uptr, tmxr_poll);              /* continue poll */
    ln = tmxr_poll_conn (&dc_desc);                     /* look for connect */
    if (ln >= 0) {                                      /* got one? rcv enb*/ 
        dc_ldsc[ln].rcve = 1;
        rx_conn |= 1 << (ln + DC10_LINES);
        rx_rdy |= 1 << (ln + DC10_LINES);
    }
    tmxr_poll_tx(&dc_desc);
    dc_doscan(uptr);
    return SCPE_OK;
}

/* Scan to see if something to do */
t_stat dc_doscan (UNIT *uptr) {
   int cnt = 2 * DC10_LINES;
   int32 ln;
   int32 lmsk;
   clr_interrupt(DC_DEVNUM);
   uptr->STATUS &= ~(RCV_PI|XMT_PI);
   tmxr_poll_rx(&dc_desc);
   ln = ((uptr->STATUS & DTS_LINE) >> 6) - 1;
   while (cnt > 0 && (uptr->STATUS & (RCV_PI|XMT_PI)) == 0) {
       cnt--;
       ln = (ln + 1) & 037;     /* Only 32 lines max */
       lmsk = 1 << ln;
       if (ln >= DC10_LINES) {
        /* Look for disconnect */
            if ((rx_conn & lmsk) != 0 &&
                dc_ldsc[ln - DC10_LINES].conn == 0) {
                rx_rdy |= lmsk;
                rx_conn &= ~lmsk;
            }
       } else {
            if (tmxr_rqln(&dc_ldsc[ln]) > 0) 
                rx_rdy |= lmsk;
       }
       if (rx_rdy & lmsk) 
           uptr->STATUS |= RCV_PI;
       if (tx_enable & lmsk) 
           uptr->STATUS |= XMT_PI;
   }
   uptr->STATUS &= ~DTS_LINE;
   uptr->STATUS |= ln << 6;
   if (uptr->STATUS & (RCV_PI|XMT_PI)) 
      set_interrupt(DC_DEVNUM, uptr->STATUS & 07);
}

/* Reset routine */

t_stat dc_reset (DEVICE *dptr)
{
int32 ln, itto;

if (dc_unit.flags & UNIT_ATT)                           /* if attached, */
    sim_activate (&dc_unit, tmxr_poll);                 /* activate */
else sim_cancel (&dc_unit);                             /* else stop */
tx_enable = 0;
rx_rdy = 0;                             /* Flags */
rx_conn = 0;
dc_unit.STATUS = 0;
clr_interrupt(DC_DEVNUM);
return SCPE_OK;
}




/* Attach routine */

t_stat dc_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = tmxr_attach (&dc_desc, uptr, cptr);
if (reason != SCPE_OK)
  return reason;
sim_activate (uptr, tmxr_poll);
return SCPE_OK;
}

/* Detach routine */

t_stat dc_detach (UNIT *uptr)
{
  int32  i;
  t_stat reason;
reason = tmxr_detach (&dc_desc, uptr);
for (i = 0; i < DC10_LINES; i++) 
    dc_ldsc[i].rcve = 0;
sim_cancel (uptr);
return reason;
}

t_stat dc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "DC10 Additional Terminal Interfaces\n\n");
fprintf (st, "For very early system programs, the PDP-11 simulator supports up to sixteen\n");
fprintf (st, "additional DC11 terminal interfaces.  The additional terminals consist of two\n");
fprintf (st, "independent devices, DCI and DCO.  The entire set is modeled as a terminal\n");
fprintf (st, "multiplexer, with DCI as the master controller.  The additional terminals\n");
fprintf (st, "perform input and output through Telnet sessions connected to a user-specified\n");
fprintf (st, "port.  The number of lines is specified with a SET command:\n\n");
fprintf (st, "   sim> SET DCI LINES=n        set number of additional lines to n [1-16]\n\n");
fprintf (st, "The ATTACH command specifies the port to be used:\n\n");
tmxr_attach_help (st, dptr, uptr, flag, cptr);
fprintf (st, "The additional terminals can be set to one of four modes: UC, 7P, 7B, or 8B.\n\n");
fprintf (st, "  mode  input characters        output characters\n\n");
fprintf (st, "  UC    lower case converted    lower case converted to upper case,\n");
fprintf (st, "        to upper case,          high-order bit cleared,\n");
fprintf (st, "        high-order bit cleared  non-printing characters suppressed\n");
fprintf (st, "  7P    high-order bit cleared  high-order bit cleared,\n");
fprintf (st, "                                non-printing characters suppressed\n");
fprintf (st, "  7B    high-order bit cleared  high-order bit cleared\n");
fprintf (st, "  8B    no changes              no changes\n\n");
fprintf (st, "The default mode is 7P.  In addition, each line can be configured to\n");
fprintf (st, "behave as though it was attached to a dataset, or hardwired to a terminal:\n\n");
fprintf (st, "Finally, each line supports output logging.  The SET DCOn LOG command enables\n");
fprintf (st, "logging on a line:\n\n");
fprintf (st, "   sim> SET DCOn LOG=filename   log output of line n to filename\n\n");
fprintf (st, "The SET DCOn NOLOG command disables logging and closes the open log file,\n");
fprintf (st, "if any.\n\n");
fprintf (st, "Once DCI is attached and the simulator is running, the terminals listen for\n");
fprintf (st, "connections on the specified port.  They assume that the incoming connections\n");
fprintf (st, "are Telnet connections.  The connections remain open until disconnected either\n");
fprintf (st, "by the Telnet client, a SET DCI DISCONNECT command, or a DETACH DCI command.\n\n");
fprintf (st, "Other special commands:\n\n");
fprintf (st, "   sim> SHOW DCI CONNECTIONS    show current connections\n");
fprintf (st, "   sim> SHOW DCI STATISTICS     show statistics for active connections\n");
fprintf (st, "   sim> SET DCOn DISCONNECT     disconnects the specified line.\n");
fprint_reg_help (st, &dc_dev);
fprintf (st, "\nThe additional terminals do not support save and restore.  All open connections\n");
fprintf (st, "are lost when the simulator shuts down or DCI is detached.\n");
return SCPE_OK;
}

const char *dc_description (DEVICE *dptr)
{
return "DC10 asynchronous line interface";
}

#endif
