/* sel32_chan.c: SEL 32 Channel functions.

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

int     channels        = MAX_CHAN;         /* maximum number of channels */
int     subchannels     = SUB_CHANS;        /* maximum number of subchannel devices */
int     irq_pend        = 0;                /* pending interrupt flag */

#define AMASK            0x00ffffff         /* 24 bit mask */

extern uint32   M[];                        /* our memory */
extern uint32   SPAD[];                     /* CPU scratchpad memory */
extern uint32   CPUSTATUS;                  /* CPU status word */
extern uint32   INTS[];                     /* Interrupt status flags */
extern int traceme, trstart;

/* device status */
DIB         *dev_unit[MAX_DEV];             /* Pointer to Device info block */
uint16      dev_status[MAX_DEV];            /* last device status flags */
uint16      loading;                        /* set when booting */

#define get_chan(chsa)  ((chsa>>8)&0x7f)    /* get channel number from ch/sa */

/* forward definitions */
CHANP *find_chanp_ptr(uint16 chsa);             /* find chanp pointer */
UNIT *find_unit_ptr(uint16 chsa);               /* find unit pointer */
int  chan_read_byte(uint16 chan, uint8 *data);
int  chan_write_byte(uint16 chan, uint8 *data);
void set_devattn(uint16 chsa, uint16 flags);
void set_devwake(uint16 chsa, uint16 flags);    /* wakeup O/S for async line */
void chan_end(uint16 chan, uint16 flags);
int test_write_byte_end(uint16 chsa);
t_stat startxio(uint16 chsa, uint32 *status);   /* start XIO */
t_stat testxio(uint16 chsa, uint32 *status);    /* test XIO */
t_stat stoptxio(uint16 chsa, uint32 *status);   /* stop XIO */
t_stat rschnlxio(uint16 chsa, uint32 *status);  /* reset channel XIO */
t_stat haltxio(uint16 chsa, uint32 *status);    /* halt XIO */
t_stat grabxio(uint16 chsa, uint32 *status);    /* grab XIO n/u */
t_stat rsctlxio(uint16 chsa, uint32 *status);   /* reset controller XIO */
uint32 find_int_icb(uint16 chsa);
uint32 find_int_lev(uint16 chsa);
uint32 scan_chan(void);
t_stat chan_boot(uint16 chsa, DEVICE *dptr);
t_stat chan_set_devs();
t_stat set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_dev_addr(FILE *st, UNIT *uptr, int32 v, CONST void *desc);

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
    DIB *dibp = dev_unit[chsa & 0x7f00];            /* get DIB pointer for channel */
    if (dibp->chan_fifo_in == ((dibp->chan_fifo_out-1+FIFO_SIZE) % FIFO_SIZE)) {
        return -1;                                  /* FIFO Full */
    }
    dibp->chan_fifo[dibp->chan_fifo_in] = entry;    /* add new entry */
    dibp->chan_fifo_in += 1;                        /* next entry */
    dibp->chan_fifo_in %= FIFO_SIZE;                /* modulo FIFO size */
    return 0;                                       /* all OK */
}

/* get the next entry from the FIFO */
int FIFO_Get(uint16 chsa, uint32 *old)
{
    DIB *dibp = dev_unit[chsa & 0x7f00];            /* get DIB pointer for channel */
    /* see if the FIFO is empty */
    if (dibp->chan_fifo_in == dibp->chan_fifo_out) {
        return -1;                                  /* FIFO is empty, tell caller */
    }
    *old = dibp->chan_fifo[dibp->chan_fifo_out];    /* get the next entry */
    dibp->chan_fifo_out += 1;                       /* next entry */
    dibp->chan_fifo_out %= FIFO_SIZE;               /* modulo FIFO size */
    return 0;                                       /* all OK */
}

/* Find interrupt level for the given device (ch/sa) */
/* return 0 if not found, otherwise level number */
uint32 find_int_lev(uint16 chsa)
{
    uint32  chan, level, val;
    int i;

    chan = (chsa >> 8) & 0x7f;                      /* get channel number */
    /* scan the channel entries for our chan */
    for (i=0; i<128; i++) {
        val = SPAD[i];                              /* get spad entry */
        if ((val == 0) || (val == 0xffffffff))
            continue;                               /* not valid entry */
        /* look for class F devices */
        if ((val & 0x0f000000) == 0x0f000000) {
            /* F class only uses chan entry */
            if (((val >> 8 ) & 0x7f) == chan) {
                /* channel matches, now get interrupt level number */
                level = ((val >> 16) & 0x7f);   /* 1's comp of int level */
                level = 127 - level;            /* get positive number level */
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "find_int_lev F SPAD %x chan %x chsa %x level %x\n", val, chan, chsa, level);
                return(level);                  /* return the level*/
            }
        }
        /* look for E class or class 3 device */
        if (((val & 0x0f000000) == 0x0e000000) ||   /* E class */
            ((val & 0x0f800000) == 0x03800000)) {   /* class 3 (interval timer) */
            /* E class uses chan and device address */
            if ((val & 0x7f00) == (chsa & 0x7f00)) {    /* check chan/subaddress */
                /* channel/subaddress matches, now get interrupt level number */
                level = ((val >> 16) & 0x7f);   /* 1's comp of int level */
                level = 127 - level;            /* get positive number level */
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "find_int_lev E SPAD %x chan %x chsa %x level %x\n", val, chan, chsa, level);
                return(level);                  /* return the level*/
            }
        }
    }
    /* not a real device, so check interrupt entries for match */
    /* scan the entries for our channel/subaddress */
    for (i=0; i<112; i++) {
        val = SPAD[i+0x80];                     /* get spade entry */
        if (val == 0 || val == 0xffffffff)
            continue;                           /* not valid entry */
        /* look for class 3 device or non device entries */
        if (((val & 0x0f800000) == 0x00800000) ||   /* clock or external interrupt */
            ((val & 0x0f800000) == 0x03800000)) {   /* class 3 (interval timer) */
            /* E class or non I/O uses chan and device address */
            if ((val & 0x7f00) == (chsa & 0x7f00)) {    /* check chan/sub address */
                /* channel/subaddress matches, now get interrupt level number */
                level = ((val >> 16) & 0x7f);   /* 1's comp of int level */
                level = 127 - level;            /* get positive number level */
                return(level);                  /* return the level*/
            }
        }
    }
    return 0;                                   /* not found */
}

/* Find interrupt context block address for given device (ch/sa) */
/* return 0 if not found, otherwise ICB memory address */
uint32 find_int_icb(uint16 chsa)
{
    uint32  level, icba;

    level = find_int_lev(chsa);                 /* find the int level */
    if (level == 0)
        return 0;                               /* not found */
    icba = SPAD[0xf1] + (level<<2);             /* interrupt vector address in memory */
    icba = M[icba>>2];                          /* get address of ICB from memory */
    return(icba);                               /* return the address */
}

/* Find unit pointer for given device (ch/sa) */
UNIT *find_unit_ptr(uint16 chsa)
{
    struct dib  *dibp;                          /* DIB pointer */
    UNIT        *uptr;                          /* UNIT pointer */
    int         i;

    dibp = dev_unit[chsa];                      /* get DIB pointer from device pointers */
    if (dibp == 0) {                            /* if zero, not defined on system */
        return NULL;                            /* tell caller */
    }

    uptr = dibp->units;                         /* get the pointer to the units on this channel */
    for (i = 0; i < dibp->numunits; i++) {      /* search through units to get a match */
        if (chsa == GET_UADDR(uptr->u3)) {      /* does ch/sa match? */
            return uptr;                        /* return the pointer */
        }
        uptr++;                                 /* next unit */
    }
    return NULL;                                /* device not found on system */
}

/* Find chanp pointer for given device (ch/sa) */
CHANP *find_chanp_ptr(uint16 chsa)
{
    struct dib  *dibp;                          /* DIB pointer */
    UNIT        *uptr;                          /* UNIT pointer */
    CHANP       *chp;                           /* CHANP pointer */
    int         i;

    dibp = dev_unit[chsa];                      /* get DIB pointer from device pointers */
    if (dibp == 0)                              /* if zero, not defined on system */
        return NULL;                            /* tell caller */
    if ((chp = (CHANP *)dibp->chan_prg) == NULL) {  /* must have channel information for each device */
        return NULL;                            /* tell caller */
    }

    uptr = dibp->units;                         /* get the pointer to the units on this channel */
    for (i = 0; i < dibp->numunits; i++) {      /* search through units to get a match */
        if (chsa == GET_UADDR(uptr->u3)) {      /* does ch/sa match? */
            return chp;                         /* return the pointer */
        }
        uptr++;                                 /* next UNIT */
        chp++;                                  /* next CHANP */
    }
    return NULL;                                /* device not found on system */
}

