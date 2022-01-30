/* ibm360_lpr.c: IBM 360 Line Printer

   Copyright (c) 2017-2020, Richard Cornwell

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

   This is the standard line printer.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "ibm360_defs.h"
#include "sim_defs.h"
#include <ctype.h>

#ifdef NUM_DEVS_LPR
#define UNIT_LPR       UNIT_ATTABLE | UNIT_DISABLE | UNIT_SEQ
#define UNIT_V_FCB     (UNIT_V_UF + 0)
#define UNIT_M_FCB     (3 << UNIT_V_FCB)

/* u3 hold command and status information */
#define CHN_SNS        0x04       /* Sense command */

#define LPR_WR         0x01       /* Write command */
#define LPR_SPKCMD     0x03       /* Skip command */
#define LPR_SPCMSK     0x18       /* Space after printing */
#define LPR_SKIP       0x80       /* Skip Flag */
#define LPR_SKPCHN     0x78       /* Skip Channel */
#define LPR_CMDMSK     0xff       /* Mask command part. */
#define LPR_FULL       0x100      /* Buffer full */
#define LPR_DATCHK     0x200      /* Don't return data-check */

/* Upper 11 bits of u3 hold the device address */

/* u4 holds current line */
/* in u5 packs sense byte 0,1 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ      0x80      /* Command reject */
#define SNS_INTVENT     0x40      /* Unit intervention required */
#define SNS_BUSCHK      0x20      /* Parity error on bus */
#define SNS_EQUCHK      0x10      /* Equipment check */
#define SNS_DATCHK      0x08      /* Data Check */
#define SNS_OVRRUN      0x04      /* Data overrun */
#define SNS_SEQUENCE    0x02      /* Unusual sequence */
#define SNS_CHN9        0x01      /* Channel 9 on printer */
#define SNS_CHN12       0x100

/* u6 hold buffer position */

#define CMD    u3
#define LINE   u4
#define SNS    u5
#define POS    u6


/* std devices. data structures

   lpr_dev       Line Printer device descriptor
   lpr_unit      Line Printer unit descriptor
   lpr_reg       Line Printer register list
   lpr_mod       Line Printer modifiers list
*/


struct _lpr_data
{
    uint8               lbuff[145];       /* Output line buffer */
    uint8               fcs[256];         /* Form control buffer */
    CONST uint16       *fcb;              /* Pointer to forms control */
}
lpr_data[NUM_DEVS_LPR];

uint8               lpr_startio(UNIT *uptr);
uint8               lpr_startcmd(UNIT *, uint8);
void                lpr_ini(UNIT *, t_bool);
t_stat              lpr_srv(UNIT *);
t_stat              lpr_reset(DEVICE *);
t_stat              lpr_attach(UNIT *, CONST char *);
t_stat              lpr_detach(UNIT *);
t_stat              lpr_setlpp(UNIT *, int32, CONST char *, void *);
t_stat              lpr_getlpp(FILE *, UNIT *, int32, CONST void *);
t_stat              lpr_setfcb(UNIT *, int32, CONST char *, void *);
t_stat              lpr_getfcb(FILE *, UNIT *, int32, CONST void *);
t_stat              lpr_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *lpr_description(DEVICE *dptr);

UNIT                lpr_unit[] = {
    {UDATA(lpr_srv, UNIT_LPR, 66), 300, UNIT_ADDR(0x0E)},
#if NUM_DEVS_LPR > 1
    {UDATA(lpr_srv, UNIT_LPR | UNIT_DIS, 66), 300, UNIT_ADDR(0x1E)},
#if NUM_DEVS_LPR > 2
    {UDATA(lpr_srv, UNIT_LPR | UNIT_DIS, 66), 300, UNIT_ADDR(0x40E)},
#if NUM_DEVS_LPR > 3
    {UDATA(lpr_srv, UNIT_LPR | UNIT_DIS, 66), 300, UNIT_ADDR(0x41E)},
#endif
#endif
#endif
};

MTAB                lpr_mod[] = {
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LINESPERPAGE", "LINESPERPAGE",
       &lpr_setlpp, &lpr_getlpp, NULL, "Number of lines per page"},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_NC, 0, "FCB", "FCB={LEGACY|STD1)",
       &lpr_setfcb, &lpr_getfcb, NULL, NULL },
    {0}
};

