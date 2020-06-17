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
#define UNIT_LPR       UNIT_ATTABLE | UNIT_DISABLE


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
}
lpr_data[NUM_DEVS_LPR];

uint8               lpr_startcmd(UNIT *, uint16, uint8);
void                lpr_ini(UNIT *, t_bool);
t_stat              lpr_srv(UNIT *);
t_stat              lpr_reset(DEVICE *);
t_stat              lpr_attach(UNIT *, CONST char *);
t_stat              lpr_detach(UNIT *);
t_stat              lpr_setlpp(UNIT *, int32, CONST char *, void *);
t_stat              lpr_getlpp(FILE *, UNIT *, int32, CONST void *);
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
    {0}
};

struct dib lpr_dib = { 0xFF, 1, NULL, lpr_startcmd, NULL, lpr_unit, lpr_ini};

DEVICE              lpr_dev = {
    "LPR", lpr_unit, NULL, lpr_mod,
    NUM_DEVS_LPR, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &lpr_attach, &lpr_detach,
    &lpr_dib, DEV_UADDR | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &lpr_help, NULL, NULL, &lpr_description
};


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

void
print_line(UNIT * uptr)
{

    char                out[150];       /* Temp conversion buffer */
    int                 i;
    int                 u = (uptr - lpr_unit);
    int                 l = (uptr->CMD >> 3) & 0x1f;
    int                 f = 1;

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
        sim_debug(DEBUG_DETAIL, &lpr_dev, "%s", out);
    }

    if (l < 4) {
        while(l != 0) {
            sim_fwrite("\r\n", 1, 2, uptr->fileref);
            f = 0;
            uptr->pos += 2;
            uptr->LINE++;
            if (((uint32)uptr->LINE) > uptr->capac)
                break;
            l--;
        }
        if ((t_addr)uptr->LINE > uptr->capac) {
           if (f)
               sim_fwrite("\r\n", 1, 2, uptr->fileref);
           sim_fwrite("\f", 1, 1, uptr->fileref);
           uptr->LINE = 1;
        }
        return;
    }

    switch (l & 0xf) {
    case 0:     /* Not available */
        break;
    case 1:
    case 2:     /* Skip to top of form */
    case 12:
        uptr->LINE = uptr->capac+1;
        break;

    case 3:     /* Even lines */
        if ((uptr->LINE & 1) == 1) {
            sim_fwrite("\r\n", 1, 2, uptr->fileref);
            f = 0;
            uptr->pos += 2;
            uptr->LINE++;
        }
        break;
    case 4:     /* Odd lines */
        if ((uptr->LINE & 1) == 0) {
            sim_fwrite("\r\n", 1, 2, uptr->fileref);
            f = 0;
            uptr->pos += 2;
            uptr->LINE++;
        }
        break;
    case 5:     /* Half page */
        while((uptr->LINE != (uptr->capac/2)) ||
              (uptr->LINE != (uptr->capac))) {
            sim_fwrite("\r\n", 1, 2, uptr->fileref);
            f = 0;
            uptr->pos += 2;
            uptr->LINE++;
            if (((uint32)uptr->LINE) > uptr->capac)
                break;
        }
        break;
    case 6:     /* 1/4 Page */
        while((uptr->LINE != (uptr->capac/4)) ||
              (uptr->LINE != (uptr->capac/2)) ||
              (uptr->LINE != (uptr->capac/2+uptr->capac/4)) ||
              (uptr->LINE != (uptr->capac))) {
            sim_fwrite("\r\n", 1, 2, uptr->fileref);
            f = 0;
            uptr->pos += 2;
            uptr->LINE++;
            if (((uint32)uptr->LINE) > uptr->capac)
                break;
        }
        break;
    case 7:     /* User defined, now 1 line */
    case 8:
    case 9:
    case 10:
    case 11:
        sim_fwrite("\r\n", 1, 2, uptr->fileref);
        f = 0;
        uptr->pos += 2;
        uptr->LINE++;
        break;
    }

    if ((t_addr)uptr->LINE > uptr->capac) {
       if (f)
           sim_fwrite("\r\n", 1, 2, uptr->fileref);
       sim_fwrite("\f", 1, 1, uptr->fileref);
       uptr->LINE = 1;
    }

    memset(&lpr_data[u].lbuff[0], 0, 144);
}


uint8 lpr_startcmd(UNIT * uptr, uint16 chan, uint8 cmd)
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
         uptr->CMD &= ~(LPR_CMDMSK);
         uptr->CMD |= (cmd & LPR_CMDMSK);
         sim_activate(uptr, 10);          /* Start unit off */
         uptr->SNS = 0;
         uptr->POS = 0;
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

    if (cmd == 4) {
         uint8 ch = uptr->SNS;
         uptr->CMD &= ~(LPR_CMDMSK);
         chan_write_byte(addr, &ch);
         chan_end(addr, SNS_DEVEND|SNS_CHNEND);
         return SCPE_OK;
    }

    if (cmd == 7) {
       uptr->CMD &= ~(LPR_FULL|LPR_CMDMSK);
       uptr->POS = 0;
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
       chan_end(addr, SNS_DEVEND|SNS_CHNEND);
       return SCPE_OK;
    }

    /* Handle UCS Load */
    if ((uptr->CMD & 0xf7) == 0xf3) {
       uint8   ch;
       for (l = 0; l < 240; l++) {
           if(chan_read_byte(addr, &ch)) 
              break;
       }
       chan_end(addr, SNS_DEVEND|SNS_CHNEND);
       uptr->CMD &= ~(LPR_CMDMSK);
       return SCPE_OK;
    }

    /* Check if valid form motion */
    if ((cmd == 1 || cmd == 3) && 
        (l > 3 && l < 0x10) || l > 0x1d) {
        uptr->SNS = SNS_CMDREJ;
        uptr->CMD &= ~(LPR_CMDMSK);
        chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
        return SCPE_OK;
    }

    /* On control end channel */
    if (cmd == 3)
       chan_end(addr, SNS_CHNEND);

    /* If at end of buffer, or control do command */
    if ((uptr->CMD & LPR_FULL) || cmd == 3) {
       print_line(uptr);
       uptr->CMD &= ~(LPR_FULL|LPR_CMDMSK);
       uptr->POS = 0;
       if (uptr->SNS & SNS_CHN12) {
           set_devattn(addr, SNS_DEVEND|SNS_UNITEXP);
           uptr->SNS &= 0xff;
       } else {
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
    uptr->CMD &= ~(LPR_FULL|LPR_CMDMSK);
    uptr->LINE = 0;
    uptr->SNS = 0;
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
