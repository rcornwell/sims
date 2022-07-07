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
#define STATUS_LENGTH    0x0040             /* Incorrect length */
#define STATUS_PCHK      0x0020             /* Program check */
#define STATUS_PROT      0x0010             /* Protection check */
#define STATUS_CDATA     0x0008             /* Channel data check */
#define STATUS_CCNTL     0x0004             /* Channel control check */
#define STATUS_INTER     0x0002             /* Channel interface check */
#define STATUS_CHAIN     0x0001             /* Channel chain check */

#define ERROR_STATUS         (STATUS_ATTN|STATUS_PCI|STATUS_EXPT|STATUS_CHECK| \
                              STATUS_PROT|STATUS_CDATA|STATUS_CCNTL|STATUS_INTER| \
                              STATUS_CHAIN)

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

#define UNIT_V_TYPE        (UNIT_V_UF + 0)
#define UNIT_SEL           (1 << UNIT_V_TYPE)   /* Selector channel */
#define UNIT_MUX           (0 << UNIT_V_TYPE)   /* Multiplexer channel */
#define UNIT_BMUX          (2 << UNIT_V_TYPE)   /* Multiplexer channel */
#define UNIT_M_TYPE        (3 << UNIT_V_TYPE)

#define UNIT_V_SUBCHAN     (UNIT_V_UF + 2)
#define UNIT_M_SUBCHAN     0xff
#define UNIT_G_SCHAN(x)    ((((x) >> UNIT_V_SUBCHAN) & UNIT_M_SUBCHAN) + 1)
#define UNIT_SCHAN(x)      (((x - 1) & UNIT_M_SUBCHAN) << UNIT_V_SUBCHAN)

