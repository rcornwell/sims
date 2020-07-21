/* sel32_chan.c: SEL 32 Channel functions.

   Copyright (c) 2018-2020, James C. Bevier
   Portions provided by Richard Cornwell, Geert Rolf and other SIMH contributers

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

/* Handle Class E and F channel I/O operations */
#include "sel32_defs.h"

/* Class E I/O device instruction format */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03 04 05|06 07 08 09|10 11 12|13 14 15|16 17 18 19 20 21 22 23|24 25 26 27 28 29 30 31| */
/* |     Op Code     | Channel   |sub-addr|  Aug   |                 Command Code                  | */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */

/* Bits 00-05 - Op code = 0xFC */
/* Bits 00-09 - I/O channel Address (0-15) */
/* Bits 10-12 - I/O sub address (0-7) */
/* Bits 13-15 - Aug code = 6 - CD */
/* Bits 16-31 - Command Code (Device Dependent) */

/* Bits 13-15 - Aug code = 5 - TD */
/* Bits 16-18 - TD Level 2000, 4000, 8000 */
/*      01 - TD 2000 Level Status Testing */
/*      02 - TD 4000 Level Status Testing */
/*      04 - TD 8000 Level Status Testing */
/*              CC1           CC2           CC3            CC4  */
/* TD8000   Undefined       I/O Activ      I/O Error     Dev Stat Present */
/* TD4000   Invd Mem Acc    Mem Parity     Prog Viol     Data Ovr/Undr  */
/* TD2000        -          Status Err       -           Controlr Absent  */

/* Class F I/O device instruction format */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03 04 05|06 07 08|09 10 11 12|13 14 15|16|17 18 19 20 21 22 23|24 25 26 27 28 29 30 31| */
/* |     Op Code     |  Reg   |  I/O type |  Aug   |0 |   Channel Address  |  Device Sub-address   | */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */

/* Bits 00-06 - Op code 0xFC */
/* Bits 09-12 - I/O type */
/*      00 - Unassigned */
/*      01 - Unassigned */
/*      02 - Start I/O (SIO) */
/*      03 - Test I/O (TIO) */
/*      04 - Stop I/O (STPIO */
/*      05 - Reset channel (RSCHNL) */
/*      06 - Halt I/O (HIO) */
/*      07 - Grab controller (GRIO) Not supported */
/*      08 - Reset channel (RSCTL) */
/*      09 - Enable write channel WCS (ECWCS) Not supported */
/*      0A - Unassigned */
/*      0B - Write channel WCS (WCWCS) Not supported */
/*      0C - Enable channel interrupt (ECI) */
/*      0D - Disable channel interrupt (DCI) */
/*      0E - Activate channel interrupt (ACI) */
/*      0F - Deactivate channel interrupt (DACI) */
/* Bits 13-15 - Aug Code */
/* Bit 16 - unused - must be zero */
/* Bits 16-23 - Channel address (0-127) */
/* Bits 24-31 - Device Sub address (0-255) */

uint32  channels        = MAX_CHAN;                 /* maximum number of channels */
int     subchannels     = SUB_CHANS;                /* maximum number of subchannel devices */
int     irq_pend        = 0;                        /* pending interrupt flag */

#define GEERT_UNDO
#ifndef GEERT_UNDO
int     ScanCycles = 0;
int     DoNextCycles = 0;
#include <signal.h>
#endif

extern uint32   CPUSTATUS;                          /* CPU status word */
extern uint32   INTS[];                             /* Interrupt status flags */
extern uint32   TPSD[];                             /*Temp save of PSD from memory 0&4 */

DIB         *dib_unit[MAX_DEV];                     /* Pointer to Device info block */
DIB         *dib_chan[MAX_CHAN];                    /* pointer to channel mux dib */
uint16      loading;                                /* set when booting */

#define get_chan(chsa)  ((chsa>>8)&0x7f)            /* get channel number from ch/sa */

/* forward definitions */
CHANP   *find_chanp_ptr(uint16 chsa);               /* find chanp pointer */
UNIT    *find_unit_ptr(uint16 chsa);                /* find unit pointer */
int     chan_read_byte(uint16 chsa, uint8 *data);
int     chan_write_byte(uint16 chsa, uint8 *data);
void    set_devattn(uint16 chsa, uint16 flags);
void    set_devwake(uint16 chsa, uint16 flags);     /* wakeup O/S for async line */
void    chan_end(uint16 chsa, uint16 flags);
int     test_write_byte_end(uint16 chsa);
t_stat  checkxio(uint16 chsa, uint32 *status);      /* check XIO */
t_stat  startxio(uint16 chsa, uint32 *status);      /* start XIO */
t_stat  testxio(uint16 chsa, uint32 *status);       /* test XIO */
t_stat  stoptxio(uint16 chsa, uint32 *status);      /* stop XIO */
t_stat  rschnlxio(uint16 chsa, uint32 *status);     /* reset channel XIO */
t_stat  haltxio(uint16 chsa, uint32 *status);       /* halt XIO */
t_stat  grabxio(uint16 chsa, uint32 *status);       /* grab XIO n/u */
t_stat  rsctlxio(uint16 chsa, uint32 *status);      /* reset controller XIO */
uint32  find_int_icb(uint16 chsa);
uint32  find_int_lev(uint16 chsa);
uint32  scan_chan(uint32 *ilev);
t_stat  set_inch(UNIT *uptr, uint32 inch_addr); /* set channel inch address */
t_stat  chan_boot(uint16 chsa, DEVICE *dptr);
t_stat  chan_set_devs();
t_stat  set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat  show_dev_addr(FILE *st, UNIT *uptr, int32 v, CONST void *desc);
DEVICE  *get_dev(UNIT *uptr);
void    store_csw(CHANP *chp);
int16   post_csw(CHANP *chp, uint32 rstat);

/* FIFO support */
/* These are FIFO queues which return an error when full.
 *
 * FIFO is empty when in == out.
 * If in != out, then
 * - items are placed into in before incrementing in
 * - items are removed from out before incrementing out
 * FIFO is full when in == (out-1 + FIFO_SIZE) % FIFO_SIZE;
 *
 * The queue will hold FIFO_SIZE items before the calls
 * to FIFO_Put fails.
 */

/* initialize FIFO to empty in boot channel code */

/* add an entry to the FIFO */
int32 FIFO_Put(uint16 chsa, uint32 entry)
{
    DIB *dibp = dib_chan[get_chan(chsa)];           /* get DIB pointer for channel */
    if (dibp == NULL) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "FIFO_Put ERR NULL dib ptr for chsa %04x\n", chsa);
        return -1;                                  /* FIFO address error */
    }

    if (dibp->chan_fifo_in == ((dibp->chan_fifo_out-1+FIFO_SIZE) % FIFO_SIZE)) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "FIFO_Put ERR FIFO full for chsa %04x\n", chsa);
        return -1;                                  /* FIFO Full */
    }
    dibp->chan_fifo[dibp->chan_fifo_in] = entry;    /* add new entry */
    dibp->chan_fifo_in += 1;                        /* next entry */
    dibp->chan_fifo_in %= FIFO_SIZE;                /* modulo FIFO size */
    return 0;                                       /* all OK */
}

/* get the next entry from the FIFO */
int32 FIFO_Get(uint16 chsa, uint32 *old)
{
    DIB *dibp = dib_chan[get_chan(chsa)];           /* get DIB pointer for channel */
    if (dibp == NULL) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "FIFO_Get ERR NULL dib ptr for chsa %04x\n", chsa);
        return -1;                                  /* FIFO address error */
    }

    /* see if the FIFO is empty */
    if (dibp->chan_fifo_in == dibp->chan_fifo_out) {
        return -1;                                  /* FIFO is empty, tell caller */
    }
    *old = dibp->chan_fifo[dibp->chan_fifo_out];    /* get the next entry */
    dibp->chan_fifo_out += 1;                       /* next entry */
    dibp->chan_fifo_out %= FIFO_SIZE;               /* modulo FIFO size */
    return 0;                                       /* all OK */
}

/* get number of entries in FIFO for channel */
int32 FIFO_Num(uint16 chsa)
{
    int32 num;                                      /* number of entries */
    DIB *dibp = dib_chan[get_chan(chsa)];           /* get DIB pointer for channel */
    if (dibp == NULL) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "FIFO_Num ERR NULL dib ptr for chsa %04x\n", chsa);
        return 0;                                   /* FIFO address error */
    }
    /* calc entries */
    num = (dibp->chan_fifo_in - dibp->chan_fifo_out + FIFO_SIZE) % FIFO_SIZE;
    return (num>>1);        /*GT*/                  /* two words/entry */
}

/* add an entry to the RDYQ */
int32 RDYQ_Put(uint32 entry)
{
    /* see if FIFO is full */
    if (RDYQIN == ((RDYQOUT-1+RDYQ_SIZE) % RDYQ_SIZE)) {
        return -1;                                  /* RDYQ Full */
    }
    sim_debug(DEBUG_XIO, &cpu_dev, "RDYQ_Put entry %04x\n", entry);
    RDYQ[RDYQIN] = entry;                           /* add new entry */
    RDYQIN += 1;                                    /* next entry */
    RDYQIN %= RDYQ_SIZE;                            /* modulo RDYQ size */
    irq_pend = 1;                                   /* do a scan */
    return 0;                                       /* all OK */
}

/* get the next entry from the RDYQ */
int32 RDYQ_Get(uint32 *old)
{
    /* see if the RDYQ is empty */
    if (RDYQIN == RDYQOUT) {
        return -1;                                  /* RDYQ is empty, tell caller */
    }
    *old = RDYQ[RDYQOUT];                           /* get the next entry */
    sim_debug(DEBUG_XIO, &cpu_dev, "RDYQ_Get entry %04x\n", *old);
    RDYQOUT += 1;                                   /* next entry */
    RDYQOUT %= RDYQ_SIZE;                           /* modulo RDYQ size */
    return 0;                                       /* all OK */
}

/* get number of entries in RDYQ for channel */
int32 RDYQ_Num(void)
{
    /* calc entries */
    return ((RDYQIN - RDYQOUT + RDYQ_SIZE) % RDYQ_SIZE);
}

/* Set INCH buffer address for channel */
/* return SCPE_OK or SCPE_MEM if invalid address or SCPE_ARG if already defined */
t_stat set_inch(UNIT *uptr, uint32 inch_addr) {
    uint16  chsa = GET_UADDR(uptr->u3);             /* get channel & sub address */
    uint32  chan = chsa & 0x7f00;                   /* get just channel address */
    CHANP   *chp;
    int     i, j;
    DIB     *dibp = dib_chan[chan>>8];              /* get channel dib ptr */
    CHANP   *pchp = 0;                              /* for channel prog ptr */

    sim_debug(DEBUG_XIO, &cpu_dev,
        "set_inch chan %04x inch addr %06x\n", chan, inch_addr);

    /* must be valid DIB pointer */
    if (dibp == NULL)
        return SCPE_MEM;                            /* return memory error */
    pchp = dibp->chan_prg;                          /* get parent channel prog ptr */

    /* must be valid channel pointer */
    if (pchp == NULL)
        return SCPE_MEM;                            /* return memory error */

    /* see if valid memory address */
    if (!MEM_ADDR_OK(inch_addr))                    /* see if mem addr >= MEMSIZE */
        return SCPE_MEM;                            /* return memory error */

    /* set INCH address for all units on master channel */
    chp = pchp;
    for (i=0; i<dibp->numunits; i++) {
        chp->chan_inch_addr = inch_addr;            /* set the inch addr */
        chp++;                                      /* next unit channel */
    }

    /* now go through all the sub addresses for the channel and set inch addr */
    for (i=0; i<256; i++) {
        chsa = chan | i;                            /* merge sa to real channel */
        if (dib_unit[chsa] == dibp)                 /* if same dibp already done */
            continue;
        if (dib_unit[chsa] == 0)                    /* make sure good address */
            continue;                               /* must have a DIB, so not used */
        dibp = dib_unit[chsa];                      /* finally get the new dib adddress */
        chp = dibp->chan_prg;                       /* get first unit channel prog ptr */
        /* set INCH address for all units on channel */
        for (j=0; j<dibp->numunits; j++) {
            chp->chan_inch_addr = inch_addr;        /* set the inch addr */
            chp++;                                  /* next unit channel */
        }
    }
    return SCPE_OK;                                 /* All OK */
}

/* Find interrupt level for the given physical device (ch/sa) */
/* return 0 if not found, otherwise level number */
uint32 find_int_lev(uint16 chsa)
{
    uint32  inta;
    /* get the device entry for channel in SPAD */
    uint32  spadent = SPAD[get_chan(chsa)];         /* get spad device entry for logical channel */

    if (spadent == 0 || spadent == 0xffffffff) {    /* see if valid entry */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_int_lev ERR chsa %04x spadent %08x\n", chsa, spadent);
        return 0;                                   /* not found */
    }
    inta = ((~spadent)>>16)&0x7f;                   /* get interrupt level */

    sim_debug(DEBUG_IRQ, &cpu_dev,
        "find_int_lev class F SPADC %08x chsa %04x lev %02x SPADI %08x INTS %08x\n",
        spadent, chsa, inta, SPAD[inta+0x80], INTS[inta]);
    return(inta);                                   /* return the level*/
}

