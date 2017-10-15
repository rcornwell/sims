/* ibm360_chan.c: IBM 360 Channel functions.

   Copyright (c) 2007, Richard Cornwell

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

#define BUFF_EMPTY       0x4                /* Buffer is empty */
#define BUFF_DIRTY       0x8                /* Buffer is dirty flag */
#define BUFF_NEWCMD      0x10               /* Channel ready for new command */
#define BUFF_CHNEND      0x20               /* Channel end */

#define AMASK            0x00ffffff
#define PMASK            0xf0000000         /* Storage protection mask */
extern uint32  *M;
extern uint8   key[MAXMEMSIZE / 2048];
extern UNIT    cpu_unit;

#define MAX_DEV         (MAX_CHAN * 256)
int         subchannels         = SUB_CHANS;         /* Number of subchannels */
int         irq_pend = 0;
uint32      caw[256];                       /* Channel command address word */
uint32      ccw_addr[256];                  /* Channel address */
uint16      ccw_count[256];                 /* Channel count */
uint8       ccw_cmd[256];                   /* Channel command and flags */
uint16      ccw_flags[256];                 /* Channel flags */
uint16      chan_status[256];               /* Channel status */
uint16      chan_dev[256];                  /* Device on channel */
uint32      chan_buf[256];                  /* Channel data buffer */
uint8       chan_byte[256];                 /* Current byte, dirty/full */
DIB         *dev_unit[MAX_DEV];             /* Pointer to Device info block */
uint8       dev_status[MAX_DEV];            /* last device status flags */

/* Find unit pointer for given device */
UNIT         *
find_chan_dev(uint16 addr) {
    struct dib         *dibp;
    UNIT               *uptr;
    int                 i;

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
    if (device > MAX_DEV)
        return -1;
    if (device > 0xff) {
        chan = (device >> 8) & 0x7;
        if (chan > MAX_CHAN)
            return -1;
        return subchannels + chan;
    }
    if (device < subchannels)
        return device;
    return ((device - subchannels)>>4) & 0xf;
}

int
readfull(int chan, uint32 addr, uint32 *word) {
    int sk, k;
    if ((addr & AMASK) > MEMSIZE) {
        chan_status[chan] |= STATUS_PCHK;
        return 1;
    }
    sk = (addr >> 24) & 0xff;
    if (sk != 0) {
        if ((cpu_dev.flags & PROTECT) == 0) {
            chan_status[chan] |= STATUS_PROT;
            return 1;
        }
        k = key[(addr & 0xfffc00) >> 10];
        if ((k & 0x8) != 0 && (k & 0xf0) != sk) {
            chan_status[chan] |= STATUS_PROT;
            return 1;
        }
    }
    addr &= AMASK;
    addr >>= 2;
    *word = M[addr];
    return 0;
}

int writefull(int chan, uint32 addr, uint32 *word) {
    int sk, k;
    if ((addr & AMASK) > MEMSIZE) {
        chan_status[chan] |= STATUS_PCHK;
        return 1;
    }
    sk = (addr >> 24) & 0xff;
    if (sk != 0) {
        if ((cpu_dev.flags & PROTECT) == 0) {
            chan_status[chan] |= STATUS_PROT;
            return 1;
        }
         k = key[(addr & 0xfffc00) >> 10];
        if ((k & 0x8) != 0 && (k & 0xf0) != sk) {
            chan_status[chan] |= STATUS_PROT;
            return 1;
        }
    }
fprintf(stderr, "Channel write %02x %06x %08x %08x '", chan, addr, *word, ccw_count[chan]);
    addr &= AMASK;
    addr >>= 2;
    M[addr] = *word;
    for(k = 24; k >= 0; k -= 8) {
        char ch = ebcdic_to_ascii[(*word >> k) & 0xFF];
        if (ch < 0x20 || ch == 0xff)
            ch = '.';
        fprintf(stderr, "%c", ch);
    }

 fprintf(stderr, "'\n\r");
    return 0;
}

