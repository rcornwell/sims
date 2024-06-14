/* ibm360_com.c: IBM 360 2703 communications controller

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


*/

#include "ibm360_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#ifdef NUM_DEVS_COM
#define UNIT_COM           UNIT_DISABLE

#define UNIT_V_DIRECT      (UNIT_V_UF + 0)
#define UNIT_DIRECT        (1 << UNIT_V_DIRECT)
#define UNIT_V_UTYPE       (UNIT_V_UF + 1) /* 2741 - ni */
#define UNIT_2741          (0 << UNIT_V_UTYPE)
#define UNIT_1052          (1 << UNIT_V_UTYPE)
#define UNIT_2780          (2 << UNIT_V_UTYPE)
#define UNIT_UTYPE         (3 << UNIT_V_UTYPE)


/* u3 */
#define CMD                u3

#define CMD_WR             0x01        /* Write data to com line */
#define CMD_RD             0x02        /* Read in data from com line */
#define CMD_NOP            0x03        /* Nop command */
#define CMD_PREP           0x06        /* Wait for incoming data  */
#define CMD_POLL           0x09        /* Poll (BSC) */
#define CMD_INH            0x0A        /* Read data without timeout  */
#define CMD_BRK            0x0D        /* Send break signal  */
#define CMD_SRCH           0x0E        /* Wait for EOT character  */
#define CMD_SETM           0x23        /* Set Mode (BSC) */
#define CMD_ENB            0x27        /* Enable line */
#define CMD_DIAL           0x29        /* Dial call */
#define CMD_DIS            0x2F        /* Disable line */

/* u3 second byte */
#define RECV               0x00100     /* Recieving data */
#define SEND               0x00200     /* Sending data */
#define ENAB               0x00400     /* Line enabled */
#define POLL               0x00800     /* Waiting for connection */
#define ADDR               0x01000     /* Address request recieved. */
#define INPUT              0x02000     /* Input ready */
#define ATTN               0x04000     /* Send attention signal */
#define ADDR9              0x08000     /* Address char 9 recieved */
#define BYPASS             0x10000     /* Don't echo output */
#define BREAK              0x20000     /* Return unit exception */

/* Upper 11 bits of u3 hold the device address */

/* u4 */
/* Where we are reading from */
#define IPTR               u4

/* u5 */
/* Sense byte 0 */
#define SNS                u5

/* u6 */
/* Pointer into buffer */

/* us9 */
/* BSC status */
#define BSCDLE             0x0001      /* BSC DLE Char output */
#define BSCTXT             0x0002      /* BSC Text mode */
#define BSCXPR             0x0004      /* BSC transparent mode */
#define BSCEIB             0x0008      /* BSC EIB byte after ITB/EBT/ETX */
#define BSCCC2             0x0010      /* BSC CC Character 2 */
#define BSCCC1             0x0020      /* BSC CC Character 1 */
#define BSCSYN1            0x0100      /* One SYNC Char recieved */
#define BSCSYN2            0x0200      /* Second SYNC Char recieved */
#define BSCSYN             0x0400      /* Sync recieved */
#define BSCIDLE            0x1000      /* Input DLE recieved */

#define BPTR       u6
#define BSCSTATE   us9

/* BSC characters */
#define SOH  0x01
#define STX  0x02
#define ETX  0x03
#define HT   0x05
#define DLE  0x10
#define EOM  0x19
#define IBC  0x1f
#define ETB  0x26
#define ESC  0x27
#define ENQ  0x2d
#define SYN  0x32
#define EOT  0x37
#define NAK  0x3d
#define ACK0 0x61
#define ACK1 0x70

uint8       coml_startcmd(UNIT *uptr, uint8 cmd) ;
uint8       coml_haltio(UNIT *uptr);
t_stat      coml_srv(UNIT *uptr);
t_stat      com_reset(DEVICE *dptr);
t_stat      com_scan(UNIT *uptr);
t_stat      com_attach(UNIT *uptr, CONST char *);
t_stat      com_detach(UNIT *uptr);
t_stat      com_help (FILE *, DEVICE *, UNIT *, int32, const char *);
const char *com_description (DEVICE *);

