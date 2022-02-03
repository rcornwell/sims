/* ibm360_con.c: IBM 360 Inquiry console.

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

#ifdef NUM_DEVS_CON

/* Held in u3 */
#define CHN_SNS         0x04    /* Sense command */
#define CON_WR          0x01    /* Write console */
#define CON_ACR         0x09    /* Auto carrage return */
#define CON_RD          0x0a    /* Read console */
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

   con_dev       Console device descriptor
   con_unit       Console unit descriptor
   con_reg       Console register list
   con_mod       Console modifiers list
*/


struct _con_data
{
    uint8               ibuff[145];       /* Input line buffer */
    uint8              inptr;
}
con_data[NUM_DEVS_CON];

uint8  con_startcmd(UNIT *, uint8);
uint8  con_haltio(UNIT *);
void                con_ini(UNIT *, t_bool);
t_stat              con_srv(UNIT *);
t_stat              con_attach(UNIT *, char *);
t_stat              con_detach(UNIT *);
t_stat              con_help(FILE *, DEVICE *, UNIT *, int32, const char *);
const char         *con_description(DEVICE *d);

UNIT                con_unit[] = {
    {UDATA(con_srv, UNIT_ATT, 0), 0, UNIT_ADDR(0x1F)},       /* A */
};

MTAB                con_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {0}
};

struct dib con_dib = { 0xFF, 1, NULL, con_startcmd, con_haltio, con_unit, con_ini};

DEVICE              con_dev = {
    "INQ", con_unit, NULL, con_mod,
    NUM_DEVS_CON, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, NULL, &con_detach,
    &con_dib, DEV_UADDR | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &con_help, NULL, NULL, &con_description
};



/*
 * Console printer routines.
 */
void
con_ini(UNIT *uptr, t_bool f) {
     int                 u = (uptr - con_unit);
     con_data[u].inptr = 0;
     uptr->CMD &= ~(CON_MSK|CON_REQ|CON_INPUT|CON_CR|CON_CANCEL);
     uptr->SNS = 0;
     sim_activate(uptr, 1000);
}

