/* ks10_dz.c: PDP-10 DZ11 communication server simulator

   Copyright (c) 2021, Richard Cornwell

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

#include "kx10_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#ifndef NUM_DEVS_DZ
#define NUM_DEVS_DZ 0
#endif

#if (NUM_DEVS_DZ > 0)


#define DZ11_LINES    8

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
#define RES_DET  0000002        /* Ring detect */
#define DLO      0000040        /* (ACU) Data line occupied */
#define PND      0000020        /* (ACU) Present next digit */
#define ACR      0000010        /* (ACU) Abandon Call and retry */
#define CRQ      0000040        /* (ACU) Call Request */
#define DPR      0000020        /* (ACU) Digit Presented */
#define NB       0000017        /* (ACU) Number */
#define OFF_HOOK 0000100        /* Off Hook (CD) */
#define CAUSE_PI 0000200        /* Cause PI */

uint64   dz_l_status;                             /* Line status */
int      dz_l_count = 0;                          /* Scan counter */
int      dz_modem = DC10_MLINES;                  /* Modem base address */
uint8    dcix_buf[DC10_MLINES] = { 0 };           /* Input buffers */
uint8    dcox_buf[DC10_MLINES] = { 0 };           /* Output buffers */
TMLN     dz_ldsc[DC10_MLINES] = { 0 };            /* Line descriptors */
TMXR     dz_desc = { DC10_LINES, 0, 0, dz_ldsc };
uint32   tx_enable, rx_rdy;                       /* Flags */
uint32   dz_enable;                               /* Enable line */
uint32   dz_ring;                                 /* Connection pending */
uint32   rx_conn;                                 /* Connection flags */
extern int32 tmxr_poll;

int    dz_write(t_addr addr, uint16 data, int32 access);
int    dz_read(t_addr addr, uint16 *data, int32 access);
t_stat dz_svc (UNIT *uptr);
t_stat dz_doscan (UNIT *uptr);
t_stat dz_reset (DEVICE *dptr);
t_stat dz_set_modem (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dz_show_modem (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dz_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dz_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dz_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat dz_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat dz_attach (UNIT *uptr, CONST char *cptr);
t_stat dz_detach (UNIT *uptr);
t_stat dz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
        const char *cptr);
const char *dz_description (DEVICE *dptr);

/* DC10 data structures

   dz_dev      DC10 device descriptor
   dz_unit     DC10 unit descriptor
   dz_reg      DC10 register list
*/

DIB dz_dib = { 0776000, 077, 0340, 5, 3, &dz_read, &dz_write, 0 }

UNIT dz_unit = {
    UDATA (&dz_svc, TT_MODE_7B+UNIT_IDLE+UNIT_DISABLE+UNIT_ATTABLE, 0), KBD_POLL_WAIT
    };

REG dz_reg[] = {
    { DRDATA (TIME, dz_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (STATUS, dz_unit.STATUS, 18), PV_LEFT },
    { NULL }
    };

MTAB dz_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", NULL },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  NULL },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  NULL },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
        &tmxr_dscln, NULL, &dz_desc, "Disconnect a specific line" },
    { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL,
        NULL, &tmxr_show_summ, (void *) &dz_desc, "Display a summary of line states" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dz_desc, "Display current connections" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
        NULL, &tmxr_show_cstat, (void *) &dz_desc, "Display multiplexer statistics" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "LINES", "LINES=n",
        &dz_setnl, &tmxr_show_lines, (void *) &dz_desc, "Set number of lines" },
    { MTAB_XTD|MTAB_VDV|MTAB_NC, 0, NULL, "LOG=n=file",
        &dz_set_log, NULL, (void *)&dz_desc },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, NULL, "NOLOG",
        &dz_set_nolog, NULL, (void *)&dz_desc, "Disable logging on designated line" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "LOG", NULL,
        NULL, &dz_show_log, (void *)&dz_desc, "Display logging for all lines" },
    { 0 }
    };

DEVICE dz_dev = {
    "DZ", &dz_unit, dz_reg, dz_mod,
    1, 10, 31, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &dz_reset,
    NULL, &dz_attach, &dz_detach,
    &dz_dib, DEV_MUX | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dz_help, NULL, NULL, &dz_description
    };