/* Read a full word into memory.
 * Return 1 if fail.
 * Return 0 if success.
 */
int readfull(CHANP *chp, uint32 maddr, uint32 *word)
{
    maddr &= AMASK;                             /* mask addr to 24 bits */
    if (maddr > MEMSIZE) {                      /* see if mem addr > MEMSIZE */
        chp->chan_status |= STATUS_PCHK;        /* program check error */
        return 1;                               /* show we have error */
    }
    maddr >>= 2;                                /* get 32 bit word index */
    *word = M[maddr];                           /* get the contents */
    sim_debug(DEBUG_EXP, &cpu_dev, "readfull read %x from addr %x\n", *word, maddr<<2);
    return 0;                                   /* return OK */
}

/* Read a word into the channel buffer.
 * Return 1 if fail.
 * Return 0 if success.
 */
int readbuff(CHANP *chp)
{
    int k;
    uint32 addr = chp->ccw_addr;                /* channel buffer address */
    uint16 chan = get_chan(chp->chan_dev);      /* our channel */

    if ((addr & AMASK) > MEMSIZE) {             /* see if memory address invalid */
        chp->chan_status |= STATUS_PCHK;        /* bad, program check */
        chp->chan_byte = BUFF_CHNEND;           /* force channel end */
        irq_pend = 1;                           /* and we have an interrupt */
        return 1;                               /* done, with error */
    }
    addr &= AMASK;                              /* address only */
    addr >>= 2;                                 /* byte to word address */
    chp->chan_buf = M[addr];                    /* get 4 bytes */

    sim_debug(DEBUG_DATA, &cpu_dev, "readbuff read memory bytes into buffer %02x %06x %08x %08x [",
          chan, chp->ccw_addr & 0xFFFFFC, chp->chan_buf, chp->ccw_count);
    for(k = 24; k >= 0; k -= 8) {
        char ch = (chp->chan_buf >> k) & 0xFF;
        if (ch < 0x20 || ch == 0xff)
           ch = '.';
        sim_debug(DEBUG_DATA, &cpu_dev, "%c", ch);
    }
    sim_debug(DEBUG_DATA, &cpu_dev, "]\n");
    return 0;
}

/* Write 32 bit channel buffer to memory.
 * Return 1 if fail.
 * Return 0 if success.
 */
int writebuff(CHANP *chp)
{
    uint32 addr = chp->ccw_addr;

    if ((addr & AMASK) > MEMSIZE) {
        chp->chan_status |= STATUS_PCHK;
        chp->chan_byte = BUFF_CHNEND;
        irq_pend = 1;
        return 1;
    }
    addr &= AMASK;
    M[addr>>2] = chp->chan_buf;
    return 0;
}

/* load in the IOCD and process the commands */
/* return = 0 OK */
/* return = 1 error, chan_status will have reason */
int load_ccw(CHANP *chp, int tic_ok)
{
    uint32      word;
    int         docmd = 0;
    UNIT        *uptr;
    uint16      chan = get_chan(chp->chan_dev);     /* our channel */

loop:
    /* Abort if we have any errors */
    if (chp->chan_status & 0x3f03) {                    /* check channel status */
        sim_debug(DEBUG_EXP, &cpu_dev, "load_ccw ERROR chan_status[%x] %x\n", chan, chp->chan_status);
        return 1;
    }

    /* Check if we have status modifier set */
    if (chp->chan_status & STATUS_MOD) {
        chp->chan_caw += 8;                         /* move to next IOCD */
        chp->chan_status &= ~STATUS_MOD;            /* turn off status modifier flag */
    }

    /* Read in first or next CCW */
    if (readfull(chp, chp->chan_caw, &word) != 0) { /* read word from memory */
        chp->chan_status |= STATUS_PCHK;            /* memory read error, program check */
        sim_debug(DEBUG_EXP, &cpu_dev, "load_ccw ERROR chan_status[%x] %x\n", chan, chp->chan_status);
        return 1;                                   /* error return */
    }

    sim_debug(DEBUG_CMD, &cpu_dev, "load_ccw read ccw chan %02x caw %06x IOCD wd 1 %08x\n",
        chan, chp->chan_caw, word);
    /* TIC can't follow TIC or be first in command chain */
    if (((word >> 24) & 0xf) == CMD_TIC) {
        if (tic_ok) {
            chp->chan_caw = word & AMASK;           /* get new IOCD address */
            tic_ok = 0;                             /* another tic not allowed */
            goto loop;                              /* restart the IOCD processing */
        }
        chp->chan_status |= STATUS_PCHK;            /* program check for invalid tic */
        sim_debug(DEBUG_EXP, &cpu_dev, "load_ccw ERROR chan_status[%x] %x\n", chan, chp->chan_status);
        irq_pend = 1;                               /* status pending */
        return 1;                                   /* error return */
    }
    chp->chan_caw += 4;                             /* point to 2nd word of the IOCD */

    /* Check if not chaining data */
    if ((chp->ccw_flags & FLAG_DC) == 0) {
        chp->ccw_cmd = (word >> 24) & 0xff;         /* not DC, so set command from IOCD wd 1 */
        sim_debug(DEBUG_EXP, &cpu_dev, "load_ccw No DC, flags %x cmd %x\n",
            chp->ccw_flags, chp->ccw_cmd);
        docmd = 1;                                  /* show we have a command */
    }
    /* Set up for this command */
    chp->ccw_addr = word & AMASK;                   /* set the data address */
    readfull(chp, chp->chan_caw, &word);            /* get IOCD WD 2 */

    sim_debug(DEBUG_CMD, &cpu_dev, "load_ccw read ccw chan %02x caw %06x IOCD wd 2 %08x\n",
        chan, chp->chan_caw, word);
    chp->chan_caw += 4;                             /* next IOCD address */
    chp->ccw_count = word & 0xffff;                 /* get 16 bit byte count from IOCD WD 2*/
    chp->ccw_flags = (word >> 16) & 0xffff;         /* get flags from bits 0-7 of WD 2 of IOCD */
    chp->chan_byte = BUFF_EMPTY;                    /* no bytes transferred yet */
    if (chp->ccw_flags & FLAG_PCI) {                /* do we have prog controlled int? */
        chp->chan_status |= STATUS_PCI;             /* set PCI flag in status */
        irq_pend = 1;                               /* interrupt pending */
    }

    sim_debug(DEBUG_EXP, &cpu_dev, "load_ccw read docmd %x irq_flag %x count %x chan %x\n",
        docmd, irq_pend, chp->ccw_count, chan);
    /* Check invalid count */
    /* HACK HACK - LPR sends CC cmd only without data addr/count */
    if ((chp->ccw_count == 0) && (chp->ccw_addr != 0)) {    /* see if byte count is zero */
        chp->chan_status |= STATUS_PCHK;            /* program check if it is */
        irq_pend = 1;                               /* status pending int */
        return 1;                                   /* error return */
    }
    if (docmd) {                                    /* see if we need to process command */
        DIB *dibp = dev_unit[chp->chan_dev];        /* get the device pointer */
        uptr = find_unit_ptr(chp->chan_dev);        /* find the unit pointer */
        if (uptr == 0)
            return 1;                               /* if none, error */

        /* Check if this is INCH command */
        if ((chp->ccw_cmd & 0xFF) == 0) {           /* see if this is an initialize channel cmd */
            uptr->u4 = (uint32)chp->ccw_addr;       /* save the memory address in wd 1 of iocd */
            uptr->us9 = chp->ccw_count & 0xffff;    /* get count from IOCD wd 2 */
            /* just drop through and call the device startcmd function */
            /* the INCH buffer will be returned in uptr->u4 and uptr->us9 will be non-zero */
            /* it should just return SNS_CHNEND and SNS_DEVEND status */
        }

        sim_debug(DEBUG_EXP, &cpu_dev, "load_ccw before start_cmd chan %0x status %.8x count %x\n",
                chan, chp->chan_status, chp->ccw_count);

        /* call the device startcmd function to process command */
        chp->chan_status = dibp->start_cmd(uptr, chan, chp->ccw_cmd);

        sim_debug(DEBUG_EXP, &cpu_dev, "load_ccw after start_cmd chan %0x status %.8x count %x\n",
                chan, chp->chan_status, chp->ccw_count);

        /* see if bad status */
        if (chp->chan_status & (STATUS_ATTN|STATUS_CHECK|STATUS_EXPT)) {
            chp->chan_status |= STATUS_CEND;        /* channel end status */
            chp->ccw_flags = 0;                     /* no flags */
            chp->ccw_cmd = 0;                       /* stop IOCD processing */
            irq_pend = 1;                           /* int coming */
            sim_debug(DEBUG_CMD, &cpu_dev, "load_ccw bad status chan %0x status %.8x\n",
                chan, chp->chan_status);
            return 1;                               /* error return */
        }

        /* see if command completed */
        if (chp->chan_status & (STATUS_DEND|STATUS_CEND)) {
            /* INCH cmd will return here too, get INCH buffer addr from uptr->u4 */
            /* see if this is an initialize channel cmd */
            if ((chp->ccw_cmd & 0xFF) == 0 && (uptr->us9 != 0)) {
                chp->chan_inch_addr = uptr->u4;     /* save INCH buffer address */
                sim_debug(DEBUG_EXP, &cpu_dev, "load_ccw INCH %x saved chan %0x\n",
                        chp->chan_inch_addr, chan);
            }
            chp->chan_status |= STATUS_CEND;        /* set channel end status */
            chp->chan_byte = BUFF_NEWCMD;           /* ready for new cmd */
            chp->ccw_cmd = 0;                       /* stop IOCD processing */
            irq_pend = 1;                           /* int coming */
            sim_debug(DEBUG_EXP, &cpu_dev, "load_ccw cmd complete chan %0x status %.8x count %x\n",
                chan, chp->chan_status, chp->ccw_count);
        }
    }
    sim_debug(DEBUG_EXP, &cpu_dev, "load_ccw return, chan %0x status %.8x count %x\n",
        chan, chp->chan_status, chp->ccw_count);
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
    if (chp->chan_status & 0x3f03)                  /* check channel status */
        return 1;                                   /* return error */

    if (chp->chan_byte == BUFF_CHNEND)
        return 1;                                   /* return error */
    if (chp->ccw_count == 0) {                      /* see if more data required */
         if ((chp->ccw_flags & FLAG_DC) == 0) {     /* see if Data Chain */
            chp->chan_status |= STATUS_CEND;        /* no, end of data */
            chp->chan_byte = BUFF_CHNEND;           /* buffer end too */
            sim_debug(DEBUG_DATA, &cpu_dev, "chan_read_byte end status %x\n", chp->chan_status);
            return 1;                               /* return error */
         } else {
            /* we have data chaining, process iocl */
            sim_debug(DEBUG_DATA, &cpu_dev, "chan_read_byte calling load_ccw chan %x\n", chan);
            if (load_ccw(chp, 1))                   /* process data chaining */ 
                return 1;                           /* return error */
         }
    }
    if (chp->chan_byte == BUFF_EMPTY) {             /* is buffer empty? */
         if (readbuff(chp))                         /* read next 4 chars */
            return 1;                               /* return error */
         chp->chan_byte = chp->ccw_addr & 0x3;      /* get byte number from address */
         chp->ccw_addr += 4 - chp->chan_byte;       /* next byte address */
    }
    chp->ccw_count--;                               /* one char less to process */
    /* get the byte of data */
    byte = (chp->chan_buf >>  (8 * (3 - (chp->chan_byte & 0x3)))) & 0xff;
    chp->chan_byte++;                               /* next byte offset in word */
    *data = byte;                                   /* return the data */
    sim_debug(DEBUG_DATA, &cpu_dev, "chan_read_byte transferred %x\n", byte);
    return 0;                                       /* good return */
}

