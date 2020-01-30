/* ibm360_com.c: IBM 360 2703 communications controller

   Copyright (c) 2017, Richard Cornwell

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


*/

#include "ibm360_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#ifdef NUM_DEVS_COM
#define UNIT_COM          TT_MODE_7B


/* u3 */
#define CMD_RD             0x02       /* Read in data from com line */
#define CMD_WR             0x01       /* Write data to com line */
#define CMD_NOP            0x03       /* Nop command */
#define CMD_DIAL           0x29       /* Dial call */
#define CMD_BRK            0x0D       /* Send break signal  */
#define CMD_PREP           0x06       /* Wait for incoming data  */
#define CMD_INH            0x0A       /* Read data without timeout  */
#define CMD_SRCH           0x0E       /* Wait for EOT character  */
#define CMD_ENB            0x27       /* Enable line */
#define CMD_DIS            0x2F       /* Disable line */

/* u3 second byte */
#define RECV               0x0100     /* Recieving data */
#define SEND               0x0200     /* Sending data */
#define ENAB               0x0400     /* Line enabled */
#define POLL               0x0800     /* Waiting for connection */

/* u5 */
/* Sense byte 0 */
#define SNS_CMDREJ         0x80       /* Command reject */
#define SNS_INTVENT        0x40       /* Unit intervention required */
#define SNS_BUSCHK         0x20       /* Parity error on bus */
#define SNS_EQUCHK         0x10       /* Equipment check */
#define SNS_DATCHK         0x08       /* Data Check */
#define SNS_OVRRUN         0x04       /* Data overrun */
#define SNS_RECV           0x02       /* Receiving */
#define SNS_TIMEOUT        0x01       /* Timeout */

/* u6 */

uint8       coml_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) ;
uint8       coml_haltio(UNIT *uptr);
t_stat      coml_srv(UNIT *uptr);
void        coml_ini(UNIT *uptr, t_bool);
t_stat      com_reset(DEVICE *dptr);
t_stat      com_scan(UNIT *uptr);
t_stat      com_attach(UNIT *uptr, CONST char *);
t_stat      com_detach(UNIT *uptr);

TMLN        com_ldsc[NUM_UNITS_COM];
TMXR        com_desc = { NUM_UNITS_COM, 0, 0, com_ldsc};
int32       tmxr_poll = 10000;


MTAB                com_mod[] = {
    {0}
};

MTAB                coml_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", NULL },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  NULL },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  NULL },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  NULL },
    {0}
};

UNIT                com_unit[] = {
    {UDATA(&com_scan, UNIT_ATTABLE | UNIT_IDLE, 0)},        /* Line scanner */
};

UNIT                coml_unit[] = {
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x030)},       /* 0 */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x031)},       /* 1 */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x032)},       /* 2 */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x033)},       /* 3 */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x034)},       /* 4 */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x035)},       /* 5 */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x036)},       /* 6 */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x037)},       /* 7 */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x038)},       /* 8 */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x039)},       /* 9 */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x03A)},       /* A */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x03B)},       /* B */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x03C)},       /* C */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x03D)},       /* D */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x03E)},       /* E */
    {UDATA(&coml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x03F)},       /* F */
};

struct dib com_dib = { 0xF0, NUM_UNITS_COM, NULL, coml_startcmd, 
    coml_haltio, coml_unit, coml_ini};

DEVICE              com_dev = {
    "COM", com_unit, NULL, com_mod,
    NUM_DEVS_COM, 8, 15, 1, 8, 8,
    NULL, NULL, com_reset, NULL, &com_attach, &com_detach,
    NULL, DEV_MUX | DEV_DISABLE | DEV_DEBUG, 0, dev_debug
};

DEVICE              coml_dev = {
    "COML", coml_unit, NULL, coml_mod,
    NUM_UNITS_COM, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &com_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug
};


 /* Cent = 0xa0 */
