/* ibm360_chan.c: IBM 360 Channel functions.

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
#include "sim_defs.h"


#define CCMDMSK          0xff000000         /* Mask for command */
#define CDADRMSK         0x00ffffff         /* Mask for data address */
#define CCNTMSK          0x0000ffff         /* Mask for data count */
#define CD               0x80000000         /* Chain data */
#define CC               0x40000000         /* Chain command */
#define SLI              0x20000000         /* Suppress length indication */
#define SKIP             0x10000000         /* Skip flag */
#define PCI              0x08000000         /* Program controlled interuption */
#define IDA              0x04000000         /* Indirect Channel addressing */

/* Command masks */
#define CMD_TYPE         0x3                /* Type mask */
#define CMD_CHAN         0x0                /* Channel command */
#define CMD_WRITE        0x1                /* Write command */
#define CMD_READ         0x2                /* Read command */
#define CMD_CTL          0x3                /* Control command */
#define CMD_SENSE        0x4                /* Sense channel command */
#define CMD_TIC          0x8                /* Transfer in channel */
#define CMD_RDBWD        0xc                /* Read backward */

#define STATUS_ATTN      0x8000             /* Device raised attention */
#define STATUS_MOD       0x4000             /* Status modifier */
#define STATUS_CTLEND    0x2000             /* Control end */
#define STATUS_BUSY      0x1000             /* Device busy */
#define STATUS_CEND      0x0800             /* Channel end */
#define STATUS_DEND      0x0400             /* Device end */
#define STATUS_CHECK     0x0200             /* Unit check */
#define STATUS_EXPT      0x0100             /* Unit excpetion */
#define STATUS_PCI       0x0080             /* Program interupt */
#define STATUS_LENGTH    0x0040             /* Incorrect lenght */
#define STATUS_PCHK      0x0020             /* Program check */
#define STATUS_PROT      0x0010             /* Protection check */
#define STATUS_CDATA     0x0008             /* Channel data check */
#define STATUS_CCNTL     0x0004             /* Channel control check */
#define STATUS_INTER     0x0002             /* Channel interface check */
#define STATUS_CHAIN     0x0001             /* Channel chain check */

#define FLAG_CD          0x8000             /* Chain data */
#define FLAG_CC          0x4000             /* Chain command */
#define FLAG_SLI         0x2000             /* Suppress length indicator */
#define FLAG_SKIP        0x1000             /* Suppress memory write */
#define FLAG_PCI         0x0800             /* Program controled interrupt */
#define FLAG_IDA         0x0400             /* Channel indirect */

#define BUFF_EMPTY       0x4                /* Buffer is empty */
#define BUFF_DIRTY       0x8                /* Buffer is dirty flag */
#define BUFF_CHNEND      0x10               /* Channel end */

#define AMASK            0x00ffffff
#define PMASK            0xf0000000         /* Storage protection mask */
extern uint32  *M;
extern uint8   key[MAXMEMSIZE / 2048];

#define MAX_DEV         (MAX_CHAN * 256)

#define SEL_BASE      (SUB_CHANS * MAX_MUX)
#define CHAN_SZ       (SEL_BASE + MAX_CHAN)
int         channels            = MAX_CHAN;
int         subchannels         = SUB_CHANS;         /* Number of subchannels */
int         irq_pend = 0;
uint32      caw[CHAN_SZ];                   /* Channel command address word */
uint32      ccw_addr[CHAN_SZ];              /* Channel address */
uint32      ccw_iaddr[CHAN_SZ];             /* Channel indirect address */
uint16      ccw_count[CHAN_SZ];             /* Channel count */
uint8       ccw_cmd[CHAN_SZ];               /* Channel command and flags */
uint8       ccw_key[CHAN_SZ];               /* Channel key */
uint16      ccw_flags[CHAN_SZ];             /* Channel flags */
uint16      chan_status[CHAN_SZ];           /* Channel status */
uint16      chan_dev[CHAN_SZ];              /* Device on channel */
uint32      chan_buf[CHAN_SZ];              /* Channel data buffer */
uint8       chan_byte[CHAN_SZ];             /* Current byte, dirty/full */
uint8       chan_pend[CHAN_SZ];             /* Pending status on this channel */
DIB         *dev_unit[MAX_DEV];             /* Pointer to Device info block */
uint8       dev_status[MAX_DEV];            /* last device status flags */

/* Find unit pointer for given device */
UNIT         *
find_chan_dev(uint16 addr) {
    struct dib         *dibp;
    UNIT               *uptr;
    int                 i;

    if (addr >= MAX_DEV)
       return NULL;
    dibp = dev_unit[addr];
    if (dibp == 0)
       return NULL;
    uptr = dibp->units;
    if (dibp->mask == 0) {
       for (i = 0; i < dibp->numunits; i++) {
            if (addr == GET_UADDR(uptr->u3))
                return uptr;
            uptr++;
       }
    } else {
       return uptr + (addr & ~dibp->mask & 0xff);
    }
    return NULL;
}

/* channel:
         subchannels = 128
         0 - 7       0x80-0xff
        8 - 127     0x00-0x7f
        128 - +6    0x1xx - 0x6xx
*/

/* look up device to find subchannel device is on */
int
find_subchan(uint16 device) {
    int     chan;
    int     base = 0;
    if (device >= MAX_DEV)
        return -1;
    chan = (device >> 8) & 0xf;
    device &= 0xff;
    if (chan > channels)
        return -1;
    switch(chan) {
    case 4:
           base += subchannels;
           /* Fall through */

    case 0:      /* Multiplexer channel */
           if (device >= subchannels)
               device = ((device - subchannels) >> 4) & 0x7;
           return base + device;
    case 1:      /* Selector channel */
    case 2:
    case 3:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
           return SEL_BASE + chan;
    }
    return -1;
}

