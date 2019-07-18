/* sel32_chan.c: Sel 32 Channel functions.

   Copyright (c) 2018, Richard Cornwell, James C. Bevier

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


/* Handle Class E and F channel I/O operations */
#include "sel32_defs.h"
#include "sim_defs.h"

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

#define AMASK            0x00ffffff
#define PMASK            0xf0000000         /* Storage protection mask */

extern uint32   M[];                        /* our memory */
extern uint32   SPAD[];                     /* CPU scratchpad memory */

uint32      chan_icb[MAX_CHAN];             /* Interrupt Context Block address in memory */
uint32      chan_inch_addr[MAX_CHAN];       /* Channel status dw in memory */
uint32      caw[MAX_CHAN];                  /* Channel command address word */
uint32      ccw_addr[MAX_CHAN];             /* Channel address */
uint16      ccw_count[MAX_CHAN];            /* Channel count */
uint8       ccw_cmd[MAX_CHAN];              /* Channel command and flags */
uint16      ccw_flags[MAX_CHAN];            /* Channel flags */
uint16      chan_status[MAX_CHAN];          /* Channel status */
uint16      chan_dev[MAX_CHAN];             /* Device on channel */
uint32      chan_buf[MAX_CHAN];             /* Channel data buffer */
uint8       chan_byte[MAX_CHAN];            /* Current byte, dirty/full */
DIB         *dev_unit[MAX_DEV];             /* Pointer to Device info block */
uint8       dev_status[MAX_DEV];            /* last device status flags */
uint16      loading;                        /* set when booting */