int
load_ccw(uint16 chan, int tic_ok) {
    uint32         word;
    int            cmd = 0;
    UNIT          *uptr;

loop:
    if ((caw[chan] & 0x7) != 0) {
        chan_status[chan] |= STATUS_PCHK;
        return 1;
    }
    /* Check if we have status modifier set */
    if (chan_status[chan] & STATUS_MOD) {
        caw[chan]+=8;
        caw[chan] &= PMASK|AMASK;         /* Mask overflow bits */
        chan_status[chan] &= ~STATUS_MOD;
    }
    readfull(chan, caw[chan], &word);
fprintf(stderr, "Channel read ccw  %02x %06x %08x\n\r", chan, caw[chan], word);
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
    caw[chan] &= PMASK|AMASK;         /* Mask overflow bits */
    /* Check if fetch ok */
    if ((ccw_flags[chan] & FLAG_CD) == 0) {
        ccw_cmd[chan] = (word >> 24) & 0xff;
        cmd = 1;
    }
    ccw_addr[chan] = word & AMASK;
    ccw_addr[chan] |= caw[chan] & PMASK;         /* Copy key */
    readfull(chan, caw[chan], &word);
fprintf(stderr, "Channel read ccw2 %02x %06x %08x\n\r", chan, caw[chan], word);
    caw[chan]+=4;
    caw[chan] &= PMASK|AMASK;         /* Mask overflow bits */
    ccw_count[chan] = word & 0xffff;
    ccw_flags[chan] = (word >> 16) & 0xffff;
    chan_byte[chan] = BUFF_EMPTY;
    if (ccw_flags[chan] & FLAG_PCI) {
        chan_status[chan] |= STATUS_PCI;
        irq_pend = 1;
    }
    /* Check invalid count */
    if (ccw_count[chan] == 0) {
        chan_status[chan] |= STATUS_PCHK;
        irq_pend = 1;
        return 1;
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
             chan_status[chan] |= STATUS_CEND;
             ccw_flags[chan] = 0;
             ccw_cmd[chan] = 0;
             irq_pend = 1;
             return 1;
         }
         if (chan_status[chan] & (STATUS_DEND|STATUS_CEND)) {
             chan_status[chan] |= STATUS_CEND;
             chan_byte[chan] = BUFF_NEWCMD;
             ccw_cmd[chan] = 0;
             irq_pend = 1;
        }
    }
    return 0;
}


/* read byte from memory */
int
chan_read_byte(uint16 addr, uint8 *data) {
    int         chan = find_subchan(addr);
    int         byte;
    int         k;

    if ((ccw_cmd[chan] & 0x1)  == 0) {
        return 1;
    }
    if (chan_byte[chan] == BUFF_CHNEND)
        return 1;
    if (chan_byte[chan] == BUFF_EMPTY) {
         if (readfull(chan, ccw_addr[chan], &chan_buf[chan])) {
            chan_byte[chan] = BUFF_CHNEND;
            irq_pend = 1;
            return 1;
         }
fprintf(stderr, "Channel read %02x %06x %08x %08x '", chan, ccw_addr[chan], chan_buf[chan], ccw_count[chan]);
         for(k = 24; k >= 0; k -= 8) {
             char ch = ebcdic_to_ascii[(chan_buf[chan] >> k) & 0xFF];
             if (ch < 0x20 || ch == 0xff)
                ch = '.';
             fprintf(stderr, "%c", ch);
         }

 fprintf(stderr, "'\n\r");
         chan_byte[chan] = ccw_addr[chan] & 0x3;
         ccw_addr[chan] += 4 - chan_byte[chan];
    }
    ccw_count[chan]--;
    byte = (chan_buf[chan] >>  (8 * (3 - (chan_byte[chan] & 0x3)))) & 0xff;
    chan_byte[chan]++;
    *data = byte;
    if (ccw_count[chan] == 0) {
         if (ccw_flags[chan] & FLAG_CD)
            return load_ccw(chan, 1);
         else {
            chan_status[chan] |= STATUS_CEND;
            chan_byte[chan] = BUFF_CHNEND;
        }
fprintf(stderr, "chan_read_end\n\r");
        return 1;
    }
    return 0;
}

