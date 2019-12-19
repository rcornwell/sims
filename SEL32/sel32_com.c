/* sel32_com.c: SEL 32 8-Line IOP communications controller

   Copyright (c) 2018, James C. Bevier
   Portions provided by Richard Cornwell and other SIMH contributers

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
   JAMES C. BEVIER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "sel32_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"
#include <ctype.h>

#ifdef NUM_DEVS_COM

extern  t_stat  set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern  t_stat  show_dev_addr(FILE *st, UNIT * uptr, int32 v, CONST void *desc);
extern  void    chan_end(uint16 chan, uint8 flags);
extern  int     chan_read_byte(uint16 chan, uint8 *data);
extern  int     chan_write_byte(uint16 chan, uint8 *data);
extern  void    set_devwake(uint16 addr, uint8 flags);

/* Constants */
#define COM_LINES       8                               /* max lines */
#define COM_LINES_DFLT  8                               /* default lines */
#define COM_INIT_POLL   8000
#define COML_WAIT       500
#define COM_WAIT        500
#define COM_NUMLIN      com_desc.lines                  /* curr # lines */

#define COMC            0                               /* channel thread */
#define COMI            1                               /* input thread */

/* Line status */
#define COML_XIA        0x01                            /* xmt intr armed */
#define COML_XIR        0x02                            /* xmt intr req */
#define COML_REP        0x04                            /* rcv enable pend */
#define COML_RBP        0x10                            /* rcv break pend */

/* Channel state */
#define COMC_IDLE       0                               /* idle */
#define COMC_INIT       1                               /* init */
#define COMC_RCV        2                               /* receive */
#define COMC_END        3                               /* end */

uint8 com_rbuf[COM_LINES];                              /* rcv buf */
uint8 com_xbuf[COM_LINES];                              /* xmt buf */
uint8 com_sta[COM_LINES];                               /* status */
uint32 com_lstat[COM_LINES][2] = { 0 };                 /* 8 bytes of line settings status */
uint32 com_tps = 2;                                     /* polls/second */
uint32 com_scan = 0;                                    /* scanner */
uint32 com_slck = 0;                                    /* scanner locked */
uint32 comc_cmd = COMC_IDLE;                            /* channel state */

TMLN com_ldsc[COM_LINES] = { 0 };                       /* line descrs */
TMXR com_desc = { COM_LINES_DFLT, 0, 0, com_ldsc };     /* com descr */

/* Held in u3 is the device command and status */
#define COM_INCH    0x00    /* Initialize channel command */
#define COM_WR      0x01    /* Write terminal */
#define COM_RD      0x02    /* Read terminal */
#define COM_NOP     0x03    /* No op command */
#define COM_SNS     0x04    /* Sense command */
#define COM_WRSCM   0x05    /* Write w/Sub chan monitor */
#define COM_RDECHO  0x06    /* Read with Echo */
#define COM_RDFC    0x0A    /* Read w/flow control */
#define COM_DEFSC   0x0B    /* Define special char */
#define COM_WRHFC   0x0D    /* Write hardware flow control */
#define COM_RDTR    0x13    /* Reset DTR (ADVR) */
#define COM_SDTR    0x17    /* Set DTR (ADVF) */
#define COM_RRTS    0x1B    /* Reset RTS */
#define COM_SRTS    0x1F    /* Set RTS */
#define COM_RBRK    0x33    /* Reset BREAK */
#define COM_SBRK    0x37    /* Set BREAK */
#define COM_RDHFC   0x8E    /* Read w/hardware flow control only */
#define COM_SACE    0xFF    /* Set ACE parameters */

#define COM_MSK     0xFF    /* Command mask */

/* Status held in u3 */
/* controller/unit address in upper 16 bits */
#define COM_INPUT   0x100   /* Input ready for unit */
#define COM_CR      0x200   /* Output at beginning of line */
#define COM_REQ     0x400   /* Request key pressed */
#define COM_EKO     0x800   /* Echo input character */
#define COM_OUTPUT  0x1000  /* Output ready for unit */
#define COM_READ    0x2000  /* Read mode selected */

/* ACE data kept in u4 */