/* forward definitions */
int  find_subchan(uint16 device);           /* look up device to find subchannel device is on */
int  chan_read_byte(uint16 chan, uint8 *data);
int  chan_write_byte(uint16 chan, uint8 *data);
void set_devattn(uint16 addr, uint8 flags);
void chan_end(uint16 chan, uint8 flags);
t_stat startxio(uint16 addr, uint32 *status);
t_stat testxio(uint16 addr, uint32 *status);
t_stat stoptxio(uint16 addr, uint32 *status);
int testchan(uint16 channel);
uint32 scan_chan(void);
t_stat chan_boot(uint16 addr, DEVICE *dptr);
t_stat chan_set_devs();
t_stat set_dev_addr(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_dev_addr(FILE * st, UNIT * uptr, int32 v, CONST void *desc);

/* Find unit pointer for given device (ch/sa) */
UNIT *find_chan_dev(uint16 addr) {
    struct dib  *dibp;          /* DIB pointer */
    UNIT        *uptr;          /* UNIT pointer */
    int         i;

fprintf(stderr, "find_chan_dev addr %0x\r\n", addr);
    dibp = dev_unit[addr];              /* get DIB pointer from device pointers */
    if (dibp == 0)                      /* if zero, not defined on system */
        return NULL;                    /* tell caller */

    uptr = dibp->units;                 /* get the pointer to the units on this channel */
fprintf(stderr, "find_chan_dev addr %x units %x \r\n", addr, dibp->numunits);
    for (i = 0; i < dibp->numunits; i++) {  /* search through units to get a match */
        if (addr == GET_UADDR(uptr->u3))    /* does ch/sa match? */
            return uptr;                /* return the pointer */
        uptr++;                         /* next unit */
    }
    return NULL;                /* device not found on system */
}

/* extract channel from device definition (ch/sa) */
/* ret chan = OK */
/* ret -1   = error */
int find_subchan(uint16 device) {
    int     chan;

//fprintf(stderr, "find_subchan device %0x\r\n", device);
    if (device > MAX_DEV)               /* if overlimit of system, error */
        return -1;                      /* error */
    chan = (device >> 8) & 0x7F;        /* channel is in upper nibble */
    if (chan > channels)                /* should not happen */
        return -1;                      /* error */
    return chan;                        /* return chan 0-7f */
}

/* Read a full word into memory.
 * Return 1 if fail.
 * Return 0 if success.
 */
int
readfull(int chan, uint32 addr, uint32 *word) {
    int sk, k;
    if ((addr & AMASK) > MEMSIZE) {
        chan_status[chan] |= STATUS_PCHK;
        return 1;
    }
    addr &= AMASK;
    addr >>= 2;
    *word = M[addr];
    return 0;
}

/* Read a word into the channel buffer.
 * Return 1 if fail.
 * Return 0 if success.
 */
int
readbuff(int chan) {
    int k;
    uint32 addr = ccw_addr[chan];
    if ((addr & AMASK) > MEMSIZE) {
        chan_status[chan] |= STATUS_PCHK;
        chan_byte[chan] = BUFF_CHNEND;
        irq_pend = 1;
        return 1;
    }
    addr &= AMASK;
    addr >>= 2;
    chan_buf[chan] = M[addr];
    sim_debug(DEBUG_DATA, &cpu_dev, "Channel write %02x %06x %08x %08x '",
          chan, ccw_addr[chan] & 0xFFFFFC, chan_buf[chan], ccw_count[chan]);
    for(k = 24; k >= 0; k -= 8) {
        char ch = (chan_buf[chan] >> k) & 0xFF;
        if (ch < 0x20 || ch == 0xff)
           ch = '.';
        sim_debug(DEBUG_DATA, &cpu_dev, "%c", ch);
    }

    sim_debug(DEBUG_DATA, & cpu_dev, "'\n");
    return 0;
}

/* Write channel buffer to memory.
 * Return 1 if fail.
 * Return 0 if success.
 */
int
writebuff(int chan) {
    int k;
    uint32 addr = ccw_addr[chan];
    if ((addr & AMASK) > MEMSIZE) {
        chan_status[chan] |= STATUS_PCHK;
        chan_byte[chan] = BUFF_CHNEND;
        irq_pend = 1;
        return 1;
    }
    addr &= AMASK;
    addr >>= 2;
    M[addr] = chan_buf[chan];
    sim_debug(DEBUG_DATA, &cpu_dev, "Channel readf %02x %06x %08x %08x '",
          chan, ccw_addr[chan] & 0xFFFFFC, chan_buf[chan], ccw_count[chan]);
    for(k = 24; k >= 0; k -= 8) {
        char ch = (chan_buf[chan] >> k) & 0xFF;
        if (ch < 0x20 || ch == 0xff)
            ch = '.';
        sim_debug(DEBUG_DATA, &cpu_dev, "%c", ch);
    }
    sim_debug(DEBUG_DATA, &cpu_dev, "'\n");
    return 0;
}

/* load in the IOCD and process the commands */
/* return = 0 OK */
/* return = 1 error, chan_status will have reason */
int load_ccw(uint16 chan, int tic_ok) {
    uint32      word;
    int         cmd = 0;
    UNIT        *uptr;

loop:
    /* Abort if channel not on double boundry */
    if ((caw[chan] & 0x7) != 0) {
        chan_status[chan] |= STATUS_PCHK;       /* Not dbl WD bounded, program check */
        return 1;
    }
    /* Abort if we have any errors */
    if (chan_status[chan] & 0x7f)               /* check channel status */
        return 1;

    /* Check if we have status modifier set */
    if (chan_status[chan] & STATUS_MOD) {
        caw[chan]+=8;                   /* move to next IOCD */
        caw[chan] &= PMASK|AMASK;       /* Mask overflow bits (4-7) */
        chan_status[chan] &= ~STATUS_MOD;   /* turn off status modifier flag */
    }

    /* Read in first or next CCW */
    if (readfull(chan, caw[chan], &word) != 0) {    /* read word from memory */
        chan_status[chan] |= STATUS_PCHK;       /* memory read error, program check */
        return 1;                               /* error return */
    }

fprintf(stderr, "Channel read ccw %02x %06x %08x\r\n", chan, caw[chan], word);
sim_debug(DEBUG_CMD, &cpu_dev, "Channel read ccw  %02x %06x %08x\n", chan, caw[chan], word);
    /* TIC can't follow TIC nor be first in chain */
    if (((word >> 24) & 0xf) == CMD_TIC) {
        if (tic_ok) {
            caw[chan] = (caw[chan] & PMASK) | (word & AMASK);   /* get new IOCD address */
            tic_ok = 0;             /* another tic not allowed */
            goto loop;              /* restart the IOCD processing */
        }
        chan_status[chan] |= STATUS_PCHK;       /* program check for invalid tic */
        irq_pend = 1;               /* status pending */
        return 1;                   /* error return */
    }
    caw[chan] += 4;                 /* point to 2nd word of the IOCD */
    caw[chan] &= PMASK|AMASK;       /* Mask overflow bits */

    /* Check if not chaining data */
    if ((ccw_flags[chan] & FLAG_DC) == 0) {
        ccw_cmd[chan] = (word >> 24) & 0xff;    /* not DC, so set command from IOCD WD 1 */
        cmd = 1;                    /* show we have a command */
    }
    /* Set up for this command */
    ccw_addr[chan] = word & AMASK;          /* set the data address */
    ccw_addr[chan] |= caw[chan] & PMASK;    /* Copy bits 0-3 from old IOCD pointer into data address ?? */
    readfull(chan, caw[chan], &word);       /* get IOCD WD 2 */

fprintf(stderr, "Channel read ccw2 %02x %06x %08x\r\n", chan, caw[chan], word);
sim_debug(DEBUG_CMD, &cpu_dev, "Channel read ccw2 %02x %06x %08x\n", chan, caw[chan], word);
    caw[chan]+=4;                           /* next IOCD address */
    caw[chan] &= PMASK|AMASK;               /* Mask overflow bits */
    ccw_count[chan] = word & 0xffff;        /* get 16 bit byte count from IOCD WD 2*/
    ccw_flags[chan] = (word >> 16) & 0xffff;    /* get flags from bits 0-7 of WD 2 of IOCD */
    chan_byte[chan] = BUFF_EMPTY;           /* no bytes transferred yet */
    if (ccw_flags[chan] & FLAG_PCI) {       /* do we have prog controlled int? */
        chan_status[chan] |= STATUS_PCI;    /* set PCI flag in status */
        irq_pend = 1;                       /* interrupt pending */
    }

    /* Check invalid count */
    if (ccw_count[chan] == 0) {             /* see if byte count is zero */
        chan_status[chan] |= STATUS_PCHK;   /* program check if it is */
        irq_pend = 1;                       /* status pending int */
        return 1;                           /* error return */
    }
    if (cmd) {                              /* see if we need to process command */
        DIB *dibp = dev_unit[chan_dev[chan]];   /* get the device pointer */
        /* Check if INCH command */
        if ((ccw_cmd[chan] & 0xF) == 0) {   /* see if this an initialize channel cmd */
fprintf(stderr, "load_ccw inch %x saved chan %0x status %0.8x\r\n", ccw_addr[chan], chan, chan_status[chan]);
            chan_inch_addr[chan] = ccw_addr[chan];  /* save channel status dw address in memory */
            /* just drop through and call the device startcmd function */
            /* it should just return SEN_CHNEND and SNS_DEVEND status */
        }
        uptr = find_chan_dev(chan_dev[chan]);   /* find the unit pointer */
        if (uptr == 0)
            return 1;                       /* if none, error */
        chan_status[chan] &= 0xff;          /* clear device sense status */
        /* call the device startcmd function to process command */
        /* store sense status in upper byte of status */
        chan_status[chan] |= dibp->start_cmd(uptr, chan, ccw_cmd[chan]) << 8;
fprintf(stderr, "load_ccw after start_cmd chan %0x status %0.8x\r\n", chan, chan_status[chan]);
        /* see if bad status */
        if (chan_status[chan] & (STATUS_ATTN|STATUS_CHECK|STATUS_EXPT)) {
            chan_status[chan] |= STATUS_CEND;   /* channel end status */
            ccw_flags[chan] = 0;                /* no flags */
            ccw_cmd[chan] = 0;                  /* stop IOCD processing */
            irq_pend = 1;                       /* int coming */
fprintf(stderr, "load_ccw bad1 chan %0x status %0.8x\r\n", chan, chan_status[chan]);
            return 1;                           /* error return */
        }
        /* see if command completed */
        if (chan_status[chan] & (STATUS_DEND|STATUS_CEND)) {
            chan_status[chan] |= STATUS_CEND;   /* set channel end status */
            chan_byte[chan] = BUFF_NEWCMD;      /* ready for new cmd */
            ccw_cmd[chan] = 0;                  /* stop IOCD processing */
            irq_pend = 1;                       /* int coming */
fprintf(stderr, "load_ccw good chan %0x status %0.8x %x\r\n", chan, chan_status[chan]);
        }
    }
    return 0;           /* good return */
}

/* read byte from memory */
int
chan_read_byte(uint16 addr, uint8 *data) {
    int     chan = find_subchan(addr);      /* get the channel number */
    int     byte;
    int     k;

    /* Abort if we have any errors */
    if (chan < 0)
        return 1;
    if (chan_status[chan] & 0x7f) 
        return 1;
    if ((ccw_cmd[chan] & 0x1)  == 0) {
        return 1;
    }
    if (chan_byte[chan] == BUFF_CHNEND)
        return 1;
    if (ccw_count[chan] == 0) {
         if ((ccw_flags[chan] & FLAG_DC) == 0) {
            chan_status[chan] |= STATUS_CEND;
            chan_byte[chan] = BUFF_CHNEND;
            sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_read_end\n");
            return 1;
         } else {
            if (load_ccw(chan, 1)) 
                return 1;
         }
    }
    if (chan_byte[chan] == BUFF_EMPTY) {
         if (readbuff(chan)) 
            return 1;
         chan_byte[chan] = ccw_addr[chan] & 0x3;
         ccw_addr[chan] += 4 - chan_byte[chan];
    }
    ccw_count[chan]--;
    byte = (chan_buf[chan] >>  (8 * (3 - (chan_byte[chan] & 0x3)))) & 0xff;
    chan_byte[chan]++;
    *data = byte;
    return 0;
}

/* write byte to memory */
int
chan_write_byte(uint16 addr, uint8 *data) {
    int     chan = find_subchan(addr);      /* get the channel number */
    int     byte;
    int     offset;
    int     k;
    uint32  mask;

    /* Abort if we have any errors */
    if (chan < 0)
        return 1;
    if (chan_status[chan] & 0x7f) 
        return 1;
    if ((ccw_cmd[chan] & 0x1)  != 0) {
        return 1;
    }
    if (chan_byte[chan] == BUFF_CHNEND) {
        if ((ccw_flags[chan] & FLAG_SLI) == 0) {
            chan_status[chan] |= STATUS_LENGTH;
        }
        return 1;
    }
    if (ccw_count[chan] == 0) {
        if (chan_byte[chan] & BUFF_DIRTY) {
            if (writebuff(chan)) 
                return 1;
        }
        if ((ccw_flags[chan] & FLAG_DC) == 0) {
            chan_byte[chan] = BUFF_CHNEND;
            if ((ccw_flags[chan] & FLAG_SLI) == 0) {
                sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_write_ length\n");
                chan_status[chan] |= STATUS_LENGTH;
            }
            sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_write_end\n");
            return 1;
        }
        if (load_ccw(chan, 1))
            return 1;
    }
    if (ccw_flags[chan] & FLAG_SKIP) {
        ccw_count[chan]--;
        chan_byte[chan] = BUFF_EMPTY;
        if ((ccw_cmd[chan] & 0xf) == CMD_RDBWD)
            ccw_addr[chan]--;
        else
            ccw_addr[chan]++;
        return 0;
    }
    if (chan_byte[chan] == (BUFF_EMPTY|BUFF_DIRTY)) {
         if (writebuff(chan)) 
            return 1;
         if ((ccw_cmd[chan] & 0xf) == CMD_RDBWD)
            ccw_addr[chan] -= 1 + (ccw_addr[chan] & 0x3);
         else
            ccw_addr[chan] += 4 - (ccw_addr[chan] & 0x3);
         chan_byte[chan] = BUFF_EMPTY;
    }
    if (chan_byte[chan] == BUFF_EMPTY) {
        if (readbuff(chan))
            return 1;
        chan_byte[chan] = ccw_addr[chan] & 0x3;
    }
    ccw_count[chan]--;
    offset = 8 * (chan_byte[chan] & 0x3);
    mask = 0xff000000 >> offset;
    chan_buf[chan] &= ~mask;
    chan_buf[chan] |= ((uint32)(*data)) << (24 - offset);
    if ((ccw_cmd[chan] & 0xf) == CMD_RDBWD) {
        if (chan_byte[chan] & 0x3)
            chan_byte[chan]--;
        else
            chan_byte[chan] = BUFF_EMPTY;
    } else
        chan_byte[chan]++;
    chan_byte[chan] |= BUFF_DIRTY;
    return 0;
}

/* post interrupt for specified channel */
void set_devattn(uint16 addr, uint8 flags) {
    int     chan = find_subchan(addr);      /* get the channel number */

fprintf(stderr, "set_devattn addr %0x flags %x\r\n", addr, flags);
    if (chan < 0)
        return;
    if (chan_dev[chan] == addr && (chan_status[chan] & STATUS_CEND) != 0 &&
            (flags & SNS_DEVEND) != 0) {
        chan_status[chan] |= ((uint16)flags) << 8;
    } else
        dev_status[addr] = flags;
    sim_debug(DEBUG_EXP, &cpu_dev, "set_devattn(%x, %x) %x\n", 
                 addr, flags, chan_dev[chan]);
    irq_pend = 1;
}

/* channel operation completed */
void chan_end(uint16 addr, uint8 flags) {
    int     chan = find_subchan(addr);      /* get the channel number */

fprintf(stderr, "chan_end addr %0x flags %x\r\n", addr, flags);
    if (chan < 0)
        return;

    sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_end(%x, %x) %x\n", addr, flags, ccw_count[chan]);
    if (chan_byte[chan] & BUFF_DIRTY) {
        if (writebuff(chan))
            return;
        chan_byte[chan] = BUFF_EMPTY;
    }
    chan_status[chan] |= STATUS_CEND;
    chan_status[chan] |= ((uint16)flags) << 8;
    ccw_cmd[chan] = 0;
    if (ccw_count[chan] != 0 &&  (ccw_flags[chan] & FLAG_SLI) == 0) {
        sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_end length\n");
        chan_status[chan] |= STATUS_LENGTH;
        ccw_flags[chan] = 0;
    }
    if (flags & (SNS_ATTN|SNS_UNITCHK|SNS_UNITEXP)) {
        ccw_flags[chan] = 0;
    }

    if (chan_status[chan] & (STATUS_DEND|STATUS_CEND)) {
        chan_byte[chan] = BUFF_NEWCMD;

        while ((ccw_flags[chan] & FLAG_DC)) {
            if (load_ccw(chan, 1)) 
                break;
            if ((ccw_flags[chan] & FLAG_SLI) == 0) {
                sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_end length\n");
                chan_status[chan] |= STATUS_LENGTH;
                ccw_flags[chan] = 0;
            }
        }
    }

    irq_pend = 1;
}

/* store the device status into the status DW in memory */
/* the INCH command provides the status address in memory */
int store_csw(uint16 chan) {
    uint32 maddr = chan_inch_addr[chan];        /* get address of stat dw */
    uint32 addr = chan_dev[chan];               /* get ch/sa information */

fprintf(stderr, "store_csw chan %x maddr %x addr %x\r\n", chan, maddr, addr);
    addr = (addr & 0xff) << 24;                 /* put sub address in byte 0 */
    M[maddr >> 2] = caw[chan] | addr;           /* IOCD address and sub address */
    /* save 16 bit channel status and residual byte count in SW 2 */
    M[(maddr+4) >> 2] = (((uint32)ccw_count[chan])) | ((uint32)chan_status[chan]<<16);
    chan_status[chan] = 0;                      /* no status anymore */
    chan_dev[chan] = 0;                         /* no ch/sa device definition */
sim_debug(DEBUG_EXP, &cpu_dev, "Channel store csw  %02x %06x %08x\n",
          chan, M[maddr>>2], M[(maddr+4)>>2]);
    /* now store the status dw address into word 5 of the ICB for the channel */
    M[(chan_icb[chan] + 20) >> 2] = maddr;      /* post sw addr in ICB+5w */
    return chan_dev[chan];
}

/* SIO CC status returned to caller */
/* val condition */
/* 0   command accepted - no CC's */
/* 1   channel busy  - CC4 */
/* 2   channel inop or undefined (operator intervention required) - CC3 */
/* 3   sub channel busy CC3 + CC4 */
/* 4   status stored - CC2 */
/* 5   unsupported transaction  CC2 + CC4 */
/* 6   unassigned CC2 + CC3 */
/* 7   unassigned CC2 + CC3 + CC4 */
/* 8   command accepted - CC1 */
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
t_stat startxio(uint16 addr, uint32 *status) {
    int     chan = find_subchan(addr);      /* get the channel number */
    DIB     *dibp = dev_unit[addr];         /* get the device information pointer */
    UNIT    *uptr;
    uint32  chan_ivl;                       /* Interrupt Level ICB address for channel */
    uint32  iocla;                          /* I/O channel IOCL address int ICB */
    uint32  stata;                          /* I/O channel status location in ICB */
    uint32  tempa, inta, spadent;

fprintf(stderr, "chan startxio 1 addr %0x chan %x\r\n", addr, chan);
    if (chan < 0 || dibp == 0) {    /* no channel found, CC3 return */
        *status = CC3BIT;           /* not found, so CC3 */
        return SCPE_OK;             /* not found, CC3 */
    }
    uptr = find_chan_dev(addr);     /* find pointer to unit on channel */
    if (uptr == 0) {                /* if non found, CC3 on return */
        *status = CC3BIT;           /* not found, so CC3 */
        return SCPE_OK;             /* not found, CC3 */
    }
fprintf(stderr, "chan startxio 2 addr %0x chan %x\r\n", addr, chan);
    if ((uptr->flags & UNIT_ATT)  == 0) {   /* is unit already attached? */
        *status = CC3BIT;           /* not attached, so error CC3 */
        return SCPE_OK;             /* not found, CC3 */
    }
    /* see if interrupt is setup in SPAD and determine IVL for channel */
    spadent = SPAD[chan];           /* get spad device entry for channel */
fprintf(stderr, "startxio dev spad %0.8x addr %x chan %x\r\n", spadent, addr, chan);
    /* the startio opcode processing software has already checked for F class */
//  if (spadent & 0x0f000000 != 0x0f000000) {   /* I/O class in bits 4-7 */
//      *status = CC3BIT;           /* not valid entry, so CC3 */
//      return SYSTEMCHK_TRP;       /* not found, CC3 */
//  }
    inta = ((spadent & 0x007f0000) >> 16);  /* 1's complement of chan int level */
    inta = 127 - inta;              /* get positive int level */
    spadent = SPAD[inta + 0x80];    /* get interrupt spad entry */
fprintf(stderr, "startxio int spad %0.8x inta %x chan %x\r\n", spadent, inta, chan);
    /* get the address of the interrupt IVL in main memory */
    chan_ivl = SPAD[0xf1] + (inta<<2);  /* contents of spad f1 points to chan ivl in mem */
    chan_ivl = M[chan_ivl >> 2];    /* get the interrupt context block addr in memory */
    chan_icb[chan] = chan_ivl;      /* point to 6 wd ICB, iocla is in wd4 and status addr in w5 */
    iocla = M[(chan_ivl+16)>>2];    /* iocla is in wd 4 of ICB */
fprintf(stderr, "chan startxio busy test addr %0x chan %x cmd %x flags %x\r\n",
        addr, chan, ccw_cmd[chan], ccw_flags[chan]);

sim_debug(DEBUG_CMD, &cpu_dev, "SIO %x %x %x %x\n", addr, chan, ccw_cmd[chan], ccw_flags[chan]);
    /* check for a Command or data chain operation in progresss */
    if (ccw_cmd[chan] != 0 || (ccw_flags[chan] & (FLAG_DC|FLAG_CC)) != 0) {
fprintf(stderr, "startxio busy return CC4\r\n");
        *status = CC4BIT;           /* busy, so CC4 */
        return SCPE_OK;             /* just busy CC4 */
    }

    /* not busy, so start a new command */
    chan_status[chan] = 0;          /* no channel status yet */
    dev_status[addr] = 0;           /* no unit status either */
    caw[chan] = iocla;              /* get iocla address in memory */
    chan_dev[chan] = addr;          /* save the ch/sa too */
    /* set status words in memory to first IOCD information */
    tempa = chan_inch_addr[chan];   /* get inch status buffer address */
    M[tempa >> 2] = (addr & 0xff) << 24 | iocla;    /* sa & IOCD address to status */
    M[(tempa+2) >> 2] = 0;          /* null status and residual byte count */

    /* determine if channel DIB has a startio command processor */
    if (dibp->start_io != NULL) {           /* NULL if no startio function */
        chan_status[chan] = dibp->start_io(uptr, chan) << 8;    /* get status from device */
        if (chan_status[chan] != 0) {       /* see if channel status is ready */
            /* save the status double word to memory */
            /* for SEL32, this memory address must be supplied by software using */
            /* the SIO cmd 0 (INCH) to set the status memory doublewd location */ 
            store_csw(chan);                /* store the status in the inch status dw */
sim_debug(DEBUG_EXP, &cpu_dev, "Channel store csw  %02x %08x\r\n", chan, chan_status[chan]);
fprintf(stderr, "startxio status store csw CC4 chan %x cstat %x\r\n", chan, chan_status[chan]);
            chan_status[chan] = 0;      /* no status anymore */
            *status = CC2BIT;           /* status stored, so CC2 */
            return SCPE_OK;             /* CC2 (0x4) status stored */
        }
    }
    /* start processing the IOCD */
    if (load_ccw(chan, 0) || (chan_status[chan] & (STATUS_PCI))) {
fprintf(stderr, "chan startxio after load_ccw addr %0x status %x\r\n", addr, chan_status[chan]);
        store_csw(chan);                /* store the status in the inch status dw */
sim_debug(DEBUG_EXP, &cpu_dev, "Channel store csw  %02x %08x\n", chan, chan_status[chan]);
        chan_status[chan] &= ~STATUS_PCI;   /* remove PCI status bit */
        dev_status[addr] = 0;       /* no device status */
        *status = CC4BIT;           /* channel busy, so CC4 */
        return SCPE_OK;             /* CC4 (0x01) channel busy */
    }
    if (chan_status[chan] & STATUS_BUSY) {
fprintf(stderr, "chan startxio busy addr %0x status %x\r\n", addr, chan_status[chan]);
sim_debug(DEBUG_EXP, &cpu_dev, "Channel store csw  %02x %08x\n", chan, chan_status[chan]);
        store_csw(chan);            /* store the status in the inch status dw */
        M[tempa >> 2] = 0;          /* zero sa & IOCD address in status WD 1 */
        chan_dev[chan] = 0;         /* zero device info */
        chan_status[chan] = 0;      /* no channel status */
        dev_status[addr] = 0;       /* not unit status */
        ccw_cmd[chan] = 0;          /* no cmd either */
        *status = CC4BIT;           /* channel busy, so CC4 */
        return SCPE_OK;             /* CC4 (0x01) channel busy */
    }

fprintf(stderr, "chan startxio done addr %0x status %x\r\n", addr, chan_status[chan]);
    *status = 0;                    /* CCs = 0, SIO accepted */
    return SCPE_OK;                 /* No CC's all OK  */
}

t_stat testxio(uint16 addr, uint32 *status) {
    int          chan = find_subchan(addr);
    DIB         *dibp = dev_unit[addr];
    UNIT        *uptr;
//    uint8        status;

fprintf(stderr, "chan testxio  addr %0x chan %x\r\n", addr, chan);
#if 0
    if (chan < 0 || dibp == 0)
        return 3;
    uptr = find_chan_dev(addr);
    if (uptr == 0)
        return 3;
    if ((uptr->flags & UNIT_ATT)  == 0)
        return 3;
    if (ccw_cmd[chan] != 0 || (ccw_flags[chan] & (FLAG_DC|FLAG_CC)) != 0) 
        return 2;
    if (chan_dev[chan] != 0 && chan_dev[chan] != addr)
        return 2;
    if (ccw_cmd[chan] == 0 && chan_status[chan] != 0) {
        store_csw(chan);
        dev_status[addr] = 0;
        return 1;
    }
    if (dev_status[addr] != 0) {
        M[0x40 >> 2] = 0;
        M[0x44 >> 2] = ((uint32)dev_status[addr]) << 24;
        dev_status[addr] = 0;
        return 1;
    }
    chan_status[chan] = dibp->start_cmd(uptr, chan, 0) << 8;
    if (chan_status[chan] & (STATUS_ATTN|STATUS_CHECK|STATUS_EXPT)) {
        M[0x44 >> 2] = ((uint32)chan_status[chan]<<16) | M[0x44 >> 2] & 0xffff;
        chan_status[chan] = 0;
        dev_status[addr] = 0;
        return 1;
    }
    chan_status[chan] = 0;
#endif

    return 0;
}

t_stat stopxio(uint16 addr, uint32 *status) {
    int           chan = find_subchan(addr);
    DIB          *dibp = dev_unit[addr];
    UNIT         *uptr;
//    uint8         status;

fprintf(stderr, "chan stopxio  addr %0x chan %x\r\n", addr, chan);
#if 0
    if (chan < 0 || dibp == 0)
        return 3;
    uptr = find_chan_dev(chan_dev[chan]);
    if (uptr == 0)
        return 3;
    if (ccw_cmd[chan]) {
        chan_byte[chan] = BUFF_CHNEND;
        return 2;
    }
    if (dibp->halt_io != NULL)
        chan_status[chan] = dibp->halt_io(uptr) << 8;
#endif

    return 0;
}

int testchan(uint16 channel) {
    uint16         st = 0;
    channel >>= 8;
    if (channel == 0)
        return 0;
    if (channel > channels) 
        return 3;
    st = chan_status[subchannels + channel];
    if (st & STATUS_BUSY)
        return 2;
    if (st & (STATUS_ATTN|STATUS_PCI|STATUS_EXPT|STATUS_CHECK|
                  STATUS_PROT|STATUS_CDATA|STATUS_CCNTL|STATUS_INTER|
                  STATUS_CHAIN))
        return 1;
    return 0;
}

/* boot from the device (ch/sa) the caller specified */
t_stat chan_boot(uint16 addr, DEVICE *dptyr) {
    int          chan = find_subchan(addr);
    DIB         *dibp = dev_unit[addr];
    UNIT        *uptr;
    uint8        status;
    int          i;

fprintf(stderr, "chan_boot addr %0x\r\n", addr);
    if (chan < 0 || dibp == 0)          /* if no channel or device, error */
        return SCPE_IOERR;              /* error */
    for (i = 0; i < MAX_DEV; i++) {
        dev_status[i] = 0;              /* clear all of the device status locations */
    }
    for (i = 0; i < MAX_CHAN; i++) {    /* loop through all the channels */
        ccw_cmd[i] = 0;                 /* clear channel command and flags */
        ccw_flags[i] = 0;               /* clear channel flags */
//      chan_icb[i] = 0;                /* Interrupt Context Block address in memory */
//      chan_inch_addr[i] = 0;          /* Channel status dw in memory */
    }
    uptr = find_chan_dev(addr);         /* find the unit pointer */
    chan_status[chan] = 0;              /* clear the channel status */
    dev_status[addr] = 0;               /* device status too */
    caw[chan] = 0x8;                    /* set IOCD address to memory location 8 */
    chan_dev[chan] = addr;              /* save our address (ch/sa) */
    ccw_count[chan] = 24;               /* channel byte count */
    ccw_flags[chan] = FLAG_CC|FLAG_SLI; /* Command chain and supress incorrect length */
    ccw_addr[chan] = 0;                 /* start loading at loc 0 */
    chan_byte[chan] = BUFF_EMPTY;       /* do data yet */
    ccw_cmd[chan] = 0x2;                /* read command */
    chan_status[chan] &= 0xff;          /* zero all but status bits */
    /* now call the controller to boot the device */
    /* sense status is returned */
    chan_status[chan] |= dibp->start_cmd(uptr, chan, ccw_cmd[chan]) << 8;
fprintf(stderr, "chan_boot after start chan %0x status %0.8x\r\n", chan, chan_status[chan]);
    /* any bad bits set ? */
    if (chan_status[chan] & (STATUS_ATTN|STATUS_CHECK|STATUS_EXPT)) {
        ccw_flags[chan] = 0;            /* clear the command flags */
        return SCPE_IOERR;              /* return error */
    }
    loading = addr;                     /* show we are loading from the boot device */
    return SCPE_OK;                     /* all OK */
}

/* Scan all channels and see if one is ready to start or has
   interrupt pending.
*/
uint32 scan_chan(void) {
    int         i;
    int         pend = 0;           /* No device */
    uint32      tempa;              /* icb address */

fprintf(stderr, "scan_chan start irq_pending %x\r\n", irq_pend);
    if (irq_pend == 0)              /* pending int? */
        return 0;                   /* no, so just return */
    irq_pend = 0;                   /* not pending anymore */

    /* loop through all the channels/units for channel with pending interrupt */
    for (i = 0; i < MAX_CHAN; i++) {
        /* If channel end, check if we should continue */
        if (chan_status[i] & STATUS_CEND) {         /* do we have channel end */
            if (ccw_flags[i] & FLAG_CC) {           /* command chain flag */
                if (chan_status[i] & STATUS_DEND)   /* device end? */
                    (void)load_ccw(i, 1);           /* go load the next IOCB */
                else
                    irq_pend = 1;                   /* still pending int */
            } else {
                /* we have channel end and no CC flag, end it */
sim_debug(DEBUG_EXP, &cpu_dev, "Scan(%x %x) end\n", i, chan_status[i]);
                if (loading != 0)                   /* boot loading? */
                    pend = chan_dev[i];             /* get the chan number */
                break;
            }
        }
    }
fprintf(stderr, "scan_chan pend %x\r\n", pend);
    /* process any pending channel */
    if (pend) {
        irq_pend = 1;                               /* int still pending */
        i = find_subchan(pend);                     /* find channel */
fprintf(stderr, "scan_chan pend %x chan %x\r\n", pend, i);
        if (i >= 0) {
sim_debug(DEBUG_EXP, &cpu_dev, "Scan end (%x %x)\n", chan_dev[i], pend);
            store_csw(i);       /* store the status */
        }
        dev_status[pend] = 0;
    } else {
        for (pend = 0; pend < MAX_DEV; pend++) {    /* loop through all the channels/units */
            if (dev_status[pend] != 0) {            /* any pending status */
                i = find_subchan(pend);             /* get channel from device number */
//              if (i >= 0 && ccw_cmd[i] == 0 && mask & (0x80 >> (pend >> 8))) {
fprintf(stderr, "scan_chan 3 pend %x chan %x inch %x\r\n", pend, i, chan_inch_addr[i]);
                if (i >= 0 && ccw_cmd[i] == 0 && chan_inch_addr[i] != 0) {
fprintf(stderr, "scan_chan 3a pend %x chan %x inch %x\r\n", pend, i, chan_inch_addr[i]);
                    tempa = chan_inch_addr[i];  /* get inch status buffer address */
                    irq_pend = 1;               /* int still pending */
                    M[tempa >> 2] = 0;          /* zero sa & IOCD address in status WD 1 */
                    /* save the status to memory */
                    M[(tempa+4) >> 2] = (((uint32)dev_status[pend]) << 24);
fprintf(stderr, "scan_chan 4b pend %x chan %x tempa %x\r\n", pend, i, tempa);
sim_debug(DEBUG_EXP, &cpu_dev, "Set atten %03x %02x [%08x] %08x\n",
                            i, dev_status[pend], M[tempa >> 2], M[(tempa+4) >> 2]);
                    dev_status[pend] = 0;
                    pend = chan_icb[i];     /* Interrupt Context Block address in memory */
fprintf(stderr, "scan_chan 5 icb %x chan %x\r\n", pend, i);
                    return pend;
                }
            }
        }
        pend = 0;
    }
    /* Only return loading unit on loading */
    if (loading != 0 && loading != pend)
        return 0;
    if (pend) {
        i = find_subchan(pend);         /* get channel from device number */
fprintf(stderr, "scan_chan end pend=%x chan %x\r\n", pend, i);
        pend = chan_icb[i];             /* Interrupt Context Block address in memory */
        return pend;
    }
    return 0;
}


/* set up the devices configured into the simulator */
/* only devices with a DIB will be processed */
t_stat chan_set_devs()
{
    int i, j;

fprintf(stderr, "chan_set_devs entry\r\n");
    for(i = 0; i < MAX_DEV; i++) {
        dev_unit[i] = NULL;                         /* clear Device pointer array */
    }
    /* Build channel array */
    for (i = 0; sim_devices[i] != NULL; i++) {
        DEVICE  *dptr = sim_devices[i];             /* get pointer to next configured device */
        UNIT    *uptr = dptr->units;                /* get pointer to units defined for this device */
        DIB     *dibp = (DIB *)dptr->ctxt;          /* get pointer to Device Information Block for this device */
        int     addr;                               /* addr of device addr & subaddress */
        int     chan;                               /* channel address 0-7f */

        if (dibp == NULL)                           /* If no DIB, not channel device */
            continue;
        if (dptr->flags & DEV_DIS)                  /* Skip disabled devices */
            continue;
        /* Check if address is in unit or dev entry */
        for (j = 0; j < dptr->numunits; j++) {      /* loop through unit entries */
            addr = GET_UADDR(uptr->u3);             /* ch/sa value */
            dev_status[addr] = 0;                   /* zero device status flags */
            chan = (addr >> 8) & 0x7f;              /* get the chan address */
fprintf(stderr, "chan_set_devs check %s u3 %x addr %0x\r\n", dptr->name, GET_UADDR(uptr->u3), addr);
            if ((uptr->flags & UNIT_DIS) == 0)      /* is unit marked disabled? */
                dev_unit[addr] = dibp;              /* no, save the dib address */
            if (dibp->dev_ini != NULL)              /* if there is an init routine, call it now */
                dibp->dev_ini(uptr, 1);             /* init the channel */
            /* zero some channel data loc's for device */
            chan_inch_addr[chan] = 0;               /* clear address of stat dw in memory */
            chan_icb[chan] = 0;                     /* Interrupt Context Block address in memory */
            uptr++;                                 /* next UNIT pointer */
        }
    }
    return SCPE_OK;                                 /* all is OK */
}

/* Validate and set the device onto a given channel */
t_stat set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE             *dptr;
    DIB                *dibp;
    t_value             newdev;
    t_stat              r;
    int                 num;
    int                 type;
    int                 i;
    int                 devaddr;

fprintf(stderr, "set_dev_addr val %x cprt %s desc %s\r\n", val, cptr, desc);
    if (cptr == NULL)                   /* is there a UNIT name specified */
        return SCPE_ARG;                /* no, arg error */
    if (uptr == NULL)                   /* is there a UNIT pointer */
        return SCPE_IERR;               /* no, arg error */
    dptr = find_dev_from_unit(uptr);    /* find the device from unit pointer */
    if (dptr == NULL)                   /* device not found, so error */
        return SCPE_IERR;               /* error */

    dibp = (DIB *)dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;

    newdev = get_uint (cptr, 16, 0xfff, &r);

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

t_stat
show_dev_addr(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    DEVICE             *dptr;
    DIB                *dibp;
    int                 addr;

fprintf(stderr, "show_dev_addr val %x desc %s\r\n", v, desc);
    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    addr = GET_UADDR(uptr->u3);
    fprintf(st, "%03x", addr);
    return SCPE_OK;
}