/* find a channel for a given index */
int
find_chan(int chan) {
    return (chan_dev[chan] >> 8) & 0xf;
}

/* Read a full word into memory.
 * Return 1 if fail.
 * Return 0 if success.
 */
int
readfull(int chan, uint32 addr, uint32 *word) {
    if ((addr & AMASK) > MEMSIZE) {
        chan_status[chan] |= STATUS_PCHK;
        irq_pend = 1;
        return 1;
    }
    addr >>= 2;
    if (ccw_key[chan] != 0) {
        int k;
        if ((cpu_unit[0].flags & FEAT_PROT) == 0) {
            chan_status[chan] |= STATUS_PROT;
            irq_pend = 1;
            return 1;
        }
        k = key[addr >> 9];
        if ((k & 0x8) != 0 && (k & 0xf0) != ccw_key[chan]) {
            chan_status[chan] |= STATUS_PROT;
            irq_pend = 1;
            return 1;
        }
    }
    key[addr >> 9] |= 0x4;
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
    uint32 addr;
    if (ccw_flags[chan] & FLAG_IDA && cpu_unit[0].flags & FEAT_370)
       addr = ccw_iaddr[chan];
    else
       addr = ccw_addr[chan];
    if (readfull(chan, addr, &chan_buf[chan])) {
        chan_byte[chan] = BUFF_CHNEND;
        return 1;
    }
    if ((ccw_cmd[chan] & 1) == 1) {
        sim_debug(DEBUG_CDATA, &cpu_dev, "Channel write %02x %06x %08x %08x '",
              chan, addr, chan_buf[chan], ccw_count[chan]);
        for(k = 24; k >= 0; k -= 8) {
            unsigned char ch = ebcdic_to_ascii[(chan_buf[chan] >> k) & 0xFF];
            if (ch < 0x20 || ch == 0xff)
               ch = '.';
            sim_debug(DEBUG_CDATA, &cpu_dev, "%c", ch);
        }

        sim_debug(DEBUG_CDATA, & cpu_dev, "'\n");
    }
    return 0;
}

/* Write channel buffer to memory.
 * Return 1 if fail.
 * Return 0 if success.
 */
int
writebuff(int chan) {
    uint32 addr;
    int k;
    if (ccw_flags[chan] & FLAG_IDA && cpu_unit[0].flags & FEAT_370)
       addr = ccw_iaddr[chan];
    else
       addr = ccw_addr[chan];
    if ((addr & AMASK) > MEMSIZE) {
        chan_status[chan] |= STATUS_PCHK;
        chan_byte[chan] = BUFF_CHNEND;
        irq_pend = 1;
        return 1;
    }
    addr >>= 2;
    if (ccw_key[chan] != 0) {
        if ((cpu_unit[0].flags & FEAT_PROT) == 0) {
            chan_status[chan] |= STATUS_PROT;
            chan_byte[chan] = BUFF_CHNEND;
            irq_pend = 1;
            return 1;
        }
        k = key[addr >> 9];
        if ((k & 0xf0) != ccw_key[chan]) {
            chan_status[chan] |= STATUS_PROT;
            chan_byte[chan] = BUFF_CHNEND;
            irq_pend = 1;
            return 1;
        }
    }
    key[addr >> 9] |= 0x6;
    M[addr] = chan_buf[chan];
    sim_debug(DEBUG_CDATA, &cpu_dev, "Channel readf %02x %06x %08x %08x '",
          chan, addr << 2, chan_buf[chan], ccw_count[chan]);
    for(k = 24; k >= 0; k -= 8) {
        unsigned char ch = ebcdic_to_ascii[(chan_buf[chan] >> k) & 0xFF];
        if (ch < 0x20 || ch == 0xff)
            ch = '.';
        sim_debug(DEBUG_CDATA, &cpu_dev, "%c", ch);
    }
    sim_debug(DEBUG_CDATA, &cpu_dev, "'\n");
    return 0;
}

/*
 * Load in the next CCW, return 1 if failure, 0 if success.
 */