uint8       com_buf[NUM_UNITS_COM][256];
TMLN        com_ldsc[NUM_UNITS_COM];
TMXR        com_desc = { NUM_UNITS_COM, 0, 0, com_ldsc};
int32       tmxr_poll = 10000;


MTAB                com_mod[] = {
    {0}
};

MTAB                coml_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {UNIT_UTYPE, UNIT_2741, "2741", "2741", NULL, NULL, NULL, "IBM 2741 terminal"},
    {UNIT_UTYPE, UNIT_1052, "1052", "1052", NULL, NULL, NULL, "IBM 1052 terminal"},
    {UNIT_UTYPE, UNIT_2780, "2780", "2780", NULL, NULL, NULL, "IBM 2780 RJE station"},
    {UNIT_DIRECT, 0, "DIALUP", "DIALUP", NULL, NULL, NULL, "Dailup line" },
    {UNIT_DIRECT, UNIT_DIRECT, "NODIAL", "NODIAL", NULL, NULL, NULL,
               "Hard wired line" },
    {0}
};

UNIT                com_unit[] = {
    {UDATA(&com_scan, UNIT_ATTABLE | UNIT_DISABLE | UNIT_IDLE, 0)},        /* Line scanner */
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
    coml_haltio, coml_unit, NULL};

