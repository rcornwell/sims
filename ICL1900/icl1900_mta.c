/* icl1900_mta.c: ICL1900 1974 mag tape drive simulator.

   Copyright (c) 2018, Richard Cornwell

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

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell

   Magnetic tapes are represented as a series of variable records
   of the form:

        32b byte count
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32b byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0.

*/

#include "icl1900_defs.h"
#include "sim_tape.h"

#if (NUM_DEVS_MTA > 0)
#define BUFFSIZE       (64 * 1024)
#define MTUF_9TR       (1 << MTUF_V_UF)
#define UNIT_MTA             UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE | MTUF_9TR
#define DEV_BUF_NUM(x)  (((x) & 07) << DEV_V_UF)
#define GET_DEV_BUF(x)  (((x) >> DEV_V_UF) & 07)

#define CMD          u3             /* Command */
#define STATUS       u4
#define ADDR         u5             /* Transfer address read from M[64+n] */
#define POS          u6             /* Position within buffer */


/*  Command is packed follows:
 *
 *   Lower 3 bits is command.
 *   Next bit is binary/BCD.
 *   Next bit is disconnect flag.
 *   Top 16 bits are count.
 */

#define MT_CMD      007
#define BCD         010
#define DISC        020

#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark =  0xFFFFFFFF

#define MT_READ      0
#define MT_WRITE     1
#define MT_REV_READ  2
#define MT_WRITEERG  3
#define MT_SKIPF     4
#define MT_WTM       5
#define MT_SKIPB     6
#define MT_REW       7

#define MT_TRCNT     M15
#define MT_STOP      B3
#define MT_START     B4
#define MT_BCD       B5

/* Error status word */
#define TERMINATE    000000001        /* Transfer complete */
#define OPAT         000000002        /* Operator attention */
#define PARITY       000000004        /* Parity error */
#define HESFAIL      000000010        /* Failed to transfer word in time */
#define ACCEPT       000000020        /* Ready for command */
#define BUSY         000000040        /* Device busy */
#define CBUSY        000000100        /* Controller Busy */
#define WPROT        000001000        /* Write protect */
#define BOT          000002000        /* Beginning of Tape */
#define EOT          000004000        /* End of Tape. */
#define OFFLINE      000040000        /* Device offline */
#define LONGBLK      000100000        /* Long block */
#define FILLWRD      000200000        /* Block short filled with stop */
#define MARK         000400000        /* Tape mark sensed */
#define DENS         014000000
#define CHAR         060000000        /* Count of character read */

int  mta_busy;   /* Indicates that controller is talking to a drive */
uint8 mta_buffer[BUFFSIZE];
void mta_nsi_cmd (uint32 dev, uint32 cmd);
void mta_nsi_status (uint32 dev, uint32 *resp);
t_stat mta_svc (UNIT *uptr);
t_stat mta_reset (DEVICE *dptr);
t_stat mta_boot (int32 unit_num, DEVICE * dptr);
t_stat mta_attach(UNIT *, CONST char *);
t_stat mta_detach(UNIT *);
t_stat mta_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
CONST char *mta_description (DEVICE *dptr);

DIB mta_dib = {  WORD_DEV|BLK_DEV, NULL, &mta_nsi_cmd, &mta_nsi_status };


MTAB                mta_mod[] = {
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL},
    {MTUF_9TR, 0, "7 track", "7T", NULL},
    {MTUF_9TR, MTUF_9TR, "9 track", "9T", NULL},
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
         &sim_tape_set_fmt, &sim_tape_show_fmt, NULL},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "DEV", "DEV",
         &set_chan, &get_chan, NULL, "Device Number"},
    {0}
};

UNIT                mta_unit[] = {
    {UDATA(&mta_svc, UNIT_MTA, 0) },       /* 0 */
    {UDATA(&mta_svc, UNIT_MTA, 0) },       /* 1 */
    {UDATA(&mta_svc, UNIT_MTA, 0) },       /* 2 */
    {UDATA(&mta_svc, UNIT_MTA, 0) },       /* 3 */
    {UDATA(&mta_svc, UNIT_MTA, 0) },       /* 4 */
    {UDATA(&mta_svc, UNIT_MTA, 0) },       /* 5 */
    {UDATA(&mta_svc, UNIT_MTA, 0) },       /* 6 */
    {UDATA(&mta_svc, UNIT_MTA, 0) },       /* 7 */
};