/* Find interrupt context block address for given device (ch/sa) */
/* return 0 if not found, otherwise ICB memory address */
uint32 find_int_icb(uint16 chsa)
{
    uint32  inta, icba;
    uint32  spadent = SPAD[get_chan(chsa)];         /* get spad device entry for logical channel */

    inta = find_int_lev(chsa);                      /* find the int level */
    if (inta == 0) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_int_icb ERR chsa %04x inta %02x\n", chsa, inta);
        return 0;                                   /* not found */
    }
    /* add interrupt vector table base address plus int # byte address offset */
    icba = SPAD[0xf1] + (inta<<2);                  /* interrupt vector address in memory */
    if (!MEM_ADDR_OK(icba)) {                       /* needs to be valid address in memory */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_int_icb ERR chsa %04x icba %02x\n", chsa, icba);
        return 0;                                   /* not found */
    }
    icba = RMW(icba);                               /* get address of ICB from memory */
    sim_debug(DEBUG_IRQ, &cpu_dev,
        "find_int_icb icba %06x SPADC %08x chsa %04x lev %02x SPADI %08x INTS %08x\n",
       icba, spadent, chsa, inta, SPAD[inta+0x80], INTS[inta]);
    return(icba);                                   /* return the address */
}

/* Find unit pointer for given device (ch/sa) */
UNIT *find_unit_ptr(uint16 chsa)
{
    struct dib  *dibp;                              /* DIB pointer */
    UNIT        *uptr;                              /* UNIT pointer */
    int         i;

    dibp = dib_unit[chsa];                          /* get DIB pointer from device address */
    if (dibp == 0) {                                /* if zero, not defined on system */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_unit_ptr ERR chsa %04x dibp %p\n", chsa, dibp);
        return NULL;                                /* tell caller */
    }

    uptr = dibp->units;                             /* get the pointer to the units on this channel */
    if (uptr == 0) {                                /* if zero, no devices defined on system */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_unit_ptr ERR chsa %04x uptr %p\n", chsa, uptr);
        return NULL;                                /* tell caller */
    }

    for (i = 0; i < dibp->numunits; i++) {          /* search through units to get a match */
        if (chsa == GET_UADDR(uptr->u3)) {          /* does ch/sa match? */
            return uptr;                            /* return the pointer */
        }
        uptr++;                                     /* next unit */
    }
    sim_debug(DEBUG_EXP, &cpu_dev,
        "find_unit_ptr ERR chsa %04x no match uptr %p\n", chsa, uptr);
    return NULL;                                    /* device not found on system */
}

/* Find channel program pointer for given device (ch/sa) */
CHANP *find_chanp_ptr(uint16 chsa)
{
    struct dib  *dibp;                              /* DIB pointer */
    UNIT        *uptr;                              /* UNIT pointer */
    CHANP       *chp;                               /* CHANP pointer */
    int         i;

    dibp = dib_unit[chsa];                          /* get DIB pointer from unit address */
    if (dibp == 0) {                                /* if zero, not defined on system */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_chanp_ptr ERR chsa %04x dibp %p\n", chsa, dibp);
        return NULL;                                /* tell caller */
    }
    if ((chp = (CHANP *)dibp->chan_prg) == NULL) {  /* must have channel information for each device */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_chanp_ptr ERR chsa %04x chp %p\n", chsa, chp);
        return NULL;                                /* tell caller */
    }

    uptr = dibp->units;                             /* get the pointer to the units on this channel */
    if (uptr == 0) {                                /* if zero, no devices defined on system */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "find_chanp_ptr ERR chsa %04x uptr %p\n", chsa, uptr);
        return NULL;                                /* tell caller */
    }

    for (i = 0; i < dibp->numunits; i++) {          /* search through units to get a match */
        if (chsa == GET_UADDR(uptr->u3)) {          /* does ch/sa match? */
            return chp;                             /* return the pointer */
        }
        uptr++;                                     /* next UNIT */
        chp++;                                      /* next CHANP */
    }
    sim_debug(DEBUG_EXP, &cpu_dev,
        "find_chanp_ptr ERR chsa %04x no match uptr %p\n", chsa, uptr);
    return NULL;                                    /* device not found on system */
}

/* Read a full word into memory.
 * Return 1 if fail.
 * Return 0 if success.
 */
int readfull(CHANP *chp, uint32 maddr, uint32 *word)
{
    maddr &= MASK24;                                /* mask addr to 24 bits */
    if (!MEM_ADDR_OK(maddr)) {                      /* see if mem addr >= MEMSIZE */
        chp->chan_status |= STATUS_PCHK;            /* program check error */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "readfull read %08x from addr %08x ERROR\n", *word, maddr);
        return 1;                                   /* show we have error */
    }
    *word = RMW(maddr);                             /* get 1 word */
    sim_debug(DEBUG_XIO, &cpu_dev, "READFULL read %08x from addr %08x\n", *word, maddr);
    return 0;                                       /* return OK */
}

/* Read a byte into the channel buffer.
 * Return 1 if fail.
 * Return 0 if success.
 */
int readbuff(CHANP *chp)
{
    uint32 addr = chp->ccw_addr;                    /* channel buffer address */

    if (!MEM_ADDR_OK(addr & MASK24)) {              /* see if memory address invalid */
        chp->chan_status |= STATUS_PCHK;            /* bad, program check */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "readbuff PCHK addr %08x to big mem %08x status %04x\n",
            addr, MEMSIZE, chp->chan_status);
        chp->chan_byte = BUFF_CHNEND;               /* force channel end & busy */
sim_debug(DEBUG_EXP, &cpu_dev, "readbuff BUFF_CHNEND chp %p chan_byte %04x\n", chp, chp->chan_byte);
        return 1;                                   /* done, with error */
    }
    chp->chan_buf = RMB(addr&MASK24);               /* get 1 byte */
    return 0;
}

/* Write byte to channel buffer in memory.
 * Return 1 if fail.
 * Return 0 if success.
 */
int writebuff(CHANP *chp)
{
    uint32 addr = chp->ccw_addr;

    if (!MEM_ADDR_OK(addr & MASK24)) {              /* make sure write to good addr */
        chp->chan_status |= STATUS_PCHK;            /* non-present memory, abort */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "writebuff PCHK addr %08x to big mem %08x status %04x\n",
            addr, MEMSIZE, chp->chan_status);
        chp->chan_byte = BUFF_CHNEND;               /* force channel end & busy */
sim_debug(DEBUG_EXP, &cpu_dev, "writebuff BUFF_CHNEND chp %p chan_byte %04x\n", chp, chp->chan_byte);
        return 1;
    }
    addr &= MASK24;                                 /* good address, write the byte */
    sim_debug(DEBUG_DATA, &cpu_dev,
        "writebuff WRITE addr %06x DATA %08x status %04x\n",
        addr, chp->chan_buf, chp->chan_status);
    WMB(addr, chp->chan_buf);                       /* write byte to memory */
    return 0;
}

/* load in the IOCD and process the commands */
/* return = 0 OK */
/* return = 1 error, chan_status will have reason */
int32 load_ccw(CHANP *chp, int32 tic_ok)
{
    uint32      word1 = 0;
    uint32      word2 = 0;
    int         docmd = 0;
    UNIT        *uptr = chp->unitptr;               /* get the unit ptr */
    uint16      chan = get_chan(chp->chan_dev);     /* our channel */
    uint16      devstat = 0;

loop:
    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "load_ccw @%06x entry chan_status[%04x] %04x\n", chp->chan_caw, chan, chp->chan_status);
    /* Abort if we have any errors */
    if (chp->chan_status & STATUS_ERROR) {          /* check channel error status */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "load_ccw ERROR chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;
    }

    /* Check if we have status modifier set */
    if (chp->chan_status & STATUS_MOD) {
        chp->chan_caw += 8;                         /* move to next IOCD */
        chp->chan_status &= ~STATUS_MOD;            /* turn off status modifier flag */
    }

    /* Read in first CCW */
    if (readfull(chp, chp->chan_caw, &word1) != 0) { /* read word1 from memory */
        chp->chan_status |= STATUS_PCHK;            /* memory read error, program check */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "load_ccw ERROR chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                                   /* error return */
    }

    /* Read in second CCW */
    if (readfull(chp, chp->chan_caw+4, &word2) != 0) { /* read word2 from memory */
        chp->chan_status |= STATUS_PCHK;            /* memory read error, program check */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "load_ccw ERROR chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                                   /* error return */
    }

    sim_debug(DEBUG_XIO, &cpu_dev,
        "load_ccw @%06x read ccw chan %02x IOCD wd 1 %08x wd 2 %08x\n",
        chp->chan_caw, chan, word1, word2);

#ifdef FOR_DEBUG
    if ((word2>>16) & (FLAG_CC|FLAG_DC)) {
        uint32      word3 = 0;
        uint32      word4 = 0;
        readfull(chp, chp->chan_caw+8, &word3);     /* read word3 from memory */
        readfull(chp, chp->chan_caw+12, &word4);    /* read word4 from memory */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "load_ccw @%06x read ccw chan %02x IOCD wd 3 %08x wd 4 %08x\n",
            chp->chan_caw, chan, word3, word4);
    }
#endif

    /* TIC can't follow TIC or be first in command chain */
    if (((word1 >> 24) & 0xf) == CMD_TIC) {
        if (tic_ok) {
            chp->chan_caw = word1 & MASK24;         /* get new IOCD address */
            tic_ok = 0;                             /* another tic not allowed */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "load_ccw tic cmd ccw chan %02x tic caw %06x IOCD wd 1 %08x\n",
                chan, chp->chan_caw, word1);
            goto loop;                              /* restart the IOCD processing */
        }
        chp->chan_status |= STATUS_PCHK;            /* program check for invalid tic */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "load_ccw TIC ERROR chan_status[%04x] %04x\n", chan, chp->chan_status);
        return 1;                                   /* error return */
    }

    /* Check if not chaining data */
    if ((chp->ccw_flags & FLAG_DC) == 0) {
        chp->ccw_cmd = (word1 >> 24) & 0xff;        /* not DC, so set command from IOCD wd 1 */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "load_ccw @%06x DO CMD No DC, ccw_flags %04x cmd %02x\n",
            chp->chan_caw, chp->ccw_flags, chp->ccw_cmd);
        docmd = 1;                                  /* show we have a command */
    }

    /* added next line 051220 */
    chp->chan_status = 0;                           /* clear status for next IOCD */
    /* Set up for this command */
    /* make a 24 bit address */
    chp->ccw_addr = word1 & MASK24;                 /* set the data/seek address */
    chp->chan_caw += 8;                             /* point to to next IOCD */
    chp->ccw_count = word2 & 0xffff;                /* get 16 bit byte count from IOCD WD 2*/
    chp->chan_byte = BUFF_BUSY;                     /* busy & no bytes transferred yet */
sim_debug(DEBUG_EXP, &cpu_dev, "load_ccw BUFF_BUSY chp %p chan_byte %04x\n", chp, chp->chan_byte);
    chp->ccw_flags = (word2 >> 16) & 0xffff;        /* get flags from bits 0-7 of WD 2 of IOCD */
    if (chp->ccw_flags & FLAG_PCI) {                /* do we have prog controlled int? */
        chp->chan_status |= STATUS_PCI;             /* set PCI flag in status */
        irq_pend = 1;                               /* interrupt pending */
    }

    sim_debug(DEBUG_XIO, &cpu_dev,
        "load_ccw @%06x read docmd %01x addr %06x count %04x chan %04x ccw_flags %04x\n",
        chp->chan_caw, docmd, chp->ccw_addr, chp->ccw_count, chan, chp->ccw_flags);

    if (docmd) {                                    /* see if we need to process a command */
        DIB *dibp = dib_unit[chp->chan_dev];        /* get the DIB pointer */
 
        uptr = chp->unitptr;                        /* get the unit ptr */
        if (dibp == 0 || uptr == 0) {
            chp->chan_status |= STATUS_PCHK;        /* program check if it is */
            return 1;                               /* if none, error */
        }

        sim_debug(DEBUG_XIO, &cpu_dev,
            "load_ccw @%06x before start_cmd chan %04x status %04x count %04x\n",
            chp->chan_caw, chan, chp->chan_status, chp->ccw_count);

        /* call the device startcmd function to process the current command */
        /* just replace device status bits */
        devstat = dibp->start_cmd(uptr, chan, chp->ccw_cmd);
        chp->chan_status = (chp->chan_status & 0xff00) | devstat;

        sim_debug(DEBUG_XIO, &cpu_dev,
            "load_ccw @%06x after start_cmd chan %04x status %08x count %04x\n",
            chp->chan_caw, chan, chp->chan_status, chp->ccw_count);

        /* see if bad status */
//      if (chp->chan_status & (STATUS_ATTN|STATUS_CHECK|STATUS_EXPT)) {
        if (chp->chan_status & (STATUS_ATTN|STATUS_ERROR)) {
            chp->chan_status |= STATUS_CEND;        /* channel end status */
            chp->ccw_flags = 0;                     /* no flags */
            chp->ccw_cmd = 0;                       /* stop IOCD processing */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "load_ccw bad status chan %04x status %04x\n",
                chan, chp->chan_status);
            chp->chan_byte = BUFF_NEXT;             /* have main pick us up */
sim_debug(DEBUG_EXP, &cpu_dev, "load_ccw BUFF_NEXT chp %p chan_byte %04x\n", chp, chp->chan_byte);
            RDYQ_Put(chp->chan_dev);                /* queue us up */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "load_ccw continue wait chsa %04x status %08x\n",
                chp->chan_dev, chp->chan_status);
        }

        /* NOTE this code needed for MPX 1.X to run! */
        /* see if command completed */
        /* we have good status */
        if (chp->chan_status & (STATUS_DEND|STATUS_CEND)) {
            uint16  chsa = GET_UADDR(uptr->u3);     /* get channel & sub address */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* show I/O complete */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "load_ccw @%06x FIFO #%1x cmd complete chan %04x status %04x count %04x\n",
                chp->chan_caw, FIFO_Num(chsa), chan, chp->chan_status, chp->ccw_count);
        }
    }
    /* the device processor return OK (0), so wait for I/O to complete */
    /* nothing happening, so return */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "load_ccw @%06x return, chan %04x status %04x count %04x irq_pend %1x\n",
        chp->chan_caw, chan, chp->chan_status, chp->ccw_count, irq_pend);
    return 0;                                       /* good return */
}

