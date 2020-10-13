/* icl1900_mt.c: ICL1900 2504 mag tape drive simulator.

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

#if (NUM_DEVS_MT > 0)
#define BUFFSIZE       (64 * 1024)
#define UNIT_MT              UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE

#define CMD          u3             /* Command */
#define STATUS       u4
#define POS          u6             /* Position within buffer */


/*  Command is packed follows:
 *
 *   Lower 3 bits is command.
 *   Next bit is binary/BCD.
 *   Next bit is disconnect flag.
 *   Top 16 bits are count.
 */

#define MT_CMD      077

#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark =  0xFFFFFFFF

#define MT_NOP       000
#define MT_FSF       001              /* No Qualifier */
#define MT_BSR       002              /* Qualifier */
#define MT_BSF       003              /* Qualifier */
#define MT_REV_READ  011              /* Qualifier */
#define MT_WRITEERG  012              /* Qualifier */
#define MT_WTM       013              /* Qualifier */
#define MT_TEST      014              /* Qualifier */
#define MT_REW       016              /* No Qualifier */
#define MT_READ      031              /* Qualifier */
#define MT_WRITE     032              /* Qualifier */
#define MT_RUN       036              /* No Qualifier */
#define MT_BOOT      037              /* No Qualifier */

#define MT_QUAL      0100             /* Qualifier expected */
#define MT_BUSY      0200             /* Device running command */

#define ST1_OK       001              /* Unit available */
#define ST1_WARN     002              /* Warning, EOT, BOT, TM */
#define ST1_ERR      004              /* Parity error, blank, no unit */
#define ST1_CORERR   010              /* Corrected error */
#define ST1_LONG     020              /* Long Block */
#define ST1_P2       040              /* P2 Status */

#define ST2_ROWS     00300            /* Number of rows read */
#define ST2_BLNK     00400            /* Blank Tape */
#define ST2_TM       00706            /* Tape Mark */

#define STQ_TERM     001              /* Operation terminated */
#define STQ_WRP      002              /* Write ring present */
#define STQ_TPT_RDY  004              /* Tape can accept orders */
#define STQ_CTL_RDY  030              /* Controller ready to accept new order */
#define STQ_P1       040              /* P1 Status on */


int  mt_busy;    /* Indicates that controller is talking to a drive */
int  mt_drive;   /* Indicates last selected drive */
uint8 mt_buffer[BUFFSIZE];
void mt_cmd (uint32 dev, uint32 cmd, uint32 *resp);
t_stat mt_svc (UNIT *uptr);
t_stat mt_reset (DEVICE *dptr);
t_stat mt_boot (int32 unit_num, DEVICE * dptr);
t_stat mt_attach(UNIT *, CONST char *);
t_stat mt_detach(UNIT *);
t_stat mt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
CONST char *mt_description (DEVICE *dptr);

DIB mt_dib = {  WORD_DEV|MULT_DEV, &mt_cmd, NULL, NULL };


MTAB                mt_mod[] = {
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL},
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
         &sim_tape_set_fmt, &sim_tape_show_fmt, NULL},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "DEV", "DEV",
         &set_chan, &get_chan, NULL, "Device Number"},
    {0}
};

UNIT                mt_unit[] = {
    {UDATA(&mt_svc, UNIT_MT, 0) },       /* 0 */
    {UDATA(&mt_svc, UNIT_MT, 0) },       /* 1 */
    {UDATA(&mt_svc, UNIT_MT, 0) },       /* 2 */
    {UDATA(&mt_svc, UNIT_MT, 0) },       /* 3 */
    {UDATA(&mt_svc, UNIT_MT, 0) },       /* 4 */
    {UDATA(&mt_svc, UNIT_MT, 0) },       /* 5 */
    {UDATA(&mt_svc, UNIT_MT, 0) },       /* 6 */
    {UDATA(&mt_svc, UNIT_MT, 0) },       /* 7 */
};

DEVICE mt_dev = {
    "MT", mt_unit, NULL, mt_mod,
    NUM_DEVS_MT, 8, 22, 1, 8, 22,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mt_dib, DEV_DISABLE | DEV_DEBUG | UNIT_ADDR(24), 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL, &mt_description
    };