int
load_ccw(uint16 chan, int tic_ok) {
    uint32         word;
    int            cmd = 0;
    UNIT          *uptr;

loop:
    /* Abort if channel not on double boundry */
    if ((caw[chan] & 0x7) != 0) {
        chan_status[chan] |= STATUS_PCHK;
        return 1;
    }
    /* Abort if we have any errors */
    if (chan_status[chan] & 0x7f)
        return 1;
    /* Check if we have status modifier set */
    if (chan_status[chan] & STATUS_MOD) {
        caw[chan]+=8;
        caw[chan] &= PMASK|AMASK;         /* Mask overflow bits */
        chan_status[chan] &= ~STATUS_MOD;
    }
    /* Read in next CCW */
    readfull(chan, caw[chan], &word);
    sim_debug(DEBUG_CMD, &cpu_dev, "Channel read ccw  %02x %06x %08x\n",
              chan, caw[chan], word);
    /* TIC can't follow TIC nor be first in chain */
    if (((word >> 24) & 0xf) == CMD_TIC) {
        if (tic_ok) {
            caw[chan] = (caw[chan] & PMASK) | (word & AMASK);
            tic_ok = 0;
            goto loop;
        }
        chan_status[chan] |= STATUS_PCHK;
        irq_pend = 1;
        return 1;
    }
    caw[chan] += 4;
    caw[chan] &= AMASK;                   /* Mask overflow bits */
    /* Check if not chaining data */
    if ((ccw_flags[chan] & FLAG_CD) == 0) {
        ccw_cmd[chan] = (word >> 24) & 0xff;
        cmd = 1;
    }
    /* Set up for this command */
    ccw_addr[chan] = word & AMASK;
    readfull(chan, caw[chan], &word);
    sim_debug(DEBUG_CMD, &cpu_dev, "Channel read ccw2 %02x %06x %08x\n",
             chan, caw[chan], word);
    caw[chan]+=4;
    caw[chan] &= AMASK;                  /* Mask overflow bits */
    ccw_count[chan] = word & 0xffff;
    /* Copy SLI indicator on CD command */
    if ((ccw_flags[chan] & (FLAG_CD|FLAG_SLI)) == (FLAG_CD|FLAG_SLI))
         word |= (FLAG_SLI<<16);
    ccw_flags[chan] = (word >> 16) & 0xff00;
    chan_byte[chan] = BUFF_EMPTY;
    /* Check invalid count */
    if (ccw_count[chan] == 0) {
        chan_status[chan] |= STATUS_PCHK;
        irq_pend = 1;
        return 1;
    }
    if (ccw_flags[chan] & FLAG_IDA && cpu_unit[0].flags & FEAT_370) {
        readfull(chan, ccw_addr[chan], &word);
        ccw_iaddr[chan] = word & AMASK;
        sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel fetch idaw %02x %08x\n",
             chan, ccw_iaddr[chan]);
    }
    if (cmd) {
         DIB         *dibp = dev_unit[chan_dev[chan]];
         /* Check if invalid command */
         if ((ccw_cmd[chan] & 0xF) == 0) {
             chan_status[chan] |= STATUS_PCHK;
             irq_pend = 1;
             return 1;
         }
         uptr = find_chan_dev(chan_dev[chan]);
         if (uptr == 0)
             return 1;
         chan_status[chan] &= 0xff;
         chan_status[chan] |= dibp->start_cmd(uptr, chan, ccw_cmd[chan]) << 8;
         if (chan_status[chan] & (STATUS_ATTN|STATUS_CHECK|STATUS_EXPT)) {
             sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel %03x abort %04x\n", 
                     chan, chan_status[chan]);
             chan_status[chan] |= STATUS_CEND;
             ccw_flags[chan] = 0;
             ccw_cmd[chan] = 0;
             irq_pend = 1;
             return 1;
         }
         if (chan_status[chan] & (STATUS_DEND|STATUS_CEND)) {
             chan_status[chan] |= STATUS_CEND;
             ccw_cmd[chan] = 0;
             irq_pend = 1;
        }
    }
    if (ccw_flags[chan] & FLAG_PCI) {
        chan_status[chan] |= STATUS_PCI;
        ccw_flags[chan] &= ~FLAG_PCI;
        irq_pend = 1;
        sim_debug(DEBUG_CMD, &cpu_dev, "Set PCI %02x load\n", chan);
    }
    return 0;
}


/* read byte from memory */
int
chan_read_byte(uint16 addr, uint8 *data) {
    int         chan = find_subchan(addr);
    int         byte;

    /* Abort if we have any errors */
    if (chan < 0)
        return 1;
    if (chan_status[chan] & 0x7f)
        return 1;
    if ((ccw_cmd[chan] & 0x1)  == 0) {
        return 1;
    }
    /* Check if finished transfer */
    if (chan_byte[chan] == BUFF_CHNEND)
        return 1;
    /* Check if count is zero */
    if (ccw_count[chan] == 0) {
         /* If not data channing, let device know there will be no
          * more data to come
          */
         if ((ccw_flags[chan] & FLAG_CD) == 0) {
            chan_status[chan] |= STATUS_CEND;
            chan_byte[chan] = BUFF_CHNEND;
            sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_read_end\n");
            return 1;
         } else {
            /* If chaining try and start next CCW */
            if (load_ccw(chan, 1))
                return 1;
         }
    }
    /* Read in next work if buffer is in empty status */
    if (chan_byte[chan] == BUFF_EMPTY) {
         if (readbuff(chan))
            return 1;
         if (ccw_flags[chan] & FLAG_IDA && cpu_unit[0].flags & FEAT_370) {
             chan_byte[chan] = ccw_iaddr[chan] & 0x3;
             ccw_iaddr[chan] += 4 - chan_byte[chan];
             if ((ccw_iaddr[chan] & 0x7ff) == 0) {
                 uint32 temp;
                 ccw_addr[chan] += 4;
                 readfull(chan, ccw_addr[chan], &temp);
                 ccw_iaddr[chan] = temp & AMASK;
                 sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel fetch idaw %02x %08x\n",
                      chan, ccw_iaddr[chan]);
             }
         } else {
             chan_byte[chan] = ccw_addr[chan] & 0x3;
             ccw_addr[chan] += 4 - chan_byte[chan];
         }
    }
    /* Return current byte */
    ccw_count[chan]--;
    byte = (chan_buf[chan] >>  (8 * (3 - (chan_byte[chan] & 0x3)))) & 0xff;
    chan_byte[chan]++;
    *data = byte;
    /* If count is zero and chainging load in new CCW */
    if (ccw_count[chan] == 0 && (ccw_flags[chan] & FLAG_CD) != 0) {
        chan_byte[chan] = BUFF_EMPTY;
        /* Try and grab next CCW */
        if (load_ccw(chan, 1))
            return 1;
    }
    return 0;
}