/* read byte from memory */
/* write to device */
int chan_read_byte(uint16 chsa, uint8 *data)
{
    int     chan = get_chan(chsa);                  /* get the channel number */
    CHANP   *chp = find_chanp_ptr(chsa);            /* get channel prog pointer */
    int     byte;

    /* Abort if we have any errors */
    if (chp->chan_status & STATUS_ERROR)            /* check channel error status */
        return 1;                                   /* return error */

    if (chp->chan_byte == BUFF_CHNEND)              /* check for end of data */
        return 1;                                   /* yes, return error */

    if (chp->ccw_count == 0) {                      /* see if more data required */
         if ((chp->ccw_flags & FLAG_DC) == 0) {     /* see if Data Chain */
            chp->chan_byte = BUFF_CHNEND;           /* buffer end too */
sim_debug(DEBUG_EXP, &cpu_dev, "chan_read BUFF_CHNEND chp %p chan_byte %04x\n", chp, chp->chan_byte);
            sim_debug(DEBUG_XIO, &cpu_dev,
                "chan_read_byte no DC chan end, cnt %04x addr %06x chan %04x\n",
                chp->ccw_count, chp->ccw_addr, chan);
            return 1;                               /* return error */
         } else {
            /* we have data chaining, process iocl */
            if (load_ccw(chp, 1)) {                 /* process data chaining */
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "chan_read_byte with DC error, cnt %04x addr %06x chan %04x\n",
                    chp->ccw_count, chp->ccw_addr, chan);
                return 1;                           /* return error */
            }
            sim_debug(DEBUG_EXP, &cpu_dev,
                "chan_read_byte with DC IOCD loaded, cnt %04x addr %06x chan %04x\n",
                chp->ccw_count, chp->ccw_addr, chan);
         }
    }
    /* get the next byte from memory */
    if (readbuff(chp))                              /* read next char */
        return 1;                                   /* return error */

    /* get the byte of data */
    byte = chp->chan_buf;                           /* read byte from memory */
    *data = byte;                                   /* return the data */
    sim_debug(DEBUG_DATA, &cpu_dev, "chan_read_byte transferred %02x\n", byte);
    chp->ccw_addr += 1;                             /* next byte address */
    chp->ccw_count--;                               /* one char less to process */
    return 0;                                       /* good return */
}

/* test end of write byte I/O (device read) */
int test_write_byte_end(uint16 chsa)
{
    CHANP   *chp = find_chanp_ptr(chsa);            /* get channel prog pointer */

    /* see if at end of buffer */
    if (chp->chan_byte == BUFF_CHNEND)              /* check for end of data */
        return 1;                                   /* return done */
    if (chp->ccw_count == 0) {
        if ((chp->ccw_flags & FLAG_DC) == 0) {      /* see if we have data chaining */
            chp->chan_status |= STATUS_CEND;        /* no, end of data */
            chp->chan_byte = BUFF_CHNEND;           /* thats all the data we want */
sim_debug(DEBUG_EXP, &cpu_dev, "test_write_byte BUFF_CHNEND chp %p chan_byte %04x\n", chp, chp->chan_byte);
            return 1;                               /* return done */
        }
    }
    return 0;                                       /* not done yet */
}

/* write byte to memory */
/* read from device */
int chan_write_byte(uint16 chsa, uint8 *data)
{
    int     chan = get_chan(chsa);                  /* get the channel number */
    CHANP   *chp = find_chanp_ptr(chsa);            /* get channel prog pointer */

    /* Abort if we have any errors */
    if (chp->chan_status & STATUS_ERROR)            /* check channel error status */
        return 1;                                   /* return error */

    /* see if at end of buffer */
    if (chp->chan_byte == BUFF_CHNEND) {            /* check for end of data */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "chan_write_byte BUFF_CHNEND ccw_flags %04x addr %06x cnt %04x\n",
            chp->ccw_flags, chp->ccw_addr, chp->ccw_count);
        /* if SLI not set, we have incorrect length */
        if ((chp->ccw_flags & FLAG_SLI) == 0) {
            sim_debug(DEBUG_EXP, &cpu_dev, "chan_write_byte 4 setting SLI ret\n");
            chp->chan_status |= STATUS_LENGTH;      /* set SLI */
        }
        return 1;                                   /* return error */
    }
    if (chp->ccw_count == 0) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "chan_write_byte ZERO chan %04x ccw_count %04x addr %06x\n",
            chan, chp->ccw_count, chp->ccw_addr);

        if ((chp->ccw_flags & FLAG_DC) == 0) {      /* see if we have data chaining */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "chan_write_byte no DC ccw_flags %04x\n", chp->ccw_flags);
            chp->chan_status |= STATUS_CEND;        /* no, end of data */
            chp->chan_byte = BUFF_CHNEND;           /* thats all the data we want */
sim_debug(DEBUG_EXP, &cpu_dev, "chan_write_byte BUFF_CHNEND chp %p chan_byte %04x\n", chp, chp->chan_byte);
            return 1;                               /* return done error */
        } else {
            /* we have data chaining, process iocl */
            sim_debug(DEBUG_DETAIL, &cpu_dev,
                "chan_write_byte got DC, calling load_ccw chan %04x\n", chan);
            if (load_ccw(chp, 1)) {                 /* process data chaining */
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "chan_write_byte with DC error, cnt %04x addr %06x chan %04x\n",
                    chp->ccw_count, chp->ccw_addr, chan);
                return 1;                           /* return error */
            }
            sim_debug(DEBUG_EXP, &cpu_dev,
                "chan_write_byte with DC IOCD loaded cnt %04x addr %06x chan %04x\n",
                chp->ccw_count, chp->ccw_addr, chan);
        }
    }
    /* we have data byte to write to chp->ccw_addr */
    /* see if we want to skip writing data to memory */
    if (chp->ccw_flags & FLAG_SKIP) {
        chp->ccw_count--;                           /* decrement skip count */
        chp->chan_byte = BUFF_BUSY;                 /* busy, but no data */
sim_debug(DEBUG_EXP, &cpu_dev, "chan_write_byte1 BUFF_BUSY chp %p chan_byte %04x\n", chp, chp->chan_byte);
        if ((chp->ccw_cmd & 0xff) == CMD_RDBWD)
            chp->ccw_addr--;                        /* backward */
        else
            chp->ccw_addr++;                        /* forward */
        sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_write_byte SKIP ret addr %08x cnt %04x\n",
            chp->ccw_addr, chp->ccw_count);
        return 0;
    }
    chp->chan_buf = *data;                          /* get data byte */
    if (writebuff(chp))                             /* write the byte */
        return 1;

    chp->ccw_count--;                               /* reduce count */
    chp->chan_byte = BUFF_BUSY;                     /* busy, but no data */
//sim_debug(DEBUG_EXP, &cpu_dev, "chan_write_byte2 BUFF_BUSY chp %p chan_byte %04x\n", chp, chp->chan_byte);
    if ((chp->ccw_cmd & 0xff) == CMD_RDBWD)         /* see if reading backwards */
        chp->ccw_addr -= 1;                         /* no, use previous address */
    else
        chp->ccw_addr += 1;                         /* yes, use next address */
    return 0;
}

/* post wakeup interrupt for specified async line */
void set_devwake(uint16 chsa, uint16 flags)
{
    uint32  stwd1, stwd2;                           /* words 1&2 of stored status */
    /* put sub address in byte 0 */
    stwd1 = (chsa & 0xff) << 24;                    /* subaddress and IOCD address to SW 1 */
    /* save 16 bit channel status and residual byte count in SW 2 */
    stwd2 = (uint32)flags << 16;
    if ((FIFO_Put(chsa, stwd1) == -1) || (FIFO_Put(chsa, stwd2) == -1)) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "set_devwake FIFO Overflow ERROR on chsa %04x\n", chsa);
    }
    irq_pend = 1;                                   /* wakeup controller */
}

/* post interrupt for specified channel */
void set_devattn(uint16 chsa, uint16 flags)
{
    CHANP   *chp = find_chanp_ptr(chsa);            /* get channel prog pointer */

    if (chp == NULL) {
        /* can not do anything, so just return */
        sim_debug(DEBUG_EXP, &cpu_dev, "set_devattn chsa %04x, flags %04x\n", chsa, flags);
        fprintf(stdout, "set_devattn chsa %04x invalid configured device\n", chsa);
        fflush(stdout);
        return;
    }

    if (chp->chan_dev == chsa && (chp->chan_status & STATUS_CEND) != 0 && (flags & SNS_DEVEND) != 0) {
        chp->chan_status |= ((uint16)flags);
    }
    sim_debug(DEBUG_CMD, &cpu_dev, "set_devattn(%04x, %04x) %04x\n", chsa, flags, chp->chan_dev);
    irq_pend = 1;
}

/* channel operation completed */
void chan_end(uint16 chsa, uint16 flags) {
    CHANP   *chp = find_chanp_ptr(chsa);            /* get channel prog pointer */

    sim_debug(DEBUG_XIO, &cpu_dev,
        "chan_end entry chsa %04x flags %04x status %04x cmd %02x\n",
        chsa, flags, chp->chan_status, chp->ccw_cmd);

    chp->chan_byte = BUFF_BUSY;                     /* we are empty & still busy now */
sim_debug(DEBUG_EXP, &cpu_dev, "chan_end BUFF_BUSY chp %p chan_byte %04x\n", chp, chp->chan_byte);
    chp->chan_status |= STATUS_CEND;                /* set channel end */
    chp->chan_status |= ((uint16)flags);            /* add in the callers flags */

    sim_debug(DEBUG_DETAIL, &cpu_dev,
       "chan_end SLI test1 chsa %04x ccw_flags %04x count %04x status %04x\n",
       chsa, chp->ccw_flags, chp->ccw_count, chp->chan_status);

    /* read/write must have none-zero byte count */
    /* all others can be zero, except NOP, which must be 0 */
    /* a NOP is Control command 0x03 with no mdifier bits */
    /* see if this is a read/write cmd */
    if (((chp->ccw_cmd & 0x7) == 0x02) || ((chp->ccw_cmd & 0x7) == 0x01)) {
        /* test for incorrect transfer length */
        if (chp->ccw_count != 0 && ((chp->ccw_flags & FLAG_SLI) == 0)) {
            chp->chan_status |= STATUS_LENGTH;      /* show incorrect length status */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "chan_end setting SLI chsa %04x count %04x ccw_flags %04x status %04x\n",
                chsa, chp->ccw_count, chp->ccw_flags, chp->chan_status);
            chp->ccw_flags = 0;                     /* no flags */
        }
    }

    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "chan_end SLI2 test chsa %04x ccw_flags %04x status %04x\n",
        chsa, chp->ccw_flags, chp->chan_status);

    /* Diags do not want SLI if we have no device end status */
    if ((chp->chan_status & STATUS_LENGTH) && ((chp->chan_status & STATUS_DEND) == 0))
        chp->chan_status &= ~STATUS_LENGTH;

    /* no flags for attention status */
    if (flags & (SNS_ATTN|SNS_UNITCHK|SNS_UNITEXP)) {
        chp->ccw_flags = 0;                         /* no flags */
    }

    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "chan_end test end chsa %04x ccw_flags %04x status %04x\n",
        chsa, chp->ccw_flags, chp->chan_status);

    /* test for device or controller end */
    if (chp->chan_status & (STATUS_DEND|STATUS_CEND)) {
        chp->chan_byte = BUFF_BUSY;                 /* we are empty & still busy now */
sim_debug(DEBUG_EXP, &cpu_dev, "chan_end2 BUFF_BUSY chp %p chan_byte %04x\n", chp, chp->chan_byte);
        while ((chp->ccw_flags & FLAG_DC)) {        /* handle data chaining */
            if (load_ccw(chp, 1))                   /* continue channel program */
                break;                              /* error */
            if ((chp->ccw_flags & FLAG_SLI) == 0) { /* suppress incorrect length? */
                chp->chan_status |= STATUS_LENGTH;  /* no, show incorrect length */
                chp->ccw_flags = 0;                 /* no flags */
            }
        }
        chp->chan_byte = BUFF_BUSY;                 /* we are empty & still busy now */
sim_debug(DEBUG_EXP, &cpu_dev, "chan_end3 BUFF_BUSY chp %p chan_byte %04x\n", chp, chp->chan_byte);
        sim_debug(DEBUG_XIO, &cpu_dev,
            "chan_end FIFO #%1x IOCL done chsa %04x ccw_flags %04x status %04x\n",
            FIFO_Num(chsa), chsa, chp->ccw_flags, chp->chan_status);

        sim_debug(DEBUG_XIO, &cpu_dev,
            "chan_end chan end chsa %04x ccw_flags %04x status %04x\n",
            chsa, chp->ccw_flags, chp->chan_status);

        /* If channel end, check if we should continue */
        if (chp->ccw_flags & FLAG_CC) {             /* command chain flag */
            /* we have channel end and CC flag, continue channel prog */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "chan_end chan end & CC chsa %04x status %04x\n",
                chsa, chp->chan_status);
            if (chp->chan_status & STATUS_DEND) {   /* device end? */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "chan_end dev end & CC chsa %04x status %04x IOCLA %08x\n",
                    chsa, chp->chan_status, chp->chan_caw);
                /* Queue us to continue from cpu level */
                chp->chan_byte = BUFF_NEXT;         /* have main pick us up */
sim_debug(DEBUG_EXP, &cpu_dev, "chan_end4 BUFF_NEXT chp %p chan_byte %04x\n", chp, chp->chan_byte);
                RDYQ_Put(chsa);                     /* queue us up */
            }
            /* just return */
        } else {
            /* we have channel end and no CC flag, end command */
            chsa = chp->chan_dev;                   /* get the chan/sa */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "chan_end chan end & no CC chsa %04x status %04x cmd %02x\n",
                chsa, chp->chan_status, chp->ccw_cmd);

            /* we have completed channel program */
            /* handle case where we are loading the O/S on boot */
            /* if loading, store status to be discovered by scan_chan */
            if (!loading) {
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "chan_end call store_csw dev/chan end chsa %04x cpustat %08x iocla %08x\n",
                    chsa, CPUSTATUS, chp->chan_caw);
            } else {
                /* we are loading, so keep moving */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "chan_end we are loading O/S with DE & CE, keep status chsa %04x status %08x\n",
                    chsa, chp->chan_status);
            }
            /* store the status in channel FIFO to continue from cpu level */
            chp->chan_byte = BUFF_DONE;             /* we are done */
            /* store_csw will change chan_byte to BUFF_POST */
            store_csw(chp);                         /* store the status */