/* test end of write byte I/O (device read) */
int test_write_byte_end(uint16 chsa)
{
    int     chan = get_chan(chsa);                  /* get the channel number */
    CHANP   *chp = find_chanp_ptr(chsa);            /* get channel prog pointer */

    /* see if at end of buffer */
    if (chp->chan_byte == BUFF_CHNEND)
        return 1;                                   /* return done */
    if (chp->ccw_count == 0) {
        if (chp->chan_byte & BUFF_DIRTY) {
            writebuff(chp);                         /* write it */
        }
        if ((chp->ccw_flags & FLAG_DC) == 0) {      /* see if we have data chaining */
            chp->chan_status |= STATUS_CEND;        /* no, end of data */
            chp->chan_byte = BUFF_CHNEND;           /* thats all the data we want */
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
    int     offset;
    uint32  mask;

    /* Abort if we have any errors */
    if (chp->chan_status & 0x3f03)                  /* check channel status */
        return 1;                                   /* return error */

    /* see if at end of buffer */
    if (chp->chan_byte == BUFF_CHNEND) {
        sim_debug(DEBUG_CMD, &cpu_dev, "chan_write_byte BUFF_CHNEND\n");
        /* if SLI not set, we have incorrect length */
        if ((chp->ccw_flags & FLAG_SLI) == 0) {
            sim_debug(DEBUG_CMD, &cpu_dev, "chan_write_byte 4 setting SLI ret\n");
            chp->chan_status |= STATUS_LENGTH;      /* set SLI */
        }
        return 1;                                   /* return error */
    }
    if (chp->ccw_count == 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "chan_write_byte cccw_count is zero ccw_count[%x] %x\n",
                chan, chp->ccw_count);
        if (chp->chan_byte & BUFF_DIRTY) {
            sim_debug(DEBUG_CMD, &cpu_dev, "chan_write_byte 2 BUF DIRTY ret\n");
            if (writebuff(chp))                     /* write it */
                return 1;                           /* return error */
        }
        if ((chp->ccw_flags & FLAG_DC) == 0) {      /* see if we have data chaining */
            sim_debug(DEBUG_CMD, &cpu_dev, "chan_write_byte no DC\n");
            chp->chan_status |= STATUS_CEND;        /* no, end of data */
            chp->chan_byte = BUFF_CHNEND;           /* thats all the data we want */
            return 1;                               /* return error */
        } else {
            /* we have data chaining, process iocl */
            sim_debug(DEBUG_DATA, &cpu_dev, "chan_write_byte calling load_ccw chan %x\n", chan);
            if (load_ccw(chp, 1))                   /* process data chaining */ 
                return 1;                           /* return error */
         }
    }
    sim_debug(DEBUG_DATA, &cpu_dev, "chan_write_byte non zero ccw_count[%x] %x\n", chan, chp->ccw_count);
    if (chp->ccw_flags & FLAG_SKIP) {
        chp->ccw_count--;
        chp->chan_byte = BUFF_EMPTY;
        if ((chp->ccw_cmd & 0xff) == CMD_RDBWD)
            chp->ccw_addr--;
        else
            chp->ccw_addr++;
        sim_debug(DEBUG_CMD, &cpu_dev, "chan_write_byte SKIP ret\n");
        return 0;
    }
    if (chp->chan_byte == (BUFF_EMPTY|BUFF_DIRTY)) {
        if (writebuff(chp)) 
            return 1;
        sim_debug(DEBUG_DATA, &cpu_dev, "chan_write_byte BUF EMPTY|DIRTY ret\n");
        if ((chp->ccw_cmd & 0xff) == CMD_RDBWD)
            chp->ccw_addr -= (1 + (chp->ccw_addr & 0x3));
        else
            chp->ccw_addr += (4 - (chp->ccw_addr & 0x3));
        chp->chan_byte = BUFF_EMPTY;
    }
    if (chp->chan_byte == BUFF_EMPTY)
        chp->chan_byte = chp->ccw_addr & 0x3;
    chp->ccw_count--;
    offset = 8 * (chp->chan_byte & 0x3);            /* calc byte offset in word */
    mask = 0xff000000 >> offset;                    /* build char mask */
    chp->chan_buf &= ~mask;                         /* zero out the old byte */
    chp->chan_buf |= ((uint32)(*data)) << (24 - offset);    /* or in the new one */

    if ((chp->ccw_cmd & 0xff) == CMD_RDBWD) {
        if (chp->chan_byte & 0x3)
            chp->chan_byte--;
        else
            chp->chan_byte = BUFF_EMPTY;
    } else
        chp->chan_byte++;                           /* next byte */
    chp->chan_byte |= BUFF_DIRTY;                   /* we are used */
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
        fprintf(stderr, "FIFO Overflow ERROR on chsa %x\r\n", chsa);
    }
    irq_pend = 1;                                   /* wakeup controller */
}

