/* ks10_lp.c: PDP-10 LP20 printer.

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

#ifndef NUM_DEVS_LP20
#define NUM_DEVS_LP20 0
#endif

#if (NUM_DEVS_LP20 > 0)

#define COL      u4
#define POS      u5
#define LINE     u6
#define LPST     us9
#define LPCNT    us10

#define EOFFLG   001      /* Tops 20 wants EOF */
#define HDSFLG   002      /* Tell Tops 20 The current device status */
#define ACKFLG   004      /* Post an acknowwledge message */
#define INTFLG   010      /* Send interrupt */
#define DELFLG   020      /* Previous character was delimiter */

#define MARGIN   6

#define UNIT_V_CT    (UNIT_V_UF + 0)
#define UNIT_UC      (1 << UNIT_V_CT)
#define UNIT_CT      (3 << UNIT_V_CT)



t_stat          lp20_svc (UNIT *uptr);
t_stat          lp20_reset (DEVICE *dptr);
t_stat          lp20_attach (UNIT *uptr, CONST char *cptr);
t_stat          lp20_detach (UNIT *uptr);
t_stat          lp20_setlpp(UNIT *, int32, CONST char *, void *);
t_stat          lp20_getlpp(FILE *, UNIT *, int32, CONST void *);
t_stat          lp20_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                         const char *cptr);
const char     *lp20_description (DEVICE *dptr);

char            lp20_buffer[134 * 3];

#define LP20_RAM_RAP  010000     /* RAM Parity */
#define LP20_RAM_INT  04000      /* Interrrupt bit */
#define LP20_RAM_DEL  02000      /* Delimiter bit */
#define LP20_RAM_TRN  01000      /* Translation bite */
#define LP20_RAM_PI   00400      /* Paper Instruction */
#define LP20_RAM_CHR  00377      /* Character translation */

uint16          lp20_vfu[256];
uint16          lp20_ram[256];
uint16          lp20_dvfu[] = {   /* Default VFU */
    /* 66 line page with 6 line margin */
    00377,    /* Line   0     8  7  6  5  4  3  2  1 */
    00220,    /* Line   1     8        5             */
    00224,    /* Line   2     8        5     3       */
    00230,    /* Line   3     8        5  4          */
    00224,    /* Line   4     8        5     3       */
    00220,    /* Line   5     8        5             */
    00234,    /* Line   6     8        5  4  3       */
    00220,    /* Line   7     8        5             */
    00224,    /* Line   8     8        5     3       */
    00230,    /* Line   9     8        5  4          */
    00264,    /* Line  10     8     6  5     3       */
    00220,    /* Line  11     8        5             */
    00234,    /* Line  12     8        5  4  3       */
    00220,    /* Line  13     8        5             */
    00224,    /* Line  14     8        5     3       */
    00230,    /* Line  15     8        5  4          */
    00224,    /* Line  16     8        5     3       */
    00220,    /* Line  17     8        5             */
    00234,    /* Line  18     8        5  4  3       */
    00220,    /* Line  19     8        5             */
    00364,    /* Line  20     8  7  6  5     3       */
    00230,    /* Line  21     8        5  4          */
    00224,    /* Line  22     8        5     3       */
    00220,    /* Line  23     8        5             */
    00234,    /* Line  24     8        5  4  3       */
    00220,    /* Line  25     8        5             */
    00224,    /* Line  26     8        5     3       */
    00230,    /* Line  27     8        5  4          */
    00224,    /* Line  28     8        5     3       */
    00220,    /* Line  29     8        5             */
    00276,    /* Line  30     8     6  5  4  3  2    */
    00220,    /* Line  31     8        5             */
    00224,    /* Line  32     8        5     3       */
    00230,    /* Line  33     8        5  4          */
    00224,    /* Line  34     8        5     3       */
    00220,    /* Line  35     8        5             */
    00234,    /* Line  36     8        5  4  3       */
    00220,    /* Line  37     8        5             */
    00224,    /* Line  38     8        5     3       */
    00230,    /* Line  39     8        5  4          */
    00364,    /* Line  40     8  7  6  5     3       */
    00220,    /* Line  41     8        5             */
    00234,    /* Line  42     8        5  4  3       */
    00220,    /* Line  43     8        5             */
    00224,    /* Line  44     8        5     3       */
    00230,    /* Line  45     8        5  4          */
    00224,    /* Line  46     8        5     3       */
    00220,    /* Line  47     8        5             */
    00234,    /* Line  48     8        5  4  3       */
    00220,    /* Line  49     8        5             */
    00264,    /* Line  50     8     6  5     3       */
    00230,    /* Line  51     8        5  4          */
    00224,    /* Line  52     8        5     3       */
    00220,    /* Line  53     8        5             */
    00234,    /* Line  54     8        5  4  3       */
    00220,    /* Line  55     8        5             */
    00224,    /* Line  56     8        5     3       */
    00230,    /* Line  57     8        5  4          */
    00224,    /* Line  58     8        5     3       */
    00220,    /* Line  59     8        5             */
    00020,    /* Line  60              5             */
    00020,    /* Line  61              5             */
    00020,    /* Line  62              5             */
    00020,    /* Line  63              5             */
    00020,    /* Line  64              5             */
    04020,    /* Line  65 12           5             */
   010000,    /* End of form */
};