//  sim_debug(DEBUG_EXP, &cpu_dev,
//  "chan_end5 BUFF_POST chp %p chan_byte %04x\n", chp, chp->chan_byte);
            sim_debug(DEBUG_XIO, &cpu_dev,
                "chan_end after store_csw call chsa %04x status %08x chan_byte %02x\n",
                chsa, chp->chan_status, chp->chan_byte);
        }
    }
    /* following statement required for boot to work */
    irq_pend = 1;                                   /* flag to test for int condition */
}

/* post the device status from the channel FIFO into memory */
/* the INCH command provides the status DW address in memory */
/* rstat are the bits to remove from status */
int16 post_csw(CHANP *chp, uint32 rstat)
{
    uint32  chsa = chp->chan_dev;                   /* get ch/sa information */
    uint32  incha = chp->chan_inch_addr;            /* get inch status buffer address */
    uint32  sw1, sw2;                               /* status words */

    irq_pend = 1;                                   /* flag to test for int condition */
    /* check channel FIFO for status to post */
    if ((FIFO_Num(chsa)) &&
        ((FIFO_Get(chsa, &sw1) == 0) && (FIFO_Get(chsa, &sw2) == 0))) {
        uint32  chan_icb = find_int_icb(chsa);      /* get icb address */

        if (chan_icb == 0) {
            sim_debug(DEBUG_EXP, &cpu_dev,
            "post_csw %04x READ FIFO #%1x inch %06x invalid chan_icb %06x\n",
            chsa, FIFO_Num(chsa), incha, chan_icb);
            return 0;                               /* no status to post */
        }
        if (chp->chan_byte != BUFF_POST) {
            sim_debug(DEBUG_EXP, &cpu_dev,
            "post_csw %04x CHP not BUFF_POST status, ERROR FIFO #%1x inch %06x chan_icb %06x\n",
            chsa, FIFO_Num(chsa), incha, chan_icb);
        }
        /* remove user specified bits */
        sw2 &= ~rstat;                              /* remove bits */
        /* we have status to post, do it now */
        /* change status from BUFF_POST to BUFF_DONE */
        chp->chan_byte = BUFF_DONE;                 /* show done & not busy */
//sim_debug(DEBUG_EXP, &cpu_dev,
//"post_csw BUFF_DONE chp %p chan_byte %04x\n", chp, chp->chan_byte);
        /* save the status double word to memory */
        WMW(incha, sw1);                            /* save sa & IOCD address in status WD 1 loc */
        WMW(incha+4, sw2);                          /* save status and residual cnt in status WD 2 loc */
        /* now store the status dw address into word 5 of the ICB for the channel */
        WMW(chan_icb+20, incha|BIT1);               /* post sw addr in ICB+5w & set CC2 in INCH addr */
        sim_debug(DEBUG_IRQ, &cpu_dev,
            "post_csw %04x READ FIFO #%1x inch %06x chan_icb %06x sw1 %08x sw2 %08x\n",
            chsa, FIFO_Num(chsa), incha, chan_icb, sw1, sw2);
        return 1;                                   /* show we posted status */
    }
    // 717 added
    sim_debug(DEBUG_EXP, &cpu_dev,
        "post_csw %04x READ FIFO #%1x inch %06x No Status chan_byte %02x\n",
        chsa, FIFO_Num(chsa), incha, chp->chan_byte);
    return 0;                                       /* no status to post */
}

/* store the device status into the status FIFO for the channel */
void store_csw(CHANP *chp)
{
    uint32  stwd1, stwd2;                           /* words 1&2 of stored status */
    uint32  chsa = chp->chan_dev;                   /* get ch/sa information */

    /* put sub address in byte 0 */
    stwd1 = ((chsa & 0xff) << 24) | chp->chan_caw;  /* subaddress and IOCD address to SW 1 */

    /* save 16 bit channel status and residual byte count in SW 2 */
    stwd2 = ((uint32)chp->chan_status << 16) | ((uint32)chp->ccw_count);

    if ((FIFO_Put(chsa, stwd1) == -1) || (FIFO_Put(chsa, stwd2) == -1)) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "store_csw FIFO Overflow ERROR on chsa %04x\n", chsa);
    }
    sim_debug(DEBUG_XIO, &cpu_dev,
        "store_csw FIFO #%1x write chsa %04x sw1 %08x sw2 %08x incha %08x cmd %02x\n",
        FIFO_Num(chsa), chsa, stwd1, stwd2, chp->chan_inch_addr, chp->ccw_cmd);
    chp->chan_status = 0;                           /* no status anymore */
    chp->ccw_cmd = 0;                               /* no command anymore */
    /* we are done with SIO, but status still needs to be posted */
    /* UTX does not like waiting, so show we are done */
//715chp->chan_byte = BUFF_DONE;                     /* show done with data */
    chp->chan_byte = BUFF_POST;                     /* show done with data */
sim_debug(DEBUG_EXP, &cpu_dev, "store_csw BUFF_POST chp %p chan_byte %04x\n", chp, chp->chan_byte);
    irq_pend = 1;                                   /* wakeup controller */
}

/* SIO CC status returned to caller */
/* val condition */
/* 0   command accepted, will echo status - no CC's */
/* 1   channel busy  - CC4 */
/* 2   channel inop or undefined (operator intervention required) - CC3 */
/* 3   sub channel busy CC3 + CC4 */
/* 4   status stored - CC2 */
/* 5   unsupported transaction  CC2 + CC4 */
/* 6   unassigned CC2 + CC3 */
/* 7   unassigned CC2 + CC3 + CC4 */
/* 8   command accepted/queued, no echo status - CC1 */
/* 9   unassigned */
/* a   unassigned */
/* b   unassigned */
/* c   unassigned */
/* d   unassigned */
/* e   unassigned */
/* f   unassigned */

/* check an XIO operation */
/* chan channel number 0-7f */
/* suba unit address within channel 0-ff */
/* Condition codes to return 0-f as specified above */
t_stat checkxio(uint16 chsa, uint32 *status) {
    DIB     *dibp;                                  /* device information pointer */
    UNIT    *uptr;                                  /* pointer to unit in channel */
    uint32  chan_icb;                               /* Interrupt level context block address */
    CHANP   *chp;                                   /* channel program pointer */
    uint16  chan = get_chan(chsa);                  /* get the logical channel number */
    uint32  inta;
    DEVICE  *dptr;                                  /* DEVICE pointer */

//    sim_debug(DEBUG_XIO, &cpu_dev, "checkxio entry chsa %04x\n", chsa);

    dibp = dib_chan[chan];                          /* get DIB pointer for channel */
    if (dibp == 0) goto nothere;

    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */
    if (chp == 0) goto nothere;

    uptr = find_unit_ptr(chsa);                     /* find pointer to unit on channel */

    if (uptr == 0) {                                /* if no dib or unit ptr, CC3 on return */
nothere:
        *status = CC3BIT;                           /* not found, so CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "checkxio chsa %04x is not found, CC3 return\n", chsa);
        return SCPE_OK;                             /* not found, CC3 */
    }

    /* check for the device being defined and attached in simh */
    if ((uptr->flags & UNIT_ATTABLE) && ((uptr->flags & UNIT_ATT) == 0)) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "checkxio chsa %04x is not attached, CC3 return\n", chsa);
        *status = CC3BIT;                           /* not attached, so error CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }

    inta = find_int_lev(chsa&0x7f00);               /* Interrupt Level for channel */
    chan_icb = find_int_icb(chsa&0x7f00);           /* Interrupt level context block address */
//  sim_debug(DEBUG_XIO, &cpu_dev,
    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "checkxio int spad %08x icb %06x inta %02x chan %04x chan_byte %02x\n",
        SPAD[inta+0x80], chan_icb, inta, chan, chp->chan_byte);

#ifdef FOR_DEBUG_ONLY
    /* get the address of the interrupt IVL in main memory */
    iocla = RMW(chan_icb+16);                       /* iocla is in wd 4 of ICB */
    incha = RMW(chan_icb+20);                       /* status is in wd 5 of ICB chp->chan_inch_addr */

    sim_debug(DEBUG_XIO, &cpu_dev,
        "checkxio busy ck1 chsa %04x cmd %02x ccw_flags %04x IOCD1 %08x IOCD2 %08x IOCLA %06x\n",
        chsa, chp->ccw_cmd, chp->ccw_flags, RMW(iocla), RMW(iocla+4), iocla);

    sim_debug(DEBUG_XIO, &cpu_dev,
        "checkxio busy ck2 chsa %04x ICBincha %08x SW1 %08x SW2 %08x\n",
        chsa, incha, RMW(incha), RMW(incha+4));
#endif

    /* check for a Command or data chain operation in progresss */
//715  if (chp->chan_byte & BUFF_BUSY) {
    if ((chp->chan_byte & BUFF_BUSY) && chp->chan_byte != BUFF_POST) {
        sim_debug(DEBUG_EXP, &cpu_dev,
            "checkxio busy return CC3&CC4 chsa %04x chan %04x cmd %02x flags %04x byte %02x\n",
            chsa, chan, chp->ccw_cmd, chp->ccw_flags, chp->chan_byte);
        *status = CC4BIT|CC3BIT;                    /* busy, so CC3&CC4 */
        return SCPE_OK;                             /* just busy CC3&CC4 */
    }

    dptr = get_dev(uptr);                           /* pointer to DEVICE structure */
    /* try this as MFP says it returns 0 on OK */
    if (dptr->flags & DEV_CHAN)
        *status = 0;                                /* CCs = 0, OK return */
    else
        /* return CC1 for non iop/mfp devices */
        *status = CC1BIT;                           /* CCs = 1, not busy */
//  sim_debug(DEBUG_XIO, &cpu_dev, "checkxio done CC status %08x\n", *status);
    return SCPE_OK;                                 /* No CC's all OK  */
}

/* start an XIO operation */
/* when we get here the cpu has verified that there is a valid channel address */
/* and an interrupt entry in spad for the channel.  The IOCL address in the ICB */
/* has also been verified as present */
/* chan channel number 0-7f */
/* suba unit address within channel 0-ff */
/* Condition codes to return 0-f as specified above */
t_stat startxio(uint16 chsa, uint32 *status) {
    DIB     *dibp;                                  /* device information pointer */
    UNIT    *uptr;                                  /* pointer to unit in channel */
    uint32  chan_icb;                               /* Interrupt level context block address */
    uint32  iocla;                                  /* I/O channel IOCL address int ICB */
    CHANP   *chp;                                   /* channel program pointer */
    uint16  chan = get_chan(chsa);                  /* get the channel number */
    uint32  tempa, inta, incha;
    uint32  word1, word2, cmd;

    sim_debug(DEBUG_XIO, &cpu_dev, "startxio entry chsa %04x\n", chsa);
    dibp = dib_unit[chsa];                          /* get the DIB pointer */
    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */

    if (dibp == 0 || chp == 0) {                    /* if no dib or channel ptr, CC3 return */
        *status = CC3BIT;                           /* not found, so CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "startxio chsa %04x device not present, CC3 returned\n", chsa);
        return SCPE_OK;                             /* not found, CC3 */
    }

    uptr = chp->unitptr;                            /* get the unit ptr */
    if ((uptr->flags & UNIT_ATTABLE) && ((uptr->flags & UNIT_ATT) == 0)) {    /* is unit attached? */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "startxio chsa %04x device not present, CC3 returned flags %08x\n", chsa, uptr->flags);
        *status = CC3BIT;                           /* not attached, so error CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }

    inta = find_int_lev(chsa);                      /* Interrupt Level for channel */
#ifndef FOR_DEBUG
    if ((INTS[inta]&INTS_ACT) || (SPAD[inta+0x80]&SINT_ACT)) { /* look for level active */
        /* just output a warning */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "SIO Busy INTS ACT FIFO #%1x irq %02x SPAD %08x INTS %08x chan_byte %02x\n",
            FIFO_Num(SPAD[inta+0x80] & 0x7f00), inta, SPAD[inta+0x80], INTS[inta], chp->chan_byte);
//      *status = CC4BIT|CC3BIT;                    /* busy, so CC3&CC4 */
//      return SCPE_OK;                             /* just busy CC3&CC4 */
    }
#endif

    /* check for a Command or data chain operation in progresss */
    if (chp->chan_byte & BUFF_BUSY) {
        sim_debug(DEBUG_XIO, &cpu_dev,
            "startxio busy return CC3&CC4 chsa %04x chan %04x\n", chsa, chan);
        *status = CC4BIT|CC3BIT;                    /* busy, so CC3&CC4 */
        return SCPE_OK;                             /* just busy CC3&CC4 */
    }

    chan_icb = find_int_icb(chsa);                  /* Interrupt level context block address */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "startxio int spad %08x icb %06x inta %02x chan %04x\n",
        SPAD[inta+0x80], chan_icb, inta, chan);

    /*  We have to validate all the addresses and parameters for the SIO */
    /* before calling load_ccw which does it again for each IOCL step */