void mt_cmd(uint32 dev, uint32 cmd, uint32 *resp) {
    UNIT  *uptr = &mt_unit[mt_drive];
    *resp = 0;
    if (cmd & 0400) {
        sim_debug(DEBUG_CMD, &mt_dev, "Cmd: set unit=%d %04o\n", mt_drive, cmd);
        mt_drive = cmd & 07;
        *resp = 5;
        return;
    }
    if (uptr->CMD & MT_QUAL) {
        sim_debug(DEBUG_CMD, &mt_dev, "Cmd: qual unit=%d %04o\n", mt_drive, cmd);
        cmd = uptr->CMD & ~MT_QUAL;
    } else {
        cmd &= 077;
        switch(cmd & 070) {
        case 000: if (cmd > 0)
                       cmd |= MT_QUAL;
                  break;
        case 010: if (cmd < 016)
                      cmd |= MT_QUAL;
                  break;
        case 020: if (cmd == SEND_Q) {
                     *resp = uptr->STATUS & 01;
                     if (mt_busy == 0)
                         *resp |= STQ_CTL_RDY;
                     if ((uptr->flags & UNIT_ATT) != 0) {
                         if ((uptr->CMD & MT_BUSY) == 0)
                             *resp |= STQ_TPT_RDY;
                         if (!sim_tape_wrp(uptr))
                             *resp |= STQ_WRP;
//                         if ((uptr->STATUS & 07776) == 0)
 //                            *resp |= STQ_P1;
                     } else {
                         *resp |= STQ_P1;
                     }
                     chan_clr_done(dev);
                  } else if (cmd == SEND_P) {
                     if ((uptr->flags & UNIT_ATT) != 0) {
                         *resp = uptr->STATUS & 036;
                         if ((uptr->CMD & MT_BUSY) == 0)
                             *resp |= ST1_OK;
                         if (uptr->STATUS & 017700)
                             *resp |= ST1_P2;
                     }
                     uptr->STATUS &= 017700;
                  } else if (cmd == SEND_P2) {
                     if ((uptr->flags & UNIT_ATT) != 0)
                         *resp = (uptr->STATUS >> 6) & 077;
                     uptr->STATUS = 0;
                  }
                  sim_debug(DEBUG_STATUS, &mt_dev, "Status: unit:=%d %02o %02o\n", mt_drive, cmd, *resp);
                  return;
        case 030: if (cmd < 036)
                       cmd |= MT_QUAL;
                  break;
        default:
                  sim_debug(DEBUG_DETAIL, &mt_dev, "extra: unit:=%d %02o %02o\n", mt_drive, cmd, *resp);
                  return;
        }
    }
    sim_debug(DEBUG_CMD, &mt_dev, "Cmd: unit=%d %02o\n", mt_drive, cmd);
    if ((uptr->flags & UNIT_ATT) == 0) {
       *resp = 0;
       return;
    }
    if (mt_busy || (uptr->CMD & MT_BUSY)) {
       *resp = 3;
       return;
    }
    if (cmd == 0) {
       *resp = 5;
       return;
    }
    uptr->CMD = cmd;
    if ((uptr->CMD & MT_QUAL) == 0) {
        sim_debug(DEBUG_CMD, &mt_dev, "Cmd: unit=%d start %02o\n", mt_drive, uptr->CMD);
       mt_busy = 1;
       CLR_BUF(uptr);
       uptr->POS = 0;
       uptr->CMD |= MT_BUSY;
       uptr->STATUS = 0;
       chan_clr_done(dev);
       sim_activate(uptr, 100);
    }
    *resp = 5;
    return;
}