/* post interrupt for specified channel */
void set_devattn(uint16 chsa, uint16 flags)
{
    int     chan = get_chan(chsa);                  /* get the channel number */
    CHANP   *chp = find_chanp_ptr(chsa);            /* get channel prog pointer */

    sim_debug(DEBUG_EXP, &cpu_dev, "set_devattn chsa %x, flags %x\n", chsa, flags);

    if (chp->chan_dev == chsa && (chp->chan_status & STATUS_CEND) != 0 && (flags & SNS_DEVEND) != 0) {
        chp->chan_status |= ((uint16)flags);
    } else
        dev_status[chsa] = flags;                   /* save device status flags */
    sim_debug(DEBUG_CMD, &cpu_dev, "set_devattn(%x, %x) %x\n", chsa, flags, chp->chan_dev);
    irq_pend = 1;
}

/* channel operation completed */
void chan_end(uint16 chsa, uint16 flags) {
    int     chan = get_chan(chsa);                  /* get the channel number */
    uint32  chan_icb = find_int_icb(chsa);          /* get icb address */
    CHANP   *chp = find_chanp_ptr(chsa);            /* get channel prog pointer */

    sim_debug(DEBUG_EXP, &cpu_dev, "chan_end chsa %x, flags %x\n", chsa, flags);
    if (chp->chan_byte & BUFF_DIRTY) {
        if (writebuff(chp))                         /* write remaining data */
            return;                                 /* error */
        chp->chan_byte = BUFF_EMPTY;                /* we are empty now */
    }
    chp->chan_status |= STATUS_CEND;                /* set channel end */
    chp->chan_status |= ((uint16)flags);            /* add in the callers flags */
    chp->ccw_cmd = 0;                               /* reset the completed channel command */

    /* test for incorrect transfer length */
    if (chp->ccw_count != 0 && ((chp->ccw_flags & FLAG_SLI) == 0)) {
        chp->chan_status |= STATUS_LENGTH;          /* show incorrect length status */
        chp->ccw_flags = 0;                         /* no flags */
    }
    /* no flags for attention status */
    if (flags & (SNS_ATTN|SNS_UNITCHK|SNS_UNITEXP)) {
        chp->ccw_flags = 0;                         /* no flags */
    }

    sim_debug(DEBUG_EXP, &cpu_dev, "chan_end test end chsa %x, flags %x\n", chsa, flags);
    /* test for device or controller end */
    if (chp->chan_status & (STATUS_DEND|STATUS_CEND)) {
        chp->chan_byte = BUFF_NEWCMD;               /* clear byte flag */
        while ((chp->ccw_flags & FLAG_DC)) {        /* handle data chaining */
            if (load_ccw(chp, 1))                   /* continue channel program */
                break;                              /* error */
            if ((chp->ccw_flags & FLAG_SLI) == 0) { /* suppress incorrect length? */
                chp->chan_status |= STATUS_LENGTH;  /* no, show incorrect length */
                chp->ccw_flags = 0;                 /* no flags */
            }
        }
    }
    irq_pend = 1;                                   /* flag to test for int condition */
}