/* LPT data structures

   lp20_dev      LPT device descriptor
   lp20_unit     LPT unit descriptor
   lp20_reg      LPT register list
*/

UNIT lp20_unit = {
    UDATA (&lp20_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 66), 100
    };

REG lp20_reg[] = {
   {BRDATA(BUFFER, lp20_buffer, 16, 8, sizeof(lp20_buffer)), REG_HRO},
   {BRDATA(VFU, lp20_vfu, 16, 16, (sizeof(lp20_vfu)/sizeof(uint16))), REG_HRO},
   {BRDATA(RAM, lp20_ram, 16, 16, (sizeof(lp20_ram)/sizeof(uint16))), REG_HRO},
   {SAVEDATA(QUEUE, lp20_queue) },
    { NULL }
};

MTAB lp20_mod[] = {
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "addr", "addr",  &uba_set_addr, uba_show_addr,
              NULL, "Sets address of LP20" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "vect", "vect",  &uba_set_vect, uba_show_vect,
              NULL, "Sets vect of LP20" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "br", "br",  &uba_set_br, uba_show_br,
              NULL, "Sets br of LP20" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "ctl", "ctl",  &uba_set_ctl, uba_show_ctl,
              NULL, "Sets uba of LP20" },
    {UNIT_CT, 0, "Lower case", "LC", NULL},
    {UNIT_CT, UNIT_UC, "Upper case", "UC", NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LINESPERPAGE", "LINESPERPAGE",
        &lp20_setlpp, &lp20_getlpp, NULL, "Number of lines per page"},
    { 0 }
};

DEVICE lp20_dev = {
    "LP20", &lp20_unit, lp20_reg, lp20_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &lp20_reset,
    NULL, &lp20_attach, &lp20_detach,
    NULL, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lp20_help, NULL, NULL, &lp20_description
};

