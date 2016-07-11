/* ka10_mt.c: TM10A/B Magnetic tape controller

   Copyright (c) 2016, Richard Cornwell

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

#include "ka10_defs.h"
#include "sim_tape.h"

#ifndef NUM_DEVS_MT
#define NUM_DEVS_MT 0
#endif

#if (NUM_DEVS_MT > 0) 

#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

#define MTDF_TYPEB      (1 << DEV_V_UF)
#define MTDF_BUFFUL     (2 << DEV_V_UF)
#define MTDF_STOP       (4 << DEV_V_UF)
#define MTDF_MOTION     (8 << DEV_V_UF)
#define MTDF_V_UNIT     (DEV_V_UF + 4)
#define MTDF_UNIT_MSK   (7 << MTDF_V_UNIT)

#define BUFFSIZE        (32 * 1024)
#define UNIT_MT(x)      UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE | \
                        UNIT_S_CHAN(x)
#define MTUF_ONLINE     (1 << UNIT_V_LOCAL)
#define LT              66      /* Time per char low density */
#define HT              16      /* Time per char high density */

#define NOP_CLR         000     /* Nop clear, interrupt */
#define NOP_IDLE        010     /* Nop interrupt when idle */
#define REWIND          001     /* Rewind */
#define UNLOAD          011     /* Unload */
#define READ            002     /* Read  */
#define READ_NOEOR      012     /* Read no end of record. */
#define CMP             003     /* Compare */
#define CMP_NOEOR       013     /* Compare no end of record. */
#define WRITE           004     /* Write */
#define WRITE_LONG      014     /* Write with long record gap. */
#define WTM             005     /* Write End of File */
#define ERG             015     /* Write blank tape */
#define SPC_FWD         006     /* Space forward */
#define SPC_EOF         016     /* Space to end of file */
#define SPC_REV         007     /* Space reverse */
#define SPC_REV_EOF     017     /* Space reverse to EOF. */

#define DATA_REQUEST    0000000001
#define NEXT_UNIT       0000000002
#define SEVEN_CHAN      0000000004
#define WRITE_LOCK      0000000010
#define CHAN_ERR        0000000020
#define IDLE_UNIT       0000000040
#define JOB_DONE        0000000100
#define BAD_TAPE        0000000200
#define DATA_LATE       0000000400
#define RLC_ERR         0000001000
#define READ_CMP        0000002000
#define EOT_FLAG        0000004000
#define EOF_FLAG        0000010000
#define PARITY_ERR      0000020000
#define ILL_OPR         0000040000
#define BOT_FLAG        0000100000
#define REW_FLAG        0000200000
#define TRAN_HUNG       0000400000
#define CHAR_COUNT      0017000000
#define WT_CW_DONE      0020000000
#define DATA_PARITY     0040000000
#define NXM_ERR         0100000000
#define CW_PAR_ERR      0200000000
#define B22_FLAG        01000000000


#define DATA_PIA        000000007       /* 0 */
#define FLAG_PIA        000000070       /* 3 */
#define DENS_200        000000000       
#define DENS_556        000000100
#define DENS_800        000000200
#define DENS_MSK        000000300       /* 6 */
#define NXT_UNIT_ENAB   000000400       /* 8 */
#define FUNCTION        000017000       /* 9 */
#define CORE_DUMP       000020000       /*13 */
#define ODD_PARITY      000040000       /*14 */
#define UNIT_NUM        000700000       /*15 */
#define NXT_UNIT        007000000       /*18 */

#define MT_DEVNUM       0340
/* in u3 is device address */
/* in u4 is current buffer position */
/* in u5 */

t_stat         mt_devio(uint32 dev, uint64 *data);
t_stat         mt_srv(UNIT *);
t_stat         mt_boot(int32, DEVICE *);
void           mt_ini(UNIT *, t_bool);
t_stat         set_mta (UNIT *uptr, int32 val, CONST char *cptr, void *desc) ;
t_stat         show_mta (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat         mt_reset(DEVICE *);
t_stat         mt_attach(UNIT *, CONST char *);
t_stat         mt_detach(UNIT *);
t_stat         mt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                  const char *cptr);
const char     *mt_description (DEVICE *dptr);

uint32          mt_cia;
uint32          mt_ccw;
uint32          mt_wcr;
uint32          mt_cda;
uint32          status;

uint64          hold_reg;
uint64          buf_reg;
int             cc;