/* store the device status into the status DW in memory */
/* the INCH command provides the status address in memory */
/* return the icb address */
void store_csw(CHANP *chp)
{
    uint32  stwd1, stwd2;                           /* words 1&2 of stored status */
    uint32  chsa = chp->chan_dev;                   /* get ch/sa information */

    /* put sub address in byte 0 */
    stwd1 = ((chsa & 0xff) << 24) | chp->chan_caw;      /* subaddress and IOCD address to SW 1 */
    /* save 16 bit channel status and residual byte count in SW 2 */
    stwd2 = ((uint32)chp->chan_status << 16) | ((uint32)chp->ccw_count);
    if ((FIFO_Put(chsa, stwd1) == -1) || (FIFO_Put(chsa, stwd2) == -1)) {
        fprintf(stderr, "FIFO Overflow ERROR on chsa %x\r\n", chsa);
    }

    chp->chan_status = 0;                           /* no status anymore */
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

/* start an XIO operation */
/* chan channel number 0-7f */
/* suba unit address within channel 0-ff */
/* Condition codes to return 0-f as specified above */
t_stat startxio(uint16 lchsa, uint32 *status) {
    int     lchan = get_chan(lchsa);                /* get the logical channel number */
    DIB     *dibp;                                  /* device information pointer */
    UNIT    *uptr;                                  /* pointer to unit in channel */
    uint32  chan_ivl;                               /* Interrupt Level ICB address for channel */
    uint32  iocla;                                  /* I/O channel IOCL address int ICB */
    uint32  tempa, inta, spadent, chan;
    uint16  chsa;
    CHANP   *chp;

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[lchan];                          /* get spad device entry for logical channel */
    chan = (spadent & 0xff00) >> 8;                 /* get real channel */
    chsa = (chan << 8) | (lchsa & 0xff);            /* merge sa to real channel */
    dibp = dev_unit[chsa];                          /* get the device information pointer */
    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */
    uptr = find_unit_ptr(chsa);                     /* find pointer to unit on channel */

    sim_debug(DEBUG_CMD, &cpu_dev, "startxio 1 chsa %x chan %x\n", chsa, chan);
    if (dibp == 0 || uptr == 0) {                   /* if no dib or unit ptr, CC3 on return */
        *status = CC3BIT;                           /* not found, so CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }
    sim_debug(DEBUG_CMD, &cpu_dev, "startxio 2 chsa %x chan %x\n", chsa, chan);
    if ((uptr->flags & UNIT_ATT) == 0) {            /* is unit attached? */
        fprintf(stderr, "startxio chsa %x is not attached, error return\r\n", chsa);
        *status = CC3BIT;                           /* not attached, so error CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }
    /* see if interrupt is setup in SPAD and determine IVL for channel */
    sim_debug(DEBUG_CMD, &cpu_dev, "startxio dev spad %.8x chsa %x chan %x\n", spadent, chsa, chan);

    inta = ((spadent & 0x007f0000) >> 16);          /* 1's complement of chan int level */
    inta = 127 - inta;                              /* get positive int level */
    spadent = SPAD[inta + 0x80];                    /* get interrupt spad entry */
    sim_debug(DEBUG_CMD, &cpu_dev, "startxio int spad %.8x inta %x chan %x\n", spadent, inta, chan);

    /* get the address of the interrupt IVL in main memory */
    chan_ivl = SPAD[0xf1] + (inta<<2);              /* contents of spad f1 points to chan ivl in mem */
    chan_ivl = M[chan_ivl >> 2];                    /* get the interrupt context block addr in memory */
    iocla = M[(chan_ivl+16)>>2];                    /* iocla is in wd 4 of ICB */
    sim_debug(DEBUG_CMD, &cpu_dev, "startxio busy test chsa %0x chan %x cmd %x flags %x IOCD1 %x IOCD2 %x\n",
        chsa, chan, chp->ccw_cmd, chp->ccw_flags, M[iocla>>2], M[(iocla+4)>>2]);

    sim_debug(DEBUG_CMD, &cpu_dev, "$$$ SIO %x %x cmd %x flags %x\n",
            chsa, chan, chp->ccw_cmd, chp->ccw_flags);
    /* check for a Command or data chain operation in progresss */
    if (chp->ccw_cmd != 0 || (chp->ccw_flags & (FLAG_DC|FLAG_CC)) != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "startxio busy return CC4 chsa %x chan %x\n", chsa, chan);
        *status = CC4BIT;                           /* busy, so CC4 */
        return SCPE_OK;                             /* just busy CC4 */
    }

    /* determine if channel DIB has a pre startio command processor */
    if (dibp->pre_io != NULL) {                     /* NULL if no startio function */
        /* call the device controller to get prestart_io status */
        tempa = dibp->pre_io(uptr, chan);           /* get status from device */
        if (tempa != 0) {                           /* see if sub channel status is ready */
            /* The device must be busy or something, but it is not ready.  Return busy */
            sim_debug(DEBUG_CMD, &cpu_dev, "startxio start_io call return busy chan %x cstat %08x\n",
                chan, tempa);
            chp->chan_status = 0;                   /* no status anymore */
            *status = CC3BIT|CC4BIT;                /* sub channel busy, so CC3|CC4 */
            return SCPE_OK;                         /* just busy or something, CC3|CC4 */
        }
    }

    /* channel not busy and ready to go, so start a new command */
    chp->chan_status = 0;                           /* no channel status yet */
    dev_status[chsa] = 0;                           /* no unit status either */
    chp->chan_caw = iocla;                          /* get iocla address in memory */
    /* set status words in memory to first IOCD information */
    tempa = chp->chan_inch_addr;                    /* get inch status buffer address */
    if (tempa != 0) {
        M[tempa >> 2] = (chsa & 0xff) << 24 | iocla;    /* suba & IOCD address to status */
        M[(tempa+4) >> 2] = 0;                      /* null status and residual byte count */
    }

    sim_debug(DEBUG_CMD, &cpu_dev, "$$ SIO starting IOCL processing chsa %02x\n", chsa);

    /* start processing the IOCL */
    if (load_ccw(chp, 0) || (chp->chan_status & STATUS_PCI)) {
        /* we have an error or user requested interrupt, return status */
        store_csw(chp);                             /* store the status in the inch status dw */
        sim_debug(DEBUG_CMD, &cpu_dev, "startxio store csw CC1 chan %02x status %08x\n", chan, chp->chan_status);
        chp->chan_status &= ~STATUS_PCI;            /* remove PCI status bit */
        dev_status[chsa] = 0;                       /* no device status */
        *status = CC1BIT;                           /* status stored, so CC1 */
        return SCPE_OK;                             /* CC1 (0x40) status stored */
    }

    if ((chp->ccw_cmd & 0xFF) == 0)                 /* see if this is an initialize channel cmd */
        *status = CC1BIT;                           /* request accepted, no status, so CC1 TRY THIS */
    else
        *status = 0;                                /* CCs = 0, SIO accepted, will echo status  */
    sim_debug(DEBUG_CMD, &cpu_dev, "$$$ SIO done chsa %x status %08x\n", chsa, chp->chan_status);
    return SCPE_OK;                                 /* No CC's all OK  */
}

/* TIO - I/O status */
t_stat testxio(uint16 lchsa, uint32 *status) {        /* test XIO */
    uint32  chan = get_chan(lchsa);
    DIB     *dibp;
    UNIT    *uptr;
    uint32  chan_ivl;                               /* Interrupt Level ICB address for channel */
    uint32  iocla;                                  /* I/O channel IOCL address int ICB */
    uint32  stata;                                  /* I/O channel status location in ICB */
    uint32  tempa, inta, spadent;
    uint16  chsa;                                   /* chan/subaddr */
    CHANP   *chp, *pchp;                            /* Channel prog pointers */
    uint32  sw1, sw2;                               /* status word 1 & 2 */

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[chan];                           /* get spad device entry for logical channel */
    chan = (spadent & 0xff00) >> 8;                 /* get real channel */
    chsa = (chan << 8) | (lchsa & 0xff);            /* merge sa to real channel */
    dibp = dev_unit[chsa];                          /* get the device information pointer */
    chp = find_chanp_ptr(chsa);                     /* find the device chanp pointer */
    uptr = find_unit_ptr(chsa);                     /* find pointer to unit on channel */
    pchp = find_chanp_ptr(chsa & 0x7f00);           /* find the channel chanp pointer */

    sim_debug(DEBUG_CMD, &cpu_dev, "testxio 1 chsa %x chan %x\n", chsa, chan);
    if ((dibp == 0) || (uptr == 0)) {               /* if non found, CC3 on return */
        *status = CC3BIT;                           /* not found, so CC3 */
        goto tioret;                                /* not found, CC3 */
    }
    sim_debug(DEBUG_CMD, &cpu_dev, "testxio 2 chsa %x chan %x\n", chsa, chan);
    if ((uptr->flags & UNIT_ATT) == 0) {            /* is unit already attached? */
        *status = CC3BIT;                           /* not attached, so error CC3 */
        goto tioret;                                /* not found, CC3 */
    }
    /* see if interrupt is setup in SPAD and determine IVL for channel */
    sim_debug(DEBUG_CMD, &cpu_dev, "testxio dev spad %.8x chsa %x chan %x\n", spadent, chsa, chan);

    /* the startio opcode processing software has already checked for F class */
    inta = ((spadent & 0x007f0000) >> 16);          /* 1's complement of chan int level */
    inta = 127 - inta;                              /* get positive int level */
    spadent = SPAD[inta + 0x80];                    /* get interrupt spad entry */
    sim_debug(DEBUG_CMD, &cpu_dev, "testxio int spad %.8x inta %x chan %x\n", spadent, inta, chan);

    /* get the address of the interrupt IVL in main memory */
    chan_ivl = SPAD[0xf1] + (inta<<2);              /* contents of spad f1 points to chan ivl in mem */
    chan_ivl = M[chan_ivl >> 2];                    /* get the interrupt context block addr in memory */
    iocla = M[(chan_ivl+16)>>2];                    /* iocla is in wd 4 of ICB */

    sim_debug(DEBUG_CMD, &cpu_dev, "testxio busy test chsa %0x chan %x cmd %x flags %x IOCD1 %x IOCD2 %x\n",
        chsa, chan, chp->ccw_cmd, chp->ccw_flags, M[iocla>>2], M[(iocla+4)>>2]);

    sim_debug(DEBUG_CMD, &cpu_dev, "$$$ TIO %x %x %x %x\n", chsa, chan, chp->ccw_cmd, chp->ccw_flags);

    /* check for a Command or data chain operation in progresss */
    if (chp->ccw_cmd != 0 || (chp->ccw_flags & (FLAG_DC|FLAG_CC)) != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "testxio busy return CC4 chsa %x chan %x\n", chsa, chan);
        *status = CC4BIT;                           /* busy, so CC4 */
        goto tioret;                                /* just busy CC4 */
    }
    /* the channel is not busy, see if any status to post */
    if ((FIFO_Get(chsa, &sw1) == 0) && (FIFO_Get(chsa, &sw2) == 0)) {
        uint32  chan_icb = find_int_icb(chsa);          /* get icb address */

    sim_debug(DEBUG_CMD, &cpu_dev, "testxio status stored OK, sw1 %x sw2 %x\n", sw1, sw2);
        /* we have status to return, do it now */
        tempa = pchp->chan_inch_addr;               /* get inch status buffer address */
        M[tempa >> 2] = sw1;                        /* save sa & IOCD address in status WD 1 loc */
        /* save the status to memory */
        M[(tempa+4) >> 2] = sw2;                    /* save status and residual count in status WD 2 loc */
        /* now store the status dw address into word 5 of the ICB for the channel */
        M[(chan_icb + 20) >> 2] = tempa | BIT1;     /* post sw addr in ICB+5w & set CC2 in INCH addr */
        *status = CC2BIT;                           /* status stored from SIO, so CC2 */
        goto tioret;                                /* CC2 and OK */
    }
    /* nothing going on, so say all OK */
    *status = CC1BIT;                               /* request accepted, no status, so CC1 TRY THIS */
tioret:
    sim_debug(DEBUG_CMD, &cpu_dev, "$$$ TIO END chsa %x chan %x cmd %x flags %x chan_stat %x CCs %x\n",
        chsa, chan, chp->ccw_cmd, chp->ccw_flags, chp->chan_status, *status);
    return SCPE_OK;                                 /* No CC's all OK  */
}