/* in u5 packs sense byte 0, 1, 2 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ  0x80000000  /* Command reject */
#define SNS_INTVENT 0x40000000  /* Unit intervention required (N/U) */
#define SNS_BOCHK   0x20000000  /* Bus out check (IOP parity error */
#define SNS_EQUIPCK 0x10000000  /* Equipment check (device error) */
#define SNS_DATACK  0x08000000  /* Data check */
#define SNS_OVERRN  0x04000000  /* Overrun (N/U) */
#define SNS_NUB01   0x02000000  /* Zero (N/U) */
#define SNS_NUB02   0x01000000  /* Zero (N/U) */
/* Sense byte 1 */
#define SNS_ASCIICD 0x00800000  /* ASCII control char detected interrupt */
#define SNS_SPCLCD  0x00400000  /* Special char detected interrupt */
#define SNS_ETX     0x00200000  /* ETX interrupt */
#define SNS_BREAK   0x00100000  /* BREAK interrupt */
#define SNS_ACEFE   0x00080000  /* ACE framing error interrupt */
#define SNS_ACEPEI  0x00040000  /* ACE parity error interrupt */
#define SNS_ACEOVR  0x00020000  /* ACE overrun error interrupt */
#define SNS_RING    0x00010000  /* Ring character interrupt */
/* Sense byte 2  Modem status */
#define SNS_RLSDS   0x00008000  /* Received line signal detect status */
#define SNS_RINGST  0x00004000  /* Ring indicator line status */
#define SNS_DSRS    0x00002000  /* DSR Data set ready line status */
#define SNS_CTSS    0x00001000  /* CTS Clear to send line status */
#define SNS_DELTA   0x00000800  /* Delta receive line signal detect failure interrupt */
#define SNS_MRING   0x00000400  /* RI Modem ring interrupt */
#define SNS_DELDSR  0x00000200  /* Delta data set ready interrupt */
#define SNS_DELCLR  0x00000100  /* Ring character interrupt */
/* Sense byte 3  Modem Control/Operation status */
#define SNS_HALFD   0x00000080  /* Half-duplix operation set */
#define SNS_MRINGE  0x00000040  /* Modem ring enabled (1) */
#define SNS_ACEDEF  0x00000020  /* ACE parameters defined */
#define SNS_DIAGM   0x00000010  /* Diagnostic mode set */
#define SNS_AUXOL2  0x00000008  /* Auxiliary output level 2 */
#define SNS_AUXOL1  0x00000004  /* Auxiliary output level 1 */
#define SNS_RTS     0x00000002  /* RTS Request to send set */
#define SNS_DTR     0x00000001  /* DTR Data terminal ready set */
/* Sense byte 4  ACE Parameters status */
#define SNS_ACEDLE  0x80000000  /* Divisor latch enable 0=dis, 1=enb */
#define SNS_ACEBS   0x40000000  /* Break set 0=reset, 1=set */
#define SNS_ACEFP   0x20000000  /* Forced parity 0=odd, 1=even */
#define SNS_ACEP    0x10000000  /* Parity 0=odd, 1=even */
#define SNS_ACEPE   0x08000000  /* Parity enable 0=dis, 1=enb */
#define SNS_ACESTOP 0x04000000  /* Stop bit 0=1, 1=1.5 or 2 */
#define SNS_ACECLEN 0x02000000  /* Character length 00=5, 01=6, 11=7, 11=8 */
#define SNS_ACECL2  0x01000000  /* 2nd bit for above */
/* Sense byte 5  Baud rate */
#define SNS_NUB50   0x00800000  /* Zero N/U */
#define SNS_NUB51   0x00400000  /* Zero N/U */
#define SNS_RINGCR  0x00200000  /* Ring or wakeup character recognition 0=enb, 1=dis */
#define SNS_DIAGL   0x00100000  /* Set diagnostic loopback */
#define SNS_BAUD    0x000F0000  /* Baud rate bits 4-7 */
#define BAUD50      0x00000000  /* 50 baud */
#define BAUD75      0x00010000  /* 75 baud */
#define BAUD110     0x00020000  /* 110 baud */
#define BAUD114     0x00030000  /* 134 baud */
#define BAUD150     0x00040000  /* 150 baud */
#define BAUD300     0x00050000  /* 300 baud */
#define BAUD600     0x00060000  /* 600 baud */
#define BAUD1200    0x00070000  /* 1200 baud */
#define BAUD1800    0x00080000  /* 1800 baud */
#define BAUD2000    0x00090000  /* 2000 baud */
#define BAUD2400    0x000A0000  /* 2400 baud */
#define BAUD3600    0x000B0000  /* 3600 baud */
#define BAUD4800    0x000C0000  /* 4800 baud */
#define BAUD7200    0x000D0000  /* 7200 baud */
#define BAUD9600    0x000E0000  /* 9600 baud */
#define BAUD19200   0x000F0000  /* 19200 baud */
/* Sense byte 6  Firmware ID, Revision Level */
#define SNS_FID     0x00006200  /* ID part 1 */
/* Sense byte 7  Firmware ID, Revision Level */
#define SNS_REV     0x0000004f  /* ID part 2 plus 4 bit rev # */

/* ACE information in u4 */
#define ACE_WAKE    0x0000FF00  /* 8 bit wake-up character */

/* in u5 packs sense byte 0,1 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ  0x80000000    /* Command reject */
#define SNS_INTVENT 0x40000000    /* Unit intervention required */
/* sense byte 3 */
#define SNS_RDY     0x80        /* device ready */
#define SNS_ONLN    0x40        /* device online */
#define SNS_DSR     0x04        /* data set ready */

/* u6 */

uint8       com_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd);
uint8       com_haltio(uint16 addr);
void        com_ini(UNIT *, t_bool);
void        coml_ini(UNIT *, t_bool);
t_stat      com_reset(DEVICE *);
t_stat      com_attach(UNIT *, CONST char *);
t_stat      com_detach(UNIT *);
t_stat      comc_srv(UNIT *uptr);
t_stat      como_srv(UNIT *uptr);
t_stat      comi_srv(UNIT *uptr);
t_stat      com_reset(DEVICE *dptr);
t_stat      com_attach(UNIT *uptr, CONST char *cptr);
t_stat      com_detach(UNIT *uptr);
void        com_reset_ln(int32 ln);
const char  *com_description(DEVICE *dptr);             /* device description */

/* COM data structures
    com_chp     COM channel program information
    com_dev     COM device descriptor
    com_unit    COM unit descriptor
    com_reg     COM register list
    com_mod     COM modifieers list
*/

//#define COM_UNITS 2
#define COM_UNITS 1

/* channel program information */
CHANP           com_chp[COM_UNITS] = {0};