DEVICE              com_dev = {
    "COM", com_unit, NULL, com_mod,
    NUM_DEVS_COM, 8, 15, 1, 8, 8,
    NULL, NULL, com_reset, NULL, &com_attach, &com_detach,
    NULL, DEV_MUX | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &com_help, NULL, NULL, &com_description
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
    0x81, 0xD7, 0x96, 0x16, 0x57, 0x8B, 0x61, 0x8D,    /* 40 - 77 */
   /*  (     )    *     +     ,     -     .     / */
    0x93, 0x95, 0x90, 0xE1, 0x37, 0xC0, 0x76, 0x23,
   /*  0    1     2     3     4     5     6     7 */
    0x15, 0x02, 0x04, 0x07, 0x08, 0x0B, 0x0D, 0x0E,
   /*  8    9     :     ;     <     =     >     ? */
    0x10, 0x13, 0x88, 0x87, 0x84, 0x82, 0x8E, 0xA3,
   /*  @    A     B     C     D     E     F     G */
    0x20, 0xE2, 0xE4, 0xE7, 0xE8, 0xEB, 0xED, 0xEE,    /* 100 - 137 */
   /*  H    I     J     K     L     M     N     O */
    0xF0, 0xF3, 0xC3, 0xC5, 0xC6, 0xC9, 0xCA, 0xCC,
   /*  P    Q     R     S     T     U     V     W */
    0xCF, 0xD1, 0xD2, 0xA5, 0xA6, 0xA9, 0xAA, 0xAC,
   /*  X    Y     Z     [     \     ]     ^     _ */
    0xAF, 0xB1, 0xB2, 0x00, 0x00, 0x00, 0x00, 0x40,
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
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
     '@', 0xff, 0xff,  '/', 0xff,  's',  't', 0xff,       /* 0x2x */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff,  'u',  'v', 0xff,  'w', 0xff,  0xff, 'x',
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
    0xff,  'y',  'z', 0xff, 0xff, 0xff, 0xff,  ',',       /* 0x3x */
   /*  8,    9,    A,    B,    C,    D,    E,    F,  */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   /*  0,    1,    2,    3,    4,    5,    6,    7,  */
     '_', 0xff, 0xff,  'j', 0xff,  'k',  'l', 0xff,       /* 0x4x */
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
     'h', 0xff, 0xff,  'i', 0xff, 0xff,  '.', 0xff,       /* 0x7x */
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
     '-',  '-', 0xff,  'J', 0xff,  'K',  'L', 0xff,       /* 0xCx */
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

/*
 * Issue a command to the 2701 controller.
 */
uint8  coml_startcmd(UNIT *uptr,  uint8 cmd) {
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);

    sim_debug(DEBUG_CMD, dptr, "CMD unit=%d %x\n", unit, cmd);
    if ((uptr->CMD & 0xff) != 0) {
       return SNS_BSY;
    }


    switch (cmd & 0x3) {
    case CMD_CTL:          /* Control */
         if ((cmd == CMD_NOP) || (cmd & 0x10) != 0)
             return SNS_CHNEND|SNS_DEVEND;
    case CMD_READ:         /* Read command */
    case CMD_WRITE:        /* Write command */
         uptr->CMD |= cmd;
         uptr->SNS = 0;
         sim_activate(uptr, 200);
         return 0;

    case 0x0:               /* Status */
         if (cmd == CMD_SENSE) {  /* Sense */
            uptr->CMD |= cmd;
            sim_activate(uptr, 200);
            return 0;
         }
         break;
    }
    if (uptr->SNS & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/*
 * Handle halt I/O instruction by stoping running command.
 */
uint8  coml_haltio(UNIT *uptr) {
    uint16         addr = GET_UADDR(uptr->CMD);
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);
    int            cmd = uptr->CMD & 0xff;

    sim_debug(DEBUG_CMD, dptr, "HLTIO unit=%d %x\n", unit, cmd);
    if ((com_unit[0].flags & UNIT_ATT) == 0)              /* attached? */
        return 3;

    switch (cmd) {
    case 0:
    case CMD_DIS:        /* Disable line */
    case CMD_DIAL:       /* Dial call */
    case CMD_SENSE:
    case CMD_SETM:       /* Set mode */
         /* Short commands nothing to do */
         break;

    case CMD_PREP:       /* Wait for incoming data  */
         uptr->CMD &= ~(ADDR9|ADDR|0xff);
         chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
         break;

    case CMD_POLL:       /* Poll command */
    case CMD_INH:        /* Read data without timeout  */
    case CMD_RD:         /* Read in data from com line */
    case CMD_WR:         /* Write data to com line */
    case CMD_BRK:        /* Send break signal  */
    case CMD_SRCH:       /* Wait for EOT character  */
         uptr->CMD &= ~(ADDR9|ADDR|0xff);
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;
    case CMD_ENB:        /* Enable line */
         /* Terminate the operation */
         uptr->CMD &= ~(POLL|ADDR9|ADDR|0xff);
         (void)tmxr_reset_ln(&com_ldsc[unit]);
         chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
         return 1;
    }
    return 1;
}

/* Handle per unit commands */
t_stat coml_srv(UNIT * uptr)
{
    uint16              addr = GET_UADDR(uptr->CMD);
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 unit = (uptr - dptr->units);
    int                 cmd = uptr->CMD & 0xff;
    uint8               ch;
    int                 bsc = (uptr->flags & UNIT_UTYPE) == UNIT_2780;

    switch (cmd) {
    case CMD_SENSE:
         ch = uptr->SNS & 0xff;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 1 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         uptr->CMD &= ~0xff;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case CMD_DIAL:       /* Dial call */
         uptr->SNS = SNS_CMDREJ;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         break;

    case CMD_INH:        /* Read data without timeout  */
    case CMD_RD:         /* Read in data from com line */
         uptr->SNS = 0;
         if (uptr->CMD & ENAB) {
             uptr->CMD |= RECV;
             if (com_ldsc[unit].conn == 0) {
                 uptr->CMD &= ~(0xff|BREAK|INPUT|ENAB|POLL|RECV);
                 uptr->SNS = SNS_INTVENT;
                 uptr->BPTR = 0;
                 uptr->IPTR = 0;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                 return SCPE_OK;
             }
             if (!bsc && (uptr->CMD & ADDR) != 0 && uptr->BPTR == 0) {
                 ch = 0x16;
                 sim_debug(DEBUG_CMD, dptr, "COM: unit=%d addr %02x\n", unit, ch);
                 uptr->CMD &= ~ADDR;
                 if (chan_write_byte( addr, &ch)) {
                     uptr->CMD &= ~(ADDR9|0xff);
                 sim_debug(DEBUG_CMD, dptr, "COM: unit=%d send err\n", unit);
                     chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                     return SCPE_OK;
                 }
                 if (uptr->CMD & ADDR9) {
                     uptr->CMD &= ~(ADDR9|0xff);
                     sim_debug(DEBUG_CMD, dptr, "COM: unit=%d addr9 %02x\n", unit, ch);
                     chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                     return SCPE_OK;
                 }
             } else if (uptr->CMD & BREAK) {
                 uptr->CMD &= ~(0xff|BREAK|INPUT|RECV);
                 uptr->SNS = SNS_INTVENT;
                 uptr->BPTR = 0;
                 uptr->IPTR = 0;
                 sim_debug(DEBUG_DETAIL, dptr, "COM: unit=%d break\n", unit);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK|SNS_UNITEXP);
                 return SCPE_OK;
             } else if (uptr->CMD & INPUT) {
                 int  ack = bsc && ((uptr->BSCSTATE & BSCIDLE) != 0);

                 if (uptr->BPTR == uptr->IPTR) {
                     uptr->CMD &= ~(0xff|INPUT|RECV);
                     uptr->BPTR = 0;
                     uptr->IPTR = 0;

                     chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                     return SCPE_OK;
                 }
                 ch = com_buf[unit][uptr->IPTR++];
                 sim_debug(DEBUG_DETAIL, dptr, "COM: unit=%d read %02x\n", unit, ch);
                 if (bsc) {
                     if (ch == 0x1f) {
                         uptr->CMD |= ADDR;
                     }
                     if (ack && ((ch == ACK0) || (ch == ACK1))) {
                         uptr->CMD &= ~(0xff|INPUT|RECV);
                         uptr->IPTR = 0;
                         uptr->BPTR = 0;
                 sim_debug(DEBUG_DETAIL, dptr, "COM: unit=%d ack\n", unit);
                         chan_end(addr, SNS_CHNEND|SNS_DEVEND|(ch == ACK1 ? SNS_UNITEXP:0));
                         return SCPE_OK;
                     }
                     if (ch == DLE) {
                         uptr->BSCSTATE |= BSCIDLE;
                     } else {
                         uptr->BSCSTATE &= ~BSCIDLE;
                     }
                 }
                 if (chan_write_byte( addr, &ch)) {
                 sim_debug(DEBUG_DETAIL, dptr, "COM: unit=%d send\n", unit);
                     uptr->CMD &= ~(0xff|INPUT|RECV);
                     uptr->IPTR = 0;
                     uptr->BPTR = 0;
                     chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                     return SCPE_OK;
                 }
             }
             sim_activate(uptr, 200);
         } else {
             sim_debug(DEBUG_CMD, dptr, "COM: unit=%d read error\n", unit);
             uptr->CMD &= ~0xff;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
         }
         break;

    case CMD_SETM:       /* Set mode */
         if (bsc) {
             uptr->SNS = 0;
             uptr->CMD &= ~(0xff|BSCTXT|BSCXPR|BSCDLE|BSCEIB);
             if (!chan_read_byte (addr, &ch))
                 ch = 0;
             uptr->CMD |= ch & 0x40 ? BSCEIB : 0;
             if (uptr->CMD & ENAB) {
                 /* bisync: If line already enabled, set mode CCW is restart of
                    device.  Tell 2780 that previous transmission ends.  */
                 tmxr_putc_ln( &com_ldsc[unit], SYN);
                 tmxr_putc_ln( &com_ldsc[unit], EOT);
             }
             sim_debug(DEBUG_CMD, dptr, "COM: unit=%d set mode (%02x)\n", unit, ch);
             chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         } else {
             uptr->SNS = SNS_CMDREJ;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
         break;

    case CMD_POLL:
         if (bsc) {
             uptr->SNS = SNS_CMDREJ;
             uptr->CMD &= ~(0xff|BREAK|INPUT|RECV|SEND|POLL);
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }
         /* Fall through */

    case CMD_WR:         /* Write data to com line */
         uptr->SNS = 0;
         if (uptr->CMD & ENAB) {
             if (com_ldsc[unit].conn == 0) {
                 uptr->CMD &= ~(0xff|BREAK|INPUT|ENAB|POLL);
                 uptr->SNS = SNS_INTVENT;
                 uptr->BPTR = 0;
                 uptr->IPTR = 0;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                 return SCPE_OK;
             }
             if (!bsc && uptr->CMD & BREAK) {
                 sim_debug(DEBUG_CMD, dptr, "COM: unit=%d attn write\n", unit);
                 uptr->CMD &= ~(0xff|BREAK);
                 uptr->SNS |= SNS_INTVENT;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 return SCPE_OK;
             }
             sim_debug(DEBUG_CMD, dptr, "COM: unit=%d write\n", unit);
             if (chan_read_byte (addr, &ch)) {
                 uptr->CMD &= ~0xff;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                 if (bsc) {
                     tmxr_send_buffered_data(&com_ldsc[unit]); /* Flush data */
                 }
             } else if (bsc) {
                 if (uptr->CMD & BSCXPR) {
                     if (ch == DLE) {
                         tmxr_putc_ln(&com_ldsc[unit], DLE);
                     } else if (ch == DLE) {
                         uptr->CMD |= BSCDLE;
                     } else {
                         if ((uptr->CMD & BSCDLE) && (ch == ETX)) {
                             uptr->CMD |= BSCXPR;
                         }
                         uptr->CMD &= ~BSCDLE;
                     }
                     sim_debug(DEBUG_CMD, dptr, "COM: unit=%d BSC send %02x\n",
                           unit, ch);
                     tmxr_putc_ln(&com_ldsc[unit], ch);
                     sim_activate(uptr, 200);
                 }
             } else {
                 int32 data;
                 data = com_2741_out[ch];
                 sim_debug(DEBUG_CMD, dptr, "COM: unit=%d send %02x %02x '%c'\n",
                              unit, ch, data, isprint(data)? data: '^');
                 if (ch == 0x1f) {  /* Check for address character */
                     uptr->CMD |= ADDR;
                 } else if (ch == 0x16) {
                     uptr->CMD &= ~ADDR;
                 } else if (ch == 0xb8) {   /* Bypass */
                     uptr->CMD |= BYPASS;
                 } else if (ch == 0x58) {   /* Restore */
                     uptr->CMD &= ~(BYPASS|ADDR|ADDR9);
                 } else if ((uptr->CMD & ADDR) != 0 && ch == 0x13) {
                     uptr->CMD |= ADDR9;
                 } else if ((uptr->CMD & ADDR) == 0 && data != 0xff) {
                     tmxr_putc_ln( &com_ldsc[unit], data);
                     if (ch == 0x5b || ch == 0xdb)
                         tmxr_putc_ln( &com_ldsc[unit], '\r');
                 }
                 sim_activate(uptr, 2000);
             }
         } else {
             sim_debug(DEBUG_CMD, dptr, "COM: unit=%d write error\n", unit);
             uptr->CMD &= ~0xff;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
         }
         break;

    case CMD_BRK:        /* Send break signal  */
         uptr->CMD &= ~0xff;
         uptr->CMD |= ADDR;
         uptr->SNS = 0;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case CMD_PREP:       /* Wait for incoming data  */
         uptr->SNS = 0;
         if (uptr->CMD & ENAB) {
             if (com_ldsc[unit].conn == 0) {
                 uptr->CMD &= ~(0xff|BREAK|INPUT|ENAB|POLL);
                 uptr->SNS = SNS_INTVENT;
                 uptr->BPTR = 0;
                 uptr->IPTR = 0;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                 return SCPE_OK;
             }
             uptr->CMD |= RECV;
             uptr->CMD &= ~(ADDR|ADDR9);
             if (uptr->CMD & (INPUT|BREAK)) {
                 uptr->CMD &= ~0xff;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             } else {
                 sim_activate(uptr, 200);
             }
         } else {
             uptr->CMD &= ~0xff;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
         }
         break;

    case CMD_SRCH:       /* Wait for EOT character  */
         uptr->CMD &= ~0xff;
         uptr->SNS = 0;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case CMD_ENB:        /* Enable line */
         uptr->SNS = 0;
         if ((uptr->CMD & (POLL|ENAB)) == ENAB) {
             uptr->CMD &= ~0xff;
             uptr->BPTR = 0;
             uptr->IPTR = 0;
             sim_debug(DEBUG_CMD, dptr, "COM: unit=%d enable connect\n", unit);
             chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         } else if ((uptr->CMD & POLL) == 0) {
             sim_debug(DEBUG_CMD, dptr, "COM: unit=%d enable\n", unit);
             (void)tmxr_set_get_modem_bits(&com_ldsc[unit], TMXR_MDM_DTR,
                   0, NULL);
             uptr->CMD |= POLL;
         }
         break;

    case CMD_DIS:        /* Disable line */
         uptr->SNS = 0;
         sim_debug(DEBUG_CMD, dptr, "COM: unit=%d disable\n", unit);
         (void)tmxr_set_get_modem_bits(&com_ldsc[unit], 0, TMXR_MDM_DTR, NULL);
         (void)tmxr_reset_ln(&com_ldsc[unit]);
         uptr->CMD &= ~(0xff|POLL|ENAB) ;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    default:
         uptr->SNS = SNS_CMDREJ;
         uptr->CMD &= ~(0xff|BREAK|INPUT|RECV|SEND|POLL);
         chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         break;
    }

    if ((uptr->CMD & (ENAB|RECV)) == (ENAB|RECV)) {
       int32 data = tmxr_getc_ln(&com_ldsc[unit]);
       if ((data & TMXR_VALID) != 0) {
           if (bsc) {
               int gotdle = (uptr->CMD & BSCDLE) != 0;
               ch = data & 0xff;
               if (ch == DLE) {
                    uptr->CMD |= BSCDLE;
               } else {
                    uptr->CMD &= ~(BSCDLE);
               }
               if ((uptr->CMD & BSCTXT) != 0) {
                    /* Text mode */
                   if ((uptr->CMD & BSCXPR) != 0) {
                       switch (ch) {
                       case DLE:
                               if (gotdle) {
                                   com_buf[unit][uptr->BPTR++] = ch;
                               }
                               break;
                       case SYN:
                               break;
                       case ETB:
                               (void) tmxr_getc_ln(&com_ldsc[unit]); /* BCC */
                               (void) tmxr_getc_ln(&com_ldsc[unit]); /* BCC */
                               /* Fall through */
                       case EOT:
                       case ETX:
                               /* End transparent mode, end read */
                       case IBC:
                               /* End transparent mode, continue read */
                               if (gotdle) {
                                   uptr->CMD &= ~(BSCXPR);
                                   com_buf[unit][uptr->BPTR++] = DLE;
                                   com_buf[unit][uptr->BPTR++] = ch;
                               }
                               if ((uptr->CMD & BSCEIB) != 0) {
                                   com_buf[unit][uptr->BPTR++] = 0;
                               }
                               if (ch != IBC) {
                                   com_buf[unit][uptr->BPTR++] = ACK0;
                                   uptr->CMD |= INPUT;
                                   uptr->CMD &= ~RECV;
                                   sim_activate( uptr, 200);
                                   return SCPE_OK;
                               }
                               break;
                       case ENQ:
                               if (gotdle) {
                                   uptr->CMD &= ~(BSCXPR|BSCTXT);
                                   com_buf[unit][uptr->BPTR++] = DLE;
                                   com_buf[unit][uptr->BPTR++] = ch;
                                   com_buf[unit][uptr->BPTR++] = ACK0;
                                   uptr->CMD |= INPUT;
                                   uptr->CMD &= ~RECV;
                                   sim_activate(uptr, 200);
                                   return SCPE_OK;
                               } else {
                                   if ((uptr->CMD & BSCEIB) != 0) {
                                       com_buf[unit][uptr->BPTR++] = 0;
                                   }
                                   if (ch != IBC) {
                                       com_buf[unit][uptr->BPTR++] = ACK0;
                                       uptr->CMD |= INPUT;
                                       uptr->CMD &= ~RECV;
                                   }
                               }
                               sim_activate( uptr, 200);
                               return SCPE_OK;

                       default:
                               if (gotdle) {
                                   com_buf[unit][uptr->BPTR++] = DLE;
                               }
                               com_buf[unit][uptr->BPTR++] = ch;
                               break;
                       }
                   }
               }
           } else {
               ch = com_2741_in[data & 0x7f];
               sim_debug(DEBUG_DATA, dptr, "COML: unit=%d read '%c' %02x\n", unit, data, ch);
               if (data & SCPE_BREAK) {
                    uptr->CMD |= BREAK;
                    return SCPE_OK;
               }
               /* Handle end of buffer */
               switch (data & 0x7f) {
               case '\r':
               case '\n':
                     com_buf[unit][uptr->BPTR++] = 0x5b;
                     com_buf[unit][uptr->BPTR++] = 0x1f;
                     uptr->CMD |= INPUT;
                     uptr->CMD &= ~RECV;
                     uptr->IPTR = 0;
                     tmxr_putc_ln( &com_ldsc[unit], '\r');
                     tmxr_putc_ln( &com_ldsc[unit], '\n');
                     break;

               case 0177:
               case '\b':
                     if (uptr->BPTR != 0) {
                          uptr->BPTR--;
                          tmxr_putc_ln( &com_ldsc[unit], '\b');
                          tmxr_putc_ln( &com_ldsc[unit], ' ');
                          tmxr_putc_ln( &com_ldsc[unit], '\b');
                     }
                     break;

               case 025: /* ^U clear line */
                     while(uptr->BPTR > 0) {
                         tmxr_putc_ln( &com_ldsc[unit], '\b');
                         tmxr_putc_ln( &com_ldsc[unit], ' ');
                         tmxr_putc_ln( &com_ldsc[unit], '\b');
                         uptr->BPTR--;
                     }
                     break;

               case 03:  /* ^C */
                     uptr->CMD |= BREAK;
                     uptr->CMD &= ~RECV;
                     break;

               default:
                     if (uptr->BPTR < 253) {
                         if (ch == 0x00) {
                            sim_putchar('\007');
                         } else {
                            com_buf[unit][uptr->BPTR++] = ch;
                            if ((uptr->CMD & BYPASS) == 0)
                                tmxr_putc_ln( &com_ldsc[unit], data);
                         }
                     } else {
                         com_buf[unit][uptr->BPTR++] = 0x5b;
                         com_buf[unit][uptr->BPTR++] = 0x1f;
                         uptr->CMD |= INPUT;
                         uptr->CMD &= ~RECV;
                         uptr->BPTR &= 0xff;
                     }
               }
            }
        }
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
    if (ln >= 0) {                                  /* got one? rcv enb*/
        line = &coml_unit[ln];
        sim_debug(DEBUG_DETAIL, &com_dev, "COM line connect %d\n", ln);
        if (line->CMD & ENAB)                        /* Already connected */
            return SCPE_OK;
        if ((line->CMD & POLL) == 0) {               /* Check if not polling */
            if (line->flags & UNIT_DIRECT) {
                 set_devattn(GET_UADDR(line->CMD), SNS_ATTN);
                 line->CMD |= ENAB|ADDR;
                 com_ldsc[ln].rcve = 1;                 /* Mark as ok */
                 sim_activate(line, 200);
            } else {
                 (void)tmxr_set_get_modem_bits(&com_ldsc[ln], 0, TMXR_MDM_DTR, NULL);
                 (void)tmxr_reset_ln(&com_ldsc[ln]);
            }
        } else {
             com_ldsc[ln].rcve = 1;                 /* Mark as ok */
             line->CMD &= ~POLL;
             line->CMD |= ENAB;
             sim_activate(line, 200);
        }
    }

    /* See if any input on line with no command on it */
    for (ln = 0; ln < com_desc.lines; ln++) {
        line = &coml_unit[ln];
        if ((line->CMD & (RECV|ENAB)) == ENAB && tmxr_rqln(&com_ldsc[ln]) > 0) {
            set_devattn(GET_UADDR(line->CMD), SNS_ATTN);
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


t_stat
com_attach(UNIT * uptr, CONST char *cptr)
{
    t_stat        r;
    int           i;

    if ((r = tmxr_attach(&com_desc, uptr, cptr)) != SCPE_OK)
       return r;
    for (i = 0; i< com_desc.lines; i++) {
        coml_unit[i].CMD &= ~0xffff;
    }
    sim_activate(uptr, tmxr_poll);
    return SCPE_OK;
}

t_stat
com_detach(UNIT * uptr)
{
    t_stat        r;
    int           i;

    for (i = 0; i< com_desc.lines; i++) {
        (void)tmxr_set_get_modem_bits(&com_ldsc[i], 0, TMXR_MDM_DTR, NULL);
        (void)tmxr_reset_ln(&com_ldsc[i]);
        coml_unit[i].CMD &= ~0xffff;
    }
    sim_cancel(uptr);
    r = tmxr_detach(&com_desc, uptr);
    return r;
}

t_stat com_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
    const char *cptr)
{
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
return SCPE_OK;
}

const char *com_description (DEVICE *dptr)
{
return "IBM 2703 communications controller";
}

#endif