int         irq_pend = 0;
t_stat      chan_reset(DEVICE *);
t_stat      set_subchan(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat      show_subchan(FILE * st, UNIT * uptr, int32 v, CONST void *desc);
t_stat      chan_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *chan_description (DEVICE *dptr);

MTAB                chan_mod[] = {
    { UNIT_M_TYPE, UNIT_MUX,  NULL, "MUX", NULL, NULL, NULL, "Multiplexer channel"},
    { UNIT_M_TYPE, UNIT_SEL,  NULL, "SEL", NULL, NULL, NULL, "Selector channel"},
    { UNIT_M_TYPE, UNIT_BMUX, NULL, "BMUX", NULL, NULL, NULL, "Block Multiplexer channel"},
    { MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "SUB", "SUB", set_subchan, show_subchan, NULL,
                                    "Number of subchannels"},
    {0}
};

#define SEL_CHAN     UNIT_SEL | UNIT_SCHAN(SUB_CHANS) | UNIT_DISABLE
#define MUX_CHAN     UNIT_MUX | UNIT_SCHAN(SUB_CHANS) | UNIT_DISABLE

UNIT                chan_unit[] = {
    {UDATA(NULL, MUX_CHAN, 0),          0, 0, },       /* 0 */
    {UDATA(NULL, SEL_CHAN, 0),          0, 0, },       /* 1 */
    {UDATA(NULL, SEL_CHAN, 0),          0, 0, },       /* 2 */
    {UDATA(NULL, SEL_CHAN, 0),          0, 0, },       /* 3 */
#if MAX_CHAN > 3
    {UDATA(NULL, MUX_CHAN|UNIT_DIS, 0), 0, 0, },       /* 4 */
#endif
#if MAX_CHAN > 4
    {UDATA(NULL, SEL_CHAN|UNIT_DIS, 0), 0, 0, },       /* 5 */
#endif
#if MAX_CHAN > 5
    {UDATA(NULL, SEL_CHAN|UNIT_DIS, 0), 0, 0, },       /* 6 */
#endif
#if MAX_CHAN > 6
    {UDATA(NULL, SEL_CHAN|UNIT_DIS, 0), 0, 0, },       /* 7 */
#endif
#if MAX_CHAN > 7
    {UDATA(NULL, MUX_CHAN|UNIT_DIS, 0), 0, 0, },       /* 8 */
#endif
#if MAX_CHAN > 8
    {UDATA(NULL, SEL_CHAN|UNIT_DIS, 0), 0, 0, },       /* 9 */
#endif
#if MAX_CHAN > 9
    {UDATA(NULL, MUX_CHAN|UNIT_DIS, 0), 0, 0, },       /* a */
#endif
#if MAX_CHAN > 10
    {UDATA(NULL, SEL_CHAN|UNIT_DIS, 0), 0, 0, },       /* b */
#endif
#if MAX_CHAN > 11
    {UDATA(NULL, SEL_CHAN|UNIT_DIS, 0), 0, 0, },       /* c */
#endif
#if MAX_CHAN > 12
    {UDATA(NULL, SEL_CHAN|UNIT_DIS, 0), 0, 0, },       /* d */
#endif
#if MAX_CHAN > 13
    {UDATA(NULL, SEL_CHAN|UNIT_DIS, 0), 0, 0, },       /* e */
#endif
#if MAX_CHAN > 14
    {UDATA(NULL, SEL_CHAN|UNIT_DIS, 0), 0, 0, },       /* f */
#endif
};

DEVICE              chan_dev = {
    "CH", chan_unit, NULL, chan_mod,
    MAX_CHAN, 8, 15, 1, 8, 8,
    NULL, NULL, &chan_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &chan_help, NULL, NULL, &chan_description
};

struct _dev {
    DIB        *dibp;                  /* Pointer to channel DIB entry */
    UNIT       *unit;                  /* Pointer to the Unit structure */
    uint16      dev_addr;              /* Address of device */
};

struct _chanctl {
    struct _dev *dev;                  /* Pointer to dev structure for current dev */
    uint32      caw;                   /* Channel command address word */
    uint32      ccw_addr;              /* Channel address */
    uint32      ccw_iaddr;             /* Channel indirect address */
    uint16      ccw_count;             /* Channel count */
    uint8       ccw_cmd;               /* Channel command and flags */
    uint8       ccw_key;               /* Channel key */
    uint32      chan_buf;              /* Channel data buffer */
    uint16      ccw_flags;             /* Channel flags */
    uint16      chan_status;           /* Channel status */
    uint16      daddr;                 /* Device on channel */
    uint8       chan_byte;             /* Current byte, dirty/full */
    uint8       chain_flg;             /* Holding on chain */
};

uint8        dev_status[MAX_CHAN * 256]; /* Device status array */
uint8        chan_pend[MAX_CHAN];        /* Status pending on channel */

#define chan_ctl      ((struct _chanctl *)(uptr->up7))
#define dev_tab       ((struct _dev *)(uptr->up8))
#define schans        u3                 /* Number of subchannels */

/* Find unit pointer for given device */
struct _dev      *
find_device(uint16 addr) {
    UNIT               *uptr;
    int                 chan;

    chan = (addr >> 8) & 0xf;
    if (chan > MAX_CHAN)
       return NULL;
    uptr = &chan_unit[chan];
    if ((uptr->flags & UNIT_DIS) != 0)
       return NULL;

    if (uptr->up8 == NULL)
       return NULL;
    return &(dev_tab[addr & 0xff]);
}

/* channel:
         subchannels = 128
         0 - 7       0x80-0xff
        8 - 127     0x00-0x7f
        128 - +6    0x1xx - 0x6xx
*/

/* look up device to find subchannel device is on */
struct _chanctl *
find_subchan(uint16 device) {
    int      chan;
    UNIT    *uptr;

    chan = (device >> 8) & 0xf;
    if (chan > MAX_CHAN)
       return NULL;
    uptr = &chan_unit[chan];
    if ((uptr->flags & UNIT_DIS) != 0)
       return NULL;
    if ((uptr->flags & UNIT_M_TYPE) == UNIT_SEL) {
       return &(chan_ctl[0]);
    }
    device &= 0xff;
    if ((uptr->flags & UNIT_M_TYPE) == UNIT_BMUX) {
        extern uint32    cregs[16];
        if ((cpu_unit[0].flags & FEAT_370) != 0 &&
            (cregs[0] & 0x80000000) != 0) {
            device = (device >> 3) & 0x1f;
            return &(chan_ctl[device]);
        }
        return &(chan_ctl[0]);
    }
    if (device >= uptr->schans) {
       if (device <= 128) { /* All shared devices over subchannels */
           return NULL;
       }
       device = (device >> 4) & 0x7;
    }
    return &(chan_ctl[device]);
}

/* Read a full word into memory.
 * Return 1 if fail.
 * Return 0 if success.
 */
int
readfull(struct _chanctl *chan, uint32 addr, uint32 *word) {
    if ((addr & AMASK) > MEMSIZE) {
        chan->chan_status |= STATUS_PCHK;
        *word = 0;
        irq_pend = 1;
        return 1;
    }
    addr >>= 2;
    if (chan->ccw_key != 0) {
        int k;
        if ((cpu_unit[0].flags & FEAT_PROT) == 0) {
            chan->chan_status |= STATUS_PROT;
            *word = 0;
            irq_pend = 1;
            return 1;
        }
        k = key[addr >> 9];
        if ((k & 0x8) != 0 && (k & 0xf0) != chan->ccw_key) {
            chan->chan_status |= STATUS_PROT;
            *word = 0;
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
readbuff(struct _chanctl *chan) {
    int k;
    uint32 addr;
    if (chan->ccw_flags & FLAG_IDA && cpu_unit[0].flags & FEAT_370)
       addr = chan->ccw_iaddr;
    else
       addr = chan->ccw_addr;
    if (readfull(chan, addr, &chan->chan_buf)) {
        chan->chan_byte = BUFF_CHNEND;
        return 1;
    }
    if ((chan->ccw_cmd & 1) == 1) {
        sim_debug(DEBUG_CDATA, &cpu_dev, "Channel write %03x %03x %08x %08x '",
              chan->daddr, addr, chan->chan_buf, chan->ccw_count);
        for(k = 24; k >= 0; k -= 8) {
            unsigned char ch = ebcdic_to_ascii[(chan->chan_buf >> k) & 0xFF];
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
writebuff(struct _chanctl *chan) {
    uint32 addr;
    int k;
    if (chan->ccw_flags & FLAG_IDA && cpu_unit[0].flags & FEAT_370)
       addr = chan->ccw_iaddr;
    else
       addr = chan->ccw_addr;
    if ((addr & AMASK) > MEMSIZE) {
        chan->chan_status |= STATUS_PCHK;
        chan->chan_byte = BUFF_CHNEND;
        irq_pend = 1;
        return 1;
    }
    addr >>= 2;
    if (chan->ccw_key != 0) {
        if ((cpu_unit[0].flags & FEAT_PROT) == 0) {
            chan->chan_status |= STATUS_PROT;
            chan->chan_byte = BUFF_CHNEND;
            irq_pend = 1;
            return 1;
        }
        k = key[addr >> 9];
        if ((k & 0xf0) != chan->ccw_key) {
            chan->chan_status |= STATUS_PROT;
            chan->chan_byte = BUFF_CHNEND;
            irq_pend = 1;
            return 1;
        }
    }
    key[addr >> 9] |= 0x6;
    M[addr] = chan->chan_buf;
    sim_debug(DEBUG_CDATA, &cpu_dev, "Channel readf %03x %06x %08x %08x '",
          chan->daddr, addr << 2, chan->chan_buf, chan->ccw_count);
    for(k = 24; k >= 0; k -= 8) {
        unsigned char ch = ebcdic_to_ascii[(chan->chan_buf >> k) & 0xFF];
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
load_ccw(struct _chanctl *chan, int tic_ok) {
    uint32         word;
    int            cmd = 0;
    int            cc = 0;

loop:
    /* If last chain, start command */
    if (chan->chain_flg == 1 && (chan->ccw_flags & FLAG_CD) == 0) {
       sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel %03x chain restart\n",
                     chan->daddr);
       cc = 1;
       chan->chain_flg = 0;
       cmd = chan->ccw_cmd;
       goto start_cmd;
    }

    /* Abort if channel not on double boundry */
    if ((chan->caw & 0x7) != 0) {
        chan->chan_status |= STATUS_PCHK;
        return 1;
    }
    /* Abort if we have any errors */
    if (chan->chan_status & 0x7f)
        return 1;

    /* remember if we were chaining */
    if ((chan->ccw_flags & FLAG_CC) != 0)
       cc = 1;
    /* Check if we have status modifier set */
    if (chan->chan_status & STATUS_MOD) {
        chan->caw+=8;
        chan->caw &= PMASK|AMASK;         /* Mask overflow bits */
        chan->chan_status &= ~STATUS_MOD;
    }
    /* Read in next CCW */
    readfull(chan, chan->caw, &word);
    sim_debug(DEBUG_CMD, &cpu_dev, "Channel read ccw  %03x %06x %08x\n",
              chan->daddr, chan->caw, word);
    /* TIC can't follow TIC nor be first in chain */
    if (((word >> 24) & 0xf) == CMD_TIC) {
        if (tic_ok) {
            chan->caw = (chan->caw & PMASK) | (word & AMASK);
            tic_ok = 0;
            goto loop;
        }
        chan->chan_status |= STATUS_PCHK;
        irq_pend = 1;
        return 1;
    }
    chan->caw += 4;
    chan->caw &= AMASK;                   /* Mask overflow bits */
    /* Check if not chaining data */
    if ((chan->ccw_flags & FLAG_CD) == 0) {
        chan->ccw_cmd = (word >> 24) & 0xff;
        cmd = 1;
    }
    /* Set up for this command */
    chan->ccw_addr = word & AMASK;
    readfull(chan, chan->caw, &word);
    sim_debug(DEBUG_CMD, &cpu_dev, "Channel read ccw2 %03x %06x %08x\n",
             chan->daddr, chan->caw, word);
    chan->caw+=4;
    chan->caw &= AMASK;                  /* Mask overflow bits */
    chan->ccw_count = word & 0xffff;
    /* Copy SLI indicator on CD command */
    if ((chan->ccw_flags & (FLAG_CD|FLAG_SLI)) == (FLAG_CD|FLAG_SLI))
         word |= (FLAG_SLI<<16);
    chan->ccw_flags = (word >> 16) & 0xff00;
    chan->chan_byte = BUFF_EMPTY;
    /* Check invalid count */
    if (chan->ccw_count == 0) {
        chan->chan_status |= STATUS_PCHK;
        chan->ccw_cmd = 0;
        irq_pend = 1;
        return 1;
    }
    if (chan->ccw_flags & FLAG_IDA && cpu_unit[0].flags & FEAT_370) {
        readfull(chan, chan->ccw_addr, &word);
        chan->ccw_iaddr = word & AMASK;
        sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel fetch idaw %03x %08x\n",
             chan->daddr, chan->ccw_iaddr);
    }
start_cmd:
    if (cmd) {
         struct _dev  *dev = chan->dev;
         DIB          *dibp;
         UNIT         *uptr;

         /* Check if invalid command */
         if ((chan->ccw_cmd & 0xF) == 0) {
             chan->chan_status |= STATUS_PCHK;
             chan->ccw_cmd = 0;
             irq_pend = 1;
             return 1;
         }
         if (dev == NULL)
             return 1;
         uptr = dev->unit;
         dibp = dev->dibp;
         if (uptr == NULL || dibp == NULL)
             return 1;
         chan->chan_status &= 0xff;
         chan->chan_status |= dibp->start_cmd(uptr, chan->ccw_cmd) << 8;
         /* If device is busy, check if last was CC, then mark pending */
         if (chan->chan_status & STATUS_BUSY) {
             sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel %03x busy %d\n",
                     chan->daddr, cc);
             if (cc) {
                chan->chain_flg = 1;
             }
             return 0;
         }

         /* Check if any errors from initial command */
         if (chan->chan_status & (STATUS_ATTN|STATUS_CHECK|STATUS_EXPT)) {
             sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel %03x abort %04x\n",
                     chan->daddr, chan->chan_status);
             chan->chan_status |= STATUS_CEND;
             chan->ccw_flags = 0;
             chan->ccw_cmd = 0;
             irq_pend = 1;
             return 1;
         }

         /* Check if immediate channel end */
         if (chan->chan_status & STATUS_CEND) {
             chan->ccw_cmd = 0;
             chan->ccw_flags |= FLAG_SLI;  /* Force SLI for immediate command */
             sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_end(%x load) %x %04x end\n", 
                          chan->daddr, chan->chan_status, chan->ccw_flags);
             if (chan->chan_status & (STATUS_DEND)) {
                 irq_pend = 1;
            }
         }
    }
    if (chan->ccw_flags & FLAG_PCI) {
        chan->chan_status |= STATUS_PCI;
        chan->ccw_flags &= ~FLAG_PCI;
        irq_pend = 1;
        sim_debug(DEBUG_CMD, &cpu_dev, "Set PCI %03x load\n", chan->daddr);
    }
    return 0;
}


/* read byte from memory */
int
chan_read_byte(uint16 addr, uint8 *data) {
    struct _chanctl *chan = find_subchan(addr);
    int              byte;

    /* Abort if we have any errors */
    if (chan == NULL)
        return 1;
    if (chan->chan_status & 0x7f)
        return 1;
    if ((chan->ccw_cmd & 0x1)  == 0) {
        return 1;
    }
    /* Check if finished transfer */
    if (chan->chan_byte == BUFF_CHNEND)
        return 1;
    /* Check if count is zero */
    if (chan->ccw_count == 0) {
         /* If not data channing, let device know there will be no
          * more data to come
          */
         if ((chan->ccw_flags & FLAG_CD) == 0) {
            chan->chan_status |= STATUS_CEND;
            chan->chan_byte = BUFF_CHNEND;
            sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_read_end\n");
            return 1;
         } else {
            /* If chaining try and start next CCW */
            if (load_ccw(chan, 1))
                return 1;
         }
    }
    /* Read in next work if buffer is in empty status */
    if (chan->chan_byte == BUFF_EMPTY) {
         if (readbuff(chan))
            return 1;
         if (chan->ccw_flags & FLAG_IDA && cpu_unit[0].flags & FEAT_370) {
             chan->chan_byte = chan->ccw_iaddr & 0x3;
             chan->ccw_iaddr += 4 - chan->chan_byte;
             if ((chan->ccw_iaddr & 0x7ff) == 0) {
                 uint32 temp;
                 chan->ccw_addr += 4;
                 readfull(chan, chan->ccw_addr, &temp);
                 chan->ccw_iaddr = temp & AMASK;
                 sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel fetch idaw %03x %08x\n",
                      chan->daddr, chan->ccw_iaddr);
             }
         } else {
             chan->chan_byte = chan->ccw_addr & 0x3;
             chan->ccw_addr += 4 - chan->chan_byte;
         }
    }
    /* Return current byte */
    chan->ccw_count--;
    byte = (chan->chan_buf >> (8 * (3 - (chan->chan_byte & 0x3)))) & 0xff;
    chan->chan_byte++;
    *data = byte;
    /* If count is zero and chainging load in new CCW */
    if (chan->ccw_count == 0 && (chan->ccw_flags & FLAG_CD) != 0) {
        chan->chan_byte = BUFF_EMPTY;
        /* Try and grab next CCW */
        if (load_ccw(chan, 1))
            return 1;
    }
    return 0;
}

/* write byte to memory */
int
chan_write_byte(uint16 addr, uint8 *data) {
    struct _chanctl *chan = find_subchan(addr);
    int              offset;
    uint32           mask;

    /* Abort if we have any errors */
    if (chan == NULL)
        return 1;
    if (chan->chan_status & 0x7f)
        return 1;
    if ((chan->ccw_cmd & 0x1)  != 0) {
        return 1;
    }
    /* Check if at end of transfer */
    if (chan->chan_byte == BUFF_CHNEND) {
        if ((chan->ccw_flags & FLAG_SLI) == 0) {
            chan->chan_status |= STATUS_LENGTH;
        }
        return 1;
    }
    /* Check if count is zero */
    if (chan->ccw_count == 0) {
        /* Flush the buffer if we got anything back. */
        if (chan->chan_byte & BUFF_DIRTY) {
            if (writebuff(chan))
                return 1;
        }
        /* If not data channing, let device know there will be no
         * more data to come
         */
        if ((chan->ccw_flags & FLAG_CD) == 0) {
            chan->chan_byte = BUFF_CHNEND;
            if ((chan->ccw_flags & FLAG_SLI) == 0) {
                sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_write_ length\n");
                chan->chan_status |= STATUS_LENGTH;
            }
            sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_write_end\n");
            return 1;
        }
        /* Otherwise try and grab next CCW */
        if (load_ccw(chan, 1))
            return 1;
    }
    /* If we are skipping, just adjust count */
    if (chan->ccw_flags & FLAG_SKIP) {
        chan->ccw_count--;
        chan->chan_byte = BUFF_EMPTY;
        if (chan->ccw_flags & FLAG_IDA && cpu_unit[0].flags & FEAT_370) {
            if ((chan->ccw_cmd & 0xf) == CMD_RDBWD) {
                chan->ccw_iaddr--;
                if ((chan->ccw_iaddr & 0x7ff) == 0x7ff) {
                    uint32 temp;
                    chan->ccw_addr += 4;
                    readfull(chan, chan->ccw_addr, &temp);
                    chan->ccw_iaddr = temp & AMASK;
                    sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel fetch idaw %03x %08x\n",
                         chan->daddr, chan->ccw_iaddr);
                }
            } else {
                chan->ccw_iaddr++;
                if ((chan->ccw_iaddr & 0x7ff) == 0) {
                    uint32 temp;
                    chan->ccw_addr += 4;
                    readfull(chan, chan->ccw_addr, &temp);
                    chan->ccw_iaddr = temp & AMASK;
                    sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel fetch idaw %03x %08x\n",
                         chan->daddr, chan->ccw_iaddr);
                }
            }
        } else {
            if ((chan->ccw_cmd & 0xf) == CMD_RDBWD)
                chan->ccw_addr--;
            else
                chan->ccw_addr++;
        }
        return 0;
    }
    /* Check if we need to save what we have */
    if (chan->chan_byte == (BUFF_EMPTY|BUFF_DIRTY)) {
        if (writebuff(chan))
            return 1;
        if (chan->ccw_flags & FLAG_IDA && cpu_unit[0].flags & FEAT_370) {
            if ((chan->ccw_cmd & 0xf) == CMD_RDBWD) {
               chan->ccw_iaddr -= 1 + (chan->ccw_iaddr & 0x3);
               if ((chan->ccw_iaddr & 0x7ff) == 0x7fc) {
                   uint32 temp;
                   chan->ccw_addr += 4;
                   readfull(chan, chan->ccw_addr, &temp);
                   chan->ccw_iaddr = temp & AMASK;
                   sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel fetch idaw %03x %08x\n",
                         chan->daddr, chan->ccw_iaddr);
               }
            } else {
               chan->ccw_iaddr += 4 - (chan->ccw_iaddr & 0x3);
               if ((chan->ccw_iaddr & 0x7ff) == 0) {
                   uint32 temp;
                   chan->ccw_addr += 4;
                   readfull(chan, chan->ccw_addr, &temp);
                   chan->ccw_iaddr = temp & AMASK;
                   sim_debug(DEBUG_DETAIL, &cpu_dev, "Channel fetch idaw %03x %08x\n",
                         chan->daddr, chan->ccw_iaddr);
               }
            }
        } else {
            if ((chan->ccw_cmd & 0xf) == CMD_RDBWD)
               chan->ccw_addr -= 1 + (chan->ccw_addr & 0x3);
            else
               chan->ccw_addr += 4 - (chan->ccw_addr & 0x3);
        }
        chan->chan_byte = BUFF_EMPTY;
    }
    if (chan->chan_byte == BUFF_EMPTY) {
        if (readbuff(chan))
            return 1;
        if (chan->ccw_flags & FLAG_IDA && cpu_unit[0].flags & FEAT_370)
            chan->chan_byte = chan->ccw_iaddr & 0x3;
        else
            chan->chan_byte = chan->ccw_addr & 0x3;
    }
    /* Store it in buffer and adjust pointer */
    chan->ccw_count--;
    offset = 8 * (chan->chan_byte & 0x3);
    mask = 0xff000000 >> offset;
    chan->chan_buf &= ~mask;
    chan->chan_buf |= ((uint32)(*data)) << (24 - offset);
    if ((chan->ccw_cmd & 0xf) == CMD_RDBWD) {
        if (chan->chan_byte & 0x3)
            chan->chan_byte--;
        else
            chan->chan_byte = BUFF_EMPTY;
    } else
        chan->chan_byte++;
    chan->chan_byte |= BUFF_DIRTY;
    /* If count is zero and chainging load in new CCW */
    if (chan->ccw_count == 0 && (chan->ccw_flags & FLAG_CD) != 0) {
        /* Flush buffer */
        if (writebuff(chan))
            return 1;
        chan->chan_byte = BUFF_EMPTY;
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
    struct _chanctl  *chan = find_subchan(addr);

    if (chan == NULL)
        return;

    /* Check if chain being held */
    if (chan->daddr == addr && chan->chain_flg != 0 &&
            (flags & SNS_DEVEND) != 0) {
        chan->chan_status |= ((uint16)flags) << 8;
    } else 
    /* Check if device is current on channel */
    if (chan->daddr == addr && (chan->chan_status & STATUS_CEND) != 0  &&
            (flags & SNS_DEVEND) != 0) {
        chan->chan_status |= ((uint16)flags) << 8;
    } else /* Device reporting status change */
    {
        dev_status[addr] = flags;
        chan_pend[(addr >> 8) & 0xf]= 1;
    }
    sim_debug(DEBUG_EXP, &cpu_dev, "set_devattn(%x, %x) %x\n",
                 addr, flags, chan->daddr);
    irq_pend = 1;
}

/*
 * Signal end of transfer by device.
 */
void
chan_end(uint16 addr, uint8 flags) {
    struct _chanctl *chan = find_subchan(addr);

    if (chan == NULL)
        return;

    sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_end(%x, %x) %x %04x %04x\n", addr, flags,
                 chan->ccw_count, chan->ccw_flags, chan->chan_status);

    /* Flush buffer if there was any change */
    if (chan->chan_byte & BUFF_DIRTY) {
        (void)(writebuff(chan));
        chan->chan_byte = BUFF_EMPTY;
    }
    chan->chan_status |= STATUS_CEND;
    chan->chan_status |= ((uint16)flags) << 8;
    chan->ccw_cmd = 0;

    /* If count not zero and not suppressing length, report error */
    if (chan->ccw_count != 0 && (chan->ccw_flags & FLAG_SLI) == 0) {
        sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_end length\n");
        chan->chan_status |= STATUS_LENGTH;
        chan->ccw_flags = 0;
    }
    if (chan->ccw_count != 0 && (chan->ccw_flags & (FLAG_CD|FLAG_SLI)) == (FLAG_CD|FLAG_SLI)) {
        sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_end length 2\n");
        chan->chan_status |= STATUS_LENGTH;
    }
    if (flags & (SNS_ATTN|SNS_UNITCHK|SNS_UNITEXP))
        chan->ccw_flags = 0;

    if ((flags & SNS_DEVEND) != 0)
        chan->ccw_flags &= ~(FLAG_CD|FLAG_SLI);

    irq_pend = 1;
    sim_debug(DEBUG_DETAIL, &cpu_dev, "chan_end(%x, %x) %x %04x end\n", addr, flags,
                 chan->chan_status, chan->ccw_flags);
}