/* dummy mux for 16 lines */
MTAB            com_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr, &show_dev_addr, NULL},
    {MTAB_XTD | MTAB_VDV, 1, NULL, "DISCONNECT", &tmxr_dscln, NULL, &com_desc},
    {UNIT_ATT, UNIT_ATT, "summary", NULL, NULL, &tmxr_show_summ, (void *) &com_desc},
    {MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS", NULL, NULL, &tmxr_show_cstat,(void *)&com_desc},
    {MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATISTICS", NULL, NULL, &tmxr_show_cstat, (void *)&com_desc},
//    {MTAB_XTD|MTAB_VDV, 0, "CHAN", "CHAN", &io_set_dvc, &io_show_dvc, NULL},
//    {MTAB_XTD|MTAB_VDV, 0, "DVA", "DVA", &io_set_dva, &io_show_dva, NULL},
//    {MTAB_XTD | MTAB_VDV, 0, "LINES", "LINES", &com_vlines, &tmxr_show_lines, (void *)&com_desc},
//    {MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "CSTATE", NULL, NULL, &io_show_cst, NULL},
//    {MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "POLL", "POLL", &rtc_set_tps, &rtc_show_tps,(void *)&com_tps},
///    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL },
///    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
///    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
///    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
///    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT", &tmxr_dscln, NULL, &com_desc },
///    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG", &tmxr_set_log, &tmxr_show_log, &com_desc },
///    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG", &tmxr_set_nolog, NULL, &com_desc },
    { 0 }
};

UNIT            com_unit[] = {
    {UDATA(&comi_srv, UNIT_ATTABLE|UNIT_IDLE, 0), COM_WAIT, UNIT_ADDR(0x0000)},       /* 0 */
};

//DIB com_dib = {NULL, com_startcmd, NULL, NULL, com_ini, com_unit, com_chp, COM_UNITS, 0x0f, 0x7e00, 0, 0, 0};

DIB             com_dib = {
    NULL,           /* uint8 (*pre_io)(UNIT *uptr, uint16 chan)*/   /* Pre Start I/O */
    com_startcmd,   /* uint8 (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start a command */
    NULL,           /* uint8 (*halt_io)(UNIT *uptr) */          /* Stop I/O */
    NULL,           /* uint8 (*test_io)(UNIT *uptr) */          /* Test I/O */
    NULL,           /* uint8 (*post_io)(UNIT *uptr) */          /* Post I/O */
    com_ini,        /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    com_unit,       /* UNIT* units */                           /* Pointer to units structure */
    com_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    COM_UNITS,      /* uint8 numunits */                        /* number of units defined */
    0x0f,           /* uint8 mask */                            /* 16 devices - device mask */
    0x7E00,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

REG             com_reg[] = {
    { BRDATAD (STA, com_sta, 16, 8, COM_LINES, "status buffers, lines 0 to 8") },
    { BRDATAD (RBUF, com_rbuf, 16, 8, COM_LINES, "input buffer, lines 0 to 8") },
    { BRDATAD (XBUF, com_xbuf, 16, 8, COM_LINES, "output buffer, lines 0 to 8") },
    { ORDATAD (SCAN, com_scan, 6, "scanner line number") },
    { FLDATAD (SLCK, com_slck, 0, "scanner lock") },
    { DRDATA (TPS, com_tps, 8), REG_HRO},
    { NULL }
    };

/* devices for channel 0x7ecx */
DEVICE          com_dev = {
    "COMC", com_unit, com_reg, com_mod,
    COM_UNITS, 8, 15, 1, 8, 8,
    &tmxr_ex, &tmxr_dep, &com_reset, NULL, &com_attach, &com_detach,
    &com_dib, DEV_NET | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, NULL, NULL, NULL, &com_description
};

/* COML data structures
    coml_dev    COM device descriptor
    coml_unit   COM unit descriptor
    coml_reg    COM register list
    coml_mod    COM modifieers list
*/

/*#define UNIT_COML UNIT_ATTABLE|UNIT_DISABLE|UNIT_IDLE */
#define UNIT_COML UNIT_IDLE|UNIT_DISABLE

/* channel program information */
CHANP           coml_chp[COM_LINES*2] = {0};

UNIT            coml_unit[] = {
    /* 0-7 is input, 8-f is output */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EC0)},       /* 0 */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EC1)},       /* 1 */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EC2)},       /* 2 */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EC3)},       /* 3 */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EC4)},       /* 4 */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EC5)},       /* 5 */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EC6)},       /* 6 */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EC7)},       /* 7 */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EC8)},       /* 8 */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7EC9)},       /* 9 */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7ECA)},       /* A */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7ECB)},       /* B */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7ECC)},       /* C */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7ECD)},       /* D */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7ECE)},       /* E */
    {UDATA(&como_srv, TT_MODE_UC|UNIT_COML, 0), COML_WAIT, UNIT_ADDR(0x7ECF)},       /* F */
};

//DIB coml_dib = { NULL, com_startcmd, NULL, NULL, NULL, coml_ini, coml_unit, coml_chp, COM_LINES*2, 0x0f, 0x7E00};
DIB             coml_dib = {
    NULL,           /* uint8 (*pre_io)(UNIT *uptr, uint16 chan)*/   /* Pre Start I/O */
    com_startcmd,   /* uint8 (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start a command */
    NULL,           /* uint8 (*halt_io)(UNIT *uptr) */          /* Stop I/O */
    NULL,           /* uint8 (*test_io)(UNIT *uptr) */          /* Test I/O */
    NULL,           /* uint8 (*post_io)(UNIT *uptr) */          /* Post I/O */
    coml_ini,       /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    coml_unit,      /* UNIT* units */                           /* Pointer to units structure */
    coml_chp,       /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    COM_LINES*2,    /* uint8 numunits */                        /* number of units defined */
    0x0f,           /* uint8 mask */                            /* 16 devices - device mask */
    0x7E00,         /* uint16 chan_addr */                      /* parent channel address */
};

REG             coml_reg[] = {
    { URDATA (TIME, coml_unit[0].wait, 10, 24, 0, COM_LINES, REG_NZ + PV_LEFT) },
    { NULL }
};

MTAB            coml_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", NULL },
    { TT_MODE, TT_MODE_7B, "7b", "7B", NULL },
    { TT_MODE, TT_MODE_8B, "8b", "8B", NULL },
    { TT_MODE, TT_MODE_7P, "7p", "7P", NULL },
    { MTAB_XTD|MTAB_VUN, 0, NULL, "DISCONNECT",
      &tmxr_dscln, NULL, &com_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, "LOG", "LOG",
      &tmxr_set_log, &tmxr_show_log, &com_desc },
    { MTAB_XTD|MTAB_VUN|MTAB_NC, 0, NULL, "NOLOG",
      &tmxr_set_nolog, NULL, &com_desc },
    { 0 }
    };