t_stat mt_svc (UNIT *uptr)
{
    DEVICE      *dptr = &mt_dev;
    int          unit = (uptr - dptr->units);
    int          dev = GET_UADDR(dptr->flags);
    t_mtrlnt     reclen;
    t_stat       r;
    uint32       word;
    int          i;
    int          stop;
    int          eor;

    /* If not busy, false schedule, just exit */
    if ((uptr->CMD & MT_BUSY) == 0)
        return SCPE_OK;
    switch (uptr->CMD & MT_CMD) {
    case MT_BOOT:
    case MT_READ:
         /* If empty buffer, fill */
         if (BUF_EMPTY(uptr)) {
             sim_debug(DEBUG_DETAIL, dptr, "Read unit=%d ", unit);
             if ((r = sim_tape_rdrecf(uptr, &mt_buffer[0], &reclen,
                              BUFFSIZE)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, " error %d\n", r);
                 uptr->STATUS = STQ_TERM;
                 if (r == MTSE_TMK)
                     uptr->STATUS |= ST1_WARN;
                 else if (r == MTSE_WRP)
                     uptr->STATUS |= ST1_ERR;
                 else if (r == MTSE_EOM)
                     uptr->STATUS |= ST1_ERR|ST2_BLNK;
                 else
                     uptr->STATUS |= ST1_ERR;
                 uptr->CMD = 0;
                 mt_busy = 0;
                 chan_set_done(dev);
                 return SCPE_OK;
             }
             uptr->hwmark = reclen;
             sim_debug(DEBUG_DETAIL, dptr, "Block %d chars\n", reclen);
         }
         stop = 0;

         /* Grab three chars off buffer */
         word = 0;
         for(i = 16; i >= 0; i-=8) {
             if ((uint32)uptr->POS >= uptr->hwmark) {
                /* Add in fill characters */
                if (i == 8) {
                   stop = 2;
                } else if (i == 16) {
                   stop = 1;
                }
                break;
             }
             word |= (uint32)mt_buffer[uptr->POS++] << i;
         }
         sim_debug(DEBUG_DATA, dptr, "unit=%d read %08o\n", unit, word);
         eor = chan_input_word(dev, &word, 0);
         if (eor || (uint32)uptr->POS >= uptr->hwmark) {
             uptr->STATUS = (stop << 12) | STQ_TERM;
             if ((uint32)uptr->POS < uptr->hwmark)
                  uptr->STATUS |= ST1_LONG;
             sim_debug(DEBUG_DATA, dptr, "unit=%d read done %08o %d\n", unit, uptr->STATUS, uptr->POS);
             uptr->CMD = 0;
             mt_busy = 0;
             chan_set_done(dev);
             return SCPE_OK;
         }
         sim_activate(uptr, 100);
         break;

    case MT_WRITEERG: /* Write and Erase */
    case MT_WRITE:
         /* Check if write protected */
         if (sim_tape_wrp(uptr)) {
             uptr->STATUS = STQ_TERM|ST1_ERR;
             uptr->CMD &= ~MT_BUSY;
             mt_busy = 0;
             chan_set_done(dev);
             return SCPE_OK;
         }

         eor = chan_output_word(dev, &word, 0);
         sim_debug(DEBUG_DATA, dptr, "unit=%d write %08o\n", unit, word);

         /* Put three chars in buffer */
         for(i = 16; i >= 0; i-=8) {
             mt_buffer[uptr->POS++] = (uint8)((word >> i) & 0xff);
         }
         uptr->hwmark = uptr->POS;
         if (eor) {
             /* Done with transfer */
             reclen = uptr->hwmark;
             sim_debug(DEBUG_DETAIL, dptr, "Write unit=%d Block %d chars\n",
                      unit, reclen);
             r = sim_tape_wrrecf(uptr, &mt_buffer[0], reclen);
             uptr->STATUS = STQ_TERM;
             if (r != MTSE_OK)
                uptr->STATUS |= ST1_ERR;
             uptr->CMD = 0;
             mt_busy = 0;
             chan_set_done(dev);
             return SCPE_OK;
         }
         sim_activate(uptr, 100);
         break;

    case MT_REV_READ:
         /* If empty buffer, fill */
         if (BUF_EMPTY(uptr)) {
             if (sim_tape_bot(uptr)) {
                 uptr->STATUS = ST1_WARN|ST1_ERR;
                 uptr->CMD = 0;
                 mt_busy = 0;
                 chan_set_done(dev);
                 return SCPE_OK;
             }
             sim_debug(DEBUG_DETAIL, dptr, "Read rev unit=%d ", unit);
             if ((r = sim_tape_rdrecr(uptr, &mt_buffer[0], &reclen,
                              BUFFSIZE)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, " error %d\n", r);
                 uptr->STATUS = STQ_TERM;
                 if (r == MTSE_TMK)
                     uptr->STATUS |= ST1_WARN;
                 else if (r == MTSE_EOM)
                     uptr->STATUS |= ST1_WARN;
                 else
                     uptr->STATUS |= ST1_ERR;
                 uptr->CMD = 0;
                 mt_busy = 0;
                 chan_set_done(dev);
                 return SCPE_OK;
             }
             uptr->POS = reclen;
             uptr->hwmark = reclen;
             sim_debug(DEBUG_DETAIL, dptr, "Block %d chars\n", reclen);
         }

         /* Grab three chars off buffer */
         word = 0;
         stop = 0;
         for(i = 0; i <= 16; i+=8) {
             word |= (uint32)mt_buffer[--uptr->POS] << i;
             if (uptr->POS == 0) {
                stop = 1;
                break;
             }
         }
         sim_debug(DEBUG_DATA, dptr, "unit=%d read %08o\n", unit, word);
         eor = chan_input_word(dev, &word, 0);
         if (eor || uptr->POS == 0) {
             uptr->STATUS = (stop << 12) |STQ_TERM;
             if (uptr->POS != 0)
                  uptr->STATUS |= ST1_LONG;
             sim_debug(DEBUG_DATA, dptr, "unit=%d read done %08o %d\n", unit, uptr->STATUS, uptr->POS);
             uptr->CMD = 0;
             mt_busy = 0;
             chan_set_done(dev);
             return SCPE_OK;
         }
         sim_activate(uptr, 100);
         break;

    case MT_FSF:
         switch(uptr->POS) {
         case 0:
              sim_debug(DEBUG_DETAIL, dptr, "Skip rec unit=%d\n", unit);
              uptr->POS ++;
              sim_activate(uptr, 1000);
              break;
         case 1:
              sim_debug(DEBUG_DETAIL, dptr, "Skip rec unit=%d ", unit);
              r = sim_tape_sprecf(uptr, &reclen);
              if (r == MTSE_TMK) {
                  uptr->POS++;
                  sim_debug(DEBUG_DETAIL, dptr, "MARK\n");
                  uptr->STATUS = 000003; //ST2_TM;
                  sim_activate(uptr, 50);
              } else if (r == MTSE_EOM) {
                  uptr->POS++;
                  uptr->STATUS = ST1_ERR|ST2_BLNK|STQ_TERM;
                  sim_activate(uptr, 50);
              } else {
                  sim_debug(DEBUG_DETAIL, dptr, "%d\n", reclen);
                  sim_activate(uptr, 10 + (20 * reclen));
              }
              break;
         case 2:
              sim_debug(DEBUG_DETAIL, dptr, "Skip rec unit=%d done\n", unit);
              uptr->CMD = 0;
              mt_busy = 0;
              chan_set_done(dev);
         }
         break;

    case MT_WTM:
         if (uptr->POS == 0) {
             if (sim_tape_wrp(uptr)) {
                 uptr->STATUS = ST1_ERR;
                 uptr->CMD = 0;
                 mt_busy = 0;
                 chan_set_done(dev);
                 return SCPE_OK;
             }
             uptr->POS ++;
             sim_activate(uptr, 500);
         } else {
             sim_debug(DEBUG_DETAIL, dptr, "Write Mark unit=%d\n", unit);
             r = sim_tape_wrtmk(uptr);
             if (r != MTSE_OK)
                 uptr->STATUS = ST1_ERR;
             uptr->CMD = 0;
             mt_busy = 0;
             chan_set_done(dev);
         }
         break;

    case MT_BSR:
         switch (uptr->POS ) {
         case 0:
              if (sim_tape_bot(uptr)) {
                  uptr->STATUS = ST1_WARN|ST1_ERR;
                  uptr->CMD = 0;
                  mt_busy = 0;
                  chan_set_done(dev);
                  break;
              }
              uptr->POS ++;
              sim_activate(uptr, 500);
              break;
         case 1:
              sim_debug(DEBUG_DETAIL, dptr, "Backspace rec unit=%d ", unit);
              r = sim_tape_sprecr(uptr, &reclen);
              if (r == MTSE_TMK) 
                  uptr->STATUS = ST1_WARN|STQ_TERM;
              else if (r == MTSE_BOT) 
                  uptr->STATUS = ST1_WARN|STQ_TERM;
              else
                  uptr->STATUS = ST1_ERR|STQ_TERM;
              uptr->CMD = 0;
              mt_busy = 0;
              chan_set_done(dev);
         }
         break;

    case MT_BSF:
         switch (uptr->POS ) {
         case 0:
              if (sim_tape_bot(uptr)) {
                  uptr->STATUS = ST1_WARN|ST1_ERR;
                  uptr->CMD = 0;
                  mt_busy = 0;
                  chan_set_done(dev);
                  break;
              }
              uptr->POS ++;
              sim_activate(uptr, 500);
              break;
         case 1:
              sim_debug(DEBUG_DETAIL, dptr, "Backspace rec unit=%d ", unit);
              r = sim_tape_sprecr(uptr, &reclen);
              if (r == MTSE_TMK || r == MTSE_BOT) {
                  uptr->POS++;
                  if (r == MTSE_TMK) 
                      uptr->STATUS = ST1_WARN|STQ_TERM;
                  if (r == MTSE_BOT) 
                      uptr->STATUS = ST1_WARN|ST1_ERR;
                  sim_activate(uptr, 50);
              } else {
                  sim_debug(DEBUG_DETAIL, dptr, "%d \n", reclen);
                  sim_activate(uptr, 10 + (10 * reclen));
              }
              break;
         case 2:
              uptr->CMD = 0;
              mt_busy = 0;
              chan_set_done(dev);
         }
         break;

    case MT_REW:
         if (uptr->POS == 0) {
             uptr->POS ++;
             sim_activate(uptr, 30000);
             mt_busy = 0;
         } else {
             sim_debug(DEBUG_DETAIL, dptr, "Rewind unit=%d\n", unit);
             r = sim_tape_rewind(uptr);
             uptr->CMD = 0;
             uptr->STATUS = 0;
             chan_set_done(dev);
         }
         break;

    case MT_RUN:
         if (uptr->POS == 0) {
             uptr->POS ++;
             sim_activate(uptr, 30000);
             mt_busy = 0;
         } else {
             sim_debug(DEBUG_DETAIL, dptr, "Unload unit=%d\n", unit);
             r = sim_tape_detach(uptr);
             uptr->CMD = 0;
             uptr->STATUS = 0;
         }
         break;
    }
    return SCPE_OK;
}


