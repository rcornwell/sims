/* pdp6_dcs.c: PDP-6 DC630 communication server simulator

   Copyright (c) 2011-2017, Richard Cornwell

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
#include "sim_sock.h"
#include "sim_tmxr.h"

#ifndef NUM_DEVS_T630
#define NUM_DEVS_T630 0
#endif

#if (NUM_DEVS_T630 > 0)

#define T630_DEVNUM 0300

#define T630_LINES    16


#define STATUS   u3

#define RPI_CHN  000007         /* IN STATUS. */
#define TPI_CHN  000700         /* In STATUS */
#define RLS_SCN  000010         /* CONO DCSA release scanner */
#define RST_SCN  000020         /* CONO DCSA reset to 0 */
#define RSCN_ACT 000040         /* Scanner line is active */
#define XMT_RLS  004000         /* Clear transmitter flag */
#define XSCN_ACT 004000         /* Transmit scanner active */

#define DATA     0000377
#define LINE     0000077        /* Line number in Left */


int      t630_rx_scan = 0;                        /* Scan counter */
int      t630_tx_scan = 0;                        /* Scan counter */
int      t630_send_line = 0;                      /* Send line number */
TMLN     t630_ldsc[T630_LINES] = { 0 };           /* Line descriptors */
TMXR     t630_desc = { T630_LINES, 0, 0, t630_ldsc };
uint32   t630_tx_enable, t630_rx_rdy;             /* Flags */
uint32   t630_enable;                             /* Enable line */
uint32   t630_rx_conn;                            /* Connection flags */
extern int32 tmxr_poll;

t_stat t630_devio(uint32 dev, uint64 *data);
t_stat t630_svc (UNIT *uptr);
t_stat t630_doscan (UNIT *uptr);
t_stat t630_reset (DEVICE *dptr);
t_stat t630_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat t630_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat t630_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat t630_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat t630_attach (UNIT *uptr, CONST char *cptr);
t_stat t630_detach (UNIT *uptr);
t_stat t630_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
        const char *cptr);
const char *t630_description (DEVICE *dptr);

/* Type 630 data structures

   t630_dev      Type 630 device descriptor
   t630_unit     Type 630 unit descriptor
   t630_reg      Type 630 register list
*/

DIB t630_dib = { T630_DEVNUM, 2, &t630_devio, NULL };

UNIT t630_unit = {
    UDATA (&t630_svc, TT_MODE_7B+UNIT_IDLE+UNIT_ATTABLE, 0), KBD_POLL_WAIT
    };

REG t630_reg[] = {
    { DRDATA (TIME, t630_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (STATUS, t630_unit.STATUS, 18), PV_LEFT },
    { NULL }
    };

MTAB t630_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", NULL },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  NULL },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  NULL },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &t630_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL,
        NULL, &tmxr_show_summ, (void *) &t630_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &t630_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &t630_desc, "Display multiplexer statistics" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &t630_setnl, &tmxr_show_lines, (void *) &t630_desc, "Set number of lines" },
    { MTAB_XTD|MTAB_VDV|MTAB_NC, 0, NULL, "LOG=n=file",
        &t630_set_log, NULL, (void *)&t630_desc },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, NULL, "NOLOG",
        &t630_set_nolog, NULL, (void *)&t630_desc, "Disable logging on designated line" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "LOG", NULL,
        NULL, &t630_show_log, (void *)&t630_desc, "Display logging for all lines" },
    { 0 }
    };

DEVICE t630_dev = {
    "DCS", &t630_unit, t630_reg, t630_mod,
    1, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &t630_reset,
    NULL, &t630_attach, &t630_detach,
    &t630_dib, DEV_NET | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &t630_help, NULL, NULL, &t630_description
    };