DEVICE mta_dev = {
    "MTA", mta_unit, NULL, mta_mod,
    NUM_DEVS_MTA, 8, 22, 1, 8, 22,
    NULL, NULL, &mta_reset, &mta_boot, &mta_attach, &mta_detach,
    &mta_dib, DEV_DIS | DEV_DISABLE | DEV_DEBUG | UNIT_ADDR(24), 0, dev_debug,
    NULL, NULL, &mta_help, NULL, NULL, &mta_description
    };

void mta_nsi_cmd(uint32 dev, uint32 cmd) {
   int32    d;
   UNIT    *uptr;

   d = dev - GET_UADDR(mta_dev.flags);
   sim_debug(DEBUG_CMD, &mta_dev, "CMD: %d: %d c=%08o\n", dev, d, cmd);
   if (d < 0 || d > (int32)mta_dev.numunits)
       return;
   uptr = &mta_dev.units[d];

   if (cmd & MT_STOP) {
       uptr->CMD |= DISC;
       return;
   }

   if (cmd & MT_START) {
       if (mta_busy)
           return;
       if ((uptr->STATUS & BUSY) || (uptr->flags & UNIT_ATT) == 0) {
           uptr->STATUS |= OPAT;
           chan_set_done(GET_UADDR(uptr->flags));
           return;
       }
       uptr->CMD = (cmd & MT_TRCNT) << 16;
       uptr->CMD |= (cmd >> 15) & 07;
       if (cmd & MT_BCD)
          uptr->CMD |= BCD;
       uptr->STATUS = BUSY;
       uptr->ADDR = M[64 + dev] & M15;  /* Get transfer address */
       uptr->POS = 0;
       CLR_BUF(uptr);
       mta_busy = 1;
       sim_activate(uptr, 100);
       chan_clr_done(GET_UADDR(uptr->flags));
   }
}

void mta_nsi_status(uint32 dev, uint32 *resp) {
   int32    d;
   UNIT    *uptr;

   *resp = 0;
   d = dev - GET_UADDR(mta_dev.flags);
   if (d < 0 || d > (int32)mta_dev.numunits)
       return;
   uptr = &mta_dev.units[d];

   *resp = uptr->STATUS;
   if (mta_busy)
       *resp |= CBUSY;
   /* Set hard status bits. */
   if ((uptr->flags & UNIT_ATT) == 0)
       *resp |= OFFLINE;
   if (sim_tape_wrp(uptr))
       *resp |= WPROT;
   if (sim_tape_bot(uptr))
       *resp |= BOT;
   if (sim_tape_eot(uptr))
       *resp |= EOT;
   sim_debug(DEBUG_CMD, &mta_dev, "STAT: %d: %d c=%08o\n", dev, d, *resp);
   chan_clr_done(dev);
}