uint8  con_startcmd(UNIT *uptr, uint8 cmd) {
    int                 u = (uptr - con_unit);

    sim_debug(DEBUG_CMD, &con_dev, "%d: Cmd %x %x\n", u, cmd, uptr->CMD);
    if ((uptr->CMD & CON_MSK) != 0)
        return SNS_BSY;

    if ((cmd & 0xf0) != 0) {
        uptr->SNS |= SNS_CMDREJ;
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    switch (cmd & 0x7) {
    case 2:                        /* Read command */
         sim_debug(DEBUG_CMD, &con_dev, "%d: Cmd RD\n", u);
         if (uptr->CMD & CON_REQ) {
              uptr->CMD &= ~CON_REQ;
              return SNS_ATTN;
         }

         if ((uptr->CMD & CON_INPUT) == 0 &&
                (con_data[u].inptr == 0 || uptr->CMD & CON_CR)) {
             /* Activate input so we can get response */
             if ((uptr->CMD & CON_OUTPUT) != 0) {
                sim_putchar('\r');
                sim_putchar('\n');
                uptr->CMD &= ~CON_OUTPUT;
             }
             sim_putchar('I');
             sim_putchar(' ');
         }
         uptr->IPTR = 0;
         uptr->CMD |= cmd & CON_MSK;
         uptr->SNS = 0;
         return 0;

    case 1:                    /* Write command */
         sim_debug(DEBUG_CMD, &con_dev, "%d: Cmd WR\n", u);
         if (uptr->CMD & CON_REQ) {
              uptr->CMD &= ~CON_REQ;
              return SNS_ATTN;
         }
         uptr->CMD |= cmd & CON_MSK;
         uptr->SNS = 0;
         if (uptr->CMD & CON_CR) {
            sim_putchar('R');
            sim_putchar(' ');
            uptr->CMD &= ~CON_CR;
            uptr->CMD |= CON_OUTPUT;
         }
         return 0;

    case 3:              /* Control */
         sim_debug(DEBUG_CMD, &con_dev, "%d: Cmd NOP\n", u);
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
uint8  con_haltio(UNIT *uptr) {
    uint16         addr = GET_UADDR(uptr->CMD);
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            u = (uptr - con_unit);
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
         con_data[u].inptr = 0;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;
    }
    return 1;
}


/* Handle transfer of data for printer */
t_stat
con_srv(UNIT *uptr) {
    uint16              addr = GET_UADDR(uptr->CMD);
    int                 u = (uptr - con_unit);
    int                 cmd = uptr->CMD & CON_MSK;
    t_stat              r = SCPE_ARG;       /* Force error if not set */
    uint8               ch;
    int                 i;
    int                 delay = 1000;


    switch (cmd) {
    case 4:              /* Sense */
         sim_debug(DEBUG_CMD, &con_dev, "%d: Cmd SNS %02x\n", u, uptr->SNS);
         /* Check if request pending */
         ch = uptr->SNS;
         chan_write_byte(addr, &ch);
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         uptr->CMD &= ~(CON_MSK);
         uptr->SNS = 0;
         break;

    case CON_WR:
    case CON_ACR:
       if (chan_read_byte(addr, &ch)) {
           if (cmd == CON_ACR) {
               sim_putchar('\r');
               sim_putchar('\n');
               uptr->CMD |= CON_CR;
               uptr->CMD &= ~CON_OUTPUT;
           }
           uptr->CMD &= ~CON_MSK;
           chan_end(addr, SNS_CHNEND|SNS_DEVEND);
           delay = 40000;
       } else {
           if (ch == 0x15) {
               sim_putchar('\r');
               sim_putchar('\n');
               uptr->CMD |= CON_CR;
               uptr->CMD &= ~CON_OUTPUT;
           } else {
               ch = ebcdic_to_ascii[ch];
               if (ch != 0) {
                   if (!isprint(ch))
                       ch = '_';
                   sim_putchar(ch);
                   uptr->CMD &= ~CON_OUTPUT;
               }
           }
       }
       break;

    case CON_RD:
       if (uptr->CMD & CON_INPUT) {
           uptr->CMD &= ~CON_REQ;
           /* Check for empty line, or end of data */
           if (con_data[u].inptr == 0 || uptr->IPTR == con_data[u].inptr) {
                   uptr->CMD &= ~CON_INPUT;
                   con_data[u].inptr = 0;
                   cmd = 0;
                   uptr->CMD &= ~(CON_MSK);
                   sim_debug(DEBUG_CMD, &con_dev, "%d: devend\n", u);
                   if (uptr->CMD & CON_CANCEL) {
                       uptr->CMD &= ~CON_CANCEL;
                       chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                   } else {
                       chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                   }
                   break;
           }

           /* Grab next character and send it to CPU */
           ch = con_data[u].ibuff[uptr->IPTR++];
           sim_debug(DEBUG_CMD, &con_dev, "%d: rd %02x\n", u, ch);
           if (chan_write_byte(addr, &ch)) {
               uptr->CMD &= ~CON_INPUT;
               con_data[u].inptr = 0;
               cmd = 0;
               uptr->CMD &= ~(CON_MSK);
               sim_debug(DEBUG_CMD, &con_dev, "%d: devend input\n", u);
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

    r = sim_poll_kbd();
    if (r & SCPE_KFLAG) {
       ch = r & 0377;
       if ((uptr->CMD & CON_INPUT) == 0) {
          /* Handle end of buffer */
          switch (ch) {
          case '\r':
          case '\n':
                sim_debug(DEBUG_DATA, &con_dev, "%d: ent\n", u);
                uptr->CMD |= CON_INPUT;
                uptr->CMD |= CON_CR;
                uptr->CMD &= ~CON_OUTPUT;
                sim_putchar('\r');
                sim_putchar('\n');
               /* Fall through */

          case 033: /* request key */
                if (cmd != CON_RD) {
                    uptr->CMD |= CON_REQ;
                }
                break;
          case 0177:
          case '\b':
                if (con_data[u].inptr != 0) {
                     con_data[u].inptr--;
                     sim_putchar('\b');
                     sim_putchar(' ');
                     sim_putchar('\b');
                }
                break;
           case 030: /* ^X Post external interrupt */
                sim_debug(DEBUG_CMD, &con_dev, "%d: ext\n", u);
                post_extirq();
                break;
           case 03:  /* ^C */
                uptr->CMD |= CON_CANCEL|CON_INPUT;
                break;

           case 025: /* ^U clear line */
                for (i = con_data[u].inptr; i> 0; i--) {
                    sim_putchar('\b');
                    sim_putchar(' ');
                    sim_putchar('\b');
                }
                con_data[u].inptr = 0;
                break;

          default:
                sim_debug(DEBUG_DATA, &con_dev, "%d: key '%c'\n", u, ch);
                if (con_data[u].inptr < sizeof(con_data[u].ibuff)) {
                    ch = ascii_to_ebcdic[ch];
                    if (ch == 0xff) {
                       sim_putchar('\007');
                       break;
                    }
                    sim_putchar(ebcdic_to_ascii[ch]);
                    con_data[u].ibuff[con_data[u].inptr++] = ch;
                }
                break;
          }
        } else {
           if (cmd == CON_RD && ch == 03) { /* Cancel */
               chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
               uptr->CMD &= ~(CON_INPUT|CON_CANCEL);
               con_data[u].inptr = 0;
               cmd = 0;
            } else {
               if (ch == 030) { /* ^X Post external interrupt */
                   sim_debug(DEBUG_CMD, &con_dev, "Console %d: ^X Key (external interrupt)\n", u);
                   post_extirq();
                   sim_activate(uptr, delay);
                   return SCPE_OK;
               }
               sim_debug(DEBUG_CMD, &con_dev, "%d: error %x\n", u, cmd);
               if (cmd == 0)
                    uptr->CMD |= CON_REQ;
               sim_putchar('\007');
            }
        }
    }

    if (cmd == 0 && uptr->CMD & CON_REQ) {
          sim_debug(DEBUG_CMD, &con_dev, "%d: setattn %x\n", u, addr);
          set_devattn(addr, SNS_ATTN);
          uptr->CMD &= ~CON_REQ;
    }
    sim_activate(uptr, delay);
    return SCPE_OK;
}

t_stat
con_detach(UNIT *uptr)
{
   return SCPE_OK;
}

t_stat
con_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
   fprintf (st, "1050 Console Terminal\n");
   fprintf (st, "This is the interface from the operator to the system. To request the\n");
   fprintf (st, "system to accept input press the <esc> key and wait until the system\n");
   fprintf (st, "responds with a line with I as the first character. When you have\n");
   fprintf (st, "finished typing your line, press return or enter key. Backspace will\n");
   fprintf (st, "delete the last character. All responses from the system are prefixed\n");
   fprintf (st, "with a R and blank as the first character. Not all operating systems\n");
   fprintf (st, "require the use of <esc> to enter data\n");
   fprintf (st, "Pressing control-X will issue a external interrupt to the CPU\n");
   return SCPE_OK;
}

const char *
con_description(DEVICE *dptr)
{
   return "1050 Console Terminal";
}


#endif