/* IOT routine */
t_stat t630_devio(uint32 dev, uint64 *data) {
    UNIT *uptr = &t630_unit;
    TMLN *lp;
    int   ln;

    switch(dev & 7) {
    case CONI:
         /* Check if we might have any interrupts pending */
         if ((uptr->STATUS & (RSCN_ACT|XSCN_ACT)) != 0)
             t630_doscan(uptr);
         *data = uptr->STATUS & (RPI_CHN|TPI_CHN);
         if ((uptr->STATUS & (RSCN_ACT)) == 0)
            *data |= 010LL;
         if ((uptr->STATUS & (XSCN_ACT)) == 0)
            *data |= 01000LL;
         sim_debug(DEBUG_CONI, &t630_dev, "T630 %03o CONI %06o PC=%o\n",
               dev, (uint32)*data, PC);
         break;

    case CONO:
         /* Set PI */
         uptr->STATUS &= ~(RPI_CHN|TPI_CHN);
         uptr->STATUS |= (RPI_CHN|TPI_CHN) & *data;
         if (*data & RST_SCN)
             t630_rx_scan = 0;
         if ((*data & (RLS_SCN|RST_SCN)) != 0)
             uptr->STATUS |= RSCN_ACT;
         if ((*data & (XSCN_ACT)) != 0)
             uptr->STATUS |= XSCN_ACT;

         sim_debug(DEBUG_CONO, &t630_dev, "T630 %03o CONO %06o PC=%06o\n",
               dev, (uint32)*data, PC);
         t630_doscan(uptr);
         break;

    case DATAO:
    case DATAO|4:
         ln = (dev & 4) ? t630_send_line : t630_tx_scan;
         if (ln < t630_desc.lines) {
             lp = &t630_ldsc[ln];
             if (lp->conn) {
                int32 ch = *data & DATA;
                ch = sim_tt_outcvt(ch, TT_GET_MODE (t630_unit.flags) | TTUF_KSR);
                tmxr_putc_ln (lp, ch);
                t630_tx_enable |= (1 << ln);
             }
         }
         if (dev & 4) {
             uptr->STATUS |= XSCN_ACT;
             t630_doscan(uptr);
         }
         sim_debug(DEBUG_DATAIO, &t630_dev, "DC %03o DATO %012llo PC=%06o\n",
                    dev, *data, PC);
         break;

    case DATAI:
    case DATAI|4:
         ln = t630_rx_scan;
         if (ln < t630_desc.lines) {
             /* Nothing happens if no recieve data, which is transmit ready */
             lp = &t630_ldsc[ln];
             if (tmxr_rqln (lp) > 0) {
                int32 ch = tmxr_getc_ln (lp);
                if (ch & SCPE_BREAK)                      /* break? */
                    ch = 0;
                else
                    ch = sim_tt_inpcvt (ch, TT_GET_MODE(t630_unit.flags) | TTUF_KSR);
                *data = (uint64)(ch & DATA);
                t630_tx_enable &= ~(1 << ln);
             }
             t630_rx_rdy &= ~(1 << ln);
         }
         if (dev & 4) {
             uptr->STATUS |= RSCN_ACT;
             t630_doscan(uptr);
         }
         sim_debug(DEBUG_DATAIO, &t630_dev, "T630 %03o DATI %012llo PC=%06o\n",
                    dev, *data, PC);
         break;
    case CONI|4:
         /* Read in scanner */
         if ((uptr->STATUS & (RSCN_ACT)) != 0)
             *data = (uint64)(t630_tx_scan);
         else
             *data = (uint64)(t630_rx_scan);
         sim_debug(DEBUG_CONI, &t630_dev, "T630 %03o CONI %06o PC=%o recieve line\n",
               dev, (uint32)*data, PC);
         break;

    case CONO|4:
         /* Output buffer pointer */
         t630_send_line = (int)(*data & 077);
         sim_debug(DEBUG_CONO, &t630_dev, "T630 %03o CONO %06o PC=%06o send line\n",
               dev, (uint32)*data, PC);
         break;
    }
    return SCPE_OK;
}


/* Unit service */