static const uint8 com_2741_in[128] = {
   /*     SOH   STX   ETX   EOT   ENQ   ACK   BEL */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     /*0-37*/
   /* 8    9     A     B     C     D     E     F */
   /*BS   HT    LF    VT    FF    CR    SO    SI */
    0xDD, 0xFA, 0xB5, 0x00, 0x00, 0x5b, 0x00, 0x00,    
   /*DLE  DC1   DC2   DC3   DC4   NAK   SYN   ETB */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,    
   /*CAN  EM    SUB   ESC   FS    GS    RS    US */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,    
   /*  sp    !    "     #     $     %     &     ' */
    0x81, 0xD7, 0x00, 0x16, 0x57, 0x8B, 0x61, 0x8D,    /* 40 - 77 */
   /*  (     )    *     +     ,     -     .     / */
    0x93, 0x94, 0x00, 0xE1, 0x37, 0xC0, 0x00, 0x23,   
   /*  0    1     2     3     4     5     6     7 */
    0x15, 0x02, 0x04, 0x07, 0x08, 0x0B, 0x0D, 0x0E,   
   /*  8    9     :     ;     <     =     >     ? */
    0x10, 0x13, 0x88, 0x87, 0x00, 0x82, 0x8E, 0xA3,   
   /*  @    A     B     C     D     E     F     G */
    0x20, 0xE2, 0xE4, 0xE7, 0xE8, 0xEB, 0xED, 0xEE,    /* 100 - 137 */
   /*  H    I     J     K     L     M     N     O */
    0xF0, 0xF3, 0xC3, 0xC5, 0xC6, 0xC9, 0xCA, 0xCC,   
   /*  P    Q     R     S     T     U     V     W */
    0xCF, 0xD1, 0xD2, 0xA5, 0xA6, 0xA9, 0xAA, 0xAC,   
   /*  X    Y     Z     [     \     ]     ^     _ */
    0xAF, 0xB1, 0xB2, 0x00, 0x00, 0x00, 0x00, 0x00,   
   /*  `    a     b     c     d     e     f     g */
    0x00, 0x62, 0x64, 0x67, 0x68, 0x6B, 0x6D, 0x6E,    /* 140 - 177 */
   /*  h    i     j     k     l     m     n     o */
    0x70, 0x73, 0x43, 0x45, 0x46, 0x49, 0x4A, 0x4C,   
   /*  p    q     r     s     t     u     v     w */
    0x4F, 0x51, 0x52, 0x25, 0x26, 0x29, 0x2A, 0x2C,   
   /*  x    y     z     {     |     }     ~   del*/
    0x2F, 0x31, 0x32, 0x00, 0xB7, 0x00, 0xF6, 0x00,   
};

static const uint8 com_2741_out[256] = {
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
    0xff,  ' ',  '1', 0xff,  '2', 0xff, 0xff,  '3',       /* 0x0x */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
     '4', 0xff, 0xff,  '5', 0xff,  '6',  '7', 0xff, 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
     '8', 0xff, 0xff,  '9', 0xff,  '0',  '#', 0xff,       /* 0x1x */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x04, 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
     '@', 0xff, 0xff,  '/', 0xff,  's',  't', 0xff,       /* 0x2x */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff,  'u',  'v', 0xff,  'w', 0xff,  'x', 0xff, 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
    0xff,  'y',  'z', 0xff, 0xff, 0xff, 0xff,  ',',       /* 0x3x */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
     '-', 0xff, 0xff,  'j', 0xff,  'k',  'l', 0xff,       /* 0x4x */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff,  'm',  'n', 0xff,  'o', 0xff, 0xff,  'p', 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
    0xff,  'q',  'r', 0xff, 0xff, 0xff, 0xff,  '$',       /* 0x5x */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff, 0xff, 0xff, 0x0a, 0xff, 0x08, 0xff, 0xff, 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
    0xff,  '&',  'a', 0xff,  'b', 0xff, 0xff,  'c',       /* 0x6x */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
     'd', 0xff, 0xff,  'e', 0xff,  'f',  'g', 0xff, 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
     'h', 0xff, 0xff,  'i', 0xff, 0xff, 0xff, 0xff,       /* 0x7x */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff, 0xff, 0x09, 0xff, 0xff, 0xff, 0xff, 0x7f, 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
    0xff,  ' ',  '=', 0xff,  '<', 0xff, 0xff,  ';',       /* 0x8x */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
     ':', 0xff, 0xff,  '%', 0xff, '\'',  '>', 0xff, 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
     '*', 0xff, 0xff,  '(', 0xff,  ')',  '"', 0xff,       /* 0x9x */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
    0xff, 0xff, 0xff,  '?', 0xff,  'S',  'T', 0xff,       /* 0xAx */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff,  'U',  'V', 0xff,  'W', 0xff, 0xff,  'X', 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
    0xff,  'Y',  'Z', 0xff, 0xff, 0xff, 0xff,  '|',       /* 0xBx */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
    0xff,  '-', 0xff,  'J', 0xff,  'K',  'L', 0xff,       /* 0xCx */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff,  'M',  'N', 0xff,  'O', 0xff, 0xff,  'P', 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
    0xff,  'Q',  'R', 0xff, 0xff, 0xff, 0xff,  '!',       /* 0xDx */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff, 0xff, 0xff, 0x0a, 0xff, 0x08, 0xff, 0xff, 
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
    0xff,  '+',  'A', 0xff,  'B', 0xff, 0xff,  'C',       /* 0xEx */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
     'D', 0xff, 0xff,  'E', 0xff,  'F',  'G', 0xff,
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
     'H', 0xff, 0xff,  'I', 0xff, 0xff,  '~', 0xff,       /* 0xFx */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff, 0xff, 0x09, 0xff, 0xff, 0xff, 0xff, 0x7f
};