int
dz_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access)
{
    struct pdp_dib   *dibp = (DIB *)dptr->ctxt;
    int               base;
    uint16            temp;
    int               ln;
    TMLN             *lp;
    int               i;

    addr &= dibp->uba_mask;
    sim_debug(DEBUG_DETAIL, dptr, "DZ%o write %06o %06o %o\n", base,
             addr, data, access);

    switch (addr & 06) {
    case 0:
            if (access == BYTE) {
                temp = dz_csr[base];
                if (addr & 1)
                    data = data | (temp & 0377);
                else
                    data = (temp & 0177400) | data;
            }
            if (data & CLR) {
                dz_csr[base] = 0;
                dz_recv[base].in_ptr = dz_recv[base].out_ptr = 0;
                dz_recv[base].len = 0;
                /* Set up the current status */
		ln = base << 3;
                for (i = 0; i < 8; i++) {
                    dz_flags[ln + i] &= ~LINE_EN;
                }
                return 0;
            }
            dz_csr[base] &= ~(TIE|SAE|RIE|MSE|CLR|MAINT);
            dz_csr[base] |= data & (TIE|SAE|RIE|MSE|MAINT);
            break;

    case 2:
            ln = (data & 07) + (base << 3);
            dz_ldsc[ln].rcve = (data & RXON) != 0;
            break;

    case 4:
            temp = 0;
            ln = base << 3;
            /* Set up the current status */
            for (i = 0; i < 8; i++) {
                if (dz_flags[ln + i] & LINE_EN)
                    temp |= LINE_ENB << i;
                if (dz_flags[ln + i] & DTR_FLAG)
                    temp |= DTR << i;
                dz_flags[ln + i] = 0;
            }
            if (access == BYTE) {
                if (addr & 1)
                    data = data | (temp & 0377);
                else
                    data = (temp & 0177400) | data;
            }
            for (i = 0; i < 8; i++) {
                lp = &dz_ldsc[ln + i];
                if ((data & (LINE_ENB << i)) != 0)
                    dz_flags[ln + i] |= LINE_EN;
                if ((data & (DTR << i)) != 0)
                    dz_flags[ln + i] |= DTR_FLAG;
                if (dz_flags[ln + i] & DTR_FLAG)
                    tmxr_set_get_modem_bits(lp, TMXR_MDM_OUTGOING, 0, NULL);
                else
                    tmxr_set_get_modem_bits(lp, 0, TMXR_MDM_OUTGOING, NULL);
       sim_debug(DEBUG_DETAIL, dptr, "DZ%o sstatus %07o %o %o\n", base, data, i, dz_flags[ln+i]);
            }
            break;

    case 6:
            if (access == BYTE && (addr & 1) != 0) {
                break;
            }

            if ((dz_csr[base] & TRDY) == 0)
                break;

            ln = ((dz_csr[base] & TLINE) >> TLINE_V) + (base << 3);
            lp = &dz_ldsc[ln];

            if ((dz_flags[ln] & LINE_EN) != 0 && lp->conn) {
                int32  ch = data & 0377;
                /* Try and send character */
                t_stat r = tmxr_putc_ln(lp, ch);
                /* If character did not send, queue it */
                if (r == SCPE_STALL)
                    dz_xmit[ln] = TRDY | ch;
             }
             break;
    }

    dz_csr[base] &= ~TRDY;
    if ((dz_csr[base] & MSE) == 0)
        return 0;
    ln = ((dz_csr[base] & TLINE) >> TLINE_V) + (base << 3);
    /* See if there is another line ready */
    for (i = 0; i < 8; i++) {
        ln = (ln & 070) | ((ln + 1) & 07);
        lp = &dz_ldsc[ln];
        /* Connected and empty xmit_buffer */
        if ((dz_flags[ln] & LINE_EN) != 0 && lp->conn && dz_xmit[ln] == 0) {
            dz_csr[base] &= ~(TLINE);
            dz_csr[base] |= TRDY | ((ln & 07) << TLINE_V);
            break;
        }
    }
    dz_checkirq(dibp);
    return 0;
}

int
dz_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access)
{
    struct pdp_dib   *dibp = (DIB *)dptr->ctxt;
    int               base;
    uint16            temp;
    int               ln;
    TMLN             *lp;
    int               i;

    addr &= dibp->uba_mask;
    switch (addr & 06) {
    case 0:
            *data = dz_csr[base];
            break;

    case 2:
            *data = 0;
            if ((dz_csr[base] & MSE) == 0)
                return 0;
            dz_csr[base] &= ~(SA|RDONE);
            if (!empty(&dz_recv[base])) {
                *data = dz_recv[base].buff[dz_recv[base].out_ptr];
                inco(&dz_recv[base]);
                dz_recv[base].len = 0;
            }
            if (!empty(&dz_recv[base]))
                dz_csr[base] |= RDONE;
            dz_checkirq(dibp);
            break;

    case 4:
            temp = 0;
            ln = base << 3;
            /* Set up the current status */
            for (i = 0; i < 8; i++) {
     sim_debug(DEBUG_DETAIL, dptr, "DZ%o status %o %o\n", base, i, dz_flags[ln+i]);
                if (dz_flags[ln + i] & LINE_EN)
                    temp |= LINE_ENB << i;
                if (dz_flags[ln + i] & DTR_FLAG)
                    temp |= DTR << i;
            }
            *data = temp;
            break;

     case 6:
            temp = (uint16)dz_ring[base];
            ln = base << 3;
            for (i = 0; i < 8; i++) {
                lp = &dz_ldsc[ln + i];
                if (lp->conn)
                    temp |= CO << i;
            }
            dz_ring[base] = 0;
            *data = temp;
            break;
     }
     sim_debug(DEBUG_DETAIL, dptr, "DZ%o read %06o %06o %o\n", base,
             addr, *data, access);
     return 0;
}