struct dib lpr_dib = { 0xFF, 1, lpr_startio, lpr_startcmd, NULL, lpr_unit, lpr_ini};

DEVICE              lpr_dev = {
    "LPR", lpr_unit, NULL, lpr_mod,
    NUM_DEVS_LPR, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &lpr_attach, &lpr_detach,
    &lpr_dib, DEV_UADDR | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lpr_help, NULL, NULL, &lpr_description
};

static CONST char *fcb_name[] = { "legacy", "std1", NULL};

static CONST uint16 legacy[] = {
/* 1      2      3      4      5      6      7      8      9     10       lines  */
0x800, 0x000, 0x000, 0x000, 0x000, 0x000, 0x400, 0x000, 0x000, 0x000, /*  1 - 10 */
0x000, 0x000, 0x200, 0x000, 0x000, 0x000, 0x000, 0x000, 0x100, 0x000, /* 11 - 20 */
0x000, 0x000, 0x000, 0x000, 0x080, 0x000, 0x000, 0x000, 0x000, 0x000, /* 21 - 30 */
0x040, 0x000, 0x000, 0x000, 0x000, 0x000, 0x020, 0x000, 0x000, 0x000, /* 31 - 40 */
0x000, 0x000, 0x010, 0x000, 0x000, 0x000, 0x000, 0x000, 0x004, 0x000, /* 41 - 50 */
0x000, 0x000, 0x000, 0x000, 0x002, 0x000, 0x000, 0x000, 0x000, 0x000, /* 51 - 60 */
0x001, 0x000, 0x008, 0x000, 0x000, 0x000, 0x1000 };                   /* 61 - 66 */
/*
    PROGRAMMMING NOTE:  the below cctape value SHOULD match
                        the same corresponding fcb value!
*/
static CONST uint16 std1[] = {
/* 1      2      3      4      5      6      7      8      9     10       lines  */
0x800, 0x000, 0x000, 0x000, 0x000, 0x000, 0x400, 0x000, 0x000, 0x000, /*  1 - 10 */
0x000, 0x000, 0x200, 0x000, 0x000, 0x000, 0x000, 0x000, 0x100, 0x000, /* 11 - 20 */
0x000, 0x000, 0x000, 0x000, 0x080, 0x000, 0x000, 0x000, 0x000, 0x000, /* 21 - 30 */
0x040, 0x000, 0x000, 0x000, 0x000, 0x000, 0x020, 0x000, 0x000, 0x000, /* 31 - 40 */
0x000, 0x000, 0x010, 0x000, 0x000, 0x000, 0x000, 0x000, 0x008, 0x000, /* 41 - 50 */
0x000, 0x000, 0x000, 0x000, 0x004, 0x000, 0x000, 0x000, 0x000, 0x000, /* 51 - 60 */
0x002, 0x000, 0x001, 0x000, 0x000, 0x000, 0x1000 };                   /* 61 - 66 */

static CONST uint16 *fcb_ptr[] = { legacy, std1, NULL, NULL};


/* Line printer routines
*/

t_stat
lpr_setlpp(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_addr i;
    if (cptr == NULL)
       return SCPE_ARG;
    if (uptr == NULL)
       return SCPE_IERR;
    i = 0;
    while(*cptr != '\0') {
       if (*cptr < '0' || *cptr > '9')
          return SCPE_ARG;
       i = (i * 10) + (*cptr++) - '0';
    }
    if (i < 20 || i > 100)
       return SCPE_ARG;
    uptr->capac = i;
    uptr->LINE = 0;
    return SCPE_OK;
}

t_stat
lpr_getlpp(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
       return SCPE_IERR;
    fprintf(st, "linesperpage=%d", uptr->capac);
    return SCPE_OK;
}

