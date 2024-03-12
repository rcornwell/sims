/* ibm360_scon.c: IBM 360 Secondary consoles.

   Copyright (c) 2017-2023, Richard Cornwell

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

   This is the standard inquiry or console interface.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.

*/

#include "ibm360_defs.h"
#include "sim_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#ifdef NUM_DEVS_SCON

/* Held in u3 */
#define CHN_SNS         0x04    /* Sense command */
#define CON_WR          0x01    /* Write console */
#define CON_ACR         0x09    /* Auto carrage return */
#define CON_RD          0x0a    /* Read console */
#define CON_ALR         0x0b    /* Control Alarm */
#define CON_NOP         0x03    /* No op command */
#define CON_MSK         0x0f    /* Command mask */

/* Status held in u3 */
#define CON_INPUT       0x100   /* Input ready for unit */
#define CON_CR          0x200   /* Output at beginning of line */
#define CON_REQ         0x400   /* Request key pressed */
#define CON_OUTPUT      0x800   /* Output characters since R */
#define CON_CANCEL      0x1000  /* Control C pressed */

/* Upper 11 bits of u3 hold the device address */

/* Input buffer pointer held in u4 */

/* in u5 packs sense byte 0,1 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ      0x80    /* Command reject */
#define SNS_INTVENT     0x40    /* Unit intervention required */

#define CMD     u3
#define IPTR    u4
#define SNS     u5

/* std devices. data structures

   scon_dev       Console device descriptor
   scon_unit      Console unit descriptor
   scon_reg       Console register list
   scon_mod       Console modifiers list
*/


struct _scon_data
{
    uint8              ibuff[145];       /* Input line buffer */
    uint8              inptr;
}
scon_data[NUM_DEVS_SCON];

TMLN        scon_ldsc[NUM_DEVS_SCON];
TMXR        scon_desc = { NUM_DEVS_SCON, 0, 0, scon_ldsc};
extern int32       tmxr_poll;

uint8  scon_startcmd(UNIT *, uint8);
uint8  scon_haltio(UNIT *);
void                scon_ini(UNIT *, t_bool);
t_stat              scon_srv(UNIT *);
t_stat              scon_srvo(UNIT *);
t_stat              scon_reset(DEVICE *);
t_stat              scon_attach(UNIT *, CONST char *);
t_stat              scon_detach(UNIT *);
t_stat              scon_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *scon_description(DEVICE *d);

UNIT                scon_unit[] = {
    {UDATA(scon_srv,  UNIT_ATTABLE|UNIT_DISABLE|UNIT_DIS|UNIT_IDLE, 0), 0, UNIT_ADDR(0x09)},       /* B */
    {UDATA(scon_srv,  UNIT_ATTABLE|UNIT_DISABLE|UNIT_DIS|UNIT_IDLE, 0), 0, UNIT_ADDR(0x2F)},       /* C */
    {UDATA(scon_srvo, UNIT_DISABLE|UNIT_DIS|UNIT_IDLE, 0) },
};

MTAB                scon_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {0}
};

struct dib scon_dib = { 0xFF, 1, NULL, scon_startcmd, scon_haltio, scon_unit, scon_ini};

DEVICE              scon_dev = {
    "SCON", scon_unit, NULL, scon_mod,
    NUM_DEVS_CON, 8, 15, 1, 8, 8,
    NULL, NULL, &scon_reset, NULL, &scon_attach, &scon_detach,
    &scon_dib, DEV_MUX | DEV_UADDR | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &scon_help, NULL, NULL, &scon_description
};



/*
 * Console printer routines.
 */
void
scon_ini(UNIT *uptr, t_bool f) {
     int                 u = (uptr - scon_unit);
     scon_data[u].inptr = 0;
     uptr->CMD &= ~(CON_MSK|CON_REQ|CON_INPUT|CON_CR|CON_CANCEL);
     uptr->SNS = 0;
     sim_activate(uptr, 1000);
}