/* One buffer per channel */
uint8               mt_buffer[BUFFSIZE];

UNIT                mt_unit[] = {
/* Controller 1 */
    {UDATA(&mt_srv,  UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                     UNIT_ROABLE, 0)},  /* 0 */
    {UDATA(&mt_srv,  UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                     UNIT_ROABLE, 0)},  /* 1 */
    {UDATA(&mt_srv,  UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                     UNIT_ROABLE, 0)},  /* 2 */
    {UDATA(&mt_srv,  UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                     UNIT_ROABLE, 0)},  /* 3 */
    {UDATA(&mt_srv,  UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                     UNIT_ROABLE, 0)},  /* 4 */
    {UDATA(&mt_srv,  UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                     UNIT_ROABLE, 0)},  /* 5 */
    {UDATA(&mt_srv,  UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                     UNIT_ROABLE, 0)},  /* 6 */
    {UDATA(&mt_srv,  UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                     UNIT_ROABLE, 0)},  /* 7 */
};

DIB mt_dib = {MT_DEVNUM, 2, &mt_devio};

MTAB                mt_mod[] = {
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL},
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, MTDF_TYPEB, "TYPE", "TYPE", &set_mta, &show_mta},
    {MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LENGTH", "LENGTH",
     &sim_tape_set_capac, &sim_tape_show_capac, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "DENSITY", "DENSITY",
     &sim_tape_set_dens, &sim_tape_show_dens, NULL},
    {0}
};


DEVICE              mt_dev = {
    "MTA", mt_unit, NULL, mt_mod,
    8, 8, 15, 1, 8, 8,
    NULL, NULL, &mt_reset, &mt_boot, &mt_attach, &mt_detach,
    &mt_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &mt_help, NULL, NULL, &mt_description
};

t_stat mt_devio(uint32 dev, uint64 *data) {
      uint64     res;
      DEVICE    *dptr = &mt_dev;
      int        unit = (dptr->flags >> MTDF_V_UNIT) & 7;
      UNIT      *uptr = &mt_unit[unit];
       
      switch(dev & 07) {
      case CONI:
          res = (uptr->u3 & 077777);
          res |= unit << 15;
          res |= unit << 18;
          if (dptr->flags & MTDF_TYPEB) 
             res |= 7;  /* Force DATA PIA to 7 on type B */
          *data = res;
          sim_debug(DEBUG_CONI, dptr, "MT CONI %03o status %06o %o\n",
                      dev, (uint32)res, unit);
          break;

       case CONO:
          unit = (*data >> 15) & 07;
          uptr = &mt_unit[unit];
          uptr->u3 = *data;
          CLR_BUF(uptr);
          dptr->flags &= ~(MTDF_BUFFUL|MTDF_STOP|MTDF_UNIT_MSK);
          dptr->flags |= (unit << MTDF_V_UNIT);
          sim_debug(DEBUG_CONO, dptr, "MT CONO %03o start %o %o %o\n",
                      dev, uptr->u3, unit, (uptr->u3 & FUNCTION) >> 9);
          if (uptr->u3 & 000007000)
             dptr->flags |= MTDF_MOTION;
          status &= ~(DATA_REQUEST|CHAN_ERR|JOB_DONE|DATA_LATE| \
                      BAD_TAPE|RLC_ERR|READ_CMP|EOF_FLAG|EOT_FLAG|BOT_FLAG|PARITY_ERR| \
                      ILL_OPR|REW_FLAG|TRAN_HUNG|CHAR_COUNT|WT_CW_DONE| \
                      DATA_PARITY|NXM_ERR|CW_PAR_ERR);
          clr_interrupt(MT_DEVNUM + 4);
          sim_activate(uptr, 1000);
          buf_reg = 0;
          mt_ccw = mt_cia;
          mt_wcr = 0;
          break;

     case DATAI:
          /* Xfer data */
          dptr->flags &= ~MTDF_BUFFUL;
          status &= ~DATA_REQUEST;
          if (status & JOB_DONE)
              buf_reg = 0;
          clr_interrupt(MT_DEVNUM);
          *data = hold_reg;
          sim_debug(DEBUG_DATA, dptr, "MT %03o >%012llo\n", dev, hold_reg);
          break;

     case DATAO:
          /* Xfer data */
          hold_reg = *data;
          dptr->flags |= MTDF_BUFFUL;
          status &= ~DATA_REQUEST;
          clr_interrupt(MT_DEVNUM);
          if (status & JOB_DONE)
              buf_reg = 0;
          sim_debug(DEBUG_DATA, dptr, "MT %03o <%012llo\n", dev, hold_reg);
          break;
     case CONI|04:
          res = status;
          if ((dptr->flags & MTDF_MOTION) == 0)
              res |= IDLE_UNIT;
#if KI10_22BIT
          res |= B22_FLAG;
#endif
          *data = res;
          sim_debug(DEBUG_CONI, dptr, "MT CONI %03o status2 %06o %o\n",
                      dev, (uint32)res, unit);
          break;
     case CONO|04:
          if (*data & 1) {
              dptr->flags |= MTDF_STOP;
              if (status & DATA_REQUEST) {
                  status &= ~DATA_REQUEST;
                  clr_interrupt(MT_DEVNUM);
              }
          }
          if (*data & 2) {
              hold_reg = buf_reg;
              buf_reg = 0;
          }   
          sim_debug(DEBUG_CONO, dptr, "MT CONO %03o control %o %o %o\n",
                      dev, uptr->u3, unit, (uptr->u3 & FUNCTION) >> 9);
          break;
     case DATAI|04:
          break;
     case DATAO|04:
          /* Set Initial CCW */
          mt_cia = *data & 0776;
          sim_debug(DEBUG_DATAIO, dptr, "MT DATAO %03o %012llo\n", dev, *data);
          break;
     }
     return SCPE_OK;
}