t_stat
lpr_setfcb (UNIT *uptr, int32 val, CONST char *gptr, void *desc)
{
    int      u = (uptr - lpr_unit);
    char     gbuf[CBUFSIZE], *cptr;
    char    *fname, *cp;
    int      i, j;
    CONST uint16   *fcb;

    if (!gptr || !*gptr)
        return SCPE_ARG;

    gbuf[sizeof(gbuf)-1] = '\0';
    strncpy (gbuf, gptr, sizeof(gbuf)-1);
    cptr = gbuf;

    fname = strchr (cptr, '=');
    if (fname)
        *fname++ = '\0';

    for (cp = cptr; *cp; cp++)
        *cp = (char)toupper (*cp);

    for (i = 0; fcb_name[i] != NULL; i++) {
        if (strncmp(cptr, fcb_name[i], strlen(cptr)) == 0) {
            uptr->flags &= ~(UNIT_M_FCB);
            uptr->flags |= i << UNIT_V_FCB;
            fcb = fcb_ptr[i];
            lpr_data[u].fcb = fcb;
            for (j = 0; (fcb[j] & 0x1000) == 0; j++);
            uptr->capac = j;
            return SCPE_OK;
        }
    }
    return SCPE_OK;
}

t_stat
lpr_getfcb(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    int      u = (uptr - lpr_unit);

    if (uptr == NULL)
       return SCPE_IERR;
    fprintf(st, "FCB=%s", fcb_name[(uptr->flags & UNIT_M_FCB) >> UNIT_V_FCB]);
    return SCPE_OK;
}

void
print_line(UNIT * uptr)
{

    char                out[150];       /* Temp conversion buffer */
    int                 i;
    int                 u = (uptr - lpr_unit);
    int                 l = (uptr->CMD >> 3) & 0x1f;
    int                 f;
    int                 mask;

    /* Dump buffer if full */
    if (uptr->CMD & LPR_FULL) {

        /* Try to convert to text */
        memset(out, ' ', sizeof(out));

        /* Scan each column */
        for (i = 0; i < uptr->POS; i++) {
           int         ch = lpr_data[u].lbuff[i];

           ch = ebcdic_to_ascii[ch];
           if (!isprint(ch))
              ch = '.';
           out[i] = ch;
        }

        /* Trim trailing spaces */
        for (--i; i > 0 && out[i] == ' '; i--) ;
        out[++i] = '\0';

        /* Print out buffer */
        sim_fwrite(&out, 1, i, uptr->fileref);
        uptr->pos += i;
        sim_debug(DEBUG_DETAIL, &lpr_dev, "%s\n", out);
        memset(&lpr_data[u].lbuff[0], 0, 144);
    }

    f = 0;
    if (l < 4) {
        while(l != 0) {
            sim_fwrite("\r\n", 1, 2, uptr->fileref);
            f = 1;
            uptr->pos += 2;
            if ((uptr->CMD & 03) == 0x1) {
               if ((lpr_data[u].fcb[uptr->LINE] & (0x1000 >> 9)) != 0)
                  uptr->SNS |= SNS_CHN9;
               if ((lpr_data[u].fcb[uptr->LINE] & (0x1000 >> 12)) != 0)
                  uptr->SNS |= SNS_CHN12;
            }
            if ((lpr_data[u].fcb[uptr->LINE] & 0x1000) != 0 ||
                 ((uint32)uptr->LINE) >= uptr->capac) {
               if (f)
                   sim_fwrite("\r\n", 1, 2, uptr->fileref);
               sim_fwrite("\f", 1, 1, uptr->fileref);
               uptr->LINE = 0;
            } else {
               uptr->LINE++;
            }
            l--;
        }
        return;
    }

    mask = 0x1000 >> (1 & 0xf);
    f = 0;     /* Flag if we skipped to new page */
    l = 0;     /* What line we should be on */

    for (i = uptr->LINE; (lpr_data[u].fcb[i] & mask) == 0 &&
                            uptr->LINE != i; i++) {
         l++;
         if ((lpr_data[u].fcb[i] & 0x1000) != 0 ||
               ((uint32)i) >= uptr->capac) {
             sim_fwrite("\r\n\f", 1, 3, uptr->fileref);
             uptr->pos += 3;
             f = 1;
             l = 0;
         }
    }

    /* If past end of form clear row */
    if (f) {
       uptr->LINE = 0;
    }

    if ((lpr_data[u].fcb[i] & mask) != 0) {
        while (l-- > 0) {
           sim_fwrite("\r\n", 1, 2, uptr->fileref);
           uptr->pos += 2;
           uptr->LINE++;
        }
    }
}