/* Stop XIO */
t_stat stopxio(uint16 lchsa, uint32 *status) {        /* stop XIO */
    int     chan = get_chan(lchsa);
    DIB     *dibp;
    UNIT    *uptr;
    uint32  chan_ivl;                               /* Interrupt Level ICB address for channel */
    uint32  iocla;                                  /* I/O channel IOCL address int ICB */
    uint32  inta, spadent;
    uint16  chsa;
    CHANP   *chp;

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[chan];                           /* get spad device entry for logical channel */
    chan = (spadent & 0xff00) >> 8;                 /* get real channel */
    chsa = (chan << 8) | (lchsa & 0xff);            /* merge sa to real channel */
    dibp = dev_unit[chsa];                          /* get the device information pointer */
    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */
    uptr = find_unit_ptr(chsa);                     /* find pointer to unit on channel */

    sim_debug(DEBUG_CMD, &cpu_dev, "stopxio 1 chsa %x chan %x\n", chsa, chan);
    if (dibp == 0 || uptr == 0) {                   /* if no dib or unit ptr, CC3 on return */
        *status = CC3BIT;                           /* not found, so CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }
    sim_debug(DEBUG_CMD, &cpu_dev, "stopxio 2 chsa %x chan %x\n", chsa, chan);
    if ((uptr->flags & UNIT_ATT) == 0) {            /* is unit already attached? */
        *status = CC3BIT;                           /* not attached, so error CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }
    /* see if interrupt is setup in SPAD and determine IVL for channel */
    sim_debug(DEBUG_CMD, &cpu_dev, "stopxio dev spad %.8x chsa %x chan %x\n", spadent, chsa, chan);

    /* the startio opcode processing software has already checked for F class */
    inta = ((spadent & 0x007f0000) >> 16);  /* 1's complement of chan int level */
    inta = 127 - inta;                              /* get positive int level */
    spadent = SPAD[inta + 0x80];                    /* get interrupt spad entry */
    sim_debug(DEBUG_CMD, &cpu_dev, "stopxio int spad %.8x inta %x chan %x\n", spadent, inta, chan);

    /* get the address of the interrupt IVL in main memory */
    chan_ivl = SPAD[0xf1] + (inta<<2);              /* contents of spad f1 points to chan ivl in mem */
    chan_ivl = M[chan_ivl >> 2];                    /* get the interrupt context block addr in memory */
    iocla = M[(chan_ivl+16)>>2];                    /* iocla is in wd 4 of ICB */
    sim_debug(DEBUG_CMD, &cpu_dev, "stopxio busy test chsa %0x chan %x cmd %x flags %x IOCD1 %x IOCD2 %x\n",
        chsa, chan, chp->ccw_cmd, chp->ccw_flags, M[iocla>>2], M[(iocla+4)>>2]);

    sim_debug(DEBUG_CMD, &cpu_dev, "$$$ STOPIO %x %x %x %x\n", chsa, chan, chp->ccw_cmd, chp->ccw_flags);

    /* check for a Command or data chain operation in progresss */
    if (chp->ccw_cmd != 0 || (chp->ccw_flags & (FLAG_DC|FLAG_CC)) != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "stopxio busy return CC4 chsa %x chan %x\n", chsa, chan);
        /* reset the DC or CC bits to force completion after current IOCD */
        chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);       /* reset chaining bits */
        dev_status[chsa] |= STATUS_ECHO;            /* show we stopped the cmd */
        *status = CC4BIT;                           /* busy, so CC4 */
        return SCPE_OK;                             /* just busy CC4 */
    }
    /* the channel is not busy, so return OK */
    *status = 0;                                    /* CCs = 0, accepted */
    sim_debug(DEBUG_CMD, &cpu_dev, "$$$ STOPIO good return chsa %x chan %x cmd %x flags %x status %x\n",
        chsa, chan, chp->ccw_cmd, chp->ccw_flags, *status);
    return SCPE_OK;                                 /* No CC's all OK  */
}

/* Reset Channel XIO */
t_stat rschnlxio(uint16 lchsa, uint32 *status) {      /* reset channel XIO */
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
    dibp = dev_unit[chsa];                          /* get the device information pointer */
    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */
    uptr = find_unit_ptr(chsa);                     /* find pointer to unit on channel */

    sim_debug(DEBUG_CMD, &cpu_dev, "rschnlxio 1 chan %x SPAD %x\n", chsa, spadent);
    if (dibp == 0 || uptr == 0) {                   /* if no dib or unit ptr, CC3 on return */
        *status = CC3BIT;                           /* not found, so CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }
    sim_debug(DEBUG_CMD, &cpu_dev, "rschnlxio 2 chan %x, spad %x\r\n", chsa, spadent);
    if ((uptr->flags & UNIT_ATT) == 0) {            /* is unit already attached? */
        *status = CC3BIT;                           /* not attached, so error CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }
    /* reset the FIFO pointers */
    dibp->chan_fifo_in = 0;
    dibp->chan_fifo_out = 0;
    dev_status[chan] = 0;                           /* clear the channel status location */
    chp->chan_inch_addr = 0;                        /* remove inch status buffer address */
    lev = find_int_lev(chan);                       /* get our int level */
    INTS[lev] &= ~INTS_ACT;                         /* clear level active */
    INTS[lev] &= ~INTS_REQ;                         /* clear level request */
    SPAD[lev+0x80] &= ~SINT_ACT;                    /* clear spad too */

    /* now go through all the sa for the channel and stop any IOCLs */
    for (i=0; i<256; i++) {
        chsa = chan | i;                            /* merge sa to real channel */
        dibp = dev_unit[chsa];                      /* get the device information pointer */
        if (dibp == 0)
        {
            continue;                               /* not used */
        }
        chp = find_chanp_ptr(chsa);                 /* find the chanp pointer */
        if (chp == 0) {
            continue;                               /* not used */
        }   
        dev_status[chsa] = 0;                       /* clear device status */
        chp->chan_status = 0;                       /* clear the channel status */
        chp->chan_byte = BUFF_EMPTY;                /* no data yet */
        chp->ccw_addr = 0;                          /* clear buffer address */
        chp->chan_caw = 0x0;                        /* clear IOCD address */
        chp->ccw_count = 0;                         /* channel byte count 0 bytes*/
        chp->ccw_flags = 0;                         /* clear flags */
        chp->ccw_cmd = 0;                           /* read command */
    }
    sim_debug(DEBUG_CMD, &cpu_dev, "rschnlxio return CC1 chan %x lev %x\n", chan, lev);
    *status = CC1BIT;                               /* request accepted, no status, so CC1 TRY THIS */
    return SCPE_OK;                                 /* All OK */
}

/* HIO - Halt I/O */
t_stat haltxio(uint16 lchsa, uint32 *status) {        /* halt XIO */
    int     chan = get_chan(lchsa);
    DIB     *dibp;
    UNIT    *uptr;
    uint32  chan_ivl;                               /* Interrupt Level ICB address for channel */
    uint32  iocla;                                  /* I/O channel IOCL address int ICB */
    uint32  inta, spadent;
    uint16  chsa;
    CHANP   *chp;

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[chan];                           /* get spad device entry for logical channel */
    chan = (spadent & 0xff00) >> 8;                 /* get real channel */
    chsa = (chan << 8) | (lchsa & 0xff);            /* merge sa to real channel */
    dibp = dev_unit[chsa];                          /* get the device information pointer */
    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */
    uptr = find_unit_ptr(chsa);                     /* find pointer to unit on channel */

    sim_debug(DEBUG_CMD, &cpu_dev, "haltxio 1 chsa %x chan %x\n", chsa, chan);
    if (dibp == 0 || uptr == 0) {                   /* if no dib or unit ptr, CC3 on return */
        *status = CC3BIT;                           /* not found, so CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }
    sim_debug(DEBUG_CMD, &cpu_dev, "haltxio 2 chsa %x chan %x\n", chsa, chan);
    if ((uptr->flags & UNIT_ATT)  == 0) {           /* is unit already attached? */
        *status = CC3BIT;                           /* not attached, so error CC3 */
        return SCPE_OK;                             /* not found, CC3 */
    }
    /* see if interrupt is setup in SPAD and determine IVL for channel */
    sim_debug(DEBUG_CMD, &cpu_dev, "haltxio dev spad %.8x chsa %x chan %x\n", spadent, chsa, chan);

    /* the startio opcode processing software has already checked for F class */
    inta = ((spadent & 0x007f0000) >> 16);          /* 1's complement of chan int level */
    inta = 127 - inta;                              /* get positive int level */
    spadent = SPAD[inta + 0x80];                    /* get interrupt spad entry */
    sim_debug(DEBUG_CMD, &cpu_dev, "haltxio int spad %.8x inta %x chan %x\n", spadent, inta, chan);

    /* get the address of the interrupt IVL in main memory */
    chan_ivl = SPAD[0xf1] + (inta<<2);              /* contents of spad f1 points to chan ivl in mem */
    chan_ivl = M[chan_ivl >> 2];                    /* get the interrupt context block addr in memory */
    iocla = M[(chan_ivl+16)>>2];                    /* iocla is in wd 4 of ICB */
    sim_debug(DEBUG_CMD, &cpu_dev, "haltxio busy test chsa %0x chan %x cmd %x flags %x IOCD1 %x IOCD2 %x\n",
        chsa, chan, chp->ccw_cmd, chp->ccw_flags, M[iocla>>2], M[(iocla+4)>>2]);

    sim_debug(DEBUG_CMD, &cpu_dev, "$$$ HIO %x %x %x %x\n", chsa, chan, chp->ccw_cmd, chp->ccw_flags);

    /* check for a Command or data chain operation in progresss */
    if (chp->ccw_cmd != 0 || (chp->ccw_flags & (FLAG_DC|FLAG_CC)) != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "haltxio busy return CC4 chsa %x chan %x\n", chsa, chan);
        fprintf(stderr,  "HIO haltxio busy return CC4 chsa %x chan %x\r\n", chsa, chan);
        /* reset the DC or CC bits to force completion after current IOCD */
        chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);       /* reset chaining bits */
        dev_status[chsa] |= STATUS_ECHO;            /* show we stopped the cmd */
        *status = 0;                                /* not busy, no CC */
        goto hioret;                                /* just busy CC4 */
    }
    /* the channel is not busy, so return OK */
    *status = CC2BIT;                               /* INCH status stored, so CC2 TRY */