/* write byte to memory */
int
chan_write_byte(uint16 addr, uint8 *data) {
    int          chan = find_subchan(addr);
    int          byte;
    int          offset;
    uint32       mask;

    if ((ccw_cmd[chan] & 0x1)  != 0) {
        return 1;
    }
    if (chan_byte[chan] == BUFF_CHNEND) {
        if ((ccw_flags[chan] & FLAG_SLI) == 0) {
            chan_status[chan] |= STATUS_LENGTH;
        }
        return 1;
    }
    if (ccw_flags[chan] & FLAG_SKIP) {
        ccw_count[chan]--;
        if ((ccw_cmd[chan] & 0xf) == CMD_RDBWD)
            ccw_addr[chan]--;
        else
            ccw_addr[chan]++;
        if (ccw_count[chan] == 0) {
            if (ccw_flags[chan] & FLAG_CD)
               return load_ccw(chan, 1);
            else
               chan_byte[chan] = BUFF_CHNEND;
           return 1;
        }
        return 0;
    }
    if (chan_byte[chan] == (BUFF_EMPTY|BUFF_DIRTY)) {
         if (writefull(chan, ccw_addr[chan], &chan_buf[chan])) {
            chan_byte[chan] = BUFF_CHNEND;
            irq_pend = 1;
            return 1;
         }
         if ((ccw_cmd[chan] & 0xf) == CMD_RDBWD)
            ccw_addr[chan] -= 1 + (ccw_addr[chan] & 0x3);
         else
            ccw_addr[chan] += 4 - (ccw_addr[chan] & 0x3);
         chan_byte[chan] = BUFF_EMPTY;
    }
    if (chan_byte[chan] == BUFF_EMPTY) {
        if (readfull(chan, ccw_addr[chan], &chan_buf[chan])) {
            chan_byte[chan] = BUFF_CHNEND;
            irq_pend = 1;
            return 1;
        }
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
    if (ccw_count[chan] == 0) {
        if (writefull(chan, ccw_addr[chan], &chan_buf[chan])) {
            chan_byte[chan] = BUFF_CHNEND;
            return 1;
        }
        if (ccw_flags[chan] & FLAG_CD)
            return load_ccw(chan, 1);
        chan_byte[chan] = BUFF_CHNEND;
fprintf(stderr, "chan_write_end\n\r");
        return 1;
    }
    return 0;
}

void
set_devattn(uint16 addr, uint8 flags) {
    int          chan = find_subchan(addr);

    if (chan_dev[chan] == addr && (chan_status[chan] & STATUS_CEND) != 0 &&
            (flags & SNS_DEVEND) != 0) {
        chan_status[chan] |= ((uint16)flags) << 8;
    } else
        dev_status[addr] = flags;
fprintf(stderr, "set_devattn(%x, %x) %x\n\r", addr, flags, chan_dev[chan]);
    irq_pend = 1;
}

void
chan_end(uint16 addr, uint8 flags) {
    int         chan = find_subchan(addr);

fprintf(stderr, "chan_end(%x, %x)\n\r", addr, flags);
    if (chan_byte[chan] & BUFF_DIRTY) {
        writefull(chan, ccw_addr[chan], &chan_buf[chan]);
        chan_byte[chan] = BUFF_EMPTY;
    }
    chan_status[chan] |= STATUS_CEND;
    chan_status[chan] |= ((uint16)flags) << 8;
    ccw_cmd[chan] = 0;
    if (ccw_count[chan] != 0 &&  (ccw_flags[chan] & FLAG_SLI) == 0) {
        chan_status[chan] |= STATUS_LENGTH;
        ccw_flags[chan] = 0;
    }
    if (flags & (SNS_ATTN|SNS_UNITCHK|SNS_UNITEXP)) {
        ccw_flags[chan] = 0;
    }

    if (chan_status[chan] & (STATUS_DEND|STATUS_CEND)) {
        chan_byte[chan] = BUFF_NEWCMD;
    }

    irq_pend = 1;
}

int
store_csw(uint16 chan) {
    M[0x40 >> 2] = caw[chan];
    M[0x44 >> 2] = (((uint32)ccw_count[chan])) | ((uint32)chan_status[chan]<<16);
fprintf(stderr, "Channel store csw  %02x %06x %08x\n\r", chan, M[0x40>>2], M[0x44 >> 2]);
    return chan_dev[chan];
}


int  startio(uint16 addr) {
    int          chan = find_subchan(addr);
    DIB         *dibp = dev_unit[addr];
    UNIT        *uptr;
    uint8        status;

fprintf(stderr, "SIO %x %x %x %x\n\r", addr, chan, ccw_cmd[chan], ccw_flags[chan]);
    if (chan < 0 || dibp == 0)
        return 3;
    uptr = find_chan_dev(addr);
    if (uptr == 0)
        return 3;
    if (ccw_cmd[chan] != 0 || (ccw_flags[chan] & (FLAG_CD|FLAG_CC)) != 0)
        return 2;
    chan_status[chan] = 0;
    dev_status[addr] = 0;
    caw[chan] = M[0x48>>2];
    chan_dev[chan] = addr;
    if (dibp->start_io != NULL) {
        chan_status[chan] = dibp->start_io(uptr, chan) << 8;
        if (chan_status[chan] != 0) {
            M[0x44 >> 2] = ((uint32)chan_status[chan]<<16) | M[0x44 >> 2] & 0xffff;
            fprintf(stderr, "Channel store csw  %02x %08x\n\r", chan, M[0x44 >> 2]);
            chan_status[chan] = 0;
            return 1;
        }
    }
    if (load_ccw(chan, 0) || (chan_status[chan] & (STATUS_PCI))) {
        M[0x44 >> 2] = ((uint32)chan_status[chan]<<16) | M[0x44 >> 2] & 0xffff;
        fprintf(stderr, "Channel store csw  %02x %08x\n\r", chan, M[0x44 >> 2]);
        chan_status[chan] &= ~STATUS_PCI;
        dev_status[addr] = 0;
        return 1;
    }
    return 0;
}

int testio(uint16 addr) {
    int          chan = find_subchan(addr);
    DIB         *dibp = dev_unit[addr];
    UNIT        *uptr;
    uint8        status;

    if (chan < 0 || dibp == 0)
        return 3;
    uptr = find_chan_dev(addr);
    if (uptr == 0)
        return 3;
    if (ccw_cmd[chan] != 0 || (ccw_flags[chan] & (FLAG_CD|FLAG_CC)) != 0)
        return 2;
    if (chan_dev[chan] != addr)
        return 2;
    if (ccw_cmd[chan] == 0 && chan_status[chan] != 0) {
        store_csw(chan);
        chan_status[chan] = 0;
        dev_status[addr] = 0;
        return 1;
    }
    if (dev_status[addr] & (SNS_ATTN|SNS_UNITCHK|SNS_UNITEXP)) {
        M[0x40 >> 2] = 0;
        M[0x44 >> 2] = ((uint32)dev_status[addr]) << 24;
        dev_status[addr] = 0;
        return 1;
    }
fprintf(stderr, "Testio %x %x\n\r", addr, chan_status[chan]);
    chan_status[chan] = dibp->start_cmd(uptr, chan, 0) << 8;
    if (chan_status[chan] & (STATUS_ATTN|STATUS_CHECK|STATUS_EXPT)) {
        M[0x44 >> 2] = ((uint32)chan_status[chan]<<16) | M[0x44 >> 2] & 0xffff;
        chan_status[chan] = 0;
        dev_status[addr] = 0;
        return 1;
    }
    chan_status[chan] = 0;
    return 0;
}

int haltio(uint16 addr) {
    int           chan = find_subchan(addr);
    DIB          *dibp = dev_unit[addr];
    UNIT         *uptr;
    uint8         status;

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
    if (chan_status[chan] & (STATUS_ATTN|STATUS_PCI|STATUS_EXPT|STATUS_CHECK|
                  STATUS_PROT|STATUS_CDATA|STATUS_CCNTL|STATUS_INTER|
                  STATUS_CHAIN))
        return 0;
    return 0;
}

int testchan(uint16 channel) {
    uint16         st = 0;
    channel >>= 8;
    if (channel = 0)
        return 0;
    st = chan_status[subchannels + channel];
    if (st & STATUS_BUSY)
        return 2;
    if (st & (STATUS_ATTN|STATUS_PCI|STATUS_EXPT|STATUS_CHECK|
                  STATUS_PROT|STATUS_CDATA|STATUS_CCNTL|STATUS_INTER|
                  STATUS_CHAIN))
        return 1;
    return 0;
}

t_stat chan_boot(uint16 addr, DEVICE *dptyr) {
    int          chan = find_subchan(addr);
    DIB         *dibp = dev_unit[addr];
    UNIT        *uptr;
    uint8        status;
    int          i;

    if (chan < 0 || dibp == 0)
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

/* Scan all channels and see if one is ready to start or has
   interrupt pending.
*/
uint16 scan_chan(uint8 mask) {
     int         i;
     int         pend = 0;         /* No device */
     int         imask = 0x80;

     if (irq_pend == 0)
         return 0;
     irq_pend = 0;
     for (i = 0; i < subchannels + MAX_CHAN; i++) {
         if (i >= subchannels)
            imask = imask / 2;

         /* If channel end, check if we should continue */
         if (chan_status[i] & STATUS_CEND) {
             if (ccw_flags[i] & FLAG_CC) {
//fprintf(stderr, "Scan(%x %x) CC\n\r", i, chan_status[i]);
                if (chan_status[i] & STATUS_DEND)
                          (void)load_ccw(i, 1);
                else
                    irq_pend = 1;
             } else {
fprintf(stderr, "Scan(%x %x %x %x) end\n\r", i, chan_status[i], imask, mask);
                if ((imask & mask) != 0 || loading != 0) {
                    pend = chan_dev[i];
                    break;
                }
             }
         }
     }
     if (pend) {
          irq_pend = 1;
          i = find_subchan(pend);
fprintf(stderr, "Scan end (%x %x)\n\r", chan_dev[i], pend);
          store_csw(i);
          chan_status[i] = 0;
          chan_dev[i] = 0;
          dev_status[pend] = 0;
     } else {
          for (i = 0; i < MAX_DEV; i++) {
             if (dev_status[i] != 0) {
                 pend = find_subchan(i);
                 if (ccw_cmd[pend] == 0 && mask & (0x80 >> (i >> 8))) {
                     irq_pend = 1;
fprintf(stderr, "Set atten %03x %02x\n\r", i, dev_status[i]);
//                   M[0x40 >> 2] = 0;
                     M[0x44 >> 2] = (((uint32)dev_status[i]) << 24) |
                           (M[0x44>>2] & 0xffff);
                     dev_status[i] = 0;
                     return i;
                 }
             }
         }
         pend = 0;
     }
     return pend;
}


t_stat
chan_set_devs()
{
    int                 i, j;

    for(i = 0; i < MAX_DEV; i++) {
        dev_unit[i] = NULL;                  /* Device pointer */
    }
    /* Build channel array */
    for (i = 0; sim_devices[i] != NULL; i++) {
        DEVICE             *dptr = sim_devices[i];
        UNIT               *uptr = dptr->units;
        DIB                *dibp = (DIB *) dptr->ctxt;
        int                 addr;
        int                 chan;

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
    int                 num;
    int                 type;
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

    devaddr = GET_UADDR(uptr->u3);
    if (newdev > MAX_DEV)
        return SCPE_ARG;

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
        fprintf(stderr, "Set dev %x\n\r", devaddr);
    } else {
        for (i = 0; i < dibp->numunits; i++)  {
             dev_unit[devaddr + i] = dibp;
             fprintf(stderr, "Set dev %x\n\r", devaddr + i);
        }
    }
    return r;
}

t_stat
show_dev_addr(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    DEVICE             *dptr;
    DIB                *dibp;
    int                 addr;


    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    addr = GET_UADDR(uptr->u3);
    fprintf(st, "%03x", addr);
    return SCPE_OK;
}

