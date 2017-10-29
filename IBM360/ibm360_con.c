/* ibm360_urec.c: IBM 360 Unit record devices.

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

   This is the standard card reader.
   This is the standard card punch.
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

/* Input buffer pointer held in u4 */

/* in u5 packs sense byte 0,1 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ      0x80    /* Command reject */
#define SNS_INTVENT     0x40    /* Unit intervention required */



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

uint8  con_startcmd(UNIT *, uint16,  uint8);
void                con_ini(UNIT *, t_bool);
t_stat              con_srv(UNIT *);
t_stat              con_reset(DEVICE *);
t_stat              con_attach(UNIT *, char *);
t_stat              con_detach(UNIT *);


UNIT                con_unit[] = {
    {UDATA(con_srv, UNIT_ATT, 0), 0, UNIT_ADDR(0x1F)},       /* A */
};

MTAB                con_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {0}
};

struct dib con_dib = { 0xFF, 1, NULL, con_startcmd, NULL, con_unit, con_ini};

DEVICE              con_dev = {
    "INQ", con_unit, NULL, con_mod,
    NUM_DEVS_CON, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &con_dib, DEV_UADDR | DEV_DISABLE | DEV_DEBUG, 0, dev_debug
};




/*
 * Console printer routines.
 */
void
con_ini(UNIT *uptr, t_bool f) {
     int                 u = (uptr - con_unit);
     con_data[u].inptr = 0;
     uptr->u5 = 0;
     sim_activate(uptr, 1000);
}

uint8  con_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) {
     int                 u = (uptr - con_unit);
    uint8   ch;

    if ((uptr->u3 & CON_MSK) != 0)
        return SNS_BSY;

    if ((cmd & 0xf0) != 0) {
        uptr->u5 |= SNS_CMDREJ;
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    switch (cmd & 0x7) {
    case 2:                        /* Read command */
         sim_debug(DEBUG_CMD, &con_dev, "%d: Cmd RD\n", u);
         if (uptr->u3 & CON_REQ) {
              return SNS_ATTN|SNS_BSY;
         }

         if ((uptr->u3 & CON_INPUT) == 0 &&
                (con_data[u].inptr == 0 || uptr->u3 & CON_CR)) {
             /* Activate input so we can get response */
             sim_putchar('I');
             sim_putchar(' ');
              uptr->u5 &= ~CON_CR;
         }
         uptr->u4 = 0;
         uptr->u3 |= cmd & CON_MSK;
         uptr->u5 = 0;
         return 0;

    case 1:                    /* Write command */
         sim_debug(DEBUG_CMD, &con_dev, "%d: Cmd WR\n", u);
         if (uptr->u3 & CON_REQ) {
              return SNS_ATTN|SNS_BSY;
         }
         uptr->u3 |= cmd & CON_MSK;
         uptr->u5 = 0;
         if (uptr->u3 & CON_CR) {
            sim_putchar('R');
            sim_putchar(' ');
             uptr->u3 &= ~CON_CR;
         }
         return 0;

    case 3:              /* Control */
         sim_debug(DEBUG_CMD, &con_dev, "%d: Cmd NOP\n", u);
         if (uptr->u3 & CON_REQ) {
              return SNS_ATTN|SNS_BSY;
         }
         uptr->u5 = 0;
         return SNS_CHNEND|SNS_DEVEND;

    case 0:               /* Status */
       break;

    case 4:              /* Sense */
         sim_debug(DEBUG_CMD, &con_dev, "%d: Cmd SNS %02x\n", u, uptr->u5);
         /* Check if request pending */
         ch = uptr->u5;
         chan_write_byte(GET_UADDR(uptr->u3), &ch);
         return SNS_CHNEND|SNS_DEVEND;

    default:              /* invalid command */
         uptr->u5 |= SNS_CMDREJ;
         break;
    }

    if (uptr->u5)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle transfer of data for printer */
t_stat
con_srv(UNIT *uptr) {
    uint16              addr = GET_UADDR(uptr->u3);
    int                 u = (uptr - con_unit);
    int                 cmd = uptr->u3 & CON_MSK;
    t_stat              r = SCPE_ARG;       /* Force error if not set */
    uint8               ch;
    int                 i;


    switch (cmd) {
    case CON_WR:
    case CON_ACR:
       if (chan_read_byte(addr, &ch)) {
           if (cmd == CON_ACR) {
               sim_putchar('\r');
               sim_putchar('\n');
               uptr->u3 |= CON_CR;
           }
           uptr->u3 &= ~CON_MSK;
           chan_end(addr, SNS_CHNEND|SNS_DEVEND);
       } else {
           ch = ebcdic_to_ascii[ch];
           if (!isprint(ch))
               ch = '_';
           sim_putchar(ch);
       }
       break;

    case CON_RD:
       if (uptr->u3 & CON_INPUT) {
           uptr->u3 &= ~CON_REQ;
           if (con_data[u].inptr == 0) {
                   uptr->u3 &= ~CON_INPUT;
                   con_data[u].inptr = 0;
                   cmd = 0;
                   uptr->u3 &= ~(CON_MSK);
                   sim_debug(DEBUG_CMD, &con_dev, "%d: devend\n", u);
                   chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                   break;
           }

           ch = con_data[u].ibuff[uptr->u4++];
           sim_debug(DEBUG_CMD, &con_dev, "%d: rd %02x\n", u, ch);
           if (chan_write_byte(addr, &ch)) {
               uptr->u3 &= ~CON_INPUT;
               con_data[u].inptr = 0;
               cmd = 0;
               uptr->u3 &= ~(CON_MSK);
               sim_debug(DEBUG_CMD, &con_dev, "%d: devend input\n", u);
               chan_end(addr, SNS_CHNEND|SNS_DEVEND);
           } else {
               if (uptr->u4 == con_data[u].inptr) {
                   uptr->u3 &= ~CON_INPUT;
                   con_data[u].inptr = 0;
                   cmd = 0;
                   uptr->u3 &= ~(CON_MSK);
                   sim_debug(DEBUG_CMD, &con_dev, "%d: devend\n", u);
                   chan_end(addr, SNS_CHNEND|SNS_DEVEND);
               }
            }
         }
         break;
    }

    r = sim_poll_kbd();
    if (r & SCPE_KFLAG) {
       ch = r & 0377;
       if ((uptr->u3 & CON_INPUT) == 0) {
          /* Handle end of buffer */
          switch (ch) {
          case '\r':
          case '\n':
                sim_debug(DEBUG_DATA, &con_dev, "%d: ent\n", u);
//                if (con_data[u].inptr != 0)
                   uptr->u3 |= CON_INPUT;
                uptr->u3 |= CON_CR;
                sim_putchar('\r');
                sim_putchar('\n');
               /* Fall through */

          case 033: /* request key */
                if (cmd != CON_RD) {
                    uptr->u3 |= CON_REQ;
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
               uptr->u3 &= ~CON_INPUT;
               con_data[u].inptr = 0;
               cmd = 0;
            } else {
               sim_putchar('\007');
            }
        }
    }

    if (cmd == 0 && uptr->u3 & CON_REQ) {
          sim_debug(DEBUG_CMD, &con_dev, "%d: setattn %x\n", u, addr);
          set_devattn(addr, SNS_ATTN);
          uptr->u3 &= ~CON_REQ;
    }
    sim_activate(uptr, 500);
    return SCPE_OK;
}

#endif