/* IOT routine */
t_stat dz_devio(uint32 dev, uint64 *data) {
    UNIT *uptr = &dz_unit;
    TMLN *lp;
    int   ln;

    switch(dev & 3) {
    case CONI:
         /* Check if we might have any interrupts pending */
         if ((uptr->STATUS & (RCV_PI|XMT_PI)) == 0)
             dz_doscan(uptr);
         *data = uptr->STATUS & (PI_CHN|RCV_PI|XMT_PI);
         sim_debug(DEBUG_CONI, &dz_dev, "DC %03o CONI %06o PC=%o\n",
               dev, (uint32)*data, PC);
         break;

    case CONO:
         /* Set PI */
         uptr->STATUS &= ~PI_CHN;
         uptr->STATUS |= PI_CHN & *data;
         if (*data & RST_SCN)
             dz_l_count = 0;
         if (*data & DTR_SET)
             uptr->STATUS |= DTR_SET;
         if (*data & CLR_SCN) {
             uptr->STATUS &= PI_CHN;
             for (ln = 0; ln < dz_desc.lines; ln++) {
                lp = &dz_ldsc[ln];
                if (lp->conn) {
                    tmxr_linemsg (lp, "\r\nLine Hangup\r\n");
                    tmxr_reset_ln(lp);
                }
             }
             tx_enable = 0;
             dz_enable = 0;
             rx_rdy = 0;                                /* Flags */
             rx_conn = 0;
             dz_ring = 0;
             dz_l_status = 0;
         }

         sim_debug(DEBUG_CONO, &dz_dev, "DC %03o CONO %06o PC=%06o\n",
               dev, (uint32)*data, PC);
         dz_doscan(uptr);
         break;

    case DATAO:
         if (*data & (LFLAG << 18))
             ln = (*data >> 18) & 077;
         else
             ln = dz_l_count;
         if (ln >= dz_modem) {
             if (*data & CAUSE_PI)
                dz_l_status |= (1LL << ln);
             else
                dz_l_status &= ~(1LL << ln);
             ln -= dz_modem;
             sim_debug(DEBUG_DETAIL, &dz_dev, "DC line modem %d %03o\n",
                   ln, (uint32)(*data & 0777));
             if ((*data & OFF_HOOK) == 0) {
                uint32 mask = ~(1 << ln);
                rx_rdy &= mask;
                tx_enable &= mask;
                dz_enable &= mask;
                lp = &dz_ldsc[ln];
                if (rx_conn & (1 << ln) && lp->conn) {
                    sim_debug(DEBUG_DETAIL, &dz_dev, "DC line hangup %d\n", ln);
                    tmxr_linemsg (lp, "\r\nLine Hangup\r\n");
                    tmxr_reset_ln(lp);
                    rx_conn &= mask;
                }
             } else {
                sim_debug(DEBUG_DETAIL, &dz_dev, "DC line off-hook %d\n", ln);
                dz_enable |= 1<<ln;
                if (dz_ring & (1 << ln)) {
                    dz_l_status |= (1LL << (ln + dz_modem));
                    dz_ring &= ~(1 << ln);
                    rx_conn |= (1 << ln);
                }
             }
         } else if (ln < dz_desc.lines) {
             lp = &dz_ldsc[ln];
             if (*data & FLAG) {
                tx_enable &= ~(1 << ln);
                dz_l_status &= ~(1LL << ln);
             } else if (lp->conn) {
                int32 ch = *data & DATA;
                ch = sim_tt_outcvt(ch, TT_GET_MODE (dz_unit.flags) | TTUF_KSR);
                tmxr_putc_ln (lp, ch);
                if (lp->xmte)
                    tx_enable |= (1 << ln);
                else
                    tx_enable &= ~(1 << ln);
                dz_l_status |= (1LL << ln);
             }
         }
         dz_doscan(uptr);
         sim_debug(DEBUG_DATAIO, &dz_dev, "DC %03o DATO %012llo PC=%06o\n",
                    dev, *data, PC);
         break;

    case DATAI:
         ln = dz_l_count;
         *data = (uint64)(ln) << 18;
         if (ln >= dz_modem) {
             dz_l_status &= ~(1LL << ln);
             ln = ln - dz_modem;
             lp = &dz_ldsc[ln];
             if (dz_enable & (1 << ln))
                *data |= FLAG|OFF_HOOK;
             if (rx_conn & (1 << ln) && lp->conn)
                *data |= FLAG|CTS;
             if (dz_ring & (1 << ln))
                *data |= FLAG|RES_DET;
         } else if (ln < dz_desc.lines) {
             /* Nothing happens if no recieve data, which is transmit ready */
             lp = &dz_ldsc[ln];
             if (tmxr_rqln (lp) > 0) {
                int32 ch = tmxr_getc_ln (lp);
                if (ch & SCPE_BREAK)                      /* break? */
                    ch = 0;
                else
                    ch = sim_tt_inpcvt (ch, TT_GET_MODE(dz_unit.flags) | TTUF_KSR);
                *data |= FLAG | (uint64)(ch & DATA);
             }
             if (tmxr_rqln (lp) > 0) {
                rx_rdy |= 1 << ln;
                dz_l_status |= (1LL << ln);
             } else {
                rx_rdy &= ~(1 << ln);
                dz_l_status &= ~(1LL << ln);
             }
         }
         dz_doscan(uptr);
         sim_debug(DEBUG_DATAIO, &dz_dev, "DC %03o DATI %012llo PC=%06o\n",
                    dev, *data, PC);
         break;
    }
    return SCPE_OK;
}