void mt_finish_op(DEVICE *dptr, int flags) {
     status |= flags|WT_CW_DONE;
     M[mt_cia|1] = (((uint64)(mt_ccw)) << CSHIFT) | mt_cda;
     dptr->flags |= MTDF_STOP;
}

int mt_df10_fetch(DEVICE *dptr, int addr) {
     uint64 data;
     data = M[mt_ccw];
     while((data & (WMASK & CSHIFT)) == 0) {
         if ((data & AMASK) == 0) {
              mt_finish_op(dptr,0);
              return 0;
         }
         mt_ccw = data & AMASK;
         if (mt_ccw > MEMSIZE) {
             mt_finish_op(dptr, NXM_ERR);
             return 0;
         }
         data = M[mt_ccw];
     }
     mt_wcr = (data >> CSHIFT) &WMASK;
     mt_cda = data &AMASK;
     mt_ccw = (mt_ccw + 1) & AMASK;
     return 1;
}

int mt_df10_read(DEVICE *dptr, UNIT *uptr) {
     uint64 data;
     if (dptr->flags & MTDF_TYPEB) {
         if (mt_wcr == 0) {
             if(!mt_df10_fetch(dptr, 0))
                  return 0;
         }
         mt_wcr = (mt_wcr + 1) & WMASK;
         if (mt_cda != 0) {
            if (mt_cda > MEMSIZE) {
               mt_finish_op(dptr, NXM_ERR);
               return 0;
            }
            mt_cda = (mt_cda + 1) & AMASK;
            buf_reg = M[mt_cda];
         } else {
            buf_reg = 0;
         }
         dptr->flags &= ~MTDF_BUFFUL;
         if (mt_wcr == 0) {
            mt_df10_fetch(dptr, mt_ccw);
         }
     } else {
        if (dptr->flags & MTDF_BUFFUL) {
            buf_reg = hold_reg;
            dptr->flags &= ~MTDF_BUFFUL;
            if ((dptr->flags & MTDF_STOP) == 0) {
                status |= DATA_REQUEST;
                set_interrupt(MT_DEVNUM, uptr->u3 & DATA_PIA);
            }
        } else {
            if ((dptr->flags & MTDF_STOP) == 0) {
                status |= DATA_LATE;
                dptr->flags |= MTDF_STOP;
            }
            return 0;
        }
     }
     return 1;
}