void
lp20_printline(UNIT *uptr, int nl) {
    int     trim = 0;

    /* Trim off trailing blanks */
    while (uptr->COL >= 0 && lp20_buffer[uptr->COL - 1] == ' ') {
         uptr->COL--;
         trim = 1;
    }
    lp20_buffer[uptr->COL] = '\0';
    sim_debug(DEBUG_DETAIL, &lp20_dev, "LP output %d %d [%s]\n", uptr->COL, nl,
              lp20_buffer);
    /* Stick a carraige return and linefeed as needed */
    if (uptr->COL != 0 || trim)
        lp20_buffer[uptr->COL++] = '\r';
    if (nl != 0) {
        lp20_buffer[uptr->COL++] = '\n';
        uptr->LINE++;
    }
    if (nl > 0 && lp20_vfu[uptr->LINE] == 010000) {
        lp20_buffer[uptr->COL++] = '\f';
        uptr->LINE = 1;
    } else if (nl < 0 && uptr->LINE >= (int32)uptr->capac) {
        uptr->LINE = 1;
    }

    sim_fwrite(&lp20_buffer, 1, uptr->COL, uptr->fileref);
    uptr->pos += uptr->COL;
    uptr->COL = 0;
    return;
}


/* Unit service */
void
lp20_output(UNIT *uptr, char c) {

    if (c == 0)
       return;
    if (uptr->COL == 132)
        lp20_printline(uptr, 1);
    if ((uptr->flags & UNIT_UC) && (c & 0140) == 0140)
        c &= 0137;
    else if (c >= 040 && c < 0177) { /* If printable */
        lp20_buffer[uptr->COL++] = c;
    } if (c == 011) { /* Tab */
        lp20_buffer[uptr->COL++] = ' ';
        while ((uptr->COL & 07) != 0)
            lp20_buffer[uptr->COL++] = ' ';
    }
    return;
}

t_stat lp20_svc (UNIT *uptr)
{
    char    ch;
    uint16  ram_ch;
    uint16  data1[5];

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_OK;
    if (dte_dev.flags & TYPE_RSX20 && uptr->LPST & HDSFLG) {
        data1[0] = 0;

        data1[1] = (uptr->LINE == 1) ? 01<<8: 0;
        sim_debug(DEBUG_DETAIL, &dte_dev, "LPT status %06o \n", uptr->LPST);
        if (uptr->LPST & EOFFLG) {
            data1[0] |= 040 << 8;
            uptr->LPCNT = 0;
        }
        if (uptr->LPST & INTFLG) {
            data1[1] |= 02 << 8;
            uptr->LPCNT = 0;
        }
        data1[2] = 0110200; 
        if (dte_queue(PRI_EMHDS+PRI_IND_FLG, PRI_EMLPT, 4, data1) == 0)
            sim_activate(uptr, 1000);
        uptr->LPST &= ~(HDSFLG);
    }

    if (empty(&lp20_queue))
           return SCPE_OK;
    while (not_empty(&lp20_queue)) {
        ch = lp20_queue.buff[lp20_queue.out_ptr];
        inco(&lp20_queue);
        ram_ch = lp20_ram[(int)ch];

        /* If previous was delimiter or translation do it */
        if (uptr->LPST & DELFLG || (ram_ch &(LP20_RAM_DEL|LP20_RAM_TRN)) != 0) {
            ch = ram_ch & LP20_RAM_CHR;
            uptr->LPST &= ~DELFLG;
            if (ram_ch & LP20_RAM_DEL)
               uptr->LPST |= DELFLG;
        }
        /* Flag if interrupt set */
        if (ram_ch & LP20_RAM_INT)
            uptr->LPST |= HDSFLG|INTFLG;
        /* Check if paper motion */
        if (ram_ch & LP20_RAM_PI) {
            int   lines = 0;  /* Number of new lines to output */
            /* Print any buffered line */
            lp20_printline(uptr, (ram_ch & 037) != 020);
            sim_debug(DEBUG_DETAIL, &lp20_dev, "LP deque %02x %04x\n",
                                 ch, ram_ch);
            if ((ram_ch & 020) == 0) { /* Find channel mark in output */
               while ((lp20_vfu[uptr->LINE] & (1 << (ram_ch & 017))) == 0) {
                   sim_debug(DEBUG_DETAIL, &lp20_dev,
                                 "LP skip chan %04x %04x %d\n",
                                 lp20_vfu[uptr->LINE], ram_ch, uptr->LINE);
                   if (lp20_vfu[uptr->LINE] & 010000) { /* Hit bottom of form */
                      sim_fwrite("\014", 1, 1, uptr->fileref);
                      uptr->pos++;
                      lines = 0;
                      uptr->LINE = 1;
                      break;
                   }
                   lines++;
                   uptr->LINE++;
               }
            } else {
               while ((ram_ch & 017) != 0) {
                   sim_debug(DEBUG_DETAIL, &lp20_dev,
                                "LP skip line %04x %04x %d\n",
                                 lp20_vfu[uptr->LINE], ram_ch, uptr->LINE);
                   if (lp20_vfu[uptr->LINE] & 010000) { /* Hit bottom of form */
                      sim_fwrite("\014", 1, 1, uptr->fileref);
                      uptr->pos++;
                      lines = 0;
                      uptr->LINE = 1;
                   }
                   lines++;
                   uptr->LINE++;
                   ram_ch--;
               }
            }
            for(;lines > 0; lines--) {
               sim_fwrite("\r\n", 1, 2, uptr->fileref);
               uptr->pos+=2;
            }
        } else if (ch != 0) {
            sim_debug(DEBUG_DETAIL, &lp20_dev, "LP deque %02x '%c' %04x\n",
                                  ch, ch, ram_ch);
            lp20_output(uptr, ch);
        }
    }
    if (empty(&lp20_queue)) {
        data1[0] = 0;
        if (dte_queue(PRI_EMLBE, PRI_EMLPT, 1, data1) == 0)
           sim_activate(uptr, 1000);
        if (dte_dev.flags & TYPE_RSX20) {
            if (uptr->LINE == 0) {
                uptr->LPST |= HDSFLG;
               sim_activate(uptr, 1000);
            }
        }
    }
    return SCPE_OK;
}