/*
 * Save full csw.
 */
void
store_csw(struct _chanctl *chan) {
    M[0x40 >> 2] = chan->caw;
    M[0x44 >> 2] = (((uint32)chan->ccw_count)) | ((uint32)chan->chan_status<<16);
    key[0] |= 0x6;
    if (chan->chan_status & STATUS_PCI) {
        chan->chan_status &= ~STATUS_PCI;
        sim_debug(DEBUG_CMD, &cpu_dev, "Clr PCI %02x store\n", chan->daddr);
    } else {
        chan->chan_status = 0;
    }
    chan->ccw_flags &= ~FLAG_PCI;
    sim_debug(DEBUG_EXP, &cpu_dev, "Channel store csw  %03x %06x %08x\n",
          chan->daddr, M[0x40>>2], M[0x44 >> 2]);
}

/*
 * Handle SIO instruction.
 */
int
startio(uint16 addr) {
    struct _dev     *dev = find_device(addr);
    struct _chanctl *chan = find_subchan(addr);
    DIB             *dibp = NULL;
    UNIT            *uptr;
    uint16          status;

    if (dev == NULL || chan == NULL) {
        sim_debug(DEBUG_CMD, &cpu_dev, "SIO %x dc cc=3\n", addr);
        return 3;
    }

    dibp = dev->dibp;
    uptr = dev->unit;

    /* Find channel this device is on, if no none return cc=3 */
    if (dibp == NULL || uptr == NULL) {
        sim_debug(DEBUG_CMD, &cpu_dev, "SIO %x cu cc=3\n", addr);
        return 3;
    }

    sim_debug(DEBUG_CMD, &cpu_dev, "SIO %03x %03x %02x %x %x\n", addr, chan->daddr,
              chan->ccw_cmd, chan->ccw_flags, chan->chan_status);

    /* If pending status is for us, return it with status code */
    if (chan->daddr == addr && chan->chan_status != 0) {
        store_csw(chan);
        return 1;
    }

    /* If channel is active return cc=2 */
    if (chan->ccw_cmd != 0 ||
        (chan->ccw_flags & (FLAG_CD|FLAG_CC)) != 0 ||
        chan->chan_status != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "SIO %03x %08x cc=2\n", addr, chan->chan_status);
        return 2;
    }

    /* Check for any pending status for this device */
    if (dev_status[addr] != 0) {
        if (dev_status[addr] & SNS_DEVEND)
            dev_status[addr] |= SNS_BSY;
        M[0x44 >> 2] = (((uint32)dev_status[addr]) << 24);
        M[0x40 >> 2] = 0;
        key[0] |= 0x6;
        sim_debug(DEBUG_EXP, &cpu_dev,
            "SIO Set atten %03x %02x [%08x] %08x\n",
            addr, dev_status[addr], M[0x40 >> 2], M[0x44 >> 2]);
        dev_status[addr] = 0;
        return 1;
    }

    /* If device has start_io function run it */
    if (dibp->start_io != NULL) {
        status = dibp->start_io(uptr) << 8;
        sim_debug(DEBUG_CMD, &cpu_dev, "SIO %03x %x\n", addr, status);
        if (status & STATUS_BUSY) {
            sim_debug(DEBUG_CMD, &cpu_dev, "SIO %03x busy cc=2\n", addr);
            return 2;
        }
        if (status != 0) {
            M[0x44 >> 2] = ((uint32)status<<16) | (M[0x44 >> 2] & 0xffff);
            sim_debug(DEBUG_EXP, &cpu_dev, "Channel store csw  %03x %08x\n",
                   chan->daddr, M[0x44 >> 2]);
            return 1;
        }
    }

    /* All ok, get caw address */
    chan->chan_status = 0;
    chan->caw = M[0x48>>2];
    chan->ccw_key = (chan->caw & PMASK) >> 24;
    chan->caw &= AMASK;
    key[0] |= 0x4;
    dev_status[addr] = 0;
    chan->daddr = addr;
    chan->dev = dev;

    /* Try to load first command */
    if (load_ccw(chan, 0)) {
        M[0x44 >> 2] = ((uint32)chan->chan_status<<16) | (M[0x44 >> 2] & 0xffff);
        key[0] |= 0x6;
        sim_debug(DEBUG_CMD, &cpu_dev, "SIO %03x %02x %x cc=1\n", addr,
              chan->ccw_cmd, chan->ccw_flags);
        chan->chan_status = 0;
        chan->ccw_cmd = 0;
        dev_status[addr] = 0;
        chan->daddr = NO_DEV;
        chan->dev = NULL;
        return ((uptr->flags & UNIT_ATT) == 0) ? 3 : 1;
    }

    /* If channel returned busy save CSW and return cc=1 */
    if (chan->chan_status & STATUS_BUSY) {
        M[0x40 >> 2] = 0;
        M[0x44 >> 2] = ((uint32)chan->chan_status<<16);
        key[0] |= 0x6;
        chan->chan_status = 0;
        chan->ccw_cmd = 0;
        dev_status[addr] = 0;
        chan->daddr = NO_DEV;
        chan->dev = NULL;
        sim_debug(DEBUG_EXP, &cpu_dev, "Channel store csw  %03x %08x\n",
                   addr, M[0x44 >> 2]);
        return 1;
    }

    return 0;
}