/* write byte to memory */
int
chan_write_byte(uint16 addr, uint8 *data) {
    int          chan = find_subchan(addr);
    int          offset;
    uint32       mask;

    /* Abort if we have any errors */
    if (chan < 0)
        return 1;
    if (chan_status[chan] & 0x7f)
        return 1;
    if ((ccw_cmd[chan] & 0x1)  != 0) {
        return 1;
    }
    /* Check if at end of transfer */
    if (chan_byte[chan] == BUFF_CHNEND) {
        if ((ccw_flags[chan] & FLAG_SLI) == 0) {
            chan_status[chan] |= STATUS_LENGTH;
        }
        return 1;
    }
    /* Check if count is zero */
    if (ccw_count[chan] == 0) {
        /* Flush the buffer if we got anything back. */
        if (chan_byte[chan] & BUFF_DIRTY) {
            if (writebuff(chan))
                return 1;
        }
        /* If not data channing, let device know there will be no
         * more data to come
         */
        if ((ccw_flags[chan] & FLAG_CD) == 0) {
            chan_byte[chan] = BUFF_CHNEND;
            if ((ccw_flags[chan] & FLAG_SLI) == 0) {
                sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_write_ length\n");
                chan_status[chan] |= STATUS_LENGTH;
            }
            sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_write_end\n");
            return 1;
        }
        /* Otherwise try and grab next CCW */
        if (load_ccw(chan, 1))
            return 1;
    }
    /* If we are skipping, just adjust count */
    if (ccw_flags[chan] & FLAG_SKIP) {
        ccw_count[chan]--;
        chan_byte[chan] = BUFF_EMPTY;
        if (ccw_flags[chan] & FLAG_IDA && cpu_unit[0].flags & FEAT_370) {
            if ((ccw_cmd[chan] & 0xf) == CMD_RDBWD) {
                ccw_iaddr[chan]--;
                if ((ccw_iaddr[chan] & 0x7ff) == 0x7ff) {
                    uint32 temp;
                    ccw_addr[chan] += 4;
                    readfull(chan, ccw_addr[chan], &temp);
                    ccw_iaddr[chan] = temp & AMASK;
                    sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel fetch idaw %02x %08x\n",
                         chan, ccw_iaddr[chan]);
                }
            } else {
                ccw_iaddr[chan]++;
                if ((ccw_iaddr[chan] & 0x7ff) == 0) {
                    uint32 temp;
                    ccw_addr[chan] += 4;
                    readfull(chan, ccw_addr[chan], &temp);
                    ccw_iaddr[chan] = temp & AMASK;
                    sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel fetch idaw %02x %08x\n",
                         chan, ccw_iaddr[chan]);
                }
            }
        } else {
            if ((ccw_cmd[chan] & 0xf) == CMD_RDBWD)
                ccw_addr[chan]--;
            else
                ccw_addr[chan]++;
        }
        return 0;
    }
    /* Check if we need to save what we have */
    if (chan_byte[chan] == (BUFF_EMPTY|BUFF_DIRTY)) {
        if (writebuff(chan))
            return 1;
        if (ccw_flags[chan] & FLAG_IDA && cpu_unit[0].flags & FEAT_370) {
            if ((ccw_cmd[chan] & 0xf) == CMD_RDBWD) {
               ccw_iaddr[chan] -= 1 + (ccw_iaddr[chan] & 0x3);
               if ((ccw_iaddr[chan] & 0x7ff) == 0x7fc) {
                   uint32 temp;
                   ccw_addr[chan] += 4;
                   readfull(chan, ccw_addr[chan], &temp);
                   ccw_iaddr[chan] = temp & AMASK;
                   sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel fetch idaw %02x %08x\n",
                         chan, ccw_iaddr[chan]);
               }
            } else {
               ccw_iaddr[chan] += 4 - (ccw_iaddr[chan] & 0x3);
               if ((ccw_iaddr[chan] & 0x7ff) == 0) {
                   uint32 temp;
                   ccw_addr[chan] += 4;
                   readfull(chan, ccw_addr[chan], &temp);
                   ccw_iaddr[chan] = temp & AMASK;
                   sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel fetch idaw %02x %08x\n",
                         chan, ccw_iaddr[chan]);
               }
            }
        } else {
            if ((ccw_cmd[chan] & 0xf) == CMD_RDBWD)
               ccw_addr[chan] -= 1 + (ccw_addr[chan] & 0x3);
            else
               ccw_addr[chan] += 4 - (ccw_addr[chan] & 0x3);
        }
        chan_byte[chan] = BUFF_EMPTY;
    }
    if (chan_byte[chan] == BUFF_EMPTY) {
        if (readbuff(chan))
            return 1;
        if (ccw_flags[chan] & FLAG_IDA && cpu_unit[0].flags & FEAT_370)
            chan_byte[chan] = ccw_iaddr[chan] & 0x3;
        else
            chan_byte[chan] = ccw_addr[chan] & 0x3;
    }
    /* Store it in buffer and adjust pointer */
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
    /* If count is zero and chainging load in new CCW */
    if (ccw_count[chan] == 0 && (ccw_flags[chan] & FLAG_CD) != 0) {
        /* Flush buffer */
        if (writebuff(chan))
            return 1;
        chan_byte[chan] = BUFF_EMPTY;
        /* Try and grab next CCW */
        if (load_ccw(chan, 1))
            return 1;
    }
    return 0;
}