/* Reset routine */

t_stat lp20_reset (DEVICE *dptr)
{
    UNIT *uptr = &lp20_unit;
    int   i;
    uptr->POS = 0;
    uptr->COL = 0;
    uptr->LINE = 1;
    /* Clear RAM & VFU */
    for (i = 0; i < 256; i++) {
       lp20_ram[i] = 0;
       lp20_vfu[i] = 0;
    }

    /* Load default VFU into VFU */
    memcpy(&lp20_vfu, lp20_dvfu, sizeof(lp20_dvfu));
    lp20_ram[012] = LP20_RAM_TRN|LP20_RAM_PI|7;   /* Line feed, print line, space one line */
    lp20_ram[013] = LP20_RAM_TRN|LP20_RAM_PI|6;   /* Vertical tab, Skip mod 20 */
    lp20_ram[014] = LP20_RAM_TRN|LP20_RAM_PI|0;   /* Form feed, skip to top of page */
    lp20_ram[015] = LP20_RAM_TRN|LP20_RAM_PI|020; /* Carrage return */
    lp20_ram[020] = LP20_RAM_TRN|LP20_RAM_PI|1;   /* Skip half page */
    lp20_ram[021] = LP20_RAM_TRN|LP20_RAM_PI|2;   /* Skip even lines */
    lp20_ram[022] = LP20_RAM_TRN|LP20_RAM_PI|3;   /* Skip triple lines */
    lp20_ram[023] = LP20_RAM_TRN|LP20_RAM_PI|4;   /* Skip one line */
    lp20_ram[024] = LP20_RAM_TRN|LP20_RAM_PI|5;
    sim_cancel (&lp20_unit);                                 /* deactivate unit */
    return SCPE_OK;
}

/* Attach routine */

t_stat lp20_attach (UNIT *uptr, CONST char *cptr)
{
    sim_switches |= SWMASK ('A');   /* Position to EOF */
    return attach_unit (uptr, cptr);
}

/* Detach routine */

t_stat lp20_detach (UNIT *uptr)
{
    return detach_unit (uptr);
}

/*
 * Line printer routines
 */

t_stat
lp20_setlpp(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_value   i;
    t_stat    r;
    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    i = get_uint (cptr, 10, 100, &r);
    if (r != SCPE_OK)
        return SCPE_ARG;
    uptr->capac = (t_addr)i;
    uptr->LINE = 0;
    return SCPE_OK;
}

t_stat
lp20_getlpp(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "linesperpage=%d", uptr->capac);
    return SCPE_OK;
}

t_stat lp20_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
        const char *cptr)
{
fprintf (st, "Line Printer (LPT)\n\n");
fprintf (st, "The line printer (LPT) writes data to a disk file.  The POS register specifies\n");
fprintf (st, "the number of the next data item to be written.  Thus, by changing POS, the\n");
fprintf (st, "user can backspace or advance the printer.\n");
fprintf (st, "The Line printer can be configured to any number of lines per page with the:\n");
fprintf (st, "        sim> SET %s0 LINESPERPAGE=n\n\n", dptr->name);
fprintf (st, "The default is 66 lines per page.\n\n");
fprintf (st, "The device address of the Line printer can be changed\n");
fprintf (st, "        sim> SET %s0 DEV=n\n\n", dptr->name);
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *lp20_description (DEVICE *dptr)
{
    return "LP20 line printer" ;
}

#endif