int mt_df10_write(DEVICE *dptr, UNIT *uptr) {
     if (dptr->flags & MTDF_TYPEB) {
        if (mt_wcr == 0) {
            if(!mt_df10_fetch(dptr, 0))
               return 0;
        }
        mt_wcr = (mt_wcr + 1) & WMASK;
        if (mt_cda != 0) {
            if (mt_cda > MEMSIZE) {
                mt_finish_op(dptr, NXM_ERR);
                return 0;
            }
            mt_cda = (mt_cda + 1) & AMASK;
            M[mt_cda] = hold_reg;
        } else {
           buf_reg = 0;
        }
        dptr->flags &= ~MTDF_BUFFUL;
        if (mt_wcr == 0) {
            mt_df10_fetch(dptr, mt_ccw);
        }
     } else {
        if (dptr->flags & MTDF_BUFFUL) {
            status |= DATA_LATE;
            dptr->flags |= MTDF_STOP;
            return 0;
        } else {
            status |= DATA_REQUEST;
            dptr->flags |= MTDF_BUFFUL;
            set_interrupt(MT_DEVNUM, uptr->u3 & DATA_PIA);
        }
     }
     return 1;
}


/* Map simH errors into machine errors */
t_stat mt_error(UNIT * uptr, t_stat r, DEVICE * dptr)
{
       switch (r) {
       case MTSE_OK:            /* no error */
            status |= JOB_DONE|IDLE_UNIT;
            set_interrupt(MT_DEVNUM+4, (uptr->u3 & FLAG_PIA) >> 3);
            return SCPE_OK;
      
       case MTSE_TMK:           /* tape mark */
            status |= EOF_FLAG;
            break;
      
       case MTSE_WRP:           /* write protected */
            status |= WRITE_LOCK;
            break;
       case MTSE_UNATT:         /* unattached */
            status |= TRAN_HUNG;
            break;
      
       case MTSE_IOERR:         /* IO error */
       case MTSE_FMT:           /* invalid format */
            status |= ILL_OPR;
            break;
       case MTSE_RECE:          /* error in record */
            status |= BAD_TAPE;
            break;
       case MTSE_BOT:           /* beginning of tape */
            status |= BOT_FLAG;
            break;
       case MTSE_INVRL:         /* invalid rec lnt */
            status |= RLC_ERR;
            break;
       case MTSE_EOM:           /* end of medium */
            status |= EOT_FLAG;
            break;
       }
       status |= JOB_DONE|IDLE_UNIT;
       sim_debug(DEBUG_EXP, dptr, "Setting status %o\n", status);
       set_interrupt(MT_DEVNUM+4, (uptr->u3 & FLAG_PIA) >> 3);
       return SCPE_OK;
}

