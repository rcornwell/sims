/* icl1900_eds8.c: ICL1900 EDS8 disk drive simulator.

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

*/

#include "icl1900_defs.h"

#if (NUM_DEVS_EDS8 > 0)
#define UNIT_EDS8       UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE

#define CMD          u3             /* Command */
#define CYL          u4             /* Current cylinder. */
#define HDSEC        u5             /* Head and sector to transfer */
#define POS          u6             /* Current position in block */

#define SECT_TRK     8              /* Sectors/ track */
#define HD_CYL       10             /* Heads / cylinder */
#define CYLS         203            /* Cylinders */
#define WD_SEC       128            /* Number of words per sector */


/*  Command is packed follows:
 *
 *  Low order 5 bits indicate current command.
 *  Bit 6 indicates command terminated.
 *  Bit 7 indicates that first qualifier has not been recieved.
 *  Bit 8 indicates that the second qualifier has not been recieved.
 *  Bit 9 set when unit is started.
 *  Bit 10 set when command starts transfering data.
 *  Bit 11 set during seeking.
 *  Bit 12 indicates error.
 *  Bit 13 indicates wrong track.
 *  Bit 14 indicates long block
 *  Bit 15 indicates drive IRQ.
 *  
 *  Bit 16-24 stores current qualifier.
 */

#define EDS8_CMD      037

#define EDS8_NOP       000
#define EDS8_WRID      007              /* Write Identifiers Q1=head, Q2=F */
#define EDS8_ERASE     014              /* Erase Q1=head, Q2=sect */
#define EDS8_TSTWR     016              /* Test Write Q1=head, Q2=sect */
#define EDS8_SEEK      030              /* Seek Q1=Th, Q2=Tl */
#define EDS8_READ      031              /* Read Q1=head, Q2=sect */
#define EDS8_WRITE     032              /* Write Q1=head, Q2=sect */
#define EDS8_WRCHK     033              /* Write & Check Q1=head, Q2=sect  */
#define EDS8_SUP_RD    034              /* Suppress read Q1=head, Q2=sect */
#define EDS8_RD_TRK    035              /* Read Track Q1=head, Q2=ignored */
#define EDS8_DISC      036              /* Disoconect No Qualifier */
#define EDS8_BOOT      037              /* Boot No Qualifier */

#define EDS8_TERM      000040           /* Command terminated */
#define EDS8_QUAL1     000100           /* 1st Qualifier expected */
#define EDS8_QUAL2     000200           /* 2nd Qualifier expected */
#define EDS8_BUSY      000400           /* Device running command */
#define EDS8_RUN       001000           /* Command executing. */
#define EDS8_SK        002000           /* Seeking in progress. */
#define EDS8_ERR       004000           /* Hard error */
#define EDS8_PATH      010000           /* Wrong track */
#define EDS8_LONG      020000           /* Reached end of cylinder during read/write */

#define ST1_OK         001            /* Unit available */
#define ST1_ERR        002            /* Hard error */
#define ST1_PATH       004            /* Wrong track */
#define ST1_LONG       010            /* Reached end of cylinder during read/write */
#define ST1_IRQ        020            /* Drive changed status */

#define STQ_TERM     001              /* Operation terminated */
#define STQ_DSK_RDY  004              /* Disk can accept orders */
#define STQ_CTL_RDY  030              /* Controller ready to accept new order */
#define STQ_P1       040              /* P1 Status on */


int  eds8_busy;    /* Indicates that controller is talking to a drive */
int  eds8_drive;   /* Indicates last selected drive */
uint32 eds8_buffer[WD_SEC];
void eds8_cmd (uint32 dev, uint32 cmd, uint32 *resp);
t_stat eds8_svc (UNIT *uptr);
t_stat eds8_reset (DEVICE *dptr);
t_stat eds8_boot (int32 unit_num, DEVICE * dptr);
t_stat eds8_attach(UNIT *, CONST char *);
t_stat eds8_detach(UNIT *);
t_stat eds8_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
CONST char *eds8_description (DEVICE *dptr);