/*
 * Handle TIO instruction.
 */
int testio(uint16 addr) {
    struct _dev     *dev = find_device(addr);
    struct _chanctl *chan = find_subchan(addr);
    DIB         *dibp = NULL;
    UNIT        *uptr;
    uint16       status;
    int          ch;

    if (dev == NULL || chan == NULL) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %03x cc=3\n", addr);
        return 3;
    }

    dibp = dev->dibp;
    uptr = dev->unit;

    /* Find channel this device is on, if no none return cc=3 */
    if (dibp == NULL || uptr == NULL) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %03x cc=3\n", addr);
        return 3;
    }

    /* If any error pending save csw and return cc=1 */
    if (chan->chan_status & ERROR_STATUS) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %03x %03x %02x %x cc=1\n", addr,
              chan->daddr, chan->ccw_cmd, chan->ccw_flags);
        store_csw(chan);
        return 1;
    }

    /* If channel active, return cc=2 */
    if (chan->ccw_cmd != 0 || (chan->ccw_flags & (FLAG_CD|FLAG_CC)) != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %03x %03x %02x %x cc=2\n", addr,
              chan->daddr, chan->ccw_cmd, chan->ccw_flags);
        return 2;
    }

    /* Device finished and channel status pending return it and cc=1 */
    if (chan->ccw_cmd == 0 && chan->chan_status != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %03x %03x %02x %x cc=1a\n", addr,
              chan->daddr, chan->ccw_cmd, chan->ccw_flags);
        store_csw(chan);
        chan->daddr = NO_DEV;
        chan->dev = NULL;
        return 1;
    }

    /* Device has returned a status, store the csw and return cc=1 */
    if (dev_status[addr] != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %03x %03x %02x %x cc=1b\n", addr,
              chan->daddr, chan->ccw_cmd, chan->ccw_flags);
        M[0x40 >> 2] = 0;
        M[0x44 >> 2] = ((uint32)dev_status[addr]) << 24;
        key[0] |= 0x6;
        dev_status[addr] = 0;
        return 1;
    }

    ch = (addr >> 8) & 0xf;
    /* If error pending for another device on subchannel, return cc=2 */
    if (chan_pend[ch]) {
        int dev;
        /* Check if might be false */
        for (dev = (addr & 0xf00); dev < (addr | 0xfff); dev++) {
            if (dev_status[dev] != 0) {
                /* Check if same subchannel */
                if (find_subchan(dev) == chan) {
                    irq_pend = 1;
                    sim_debug(DEBUG_CMD, &cpu_dev, "TIO %03x %03x %x %x %x cc=2a\n",
                          addr, chan->daddr, chan->ccw_cmd, chan->ccw_flags, dev);
                    return 2;
                }
            }
        }
    }

    /* Nothing pending, send a 0 command to device to get status */
    status = dibp->start_cmd(uptr, 0) << 8;

    /* If we get a error, save csw and return cc=1 */
    if (status & ERROR_STATUS) {
        M[0x44 >> 2] = ((uint32)status<<16) | (M[0x44 >> 2] & 0xffff);
        key[0] |= 0x6;
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %03x %03x %02x %x %x cc=1d\n", addr,
              chan->daddr, chan->ccw_cmd, chan->ccw_flags, status);
        return 1;
    }

    if (status & STATUS_BUSY) {             /* Device busy */
        sim_debug(DEBUG_CMD, &cpu_dev, "TIO %03x %03x %02x %x %x cc=2\n", addr,
              chan->daddr, chan->ccw_cmd, chan->ccw_flags, status);
        return 2;
    }

    /* Everything ok, return cc=0 */
    sim_debug(DEBUG_CMD, &cpu_dev, "TIO %03x %03x %02x %x cc=0\n", addr,
              chan->daddr, chan->ccw_cmd, chan->ccw_flags);
    return 0;
}