//  inta = find_int_lev(chsa);                      /* Interrupt Level for channel */
//  chan_icb = find_int_icb(chsa);                  /* Interrupt level context block address */

    iocla = RMW(chan_icb+16);                       /* iocla is in wd 4 of ICB */
    word1 = RMW(iocla & MASK24);                    /* get 1st IOCL word */
    incha = word1 & MASK24;                         /* should be inch addr */
    word2 = RMW((iocla + 4) & MASK24);              /* get 2nd IOCL word */
    cmd = (word1 >> 24) & 0xff;                     /* get channel cmd from IOCL */
    chp = find_chanp_ptr(chsa&0x7f00);              /* find the parent chanp pointer */
    if (cmd == 0) {                                 /* INCH command? */
        if ((word2 & MASK16) == 36)                 /* see if disk with 224 wd buffer */
            incha = RMW(incha);                     /* 224 word buffer is inch addr */
        dibp = dib_chan[chan];                      /* get the channel DIB pointer */
        chp = dibp->chan_prg;                       /* get first unit channel prog ptr */
        chp->chan_inch_addr = incha;                /* set the inch addr for channel */
    }

    incha = chp->chan_inch_addr;                    /* get inch address */

    sim_debug(DEBUG_XIO, &cpu_dev,
        "startxio do normal chsa %04x iocla %06x IOCD1 %08x IOCD2 %08x\n",
        chsa, iocla, RMW(iocla), RMW(iocla+4));

    iocla = RMW(chan_icb+16);                       /* iocla is in wd 4 of ICB */
//  incha = chp->chan_inch_addr;                    /* get inch address */
#ifdef LEAVE_IT_ALONE
    /* now store the inch status address into word 5 of the ICB for the channel */
    WMW(chan_icb+20, incha);                        /* post inch addr in ICB+5w */
#endif

    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "startxio test chsa %04x iocla %06x IOCD1 %08x IOCD2 %08x\n",
        chsa, iocla, RMW(iocla), RMW(iocla+4));

    sim_debug(DEBUG_CMD, &cpu_dev, "$$$ SIO %04x %04x cmd %02x ccw_flags %04x\n",
        chsa, chan, chp->ccw_cmd, chp->ccw_flags);

    /* determine if channel DIB has a pre startio command processor */
    if (dibp->pre_io != NULL) {                     /* NULL if no startio function */
        /* call the device controller to get prestart_io status */
        tempa = dibp->pre_io(uptr, chan);           /* get status from device */
        if (tempa != SCPE_OK) {                     /* see if sub channel status is ready */
            /* The device must be busy or something, but it is not ready.  Return busy */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "startxio pre_io call return busy chan %04x cstat %08x\n", chan, tempa);
            *status = CC3BIT|CC4BIT;                /* sub channel busy, so CC3|CC4 */
            return SCPE_OK;                         /* just busy or something, CC3|CC4 */
        }
        sim_debug(DEBUG_XIO, &cpu_dev,
            "startxio pre_io call return not busy chan %04x cstat %08x\n",
            chan, tempa);
    }

    /* channel not busy and ready to go, so start a new command */
    chp->chan_int = inta;                           /* save interrupt level in channel */
    chp->chan_status = 0;                           /* no channel status yet */
    chp->chan_caw = iocla;                          /* get iocla address in memory */

    /* set status words in memory to first IOCD information */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "$$ SIO start IOCL processing chsa %04x iocla %08x incha %08x\n",
        chsa, iocla, incha);

    /* We are queueing the SIO */
    /* Queue us to continue IOCL from cpu level & make busy */
    chp->chan_byte = BUFF_NEXT;                     /* have main pick us up */
sim_debug(DEBUG_EXP, &cpu_dev, "startxio BUFF_NEXT chp %p chan_byte %04x\n", chp, chp->chan_byte);
    RDYQ_Put(chsa);                                 /* queue us up */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "$$$ SIO queued chsa %04x iocla %06x IOCD1 %08x IOCD2 %08x\n",
        chsa, iocla, RMW(iocla), RMW(iocla+4));

    *status = CC1BIT;                               /* CCs = 1, SIO accepted & queued, no echo status */
    sim_debug(DEBUG_XIO, &cpu_dev, "$$$ SIO done chsa %04x status %08x iocla %08x CC's %08x\n",
        chsa, chp->chan_status, iocla, *status);
    return SCPE_OK;                                 /* No CC's all OK  */
}

/* TIO - I/O status */
t_stat testxio(uint16 chsa, uint32 *status) {       /* test XIO */
    DIB     *dibp;                                  /* device information pointer */
    UNIT    *uptr;                                  /* pointer to unit in channel */
    uint32  chan_icb;                               /* Interrupt level context block address */
    uint32  iocla;                                  /* I/O channel IOCL address int ICB */
    CHANP   *chp;                                   /* Channel prog pointers */
    uint16  chan = get_chan(chsa);                  /* get the channel number */
    uint32  inta, incha;

    sim_debug(DEBUG_XIO, &cpu_dev, "testxio entry chsa %04x\n", chsa);
    /* get the device entry for the channel in SPAD */
    dibp = dib_unit[chsa];                          /* get the DIB pointer */
    chp = find_chanp_ptr(chsa);                     /* find the device chanp pointer */

    if (dibp == 0 || chp == 0) {                    /* if no dib or channel ptr, CC3 return */
        *status = CC3BIT;                           /* not found, so CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "testxio chsa %04x device not present, CC3 returned\n", chsa);
        return SCPE_OK;                             /* Not found, CC3 */
    }

    uptr = chp->unitptr;                            /* get the unit ptr */
    if ((uptr->flags & UNIT_ATTABLE) && ((uptr->flags & UNIT_ATT) == 0)) {    /* is unit attached? */
        *status = CC3BIT;                           /* not attached, so error CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "testxio chsa %04x device not attached, CC3 returned\n", chsa);
        return SCPE_OK;                             /* Not found, CC3 */
    }

    /* check for a Command or data chain operation in progresss */
//715if (chp->chan_byte & BUFF_BUSY) {
    if ((chp->chan_byte & BUFF_BUSY) && chp->chan_byte != BUFF_POST) {
        sim_debug(DEBUG_XIO, &cpu_dev,
            "testxio busy return CC4 chsa %04x chan %04x\n", chsa, chan);
        *status = CC4BIT|CC3BIT;                    /* busy, so CC3&CC4 */
        return SCPE_OK;                             /* just busy CC3&CC4 */
    }

    /* the XIO opcode processing software has already checked for F class */
    inta = find_int_lev(chsa);                      /* Interrupt Level for channel */
    chan_icb = find_int_icb(chsa);                  /* Interrupt level context block address */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "testxio int spad %08x icb %06x inta %04x chan %04x\n",
        SPAD[inta+0x80], chan_icb, inta, chan);

    iocla = RMW(chan_icb+16);                       /* iocla is in wd 4 of ICB */
    incha = chp->chan_inch_addr;                    /* get inch address */

    sim_debug(DEBUG_XIO, &cpu_dev,
        "testxio test1 chsa %04x cmd %02x ccw_flags %04x IOCD1 %08x IOCD2 %08x IOCLA %06x\n",
        chsa, chp->ccw_cmd, chp->ccw_flags, RMW(iocla), RMW(iocla+4), iocla);

    sim_debug(DEBUG_XIO, &cpu_dev,
        "testxio test2 chsa %04x ICBincha %08x SW1 %08x SW2 %08x\n",
        chsa, incha, RMW(incha), RMW(incha+4));

    /* the channel is not busy, see if any status to post */
    if (post_csw(chp, 0)) {
        *status = CC2BIT;                           /* status stored from SIO, so CC2 */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "testxio END status stored incha %06x chsa %04x sw1 %08x sw2 %08x\n",
            incha, chsa, RMW(incha), RMW(incha+4));
        INTS[inta] &= ~INTS_REQ;                    /* clear any level request */
        return SCPE_OK;                             /* CC2 and OK */
    }
    /* nothing going on, so say all OK */
    *status = CC1BIT;                               /* request accepted, no status, so CC1 */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "$$$ TIO END chsa %04x chan %04x cmd %02x ccw_flags %04x chan_stat %04x CCs %08x\n",
        chsa, chan, chp->ccw_cmd, chp->ccw_flags, chp->chan_status, *status);
    return SCPE_OK;                                 /* No CC's all OK  */
}

/* Stop XIO */
t_stat stopxio(uint16 chsa, uint32 *status) {       /* stop XIO */
    DIB     *dibp;                                  /* device information pointer */
    UNIT    *uptr;                                  /* pointer to unit in channel */
    uint32  chan_icb;                               /* Interrupt level context block address */
    uint32  iocla;                                  /* I/O channel IOCL address int ICB */
    CHANP   *chp;                                   /* Channel prog pointers */
    uint16  chan = get_chan(chsa);                  /* get the channel number */

    sim_debug(DEBUG_XIO, &cpu_dev, "stopxio entry chsa %04x\n", chsa);
    /* get the device entry for the logical channel in SPAD */
    dibp = dib_unit[chsa];                          /* get the DIB pointer */
    chp = find_chanp_ptr(chsa);                     /* find the device chanp pointer */

    if (dibp == 0 || chp == 0) {                    /* if no dib or channel ptr, CC3 return */
        *status = CC3BIT;                           /* not found, so CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "stopxio test 1 chsa %04x device not present, CC3 returned\n", chsa);
        return SCPE_OK;                             /* not found CC3 */
    }

    uptr = chp->unitptr;                            /* get the unit ptr */
    if ((uptr->flags & UNIT_ATTABLE) && ((uptr->flags & UNIT_ATT) == 0)) {    /* is unit attached? */
        *status = CC3BIT;                           /* not attached, so error CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "stopxio test 2 chsa %04x device not present, CC3 returned\n", chsa);
        return SCPE_OK;                             /* not found CC3 */
    }

    chan_icb = find_int_icb(chsa);                  /* Interrupt level context block address */
    iocla = RMW(chan_icb+16);                       /* iocla is in wd 4 of ICB */
    sim_debug(DEBUG_CMD, &cpu_dev,
        "stopxio busy test chsa %04x cmd %02x ccw_flags %04x IOCD1 %08x IOCD2 %08x\n",
        chsa, chp->ccw_cmd, chp->ccw_flags, M[iocla>>2], M[(iocla+4)>>2]);

    sim_debug(DEBUG_CMD, &cpu_dev, "$$$ STOPIO %04x %02x %04x\n",
        chsa, chp->ccw_cmd, chp->ccw_flags);

    /* check for a Command or data chain operation in progresss */
    if (chp->chan_byte & BUFF_BUSY) {
        sim_debug(DEBUG_CMD, &cpu_dev, "stopxio busy return CC1 chsa %04x chan %04x\n", chsa, chan);
        /* reset the DC or CC bits to force completion after current IOCD */
        chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);       /* reset chaining bits */
        *status = CC1BIT;                           /* request accepted, no status, so CC1 */
        return SCPE_OK;                             /* go wait CC1 */
    }
    /* the channel is not busy, so return OK */
    *status = CC1BIT;                               /* request accepted, no status, so CC1 */
    sim_debug(DEBUG_CMD, &cpu_dev,
        "$$$ STOPIO good return chsa %04x chan %04x cmd %02x ccw_flags %04x status %04x\n",
        chsa, chan, chp->ccw_cmd, chp->ccw_flags, *status);
    return SCPE_OK;                                 /* No CC's all OK  */
}

/* Reset Channel XIO */
t_stat rschnlxio(uint16 chsa, uint32 *status) {     /* reset channel XIO */
    DIB     *dibp;                                  /* device information pointer */
    UNIT    *uptr;                                  /* pointer to unit in channel */
    CHANP   *chp;                                   /* Channel prog pointers */
    uint16  chan = chsa & 0x7f00;                   /* get just the channel number */
    uint32  lev;
    int     i;

    sim_debug(DEBUG_XIO, &cpu_dev, "rschnlxio entry chan %04x\n", chan);
    /* get the device entry for the logical channel in SPAD */
    dibp = dib_chan[get_chan(chan)];                /* get the channel device information pointer */
    chp = find_chanp_ptr(chan);                     /* find the channel chanp pointer */

    if (dibp == 0 || chp == 0) {                    /* if no dib or channel ptr, CC3 return */
        *status = CC3BIT;                           /* not found, so CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "rschnlxio test 1 chsa %04x device not present, CC3 returned\n", chsa);
        return SCPE_OK;                             /* not found CC3 */
    }

    uptr = chp->unitptr;                            /* get the unit ptr */
    if ((uptr->flags & UNIT_ATTABLE) && ((uptr->flags & UNIT_ATT) == 0)) {    /* is unit attached? */
        *status = CC3BIT;                           /* not attached, so error CC3 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "rschnlxio test 2 chsa %04x device not present, CC3 returned\n", chsa);
        return SCPE_OK;                             /* not found CC3 */
    }

    /* reset this channel */
    dibp->chan_fifo_in = 0;                         /* reset the FIFO pointers */
    dibp->chan_fifo_out = 0;                        /* reset the FIFO pointers */
    chp->chan_inch_addr = 0;                        /* remove inch status buffer address */
    lev = find_int_lev(chan);                       /* Interrupt Level for channel */
    INTS[lev] &= ~INTS_ACT;                         /* clear level active */
    SPAD[lev+0x80] &= ~SINT_ACT;                    /* clear in spad too */

    /* now go through all the sa for the channel and stop any IOCLs */
    for (i=0; i<256; i++) {
        chsa = chan | i;                            /* merge sa to real channel */
        dibp = dib_unit[chsa];                      /* get the DIB pointer */
        if (dibp == 0)
            continue;                               /* not used */
        chp = find_chanp_ptr(chsa);                 /* find the chanp pointer */
        if (chp == 0)
            continue;                               /* not used */
        chp->chan_status = 0;                       /* clear the channel status */
        chp->chan_byte = BUFF_EMPTY;                /* no data yet */
//sim_debug(DEBUG_EXP, &cpu_dev,
//"rschnlxio BUFF_EMPTY chp %p chan_byte %04x\n", chp, chp->chan_byte);
        chp->ccw_addr = 0;                          /* clear buffer address */
        chp->chan_caw = 0x0;                        /* clear IOCD address */
        chp->ccw_count = 0;                         /* channel byte count 0 bytes*/
        chp->ccw_flags = 0;                         /* clear flags */
        chp->ccw_cmd = 0;                           /* read command */
        chp->chan_inch_addr = 0;                    /* clear inch addr */
    }
    sim_debug(DEBUG_XIO, &cpu_dev, "rschnlxio return CC1 chan %04x lev %04x\n", chan, lev);
    *status = CC1BIT;                               /* request accepted, no status, so CC1 TRY THIS */
    return SCPE_OK;                                 /* All OK */
}