/* Unit service */

t_stat dz_svc (UNIT *uptr)
{
int32 ln;

    if ((uptr->flags & UNIT_ATT) == 0)                  /* attached? */
        return SCPE_OK;
    ln = tmxr_poll_conn (&dz_desc);                     /* look for connect */
    if (ln >= 0) {                                      /* got one? rcv enb*/
        dz_ldsc[ln].rcve = 1;
        dz_ring |= (1 << ln);
        dz_l_status |= (1LL << (ln + dz_modem));        /* Flag modem line */
        sim_debug(DEBUG_DETAIL, &dz_dev, "DC line connect %d\n", ln);
    }
    tmxr_poll_tx(&dz_desc);
    tmxr_poll_rx(&dz_desc);
    for (ln = 0; ln < dz_desc.lines; ln++) {
       /* Check if buffer empty */
       if (dz_ldsc[ln].xmte && ((dz_l_status & (1ll << ln)) != 0)) {
           tx_enable |= 1 << ln;
       }

       /* Check to see if any pending data for this line */
       if (tmxr_rqln(&dz_ldsc[ln]) > 0) {
           rx_rdy |= (1 << ln);
           dz_l_status |= (1LL << ln);                  /* Flag line */
           sim_debug(DEBUG_DETAIL, &dz_dev, "DC recieve %d\n", ln);
       }
       /* Check if disconnect */
       if ((rx_conn & (1 << ln)) != 0 && dz_ldsc[ln].conn == 0) {
           rx_conn &= ~(1 << ln);
           dz_l_status |= (1LL << (ln + dz_modem));     /* Flag modem line */
           sim_debug(DEBUG_DETAIL, &dz_dev, "DC line disconnect %d\n", ln);
       }
    }

    /* If any pending status request, raise the PI signal */
    if (dz_l_status)
        set_interrupt(DC_DEVNUM, uptr->STATUS);
    sim_clock_coschedule(uptr, tmxr_poll);              /* continue poll */
    return SCPE_OK;
}

/* Scan to see if something to do */
t_stat dz_doscan (UNIT *uptr) {
   int32 lmask;

   uptr->STATUS &= ~(RCV_PI|XMT_PI);
   clr_interrupt(DC_DEVNUM);
   for (;dz_l_status != 0; dz_l_count++) {
      dz_l_count &= 077;
      /* Check if we found it */
      if (dz_l_status & (1LL << dz_l_count)) {
         /* Check if modem control or data line */
         if (dz_l_count >= dz_modem) {
            uptr->STATUS |= RCV_PI;
         } else {
            /* Must be data line */
            lmask = 1 << dz_l_count;
            if (rx_rdy & lmask)
                uptr->STATUS |= RCV_PI;
            if (tx_enable & lmask)
                uptr->STATUS |= XMT_PI;
         }
         /* Stop scanner */
         set_interrupt(DC_DEVNUM, uptr->STATUS);
         return SCPE_OK;
      }
   }
   return SCPE_OK;
}

/* Reset routine */