/*
 * A device wishes to inform the CPU it needs some service.
 */
void
set_devattn(uint16 addr, uint8 flags) {
    int          chan = find_subchan(addr);

    if (chan < 0)
        return;
    if (chan_dev[chan] == addr && (chan_status[chan] & STATUS_CEND) != 0 &&
            (flags & SNS_DEVEND) != 0) {
        chan_status[chan] |= ((uint16)flags) << 8;
    } else {
        dev_status[addr] = flags;
        chan_pend[chan] = 1;
    }
    sim_debug(DEBUG_EXP, &cpu_dev, "set_devattn(%x, %x) %x\n",
                 addr, flags, chan_dev[chan]);
    irq_pend = 1;
}

/*
 * Signal end of transfer by device.
 */
void
chan_end(uint16 addr, uint8 flags) {
    int         chan = find_subchan(addr);

    if (chan < 0)
        return;

    sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_end(%x, %x) %x %04x %04x\n", addr, flags,
                 ccw_count[chan], ccw_flags[chan], chan_status[chan]);

    /* Flush buffer if there was any change */
    if (chan_byte[chan] & BUFF_DIRTY) {
        (void)(writebuff(chan));
        chan_byte[chan] = BUFF_EMPTY;
    }
    chan_status[chan] |= STATUS_CEND;
    chan_status[chan] |= ((uint16)flags) << 8;
    ccw_cmd[chan] = 0;

    /* If count not zero and not suppressing length, report error */
    if (ccw_count[chan] != 0 && (ccw_flags[chan] & FLAG_SLI) == 0) {
        sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_end length\n");
        chan_status[chan] |= STATUS_LENGTH;
        ccw_flags[chan] = 0;
    }
    if (ccw_count[chan] != 0 && (ccw_flags[chan] & (FLAG_CD|FLAG_SLI)) == (FLAG_CD|FLAG_SLI)) {
        sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_end length 2\n");
        chan_status[chan] |= STATUS_LENGTH;
    }
    if (flags & (SNS_ATTN|SNS_UNITCHK|SNS_UNITEXP))
        ccw_flags[chan] = 0;

    if ((flags & SNS_DEVEND) != 0)
        ccw_flags[chan] &= ~(FLAG_CD|FLAG_SLI);

    irq_pend = 1;
    sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_end(%x, %x) %x %04x end\n", addr, flags,
                 chan_status[chan], ccw_flags[chan]);
}

/*
 * Save full csw.
 */
void
store_csw(uint16 chan) {
    M[0x40 >> 2] = caw[chan];
    M[0x44 >> 2] = (((uint32)ccw_count[chan])) | ((uint32)chan_status[chan]<<16);
    key[0] |= 0x6;
    if (chan_status[chan] & STATUS_PCI) {
        chan_status[chan] &= ~STATUS_PCI;
        sim_debug(DEBUG_CMD, &cpu_dev, "Clr PCI %02x store\n", chan);
    } else {
        chan_status[chan] = 0;
    }
    ccw_flags[chan] &= ~FLAG_PCI;
    sim_debug(DEBUG_EXP, &cpu_dev, "Channel store csw  %02x %06x %08x\n",
          chan, M[0x40>>2], M[0x44 >> 2]);
}

/*
 * Handle SIO instruction.
 */
int
startio(uint16 addr) {
    int          chan = find_subchan(addr);
    DIB         *dibp = NULL;
    UNIT        *uptr;

    if (addr < MAX_DEV)
        dibp = dev_unit[addr];

    /* Find channel this device is on, if no none return cc=3 */
    if (chan < 0 || dibp == NULL) {
        sim_debug(DEBUG_CMD, &cpu_dev, "SIO %x %x cc=3\n", addr, chan);
        return 3;
    }
    uptr = find_chan_dev(addr);
    if (uptr == 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "SIO u %x %x cc=3\n", addr, chan);
        return 3;
    }

    sim_debug(DEBUG_CMD, &cpu_dev, "SIO %x %x %x %x %x\n", addr, chan,
              ccw_cmd[chan], ccw_flags[chan], chan_status[chan]);

    /* If pending status is for us, return it with status code */
    if (chan_dev[chan] == addr && chan_status[chan] != 0) {
        store_csw(chan);
        return 1;
    }

    /* If channel is active return cc=2 */
    if (ccw_cmd[chan] != 0 ||
        (ccw_flags[chan] & (FLAG_CD|FLAG_CC)) != 0 ||
        chan_status[chan] != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "SIO %x %x %08x cc=2\n", addr, chan, 
               chan_status[chan]);
        return 2;
    }


    if (dev_status[addr] != 0) {
        M[0x44 >> 2] = (((uint32)dev_status[addr]) << 24);
        M[0x40 >> 2] = 0;
        key[0] |= 0x6;
        sim_debug(DEBUG_EXP, &cpu_dev,
            "SIO Set atten %03x %x %02x [%08x] %08x\n",
            addr, chan, dev_status[addr], M[0x40 >> 2], M[0x44 >> 2]);
        dev_status[addr] = 0;
        return 1;
    }

    /* All ok, get caw address */
    chan_status[chan] = 0;
    dev_status[addr] = 0;
    caw[chan] = M[0x48>>2];
    ccw_key[chan] = (caw[chan] & PMASK) >> 24;
    caw[chan] &= AMASK;
    key[0] |= 0x4;
    chan_dev[chan] = addr;

    /* If device has start_io function run it */
    if (dibp->start_io != NULL) {
        chan_status[chan] = dibp->start_io(uptr, chan) << 8;
        if (chan_status[chan] != 0) {
            M[0x44 >> 2] = ((uint32)chan_status[chan]<<16) | (M[0x44 >> 2] & 0xffff);
            sim_debug(DEBUG_EXP, &cpu_dev, "Channel store csw  %02x %08x\n",
                   chan, M[0x44 >> 2]);
            chan_status[chan] = 0;
            return 1;
        }
    }

    /* Try to load first command */
    if (load_ccw(chan, 0)) {
        M[0x44 >> 2] = ((uint32)chan_status[chan]<<16) | (M[0x44 >> 2] & 0xffff);
        key[0] |= 0x6;
        sim_debug(DEBUG_CMD, &cpu_dev, "SIO %x %x %x %x cc=2\n", addr, chan,
              ccw_cmd[chan], ccw_flags[chan]);
        chan_status[chan] = 0;
        dev_status[addr] = 0;
        ccw_cmd[chan] = 0;
        return 1;
    }

    /* If channel returned busy save CSW and return cc=1 */
    if (chan_status[chan] & STATUS_BUSY) {
        M[0x40 >> 2] = 0;
        M[0x44 >> 2] = ((uint32)chan_status[chan]<<16);
        key[0] |= 0x6;
        sim_debug(DEBUG_EXP, &cpu_dev, "Channel store csw  %02x %08x\n",
                   chan, M[0x44 >> 2]);
        chan_status[chan] = 0;
        dev_status[addr] = 0;
        chan_dev[chan] = 0;
        ccw_cmd[chan] = 0;
        return 1;
    }

    return 0;
}