t_stat t630_svc (UNIT *uptr)
{
int32 ln;

    if ((uptr->flags & UNIT_ATT) == 0)                  /* attached? */
        return SCPE_OK;
    ln = tmxr_poll_conn (&t630_desc);                     /* look for connect */
    if (ln >= 0) {                                      /* got one? rcv enb*/
        t630_ldsc[ln].rcve = 1;
        t630_tx_enable |= 1 << ln;
        sim_debug(DEBUG_DETAIL, &t630_dev, "DC line connect %d\n", ln);
    }
    tmxr_poll_tx(&t630_desc);
    tmxr_poll_rx(&t630_desc);
    for (ln = 0; ln < t630_desc.lines; ln++) {
       /* Check to see if any pending data for this line */
       if (tmxr_rqln(&t630_ldsc[ln]) > 0) {
           t630_rx_rdy |= (1 << ln);
           sim_debug(DEBUG_DETAIL, &t630_dev, "DC recieve %d\n", ln);
       }
       /* Check if disconnect */
       if ((t630_rx_conn & (1 << ln)) != 0 && t630_ldsc[ln].conn == 0) {
           t630_tx_enable &= ~(1 << ln);
           t630_rx_conn &= ~(1 << ln);
           sim_debug(DEBUG_DETAIL, &t630_dev, "DC line disconnect %d\n", ln);
       }
    }

    /* If any pending status request, raise the PI signal */
    t630_doscan(uptr);
    sim_clock_coschedule(uptr, tmxr_poll);              /* continue poll */
    return SCPE_OK;
}

/* Scan to see if something to do */
t_stat t630_doscan (UNIT *uptr) {
   uint32 lmask;

   if ((uptr->STATUS & (RSCN_ACT|XSCN_ACT)) == 0)
       return SCPE_OK;
   clr_interrupt(T630_DEVNUM);
   if ((uptr->STATUS & (RSCN_ACT)) != 0) {
       for (;t630_rx_rdy != 0; t630_rx_scan++) {
          t630_rx_scan &= 037;
          /* Check if we found it */
          if (t630_rx_rdy & (1 << t630_rx_scan)) {
             uptr->STATUS &= ~RSCN_ACT;
             /* Stop scanner */
             set_interrupt(T630_DEVNUM, uptr->STATUS);
             return SCPE_OK;
          }
       }
   }
   if ((uptr->STATUS & (XSCN_ACT)) != 0) {
       for (;t630_tx_enable != 0; t630_tx_scan++) {
          t630_tx_scan &= 037;
          /* Check if we found it */
          if (t630_tx_enable & (1 << t630_tx_scan)) {
             uptr->STATUS &= ~XSCN_ACT;
             /* Stop scanner */
             set_interrupt(T630_DEVNUM, (uptr->STATUS >> 6));
             return SCPE_OK;
          }
       }
   }
   return SCPE_OK;
}

/* Reset routine */

t_stat t630_reset (DEVICE *dptr)
{
    if (t630_unit.flags & UNIT_ATT)                           /* if attached, */
        sim_activate (&t630_unit, tmxr_poll);                 /* activate */
    else
        sim_cancel (&t630_unit);                             /* else stop */
    t630_tx_enable = 0;
    t630_rx_rdy = 0;                             /* Flags */
    t630_rx_conn = 0;
    t630_send_line = 0;
    t630_tx_scan = 0;
    t630_rx_scan = 0;
    t630_unit.STATUS = 0;
    clr_interrupt(T630_DEVNUM);
    return SCPE_OK;
}


/* SET LINES processor */

t_stat t630_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 newln, i, t;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    newln = (int32) get_uint (cptr, 10, T630_LINES, &r);
    if ((r != SCPE_OK) || (newln == t630_desc.lines))
        return r;
    if ((newln == 0) || (newln >= T630_LINES) || (newln % 8) != 0)
        return SCPE_ARG;
    if (newln < t630_desc.lines) {
        for (i = newln, t = 0; i < t630_desc.lines; i++)
            t = t | t630_ldsc[i].conn;
        if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
            return SCPE_OK;
        for (i = newln; i < t630_desc.lines; i++) {
            if (t630_ldsc[i].conn) {
                tmxr_linemsg (&t630_ldsc[i], "\r\nOperator disconnected line\r\n");
                tmxr_send_buffered_data (&t630_ldsc[i]);
                }
            tmxr_detach_ln (&t630_ldsc[i]);               /* completely reset line */
        }
    }
    if (t630_desc.lines < newln)
        memset (t630_ldsc + t630_desc.lines, 0, sizeof(*t630_ldsc)*(newln-t630_desc.lines));
    t630_desc.lines = newln;
    return t630_reset (&t630_dev);                         /* setup lines and auto config */
}