DEVICE          coml_dev = {
    "COML", coml_unit, coml_reg, coml_mod,
    COM_LINES*2, 10, 31, 1, 8, 8,
    NULL, NULL, &com_reset,
    NULL, NULL, NULL,
    &coml_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, NULL, NULL, NULL, &com_description
};

/* 8-line serial routines */
void coml_ini(UNIT *uptr, t_bool f)
{
//  int unit;
//  uint16 chsa;

//  unit = uptr - coml_unit;                /* unit # */
//  chsa = GET_UADDR(uptr->u3);             /* get channel/sub-addr */

    /* maybe do someting here on master channel init */
    uptr->u5 = SNS_RDY|SNS_ONLN;            /* status is online & ready */
}

/* 8-line serial routines */
void com_ini(UNIT *uptr, t_bool f)
{
    DEVICE *dptr = find_dev_from_unit(uptr);

    sim_debug(DEBUG_CMD, &com_dev, "COM init device %s controller 0x7e00\n", dptr->name);
    sim_activate(uptr, 1000);               /* time increment */
}

/* called from sel32_chan to start an I/O operation */
uint8  com_startcmd(UNIT *uptr, uint16 chan, uint8 cmd)
{
    DEVICE      *dptr = find_dev_from_unit(uptr);
    int         unit = (uptr - dptr->units);
    uint8       ch;

    if ((uptr->u3 & COM_MSK) != 0) {    /* is unit busy */
        return SNS_BSY;                 /* yes, return busy */
    }

    sim_debug(DEBUG_CMD, &com_dev, "CMD unit %04x chan %04x cmd %02x", unit, chan, cmd);

    /* process the commands */
    switch (cmd & 0xFF) {
    case COM_INCH:      /* 00 */        /* INCH command */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: CMD INCH\n", chan);
        uptr->u3 &= LMASK;              /* leave only chsa */
        uptr->u3 |= (0x7f & COM_MSK);   /* save 0x7f as INCH cmd command */
        uptr->u5 = SNS_RDY|SNS_ONLN;    /* status is online & ready */
        sim_activate(uptr, 20);         /* start us up */
//        return SNS_CHNEND|SNS_DEVEND;   /* all is well */
        break;

    /* write commands must use address 8-f */
    case COM_WR:        /* 0x01 */      /* Write command */
    case COM_WRSCM:     /* 0x05 */      /* Write w/sub channel monitor */
    case COM_WRHFC:     /* 0x0D */      /* Write w/hardware flow control */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd WRITE %02x\n", chan, cmd);

        uptr->u3 &= LMASK;              /* leave only chsa */
        uptr->u3 |= (cmd & COM_MSK);    /* save command */
        uptr->u5 = SNS_RDY|SNS_ONLN;    /* status is online & ready */
        sim_activate(uptr, 150);        /* TRY 08-13-18 */
        return 0;                       /* no status change */
        break;

    /* read commands must use address 0-7 */
    /* DSR must be set when a read command is issued, else it is unit check */
    /* bit 1-3 (ASP) of command has more definition */
    /* bit 1 A=1 ASCII control character  detect (7-char mode only) */
    /* bit 2 S=1 Special character detect (7-char mode only) */
    /* bit 3 P=1 Purge input buffer */
    case COM_RD:        /* 0x02 */      /* Read command */
    case COM_RDECHO:    /* 0x06 */      /* Read command w/ECHO */
    case 0x46:          /* 0x46 */      /* Read command w/ECHO & ASCII */
    case 0x56:          /* 0x56 */      /* Read command w/ECHO & ASCII & Purge input */
    /* if bit 0 set for COM_RDFC, use DTR for flow, else use RTS for flow control */
    case COM_RDFC:      /* 0x0A */      /* Read command w/flow control */
    case COM_RDHFC:     /* 0x8E */      /* Read command w/hardware flow control only */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd read\n", chan);
        uptr->u3 &= LMASK;              /* leave only chsa */
        uptr->u3 |= (cmd & COM_MSK);    /* save command */
        if ((cmd & 0x06) == COM_RDECHO) /* echo command? */
            uptr->u3 |= COM_EKO;        /* save echo status */
        uptr->u3 |= COM_READ;           /* show read mode */
        uptr->u5 = SNS_RDY|SNS_ONLN;    /* status is online & ready */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: input cnt = %04x\n", chan, coml_chp[unit].ccw_count);
        return 0;
        break;

    case COM_NOP:       /* 0x03 */      /* NOP has do nothing */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd %02x NOP\n", chan, cmd);
        uptr->u5 = SNS_RDY|SNS_ONLN;    /* status is online & ready */
        uptr->u3 &= LMASK;              /* leave only chsa */
        uptr->u3 |= (cmd & COM_MSK);    /* save command */
        sim_activate(uptr, 20);         /* start us up */
//        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case COM_SNS:       /* 0x04 */      /* Sense (8 bytes) */
        com_lstat[unit][0] = 0;         /* Clear status wd 0 */
        com_lstat[unit][1] = 0;         /* Clear status wd 1 */
        /* value 4 is Data Set Ready */
        /* value 5 is Data carrier detected n/u */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: unit %04x Cmd Sense %02x\n", chan, unit, uptr->u5);
/* Sense byte 0 */
//#define SNS_CMDREJ    0x80000000  /* Command reject */
//#define SNS_INTVENT   0x40000000  /* Unit intervention required (N/U) */
//#define SNS_BOCHK     0x20000000  /* Bus out check (IOP parity error */
//#define SNS_EQUIPCK   0x10000000  /* Equipment check (device error) */
//#define SNS_DATACK    0x08000000  /* Data check */
//#define SNS_OVERRN    0x04000000  /* Overrun (N/U) */
//#define SNS_NUB01     0x02000000  /* Zero (N/U) */
//#define SNS_NUB02     0x01000000  /* Zero (N/U) */
//      com_lstat[unit][0] |= (SNS_ASCIICD | SNS_SPCLCD|SNS_RING);  /* set char detect status */
//      com_lstat[unit][0] |= (SNS_ASCIICD | SNS_SPCLCD);   /* set char detect status */
//      com_lstat[unit][0] |= (SNS_RING);       /* set char detect status */
        ch = (com_lstat[unit][0] >> 24) & 0xff;
        chan_write_byte(GET_UADDR(uptr->u3), &ch);  /* write status */
/* Sense byte 1 */
//#define SNS_ASCIICD   0x00800000  /* ASCII control char detected interrupt */
//#define SNS_SPCLCD    0x00400000  /* Special char detected interrupt */
//#define SNS_ETX       0x00200000  /* ETX interrupt */
//#define SNS_BREAK     0x00100000  /* X BREAK interrupt */
//#define SNS_ACEFE     0x00080000  /* ACE framing error interrupt */
//#define SNS_ACEPEI    0x00040000  /* ACE parity error interrupt */
//#define SNS_ACEOVR    0x00020000  /* ACE overrun error interrupt */
//#define SNS_RING      0x00010000  /* X Ring character interrupt */
        com_lstat[unit][0] |= (SNS_RING);           /* set char detect status */
        com_lstat[unit][0] |= (SNS_ASCIICD);        /* set char detect status */
        ch = (com_lstat[unit][0] >> 16) & 0xff;
        chan_write_byte(GET_UADDR(uptr->u3), &ch);  /* write status */
/* Sense byte 2  Modem status */
//#define SNS_RLSDS     0x00008000  /* S Received line signal detect status */
//#define SNS_RINGST    0x00004000  /* Ring indicator line status */
//#define SNS_DSRS      0x00002000  /* C DSR Data set ready line status */
//#define SNS_CTSS      0x00001000  /* C CTS Clear to send line status */
//#define SNS_DELTA     0x00000800  /* BS Delta receive line signal detect failure interrupt */
//#define SNS_MRING     0x00000400  /* X RI Modem ring interrupt */
//#define SNS_DELDSR    0x00000200  /* BS Delta data set ready interrupt */
//#define SNS_DELCLR    0x00000100  /* B Delta data set CTS failure interrupt */
        com_lstat[unit][0] |= (SNS_CTSS|SNS_DSRS);      /* set CTS & DSR status */
        com_lstat[unit][0] |= (SNS_MRING);          /* set char detect status */
        ch = (com_lstat[unit][0] >> 8) & 0xff;
        chan_write_byte(GET_UADDR(uptr->u3), &ch);  /* write status */
/* Sense byte 3  Modem Control/Operation status */
//#define SNS_HALFD     0x00000080  /* Half-duplix operation set */
//#define SNS_MRINGE    0x00000040  /* Modem ring enabled (1) */
//#define SNS_ACEDEF    0x00000020  /* ACE parameters defined */
//#define SNS_DIAGM     0x00000010  /* Diagnostic mode set */
//#define SNS_AUXOL2    0x00000008  /* Auxiliary output level 2 */
//#define SNS_AUXOL1    0x00000004  /* Auxiliary output level 1 */
//#define SNS_RTS       0x00000002  /* RTS Request to send set */
//#define SNS_DTR       0x00000001  /* DTR Data terminal ready set */
//      com_lstat[unit][0] |= (SNS_RTS|SNS_DTR);    /* set RTS & DTR status */
        com_lstat[unit][0] |= (SNS_DTR);    /* set DTR status */
        ch = (com_lstat[unit][0] >> 0) & 0xff;

        chan_write_byte(GET_UADDR(uptr->u3), &ch);  /* write status */
        ch = (com_lstat[unit][1] >> 24) & 0xff;
        chan_write_byte(GET_UADDR(uptr->u3), &ch);  /* write status */
        ch = (com_lstat[unit][1] >> 16) & 0xff;
        chan_write_byte(GET_UADDR(uptr->u3), &ch);  /* write status */
        ch = (com_lstat[unit][1] >> 8) & 0xff;
        chan_write_byte(GET_UADDR(uptr->u3), &ch);  /* write status */
        ch = (com_lstat[unit][1] >> 0) & 0xff;
        chan_write_byte(GET_UADDR(uptr->u3), &ch);  /* write status */
        sim_debug(DEBUG_CMD, &com_dev,
            "com_startcmd Cmd SENSE return chan %04x u5-status %04x ls0 %08x ls1 %08x\n",
            chan, uptr->u5, com_lstat[unit][0], com_lstat[unit][1]);
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case COM_DEFSC:     /* 0x0B */      /* Define special char */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd %02x DEFSC\n", chan, cmd);
        chan_read_byte(GET_UADDR(uptr->u3), &ch);   /* read char */
        uptr->u5 = ~SNS_RTS;            /* Request to send not ready */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case COM_RRTS:      /* 0x1B */      /* Reset RTS */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd %02x RRTS\n", chan, cmd);
        uptr->u5 &= ~SNS_RTS;           /* Request to send not ready */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case COM_SRTS:      /* 0x1F */      /* Set RTS */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd %02x SRTS\n", chan, cmd);
        uptr->u5 |= SNS_RTS;            /* Requestd to send ready */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case COM_RBRK:      /* 0x33 */      /* Reset BREAK */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd %02x RBRK\n", chan, cmd);
        uptr->u5 &= ~SNS_BREAK;         /* Request to send not ready */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case COM_SBRK:      /* 0x37 */      /* Set BREAK */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd %02x SBRK\n", chan, cmd);
        uptr->u5 |= SNS_BREAK;          /* Requestd to send ready */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case COM_RDTR:      /* 0x13 */      /* Reset DTR (ADVR) */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd %02x DTR\n", chan, cmd);
        uptr->u5 &= ~SNS_DTR;           /* Data terminal not ready */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

    case COM_SDTR:      /* 0x17 */      /* Set DTR (ADVF) */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd %02x NOP\n", chan, cmd);
        uptr->u5 |= SNS_DTR;            /* Data terminal ready */
        return SNS_CHNEND|SNS_DEVEND;   /* good return */
        break;

#if 0
/* ACE byte 0  Modem Control/Operation status */
/* stored in u4 bytes 0-3 */
#define SNS_HALFD   0x80000000  /* Half-duplix operation set */
#define SNS_MRINGE  0x40000000  /* Modem ring enabled */
#define SNS_ACEFP   0x20000000  /* Forced parity 0=odd, 1=even */
#define SNS_ACEP    0x10000000  /* Parity 0=odd, 1=even */
#define SNS_ACEPE   0x08000000  /* Parity enable 0=dis, 1=enb */
#define SNS_ACESTOP 0x04000000  /* Stop bit 0=1, 1=1.5 or 2 */
#define SNS_ACECLEN 0x02000000  /* Character length 00=5, 01=6, 11=7, 11=8 */
#define SNS_ACECL2  0x01000000  /* 2nd bit for above */

/* ACE byte 1  Baud rate */
#define SNS_NUB50   0x00800000  /* Zero N/U */
#define SNS_NUB51   0x00400000  /* Zero N/U */
#define SNS_RINGCR  0x00200000  /* Ring or wakeup character recognition 0=enb, 1=dis */
#define SNS_DIAGL   0x00100000  /* Set diagnostic loopback */
#define SNS_BAUD    0x000F0000  /* Baud rate bits 4-7 */
#define BAUD50      0x00000000  /* 50 baud */
#define BAUD75      0x00010000  /* 75 baud */
#define BAUD110     0x00020000  /* 110 baud */
#define BAUD114     0x00030000  /* 134 baud */
#define BAUD150     0x00040000  /* 150 baud */
#define BAUD300     0x00050000  /* 300 baud */
#define BAUD600     0x00060000  /* 600 baud */
#define BAUD1200    0x00070000  /* 1200 baud */
#define BAUD1800    0x00080000  /* 1800 baud */
#define BAUD2000    0x00090000  /* 2000 baud */
#define BAUD2400    0x000A0000  /* 2400 baud */
#define BAUD3600    0x000B0000  /* 3600 baud */
#define BAUD4800    0x000C0000  /* 4800 baud */
#define BAUD7200    0x000D0000  /* 7200 baud */
#define BAUD9600    0x000E0000  /* 9600 baud */
#define BAUD19200   0x000F0000  /* 19200 baud */

/* ACE byte 2  Wake-up character */
#define ACE_WAKE    0x0000FF00  /* 8 bit wake-up character */
#endif

    case COM_SACE:      /* 0xff */      /* Set ACE parameters (3 chars) */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd %02x SACE\n", chan, cmd);
        chan_read_byte(GET_UADDR(uptr->u3), &ch);   /* read char 0 */
        uptr->u4 = ((uint32)ch)<<24;                /* byte 0 of ACE data */
        chan_read_byte(GET_UADDR(uptr->u3), &ch);   /* read char 1 */
        uptr->u4 |= ((uint32)ch)<<16;               /* byte 1 of ACE data */
        chan_read_byte(GET_UADDR(uptr->u3), &ch);   /* read char 2 */
        uptr->u4 |= ((uint32)ch)<<8;                /* byte 2 of ACE data */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd %02x ACE bytes %08x\n",
            chan, cmd, uptr->u4);
        return SNS_CHNEND|SNS_DEVEND;               /* good return */
        break;

    default:                                        /* invalid command */
        uptr->u5 |= SNS_CMDREJ;                     /* command rejected */
        sim_debug(DEBUG_CMD, &com_dev, "com_startcmd %04x: Cmd Invald %02x status %02x\n",
            chan, cmd, uptr->u5);
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* unit check */
        break;
    }

    if (uptr->u5 & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Unit service - polled input
   Poll for new connections
   Poll all connected lines for input
*/
t_stat comi_srv(UNIT *uptr)
{
    uint8   ch;
    int32   newln, ln, c;
    uint16  chsa = GET_UADDR(uptr->u3);                 /* get channel/sub-addr */
    int     cmd = uptr->u3 & 0xff;
//  uint32  cln = (uptr - coml_unit) & 0x7;             /* use line # 0-7 for 8-15 */

    ln = uptr - com_unit;                               /* line # */
    sim_debug(DEBUG_CMD, &com_dev, "comi_srv entry chsa %04x line %04x cmd %02x\n", chsa, ln, cmd);
    /* handle NOP and INCH cmds */
    sim_debug(DEBUG_CMD, &com_dev, "comi_srv entry chsa %04x line %04x cmd %02x\n", chsa, ln, cmd);
    if (cmd == COM_NOP || cmd == 0x7f) {                /* check for NOP or INCH */
        uptr->u3 &= LMASK;                              /* leave only chsa */
        sim_debug(DEBUG_CMD, &com_dev, "comi_srv NOP or INCH done chsa %04x line %04x cmd %02x\n",
            chsa, ln, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);          /* done */
        return SCPE_OK;                                 /* return */
    }

    ln = uptr - com_unit;                               /* line # */
    if ((com_unit[COMC].flags & UNIT_ATT) == 0){        /* attached? */
        return SCPE_OK;
    }
    newln = tmxr_poll_conn(&com_desc);                  /* look for connect */
    if (newln >= 0) {                                   /* rcv enb pending? */
        uint16  chsa = GET_UADDR(coml_unit[newln].u3);  /* get channel/sub-addr */
//      int     chan = ((chsa >> 8) & 0x7f);            /* get the channel number */
//      UNIT    *comlp = coml_unit+ln;                  /* get uptr for coml line */
//      int     cmd = comlp->u3 & 0xff;                 /* get the active cmd */
//fprintf(stderr, "comi_srv poll chsa %04x new line %04x\r\n", chsa, newln);
        com_ldsc[newln].rcve = 1;                       /* enable rcv */
//BAD   com_ldsc[newln+8].xmte = 1;                     /* enable xmt for output line */
        com_ldsc[newln].xmte = 1;                       /* enable xmt for output line */
        com_sta[newln] &= ~COML_REP;                    /* clr pending */
        /* send attention to OS here for this channel */
        /* need to get chsa here for the channel */
//fprintf(stderr, "comi_srv chsa %04x chan %04x\r\n", chsa, chan);
        set_devwake(chsa, SNS_ATTN|SNS_DEVEND|SNS_CHNEND);  /* tell user */
    }
    /* poll all devices for input */
    tmxr_poll_rx(&com_desc);                            /* poll for input */
    for (ln = 0; ln < COM_NUMLIN; ln++) {               /* loop thru lines */
        UNIT    *comlp = coml_unit+ln;                  /* get uptr for coml line */
        int     cmd = comlp->u3 & 0xff;                 /* get the active cmd */
        uint16  chsa = GET_UADDR(comlp->u3);            /* get channel/sub-addr */
        if (com_ldsc[ln].conn) {                        /* connected? */
            if ((c = tmxr_getc_ln(&com_ldsc[ln]))) {    /* get char */
//fprintf(stderr, "comi_srv chsa %04x input %02x cmd %02x\r\n", chsa, c, cmd);
                ch = c;                                 /* just the char */
                /* echo the char out */
                tmxr_putc_ln(&com_ldsc[ln], ch);        /* output char */
                tmxr_poll_tx(&com_desc);                /* poll xmt */
                if (c & SCPE_BREAK)                     /* break? */
                    com_sta[ln] |= COML_RBP;            /* set rcv brk */
                else {                                  /* normal char */
                    com_sta[ln] &= ~COML_RBP;           /* clr rcv brk */
                    c = sim_tt_inpcvt(c, TT_GET_MODE(coml_unit[ln].flags));
                    com_rbuf[ln] = c;                   /* save char */
                    if ((cmd & COM_RD) == COM_RD) {     /* read active? */
                        ch = c;                         /* clean the char */
                        if (ch == '\n')                 /* convert newline */
                            ch = '\r';                  /* to C/R */
                        /* write byte to memory */
                        if (chan_write_byte(chsa, &ch)) {
                            /* done, reading chars */
//fprintf(stderr, "comi_srv chsa %04x input %02x complete cmd %02x\r\n", chsa, c, cmd);
                            comlp->u3 &= LMASK;         /* nothing left, clear cmd */
                            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                        } else {
                            /* more to go, continue */
//fprintf(stderr, "comi_srv chsa %04x input %02x cmd %02x\r\n", chsa, c, cmd);
                            if (ch == '\r') {           /* see if done */
                                comlp->u3 &= LMASK;     /* nothing left, clear cmd */
                                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                            }
                        }
                    }
                    else {
                        /* see if user hit the wakeup key */
                        if (((comlp->u4 & ACE_WAKE) >> 8) == ch) {
                            /* send attention to OS here for this channel */
                            /* need to get chsa here for the channel */
//      fprintf(stderr, "comi_srv WAKEUP chsa %04x ch %02x wake %04x\r\n",
//      chsa, ch, (comlp->u4 & ACE_WAKE)>>8);
                            set_devwake(chsa, SNS_ATTN|SNS_DEVEND|SNS_CHNEND);  /* tell user */
                        }
                    }
                }                                       /* end else char */
            }                                           /* end if char */
        }                                               /* end if conn */
        else
            com_sta[ln] &= ~COML_RBP;                   /* disconnected */
    }                                                   /* end for */
    return sim_clock_coschedule(uptr, 200);             /* continue poll */
}

/* Unit service - output transfers */
t_stat como_srv(UNIT *uptr)
{
    uint16  chsa = GET_UADDR(uptr->u3);                 /* get channel/sub-addr */
    uint32  ln = (uptr - coml_unit) & 0x7;              /* use line # 0-7 for 8-15 */
    uint32  done;
    int     cmd = uptr->u3 & 0xff;                      /* get active cmd */
    uint8   ch;

    /* handle NOP and INCH cmds */
    sim_debug(DEBUG_CMD, &com_dev, "como_srv entry chsa %04x line %04x cmd %02x\n", chsa, ln, cmd);
    if (cmd == COM_NOP || cmd == 0x7f) {                /* check for NOP or INCH */
        uptr->u3 &= LMASK;                              /* leave only chsa */
        sim_debug(DEBUG_CMD, &com_dev, "como_srv NOP or INCH done chsa %04x line %04x cmd %02x\n",
            chsa, ln, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);          /* done */
        return SCPE_OK;                                 /* return */
    }

    sim_debug(DEBUG_CMD, &com_dev, "como_srv entry 1 chsa %04x line %04x cmd %02x\n", chsa, ln, cmd);
    if (cmd) {
        /* get a user byte from memory */
        done = chan_read_byte(chsa, &ch);               /* get byte from memory */
        if (done)
            uptr->u3 &= LMASK;                          /* leave only chsa */
    } else
        return SCPE_OK;

    if (com_dev.flags & DEV_DIS) {                      /* disabled */
        sim_debug(DEBUG_CMD, &com_dev, "como_srv chsa %04x line %04x DEV_DIS set\n", chsa, ln);
            if (done) {
                sim_debug(DEBUG_CMD, &com_dev, "como_srv Write DONE %04x status %04x\n",
                    ln, SNS_CHNEND|SNS_DEVEND);
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
            }
            return SCPE_OK;                             /* return */
    }
    sim_debug(DEBUG_CMD, &com_dev, "como_srv poll chsa %04x line %04x DEV_DIS set\n", chsa, ln);

    if (com_ldsc[ln].conn) {                            /* connected? */
        if (com_ldsc[ln].xmte) {                        /* xmt enabled? */
            if (done) {                                 /* are we done writing */
endit:
                uptr->u3 &= LMASK;                      /* nothing left, command complete */
                sim_debug(DEBUG_CMD, &com_dev, "com_srvo write %04x: chnend|devend\n", ln);
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* done */
                return SCPE_OK;
            }
            /* send the next char out */
            tmxr_putc_ln(&com_ldsc[ln], ch);            /* output char */
            sim_debug(DEBUG_CMD, &com_dev, "com_srvo writing char 0x%02x to ln %04x\n", ch, ln);
            tmxr_poll_tx(&com_desc);                    /* poll xmt */
            sim_activate(uptr, uptr->wait);             /* wait */
            return SCPE_OK;
        } else {                                        /* buf full */
            if (done)                                   /* are we done writing */
                goto endit;                             /* done */
            /* just dump the char */
//          /* xmt disabled, just wait around */
            sim_debug(DEBUG_CMD, &com_dev, "com_srvo write dumping char 0x%02x on line %04x\n", ch, ln);
            tmxr_poll_tx(&com_desc);                    /* poll xmt */
//??            sim_activate(uptr, coml_unit[ln].wait);     /* wait */
            sim_activate(uptr, uptr->wait);             /* wait */
            return SCPE_OK;
        }
    } else {
        /* not connected, so dump char on ground */
        if (done) {
            sim_debug(DEBUG_CMD, &com_dev, "com_srvo write dump DONE line %04x status %04x\n",
                ln, SNS_CHNEND|SNS_DEVEND);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);      /* done */
            uptr->u3 &= LMASK;                          /* nothing left, command complete */
        }
        sim_activate(uptr, uptr->wait);                 /* wait */
        return SCPE_OK;
    }
}