uint8  scon_startcmd(UNIT *uptr, uint8 cmd) {
    int                 u = (uptr - scon_unit);

    sim_debug(DEBUG_CMD, &scon_dev, "%d: Cmd %x %x\n", u, cmd, uptr->CMD);
    if ((uptr->CMD & CON_MSK) != 0)
        return SNS_BSY;

    if ((cmd & 0xf0) != 0) {
        uptr->SNS |= SNS_CMDREJ;
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    switch (cmd & 0x7) {
    case 2:                        /* Read command */
         sim_debug(DEBUG_CMD, &scon_dev, "%d: Cmd RD\n", u);
         if (uptr->CMD & CON_REQ) {
             uptr->CMD &= ~CON_REQ;
             return SNS_ATTN;
         }

         if ((uptr->CMD & CON_INPUT) == 0 &&
                (scon_data[u].inptr == 0 || uptr->CMD & CON_CR)) {
             /* Activate input so we can get response */
             if ((uptr->CMD & CON_OUTPUT) != 0) {
                 tmxr_putc_ln(&scon_ldsc[u], '\r');
                 tmxr_putc_ln(&scon_ldsc[u], '\n');
                 uptr->CMD &= ~CON_OUTPUT;
             }
             tmxr_putc_ln(&scon_ldsc[u], 'I');
             tmxr_putc_ln(&scon_ldsc[u], ' ');
         }
         uptr->IPTR = 0;
         uptr->CMD |= cmd & CON_MSK;
         uptr->SNS = 0;
         return 0;

    case 1:                    /* Write command */
         sim_debug(DEBUG_CMD, &scon_dev, "%d: Cmd WR\n", u);
         if (uptr->CMD & CON_REQ) {
             uptr->CMD &= ~CON_REQ;
             return SNS_ATTN;
         }
         uptr->CMD |= cmd & CON_MSK;
         uptr->SNS = 0;
         if (uptr->CMD & CON_CR) {
             tmxr_putc_ln(&scon_ldsc[u], 'R');
             tmxr_putc_ln(&scon_ldsc[u], ' ');
             uptr->CMD &= ~CON_CR;
             uptr->CMD |= CON_OUTPUT;
         }
         return 0;

    case 3:              /* Control */
         if (cmd == CON_ALR) {
             tmxr_putc_ln(&scon_ldsc[u], '\a');
         }
         sim_debug(DEBUG_CMD, &scon_dev, "%d: Cmd NOP\n", u);
         if (uptr->CMD & CON_REQ) {
             uptr->CMD &= ~CON_REQ;
             return SNS_ATTN;
         }
         uptr->SNS = 0;
         return SNS_CHNEND|SNS_DEVEND;

    case 0:               /* Status */
         return 0;

    case 4:              /* Sense */
         uptr->CMD |= cmd & CON_MSK;
         return 0;

    default:              /* invalid command */
         uptr->SNS |= SNS_CMDREJ;
         break;
    }

    if (uptr->SNS)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/*
 * Handle halt I/O instruction by stoping running command.
 */
uint8  scon_haltio(UNIT *uptr) {
    uint16         addr = GET_UADDR(uptr->CMD);
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            u = (uptr - scon_unit);
    int            cmd = uptr->CMD & 0xff;

    sim_debug(DEBUG_CMD, dptr, "HLTIO inq %x\n", cmd);

    switch (cmd) {
    case 0:
    case 0x4:
         /* Short commands nothing to do */
         break;

    case CON_WR:
    case CON_ACR:
    case CON_RD:
         uptr->CMD &= ~(CON_MSK|CON_INPUT|CON_CANCEL);
         scon_data[u].inptr = 0;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;
    }
    return 1;
}


/* Handle transfer of data for printer */
t_stat
scon_srv(UNIT *uptr) {
    uint16              addr = GET_UADDR(uptr->CMD);
    int                 u = (uptr - scon_unit);
    int                 cmd = uptr->CMD & CON_MSK;
    int32               ch;
    uint8               data;
    int                 i;
    int                 delay = 1000;

    switch (cmd) {
    case 4:              /* Sense */
         sim_debug(DEBUG_CMD, &scon_dev, "%d: Cmd SNS %02x\n", u, uptr->SNS);
         /* Check if request pending */
         data = uptr->SNS;
         chan_write_byte(addr, &data);
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         uptr->CMD &= ~(CON_MSK);
         uptr->SNS = 0;
         break;

    case CON_WR:
    case CON_ACR:
         sim_debug(DEBUG_CMD, &scon_dev, "%d: Cmd Write\n", u);
         if (chan_read_byte(addr, &data)) {
             if (cmd == CON_ACR) {
                 tmxr_putc_ln(&scon_ldsc[u], '\r');
                 tmxr_putc_ln(&scon_ldsc[u], '\n');
                 uptr->CMD |= CON_CR;
                 uptr->CMD &= ~CON_OUTPUT;
             }
             uptr->CMD &= ~CON_MSK;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             delay = 40000;
         } else {
             if (scon_ldsc[u].conn == 0) {
                 sim_debug(DEBUG_CMD, &scon_dev, "%d: Cmd not connected\n", u);
                 break;
             }
             if (data == 0x15) {
                 tmxr_putc_ln(&scon_ldsc[u], '\r');
                 tmxr_putc_ln(&scon_ldsc[u], '\n');
                 uptr->CMD |= CON_CR;
                 uptr->CMD &= ~CON_OUTPUT;
             } else {
                 ch = ebcdic_to_ascii[data];
                 if (ch != 0) {
                     if (!isprint(ch))
                         ch = '_';
                     sim_debug(DEBUG_CMD, &scon_dev, "%d: send %02x\n", u, ch);
                     tmxr_putc_ln(&scon_ldsc[u], ch);
                     uptr->CMD &= ~CON_OUTPUT;
                 }
             }
         }
         break;

    case CON_RD:
         if (uptr->CMD & CON_INPUT) {
             uptr->CMD &= ~CON_REQ;
             /* Check for empty line, or end of data */
             if (scon_data[u].inptr == 0 || uptr->IPTR == scon_data[u].inptr) {
                     uptr->CMD &= ~CON_INPUT;
                     scon_data[u].inptr = 0;
                     cmd = 0;
                     uptr->CMD &= ~(CON_MSK);
                     sim_debug(DEBUG_CMD, &scon_dev, "%d: devend\n", u);
                     if (uptr->CMD & CON_CANCEL) {
                         uptr->CMD &= ~CON_CANCEL;
                         chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                     } else {
                         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                     }
                     break;
             }

             /* Grab next character and send it to CPU */
             data = scon_data[u].ibuff[uptr->IPTR++];
             sim_debug(DEBUG_CMD, &scon_dev, "%d: rd %02x\n", u, data);
             if (chan_write_byte(addr, &data)) {
                 uptr->CMD &= ~CON_INPUT;
                 scon_data[u].inptr = 0;
                 cmd = 0;
                 uptr->CMD &= ~(CON_MSK);
                 sim_debug(DEBUG_CMD, &scon_dev, "%d: devend input\n", u);
                 if (uptr->CMD & CON_CANCEL) {
                     uptr->CMD &= ~CON_CANCEL;
                     chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                 } else {
                     chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                 }
            }
         }
         break;
    }

    ch = tmxr_getc_ln(&scon_ldsc[u]);         /* get char */
    if ((ch & TMXR_VALID) != 0) {
       data = (uint8)(ch & 0xff);
       if ((uptr->CMD & CON_INPUT) == 0) {
          /* Handle end of buffer */
          switch (data) {
          case '\r':
          case '\n':
                sim_debug(DEBUG_DATA, &scon_dev, "%d: ent\n", u);
                uptr->CMD |= CON_INPUT;
                uptr->CMD |= CON_CR;
                uptr->CMD &= ~CON_OUTPUT;
                tmxr_putc_ln(&scon_ldsc[u], '\r');
                tmxr_putc_ln(&scon_ldsc[u], '\n');
               /* Fall through */

          case 033: /* request key */
                if (cmd != CON_RD) {
                    uptr->CMD |= CON_REQ;
                }
                break;

          case 0177:
          case '\b':
                if (scon_data[u].inptr != 0) {
                    scon_data[u].inptr--;
                    tmxr_putc_ln(&scon_ldsc[u], '\b');
                    tmxr_putc_ln(&scon_ldsc[u], ' ');
                    tmxr_putc_ln(&scon_ldsc[u], '\b');
                }
                break;

           case 03:  /* ^C */
                uptr->CMD |= CON_CANCEL|CON_INPUT;
                break;

           case 025: /* ^U clear line */
                for (i = scon_data[u].inptr; i> 0; i--) {
                    tmxr_putc_ln(&scon_ldsc[u], '\b');
                    tmxr_putc_ln(&scon_ldsc[u], ' ');
                    tmxr_putc_ln(&scon_ldsc[u], '\b');
                }
                scon_data[u].inptr = 0;
                break;

          default:
                sim_debug(DEBUG_DATA, &scon_dev, "%d: key '%c'\n", u, ch);
                if (scon_data[u].inptr < sizeof(scon_data[u].ibuff)) {
                    data = ascii_to_ebcdic[data];
                    if (data == 0xff) {
                       tmxr_putc_ln(&scon_ldsc[u], '\007');
                       break;
                    }
                    tmxr_putc_ln(&scon_ldsc[u], ebcdic_to_ascii[data]);
                    scon_data[u].ibuff[scon_data[u].inptr++] = data;
                }
                break;
          }
        } else {
           if (cmd == CON_RD && data == 03) { /* Cancel */
               chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
               uptr->CMD &= ~(CON_INPUT|CON_CANCEL);
               scon_data[u].inptr = 0;
               cmd = 0;
            } else {
               sim_debug(DEBUG_CMD, &scon_dev, "%d: error %x\n", u, cmd);
               if (cmd == 0)
                    uptr->CMD |= CON_REQ;
               tmxr_putc_ln(&scon_ldsc[u], '\007');
            }
        }
    }

    if (cmd == 0 && uptr->CMD & CON_REQ) {
          sim_debug(DEBUG_CMD, &scon_dev, "%d: setattn %x\n", u, addr);
          set_devattn(addr, SNS_ATTN);
          uptr->CMD &= ~CON_REQ;
    }
    sim_activate(uptr, delay);
    return SCPE_OK;
}

/* Handle connect disconnect and data transfer */
t_stat
scon_srvo(UNIT *uptr) {
    int       ln;

    sim_clock_coschedule(uptr, tmxr_poll);
    ln = tmxr_poll_conn(&scon_desc);     /* look for connect */
    if (ln >= 0) {              /* got one? */
        scon_ldsc[ln].rcve = 1;
        sim_debug(DEBUG_DETAIL, &scon_dev, "Inquiry connect %d\n", ln);
    }

    tmxr_poll_rx(&scon_desc);
    tmxr_poll_tx(&scon_desc);
    return SCPE_OK;
}

t_stat
scon_reset(DEVICE *dptr)
{
    sim_activate(&scon_unit[NUM_DEVS_SCON], tmxr_poll);
    return SCPE_OK;
}

t_stat
scon_attach(UNIT * uptr, CONST char *cptr)
{
    int           u = (uptr - scon_unit);
    t_stat        r;

    r = tmxr_attach(&scon_desc, uptr, cptr);
    uptr->CMD &= ~(CON_MSK);
    sim_activate(uptr, tmxr_poll);
    return r;
}

t_stat
scon_detach(UNIT * uptr)
{
    int           u = (uptr - scon_unit);
    t_stat        r;

    sim_cancel(uptr);
    r = tmxr_detach(&scon_desc, uptr);
    return r;
}

t_stat
scon_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "1050 Console Terminal\n");
   fprintf (st, "This is the interface from the operator to the system. To request the\n");
   fprintf (st, "system to accept input press the <esc> key and wait until the system\n");
   fprintf (st, "responds with a line with I as the first character. When you have\n");
   fprintf (st, "finished typing your line, press return or enter key. Backspace will\n");
   fprintf (st, "delete the last character. All responses from the system are prefixed\n");
   fprintf (st, "with a R and blank as the first character. Not all operating systems\n");
   fprintf (st, "require the use of <esc> to enter data\n");
   return SCPE_OK;
}

const char *
scon_description(DEVICE *dptr)
{
   return "1050 Console Terminal";
}


#endif