/* SET LOG processor */

t_stat t630_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    char gbuf[CBUFSIZE];
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    cptr = get_glyph (cptr, gbuf, '=');
    if ((cptr == NULL) || (*cptr == 0) || (gbuf[0] == 0))
        return SCPE_ARG;
    ln = (int32) get_uint (gbuf, 10, t630_desc.lines, &r);
    if ((r != SCPE_OK) || (ln >= t630_desc.lines))
        return SCPE_ARG;
    return tmxr_set_log (NULL, ln, cptr, desc);
}

/* SET NOLOG processor */

t_stat t630_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    ln = (int32) get_uint (cptr, 10, t630_desc.lines, &r);
    if ((r != SCPE_OK) || (ln >= t630_desc.lines))
        return SCPE_ARG;
    return tmxr_set_nolog (NULL, ln, NULL, desc);
}

/* SHOW LOG processor */

t_stat t630_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int32 i;

    for (i = 0; i < t630_desc.lines; i++) {
        fprintf (st, "line %d: ", i);
        tmxr_show_log (st, NULL, i, desc);
        fprintf (st, "\n");
        }
    return SCPE_OK;
}


/* Attach routine */

t_stat t630_attach (UNIT *uptr, CONST char *cptr)
{
t_stat reason;

reason = tmxr_attach (&t630_desc, uptr, cptr);
if (reason != SCPE_OK)
  return reason;
sim_activate (uptr, tmxr_poll);
return SCPE_OK;
}

/* Detach routine */

t_stat t630_detach (UNIT *uptr)
{
  int32  i;
  t_stat reason;
reason = tmxr_detach (&t630_desc, uptr);
for (i = 0; i < t630_desc.lines; i++)
    t630_ldsc[i].rcve = 0;
sim_cancel (uptr);
return reason;
}

t_stat t630_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Type 630 Terminal Interfaces\n\n");
fprintf (st, "The Type 630 supported up to 8 blocks of 8 lines. Modem control was on a seperate\n");
fprintf (st, "line. The simulator supports this by setting modem control to a fixed offset\n");
fprintf (st, "from the given line. The number of lines is specified with a SET command:\n\n");
fprintf (st, "   sim> SET DC LINES=n          set number of additional lines to n [8-32]\n\n");
fprintf (st, "Lines must be set in multiples of 8.\n");
fprintf (st, "The default offset for modem lines is 32. This can be changed with\n\n");
fprintf (st, "   sim> SET DC MODEM=n          set offset for modem control to n [8-32]\n\n");
fprintf (st, "Modem control must be set larger then the number of lines\n");
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
fprintf (st, "The default mode is 7P.\n");
fprintf (st, "Finally, each line supports output logging.  The SET DCn LOG command enables\n");
fprintf (st, "logging on a line:\n\n");
fprintf (st, "   sim> SET DCn LOG=filename   log output of line n to filename\n\n");
fprintf (st, "The SET DCn NOLOG command disables logging and closes the open log file,\n");
fprintf (st, "if any.\n\n");
fprintf (st, "Once DC is attached and the simulator is running, the terminals listen for\n");
fprintf (st, "connections on the specified port.  They assume that the incoming connections\n");
fprintf (st, "are Telnet connections.  The connections remain open until disconnected either\n");
fprintf (st, "by the Telnet client, a SET DC DISCONNECT command, or a DETACH DC command.\n\n");
fprintf (st, "Other special commands:\n\n");
fprintf (st, "   sim> SHOW DC CONNECTIONS    show current connections\n");
fprintf (st, "   sim> SHOW DC STATISTICS     show statistics for active connections\n");
fprintf (st, "   sim> SET DCn DISCONNECT     disconnects the specified line.\n");
fprint_reg_help (st, &t630_dev);
fprintf (st, "\nThe additional terminals do not support save and restore.  All open connections\n");
fprintf (st, "are lost when the simulator shuts down or DC is detached.\n");
return SCPE_OK;
}

const char *t630_description (DEVICE *dptr)
{
return "Type 630 asynchronous line interface";
}

#endif