/* Reset routine */
t_stat com_reset (DEVICE *dptr)
{
    int32 i;

#ifndef JUNK
    if (com_dev.flags & DEV_DIS)                        /* master disabled? */
        com_dev.flags |= DEV_DIS;                       /* disable lines */
    else
        com_dev.flags &= ~DEV_DIS;
#endif
    if (com_unit[COMC].flags & UNIT_ATT)                /* master att? */
        sim_clock_coschedule(&com_unit[0], 200);        /* activate */
    for (i = 0; i < COM_LINES; i++)                     /* reset lines */
        com_reset_ln(i);
    return SCPE_OK;
}


/* attach master unit */
t_stat com_attach(UNIT *uptr, CONST char *cptr)
{
    uint16      chsa = GET_UADDR(com_unit[COMC].u3);        /* get channel/subaddress */
    t_stat      r;

    chsa = GET_UADDR(com_unit[COMC].u3);        /* get channel/subaddress */
    r = tmxr_attach(&com_desc, uptr, cptr);     /* attach */
    if (r != SCPE_OK)                           /* error? */
        return r;                               /* return error */
    sim_debug(DEBUG_CMD, &com_dev, "com_srv com is now attached chsa %04x\n", chsa);
    sim_activate(uptr, 0);                      /* start poll at once */
    return SCPE_OK;
}

