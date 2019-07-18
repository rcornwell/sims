/* sel32_iop.c: SEL 32 Class F IOP processor channel.

   Copyright (c) 2018, James C. Bevier

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

   This channel is the interrupt fielder for all of the IOP sub channels.  It's
   channel address is 7E00.  This code handles the INCH command for the IOP
   devices and controls the status FIFO for the iop devices on interrupts and
   TIO instructions..

   Possible devices:
   The f8iop communication controller (TY7EA0), (TY7EB0), (TY7EC0) 
   The ctiop console communications controller (CT7EFC & CT7EFD)
   The lpiop line printer controller (LP7EF8), (LP7EF9)

*/

#include "sel32_defs.h"

#ifdef NUM_DEVS_IOP

extern  t_stat  set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern  t_stat  show_dev_addr(FILE *st, UNIT * uptr, int32 v, CONST void *desc);
extern  void    chan_end(uint16 chan, uint8 flags);
extern  int     chan_read_byte(uint16 chan, uint8 *data);
extern  int     chan_write_byte(uint16 chan, uint8 *data);
extern  void    set_devattn(uint16 addr, uint8 flags);
extern  void    post_extirq(void);
extern  uint32  attention_trap;             /* set when trap is requested */
extern  void    set_devwake(uint16 addr, uint8 flags);

/* Held in u3 is the device command and status */
#define IOP_INCH    0x00    /* Initialize channel command */
#define IOP_MSK     0xff    /* Command mask */

/* Status held in u3 */
/* controller/unit address in upper 16 bits */
#define CON_INPUT   0x100   /* Input ready for unit */
#define CON_CR      0x200   /* Output at beginning of line */
#define CON_REQ     0x400   /* Request key pressed */
#define CON_EKO     0x800   /* Echo input character */
#define CON_OUTPUT  0x1000  /* Output ready for unit */
#define CON_READ    0x2000  /* Read mode selected */

/* Input buffer pointer held in u4 */

/* in u5 packs sense byte 0,1 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ  0x80000000    /* Command reject */
#define SNS_INTVENT 0x40000000    /* Unit intervention required */
/* sense byte 3 */
#define SNS_RDY     0x80        /* device ready */
#define SNS_ONLN    0x40        /* device online */

/* std devices. data structures

    iop_dev     Console device descriptor
    iop_unit    Console unit descriptor
    iop_reg     Console register list
    iop_mod     Console modifiers list
*/

struct _iop_data
{
    uint8       ibuff[145];         /* Input line buffer */
    uint8       incnt;              /* char count */
}
iop_data[NUM_UNITS_CON];

/* forward definitions */
uint8 iop_startcmd(UNIT *, uint16,  uint8);
void    iop_ini(UNIT *, t_bool);
t_stat  iop_srv(UNIT *);

/* channel program information */
CHANP           iop_chp[NUM_UNITS_MT] = {0};

MTAB            iop_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr, &show_dev_addr, NULL},
    {0}
};

UNIT            iop_unit[] = {
    {UDATA(iop_srv, UNIT_ATT|UNIT_IDLE, 0), 0, UNIT_ADDR(0x7E00)},    /* Channel controlller */
};

//DIB iop_dib = {NULL, iop_startcmd, NULL, NULL, NULL, iop_ini, iop_unit, iop_chp, NUM_UNITS_IOP, 0xff, 0x7e00,0,0,0};
DIB             iop_dib = {
    NULL,           /* uint8 (*pre_io)(UNIT *uptr, uint16 chan)*/       /* Start I/O */
    iop_startcmd,   /* uint8 (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start a command SIO */
    NULL,           /* uint8 (*halt_io)(UNIT *uptr) */          /* Stop I/O HIO */
    NULL,           /* uint8 (*test_io)(UNIT *uptr) */          /* Test I/O TIO */
    NULL,           /* uint8 (*post_io)(UNIT *uptr) */          /* Post I/O */
    iop_ini,        /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    iop_unit,       /* UNIT* units */                           /* Pointer to units structure */
    iop_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NUM_UNITS_IOP,  /* uint8 numunits */                        /* number of units defined */
    0xff,           /* uint8 mask */                            /* 16 devices - device mask */
    0x7e00,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    0,              /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE          iop_dev = {
    "IOP", iop_unit, NULL, iop_mod,
    NUM_UNITS_IOP, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &iop_dib, DEV_UADDR|DEV_DISABLE|DEV_DEBUG, 0, dev_debug
};

/* IOP controller routines */
/* initialize the console chan/unit */
void iop_ini(UNIT *uptr, t_bool f)
{
    int     unit = (uptr - iop_unit);               /* unit 0 */
    DEVICE *dptr = &iop_dev;                        /* one and only dummy device */

    sim_debug(DEBUG_CMD, &iop_dev, "IOP init device %s controller/device %x\n", dptr->name, GET_UADDR(uptr->u3));
    iop_data[unit].incnt = 0;                       /* no input data */
    uptr->u5 = SNS_RDY|SNS_ONLN;                    /* status is online & ready */
}

/* start an I/O operation */
uint8  iop_startcmd(UNIT *uptr, uint16 chan, uint8 cmd)
{
    if ((uptr->u3 & IOP_MSK) != 0)                  /* is unit busy */
        return SNS_BSY;                             /* yes, return busy */

    /* process the commands */
    switch (cmd & 0xFF) {
    case IOP_INCH:                                  /* INCH command */
        sim_debug(DEBUG_CMD, &iop_dev, "iop_startcmd %x: Cmd INCH\n", chan);
        return SNS_CHNEND|SNS_DEVEND;               /* all is well */
        break;

    default:                                        /* invalid command */
        uptr->u5 |= SNS_CMDREJ;                     /* command rejected */
        sim_debug(DEBUG_CMD, &iop_dev, "iop_startcmd %x: Cmd Invald %x status %02x\n", chan, cmd, uptr->u5);
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* unit check */
        break;
    }

    if (uptr->u5 & (~(SNS_RDY|SNS_ONLN)))
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle transfers for other sub-channels on IOP */
t_stat iop_srv(UNIT *uptr)
{
    uint16      chsa = GET_UADDR(uptr->u3);
    int         unit = (uptr - iop_unit);           /* unit 0 is channel */

    uptr->u3 &= LMASK;                              /* nothing left, command complete */
    sim_debug(DEBUG_CMD, &iop_dev, "iop_srv chan %d: devend|devend\n", unit);
//  chan_end(chsa, SNS_CHNEND|SNS_DEVEND);          /* TRY 6/12/18 done */
    return SCPE_OK;
}

/* Handle output transfers for console */
t_stat iop_srvo(UNIT *uptr)
{
    uint16      chsa = GET_UADDR(uptr->u3);
    int         cmd = uptr->u3 & IOP_MSK;

    sim_debug(DEBUG_CMD, &iop_dev, "iop_srvo start %x: cmd %x \n", chsa, cmd);
    return SCPE_OK;
}

/* Handle input transfers for console */
t_stat iop_srvi(UNIT *uptr)
{
    uint16      chsa = GET_UADDR(uptr->u3);
    int         cmd = uptr->u3 & IOP_MSK;

    sim_debug(DEBUG_CMD, &iop_dev, "iop_srv start %x: cmd %x \n", chsa, cmd);
    return SCPE_OK;
}

#endif