/*
 * Handle HIO instruction.
 */
int haltio(uint16 addr) {
    struct _dev     *dev = find_device(addr);
    struct _chanctl *chan = find_subchan(addr);
    DIB             *dibp = NULL;
    UNIT            *uptr;
    int             cc;

    if (dev == NULL || chan == NULL) {
        sim_debug(DEBUG_CMD, &cpu_dev, "HIO %03x cc=3\n", addr);
        return 3;
    }

    dibp = dev->dibp;
    uptr = dev->unit;

    /* Find channel this device is on, if no none return cc=3 */
    if (dibp == NULL || uptr == NULL) {
        sim_debug(DEBUG_CMD, &cpu_dev, "HIO %03x cc=3\n", addr);
        return 3;
    }

    sim_debug(DEBUG_CMD, &cpu_dev, "HIO %03x %03x %x %x\n", addr, chan->daddr,
              chan->ccw_cmd, chan->ccw_flags);

    /* Generic halt I/O, tell device to stop and */
    /* If any error pending save csw and return cc=1 */
    if ((chan->chan_status & ERROR_STATUS) != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "HIO %03x %03x %x cc=0\n", addr, chan->daddr,
              chan->chan_status);
        return 0;
    }

    /* If channel active, tell it to terminate */
    if (chan->ccw_cmd != 0) {
        chan->chan_byte = BUFF_CHNEND;
        chan->ccw_flags &= ~(FLAG_CD|FLAG_CC);
    }

    /* Not executing a command, issue halt if available */
    if (dibp->halt_io != NULL) {
        /* Let device do it's thing */
        cc = dibp->halt_io(uptr);
        sim_debug(DEBUG_CMD, &cpu_dev, "HIOd %03x %03x cc=%d\n", addr,
                   chan->daddr, cc);
        if (cc == 1) {
            M[0x44 >> 2] = (((uint32)chan->chan_status) << 16) |
                       (M[0x44 >> 2] & 0xffff);
            key[0] |= 0x6;
            sim_debug(DEBUG_EXP, &cpu_dev, "Channel store csw %03x %08x\n",
                    chan->daddr, M[0x44 >> 2]);
        }
        return cc;
    }

    /* Store CSW and return 1. */
    sim_debug(DEBUG_CMD, &cpu_dev, "HIOx %03x %03x cc=1\n", addr, chan->daddr);
    M[0x44 >> 2] = (((uint32)chan->chan_status) << 16) | (M[0x44 >> 2] & 0xffff);
    key[0] |= 0x6;
    sim_debug(DEBUG_EXP, &cpu_dev, "Channel store csw %03x %08x\n", chan->daddr,
              M[0x44 >> 2]);
    return 1;
}