/* Handle processing of tape requests. */
t_stat mt_srv(UNIT * uptr)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 unit = (uptr - dptr->units) & 7;
    int                 cmd = (uptr->u3 & FUNCTION) >> 9;
    t_mtrlnt            reclen;
    t_stat              r = SCPE_ARG;   /* Force error if not set */


    switch(cmd) {
    case NOP_IDLE:
        sim_debug(DEBUG_DETAIL, dptr, "MT%o Idle\n", unit);
        dptr->flags &= ~MTDF_MOTION;
        if (status & NEXT_UNIT) 
            set_interrupt(MT_DEVNUM+4, (uptr->u3 & FLAG_PIA) >> 3);
        return mt_error(uptr, MTSE_OK, dptr);
    case NOP_CLR:
        sim_debug(DEBUG_DETAIL, dptr, "MT%o nop\n", unit);
        return SCPE_OK;
    case REWIND:
        sim_debug(DEBUG_DETAIL, dptr, "MT%o rewind\n", unit);
        dptr->flags &= ~MTDF_MOTION;
        status |= BOT_FLAG;
        if (status & NEXT_UNIT) 
            set_interrupt(MT_DEVNUM+4, (uptr->u3 & FLAG_PIA) >> 3);
        return mt_error(uptr, sim_tape_rewind(uptr), dptr);

    case UNLOAD:
        sim_debug(DEBUG_DETAIL, dptr, "MT%o unload\n", unit);
        dptr->flags &= ~MTDF_MOTION;
        if (status & NEXT_UNIT) 
            set_interrupt(MT_DEVNUM+4, (uptr->u3 & FLAG_PIA) >> 3);
        return mt_error(uptr, sim_tape_detach(uptr), dptr);
    case READ:
    case READ_NOEOR:
    case CMP:
    case CMP_NOEOR:
        if (BUF_EMPTY(uptr)) {
            if ((uptr->u3 & DENS_MSK) != DENS_800) 
                return mt_error(uptr, MTSE_FMT, dptr);
            if ((uptr->flags & UNIT_ATT) == 0)
                  return mt_error(uptr, MTSE_UNATT, dptr);      /* attached? */
            if ((uptr->u3 & DENS_MSK) != DENS_800)
                  return mt_error(uptr, MTSE_FMT, dptr);        /* attached? */
        }       
        if (BUF_EMPTY(uptr)) {
            if (dptr->flags & MTDF_STOP) {
                return mt_error(uptr, MTSE_OK, dptr);
            }
            status &= ~(BOT_FLAG|EOF_FLAG|EOT_FLAG);
            if ((r = sim_tape_rdrecf(uptr, &mt_buffer[0], &reclen, 
                                BUFFSIZE)) != MTSE_OK) {
                sim_debug(DEBUG_DETAIL, dptr, "MT%o read error %d\n", unit, r);
                return mt_error(uptr, r, dptr);
            }
            sim_debug(DEBUG_DETAIL, dptr, "MT%o read %d\n", unit, reclen);
            uptr->hwmark = reclen;
            uptr->u6 = 0;
            uptr->u5 = 0;
        }
        if (uptr->u6 < uptr->hwmark) {
            int cc = (8 * (3 - uptr->u5)) + 4;
            uint8 ch;
            if ((cmd & 07) == CMP && uptr->u5 == 0) {
                buf_reg = 0;
                if (!mt_df10_read(dptr, uptr)) {
                    dptr->flags |= MTDF_STOP;
                    return mt_error(uptr, MTSE_INVRL, dptr);
                }
            }
            if ((cmd & 07) == CMP) {
                if (cc < 0) 
                     ch = buf_reg & 0x3f;
                else
                     ch = (buf_reg >> cc) & 0xff;
                if (ch != mt_buffer[uptr->u6]) {
                    status |= READ_CMP;
                    mt_error(uptr, MTSE_OK, dptr);
                }
            } else {
                ch = mt_buffer[uptr->u6];
                if (cc < 0) 
                    buf_reg |=  (uint64)(ch & 0x3f);
                else
                    buf_reg |= (uint64)(ch & 0xff) << cc;
            }
            uptr->u6++;
            uptr->u5++;
            status &= ~CHAR_COUNT;
            status |= (uint64)(uptr->u5) << 18;
            if (uptr->u5 == (4 + ((uptr->u3 & CORE_DUMP) != 0))) {
                uptr->u5 = 0;
                if ((cmd & 7)  != CMP) {
                    hold_reg = buf_reg;
                    buf_reg = 0;
                    mt_df10_write(dptr, uptr);
                }
                if (dptr->flags & MTDF_STOP) {
                    return mt_error(uptr, MTSE_INVRL, dptr);
                }
            }
          } else {
                if ((cmd & 010) == 0) {
                    dptr->flags &= ~MTDF_MOTION;
                    if (status & NEXT_UNIT) 
                        set_interrupt(MT_DEVNUM+4, (uptr->u3 & FLAG_PIA) >> 3);
                    return mt_error(uptr, MTSE_OK, dptr);
                } else {
                    CLR_BUF(uptr);
                }
          }
          break;
    case WRITE:
    case WRITE_LONG:
         if (BUF_EMPTY(uptr)) {
             if ((uptr->u3 & DENS_MSK) != DENS_800) 
                    return mt_error(uptr, MTSE_FMT, dptr);
             if ((uptr->flags & UNIT_ATT) == 0)
                   return mt_error(uptr, MTSE_UNATT, dptr);     /* attached? */
             if ((uptr->u3 & DENS_MSK) != DENS_800)
                   return mt_error(uptr, MTSE_FMT, dptr);       /* attached? */
         }      
         if (BUF_EMPTY(uptr) && (dptr->flags & MTDF_TYPEB) == 0) {
            sim_debug(DEBUG_EXP, dptr, "MT%o Init write\n", unit);
            status |= DATA_REQUEST;
            dptr->flags &= ~(MTDF_BUFFUL);
            uptr->hwmark = 0;
            uptr->u5 = 0;
            uptr->u6 = 0;
            set_interrupt(MT_DEVNUM, uptr->u3 & DATA_PIA);
            break;
         }
         if (uptr->u5 != 0) {
                /* Write next char out */
                int cc = (8 * (3 - uptr->u5)) + 4;
                uint8 ch;
                if (cc < 0) 
                     ch = buf_reg & 0x3f;
                else
                     ch = (buf_reg >> cc) & 0xff;
                mt_buffer[uptr->u6] = ch;
                uptr->u6++;
                uptr->hwmark = uptr->u6;
                uptr->u5++;
                if (uptr->u5 == (4 + ((uptr->u3 & CORE_DUMP) != 0))) 
                    uptr->u5 = 0;
                status &= ~CHAR_COUNT;
                status |= (uint64)(uptr->u5) << 18;
         } else if (dptr->flags & MTDF_STOP && (dptr->flags & MTDF_BUFFUL) == 0) {
                /* Write out the block */
                reclen = uptr->hwmark;
                status &= ~(BOT_FLAG|EOF_FLAG|EOT_FLAG);
                r = sim_tape_wrrecf(uptr, &mt_buffer[0], reclen);
                sim_debug(DEBUG_DETAIL, dptr, "MT%o Write %d\n", unit, reclen);
                uptr->u6 = 0;
                uptr->hwmark = 0;
                dptr->flags &= ~MTDF_MOTION;
                return mt_error(uptr, r, dptr); /* Record errors */
         } else {
                /* Write out first character. */
                if (mt_df10_read(dptr, uptr)) {
                    uint8 ch;
                    ch = (buf_reg >> 28) & 0xff;
                    mt_buffer[uptr->u6] = ch;
                    uptr->u6++;
                    uptr->hwmark = uptr->u6;
                    uptr->u5++;
                    status &= ~CHAR_COUNT;
                    status |= (uint64)(uptr->u5) << 18;
                } else {
                    dptr->flags |= MTDF_STOP;
                }
         }
         break;
    case WTM:
        if ((uptr->u3 & DENS_MSK) != DENS_800) 
            return mt_error(uptr, MTSE_FMT, dptr);
        if ((uptr->flags & UNIT_ATT) == 0)
              return mt_error(uptr, MTSE_UNATT, dptr);  /* attached? */
        if ((uptr->u3 & DENS_MSK) != DENS_800)
              return mt_error(uptr, MTSE_FMT, dptr);    /* attached? */
        dptr->flags &= ~MTDF_MOTION;
        status &= ~(BOT_FLAG|EOT_FLAG);
        sim_debug(DEBUG_DETAIL, dptr, "MT%o WTM\n", unit);
        return mt_error(uptr, sim_tape_wrtmk(uptr), dptr);
    case ERG:
        if ((uptr->u3 & DENS_MSK) != DENS_800) 
            return mt_error(uptr, MTSE_FMT, dptr);
        if ((uptr->flags & UNIT_ATT) == 0)
              return mt_error(uptr, MTSE_UNATT, dptr);  /* attached? */
        if ((uptr->u3 & DENS_MSK) != DENS_800)
              return mt_error(uptr, MTSE_FMT, dptr);    /* attached? */
        dptr->flags &= ~MTDF_MOTION;
        status &= ~(BOT_FLAG|EOT_FLAG);
        sim_debug(DEBUG_DETAIL, dptr, "MT%o ERG\n", unit);
        return mt_error(uptr, sim_tape_wrgap(uptr, 35), dptr);
    case SPC_REV_EOF:
    case SPC_EOF:
    case SPC_REV:
    case SPC_FWD:
        if (BUF_EMPTY(uptr)) {
            if ((uptr->u3 & DENS_MSK) != DENS_800) 
                return mt_error(uptr, MTSE_FMT, dptr);
            if ((uptr->flags & UNIT_ATT) == 0)
                  return mt_error(uptr, MTSE_UNATT, dptr);      /* attached? */
            if ((uptr->u3 & DENS_MSK) != DENS_800)
                  return mt_error(uptr, MTSE_FMT, dptr);        /* attached? */
        }       
        sim_debug(DEBUG_DETAIL, dptr, "MT%o space %o\n", unit, cmd);
        /* Clear tape mark, command, idle since we will need to change dir */
        if ((cmd & 010) == 0) {
            if (BUF_EMPTY(uptr)) {
                mt_df10_read(dptr, uptr);
                status &= ~DATA_LATE;
                uptr->hwmark = 0;
            } else {
                if (!mt_df10_read(dptr, uptr)) {
                    status &= ~DATA_LATE;
                    dptr->flags |= MTDF_MOTION;
                    return mt_error(uptr, MTSE_OK, dptr);
                }
            }
        }
        status &= ~(BOT_FLAG|EOT_FLAG);
        if ((cmd & 7) == SPC_FWD)
            r = sim_tape_sprecf(uptr, &reclen);
        else 
            r = sim_tape_sprecr(uptr, &reclen);
        /* Always skip if first record is mark */
        if ((cmd & 010) != 0 && r == MTSE_TMK && BUF_EMPTY(uptr)) {
            r = MTSE_OK;
        }
        if (cmd & 010) {
            uptr->hwmark = 0;
        }

        if (r != MTSE_OK) {
            dptr->flags &= ~MTDF_MOTION;
            return mt_error(uptr, r, dptr);
        }
        sim_activate(uptr, 5000);
        return SCPE_OK;
    }
    sim_activate(uptr, 50);
    return SCPE_OK;
}