DIB eds8_dib = {  WORD_DEV|MULT_DEV, &eds8_cmd, NULL, NULL };


MTAB                eds8_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock drive" },
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "DEV", "DEV",
         &set_chan, &get_chan, NULL, "Device Number"},
    {0}
};

UNIT                eds8_unit[] = {
    {UDATA(&eds8_svc, UNIT_EDS8, 0) },       /* 0 */
    {UDATA(&eds8_svc, UNIT_EDS8, 0) },       /* 1 */
    {UDATA(&eds8_svc, UNIT_EDS8, 0) },       /* 2 */
    {UDATA(&eds8_svc, UNIT_EDS8, 0) },       /* 3 */
    {UDATA(&eds8_svc, UNIT_EDS8, 0) },       /* 4 */
    {UDATA(&eds8_svc, UNIT_EDS8, 0) },       /* 5 */
    {UDATA(&eds8_svc, UNIT_EDS8, 0) },       /* 6 */
    {UDATA(&eds8_svc, UNIT_EDS8, 0) },       /* 7 */
};

DEVICE eds8_dev = {
    "ED", eds8_unit, NULL, eds8_mod,
    NUM_DEVS_EDS8, 8, 22, 1, 8, 22,
    NULL, NULL, &eds8_reset, &eds8_boot, &eds8_attach, &detach_unit,
    &eds8_dib, DEV_DISABLE | DEV_DEBUG | UNIT_ADDR(27), 0, dev_debug,
    NULL, NULL, &eds8_help, NULL, NULL, &eds8_description
    };

void eds8_cmd(uint32 dev, uint32 cmd, uint32 *resp) {
    UNIT  *uptr = &eds8_unit[eds8_drive];
    *resp = 0;
    if (cmd & 0400) {
        sim_debug(DEBUG_CMD, &eds8_dev, "Cmd: set unit=%d %04o\n", eds8_drive, cmd);
        eds8_drive = cmd & 07;
        uptr = &eds8_unit[eds8_drive];
        if ((uptr->flags & UNIT_ATT) != 0)
           *resp = 5;
        return;
    }
    cmd &= ~02000;
    switch(cmd & 070) {
    case 000: if (cmd == 7)
                  cmd |= EDS8_QUAL1|EDS8_QUAL2;
              else if (cmd != 0) {
                  *resp = 3;
                  return;
              }
              break;

    case 010: if ((cmd & 05) != 4) {
                  *resp = 3;
                  return;
              }
              cmd |= EDS8_QUAL1|EDS8_QUAL2;
              break;

    case 020: if (cmd == SEND_Q) {
                 if (uptr->CMD & EDS8_TERM)
                    *resp |= STQ_TERM;
                 if ((uptr->CMD & (EDS8_BUSY|EDS8_QUAL1|EDS8_QUAL2)) == 0)
                     *resp |= STQ_DSK_RDY;
                 if (eds8_busy == 0)
                     *resp |= STQ_CTL_RDY;
                 if ((uptr->flags & UNIT_ATT) != 0)
                     *resp |= STQ_P1;
                 uptr->CMD &= ~EDS8_TERM;
                 chan_clr_done(dev);
              } else if (cmd == SEND_P) {
                 *resp = (uptr->CMD >> 10) & 036;
                 if ((uptr->flags & UNIT_ATT) != 0)
                     *resp |= 1;
                 uptr->CMD &= ~(036 << 10);
              }
              sim_debug(DEBUG_STATUS, &eds8_dev, "Status: unit:=%d %02o %02o\n", eds8_drive, cmd, *resp);
              return;
    case 030: 
              sim_debug(DEBUG_CMD, &eds8_dev, "Cmd: unit=%d %02o\n", eds8_drive, cmd);
              cmd &= 077;
              if (cmd < 036)
                   cmd |= EDS8_QUAL1|EDS8_QUAL2;
              break;
    case 040:
    case 050:
              if (uptr->CMD & EDS8_QUAL1) {
                 uptr->CMD |= ((cmd & 017) << 20);
                 uptr->CMD &= ~EDS8_QUAL1;
              } else if (uptr->CMD & EDS8_QUAL2) {
                 uptr->CMD |= ((cmd & 017) << 16);
                 uptr->CMD &= ~EDS8_QUAL2;
              }
              sim_debug(DEBUG_STATUS, &eds8_dev, "Qual: unit:=%d %02o %02o\n", eds8_drive, cmd, *resp);
              cmd = uptr->CMD;
              *resp = 5;
              break;
    default:
              *resp = 3;
              return;
    }
    sim_debug(DEBUG_CMD, &eds8_dev, "Cmd: unit=%d %02o\n", eds8_drive, cmd);
    if ((uptr->flags & UNIT_ATT) == 0) {
       *resp = 0;
       return;
    }
    if (eds8_busy || (uptr->CMD & EDS8_BUSY)) {
       *resp = 3;
       return;
    }
    if (cmd == 0) {
       *resp = 5;
       return;
    }
    uptr->CMD = cmd;
    if ((uptr->CMD & (EDS8_QUAL1|EDS8_QUAL2)) == 0) {
       sim_debug(DEBUG_CMD, &eds8_dev, "Cmd: unit=%d start %02o\n", eds8_drive, uptr->CMD);
       eds8_busy = 1;
       uptr->CMD |= EDS8_BUSY;
       chan_clr_done(dev);
       sim_activate(uptr, 100);
    }
    *resp = 5;
    return;
}