/*
 * Handle TCH instruction.
 */
int testchan(uint16 channel) {
    UNIT            *uptr;
    struct _chanctl *chan;
    uint8            cc = 0;

    /* 360 Principles of Operation says, "Bit positions 21-23 of the
    sum formed by the addition of the content of register B1 and the
    content of the D1 field identify the channel to which the
    instruction applies. Bit positions 24-31 of the address are ignored.”
    /67 Functional Characteristics do not mention any changes in basic or
    extended control mode of the TCH instruction behaviour.
    However, historic /67 code for MTS suggests that bits 19-20 of the
    address indicate the channel controller which should be used to query
    the channel.

    Original testchan code did not recognize the channel controller (CC) part
    of the address and treats the query as referring to a channel # like so:
    CC = 0 channel# 0  1  2  3  4  5  6
    CC = 1    "     8  9 10 11 12 13 14
    CC = 2    "    16 17 18 19 20 21 22
    CC = 3    "    24 25 26 27 28 29 30
    which may interfere with subchannel mapping.

    For the nonce, TCH only indicates that channels connected to CC 0 & 1 are
    attached.  Channels 0, 4, 8 (0 on CC 1) & 12 (4 on CC 1) are multiplexer
    channels. */

    cc = (channel >> 11) & 0x03;
    channel = (channel >> 8) & 0x0f;
    if (cc > 1 || channel > chan_dev.numunits) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TCH CC %x %x cc=3\n", cc, channel);
        return 3;
    }

    uptr = &(chan_dev.units[channel]);

    /* Return 3 if no channel */
    if ((uptr->flags & UNIT_DIS) != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TCH CC %x cc=3\n", channel);
        return 3;
     }

    /* Multiplexer channels return channel is available */
    if ((uptr->flags & UNIT_M_TYPE) == UNIT_MUX) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TCH CC %x cc=0, mux\n", channel);
        return 0;
    }

    /* Block Multiplexer channels operating in select mode */
    if ((uptr->flags & UNIT_M_TYPE) == UNIT_BMUX) {
        extern uint32    cregs[16];
        if ((cpu_unit[0].flags & FEAT_370) != 0 &&
            (cregs[0] & 0x80000000) != 0) {
            sim_debug(DEBUG_CMD, &cpu_dev, "TCH CC %x cc=0, bmux\n", channel);
            return 0;
        }
    }

    chan = &(chan_ctl[0]);

    /* If channel is processing a command, return 2 */
    if (chan->ccw_cmd != 0 || (chan->ccw_flags & (FLAG_CD|FLAG_CC)) != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TCH CC %x cc=2, sel busy\n", channel);
        return 2;
    }

    /* If channel has pending status, return 1 */
    if (chan->chan_status != 0) {
        sim_debug(DEBUG_CMD, &cpu_dev, "TCH CC %x cc=1, error\n", channel);
        return 1;
    }

    /* Otherwise return 0. */
    sim_debug(DEBUG_CMD, &cpu_dev, "TCH CC %x cc=0, ok\n", channel);
    return 0;
}

/*
 * Bootstrap a device. Set command to READ IPL, length 24 bytes, suppress
 * length warning, and chain command.
 */