uint8  coml_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) {
    uint16         addr = GET_UADDR(uptr->u3);
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);
    uint8          ch;

    sim_debug(DEBUG_CMD, dptr, "CMD unit=%d %x\n", unit, cmd);
    if ((uptr->u3 & 0xff) != 0) {
       return SNS_BSY;
    }


    switch (cmd & 0x3) {
    case 0x3:              /* Control */
         if (cmd == CMD_NOP)
             break;
    case 0x1:              /* Write command */
    case 0x2:              /* Read command */
         uptr->u3 |= cmd;
         sim_activate(uptr, 200);
         return 0;

    case 0x0:               /* Status */
         if (cmd == 0x4) {  /* Sense */
            uptr->u3 |= cmd;
            sim_activate(uptr, 200);
            return 0;
         }
         break;
    }
    if (uptr->u5 & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

uint8  coml_haltio(UNIT *uptr) {
    uint16         addr = GET_UADDR(uptr->u3);
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);
    int            cmd = uptr->u3 & 0xff;
    uint8          ch;

    sim_debug(DEBUG_CMD, dptr, "HLTIO unit=%d %x\n", unit, cmd);

    switch (cmd) {
    case 0:
    case CMD_DIS:        /* Disable line */
    case CMD_DIAL:       /* Dial call */
    case 0x4:
    /* Short commands nothing to do */
         return 0;

    case CMD_INH:        /* Read data without timeout  */
    case CMD_RD:         /* Read in data from com line */
    case CMD_WR:         /* Write data to com line */
    case CMD_BRK:        /* Send break signal  */
    case CMD_PREP:       /* Wait for incoming data  */
    case CMD_SRCH:       /* Wait for EOT character  */
         uptr->u3 &= ~0xffff;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;
    case CMD_ENB:        /* Enable line */
         /* Terminate the operation */
         (void)tmxr_set_get_modem_bits(&com_ldsc[unit], 0, TMXR_MDM_DTR, NULL);
         (void)tmxr_reset_ln(&com_ldsc[unit]);
         uptr->u3 &= ~0xffff;
         break;
    }
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle per unit commands */
t_stat coml_srv(UNIT * uptr)
{
    uint16              addr = GET_UADDR(uptr->u3);
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 unit = (uptr - dptr->units);
    int                 cmd = uptr->u3 & 0xff;
    uint8               ch;


    switch (cmd) {
    case 0:
         break;

    case 0x4:
         ch = uptr->u5 & 0xff;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 1 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         uptr->u3 &= ~0xff;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case CMD_DIAL:       /* Dial call */
         uptr->u5 = SNS_CMDREJ;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         break;
         
    case CMD_INH:        /* Read data without timeout  */
    case CMD_RD:         /* Read in data from com line */
         if (uptr->u3 & ENAB) {
             if (tmxr_rqln(&com_ldsc[unit]) > 0) {
                 int32   data = tmxr_getc_ln (&com_ldsc[unit]);
             sim_debug(DEBUG_DATA, dptr, "COML: unit=%d read '%c'\n", unit, data);
                 if (data & SCPE_BREAK) {
                    uptr->u3 &= ~0xff;
                    chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                    return SCPE_OK;
                 } else {
                    ch = sim_tt_inpcvt (data, TT_GET_MODE(uptr->flags) |
                                                     TTUF_KSR);
                    ch = com_2741_in[data & 0x7f];
                    if (chan_write_byte( addr, &ch)) {
                        uptr->u3 &= ~0xff;
                        chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                        return SCPE_OK;
                    }
                    if (ch == 0x5b) {
                        ch = 0x1f;
                        (void)chan_write_byte( addr, &ch);
                        uptr->u3 &= ~0xff;
                        chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                        return SCPE_OK;
                    }
                }
             }
             sim_activate(uptr, 200);
         }
         break;

    case CMD_WR:         /* Write data to com line */
         if (uptr->u3 & ENAB) {
             sim_debug(DEBUG_CMD, dptr, "COM: unit=%d write\n", unit);
             if (chan_read_byte (addr, &ch)) {
                 uptr->u3 &= ~0xff;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             } else {
                 int32 data;
                 data = com_2741_out[ch];
                 data = sim_tt_outcvt(data, TT_GET_MODE(uptr->flags) |
                                                                  TTUF_KSR);
                 tmxr_putc_ln( &com_ldsc[unit], data);
                 sim_activate(uptr, 200);
             }
         } else {
             sim_debug(DEBUG_CMD, dptr, "COM: unit=%d write error\n", unit);
             uptr->u3 &= ~0xff;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
         }
         break;

    case CMD_BRK:        /* Send break signal  */
         uptr->u3 &= ~0xff;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case CMD_PREP:       /* Wait for incoming data  */
         if (uptr->u3 & ENAB) {
             if (tmxr_rqln(&com_ldsc[unit]) > 0) {
                 uptr->u3 &= ~0xff;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             } else
                 sim_activate(uptr, 200);
         } else {
             uptr->u3 &= ~0xff;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
         }
         break;

    case CMD_SRCH:       /* Wait for EOT character  */
         uptr->u3 &= ~0xff;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case CMD_ENB:        /* Enable line */
         if ((uptr->u3 & (POLL|ENAB)) == ENAB) {
             uptr->u3 &= ~0xff;
             sim_debug(DEBUG_CMD, dptr, "COM: unit=%d enable connect\n", unit);
             chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         } else if ((uptr->u3 & POLL) == 0) {
             sim_debug(DEBUG_CMD, dptr, "COM: unit=%d enable\n", unit);
             (void)tmxr_set_get_modem_bits(&com_ldsc[unit], TMXR_MDM_DTR, 
                   0, NULL);
             uptr->u3 |= POLL;
         }
         break;

    case CMD_DIS:        /* Disable line */
         sim_debug(DEBUG_CMD, dptr, "COM: unit=%d disable\n", unit);
         (void)tmxr_set_get_modem_bits(&com_ldsc[unit], 0, TMXR_MDM_DTR, NULL);
         (void)tmxr_reset_ln(&com_ldsc[unit]);
         uptr->u3 &= ~(0xff|POLL|ENAB) ;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;
    }
    return SCPE_OK;
}

/* Scan for new connections, flush and poll for data */
t_stat com_scan(UNIT * uptr)
{
    UNIT      *line;
    int32      ln;

    sim_activate(uptr, tmxr_poll);          /* continue poll */
    if ((uptr->flags & UNIT_ATT) == 0)              /* attached? */
        return SCPE_OK;
    ln = tmxr_poll_conn (&com_desc);                 /* look for connect */
    sim_debug(DEBUG_DETAIL, &com_dev, "COM Poll %d\n", ln);
    if (ln >= 0) {                                  /* got one? rcv enb*/
        line = &coml_unit[ln];
        sim_debug(DEBUG_DETAIL, &com_dev, "COM line connect %d\n", ln);
        if (line->u3 & ENAB)                        /* Already connected */
            return SCPE_OK;
        if ((line->u3 & POLL) == 0) {               /* Check if not polling */
            (void)tmxr_set_get_modem_bits(&com_ldsc[ln], 0,
                           TMXR_MDM_DTR, NULL);
            (void)tmxr_reset_ln(&com_ldsc[ln]);
        } else {
             com_ldsc[ln].rcve = 1;                 /* Mark as ok */
             line->u3 &= ~POLL;
             line->u3 |= ENAB;
             sim_debug(DEBUG_DETAIL, &com_dev, "COM line connect %d\n", ln);
             sim_activate(line, 200);
        }
    }
    tmxr_poll_tx(&com_desc);
    tmxr_poll_rx(&com_desc);
    return SCPE_OK;
}

t_stat
com_reset(DEVICE * dptr)
{
    sim_activate(&com_unit[0], tmxr_poll);
    return SCPE_OK;
}


void
coml_ini(UNIT * uptr, t_bool f)
{
    UNIT          *srv;
}

t_stat
com_attach(UNIT * uptr, CONST char *cptr)
{
    t_stat        r;
    int           i;

    if ((r = tmxr_attach(&com_desc, uptr, cptr)) != SCPE_OK)
       return r;
    for (i = 0; i< com_desc.lines; i++) {
 //       (void)tmxr_set_line_modem_control(&com_ldsc[i], TRUE);
//        (void)tmxr_set_get_modem_bits(&com_ldsc[i], 0, TMXR_MDM_DTR, NULL);
//        (void)tmxr_reset_ln(&com_ldsc[i]);
        coml_unit[i].u3 &= ~0xffff;
    }
    sim_activate(uptr, tmxr_poll);
    return SCPE_OK;
}

t_stat
com_detach(UNIT * uptr)
{
    t_stat        r;
    int           i;
    UNIT          *srv;

    for (i = 0; i< com_desc.lines; i++) {
        (void)tmxr_set_get_modem_bits(&com_ldsc[i], 0, TMXR_MDM_DTR, NULL);
        (void)tmxr_reset_ln(&com_ldsc[i]);
        coml_unit[i].u3 &= ~0xffff;
    }
    sim_cancel(uptr);
    r = tmxr_detach(&com_desc, uptr);
    return r;
}

t_stat com_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
    const char *cptr)
{
      int i;
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
return SCPE_OK;
}

const char *com_description (DEVICE *dptr)
{
return "IBM 2701 communications controller";
}

#endif