/* HIO - Halt I/O */
t_stat haltxio(uint16 lchsa, uint32 *status) {       /* halt XIO */
    int     chan = get_chan(lchsa);
    DIB     *dibp;
    UNIT    *uptr;
    uint32  chan_ivl;                               /* Interrupt Level ICB address for channel */
    uint32  chan_icb;                               /* Interrupt level context block address */
    uint32  iocla;                                  /* I/O channel IOCL address int ICB */
    uint32  inta, spadent, tempa;
    uint16  chsa;
    CHANP   *chp;                                   /* Channel prog pointers */

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[chan];                           /* get spad device entry for logical channel */
    chan = (spadent & 0x7f00) >> 8;                 /* get real channel */
    chsa = (chan << 8) | (lchsa & 0xff);            /* merge sa to real channel */
    dibp = dib_unit[chsa];                          /* get the device DIB pointer */
    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */
    uptr = chp->unitptr;                            /* get the unit ptr */

    sim_debug(DEBUG_XIO, &cpu_dev, "haltxio 1 chsa %04x chan %04x\n", chsa, chan);
    if (dibp == 0 || uptr == 0) {                   /* if no dib or unit ptr, CC3 on return */
        *status = CC3BIT;                           /* not found, so CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }
    sim_debug(DEBUG_XIO, &cpu_dev, "haltxio 2 chsa %04x chan %04x\n", chsa, chan);
    if ((uptr->flags & UNIT_ATTABLE) && ((uptr->flags & UNIT_ATT) == 0)) {    /* is unit attached? */
        *status = CC3BIT;                           /* not attached, so error CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }
    /* see if interrupt is setup in SPAD and determine IVL for channel */
    sim_debug(DEBUG_XIO, &cpu_dev, "haltxio dev spad %08x chsa %04x chan %04x\n", spadent, chsa, chan);

    /* the startio opcode processing software has already checked for F class */
    inta = ((spadent & 0x007f0000) >> 16);          /* 1's complement of chan int level */
    inta = 127 - inta;                              /* get positive int level */
    spadent = SPAD[inta + 0x80];                    /* get interrupt spad entry */
    sim_debug(DEBUG_XIO, &cpu_dev, "haltxio int spad %08x inta %02x chan %04x\n", spadent, inta, chan);

    /* get the address of the interrupt IVL in main memory */
    chan_ivl = SPAD[0xf1] + (inta<<2);              /* contents of spad f1 points to chan ivl in mem */
    chan_icb = M[chan_ivl >> 2];                    /* get the interrupt context block addr in memory */
    iocla = M[(chan_icb+16)>>2];                    /* iocla is in wd 4 of ICB */
    sim_debug(DEBUG_XIO, &cpu_dev,
        "haltxio busy test chsa %04x chan %04x cmd %02x ccw_flags %04x IOCD1 %08x IOCD2 %08x\n",
        chsa, chan, chp->ccw_cmd, chp->ccw_flags, M[iocla>>2], M[(iocla+4)>>2]);

    sim_debug(DEBUG_XIO, &cpu_dev, "$$$ HIO chsa %04x chan %04x cmd %02x ccw_flags %04x\n",
        chsa, chan, chp->ccw_cmd, chp->ccw_flags);

    if ((chp->chan_byte & BUFF_BUSY) == 0) {
        /* the channel is not busy, so return OK */

        /* diag wants an interrupt for a non busy HIO ??? */
        sim_debug(DEBUG_XIO, &cpu_dev,
            "$$$ HIO DIAG chsa %04x chan %04x cmd %02x ccw_flags %04x status %04x\n",
            chsa, chan, chp->ccw_cmd, chp->ccw_flags, *status);
        irq_pend = 1;                               /* still pending int */
        *status = CC1BIT;                           /* request accepted, no status, so CC1 */
        goto hiogret;                               /* CC1 and OK */
    }

    /* the channel is busy, so process */
    /* see if we have a haltio device entry */
    if (dibp->halt_io != NULL) {                    /* NULL if no haltio function */

        /* call the device controller to get halt_io status */
        tempa = dibp->halt_io(uptr);                /* get status from device */

        /* test for SCPE_IOERR */
        if (tempa != 0) {                           /* sub channel has status ready */
            /* The device I/O has been terminated and status stored. */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "haltxio halt_io call return ERROR FIFO #%1x chan %04x retstat %08x cstat %08x\n",
                FIFO_Num(chsa), chan, tempa, chp->chan_status);

            /* chan_end called in hio device service routine */
            /* the device is no longer busy, post status */
            /* remove SLI, PCI and Unit check status bits */
            if (post_csw(chp, ((STATUS_LENGTH|STATUS_PCI|STATUS_EXPT) << 16))) {
/// UTX     if (post_csw(chp, ((STATUS_LENGTH|STATUS_PCI|STATUS_CHECK) << 16))) {
                /* TODO clear SLI bit in status */
//              chp->chan_status &= ~STATUS_LENGTH; /* remove SLI status bit */
//              chp->chan_status &= ~STATUS_PCI;    /* remove PCI status bit */
                INTS[inta] &= ~INTS_REQ;            /* clear any level request */
                *status = CC2BIT;                   /* status stored */
                goto hioret;                        /* CC2 and OK */
            }
        }
        /* the device is not busy, so cmd has not started */
    }
    /* device does not have a HIO entry, so terminate the I/O */
    /* check for a Command or data chain operation in progresss */
    if (chp->chan_byte & BUFF_BUSY) {
        sim_debug(DEBUG_XIO, &cpu_dev, "haltxio busy return CC4 chsa %04x chan %04x\n", chsa, chan);

        /* reset the DC or CC bits to force completion after current IOCD */
        chp->chan_status |= STATUS_ECHO;            /* show we stopped the cmd */
        chp->chan_status &= ~STATUS_LENGTH;         /* remove SLI status bit */
        chp->chan_status &= ~STATUS_PCI;            /* remove PCI status bit */
        chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);       /* reset chaining bits */
        chp->chan_byte = BUFF_BUSY;                 /* wait for post_csw to be done */
sim_debug(DEBUG_EXP, &cpu_dev, "haltxio BUFF_BUSY chp %p chan_byte %04x\n", chp, chp->chan_byte);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);  /* show I/O complete */

        /* post the channel status */
        /* remove SLI, PCI and Unit check status bits */
        if (post_csw(chp, ((STATUS_LENGTH|STATUS_PCI|STATUS_EXPT) << 16))) {
            /* TODO clear SLI bit in status */
            INTS[inta] &= ~INTS_REQ;                /* clear any level request */
            *status = CC2BIT;                       /* status stored from SIO, so CC2 */
            goto hioret;                            /* just return */
        }
    }
hiogret:
    chp->chan_byte = BUFF_DONE;                     /* chan prog done */
sim_debug(DEBUG_EXP, &cpu_dev, "haltxioret BUFF_DONE chp %p chan_byte %04x\n", chp, chp->chan_byte);
    /* the channel is not busy, so return OK */
    *status = CC1BIT;                               /* request accepted, no status, so CC1 */
hioret:
    sim_debug(DEBUG_CMD, &cpu_dev, "$$$ HIO END chsa %04x chan %04x cmd %02x ccw_flags %04x status %04x\n",
        chsa, chan, chp->ccw_cmd, chp->ccw_flags, *status);
    return SCPE_OK;                                 /* No CC's all OK  */
}

/* grab controller n/u */
/* TODO return unimplemented function error, not busy */
t_stat grabxio(uint16 lchsa, uint32 *status) {        /* grab controller XIO n/u */
    int     chan = get_chan(lchsa);
    uint32  spadent;
    uint16  chsa;
    CHANP   *chp;

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[chan];                           /* get spad device entry for logical channel */
    chan = (spadent & 0x7f00) >> 8;                 /* get real channel */
    chsa = (chan << 8) | (lchsa & 0xff);            /* merge sa to real channel */
    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */

    /* check for a Command or data chain operation in progresss */
    if (chp->ccw_cmd != 0 || (chp->ccw_flags & (FLAG_DC|FLAG_CC)) != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "grabxio busy return CC4 chsa %04x chan %04x\n", chsa, chan);
        *status = CC4BIT;                           /* busy, so CC4 */
        return SCPE_OK;                             /* CC4 all OK  */
    }
    *status = 0;                                    /* not busy, no CC */
    sim_debug(DEBUG_CMD, &cpu_dev, "grabxio chsa %04x chan %04x\n", chsa, chan);
    return 0;
}