t_stat mta_svc (UNIT *uptr)
{
    DEVICE      *dptr = &mta_dev;
    int          unit = (uptr - dptr->units);
    int          dev = unit + GET_UADDR(dptr->flags);
    t_mtrlnt     reclen;
    t_stat       r;
    uint8        ch;
    uint32       word;
    int          i;
    int          stop;
    uint8        mode;

    /* Handle a disconnect request */
    if (uptr->CMD & DISC) {
       uptr->STATUS &= ~BUSY;
       mta_busy = 0;
       chan_set_done(dev);
       return SCPE_OK;
    }
    /* If not busy, false schedule, just exit */
    if ((uptr->STATUS & BUSY) == 0)
        return SCPE_OK;

    switch (uptr->CMD & MT_CMD) {
    case MT_READ:
         /* If empty buffer, fill */
         if (BUF_EMPTY(uptr)) {
             sim_debug(DEBUG_DETAIL, dptr, "Read unit=%d ", unit);
             if ((r = sim_tape_rdrecf(uptr, &mta_buffer[0], &reclen,
                              BUFFSIZE)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, " error %d\n", r);
                 if (r == MTSE_TMK)
                     uptr->STATUS |= MARK;
                 else if (r == MTSE_WRP)
                     uptr->STATUS |= WPROT;
                 else if (r == MTSE_EOM)
                     uptr->STATUS |= EOT;
                 else if (r == MTSE_UNATT)
                     uptr->STATUS |= OFFLINE|OPAT;
                 else
                     uptr->STATUS |= OPAT;
                 uptr->STATUS |= TERMINATE;
                 uptr->STATUS &= ~BUSY;
                 mta_busy = 0;
                 chan_set_done(dev);
                 return SCPE_OK;
             }
             uptr->hwmark = reclen;
             sim_debug(DEBUG_DETAIL, dptr, "Block %d chars\n", reclen);
         }
         stop = 0;
         if (uptr->flags & MTUF_9TR) {
             /* Grab three chars off buffer */
             word = 0;
             uptr->STATUS &= ~CMASK;
             for(i = 16; i >= 0; i-=8) {
                 if ((uint32)uptr->POS >= uptr->hwmark) {
                    /* Add in fill characters */
                    stop = 1;
                    if (i == 8) {
                       uptr->STATUS += B2|B1;
                       word |= 074;
                    } else if (i == 16) {
                       uptr->STATUS += B1;
                       word |= 07474;
                    }
                    break;
                 }
                 word |= (uint32)mta_buffer[uptr->POS++] << i;
             }
             uptr->STATUS |= BM1;
         } else {
             /* Grab four chars and check parity */
             word = 0;
             mode = (uptr->CMD & BCD) ? 0 : 0100;
             uptr->STATUS &= ~CMASK;
             for(i = 18; i >= 0; i-=6) {
                 if (stop || (uint32)uptr->POS >= uptr->hwmark) {
                    stop = 1;
                    ch = 074;
                 } else {
                    ch = mta_buffer[uptr->POS++];
                    if ((parity_table[ch & 077] ^ (ch & 0100) ^ mode) == 0) {
                        sim_debug(DEBUG_DETAIL, dptr, "Parity error unit=%d %d %03o\n",
                              unit, uptr->POS-1, ch);
                        uptr->STATUS |= PARITY;
                        break;
                    }
                    uptr->STATUS += B1;
                 }
                 word |= (ch & 077) << i;
             }
         }
         sim_debug(DEBUG_DATA, dptr, "unit=%d %08o read %08o\n", unit, uptr->ADDR, word);
         if (stop || (uptr->STATUS & (CMASK|BM1)) != 0) {
             if (uptr->ADDR < 8)
                 XR[uptr->ADDR] = word;
             M[uptr->ADDR++] = word;
             uptr->ADDR &= M15;
             uptr->CMD -= 1 << 16;
             if (stop || (uptr->CMD & (M15 << 16)) == 0 || (uint32)uptr->POS >= uptr->hwmark) {
                 /* Done with transfer */
                 sim_debug(DEBUG_DETAIL, dptr, "unit=%d %08o left %08o\n", unit, uptr->ADDR,
                                    uptr->CMD >> 16);
                 if ((uptr->CMD & (M15 << 16)) == 0 && (uint32)uptr->POS < uptr->hwmark)
                     uptr->STATUS |= LONGBLK;
                 if ((uptr->CMD & BCD) != 0 && (uptr->CMD & (M15 << 16)) != 0
                                  && (uint32)uptr->POS >= uptr->hwmark) {
                     uptr->STATUS |= FILLWRD;
                     M[uptr->ADDR++] = 074747474;
                     uptr->ADDR &= M15;
                 }
                 uptr->STATUS |= TERMINATE;
                 uptr->STATUS &= ~BUSY;
                 M[64 + dev] = uptr->ADDR;  /* Get transfer address */
                 mta_busy = 0;
                 uptr->STATUS &= FMASK;
                 chan_set_done(dev);
                 return SCPE_OK;
             }
             uptr->STATUS &= FMASK;
         }
         sim_activate(uptr, 100);
         break;

    case MT_WRITEERG: /* Write and Erase */
    case MT_WRITE:
         /* Check if write protected */
         if (sim_tape_wrp(uptr)) {
             uptr->STATUS |= WPROT;
             uptr->STATUS &= ~BUSY;
             mta_busy = 0;
             uptr->STATUS &= FMASK;
             chan_set_done(dev);
             return SCPE_OK;
         }

         word = M[uptr->ADDR++];
         uptr->ADDR &= M15;
         uptr->CMD -= 1 << 16;
         sim_debug(DEBUG_DATA, dptr, "unit=%d %08o write %08o\n", unit, uptr->ADDR, word);

         stop = 0;
         if (uptr->flags & MTUF_9TR) {
             /* Put three chars in buffer */
             uptr->STATUS &= ~CMASK;
             for(i = 16; i >= 0; i-=8) {
                 mta_buffer[uptr->POS++] = (uint8)((word >> i) & 0xff);
                 uptr->STATUS += B1;
             }
             /* Check if end character detected */
             if ((uptr->CMD & BCD) != 0) {
                 for (i = 0; i <= 18; i+= 6) {
                     if (((word >> i) & 077)  == 074) {
                         uptr->POS--;
                         uptr->STATUS -= B1;
                         stop = 1;
                     }
                 }
             }
         } else {
             /* Put four chars and generate parity */
             mode = (uptr->CMD & BCD) ? 0 : 0100;
             uptr->STATUS &= ~CMASK;
             for(i = 18; i >= 0; i-=6) {
                 ch = (uint8)((word >> i) & 077);
                 if ((uptr->CMD & BCD) != 0 && ch == 074) {
                     stop = 1;
                     break;
                 }
                 ch |= parity_table[ch] ^ mode;
                 mta_buffer[uptr->POS++] = ch;
                 uptr->STATUS += B1;
             }
         }
         uptr->STATUS &= FMASK;
         uptr->hwmark = uptr->POS;
         if (stop || (uptr->CMD & (M15 << 16)) == 0) {
             /* Done with transfer */
             reclen = uptr->hwmark;
             sim_debug(DEBUG_DETAIL, dptr, "Write unit=%d Block %d chars\n",
                      unit, reclen);
             r = sim_tape_wrrecf(uptr, &mta_buffer[0], reclen);
             if (r != MTSE_OK)
                uptr->STATUS |= OPAT;
             uptr->STATUS |= TERMINATE;
             uptr->STATUS &= ~BUSY;
             mta_busy = 0;
             uptr->STATUS &= FMASK;
             M[64 + dev] = uptr->ADDR;  /* Set transfer address */
             chan_set_done(dev);
             return SCPE_OK;
         }
         sim_activate(uptr, 100);
         break;

    case MT_REV_READ:
         /* If empty buffer, fill */
         if (BUF_EMPTY(uptr)) {
             if (sim_tape_bot(uptr)) {
                 uptr->STATUS |= OPAT|TERMINATE;
                 uptr->STATUS &= ~BUSY;
                 mta_busy = 0;
                 chan_set_done(dev);
                 return SCPE_OK;
             }
             sim_debug(DEBUG_DETAIL, dptr, "Read rev unit=%d ", unit);
             if ((r = sim_tape_rdrecr(uptr, &mta_buffer[0], &reclen,
                              BUFFSIZE)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, " error %d\n", r);
                 if (r == MTSE_TMK)
                     uptr->STATUS |= MARK;
                 else if (r == MTSE_WRP)
                     uptr->STATUS |= WPROT;
                 else if (r == MTSE_EOM)
                     uptr->STATUS |= EOT;
                 else if (r == MTSE_UNATT)
                     uptr->STATUS |= OFFLINE|OPAT;
                 else
                     uptr->STATUS |= OPAT;
                 uptr->STATUS |= TERMINATE;
                 uptr->STATUS &= ~BUSY;
                 mta_busy = 0;
                 chan_set_done(dev);
                 return SCPE_OK;
             }
             uptr->POS = reclen;
             uptr->ADDR += (uptr->CMD >> 16) + 1;
             uptr->hwmark = reclen;
             sim_debug(DEBUG_DETAIL, dptr, "Block %d chars\n", reclen);
         }

         stop = 0;
         if (uptr->flags & MTUF_9TR) {
             /* Grab three chars off buffer */
             word = 0;
             uptr->STATUS &= ~CMASK;
             for(i = 0; i <= 16; i+=8) {
                 word |= (uint32)mta_buffer[--uptr->POS] << i;
                 if (uptr->POS == 0) {
                    stop = 1;
                    break;
                 }
             }
             uptr->STATUS |= BM1;
         } else {
             /* Grab four chars and check parity */
             word = 0;
             mode = (uptr->CMD & BCD) ? 0 : 0100;
             uptr->STATUS &= ~CMASK;
             for(i = 0; i <= 16; i+=6) {
                 if (uptr->POS == 0) {
                     ch = 074;
                     stop = 1;
                 } else {
                     ch = mta_buffer[--uptr->POS];
                     if ((parity_table[ch & 077] ^ (ch & 0100) ^ mode) == 0) {
                         sim_debug(DEBUG_DETAIL, dptr, "Parity error unit=%d %d %03o\n",
                               unit, uptr->POS, ch);
                         uptr->STATUS |= PARITY;
                         break;
                     }
                     uptr->STATUS += B1;
                 }
                 word |= (ch & 077) << i;
             }
         }
         sim_debug(DEBUG_DATA, dptr, "unit=%d %08o read %08o\n", unit, uptr->ADDR, word);
         if (stop || (uptr->STATUS & (CMASK|BM1)) != 0) {
             uptr->ADDR = (uptr->ADDR - 1) & M15;
             if (uptr->ADDR < 8)
                 XR[uptr->ADDR] = word;
             M[uptr->ADDR] = word;
             uptr->CMD -= 1 << 16;
             if (stop || (uptr->CMD & (M15 << 16)) == 0 || uptr->POS == 0) {
                 sim_debug(DEBUG_DETAIL, dptr, "unit=%d %08o left %08o\n", unit, uptr->ADDR,
                                             uptr->CMD >> 16);
                 /* Done with transfer */
                 if ((uptr->CMD & (M15 << 16)) == 0 && uptr->POS != 0)
                     uptr->STATUS |= LONGBLK;
                 if ((uptr->CMD & BCD) != 0 && (uptr->CMD & (M15 << 16)) != 0
                                  && uptr->POS != 0) {
                     uptr->STATUS |= FILLWRD;
                     uptr->ADDR = (uptr->ADDR - 1) & M15;
                     M[uptr->ADDR] = 074747474;
                     uptr->ADDR &= M15;
                 }
                 uptr->STATUS |= TERMINATE;
                 uptr->STATUS &= ~BUSY;
                 M[64 + dev] = uptr->ADDR;  /* Set transfer address */
                 mta_busy = 0;
                 uptr->STATUS &= FMASK;
                 chan_set_done(dev);
                 return SCPE_OK;
             }
             uptr->STATUS &= FMASK;
         }
         sim_activate(uptr, 100);
         break;

    case MT_SKIPF:
         switch(uptr->POS) {
         case 0:
              uptr->POS ++;
              sim_activate(uptr, 500);
              break;
         case 1:
              sim_debug(DEBUG_DETAIL, dptr, "Skip rec unit=%d ", unit);
              r = sim_tape_sprecf(uptr, &reclen);
              if (r == MTSE_TMK) {
                  uptr->POS++;
                  sim_debug(DEBUG_DETAIL, dptr, "MARK\n");
                  sim_activate(uptr, 50);
              } else if (r == MTSE_EOM) {
                  uptr->POS++;
                  uptr->STATUS |= EOT;
                  sim_activate(uptr, 50);
              } else {
                  sim_debug(DEBUG_DETAIL, dptr, "%d\n", reclen);
                  sim_activate(uptr, 10 + (10 * reclen));
              }
              break;
         case 2:
              uptr->STATUS &= ~BUSY;
              uptr->STATUS |= TERMINATE;
//              uptr->STATUS |= TERMINATE|MARK;
              mta_busy = 0;
              chan_set_done(dev);
         }
         break;

    case MT_WTM:
         if (uptr->POS == 0) {
             if (sim_tape_wrp(uptr)) {
                 uptr->STATUS |= WPROT|OPAT|TERMINATE;
                 uptr->STATUS &= ~BUSY;
                 mta_busy = 0;
                 uptr->STATUS &= FMASK;
                 chan_set_done(dev);
                 break;
             }
             uptr->POS ++;
             sim_activate(uptr, 500);
         } else {
             sim_debug(DEBUG_DETAIL, dptr, "Write Mark unit=%d\n", unit);
             r = sim_tape_wrtmk(uptr);
             if (r != MTSE_OK)
                 uptr->STATUS |= OPAT;
             uptr->STATUS &= ~BUSY;
             uptr->STATUS |= TERMINATE;
             mta_busy = 0;
             chan_set_done(dev);
         }
         break;

    case MT_SKIPB:
         switch (uptr->POS ) {
         case 0:
              if (sim_tape_bot(uptr)) {
                  uptr->STATUS |= OPAT|TERMINATE;
                  uptr->STATUS &= ~BUSY;
                  mta_busy = 0;
                  chan_set_done(dev);
                  break;
              }
              uptr->POS ++;
              sim_activate(uptr, 500);
              break;
         case 1:
              sim_debug(DEBUG_DETAIL, dptr, "Backspace rec unit=%d ", unit);
              r = sim_tape_sprecr(uptr, &reclen);
              /* We don't set EOF on BSR */
              if (r == MTSE_TMK || r == MTSE_BOT) {
                  uptr->POS++;
                  sim_activate(uptr, 50);
              } else {
                  sim_debug(DEBUG_DETAIL, dptr, "%d \n", reclen);
                  sim_activate(uptr, 10 + (10 * reclen));
              }
              break;
         case 2:
              uptr->STATUS &= ~BUSY;
              uptr->STATUS |= TERMINATE|MARK;
              mta_busy = 0;
              chan_set_done(dev);
         }
         break;

    case MT_REW:
         if (uptr->POS == 0) {
             uptr->POS ++;
             sim_activate(uptr, 30000);
             mta_busy = 0;
         } else {
             sim_debug(DEBUG_DETAIL, dptr, "Rewind unit=%d dev=%d\n", unit, dev);
             r = sim_tape_rewind(uptr);
             uptr->STATUS &= ~BUSY;
             uptr->STATUS |= TERMINATE;
             chan_set_done(dev);
         }
         break;

    }
    return SCPE_OK;
}