t_stat dz_reset (DEVICE *dptr)
{

    if (dz_unit.flags & UNIT_ATT)                           /* if attached, */
        sim_activate (&dz_unit, tmxr_poll);                 /* activate */
    else
        sim_cancel (&dz_unit);                             /* else stop */
    tx_enable = 0;
    rx_rdy = 0;                             /* Flags */
    rx_conn = 0;
    dz_l_status = 0;
    dz_l_count = 0;
    dz_unit.STATUS = 0;
    clr_interrupt(DC_DEVNUM);
    return SCPE_OK;
}


/* SET LINES processor */

t_stat dz_setnl (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 newln, i, t;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    newln = (int32) get_uint (cptr, 10, DC10_MLINES, &r);
    if ((r != SCPE_OK) || (newln == dz_desc.lines))
        return r;
    if (newln > dz_modem)
        return SCPE_ARG;
    if ((newln == 0) || (newln > DC10_MLINES) || (newln % 8) != 0)
        return SCPE_ARG;
    if (newln < dz_desc.lines) {
        for (i = newln - 1, t = 0; i < dz_desc.lines; i++)
            t = t | dz_ldsc[i].conn;
        if (t && !get_yn ("This will disconnect users; proceed [N]?", FALSE))
            return SCPE_OK;
        for (i = newln - 1; i < dz_desc.lines; i++) {
            if (dz_ldsc[i].conn) {
                tmxr_linemsg (&dz_ldsc[i], "\r\nOperator disconnected line\r\n");
                tmxr_send_buffered_data (&dz_ldsc[i]);
            }
            tmxr_detach_ln (&dz_ldsc[i]);               /* completely reset line */
        }
    }
    if (dz_desc.lines < newln)
        memset (dz_ldsc + dz_desc.lines, 0, sizeof(*dz_ldsc)*(newln-dz_desc.lines));
    dz_desc.lines = newln;
    return dz_reset (&dz_dev);                         /* setup lines and auto config */
}

/* SET LOG processor */

t_stat dz_set_log (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    char gbuf[CBUFSIZE];
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    cptr = get_glyph (cptr, gbuf, '=');
    if ((cptr == NULL) || (*cptr == 0) || (gbuf[0] == 0))
        return SCPE_ARG;
    ln = (int32) get_uint (gbuf, 10, dz_desc.lines, &r);
    if ((r != SCPE_OK) || (ln > dz_desc.lines))
        return SCPE_ARG;
    return tmxr_set_log (NULL, ln, cptr, desc);
}

/* SET NOLOG processor */

t_stat dz_set_nolog (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    int32 ln;

    if (cptr == NULL)
        return SCPE_ARG;
    ln = (int32) get_uint (cptr, 10, dz_desc.lines, &r);
    if ((r != SCPE_OK) || (ln > dz_desc.lines))
        return SCPE_ARG;
    return tmxr_set_nolog (NULL, ln, NULL, desc);
}

/* SHOW LOG processor */

t_stat dz_show_log (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int32 i;

    for (i = 0; i < dz_desc.lines; i++) {
        fprintf (st, "line %d: ", i);
        tmxr_show_log (st, NULL, i, desc);
        fprintf (st, "\n");
        }
    return SCPE_OK;
}


/* Attach routine */

t_stat dz_attach (UNIT *uptr, CONST char *cptr)
{
t_stat reason;

reason = tmxr_attach (&dz_desc, uptr, cptr);
if (reason != SCPE_OK)
  return reason;
sim_activate (uptr, tmxr_poll);
return SCPE_OK;
}

/* Detach routine */

t_stat dz_detach (UNIT *uptr)
{
  int32  i;
  t_stat reason;
reason = tmxr_detach (&dz_desc, uptr);
for (i = 0; i < dz_desc.lines; i++)
    dz_ldsc[i].rcve = 0;
sim_cancel (uptr);
return reason;
}

t_stat dz_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "DC10E Terminal Interfaces\n\n");
fprintf (st, "The DC10 supported up to 8 blocks of 8 lines. Modem control was on a seperate\n");
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
fprint_reg_help (st, &dz_dev);
fprintf (st, "\nThe additional terminals do not support save and restore.  All open connections\n");
fprintf (st, "are lost when the simulator shuts down or DC is detached.\n");
return SCPE_OK;
}

const char *dz_description (DEVICE *dptr)
{
return "DZ11 asynchronous line interface";
}

#endif