/*
 * Handle TIO instruction.
 */
int testio(uint16 addr) {
    int          chan = find_subchan(addr);
    DIB         *dibp = NULL;
    UNIT        *uptr;
    uint16       status;

    if (addr < MAX_DEV)
        dibp = dev_unit[addr];

    /* Find channel this device is on, if no none return cc=3 */
    if (chan < 0 || dibp == NULL) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %x %x cc=3\n", addr, chan);
        return 3;
    }
    uptr = find_chan_dev(addr);
    if (uptr == 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %x %x uptr cc=3\n", addr, chan);
        return 3;
    }

    /* If any error pending save csw and return cc=1 */
    if (chan_status[chan] & (STATUS_PCI|STATUS_ATTN|STATUS_CHECK|\
            STATUS_PROT|STATUS_PCHK|STATUS_EXPT)) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %x %x %x %x cc=1\n", addr, chan,
              ccw_cmd[chan], ccw_flags[chan]);
        store_csw(chan);
        return 1;
    }

    /* If channel active, return cc=2 */
    if (ccw_cmd[chan] != 0 || (ccw_flags[chan] & (FLAG_CD|FLAG_CC)) != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %x %x %x %x cc=2\n", addr, chan,
              ccw_cmd[chan], ccw_flags[chan]);
        return 2;
    }

    /* Device finished and channel status pending return it and cc=1 */
    if (ccw_cmd[chan] == 0 && chan_status[chan] != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %x %x %x %x cc=1a\n", addr, chan,
              ccw_cmd[chan], ccw_flags[chan]);
        store_csw(chan);
        chan_dev[chan] = 0;
        return 1;
    }

    /* Device has returned a status, store the csw and return cc=1 */
    if (dev_status[addr] != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %x %x %x %x cc=1b\n", addr, chan,
              ccw_cmd[chan], ccw_flags[chan]);
        M[0x40 >> 2] = 0;
        M[0x44 >> 2] = ((uint32)dev_status[addr]) << 24;
        key[0] |= 0x6;
        dev_status[addr] = 0;
        chan_pend[chan] = 0;
        return 1;
    }

    /* If error pending for another device, return cc=2 */
    if (chan_pend[chan] != 0) {
        int pend, ch;
        chan_pend[chan] = 0;
        /* Check if might be false */
        for (pend = 0; pend < MAX_DEV; pend++) {
            if (dev_status[pend] != 0) {
                ch = find_subchan(pend);
                if (ch == chan) {
                    chan_pend[ch] = 1;
                    sim_debug(DEBUG_CMD, &cpu_dev, "TIO %x %x %x %x %x cc=2a\n", addr, chan,
                          ccw_cmd[chan], ccw_flags[chan], pend);
                    return 2;
                }
            }
        }
    }

    /* Nothing pending, send a 0 command to device to get status */
    status = dibp->start_cmd(uptr, chan, 0) << 8;

    /* If no status and unattached device return cc=3 */
    if (status == 0 && (uptr->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %x %x %x %x cc=1c\n", addr, chan,
              ccw_cmd[chan], ccw_flags[chan]);
        return 3;
    }

    /* If we get a error, save csw and return cc=1 */
    if (status & (STATUS_ATTN|STATUS_CHECK|STATUS_EXPT)) {
        M[0x44 >> 2] = ((uint32)status<<24) | (M[0x44 >> 2] & 0xffff);
        key[0] |= 0x6;
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %x %x %x %x cc=1d\n", addr, chan,
              ccw_cmd[chan], ccw_flags[chan]);
        return 1;
    }

    /* Everything ok, return cc=0 */
    sim_debug(DEBUG_CMD, &cpu_dev, "TIO %x %x %x %x cc=0\n", addr, chan,
              ccw_cmd[chan], ccw_flags[chan]);
    return 0;
}