t_stat chan_boot(uint16 addr, DEVICE *dptyr) {
    struct _dev     *dev;
    struct _chanctl *chan;
    DIB             *dibp = NULL;
    UNIT            *uptr;
    int             status;
    int             i;

    for (i = 0; i < (MAX_CHAN * 256); i++)
        dev_status[i] = 0;
    chan_set_devs();
    dev = find_device(addr);
    chan = find_subchan(addr);
    if (dev == NULL || chan == NULL) {
        return SCPE_IOERR;
    }

    dibp = dev->dibp;
    uptr = dev->unit;

    /* Find channel this device is on, if no none return cc=3 */
    if (dibp == NULL || uptr == NULL) {
        return SCPE_IOERR;
    }

    /* If device has start_io function run it */
    if (dibp->start_io != NULL) {
        status = dibp->start_io(uptr) << 8;
        if (status != 0) {
            return SCPE_IOERR;
        }
    }
    chan->chan_status = 0;
    dev_status[addr] = 0;
    chan->dev = dev;
    chan->caw = 0x8;
    chan->daddr = addr;
    chan->ccw_count = 24;
    chan->ccw_flags = FLAG_CC|FLAG_SLI;
    chan->ccw_addr = 0;
    chan->ccw_key = 0;
    chan->chan_byte = BUFF_EMPTY;
    chan->ccw_cmd = 0x2;    /* IPL command */
    chan->chan_status = dibp->start_cmd(uptr, chan->ccw_cmd) << 8;
    if (chan->chan_status & (STATUS_ATTN|STATUS_CHECK|STATUS_EXPT)) {
        chan->ccw_flags = 0;
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
     unsigned int     i;
     int              j;
     int              pend;             /* I/O done on device */
     int              imask;
     UNIT            *uptr;
     int              nchan;
     struct _chanctl *chan;

     /* Quick exit if no pending IRQ's */
     if (irq_pend == 0)
         return NO_DEV;
     irq_pend = 0;                     /* Clear pending flag */
     pend = NO_DEV;
     /* Start with channel 0 and work through all channels */
     for (i = 0; i < chan_dev.numunits && pend == NO_DEV; i++) {
         imask = 0x8000 >> i;   /* Mask for this channel */
         uptr = &((chan_dev.units)[i]);
         /* If channel disabled, just skip */
         if ((uptr->flags & UNIT_DIS) != 0)
            continue;
         nchan = 1;
         if ((uptr->flags & UNIT_M_TYPE) == UNIT_BMUX) {
             extern uint32    cregs[16];
             if ((cpu_unit[0].flags & FEAT_370) != 0 &&
                 (cregs[0] & 0x80000000) != 0) {
                   nchan = 32;
             }
         } else if ((uptr->flags & UNIT_M_TYPE) == UNIT_MUX) {
             nchan = UNIT_G_SCHAN(uptr->flags);
         }
         /* Scan all subchannels on this channel */
         for (j = 0; j < nchan; j++) {
             chan = &(chan_ctl[j]);
             if (chan->daddr == NO_DEV)
                 continue;
             /* Check if PCI pending */
             if (irq_en && (chan->chan_status & STATUS_PCI) != 0) {
                 sim_debug(DEBUG_EXP, &cpu_dev, "Scan PCI(%x %x %x %x %x) end\n", i,
                             chan->daddr, chan->chan_status, imask, mask);
                 if ((imask & mask) != 0) {
                    pend = chan->daddr;
                    break;
                 }
             }

             /* If chaining and device end continue */
             if (chan->chain_flg && chan->chan_status & STATUS_DEND) {
                     sim_debug(DEBUG_EXP, &cpu_dev, "Scan(%x %x %x %x) CC\n", i,
                              chan->daddr, chan->chan_status, chan->ccw_flags);
                 /* Restart command that was flaged as an issue */
                 (void)load_ccw(chan, 1);
             }

             /* If channel end, check if we should continue */
             if (chan->chan_status & STATUS_CEND) {
                     sim_debug(DEBUG_EXP, &cpu_dev, "Scan(%x %x %x %x) Cend\n", i,
                              chan->daddr, chan->chan_status, chan->ccw_flags);
                 /* Grab another command if command chainging in effect */
                 if (chan->ccw_flags & FLAG_CC) {
                     if (chan->chan_status & STATUS_DEND)
                        (void)load_ccw(chan, 1);
                     else
                        irq_pend = 1;
                 } else if (irq_en || loading) {
                     /* Disconnect from device */
                     sim_debug(DEBUG_EXP, &cpu_dev, "Scan(%x %x %x %x %x) end\n", i,
                              chan->daddr, chan->chan_status, imask, mask);
                     if (//(chan->chan_status & STATUS_CEND) != 0 &&
                          ((imask & mask) != 0 || loading != 0)) {
                        pend = chan->daddr;
                        break;
                     }
                 }
             }
         }
     }
     /* Only return loading unit on loading */
     if (loading != 0 && loading != pend)
        return NO_DEV;
     /* See if we can post an IRQ */
     if (pend != NO_DEV) {
          irq_pend = 1;       /* Set to scan next time */
          if (loading && loading == pend) {
              chan->chan_status = 0;
              return pend;
          }
          if (loading == 0) {
              sim_debug(DEBUG_EXP, &cpu_dev, "Scan end (%x %x)\n", chan->daddr, pend);
              store_csw(chan);
              dev_status[pend] = 0;
              return pend;
          }
     } else if (irq_en) {
          /* If interrupts are wanted, check for pending device status */

          /* Scan channel for pending device status change */
          for (j = 0; j < MAX_CHAN; j++) {
              /* Nothing pending, or not enabled, just skip */
              if (chan_pend[j] == 0 || ((0x8000 >> j) & mask) == 0)
                  continue;
              nchan = j << 8;
              chan_pend[j] = 0;
              for (i = 0; i < 256; i++) {
                  if (dev_status[nchan|i] != 0) {
                      chan_pend[j] = 1;
                      irq_pend = 1;
                      M[0x44 >> 2] = (((uint32)dev_status[nchan|i]) << 24);
                      M[0x40>>2] = 0;
                      key[0] |= 0x6;
                      sim_debug(DEBUG_EXP, &cpu_dev,
                               "Set atten %03x %02x [%08x] %08x\n", nchan|i,
                               dev_status[nchan|i], M[0x40 >> 2], M[0x44 >> 2]);
                      dev_status[nchan|i] = 0;
                      return nchan|i;
                  }
              }
          }
     }
     return NO_DEV;
}


t_stat
chan_reset(DEVICE * dptr)
{
    unsigned int     i, j;

    for (i = 0; i < dptr->numunits; i++) {
         UNIT   *uptr = &dptr->units[i];
         unsigned int   n;

         if (uptr->up7 != NULL)
             free(uptr->up7);
         if (uptr->up8 != NULL)
             free(uptr->up8);
         uptr->up7 = NULL;
         uptr->up8 = NULL;
         if (uptr->flags & UNIT_DIS)
             continue;
         n = 1;
         if ((uptr->flags & UNIT_M_TYPE) == UNIT_MUX)
             n = UNIT_G_SCHAN(uptr->flags)+1;
         if ((uptr->flags & UNIT_M_TYPE) == UNIT_BMUX)
             n = 32;
         uptr->schans = n;
         uptr->up7 = calloc(n, sizeof(struct _chanctl));
         uptr->up8 = calloc(256, sizeof(struct _dev));
         for (j = 0; j < n; j++) {
              struct _chanctl *chan = &(chan_ctl[j]);
              chan->daddr = NO_DEV;
         }
    }
    return SCPE_OK;
}

/*
 * Scan all devices and create the device mapping.
 */
t_stat
chan_set_devs()
{
    unsigned          i, j;

    /* Readjust subchannels if there was a change */
    for (i = 0; i < chan_dev.numunits; i++) {
         UNIT   *uptr = &chan_unit[i];
         unsigned int   n;

         /* If channel disconnected free the buffers */
         if (uptr->flags & UNIT_DIS) {
             if (uptr->up7 != NULL)
                 free(uptr->up7);
             if (uptr->up8 != NULL)
                 free(uptr->up8);
             uptr->up7 = NULL;
             uptr->up8 = NULL;
             continue;
         }
         n = 1;
         if ((uptr->flags & UNIT_M_TYPE) == UNIT_MUX)
             n = UNIT_G_SCHAN(uptr->flags)+1;
         if ((uptr->flags & UNIT_M_TYPE) == UNIT_BMUX)
             n = 32;
         /* If no device array, create one */
         if (uptr->up8 == NULL)
             uptr->up8 = calloc(256, sizeof(struct _dev));
         if (uptr->up7 == NULL || (uint32)uptr->schans != n) {
             if (uptr->up7 != NULL)
                free(uptr->up7);
             uptr->up7 = calloc(n, sizeof(struct _chanctl));
             for (j = 0; j < n; j++) {
                 struct _chanctl *chan = &(chan_ctl[j]);
                 chan->daddr = NO_DEV;
             }
         }
         uptr->schans = n;
         for (j = 0; j < 256; j++) {
             struct _dev  *dev = &(dev_tab[j]);
             dev->dibp = NULL;
             dev->unit = NULL;
             dev->dev_addr = NO_DEV;
         }
    }

    /* Build channel array */
    for (i = 0; sim_devices[i] != NULL; i++) {
        DEVICE           *dptr = sim_devices[i];
        UNIT             *uptr = dptr->units;
        DIB              *dibp = (DIB *) dptr->ctxt;
        int               addr;
        struct _dev      *dev;

        /* If no DIB, not channel device */
        if (dibp == NULL)
            continue;
        /* Skip disabled devices */
        if (dptr->flags & DEV_DIS)
            continue;
        /* Check if address is in unit or dev entry */
        for (j = 0; j < dptr->numunits; j++) {
           addr = GET_UADDR(uptr->u3);
           dev = find_device(addr);
           if (dev != NULL && (uptr->flags & UNIT_DIS) == 0) {
                dev->unit = uptr;
                dev->dibp = dibp;
                dev->dev_addr = addr;
                if (dibp->dev_ini != NULL)
                    dibp->dev_ini(uptr, 1);
           }
           uptr++;

        }
    }
    return SCPE_OK;
}

/* Sets the number of subchannels for a channel */
t_stat
set_subchan(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    t_value             nschans;
    t_stat              r;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;

    nschans = get_uint (cptr, 10, 256, &r);

    if (r != SCPE_OK)
        return r;

    if (nschans < 32 || (nschans & 0xf) != 0)
        return SCPE_ARG;

    uptr->flags &= ~UNIT_SCHAN(0xff);
    uptr->flags |= UNIT_SCHAN(nschans);

    return r;
}

/* Display the address of the device */
t_stat
show_subchan(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    int                 n;

    if (uptr == NULL)
        return SCPE_IERR;
    if ((uptr->flags & UNIT_M_TYPE) == UNIT_SEL) {
        fprintf(st, "SEL");
    } else if ((uptr->flags & UNIT_M_TYPE) == UNIT_BMUX) {
        fprintf(st, "BMUX");
    } else {
        n = UNIT_G_SCHAN(uptr->flags);
        fprintf(st, "MUX SUB=%d", n);
    }
    return SCPE_OK;
}


/* Sets the device onto a given channel */
t_stat
set_dev_addr(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE             *dptr;
    struct _dev        *dev;
    DIB                *dibp;
    t_value             newdev;
    struct _dev        *ndev;
    t_stat              r;
    unsigned int        i;
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

    if ((newdev >> 8) > (t_value)dptr->numunits)
        return SCPE_ARG;

    if ((newdev >> 8) >= MAX_CHAN)
        return SCPE_ARG;

    devaddr = GET_UADDR(uptr->u3);
    dev = find_device(devaddr);

    if (dev == NULL)
       return SCPE_ARG;

    /* Clear out existing entry */
    if (dptr->flags & DEV_UADDR) {
        dev->dibp = NULL;
        dev->unit = NULL;
    } else {
        devaddr &= dibp->mask | 0xf00;
        for (i = 0; i < dibp->numunits; i++) {
            dev = find_device(devaddr + i);
            if (dev != NULL) {
                dev->dibp = NULL;
                dev->unit = NULL;
            } else {
                r = SCPE_ARG;
            }
        }
    }

    ndev = find_device(newdev);

    if (ndev == NULL) {
        r = SCPE_ARG;
    } else {
        /* Check if device already at newdev */
        if (dptr->flags & DEV_UADDR) {
            if (dev->unit != NULL)
                r = SCPE_ARG;
        } else {
            newdev &= dibp->mask | 0xf00;
            for (i = 0; i < dibp->numunits; i++) {
                 ndev = find_device(newdev + i);
                 if (dev == NULL || dev->unit != NULL) {
                    r = SCPE_ARG;
                 }
            }
        }
    }

    /* If not, point to new dev, else restore old */
    if (r == SCPE_OK)
       devaddr = newdev;

    dev = find_device(devaddr);
    /* Update device entry */
    if (dptr->flags & DEV_UADDR) {
        dev->dibp = dibp;
        dev->unit = uptr;
        dev->dev_addr = devaddr;
        uptr->u3 &= ~UNIT_ADDR(0xfff);
        uptr->u3 |= UNIT_ADDR(devaddr);
        sim_printf("Set dev %s %x\r\n", dptr->name, GET_UADDR(uptr->u3));
    } else {
        sim_printf("Set dev %s0 %x\r\n",  dptr->name, devaddr);
        for (i = 0; i < dibp->numunits; i++)  {
             dev = find_device(devaddr + i);
             uptr = &((dibp->units)[i]);
             if (dev != NULL) {
                dev->dibp = dibp;
                dev->unit = uptr;
                dev->dev_addr = devaddr + i;
             }
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
    int                 addr;

    if (uptr == NULL)
        return SCPE_IERR;
    addr = GET_UADDR(uptr->u3);
    fprintf(st, "DEV=%03x", addr);
    return SCPE_OK;
}

t_stat
chan_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "IBM360/370 Chan\n\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *
chan_description (DEVICE *dptr)
{
       return "IBM 360/370 Channel";
}


