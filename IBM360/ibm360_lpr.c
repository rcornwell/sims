/* ibm360_urec.c: IBM 360 Line Printer

   Copyright (c) 2016, Richard Cornwell

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
   This is the standard inquiry or console interface.

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
/* u6 hold buffer position */


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

UNIT                lpr_unit[] = {
    {UDATA(lpr_srv, UNIT_LPR, 55), 300, UNIT_ADDR(0x0E)},       /* A */
#if NUM_DEVS_LPR > 1
    {UDATA(lpr_srv, UNIT_LPR, 55), 300, UNIT_ADDR(0x1E)},       /* B */
#endif
};

MTAB                lpr_mod[] = {
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LINESPERPAGE", "LINESPERPAGE",
       &lpr_setlpp, &lpr_getlpp, NULL, "Number of lines per page"},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {0}
};

struct dib lpr_dib = { 0xFF, 1, NULL, lpr_startcmd, NULL, lpr_unit};

DEVICE              lpr_dev = {
    "LPR", lpr_unit, NULL, lpr_mod,
    NUM_DEVS_LPR, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &lpr_attach, &lpr_detach,
    &lpr_dib, DEV_UADDR | DEV_DISABLE | DEV_DEBUG, 0, dev_debug
};


/* Line printer routines
*/

t_stat
lpr_setlpp(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int i;
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
    uptr->u4 = 0;
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

    /* Try to convert to text */
    memset(out, ' ', sizeof(out));

    /* Scan each column */
    for (i = 0; i < uptr->u6; i++) {
       int         ch = lpr_data[u].lbuff[i];

       ch = ebcdic_to_ascii[ch];
       if (!isprint(ch))
          ch = '.';
       out[i] = ch;
    }

    /* Trim trailing spaces */
    for (--i; i > 0 && out[i] == ' '; i--) ;
    out[++i] = '\n';
    out[++i] = '\r';
    out[++i] = '\0';

    /* Print out buffer */
    sim_fwrite(&out, 1, i, uptr->fileref);
fprintf(stderr, "%s", out);
    uptr->u4++;
    if (uptr->u4 > uptr->capac) {
       uptr->u4 = 1;
    }

    memset(&lpr_data[u].lbuff[0], 0, 144);
}


uint8 lpr_startcmd(UNIT * uptr, uint16 chan, uint8 cmd)
{
    uint8   ch;

    if ((uptr->u3 & LPR_CMDMSK) != 0) {
        if ((uptr->flags & UNIT_ATT) != 0)
            return SNS_BSY;
       return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    switch (cmd & 0x7) {
    case 1:              /* Write command */
    case 3:
         uptr->u3 &= ~(LPR_CMDMSK);
         uptr->u3 |= (cmd & LPR_CMDMSK);
         sim_activate(uptr, 10);          /* Start unit off */
         uptr->u5 = 0;
         uptr->u6 = 0;
         return 0;

    case 0:               /* Status */
         break;

    case 4:              /* Sense */
         uptr->u3 &= ~(LPR_CMDMSK);
         uptr->u3 |= (cmd & LPR_CMDMSK);
         sim_activate(uptr, 10);       /* Start unit off */
         return 0;

    default:              /* invalid command */
         uptr->u5 |= SNS_CMDREJ;
         break;
    }
    if (uptr->u5 & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle transfer of data for printer */
t_stat
lpr_srv(UNIT *uptr) {
    int             addr = GET_UADDR(uptr->u3);
    int             u = (uptr - lpr_unit);

    if ((uptr->u3 & 0xFF) == 4) {
         uint8 ch = uptr->u5;
         chan_write_byte(GET_UADDR(uptr->u3), &ch);
         chan_end(addr, SNS_DEVEND|SNS_CHNEND);
         return SCPE_OK;
    }

    if ((uptr->u3 & LPR_FULL) || (uptr->u3 & 0x7) == 0x3) {
       print_line(uptr);
       uptr->u3 &= ~(LPR_FULL|LPR_CMDMSK);
       uptr->u6 = 0;
           chan_end(addr, SNS_CHNEND|SNS_DEVEND);
//       set_devattn(addr, SNS_DEVEND);
    }

    /* Copy next column over */
    if ((uptr->u3 & 0x7) == 1 && uptr->u6 < 144) {
       if(chan_read_byte(addr, &lpr_data[u].lbuff[uptr->u6++])) {
           uptr->u3 |= LPR_FULL;
       } else {
           sim_activate(uptr, 10);
       }
       if (uptr->u3 & LPR_FULL || uptr->u6 >= 144) {
           uptr->u3 |= LPR_FULL;
//           chan_end(addr, SNS_CHNEND);
           sim_activate(uptr, 1000);
       }
    }
    return SCPE_OK;
}

void
lpr_ini(UNIT *uptr, t_bool f) {
}

t_stat
lpr_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
       return r;
    uptr->u3 &= ~(LPR_FULL|LPR_CMDMSK);
    uptr->u4 = 0;
    uptr->u5 = 0;
    return SCPE_OK;
}

t_stat
lpr_detach(UNIT * uptr)
{
    if (uptr->u3 & LPR_FULL)
        print_line(uptr);
    return detach_unit(uptr);
}

#endif