/*
 * Handle HIO instruction.
 */
int haltio(uint16 addr) {
    int           chan = find_subchan(addr);
    DIB          *dibp = NULL;
    UNIT         *uptr;
    int           cc;

    if (addr < MAX_DEV)
        dibp = dev_unit[addr];

    /* Find channel this device is on, if no none return cc=3 */
    if (chan < 0 || dibp == NULL) {
        sim_debug(DEBUG_CMD, &cpu_dev, "HIO %x %x\n", addr, chan);
        return 3;
    }
    uptr = find_chan_dev(addr);
    if (uptr == 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "HIOu %x %x\n", addr, chan);
        return 3;
    }
    sim_debug(DEBUG_CMD, &cpu_dev, "HIO %x %x %x %x\n", addr, chan,
              ccw_cmd[chan], ccw_flags[chan]);

    /* Generic halt I/O, tell device to stop and */
    /* If any error pending save csw and return cc=1 */
    if (chan_status[chan] & (STATUS_PCI|STATUS_ATTN|STATUS_CHECK|\
            STATUS_PROT|STATUS_PCHK|STATUS_EXPT)) {
        sim_debug(DEBUG_CMD, &cpu_dev, "HIO %x %x %x cc=0\n", addr, chan,
              chan_status[chan]);
        return 0;
    }

    /* If channel active, tell it to terminate */
    if (ccw_cmd[chan]) {
        chan_byte[chan] = BUFF_CHNEND;
        ccw_flags[chan] &= ~(FLAG_CD|FLAG_CC);
    }

    /* Not executing a command, issue halt if available */
    if (dibp->halt_io != NULL) {
        /* Let device do it's thing */
        cc = dibp->halt_io(uptr);
        sim_debug(DEBUG_CMD, &cpu_dev, "HIOd %x %x cc=%d\n", addr, chan, cc);
        if (cc == 1) {
            M[0x44 >> 2] = (((uint32)chan_status[chan]) << 16) |
                       (M[0x44 >> 2] & 0xffff);
            key[0] |= 0x6;
        }
        return cc;
    }


    /* Store CSW and return 1. */
    sim_debug(DEBUG_CMD, &cpu_dev, "HIOx %x %x cc=1\n", addr, chan);
    M[0x44 >> 2] = (((uint32)chan_status[chan]) << 16) |
                   (M[0x44 >> 2] & 0xffff);
    key[0] |= 0x6;
    return 1;
}

/*
 * Handle TCH instruction.
 */
int testchan(uint16 channel) {
    uint16         st = 0;
    channel >>= 8;
    if (channel == 0 || channel == 4)
        return 0;
    if (channel > channels)
        return 3;
    st = chan_status[SEL_BASE + channel];
    if (st & STATUS_BUSY)
        return 2;
    if (st & (STATUS_ATTN|STATUS_PCI|STATUS_EXPT|STATUS_CHECK|
                  STATUS_PROT|STATUS_CDATA|STATUS_CCNTL|STATUS_INTER|
                  STATUS_CHAIN))
        return 1;
    return 0;
}

/*
 * Bootstrap a device. Set command to READ IPL, length 24 bytes, suppress
 * length warning, and chain command.
 */
t_stat chan_boot(uint16 addr, DEVICE *dptyr) {
    int          chan = find_subchan(addr);
    DIB         *dibp = NULL;
    UNIT        *uptr;
    int          i;

    if (addr < MAX_DEV)
        dibp = dev_unit[addr];

    if (chan < 0 || dibp == NULL)
        return SCPE_IOERR;
    for (i = 0; i < MAX_DEV; i++) {
        dev_status[i] = 0;
    }
    for (i = 0; i < 256; i++) {
        ccw_cmd[i] = 0;
        ccw_flags[i] = 0;
    }
    uptr = find_chan_dev(addr);
    chan_status[chan] = 0;
    dev_status[addr] = 0;
    caw[chan] = 0x8;
    chan_dev[chan] = addr;
    ccw_count[chan] = 24;
    ccw_flags[chan] = FLAG_CC|FLAG_SLI;
    ccw_addr[chan] = 0;
    ccw_key[chan] = 0;
    chan_byte[chan] = BUFF_EMPTY;
    ccw_cmd[chan] = 0x2;
    chan_status[chan] &= 0xff;
    chan_status[chan] |= dibp->start_cmd(uptr, chan, ccw_cmd[chan]) << 8;
    if (chan_status[chan] & (STATUS_ATTN|STATUS_CHECK|STATUS_EXPT)) {
        ccw_flags[chan] = 0;
        return SCPE_IOERR;
    }
    loading = addr;
    return SCPE_OK;
}

/*
 * Scan all channels and see if one is ready to start or has
 * interrupt pending.
 */