/* reset controller XIO */
t_stat rsctlxio(uint16 lchsa, uint32 *status) {       /* reset controller XIO */
    DIB     *dibp;
    UNIT    *uptr;
    uint32  spadent;
    uint16  chsa;
    CHANP   *chp;
    int     lev, i;
    uint32  chan = get_chan(lchsa);

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[chan];                           /* get spad device entry for logical channel */
    chan = spadent & 0x7f00;                        /* get real channel */
    chsa = chan;                                    /* use just channel */
    dibp = dib_unit[chsa];                          /* get the DIB pointer */
    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */
    uptr = chp->unitptr;                            /* get the unit ptr */

    sim_debug(DEBUG_CMD, &cpu_dev, "rsctlxio 1 chan %04x SPAD %08x\n", chsa, spadent);
    if (dibp == 0 || uptr == 0) {                   /* if no dib or unit ptr, CC3 on return */
        *status = CC3BIT;                           /* not found, so CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }
    sim_debug(DEBUG_CMD, &cpu_dev, "rsctlxio 2 chan %04x, spad %08x\r\n", chsa, spadent);
    if ((uptr->flags & UNIT_ATTABLE) && ((uptr->flags & UNIT_ATT) == 0)) {    /* is unit attached? */
        *status = CC3BIT;                           /* not attached, so error CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }
    /* reset the FIFO pointers */
    dibp->chan_fifo_in = 0;
    dibp->chan_fifo_out = 0;
    chp->chan_inch_addr = 0;                        /* remove inch status buffer address */
    lev = find_int_lev(chan);                       /* get our int level */
    INTS[lev] &= ~INTS_ACT;                         /* clear level active */
    SPAD[lev+0x80] &= ~SINT_ACT;                    /* clear spad too */
    INTS[lev] &= ~INTS_REQ;                         /* clear level request */

    /* now go through all the sa for the channel and stop any IOCLs */
    for (i=0; i<256; i++) {
        chsa = chan | i;                            /* merge sa to real channel */
        dibp = dib_unit[chsa];                      /* get the DIB pointer */
        if (dibp == 0) {
            continue;                               /* not used */
        }
        chp = find_chanp_ptr(chsa);                 /* find the chanp pointer */
        if (chp == 0) {
            continue;                               /* not used */
        }
        chp->chan_status = 0;                       /* clear the channel status */
        chp->chan_byte = BUFF_EMPTY;                /* no data yet */
sim_debug(DEBUG_EXP, &cpu_dev, "rsctlxio BUFF_EMPTY chp %p chan_byte %04x\n", chp, chp->chan_byte);
        chp->ccw_addr = 0;                          /* clear buffer address */
        chp->chan_caw = 0x0;                        /* clear IOCD address */
        chp->ccw_count = 0;                         /* channel byte count 0 bytes*/
        chp->ccw_flags = 0;                         /* clear flags */
        chp->ccw_cmd = 0;                           /* read command */
    }
    sim_debug(DEBUG_CMD, &cpu_dev, "rsctlxio return CC1 chan %04x lev %04x\n", chan, lev);
    *status = CC1BIT;                               /* request accepted, no status, so CC1 TRY THIS */
    return SCPE_OK;                                 /* All OK */
}

/* boot from the device (ch/sa) the caller specified */
/* on CPU reset, the cpu has set the IOCD data at location 0-4 */
t_stat chan_boot(uint16 chsa, DEVICE *dptr) {
    int     chan = get_chan(chsa);
    DIB     *dibp = (DIB *)dptr->ctxt;              /* get pointer to DIB for this device */
    CHANP   *chp = 0;

    sim_debug(DEBUG_EXP, &cpu_dev, "Channel Boot chan/device addr %04x\n", chsa);
    if (dibp == 0)                                  /* if no channel or device, error */
        return SCPE_IOERR;                          /* error */
    if (dibp->chan_prg == NULL)                     /* must have channel information for each device */
        return SCPE_IOERR;                          /* error */
    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */
    if (chp == 0)                                   /* if no channel, error */
        return SCPE_IOERR;                          /* error */

    /* make sure there is an IOP/MFP configured at 7e00 on system */
    if (dib_chan[0x7e] == NULL) {
        sim_debug(DEBUG_CMD, dptr,
            "ERROR===ERROR\nIOP/MFP device 0x7e00 not configured on system, aborting\n");
        printf("ERROR===ERROR\nIOP/MFP device 0x7e00 not configured on system, aborting\n");
        return SCPE_UNATT;                          /* error */
    }

    /* make sure there is an IOP/MFP console configured at 7efc/7efd on system */
    if ((dib_unit[0x7efc] == NULL) || (dib_unit[0x7efd] == NULL)) {
        sim_debug(DEBUG_CMD, dptr,
            "ERROR===ERROR\nCON device 0x7efc/0x7ecd not configured on system, aborting\n");
        printf("ERROR===ERROR\nCON device 0x7efc/0x7efd not configured on system, aborting\n");
        return SCPE_UNATT;                          /* error */
    }

    chp->chan_status = 0;                           /* clear the channel status */
    chp->chan_dev = chsa;                           /* save our address (ch/sa) */
    chp->chan_byte = BUFF_EMPTY;                    /* no data yet */
sim_debug(DEBUG_EXP, &cpu_dev, "chan_boot BUFF_EMPTY chp %p chan_byte %04x\n", chp, chp->chan_byte);
    chp->ccw_addr = 0;                              /* start loading at loc 0 */
    chp->chan_caw = 0x0;                            /* set IOCD address to memory location 0 */
    chp->ccw_count = 0;                             /* channel byte count 0 bytes*/
    chp->ccw_flags = 0;                             /* Command chain and supress incorrect length */
    chp->ccw_cmd = 0;                               /* read command */
    loading = chsa;                                 /* show we are loading from the boot device */

    sim_debug(DEBUG_CMD, &cpu_dev, "Channel Boot calling load_ccw chan %04x status %08x\n",
        chan, chp->chan_status);

    /* start processing the boot IOCL at loc 0 */
    if (load_ccw(chp, 0)) {                         /* load IOCL starting from location 0 */
        sim_debug(DEBUG_XIO, &cpu_dev, "Channel Boot Error return from load_ccw chan %04x status %08x\n",
            chan, chp->chan_status);
        chp->ccw_flags = 0;                         /* clear the command flags */
        chp->chan_byte = BUFF_DONE;                 /* done with errors */
sim_debug(DEBUG_EXP, &cpu_dev, "chan_boot BUFF_DONE chp %p chan_byte %04x\n", chp, chp->chan_byte);
        loading = 0;                                /* show we are done loading from the boot device */
        return SCPE_IOERR;                          /* return error */
    }
    sim_debug(DEBUG_XIO, &cpu_dev, "Channel Boot OK return from load_ccw chsa %04x status %04x\n",
        chsa, chp->chan_status);
    return SCPE_OK;                                 /* all OK */
}

/* Continue a channel program for a device */
uint32 cont_chan(uint16 chsa)
{
    int32   stat;                                   /* return status 0/1 from loadccw */
    CHANP   *chp = find_chanp_ptr(chsa);            /* channel program */

        sim_debug(DEBUG_EXP, &cpu_dev,
            "cont_chan entry chp %p chan_byte %02x chsa %04x addr %06x\n",
            chp, chp->chan_byte, chsa, chp->ccw_addr);
    /* we have entries, continue channel program */
    if (chp->chan_byte != BUFF_NEXT) {
        /* channel program terminated already, ignore entry */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "cont_chan chan_byte %02x is NOT BUFF_NEXT chsa %04x addr %06x\n",
            chp->chan_byte, chsa, chp->ccw_addr);
        return 1;
//chp->chan_byte = BUFF_NEXT;
//716   return 1;
    }
    if (chp->chan_byte == BUFF_NEXT) {
        uint32  chan = get_chan(chsa);
        sim_debug(DEBUG_EXP, &cpu_dev,
            "cont_chan resume chan prog chsa %04x iocl %06x\n",
            chsa, chp->chan_caw);
        stat = load_ccw(chp, 1);                    /* resume the channel program */
        if (stat || (chp->chan_status & STATUS_PCI)) {
            /* we have an error or user requested interrupt, return status */
            sim_debug(DEBUG_EXP, &cpu_dev, "cont_chan error, store csw chsa %04x status %08x\n",
                chsa, chp->chan_status);
            /* DIAG's want CC1 with memory access error */
            if (chp->chan_status & STATUS_PCHK) {
                chp->chan_status &= ~STATUS_LENGTH; /* clear incorrect length */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* show I/O complete */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "cont_chan Error1 FIFO #%1x store_csw CC1 chan %04x status %08x\n",
            FIFO_Num(chsa), chan, chp->chan_status);
                return SCPE_OK;                     /* done */
            }
            /* other error, stop the show */
            chp->chan_status &= ~STATUS_PCI;        /* remove PCI status bit */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* show I/O complete */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "cont_chan Error2 FIFO #%1x store_csw CC1 chan %04x status %08x\n",
            FIFO_Num(chsa), chan, chp->chan_status);
            return SCPE_OK;                         /* done */
        }
        /* we get here when nothing left to do and status is stored */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "cont_chan continue wait chsa %04x status %08x iocla %06x\n",
            chsa, chp->chan_status, chp->chan_caw);
        return SCPE_OK;                             /* done, status stored */
    }
    /* must be more IOCBs, wait for them */
    return SCPE_OK;
}

/* Scan all channels and see if one is ready to start or has
   interrupt pending. Return icb address and interrupt level
*/
uint32 scan_chan(uint32 *ilev) {
    int         i;
#ifndef GEERT_UNDO
    void        CatchSig();
    void        ReportIRQs();
#endif
    uint32      chsa;                               /* No device */
    uint32      chan;                               /* channel num 0-7f */
    uint32      tempa;                              /* icb address */
    uint32      chan_ivl;                           /* int level table address */
    uint32      chan_icba;                          /* Interrupt level context block address */
    CHANP       *chp;                               /* channel prog pointer */
    DIB         *dibp;                              /* DIB pointer */
    uint32      sw1, sw2;                           /* status words */

    /* see if we are loading */
    if (loading) {
        /* we are loading see if chan prog complete */
        /* get the device entry for the logical channel in SPAD */
        chan = loading & 0x7f00;                    /* get real channel and zero sa */
        dibp = dib_unit[chan];                      /* get the IOP/MFP DIB pointer */
        if (dibp == 0)
             return 0;                              /* skip unconfigured channel */
        /* see if status is stored in FIFO */
        /* see if the FIFO is empty */
        if ((FIFO_Num(chan)) && ((FIFO_Get(chan, &sw1) == 0) &&
            (FIFO_Get(chan, &sw2) == 0))) {
            /* the SPAD entries are not set up, so no access to icb or ints */
#if NOT_SETUP_YET
            uint32  chan_icb = find_int_icb(chan);  /* get icb address */
#endif
            /* get the status from the FIFO and throw it away */
            /* we really should post it to the current inch address */
            /* there is really no need to post, but it might be helpfull */
            chp = find_chanp_ptr(chan);             /* find the chanp pointer for channel */
            /* this address most likely will be zero here */
            tempa = chp->chan_inch_addr;            /* get inch status buffer address */
#if 1
            /* before overwriting memory loc 0+4, save PSD for caller in TPSD[] locations */
            TPSD[0] = M[0];                         /* save PSD from loc 0&4 */
            TPSD[1] = M[1];
            /* save the status double word to memory */
            WMW(tempa, sw1);                        /* save sa & IOCD address in status WD 1 loc */
            WMW(tempa+4, sw2);                      /* save status and residual cnt in status WD 2 loc */
            /* now store the status dw address into word 5 of the ICB for the channel */
            /* there is no icb when we are ipl booting, so skip this */
//NO_GOOD   WMW(chan_icb+20, tempa|BIT1);           /* post sw addr in ICB+5w & set CC2 in INCH addr */
#endif
            chp->chan_byte = BUFF_DONE;             /* we are done */
sim_debug(DEBUG_EXP, &cpu_dev, "scan_chan BUFF_DONE chp %p chan_byte %04x\n", chp, chp->chan_byte);
            sim_debug(DEBUG_IRQ, &cpu_dev,
            "LOADING %06x %04x FIFO #%1x read inch %06x sw1 %08x sw2 %08x\n",
            chp->chan_caw, chan, FIFO_Num(chan), tempa, sw1, sw2);
            return loading;
        }
        return 0;                                   /* not ready, return */
    }

#ifndef GEERT_UNDO
    ScanCycles++;

    /* install signal handler after say 100 ScanCycles */
    if (ScanCycles == 100)
        signal(SIGQUIT, CatchSig);

    if (DoNextCycles) {
        ReportIRQs();
        DoNextCycles--;
    }
#endif
    /* see if we are able to look for ints */
    if (irq_pend == 0)                              /* pending int? */
        return 0;                                   /* no, done */

    /* ints not blocked, so look for highest requesting interrupt */
    for (i=0; i<112; i++) {
        if (SPAD[i+0x80] == 0)                      /* not initialize? */
            continue;                               /* skip this one */
        if (SPAD[i+0x80] == 0xffffffff)             /* not initialize? */
            continue;                               /* skip this one */
        /* this is a bug fix for MPX 1.x restart command */
//      if (SPAD[i+0x80] == 0xefffffff)             /* not initialize? */
//          continue;                               /* skip this one */
        if ((INTS[i] & INTS_ENAB) == 0)             /* ints must be enabled */
            continue;                               /* skip this one */

        if (INTS[i] & INTS_REQ)                     /* if already requesting, skip */
            continue;                               /* skip this one */

        /* see if there is pending status for this channel */
        /* if there is and the level is not requesting, do it */
        /* get the device entry for the logical channel in SPAD */
        chan = (SPAD[i+0x80] & 0x7f00);             /* get real channel and zero sa */
        dibp = dib_chan[get_chan(chan)];            /* get the channel device information pointer */
        if (dibp == 0)                              /* we have a channel to check */
            continue;                               /* not defined, skip this one */

        /* we have a channel to check */
        /* check for pending status */
        if (FIFO_Num(chan)) {
            INTS[i] |= INTS_REQ;                    /* turn on channel interrupt request */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "scan_chan FIFO REQ FIFO #%1x irq %02x SPAD %08x INTS %08x\n",
                FIFO_Num(SPAD[i+0x80] & 0x7f00), i, SPAD[i+0x80], INTS[i]);
            continue;
        }
    }

    /* cannot make anyone active if ints are blocked */
    if (CPUSTATUS & 0x80) {                         /* interrupts blocked? */
        sim_debug(DEBUG_IRQ, &cpu_dev, "scan_chan INTS blocked!\n");
        goto tryme;                                 /* needed for MPX */
//718   return 0;                                   /* yes, done */
    }

    /* now go process the highest requesting interrupt */
    for (i=0; i<112; i++) {
        if (SPAD[i+0x80] == 0)                      /* not initialize? */
            continue;                               /* skip this one */
        if (SPAD[i+0x80] == 0xffffffff)             /* not initialize? */
            continue;                               /* skip this one */
        /* this is a bug fix for MPX 1.x restart command */
        if (SPAD[i+0x80] == 0xefffffff)             /* not initialize? */
            continue;                               /* skip this one */
        if ((INTS[i]&INTS_ACT) || (SPAD[i+0x80]&SINT_ACT)) { /* look for level active */
//          sim_debug(DEBUG_DETAIL, &cpu_dev,
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "scan_chan INTS ACT irq %02x SPAD %08x INTS %08x\n",
                i, SPAD[i+0x80], INTS[i]);
            return 0;                               /* this level active, so stop looking */
        }

        if ((INTS[i] & INTS_ENAB) == 0) {           /* ints must be enabled */
            continue;                               /* skip this one */
        }

        /* look for the highest requesting interrupt */
        /* that is enabled */
        if (((INTS[i] & INTS_ENAB) && (INTS[i] & INTS_REQ)) ||
            ((SPAD[i+0x80] & SINT_ENAB) && (INTS[i] & INTS_REQ))) {

//          sim_debug(DEBUG_DETAIL, &cpu_dev,
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "scan_chan highest int req irq %02x SPAD %08x INTS %08x\n",
                i, SPAD[i+0x80], INTS[i]);

            /* requesting, make active and turn off request flag */
            INTS[i] &= ~INTS_REQ;                   /* turn off request */
            INTS[i] |= INTS_ACT;                    /* turn on active */
            SPAD[i+0x80] |= SINT_ACT;               /* show active in SPAD too */

            /* get the address of the interrupt IVL table in main memory */
            chan_ivl = SPAD[0xf1] + (i<<2);         /* contents of spad f1 points to chan ivl in mem */
            chan_icba = RMW(chan_ivl);              /* get the interrupt context block addr in memory */

            /* see if there is pending status for this channel */
            /* get the device entry for the logical channel in SPAD */
            chan = (SPAD[i+0x80] & 0x7f00);         /* get real channel and zero sa */
            dibp = dib_chan[get_chan(chan)];        /* get the channel device information pointer */
            if (dibp == 0) {                        /* see if we have a channel to check */
                /* not a channel, must be clk or ext int */
                *ilev = i;                          /* return interrupt level */
                irq_pend = 0;                       /* not pending anymore */
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "scan_chan %04x POST NON FIFO irq %02x chan_icba %06x SPAD[%02x] %08x\n",
                    chan, i, chan_icba, i+0x80, SPAD[i+0x80]);
                return(chan_icba);                  /* return ICB address */
            }
            /* must be a device, get status ready to post */
            if (FIFO_Num(chan)) {
                /* new 051020 find actual device with the channel program */
                /* not the channel, that is not correct most of the time */
                tempa = dibp->chan_fifo[dibp->chan_fifo_out];   /* get SW1 of FIFO entry */
                chsa = chan | (tempa >> 24);        /* find device address for requesting chan prog */
                chp = find_chanp_ptr(chsa);         /* find the chanp pointer for channel */
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "scan_chan %04x LOOK FIFO #%1x irq %02x inch %06x chsa %04x chan_icba %06x chan_byte %02x\n",
                    chan, FIFO_Num(chan), i, chp->chan_inch_addr, chsa, chan_icba, chp->chan_byte);
                if (post_csw(chp, 0)) {
                    sim_debug(DEBUG_IRQ, &cpu_dev,
                        "scan_chanx %04x POST FIFO #%1x irq %02x inch %06x chan_icba %06x chan_byte %02x\n",
                        chan, FIFO_Num(chan), i, chp->chan_inch_addr, chan_icba, chp->chan_byte);
                } else {
                    sim_debug(DEBUG_IRQ, &cpu_dev,
                        "scan_chanx %04x NOT POSTED FIFO #%1x irq %02x inch %06x chan_icba %06x chan_byte %02x\n",
                        chan, FIFO_Num(chan), i, chp->chan_inch_addr, chan_icba, chp->chan_byte);
                }
                *ilev = i;                          /* return interrupt level */
                irq_pend = 0;                       /* not pending anymore */
                return(chan_icba);                  /* return ICB address */
            }
        }
    }