hioret:
    sim_debug(DEBUG_CMD, &cpu_dev, "$$$ HIO END chsa %x chan %x cmd %x flags %x status %x\n",
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
    chan = (spadent & 0xff00) >> 8;                 /* get real channel */
    chsa = (chan << 8) | (lchsa & 0xff);            /* merge sa to real channel */
    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */

    /* check for a Command or data chain operation in progresss */
    if (chp->ccw_cmd != 0 || (chp->ccw_flags & (FLAG_DC|FLAG_CC)) != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "grabxio busy return CC4 chsa %x chan %x\n", chsa, chan);
        *status = CC4BIT;                           /* busy, so CC4 */
        return SCPE_OK;                             /* CC4 all OK  */
    }
    *status = 0;                                    /* not busy, no CC */
    sim_debug(DEBUG_CMD, &cpu_dev, "grabxio chsa %x chan %08x\n", chsa, chan);
    return 0;
}

/* reset controller XIO */
t_stat rsctlxio(uint16 lchsa, uint32 *status) {       /* reset controller XIO */
    int     chan = get_chan(lchsa);
    uint32  spadent;
    uint16  chsa;
    CHANP   *chp;

    /* get the device entry for the logical channel in SPAD */
    spadent = SPAD[chan];                           /* get spad device entry for logical channel */
    chan = (spadent & 0xff00) >> 8;                 /* get real channel */
    chsa = (chan << 8) | (lchsa & 0xff);            /* merge sa to real channel */
    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */

    *status = 0;                                    /* not busy, no CC */
    sim_debug(DEBUG_CMD, &cpu_dev, "rsctlxio chsa %x chan %08x\n", chsa, chan);
    return 0;
}

/* boot from the device (ch/sa) the caller specified */
/* on CPU reset, the cpu has set the IOCD data at location 0-4 */
t_stat chan_boot(uint16 chsa, DEVICE *dptr) {
    int     chan = get_chan(chsa);
    DIB     *dibp = dev_unit[chsa];
    CHANP   *chp = dibp->chan_prg;
    int     i,j;

    sim_debug(DEBUG_EXP, &cpu_dev, "Channel Boot chan/device addr %x\n", chsa);
    if (dibp == 0)                                  /* if no channel or device, error */
        return SCPE_IOERR;                          /* error */
    if (dibp->chan_prg == NULL)                     /* must have channel information for each device */
        return SCPE_IOERR;                          /* error */
    chp = find_chanp_ptr(chsa);                     /* find the chanp pointer */

    dev_status[chsa&0x7f00] = 0;                    /* clear the channel status location */
    dev_status[chsa] = 0;                           /* device status too */
    chp->chan_status = 0;                           /* clear the channel status */
    chp->chan_dev = chsa;                           /* save our address (ch/sa) */
    chp->chan_byte = BUFF_EMPTY;                    /* no data yet */
    chp->ccw_addr = 0;                              /* start loading at loc 0 */
    chp->chan_caw = 0x0;                            /* set IOCD address to memory location 0 */
    chp->ccw_count = 0;                             /* channel byte count 0 bytes*/
    chp->ccw_flags = 0;                             /* Command chain and supress incorrect length */
    chp->ccw_cmd = 0;                               /* read command */

    sim_debug(DEBUG_CMD, &cpu_dev, "Channel Boot calling load_ccw chan %02x status %08x\n",
        chan, chp->chan_status);

    /* start processing the boot IOCL at loc 0 */
    if (load_ccw(chp, 0)) {                         /* load IOCL starting from location 0 */
        sim_debug(DEBUG_CMD, &cpu_dev, "Channel Boot Error return from load_ccw chan %02x status %08x\n",
            chan, chp->chan_status);
        dev_status[chsa] = 0;                       /* no device status */
        chp->ccw_flags = 0;                         /* clear the command flags */
        return SCPE_IOERR;                          /* return error */
    }
    sim_debug(DEBUG_CMD, &cpu_dev, "Channel Boot OK return from load_ccw chsa %02x status %08x\n",
        chsa, chp->chan_status);
    loading = chsa;                                 /* show we are loading from the boot device */
    return SCPE_OK;                                 /* all OK */
}

/* Scan all channels and see if one is ready to start or has
   interrupt pending.
*/
uint32 scan_chan(void) {
    int         i,j;
    uint32      chsa = 0;                           /* No device */
    uint32      chan;                               /* channel num 0-7f */
    uint32      tempa;                              /* icb address */
    uint32      chan_ivl;                           /* int level table address */
    int         lev;                                /* interrupt level */
    uint32      chan_icba;                          /* int level context block address */
    CHANP       *chp;                               /* channel prog pointer */
    DIB         *dibp;                              /* DIB pointer */

    /* see if we are able to look for ints */
    if ((CPUSTATUS & 0x80) == 0) {                  /* are interrupts blocked */
        /* ints not blocked, so look for highest requesting interrupt */
        for (i=0; i<112; i++) {
            if (INTS[i]&INTS_ACT)                   /* look for level active */
                break;                              /* this level active, so stop looking */
            if (SPAD[i+0x80] == 0)                  /* not initialize? */
                continue;                           /* skip this one */
            if (SPAD[i+0x80] == 0xffffffff)         /* not initialize? */
                continue;                           /* skip this one */
            /* see if there is pending status for this channel */
            /* if there is and the level is not requesting, do it */
            if ((INTS[i] & INTS_ENAB) && !(INTS[i] & INTS_REQ)) {
                /* get the device entry for the logical channel in SPAD */
                chan = (SPAD[i+0x80] & 0xff00);     /* get real channel and zero sa */
                dibp = dev_unit[chan];              /* get the device information pointer */
                if (dibp == 0)
                    continue;                       /* skip unconfigured channel */
                /* see if the FIFO is empty */
                if (dibp->chan_fifo_in != dibp->chan_fifo_out) {
                    uint32 sw1, sw2;
                    /* fifo is not empty, so post status and request an interrupt */
                    if ((FIFO_Get(chan, &sw1) == 0) && (FIFO_Get(chan, &sw2) == 0)) {
                        /* we have status to return, do it now */
                        chp = find_chanp_ptr(chan);     /* find the chanp pointer for channel */
                        /* get the address of the interrupt IVL table in main memory */
                        chan_ivl = SPAD[0xf1] + (i<<2); /* contents of spad f1 points to chan ivl in mem */
                        chan_icba = M[chan_ivl >> 2];   /* get the interrupt context block addr in memory */
                        tempa = chp->chan_inch_addr;    /* get inch status buffer address */
                        M[tempa >> 2] = sw1;            /* save sa & IOCD address in status WD 1 loc */
                        /* save the status to memory */
                        M[(tempa+4) >> 2] = sw2;        /* save status and residual count in status WD 2 loc */
                        /* now store the status dw address into word 5 of the ICB for the channel */
                        M[(chan_icba + 20) >> 2] = tempa | BIT1;    /* post sw addr in ICB+5w & set CC2 in SW */
                        INTS[i] |= INTS_REQ;            /* turn on channel interrupt request */
                    }
                }
            }
            /* look for the highest requesting interrupt */
            /* that is enabled */
            if (((INTS[i] & INTS_ENAB) && (INTS[i] & INTS_REQ)) ||
                ((SPAD[i+0x80] & INTS_ENAB) && (INTS[i] & INTS_REQ))) {
                /* requesting, make active and turn off request flag */
                INTS[i] &= ~INTS_REQ;               /* turn off request */
                INTS[i] |= INTS_ACT;                /* turn on active */
                SPAD[i+0x80] |= SINT_ACT;           /* show active in SPAD too */
                /* make sure both enabled too */
                INTS[i] |= INTS_ENAB;               /* turn on enable */
                SPAD[i+0x80] |= SINT_ENAB;          /* show enabled in SPAD too */
                /* get the address of the interrupt IVL table in main memory */
                chan_ivl = SPAD[0xf1] + (i<<2);     /* contents of spad f1 points to chan ivl in mem */
                chan_icba = M[chan_ivl >> 2];       /* get the interrupt context block addr in memory */
                sim_debug(DEBUG_EXP, &cpu_dev, "scan_chan INTS REQ irq %x found chan_icba %x INTS %x\n",
                    i, chan_icba, INTS[i]);
                return(chan_icba);                  /* return ICB address */
            }
        }
    }
    if (irq_pend == 0)                              /* pending int? */
        return 0;                                   /* no, so just return */
    irq_pend = 0;                                   /* not pending anymore */

    /* loop through all the channels/units for channel with pending I/O completion */
    for (i = 0; sim_devices[i] != NULL; i++) {
        DEVICE  *dptr = sim_devices[i];             /* get pointer to configured device */
        DIB     *dibp = (DIB *)dptr->ctxt;          /* get pointer to Device Information Block for this device */
        UNIT    *uptr = dptr->units;                /* get pointer to units defined for this device */

        if (dibp == NULL)                           /* If no DIB, not channel device */
            continue;
        if (dptr->flags & DEV_DIS) {                /* Skip disabled devices */
            continue;
        }
        if ((chp = (CHANP *)dibp->chan_prg) == NULL)/* must have channel information for each device */
            continue;
        /* Check if address is in unit or dev entry */
        for (j = 0; j < dibp->numunits; j++) {      /* loop through unit entries */
            chsa = GET_UADDR(uptr->u3);             /* ch/sa value */

            /* If channel end, check if we should continue */
            if (chp->chan_status & STATUS_CEND) {   /* do we have channel end */
                if (chp->ccw_flags & FLAG_CC) {     /* command chain flag */
                    /* we have channel end and CC flag, continue channel prog */
                    if (chp->chan_status & STATUS_DEND) {   /* device end? */
                        (void)load_ccw(chp, 1);     /* go load the next IOCB */
                    } else
                        irq_pend = 1;               /* still pending int */
                } else {
                    /* we have channel end and no CC flag, end command */
                    chsa = chp->chan_dev;           /* get the chan/sa */
                    dev_status[chsa] = 0;           /* no device status anymore */
                    /* handle case where we are loading the O/S on boot */
                    if (loading) {
                        if (chp->chan_status & 0x3f03) {    /* check if any channel errors */
                            return 0;               /* yes, just return */
                        }
                        irq_pend = 0;               /* no pending int */
                        chp->chan_status = 0;       /* no channel status */
                        return chsa;                /* if loading, just channel number */
                    }
                    /* we are not loading, but have completed channel program */
                    store_csw(chp);                 /* store the status */
                    lev = find_int_lev(chsa);       /* get interrupt level */
                    if (lev == 0) {
                        irq_pend = 1;               /* still pending int */
                        return 0;                   /* just return */
                    }
                    irq_pend = 1;                   /* still pending int */
                    return 0;                       /* just return */
                }
            }
            uptr++;                                 /* next UNIT pointer */
            chp++;                                  /* next CHANP pointer */
        }
    }
    return 0;                                       /* done */
}