/* Reset */

t_stat mt_reset (DEVICE *dptr)
{
    UNIT  *uptr = dptr->units;
    uint32 unit;

    for (unit = 0; unit < dptr->numunits; unit++, uptr++) {
       uptr->CMD = 0;
       uptr->STATUS = 0;
       if ((uptr->flags & UNIT_ATT) != 0)
           uptr->STATUS = 0;
       mt_busy = 0;
    }
    chan_clr_done(GET_UADDR(dptr->flags));
    return SCPE_OK;
}

/* Boot from given device */
t_stat
mt_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT    *uptr = &dptr->units[unit_num];
    int     chan = GET_UADDR(dptr->flags);

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    M[64 + chan] = 0;
    M[256 + 4 * chan] = B2;
    M[257 + 4 * chan] = 020;
    loading = 1;
    mt_busy = 1;
    CLR_BUF(uptr);
    uptr->CMD = MT_BUSY|MT_BOOT;
    uptr->STATUS = 0;
    sim_activate (uptr, 100);
    return SCPE_OK;
}

t_stat
mt_attach(UNIT * uptr, CONST char *file)
{
    t_stat  r;
    uptr->STATUS = 0;
    if ((r = sim_tape_attach_ex(uptr, file, 0, 0)) == SCPE_OK) {
        if (uptr->flags & UNIT_RO)
           uptr->flags |= MTUF_WLK;
    }
    return r;
}

t_stat
mt_detach(UNIT * uptr)
{
    uptr->STATUS = 0;
    return sim_tape_detach(uptr);
}


t_stat mt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cmt)
{
fprintf (st, "The Paper Tape Reader can be set to one of twp modes: 7P, or 7B.\n\n");
fprintf (st, "  mode \n");
fprintf (st, "  7P    Process even parity input tapes. \n");
fprintf (st, "  7B    Ignore parity of input data.\n");
fprintf (st, "The default mode is 7B.\n");
return SCPE_OK;
}

CONST char *mt_description (DEVICE *dptr)
{
    return "MT";

}
#endif