t_stat eds8_svc (UNIT *uptr)
{
    DEVICE      *dptr = &eds8_dev;
    int          unit = (uptr - dptr->units);
    int          dev = GET_UADDR(dptr->flags);
    uint32       word;
    int          i;
    int          wc;
    int          eor;
    int          da;

    /* If not busy, false schedule, just exit */
    if ((uptr->CMD & EDS8_BUSY) == 0)
        return SCPE_OK;

    /* If we need to seek, move heads. */
    if (uptr->CMD & EDS8_SK) {
        int diff;
        sim_debug(DEBUG_DETAIL, &eds8_dev, "Seek: unit:=%d %d %d\n", unit, uptr->CYL, (uptr->CMD >> 16) & 0377);
        diff = uptr->CYL - ((uptr->CMD >> 16) & 0377);
        i = 1;
        if (diff < 0) {
            diff = -diff;
            i = -1;
        }
        if (diff > 80) {
            uptr->CYL -= i*20;
            sim_activate(uptr, 2000);
            return SCPE_OK;
        } else if (diff > 50) {
            uptr->CYL -= i*10;
            sim_activate(uptr, 1000);
            return SCPE_OK;
        } else if (diff > 10) {
            uptr->CYL -= i*10;
            sim_activate(uptr, 500);
            return SCPE_OK;
        } else if (diff > 0) {
            uptr->CYL -= i*1;
            sim_activate(uptr, 10);
            return SCPE_OK;
        } else if (diff == 0) {
            uptr->CMD &= ~EDS8_SK;
        }
    }
    
    switch (uptr->CMD & EDS8_CMD) {
    case EDS8_TSTWR:
         /* Compare disk control word with generated one */
         break;

    case EDS8_ERASE:
         /* Write data directly to disk ignoring headers. */
         /* 146 to 166 words per sector */
         break;

    case EDS8_DISC:
         /* Retract heads and offline the drive */
         detach_unit(uptr);
         uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
         uptr->CMD |= EDS8_TERM;
         eds8_busy = 0;
         chan_set_done(dev);
         break;

    case EDS8_SEEK:
         /* Set desired cylinder to value */
         if ((uptr->CMD & EDS8_RUN) == 0) {
             int trk = (uptr->CMD >> 16) & 0377;
             sim_debug(DEBUG_DETAIL, &eds8_dev, "Seek: start unit:=%d %d %d\n", unit, uptr->CYL, trk);
             if (uptr->CYL == trk) {
                /* Terminate */
                uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
                uptr->CMD |= EDS8_TERM;
             } else if (trk > CYLS) {
                /* Terminate with error */
                uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
                uptr->CMD |= EDS8_TERM;
             } else {
                uptr->CMD |= EDS8_RUN|EDS8_SK|EDS8_TERM;
                sim_activate(uptr, 500);
             }
             eds8_busy = 0;
             /* trigger controller available */
             chan_set_done(dev);
             break;
         }
         if (uptr->CYL == ((uptr->CMD >> 16) & 0377)) {
             /* Terminate */
             uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
             uptr->CMD |= EDS8_TERM;
             chan_set_done(dev);
         }
         break;

    case EDS8_SUP_RD:
         /* Start reading sectors at the requested value. */
         /* Read one sector for each word transfered. */
         if ((uptr->CMD & EDS8_RUN) == 0) {
             uptr->CMD |= EDS8_RUN;
             uptr->HDSEC = (uptr->CMD >> 16) & 0377;
             if (((uptr->HDSEC >> 4) & 017) > HD_CYL || (uptr->HDSEC & 017) > SECT_TRK) {  
                 /* Terminate wrong track */
                 uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
                 uptr->CMD |= EDS8_TERM|EDS8_PATH;
                 eds8_busy = 0;
                 chan_set_done(dev);
                 break;
             }
         }
         eor = chan_output_word(dev, &word, 0);
         if (eor) {
             /* Terminate */
             uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
             uptr->CMD |= EDS8_TERM;
             eds8_busy = 0;
             chan_set_done(dev);
             break;
         }

         sim_debug(DEBUG_DATA, &eds8_dev, "RSUP: %08o\n", word);
         uptr->HDSEC += 9;        /* Bump sector number, if more then 8, will bump head */
         uptr->HDSEC &= 0367;
         /* If empty buffer, fill */
         if (((uptr->HDSEC >> 4) & 017) > HD_CYL) {  
             /* Terminate long read */
             uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
             uptr->CMD |= EDS8_TERM|EDS8_LONG;
             eds8_busy = 0;
             chan_set_done(dev);
             break;
         }
         sim_activate(uptr, 100);
         break;

    case EDS8_BOOT:
         /* Set cylinder to zero and set sector to zero and start read */
         if ((uptr->CMD & EDS8_RUN) == 0 && uptr->CYL != 0) {
            uptr->CMD |= EDS8_SK; /* Restore to cylinder zero */
            sim_activate(uptr, 100);
            return SCPE_OK;
         } 
         /* Fall through */


    case EDS8_RD_TRK:
         /* Read a track into memory */
         /* Format is: Word 0: T4 T5 T6 T7 0 0 
                               T0 T1 T2 T3 0 0
                               H0 H1 H2 H3 0 0
                               S0 S1 S2 F  0 0
                       Word 1-129: sector data.
                       Word 130: control word of data block.
          */

    case EDS8_READ:
         if ((uptr->CMD & EDS8_RUN) == 0) {
             uptr->CMD |= EDS8_RUN;
             uptr->HDSEC = (uptr->CMD >> 16) & 0377;
             if (((uptr->HDSEC >> 4) & 017) > HD_CYL || (uptr->HDSEC & 017) > SECT_TRK) {  
                 /* Terminate wrong track */
                 uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
                 uptr->CMD |= EDS8_TERM|EDS8_PATH;
                 eds8_busy = 0;
                 chan_set_done(dev);
                 break;
             }
         }
         da = ((((uptr->CYL * HD_CYL) + ((uptr->HDSEC >> 4) & 017)) * SECT_TRK) +
                (uptr->HDSEC & 07)) * WD_SEC;
         (void)sim_fseek(uptr->fileref, da * sizeof(uint32), SEEK_SET);
         wc = sim_fread(&eds8_buffer[0], sizeof(uint32), WD_SEC, uptr->fileref);
         while(wc < WD_SEC)
            eds8_buffer[wc++] = 0;

         /* Compute header word */
         if ((uptr->CMD & EDS8_CMD) == EDS8_RD_TRK) {
             word = (((uptr->CYL >> 4) & 017) << 18) |
                     ((uptr->CYL & 017) << 12) |
                     (((uptr->HDSEC >> 4) & 017) << 6) |
                     ((uptr->HDSEC << 1) & 016);
             eor = chan_input_word(dev, &word, 0);
             if (eor) {
                 /* Terminate */
                 uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
                 uptr->CMD |= EDS8_TERM;
                 eds8_busy = 0;
                 chan_set_done(dev);
                 break;
             }
         }

         for (i = 0; i < WD_SEC; i++) {
             sim_debug(DEBUG_DATA, &eds8_dev, "Data: %d <%08o\n", i, eds8_buffer[i]);
             eor = chan_input_word(dev, &eds8_buffer[i], 0);
             if (eor) {
                 /* Terminate */
                 uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
                 uptr->CMD |= EDS8_TERM;
                 eds8_busy = 0;
                 chan_set_done(dev);
                 break;
             }
         }

         /* Compute control word */ 
         if ((uptr->CMD & EDS8_CMD) == EDS8_RD_TRK) {
             uint32   even = 0;
             uint32   odd = 0;

             for (i = 0; i <WD_SEC; i++) {
                 word = eds8_buffer[i];
                 even ^= (word >> 20) ^ (word >> 16) ^ (word >> 12) ^
                         (word >> 8) ^ (word >> 4) ^ word;
                 odd ^= (word >> 18) ^ (word >> 12) ^ (word >> 6) ^ word;
             }
             word = ((even & 017) << 12) | (((odd ^ 017) & 017) << 8) | 0100;
             eor = chan_input_word(dev, &word, 0);
             if (eor) {
                 /* Terminate */
                 uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
                 uptr->CMD |= EDS8_TERM;
                 eds8_busy = 0;
                 chan_set_done(dev);
                 break;
             }
         }
    
         uptr->HDSEC += 9;        /* Bump sector number, if more then 8, will bump head */
         uptr->HDSEC &= 0367;
         /* If empty buffer, fill */
         if (((uptr->HDSEC >> 4) & 017) > HD_CYL) {  
             /* Terminate long read */
             uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
             uptr->CMD |= EDS8_TERM|EDS8_LONG;
             eds8_busy = 0;
             chan_set_done(dev);
             break;
         }
         sim_activate(uptr, 100);
         break;

    case EDS8_WRID:
         /* Write track id's set F if set */
         if ((uptr->CMD & EDS8_RUN) == 0) {
         /* Write the starting at head/sector, if check, then check
            that data can be read back at end of each track */
             /* Check if write protected */
             if ((uptr->flags & UNIT_WPRT) != 0) {
                     uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
                     uptr->CMD |= EDS8_TERM|EDS8_ERR;
                     eds8_busy = 0;
                     chan_set_done(dev);
                     break;
             }
             uptr->CMD |= EDS8_RUN;
             uptr->HDSEC = (uptr->CMD >> 16) & 0360;
             if (((uptr->HDSEC >> 4) & 017) > HD_CYL) {  
                 /* Terminate wrong track */
                 uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
                 uptr->CMD |= EDS8_TERM|EDS8_PATH;
                 eds8_busy = 0;
                 chan_set_done(dev);
                 break;
             }
         }
         for (wc = 0; wc < WD_SEC; wc++)
            eds8_buffer[wc] = 0;

         da = ((((uptr->CYL * HD_CYL) + ((uptr->HDSEC >> 4) & 017)) * SECT_TRK) +
                (uptr->HDSEC & 07)) * WD_SEC;
         (void)sim_fseek(uptr->fileref, da * sizeof(uint32), SEEK_SET);
         wc = sim_fwrite(&eds8_buffer[0], sizeof(uint32), WD_SEC, uptr->fileref);
    
         uptr->HDSEC += 9;        /* Bump sector number, if more then 8, will bump head */
         /* If empty buffer, fill */
         if ((uptr->HDSEC & 010) != 0) {  
             /* Terminate */
             uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
             uptr->CMD |= EDS8_TERM;
             eds8_busy = 0;
             chan_set_done(dev);
             break;
         }
         sim_activate(uptr, 100);
         break;

    case EDS8_WRCHK: 
    case EDS8_WRITE:
         /* Write the starting at head/sector, if check, then check
            that data can be read back at end of each track */
         if ((uptr->CMD & EDS8_RUN) == 0) {
             /* Check if write protected */
             if ((uptr->flags & UNIT_WPRT) != 0) {
                 uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
                 uptr->CMD |= EDS8_TERM|EDS8_ERR;
                 eds8_busy = 0;
                 chan_set_done(dev);
                 break;
             }
             uptr->CMD |= EDS8_RUN;
             uptr->HDSEC = (uptr->CMD >> 16) & 0377;
             if (((uptr->HDSEC >> 4) & 017) > HD_CYL || (uptr->HDSEC & 017) > SECT_TRK) {  
             /* Terminate wrong track */
                 uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
                 uptr->CMD |= EDS8_TERM|EDS8_PATH;
                 eds8_busy = 0;
                 chan_set_done(dev);
                 break;
             }
         }
         for (wc = 0; wc < WD_SEC; wc++)
            eds8_buffer[wc] = 0;

         for (i = 0; i < WD_SEC; i++) {
             eor = chan_output_word(dev, &eds8_buffer[i], 0);
             if (eor)
                break;
             sim_debug(DEBUG_DATA, &eds8_dev, "Data: %d >%08o\n", i, eds8_buffer[i]);
         }

         da = ((((uptr->CYL * HD_CYL) + ((uptr->HDSEC >> 4) & 017)) * SECT_TRK) +
                (uptr->HDSEC & 07)) * WD_SEC;
         (void)sim_fseek(uptr->fileref, da * sizeof(uint32), SEEK_SET);
         wc = sim_fwrite(&eds8_buffer[0], sizeof(uint32), WD_SEC, uptr->fileref);

         if (eor) {
             /* Terminate */
             uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
             uptr->CMD |= EDS8_TERM;
             eds8_busy = 0;
             chan_set_done(dev);
             break;
         }

    
         uptr->HDSEC += 9;        /* Bump sector number, if more then 8, will bump head */
         uptr->HDSEC &= 0367;
         /* If empty buffer, fill */
         if (((uptr->HDSEC >> 4) & 017) > HD_CYL) {  
             /* Terminate long read */
             uptr->CMD &= ~(EDS8_RUN|EDS8_SK|EDS8_BUSY);
             uptr->CMD |= EDS8_TERM|EDS8_LONG;
             eds8_busy = 0;
             chan_set_done(dev);
             break;
         }
         sim_activate(uptr, 100);
         break;
    }
    return SCPE_OK;
}