void mt_read_word(UNIT *uptr) {
     int i, cc, ch;
     uint64 buf;
   
     buf_reg = 0;
     for(i = 0; i <= 4; i++) {
        cc = (8 * (3 - i)) + 4;
        ch = mt_buffer[uptr->u6];
        if (cc < 0) 
            buf_reg |=  (uint64)(ch & 0x3f);
        else
            buf_reg |= (uint64)(ch & 0xff) << cc;
        uptr->u6++;
     }  
}

/* Boot from given device */
t_stat
mt_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    t_mtrlnt            reclen;
    t_stat              r;
    int                 wc, addr;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    r = sim_tape_rewind(uptr);
    if (r != SCPE_OK)
        return r;
    r = sim_tape_rdrecf(uptr, &mt_buffer[0], &reclen, BUFFSIZE);
    if (r != SCPE_OK)
        return r;
    uptr->u6 = 0;
    uptr->hwmark = reclen;

    mt_read_word(uptr);
    wc = (buf_reg >> 18) & RMASK;
    addr = buf_reg & RMASK;
    while (wc != 0) {
        wc = (wc + 1) & RMASK;
        addr = (addr + 1) & RMASK;
        if (uptr->u6 >= reclen) {
            r = sim_tape_rdrecf(uptr, &mt_buffer[0], &reclen, BUFFSIZE);
            if (r != SCPE_OK)
                return r;
            uptr->u6 = 0;
        }
        mt_read_word(uptr);
        M[addr] = buf_reg;
    }
    PC = addr;
    return SCPE_OK;
}