/* Reset */

t_stat mta_reset (DEVICE *dptr)
{
    UNIT  *uptr = dptr->units;
    uint32 unit;

    for (unit = 0; unit < dptr->numunits; unit++, uptr++) {
       uptr->CMD = 0;
       uptr->STATUS = 0;
       if ((uptr->flags & UNIT_ATT) == 0)
           uptr->STATUS |= OFFLINE;
       mta_busy = 0;
       chan_clr_done(GET_UADDR(dptr->flags)+unit);
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
mta_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT    *uptr = &dptr->units[unit_num];

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    uptr->ADDR = 0;
    uptr->CMD = MT_READ;
    uptr->STATUS = BUSY;
    uptr->POS = 0;
    CLR_BUF(uptr);
    loading = 1;
    mta_busy = 1;
    sim_activate (uptr, uptr->wait);
    return SCPE_OK;
}

t_stat
mta_attach(UNIT * uptr, CONST char *file)
{
    t_stat              r;
    DEVICE             *dptr = &mta_dev;
    int                 unit = (uptr - dptr->units);

    if ((r = sim_tape_attach_ex(uptr, file, 0, 0)) != SCPE_OK)
       return r;
    uptr->STATUS = ACCEPT;
    if (uptr->flags & UNIT_RO)
        uptr->flags |= MTUF_WLK;
    chan_set_done(unit + GET_UADDR(dptr->flags));
    return r;
}

t_stat
mta_detach(UNIT * uptr)
{
    uptr->STATUS |= OFFLINE;
    return sim_tape_detach(uptr);
}


t_stat mta_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cmta)
{
fprintf (st, "The Paper Tape Reader can be set to one of twp modes: 7P, or 7B.\n\n");
fprintf (st, "  mode \n");
fprintf (st, "  7P    Process even parity input tapes. \n");
fprintf (st, "  7B    Ignore parity of input data.\n");
fprintf (st, "The default mode is 7B.\n");
return SCPE_OK;
}

CONST char *mta_description (DEVICE *dptr)
{
    return "PTR";

}
#endif