/* Reset */

t_stat eds8_reset (DEVICE *dptr)
{
    UNIT  *uptr = dptr->units;
    uint32 unit;

    for (unit = 0; unit < dptr->numunits; unit++, uptr++) {
       uptr->CMD = 0;
       uptr->CYL = 0;
       eds8_busy = 0;
    }
    chan_clr_done(GET_UADDR(dptr->flags));
    return SCPE_OK;
}

/* Boot from given device */
t_stat
eds8_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT    *uptr = &dptr->units[unit_num];
    int     chan = GET_UADDR(dptr->flags);

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    M[64 + chan] = 0;
    M[256 + 4 * chan] = B2;
    M[257 + 4 * chan] = 020;
    loading = 1;
    eds8_busy = 1;
    uptr->CMD = EDS8_BUSY|EDS8_BOOT;
    sim_activate (uptr, 100);
    return SCPE_OK;
}

t_stat
eds8_attach(UNIT * uptr, CONST char *file)
{
    t_stat r;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;
    uptr->CYL = 0;
    uptr->CMD = EDS8_TERM;
    chan_set_done(GET_UADDR(eds8_dev.flags));
    return SCPE_OK;
}



t_stat eds8_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cmt)
{
fprintf (st, "The Paper Tape Reader can be set to one of twp modes: 7P, or 7B.\n\n");
fprintf (st, "  mode \n");
fprintf (st, "  7P    Process even parity input tapes. \n");
fprintf (st, "  7B    Ignore parity of input data.\n");
fprintf (st, "The default mode is 7B.\n");
return SCPE_OK;
}

CONST char *eds8_description (DEVICE *dptr)
{
    return "ED";

}
#endif