/* set up the devices configured into the simulator */
/* only devices with a DIB will be processed */
t_stat chan_set_devs() {
    int i, j;

    for(i = 0; i < MAX_DEV; i++) {
        dev_unit[i] = NULL;                         /* clear Device pointer array */
    }
    /* Build channel array */
    for (i = 0; sim_devices[i] != NULL; i++) {
        DEVICE  *dptr = sim_devices[i];             /* get pointer to next configured device */
        UNIT    *uptr = dptr->units;                /* get pointer to units defined for this device */
        DIB     *dibp = (DIB *)dptr->ctxt;          /* get pointer to Device Information Block for this device */
        CHANP   *chp;                               /* channel program pointer */
        int     chsa;                               /* addr of device chan & subaddress */

        if (dibp == NULL)                           /* If no DIB, not channel device */
            continue;
        if (dptr->flags & DEV_DIS) {                /* Skip disabled devices */
            continue;
        }
        if ((chp = (CHANP *)dibp->chan_prg) == NULL)/* must have channel information for each device */
            continue;
        /* Check if address is in unit or dev entry */
        for (j = 0; j < dptr->numunits; j++) {      /* loop through unit entries */
            chsa = GET_UADDR(uptr->u3);             /* ch/sa value */
            /* zero some channel data loc's for device */
            dev_status[chsa] = 0;                   /* zero device status flags */
            dev_status[chsa&0x7f00] = 0;            /* clear the channel status location */
            dev_status[chsa] = 0;                   /* device status too */
            chp->chan_status = 0;                   /* clear the channel status */
            chp->chan_dev = chsa;                   /* save our address (ch/sa) */
            chp->chan_byte = BUFF_EMPTY;            /* no data yet */
            chp->ccw_addr = 0;                      /* start loading at loc 0 */
            chp->chan_caw = 0;                      /* set IOCD address to memory location 0 */
            chp->ccw_count = 0;                     /* channel byte count 0 bytes*/
            chp->ccw_flags = 0;                     /* Command chain and supress incorrect length */
            chp->ccw_cmd = 0;                       /* read command */
            chp->chan_inch_addr = 0;                /* clear address of stat dw in memory */
            if ((uptr->flags & UNIT_DIS) == 0)      /* is unit marked disabled? */
                dev_unit[chsa] = dibp;              /* no, save the dib address */
            if (dibp->dev_ini != NULL)              /* if there is an init routine, call it now */
                dibp->dev_ini(uptr, 1);             /* init the channel */
            uptr++;                                 /* next UNIT pointer */
            chp++;                                  /* next CHANP pointer */
        }
    }
    return SCPE_OK;                                 /* all is OK */
}

/* Validate and set the device onto a given channel */
t_stat set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc) {
    DEVICE             *dptr;
    DIB                *dibp;
    t_value             newdev;
    t_stat              r;
    int                 i;
    int                 devaddr;

    if (cptr == NULL)                               /* is there a UNIT name specified */
        return SCPE_ARG;                            /* no, arg error */
    if (uptr == NULL)                               /* is there a UNIT pointer */
        return SCPE_IERR;                           /* no, arg error */
    dptr = find_dev_from_unit(uptr);                /* find the device from unit pointer */
    if (dptr == NULL)                               /* device not found, so error */
        return SCPE_IERR;                           /* error */

    dibp = (DIB *)dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;

    newdev = get_uint(cptr, 16, 0xfff, &r);

    if (r != SCPE_OK)
        return r;

    if ((newdev >> 8) > channels) 
        return SCPE_ARG;

    if (newdev >= MAX_DEV)
        return SCPE_ARG;

    devaddr = GET_UADDR(uptr->u3);

    /* Clear out existing entry */
    if (dptr->flags & DEV_UADDR) {
        dev_unit[devaddr] = NULL;
    } else {
        devaddr &= dibp->mask | 0x700;
        for (i = 0; i < dibp->numunits; i++)
             dev_unit[devaddr + i] = NULL;
    }

    /* Check if device already at newdev */
    if (dptr->flags & DEV_UADDR) {
        if (dev_unit[newdev] != NULL)
            r = SCPE_ARG;
    } else {
        newdev &= dibp->mask | 0x700;
        for (i = 0; i < dibp->numunits; i++) {
             if (dev_unit[newdev + i] != NULL)
                r = SCPE_ARG;
        }
    }

    /* If not, point to new dev, else restore old */
    if (r == SCPE_OK)
       devaddr = newdev;

    /* Update device entry */
    if (dptr->flags & DEV_UADDR) {
        dev_unit[devaddr] = dibp;
        uptr->u3 &= ~UNIT_ADDR(0x7ff);
        uptr->u3 |= UNIT_ADDR(devaddr);
        fprintf(stderr, "Set dev %x\r\n", GET_UADDR(uptr->u3));
    } else {
        for (i = 0; i < dibp->numunits; i++)  {
             dev_unit[devaddr + i] = dibp;
             uptr = &((dibp->units)[i]);
             uptr->u3 &= ~UNIT_ADDR(0x7ff);
             uptr->u3 |= UNIT_ADDR(devaddr + i);
             fprintf(stderr, "Set dev %x\r\n", GET_UADDR(uptr->u3));
        }
    }
    return r;
}

t_stat show_dev_addr(FILE *st, UNIT *uptr, int32 v, CONST void *desc) {
    DEVICE      *dptr;
    int         chsa;

    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    chsa = GET_UADDR(uptr->u3);
    fprintf(st, "%04x", chsa);
    return SCPE_OK;
}