/*
 * Check if device ready to start commands.
 */

uint8  lpr_startio(UNIT *uptr) {

    if ((uptr->CMD & LPR_CMDMSK) != 0)
        return SNS_BSY;
    sim_debug(DEBUG_CMD, &lpr_dev, "start io unit\n");
    return 0;
}


uint8 lpr_startcmd(UNIT * uptr, uint8 cmd)
{
    if ((uptr->CMD & LPR_CMDMSK) != 0) {
       if ((uptr->flags & UNIT_ATT) != 0)
            return SNS_BSY;
       return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    sim_debug(DEBUG_CMD, &lpr_dev, "Cmd %02x %02x\n", cmd, (cmd >> 3) & 0x1f);

    switch (cmd & 0x3) {
    case 1:              /* Write command */
         uptr->CMD &= ~(LPR_CMDMSK);
         uptr->CMD |= (cmd & LPR_CMDMSK);
         sim_activate(uptr, 10);          /* Start unit off */
         uptr->SNS = 0;
         uptr->POS = 0;
         return 0;

    case 3:              /* Carrage control */
         uptr->SNS = 0;
         uptr->POS = 0;
         uptr->CMD &= ~(LPR_CMDMSK);
         /* Nop is immediate command */
         if (cmd == 0x3)
             return SNS_CHNEND|SNS_DEVEND;
         uptr->CMD |= (cmd & LPR_CMDMSK);
         sim_activate(uptr, 10);          /* Start unit off */
         /* Motion and not load UCS */
         if ((cmd & 0x77) != 0x73 && (cmd & 07) == 3)
             return SNS_CHNEND;
         return 0;

    case 0:               /* Status */
         if (cmd == 0x4) {           /* Sense */
             uptr->CMD &= ~(LPR_CMDMSK);
             uptr->CMD |= (cmd & LPR_CMDMSK);
             sim_activate(uptr, 10);       /* Start unit off */
             return 0;
         }
         break;

    default:              /* invalid command */
         uptr->SNS |= SNS_CMDREJ;
         break;
    }
    if (uptr->SNS & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle transfer of data for printer */
t_stat
lpr_srv(UNIT *uptr) {
    int             addr = GET_UADDR(uptr->CMD);
    int             u = (uptr - lpr_unit);
    int             cmd = (uptr->CMD & 0x7);
    int             l = (uptr->CMD >> 3) & 0x1f;
    uint8           ch;

    if (cmd == 4) {
         ch = uptr->SNS;
         uptr->CMD &= ~(LPR_CMDMSK);
         chan_write_byte(addr, &ch);
         chan_end(addr, SNS_DEVEND|SNS_CHNEND);
         return SCPE_OK;
    }

    if (cmd == 7) {
       uptr->CMD &= ~(LPR_FULL|LPR_CMDMSK);
       uptr->POS = 0;
       (void)chan_read_byte(addr, &ch);
       chan_end(addr, SNS_DEVEND|SNS_CHNEND);
       return SCPE_OK;
    }

    /* Handle Block-Data-check */
    if ((uptr->CMD & 0xf7) == 0x73) {
        if (uptr->CMD & 0x8)
            uptr->CMD &= ~LPR_DATCHK;
        else
            uptr->CMD |= LPR_DATCHK;
       uptr->CMD &= ~(LPR_CMDMSK);
       (void)chan_read_byte(addr, &ch);
       chan_end(addr, SNS_DEVEND|SNS_CHNEND);
       return SCPE_OK;
    }

    /* Handle UCS Load */
    if ((uptr->CMD & 0xf7) == 0xf3) {
       for (l = 0; l < 240; l++) {
           if(chan_read_byte(addr, &ch)) 
              break;
       }
       uptr->CMD &= ~(LPR_CMDMSK);
       chan_end(addr, SNS_DEVEND|SNS_CHNEND);
       return SCPE_OK;
    }

    /* Check if valid form motion */
    if ((cmd == 1 || cmd == 3) && 
        ((l > 3 && l < 0x10) || l > 0x1d)) {
        uptr->SNS = SNS_CMDREJ;
        uptr->CMD &= ~(LPR_CMDMSK);
        sim_debug(DEBUG_DETAIL, &lpr_dev, "%d Invalid skip %x %d", u, l, l);
        if (cmd == 3) 
            set_devattn(addr, SNS_DEVEND|SNS_UNITCHK);
        else
            chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
        return SCPE_OK;
    }

    /* If at end of buffer, or control do command */
    if ((uptr->CMD & LPR_FULL) || cmd == 3) {
       print_line(uptr);
       uptr->CMD &= ~(LPR_FULL|LPR_CMDMSK);
       uptr->POS = 0;
       if (uptr->SNS & SNS_CHN12) {
           set_devattn(addr, SNS_DEVEND|SNS_UNITEXP);
           uptr->SNS &= 0xff;
       } else {
           if ((uptr->SNS & 0xff) != 0)
              set_devattn(addr, SNS_DEVEND|SNS_UNITCHK);
           else
              set_devattn(addr, SNS_DEVEND);
       }
       return SCPE_OK;
    }

    /* Copy next column over */
    if (cmd == 1 && (uptr->CMD & LPR_FULL) == 0) {
       if(chan_read_byte(addr, &lpr_data[u].lbuff[uptr->POS])) {
           uptr->CMD |= LPR_FULL;
       } else {
           sim_activate(uptr, 20);
           uptr->POS++;
       }
       if (uptr->CMD & LPR_FULL || uptr->POS > 132) {
           uptr->CMD |= LPR_FULL;
           chan_end(addr, SNS_CHNEND);
           sim_activate(uptr, 5000);
       }
    }
    return SCPE_OK;
}

void
lpr_ini(UNIT *uptr, t_bool f) {
    int             u = (uptr - lpr_unit);
    int             i, j;
    CONST uint16   *fcb;

    uptr->CMD &= ~(LPR_FULL|LPR_CMDMSK);
    uptr->LINE = 0;
    uptr->SNS = 0;
    i = (uptr->flags & UNIT_M_FCB) >> UNIT_V_FCB;
    fcb = fcb_ptr[i];
    lpr_data[u].fcb = fcb;
    for (j = 0; (fcb[j] & 0x1000) == 0; j++);
    uptr->capac = j;
}

t_stat
lpr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    sim_switches |= SWMASK ('A');   /* Position to EOF */
    if ((r = attach_unit(uptr, file)) != SCPE_OK)
       return r;
    uptr->CMD &= ~(LPR_FULL|LPR_CMDMSK);
    uptr->LINE = 0;
    uptr->SNS = 0;
    set_devattn(GET_UADDR(uptr->CMD), SNS_DEVEND);
    return SCPE_OK;
}

t_stat
lpr_detach(UNIT * uptr)
{
    if (uptr->CMD & LPR_FULL)
        print_line(uptr);
    return detach_unit(uptr);
}

t_stat
lpr_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "1403 Line Printer\n\n");
   fprintf (st, "The 1403 Line printer can be configured to any number of\n");
   fprintf (st, "lines per page with the:\n");
   fprintf (st, "        sim> SET LPn LINESPERPAGE=n\n\n");
   fprintf (st, "The default is 59 lines per page. The Line Printer has the following\n");
   fprintf (st, "control tape attached.\n");
   fprintf (st, "     Channel 1:     Skip to top of page\n");
   fprintf (st, "     Channel 2:     Skip to top of page\n");
   fprintf (st, "     Channel 3:     Skip to next even line\n");
   fprintf (st, "     Channel 4:     Skip to next odd line\n");
   fprintf (st, "     Channel 5:     Skip to middle or top of page\n");
   fprintf (st, "     Channel 6:     Skip 1/4 of page\n");
   fprintf (st, "     Channel 7:     Skip one line\n");
   fprintf (st, "     Channel 8:     Skip one line\n");
   fprintf (st, "     Channel 9:     Skip one line\n");
   fprintf (st, "     Channel 10:    Skip one line\n");
   fprintf (st, "     Channel 11:    Skip one line\n");
   fprintf (st, "     Channel 12:    Skip to top of page\n");
   fprint_set_help(st, dptr);
   fprint_show_help(st, dptr);
   return SCPE_OK;
}

const char *
lpr_description(DEVICE *dptr)
{
   return "1403 Line Printer";
}

#endif