/* detach master unit */
t_stat com_detach(UNIT *uptr)
{
    int32 i;
    t_stat r;

    r = tmxr_detach(&com_desc, uptr);           /* detach */
    for (i = 0; i < COM_LINES; i++)             /* disable rcv */
        com_reset_ln(i);                        /* reset the line */
    sim_cancel(uptr);                           /* stop poll, cancel timer */
    return r;
}

/* Reset an individual line */
void com_reset_ln (int32 ln)
{
    sim_cancel(&coml_unit[ln]);
    com_sta[ln] = 0;
    com_rbuf[ln] = 0;               /* clear read buffer */
    com_xbuf[ln] = 0;               /* clear write buffer */
    com_ldsc[ln].rcve = 0;
    return;
}

t_stat com_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "SEL32 8-Line Async Controller Terminal Interfaces\n\n");
fprintf (st, "Terminals perform input and output through Telnet sessions connected to a \n");
fprintf (st, "user-specified port.\n\n");
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
fprintf (st, "The default mode is 7P.  In addition, each line can be configured to\n");
fprintf (st, "behave as though it was attached to a dataset, or hardwired to a terminal:\n\n");
fprintf (st, "   sim> SET COMLn DATASET        simulate attachment to a dataset (modem)\n");
fprintf (st, "   sim> SET COMLn NODATASET      simulate direct attachment to a terminal\n\n");
fprintf (st, "Finally, each line supports output logging.  The SET COMLn LOG command enables\n");
fprintf (st, "logging on a line:\n\n");
fprintf (st, "   sim> SET COMLn LOG=filename   log output of line n to filename\n\n");
fprintf (st, "The SET COMLn NOLOG command disables logging and closes the open log file,\n");
fprintf (st, "if any.\n\n");
fprintf (st, "Once DCI is attached and the simulator is running, the terminals listen for\n");
fprintf (st, "connections on the specified port.  They assume that the incoming connections\n");
fprintf (st, "are Telnet connections.  The connections remain open until disconnected either\n");
fprintf (st, "by the Telnet client, a SET DCI DISCONNECT command, or a DETACH DCI command.\n\n");
fprintf (st, "Other special commands:\n\n");
fprintf (st, "   sim> SHOW COMC CONNECTIONS    show current connections\n");
fprintf (st, "   sim> SHOW COMC STATISTICS     show statistics for active connections\n");
fprintf (st, "   sim> SET COMLn DISCONNECT     disconnects the specified line.\n");
fprintf (st, "\nThe additional terminals do not support save and restore.  All open connections\n");
fprintf (st, "are lost when the simulator shuts down or DCI is detached.\n");
    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    return SCPE_OK;
}

/* description of controller */
const char *com_description (DEVICE *dptr)
{
    return "SEL 32 8-Line async communications controller";
}

#endif