uint16
scan_chan(uint16 mask, int irq_en) {
     int         i;
     int         ch;
     int         pend = 0;         /* No device */
     int         imask = 0x8000;

     /* Quick exit if no pending IRQ's */
     if (irq_pend == 0)
         return 0;
     irq_pend = 0;
     /* Start with channel 0 and work through all channels */
     for (i = 0; i < CHAN_SZ; i++) {
         /* Check if PCI pending */
         if (irq_en && (chan_status[i] & STATUS_PCI) != 0) {
             imask = 0x8000 >> find_chan(i);
             sim_debug(DEBUG_EXP, &cpu_dev, "Scan PCI(%x %x %x %x) end\n", i,
                         chan_status[i], imask, mask);
             if ((imask & mask) != 0) {
                pend = chan_dev[i];
                break;
             }
         }

         /* If channel end, check if we should continue */
         if (chan_status[i] & STATUS_CEND) {
             if (ccw_flags[i] & FLAG_CC) {
                if (chan_status[i] & STATUS_DEND)
                   (void)load_ccw(i, 1);
                else
                    irq_pend = 1;
             } else if (irq_en || loading) {
                imask = 0x8000 >> find_chan(i);
                sim_debug(DEBUG_EXP, &cpu_dev, "Scan(%x %x %x %x) end\n", i,
                         chan_status[i], imask, mask);
                if ((chan_status[i] & STATUS_DEND) != 0 &&
                     ((imask & mask) != 0 || loading != 0)) {
                    pend = chan_dev[i];
                    break;
                }
             }
         }
     }
     /* Only return loading unit on loading */
     if (loading != 0 && loading != pend)
        return 0;
     /* See if we can post an IRQ */
     if (pend) {
          irq_pend = 1;
          ch = find_subchan(pend);
          if (ch < 0)
              return 0;
          if (loading && loading == pend) {
              chan_status[ch] = 0;
              return pend;
          }
          if (ch >= 0 && loading == 0) {
              sim_debug(DEBUG_EXP, &cpu_dev, "Scan end (%x %x)\n", chan_dev[ch], pend);
              store_csw(ch);
              dev_status[pend] = 0;
              return pend;
          }
     } else {
          if (!irq_en)
             return 0;
          for (pend = 0; pend < MAX_DEV; pend++) {
             if (dev_status[pend] != 0) {
                 ch = find_subchan(pend);
                 if (ch >= 0 && ccw_cmd[ch] == 0 &&
                        (mask & (0x8000 >> (pend >> 8))) != 0) {
                     irq_pend = 1;
                     M[0x44 >> 2] = (((uint32)dev_status[pend]) << 24);
                     M[0x40>>2] = 0;
                     key[0] |= 0x6;
                     sim_debug(DEBUG_EXP, &cpu_dev,
                            "Set atten %03x %02x [%08x] %08x\n",
                            ch, dev_status[pend], M[0x40 >> 2], M[0x44 >> 2]);
                     dev_status[pend] = 0;
                     return pend;
                 }
             }
         }
     }
     return 0;
}


/*
 * Scan all devices and create the device mapping.
 */
t_stat
chan_set_devs()
{
    uint32              i, j;

    for(i = 0; i < MAX_DEV; i++) {
        dev_unit[i] = NULL;                  /* Device pointer */
    }
    /* Build channel array */
    for (i = 0; sim_devices[i] != NULL; i++) {
        DEVICE             *dptr = sim_devices[i];
        UNIT               *uptr = dptr->units;
        DIB                *dibp = (DIB *) dptr->ctxt;
        int                 addr;

        /* If no DIB, not channel device */
        if (dibp == NULL)
            continue;
        /* Skip disabled devices */
        if (dptr->flags & DEV_DIS)
            continue;
        /* Check if address is in unit or dev entry */
        for (j = 0; j < dptr->numunits; j++) {
            addr = GET_UADDR(uptr->u3);
           if ((uptr->flags & UNIT_DIS) == 0)
                dev_unit[addr] = dibp;
           if (dibp->dev_ini != NULL)
               dibp->dev_ini(uptr, 1);
           uptr++;

        }
    }
    return SCPE_OK;
}

/* Sets the device onto a given channel */
t_stat
set_dev_addr(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE             *dptr;
    DIB                *dibp;
    t_value             newdev;
    t_stat              r;
    int                 i;
    int                 devaddr;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;

    dibp = (DIB *) dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;

    newdev = get_uint (cptr, 16, 0xfff, &r);

    if (r != SCPE_OK)
        return r;

    if ((newdev >> 8) > (t_value)channels)
        return SCPE_ARG;

    if (newdev >= MAX_DEV)
        return SCPE_ARG;

    devaddr = GET_UADDR(uptr->u3);

    /* Clear out existing entry */
    if (dptr->flags & DEV_UADDR) {
        dev_unit[devaddr] = NULL;
    } else {
        devaddr &= dibp->mask | 0xf00;
        for (i = 0; i < dibp->numunits; i++)
             dev_unit[devaddr + i] = NULL;
    }

    /* Check if device already at newdev */
    if (dptr->flags & DEV_UADDR) {
        if (dev_unit[newdev] != NULL)
            r = SCPE_ARG;
    } else {
        newdev &= dibp->mask | 0xf00;
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
        uptr->u3 &= ~UNIT_ADDR(0xfff);
        uptr->u3 |= UNIT_ADDR(devaddr);
        fprintf(stderr, "Set dev %s %x\n\r", dptr->name, GET_UADDR(uptr->u3));
    } else {
        fprintf(stderr, "Set dev %s0 %x\n\r",  dptr->name, devaddr);
        for (i = 0; i < dibp->numunits; i++)  {
             dev_unit[devaddr + i] = dibp;
             uptr = &((dibp->units)[i]);
             uptr->u3 &= ~UNIT_ADDR(0xfff);
             uptr->u3 |= UNIT_ADDR(devaddr + i);
        }
    }
    return r;
}

/* Display the address of the device */
t_stat
show_dev_addr(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    DEVICE             *dptr;
    int                 addr;


    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    addr = GET_UADDR(uptr->u3);
    fprintf(st, "DEV=%03x", addr);
    return SCPE_OK;
}