t_stat set_mta (UNIT *uptr, int32 val, CONST char *cptr, void *desc) 
{
    DEVICE *dptr;
    dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
       return SCPE_IERR;
    dptr->flags &= ~MTDF_TYPEB;
    if (*cptr == 'B') 
        dptr->flags |= val;
    else if (*cptr != 'A') 
        return SCPE_ARG;
    return SCPE_OK;
}

t_stat show_mta (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
   DEVICE *dptr;

   if (uptr == NULL)
      return SCPE_IERR;

   dptr = find_dev_from_unit(uptr);
   if (dptr == NULL)
      return SCPE_IERR;
   if (dptr->flags & val) {
      fprintf (st, "MT10B");
   } else {
      fprintf (st, "MT10A");
   }
   return SCPE_OK;
}


t_stat
mt_reset(DEVICE * dptr)
{
    int i;
    for (i = 0 ; i < 8; i++) {
        UNIT    *uptr = &mt_unit[i];

        if (MT_DENS(uptr->dynflags) == MT_DENS_NONE)
                uptr->dynflags = MT_200_VALID | MT_556_VALID | 
                        MT_800_VALID | (MT_DENS_800 << UNIT_V_DF_TAPE);
    }
    return SCPE_OK;
}

t_stat
mt_attach(UNIT * uptr, CONST char *file)
{
    return sim_tape_attach(uptr, file);
}

t_stat
mt_detach(UNIT * uptr)
{
    uptr->u5 = 0;
    return sim_tape_detach(uptr);
}

t_stat mt_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "MT10 Magnetic Tape\n\n");
fprintf (st, "The MT10 tape controller can be set to either type A or B\n");
fprintf (st, "The A model lacks a DF10, so all I/O must be polled mode. To set the\n");
fprintf (st, "tape controller to a B model with DF10 do:\n\n");
fprintf (st, "    sim> SET %s TYPE=B \n", dptr->name);
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.  The\n");
fprintf (st, "bad block option can be used only when a unit is attached to a file.\n");
fprintf (st, "The MT10 does support the BOOT command.\n");
sim_tape_attach_help (st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

const char *mt_description (DEVICE *dptr)
{
return "MT10 magnetic tape controller" ;
}

#endif