tryme:
    irq_pend = 0;                                   /* not pending anymore */
#ifndef TEST_071820
    if (RDYQ_Num()) {
        /* we have entries, continue channel program */
        if (RDYQ_Get(&chsa) == SCPE_OK) {           /* get chsa for program */
            int32 stat;
            sim_debug(DEBUG_XIO, &cpu_dev,
                "scan_chan CPU RDYQ entry for chsa %04x starting\n", chsa);
            stat = cont_chan(chsa);                 /* resume the channel program */
            if (stat)
                sim_debug(DEBUG_XIO, &cpu_dev,
                     "CPU RDYQ entry for chsa %04x processed\n", chsa);
        }
    }
#endif
    return 0;                                       /* done */
}

/* part of find_dev_from_unit(UNIT *uptr) in scp.c */
/* Find_dev pointer for a unit
   Input:  uptr = pointer to unit
   Output: dptr = pointer to device
*/
DEVICE *get_dev(UNIT *uptr)
{
    DEVICE *dptr = NULL;
    uint32 i, j;

    if (uptr == NULL)                               /* must be valid unit */
        return NULL;
    if (uptr->dptr)                                 /* get device pointer from unit */
        return uptr->dptr;                          /* return valid pointer */

    /* the device pointer in the unit is not set up, do it now */
    /* This should never happed as the pointer is setup in first reset call */
    for (i = 0; (dptr = sim_devices[i]) != NULL; i++) { /* do all devices */
        for (j = 0; j < dptr->numunits; j++) {      /* do all units for device */
            if (uptr == (dptr->units + j)) {        /* match found? */
                uptr->dptr = dptr;                  /* set the pointer in unit */
                return dptr;                        /* return valid pointer */
            }
        }
    }
    return NULL;
}

/* set up the devices configured into the simulator */
/* only devices with a DIB will be processed */
t_stat chan_set_devs() {
    uint32 i, j;

    for (i = 0; i < MAX_DEV; i++) {
        dib_unit[i] = NULL;                         /* clear DIB pointer array */
    }
    for (i = 0; i < MAX_CHAN; i++) {
        dib_chan[i] = NULL;                         /* clear DIB pointer array */
    }
    /* Build channel & device arrays */
    for (i = 0; sim_devices[i] != NULL; i++) {
        DEVICE  *dptr = sim_devices[i];             /* get pointer to next configured device */
        UNIT    *uptr = dptr->units;                /* get pointer to units defined for this device */
        DIB     *dibp = (DIB *)dptr->ctxt;          /* get pointer to DIB for this device */
        CHANP   *chp;                               /* channel program pointer */
        int     chsa;                               /* addr of device chan & subaddress */

        /* set the device back pointer in the unit structure */
        for (j = 0; j < dptr->numunits; j++) {      /* loop through unit entries */
            uptr->dptr = dptr;                      /* set the device pointer in unit structure */
            uptr++;                                 /* next UNIT pointer */
        }
        uptr = dptr->units;                         /* get pointer to units again */

        if (dibp == NULL)                           /* If no DIB, not channel device */
            continue;
        if ((dptr->flags & DEV_DIS) ||              /* Skip disabled devices */
            ((dibp->chan_prg) == NULL)) {           /* must have channel info for each device */

            chsa = GET_UADDR(uptr->u3);             /* ch/sa value */
            printf("Device %s chsa %04x not set up dibp %p\n", dptr->name, chsa, dibp);
            continue;
        }

        chp = (CHANP *)dibp->chan_prg;              /* must have channel information for each device */
        /* Check if address is in unit or dev entry */
        for (j = 0; j < dptr->numunits; j++) {      /* loop through unit entries */
            chsa = GET_UADDR(uptr->u3);             /* ch/sa value */
            printf("Setup device %s%d chsa %04x type %03d dibp %p\n",
                dptr->name, j, chsa, GET_TYPE(uptr->flags), dibp);
            /* zero some channel data loc's for device */
            chp->unitptr = uptr;                    /* set the unit back pointer */
            chp->chan_status = 0;                   /* clear the channel status */
            chp->chan_dev = chsa;                   /* save our address (ch/sa) */
            chp->chan_byte = BUFF_EMPTY;            /* no data yet */
//sim_debug(DEBUG_EXP, &cpu_dev,
//"chan_set_devs BUFF_EMPTY chp %p chan_byte %04x\n", chp, chp->chan_byte);
            chp->ccw_addr = 0;                      /* start loading at loc 0 */
            chp->chan_caw = 0;                      /* set IOCD address to memory location 0 */
            chp->ccw_count = 0;                     /* channel byte count 0 bytes*/
            chp->ccw_flags = 0;                     /* Command chain and supress incorrect length */
            chp->ccw_cmd = 0;                       /* read command */
            chp->chan_inch_addr = 0;                /* clear address of stat dw in memory */

            if ((uptr->flags & UNIT_DIS) == 0) {    /* is unit marked disabled? */
                /* see if this is unit zero */
                if ((chsa & 0xff) == 0) {
                    /* we have channel mux or dev 0 of units */
                    if (dptr->flags & DEV_CHAN) {
                        /* see if channel address already defined */
                        if (dib_chan[get_chan(chsa)] != 0) {
                            printf("Channel mux %04x already defined, aborting\n", chsa);
                            return SCPE_IERR;               /* no, arg error */
                        }
                        printf("Setting Channel mux %04x dibp %p\n", chsa, dibp);
                        /* channel mux, save dib for channel */
                        dib_chan[get_chan(chsa)] = dibp;
                        if (dibp->dev_ini != NULL)          /* if there is an init routine, call it now */
                            dibp->dev_ini(uptr, 1);         /* init the channel */
                    } else {
                        /* we have unit 0 of non-IOP/MFP device */
                        if (dib_unit[chsa] != 0) {
                            printf("Channel/Dev %04x already defined\n", chsa);
                            return SCPE_IERR;               /* no, arg error */
                        } else {
                            /* channel mux, save dib for channel */
                            /* for now, save any zero dev as chan */
                            if (chsa) {
                                printf("Setting Channel zero unit 0 device %04x dibp %p\n", chsa, dibp);
                                dib_unit[chsa] = dibp;      /* no, save the dib address */
                                if (dibp->dev_ini != NULL)  /* if there is an init routine, call it now */
                                    dibp->dev_ini(uptr, 1); /* init the channel */
                            }
                        }
                    }
                } else {
                    /* see if address already defined */
                    if (dib_unit[chsa] != 0) {
                        printf("Channel/SubAddress %04x multiple defined, aborting\n", chsa);
                        return SCPE_IERR;           /* no, arg error */
                    }
                    dib_unit[chsa] = dibp;          /* no, save the dib address */
                }
            }
            if (dibp->dev_ini != NULL)              /* call channel init if defined */
                dibp->dev_ini(uptr, 1);             /* init the channel */
            uptr++;                                 /* next UNIT pointer */
            chp++;                                  /* next CHANP pointer */
        }
    }
    /* now make another pass through the channels and see which integrated */
    /* channel/controllers are defined and add them to the dib_chan definitions */
    /* this will handle non-MFP/IOP channel controllers */
    for (i = 0; i < MAX_CHAN; i++) {
        if (dib_chan[i] == 0) {
            /* channel not defined, see if defined in dib_unit array */
            /* check device zero for suspected channel */
            if (dib_unit[i<<8]) {
                /* write dibp to channel array */
                dib_chan[i] = dib_unit[i<<8];       /* save the channel dib */
            printf("Chan_set_dev new Channel %04x defined at dibp %p\n", i<<8, dib_unit[i<<8]);
            }
        } else {
            printf("Chan_set_dev Channel %04x defined at dibp %p\n", i<<8, dib_chan[i]);
            /* channel is defined, see if defined in dib_unit array */
            if ((dib_unit[i<<8]) == 0) {
                /* write dibp to units array */
                dib_unit[i<<8] = dib_chan[i];       /* save the channel dib */
            }
        }
    }
    return SCPE_OK;                                 /* all is OK */
}

/* Validate and set the device onto a given channel */
t_stat set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
    DEVICE  *dptr;                                  /* device pointer */
    DIB     *dibp;                                  /* dib pointer */
    UNIT    *tuptr;                                 /* temp unit pointer */
    t_value chan;                                   /* new channel addr */
    t_stat  r;                                      /* return status */
    int     i;                                      /* temp */
    int     chsa;                                   /* dev addr */

    if (cptr == NULL)                               /* is there a UNIT name specified */
        return SCPE_ARG;                            /* no, arg error */
    if (uptr == NULL)                               /* is there a UNIT pointer */
        return SCPE_IERR;                           /* no, arg error */
    dptr = get_dev(uptr);                           /* find the device from unit pointer */
    if (dptr == NULL)                               /* device not found, so error */
        return SCPE_IERR;                           /* error */

    dibp = (DIB *)dptr->ctxt;                       /* get dib pointer from device struct */
    if (dibp == NULL)                               /* we need a DIB */
        return SCPE_IERR;                           /* no DIB, so error */

    chan = get_uint(cptr, 16, 0xffff, &r);          /* get new device address */
    if (r != SCPE_OK)                               /* need good number */
        return r;                                   /* number error, return error */

    chan &= 0x7f00;                                 /* clean channel address */
    dibp->chan_addr = chan;                         /* set new parent channel addr */

    /* change all the unit addresses with the new channel, but keep sub address */
    /* Clear out existing entries for all units on this device */
    tuptr = dptr->units;                            /* get pointer to units defined for this device */

    /* loop through all units for this device */
    for (i = 0; i < dibp->numunits; i++) {
        chsa = GET_UADDR(tuptr->u3);                /* get old chsa for this unit */
        dib_unit[chsa] = NULL;                      /* clear sa dib pointer */
        dib_unit[chsa&0x7f00] = NULL;               /* clear the channel dib address */
        chsa = chan | (chsa & 0xff);                /* merge new channel with new sa */
        tuptr->u3 &= ~UNIT_ADDR_MASK;               /* clear old chsa for this unit */
        tuptr->u3 |= UNIT_ADDR(chsa);               /* clear old chsa for this unit */
        dib_unit[chan&0x7f00] = dibp;               /* set the channel dib address */
        dib_unit[chsa] = dibp;                      /* save the dib address for new chsa */
        fprintf(stderr, "Set dev %04x to %04x\r\n", GET_UADDR(tuptr->u3), chsa);
        tuptr++;                                    /* next unit pointer */
    }
    return SCPE_OK;
}

/* display channel/sub-address for device */
t_stat show_dev_addr(FILE *st, UNIT *uptr, int32 v, CONST void *desc) {
    DEVICE      *dptr;
    int         chsa;

    if (uptr == NULL)                               /* valid unit? */
        return SCPE_IERR;                           /* no, error return */
    dptr = get_dev(uptr);                           /* get the device pointer from unit */
    if (dptr == NULL)                               /* valid pointer? */
        return SCPE_IERR;                           /* return error */
    chsa = GET_UADDR(uptr->u3);                     /* get the unit address */
    fprintf(st, "CHAN/SA %04x", chsa);              /* display channel/subaddress */
    return SCPE_OK;                                 /* we done */
}

#ifndef GEERT_UNDO
void CatchSig() {
    void    ReportIRQs();

    /* re-install signal handler */
    signal(SIGQUIT, CatchSig);

    sim_debug(DEBUG_IRQ, &cpu_dev, "SIG[%d] CPUSTATUS=%08x\n",
        ScanCycles, CPUSTATUS);

    ReportIRQs();
    DoNextCycles = 20;
}

void ReportIRQs() {
    int i, irqs = 0;
    int levels[5];
    char *s;

    for (i=0; i<112; i++)
        if (INTS[i] & INTS_REQ) {
            if (irqs < 5)
                levels[irqs] = i;
            irqs++;
        }

    if (CPUSTATUS & 0x80)
        s = " BLOCKED";
    else
        s = "";

    switch (irqs) {
    case 1:
        sim_debug(DEBUG_IRQ, &cpu_dev,
            "SIG[%d]%s irq_pend=%d, ACT=%d: %x\n",
            ScanCycles, s, irq_pend, irqs, levels[0]);
        break;
    case 2:
        sim_debug(DEBUG_IRQ, &cpu_dev,
            "SIG[%d]%s irq_pend=%d, ACT=%d: %x, %x\n",
            ScanCycles, s, irq_pend, irqs, levels[0], levels[1]);
        break;
    case 3:
        sim_debug(DEBUG_IRQ, &cpu_dev,
            "SIG[%d]%s irq_pend=%d, ACT=%d: %x, %x, %x\n",
            ScanCycles, s, irq_pend, irqs, levels[0], levels[1], levels[2]);
        break;
    case 4:
        sim_debug(DEBUG_IRQ, &cpu_dev,
            "SIG[%d]%s irq_pend=%d, ACT=%d: %x, %x, %x, %x\n",
            ScanCycles, s, irq_pend, irqs, levels[0], levels[1], levels[2], levels[3]);
        break;
    default:
        sim_debug(DEBUG_IRQ, &cpu_dev,
            "SIG[%d]%s irq_pend=%d, ACT=%d\n",
            ScanCycles, s, irq_pend, irqs);
    }
}
#endif
