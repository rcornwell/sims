/* kx10_ddc.c: Drum RES-10 Disk Controller.

   Copyright (c) 2013-2020, Richard Cornwell

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MEDDCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "kx10_defs.h"

#ifndef NUM_DEVS_DDC
#define NUM_DEVS_DDC 0
#endif

#if (NUM_DEVS_DDC > 0)

#define DDC_DEVNUM       0440                    /* 0174 */
#define NUM_UNITS_DDC    4

/* Flags in the unit flags word */

#define UNIT_V_DTYPE    (UNIT_V_UF + 0)                 /* disk type */
#define UNIT_M_DTYPE    1
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)

/* Parameters in the unit descriptor */

#define CUR_CYL         u3              /* current cylinder */
#define DATAPTR         u4              /* data pointer */
#define UFLAGS          u5              /* Function */


/* CONI bits */
#define QUEUE_PAR       0400000000000LL
#define DDC_BSY         0000001000000LL
#define DDC_DON         0000000400000LL
#define DDC_CSE         0000000001000LL
#define DDC_QF          0000000000400LL
#define DDC_RDY         0000000000200LL    /* Drum Ready */
#define DDC_SPA         0000000000100LL    /* Drum Silo Parity Error */
#define DDC_NXM         0000000000040LL    /* NXM */
#define DDC_EXC         0000000000020LL    /* Exceed Capacity */
#define DDC_HUD         0000000000010LL    /* Drum Hung */
#define DDC_MPE         0000000000004LL    /* MPE */
#define DDC_OVR         0000000000002LL    /* Data overrun */
#define DDC_CKR         0000000000001LL    /* Checksum error */
                      
/* CONO bits */       
#define DDC_RST         0000003000000LL    /* Drum Reset */
#define DDC_CLR         0000000400000LL    /* Clear Int */
#define DDC_ERR         0000000200000LL    /* Clear Errors */
#define DDC_EXF         0000000100000LL    /* Execute FR */
#define DDC_EXQ         0000000040000LL    /* Execute Queue */
                      
/* Command words */   
#define DDC_CMD         0700000000000LL    /* Drum command */
#define DDC_SEQ         0076000000000LL    /* Sequence number */
#define DDC_PIA         0001600000000LL    /* PIA */
#define DDC_FUNC        0000140000000LL    /* Function */
#define DDC_DISK        0000003000000LL    /* Logical Disc */
#define DDC_TRK         0000000377600LL    /* Track */
#define DDC_SEC         0000000000177LL    /* Sector */
                      
/* Drum Status */     
#define DDC_SECCNT      0000177000000LL    /* Sequence counter */
#define DDC_STATUS      0000000760000LL

/* DataI */
/* 177 Sector number */




#define DDC_SIZE        100000
#if 0
#define RD10_DTYPE      0
#define RD10_WDS        32
#define RD10_SEGS       80
#define RD10_CYL        200
#define RD10_SIZE       (RD10_SEGS * RD10_CYL * RD10_WDS)

#define RM10_DTYPE      1
#define RM10_WDS        64
#define RM10_SEGS       60
#define RM10_CYL        90
#define RM10_SIZE       (RM10_SEGS * RM10_CYL * RM10_WDS)

struct drvtyp {
    int32       wd_seg;                                 /* Number of words per segment */
    int32       seg;                                    /* segments */
    int32       cyl;                                    /* cylinders */
    int32       size;                                   /* #blocks */
    int32       devtype;                                /* device type */
    };

struct drvtyp ddc_drv_tab[] = {
    { RD10_WDS, RD10_SEGS, RD10_CYL, RD10_SIZE, RD10_DTYPE},
    { RM10_WDS, RM10_SEGS, RM10_CYL, RM10_SIZE, RM10_DTYPE},
    { 0 }
    };

struct  df10    ddc_df10[NUM_DEVS_DDC];
uint64          ddc_buf[NUM_DEVS_DDC][RM10_WDS];
uint32          ddc_ipr[NUM_DEVS_DDC];
#endif

t_stat          ddc_devio(uint32 dev, uint64 *data);
t_stat          ddc_svc(UNIT *);
void            ddc_ini(UNIT *, t_bool);
t_stat          ddc_reset(DEVICE *);
t_stat          ddc_attach(UNIT *, CONST char *);
t_stat          ddc_detach(UNIT *);
t_stat          ddc_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat          ddc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                     const char *cptr);
const char      *ddc_description (DEVICE *dptr);


UNIT                ddc_unit[] = {
/* Controller 1 */
    { UDATA (&ddc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, DDC_SIZE) },
    { UDATA (&ddc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, DDC_SIZE) },
    { UDATA (&ddc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, DDC_SIZE) },
    { UDATA (&ddc_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE, DDC_SIZE) },

};

DIB ddc_dib[] = {
    {DDC_DEVNUM+000, 1, &ddc_devio, NULL},
    };

MTAB                ddc_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock drive" },
//    {UNIT_DTYPE, (RD10_DTYPE << UNIT_V_DTYPE), "RD10", "RD10", &ddc_set_type },
 //   {UNIT_DTYPE, (RM10_DTYPE << UNIT_V_DTYPE), "RM10", "RM10", &ddc_set_type },
    {0}
};

REG                 ddc_reg[] = {
#if 0
    {BRDATA(BUFF, ddc_buf[0], 16, 64, RM10_WDS), REG_HRO},
    {ORDATA(IPR, ddc_ipr[0], 2), REG_HRO},
    {ORDATA(STATUS, ddc_df10[0].status, 18), REG_RO},
    {ORDATA(CIA, ddc_df10[0].cia, 18)},
    {ORDATA(CCW, ddc_df10[0].ccw, 18)},
    {ORDATA(WCR, ddc_df10[0].wcr, 18)},
    {ORDATA(CDA, ddc_df10[0].cda, 18)},
    {ORDATA(DEVNUM, ddc_df10[0].devnum, 9), REG_HRO},
    {ORDATA(BUF, ddc_df10[0].buf, 36), REG_HRO},
    {ORDATA(NXM, ddc_df10[0].nxmerr, 8), REG_HRO},
    {ORDATA(COMP, ddc_df10[0].ccw_comp, 8), REG_HRO},
#endif
    {0}
};

DEVICE              ddc_dev = {
    "DDC", ddc_unit, ddc_reg, ddc_mod,
    NUM_UNITS_DDC, 8, 18, 1, 8, 36,
    NULL, NULL, &ddc_reset, NULL, &ddc_attach, &ddc_detach,
    &ddc_dib[0], DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &ddc_help, NULL, NULL, &ddc_description
};


t_stat ddc_devio(uint32 dev, uint64 *data) {
//     int          ctlr = (dev - DDC_DEVNUM) >> 2;
//     struct df10 *df10;
     UNIT        *uptr;
     DEVICE      *dptr;
     int          unit;
     int          tmp;
     int          drv;
     int          cyl;
     int          dtype;

//     if (ctlr < 0 || ctlr >= NUM_DEVS_DDC)
 //       return SCPE_OK;

  //   df10 = &ddc_df10[ctlr];
     dptr = &ddc_dev;
     switch(dev & 3) {
     case CONI:
        sim_debug(DEBUG_CONI, dptr, "DDC %03o CONI %06o PC=%o\n", dev,
                          (uint32)*data, PC);
        break;
     case CONO:
#if 0
         if (*data & PI_ENABLE)
             df10->status &= ~(PI_ENABLE);
         clr_interrupt(dev);
         df10->status &= ~07;
         df10->status |= *data & 07;
         df10->status &= ~(RST_MSK & *data);
         if ((*data & BUSY) != 0) {
             unit = ddc_ipr[ctlr] & 3;
             drv = unit + (ctlr * NUM_UNITS_DDC);
             uptr = &ddc_unit[drv];
             if ((df10->status & BUSY) != 0) {
                  sim_cancel(uptr);
                  df10_finish_op(df10, 0);
             } else {
                  df10->status &= ~BUSY;
                  df10_setirq(df10);
             }
         }
         ddc_ipr[ctlr] &= ~SEC_SCTR;
         ddc_ipr[ctlr] |= *data & SEC_SCTR;

         if ((df10->status & BUSY) != 0 && (*data & CCW_COMP) != 0) {
            df10_writecw(df10);
         } else
            df10->status &= ~CCW_COMP;
#endif
         sim_debug(DEBUG_CONO, dptr, "DDC %03o CONO %06o PC=%o\n", dev,
                   (uint32)*data, PC);
         break;
     case DATAI:
#if 0
         *data = ddc_ipr[ctlr];
         unit = (ddc_ipr[ctlr] & SEC_SCTR) >> 16;
         uptr = &ddc_unit[(ctlr * NUM_UNITS_DDC) + unit];
         *data |= (uptr->UFLAGS >> 3) & 0177;
#endif
         sim_debug(DEBUG_DATAIO, dptr, "DDC %03o DATI %012llo PC=%o\n",
                  dev, *data, PC);
         break;
     case DATAO:
         sim_debug(DEBUG_DATAIO, dptr, "DDC %03o DATO %012llo, PC=%o\n",
                  dev, *data, PC);
#if 0
         if (df10->status & BUSY) {
            return SCPE_OK;
         }
         df10->status &= ~(PI_ENABLE|S_ERROR);
         clr_interrupt(DDC_DEVNUM + (ctlr * 4));
         ddc_ipr[ctlr] &= ~(INIT_PAR|3);
         ddc_ipr[ctlr] |= *data & INIT_PAR;
         unit = (*data >> 34) & 03;
         ddc_ipr[ctlr] |= unit;
         drv = unit + (ctlr * NUM_UNITS_DDC);
         uptr = &ddc_unit[drv];
         if ((uptr->flags & UNIT_ATT) == 0) {
            df10->status &= ~BUSY;
            df10->status |= NOT_RDY;
            df10_setirq(df10);
            return SCPE_OK;
         }
         if ((uptr->flags & UNIT_WPRT) && *data & WRITE) {
            df10->status &= ~BUSY;
            df10->status |= ILL_WR;
            df10_setirq(df10);
            return SCPE_OK;
         }
         df10_setup(df10, (uint32)*data);
         tmp = (uint32)(*data >> 15) & ~07;
         cyl = (tmp >> 10) & 0777;
         if (((cyl & 017) > 9) || (((cyl >> 4) & 017) > 9)) {
              sim_debug(DEBUG_DETAIL, dptr, "HK %d non-bcd cyl %02x\n",
                        ctlr, cyl);
              df10_finish_op(df10, TRK_SEL_E);
              return SCPE_OK;
         }
         cyl = (((cyl >> 4) & 017) * 10) + (cyl & 017) +
                ((cyl & 0x100) ? 100 : 0);
         dtype = GET_DTYPE(uptr->flags);
         if (cyl >= ddc_drv_tab[dtype].cyl) {
              sim_debug(DEBUG_DETAIL, dptr, "HK %d invalid cyl %d %d\n",
                       ctlr, cyl, ddc_drv_tab[dtype].cyl);
              df10_finish_op(df10, TRK_SEL_E);
              return SCPE_OK;
         }
         cyl = (tmp >> 3) & 0177;
         if ((cyl & 017) > 9) {
              sim_debug(DEBUG_DETAIL, dptr, "HK %d non-bcd seg %02x\n",
                       ctlr, cyl);
              df10_finish_op(df10, TRK_SEL_E);
              return SCPE_OK;
         }
         uptr->UFLAGS =  tmp | ((*data & WRITE) != 0) | (ctlr << 1);
         uptr->DATAPTR = -1;    /* Set no data */
         if ((*data & WRITE) != 0)
            (void)df10_read(df10);
         sim_debug(DEBUG_DETAIL, dptr, "HK %d cyl %o\n", ctlr, uptr->UFLAGS);
         sim_activate(uptr, 100);
#endif
        break;
    }
    return SCPE_OK;
}


t_stat ddc_svc (UNIT *uptr)
{
#if 0
   int           dtype = GET_DTYPE(uptr->flags);
   int           ctlr  = (uptr->UFLAGS >> 1) & 03;
   int           seg   = (uptr->UFLAGS >> 3) & 0177;
   int           cyl   = (uptr->UFLAGS >> 10) & 0777;
   int           wr    = (uptr->UFLAGS & 1);
   int           seg_size = ddc_drv_tab[dtype].wd_seg;
   struct df10  *df10  = &ddc_df10[ctlr];
   int           tmp, wc;
   DEVICE       *dptr;
   t_stat        err, r;

   dptr = ddc_devs[ctlr];
   /* Check if we need to seek */
   if (uptr->DATAPTR == -1) {
        cyl = (((cyl >> 4) & 017) * 10) + (cyl & 017) +
                ((cyl & 0x100) ? 100 : 0);
        if (cyl >= ddc_drv_tab[dtype].cyl) {
              sim_debug(DEBUG_DETAIL, dptr, "HK %d invalid cyl %d %d %o\n",
                       ctlr, cyl, ddc_drv_tab[dtype].cyl, uptr->UFLAGS);
              df10_finish_op(df10, TRK_SEL_E);
              return SCPE_OK;
        }
        /* Convert segment from BCD to binary */
        if ((seg & 017) > 10) {
              sim_debug(DEBUG_DETAIL, dptr, "HK %d non-bcd seg %02x %d %o\n",
                       ctlr, seg, ddc_drv_tab[dtype].seg, uptr->UFLAGS);
              df10_finish_op(df10, S_ERROR);
              return SCPE_OK;
        }
        seg = (((seg >> 4) & 07) * 10) + (seg & 017);
        if (seg >= ddc_drv_tab[dtype].seg) {
              sim_debug(DEBUG_DETAIL, dptr, "HK %d invalid sec %d %d %o\n",
                       ctlr, seg, ddc_drv_tab[dtype].seg, uptr->UFLAGS);
              df10_finish_op(df10, S_ERROR);
              return SCPE_OK;
        }
        /* Check if reading */
        if (!wr) {
                 /* Read the block */
           int da;
           da = ((cyl * ddc_drv_tab[dtype].seg) + seg) * seg_size;
           err = sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
           wc = sim_fread (&ddc_buf[ctlr][0], sizeof(uint64),
                        seg_size, uptr->fileref);
           sim_debug(DEBUG_DETAIL, dptr, "HK %d Read %d %d %d %x\n",
                ctlr, da, cyl, seg, uptr->UFLAGS << 1 );
           for (; wc < seg_size; wc++)
                ddc_buf[ctlr][wc] = 0;
         }
         uptr->DATAPTR = 0;
         df10->status |= SCDDCHCMP;
    }
    if (wr) {
        ddc_buf[ctlr][uptr->DATAPTR] = df10->buf;
        r = df10_read(df10);
    } else {
        df10->buf = ddc_buf[ctlr][uptr->DATAPTR];
        r = df10_write(df10);
    }
    sim_debug(DEBUG_DATA, dptr, "Xfer %d %012llo %06o %06o\n", uptr->DATAPTR, df10->buf,
             df10->wcr, df10->cda);

    uptr->DATAPTR++;
    if (uptr->DATAPTR >= seg_size || r == 0 ) {
        /* Check if writing */
        df10->status &= ~SCDDCHCMP;
        seg = (((seg >> 4) & 017) * 10) + (seg & 017);
        cyl = (((cyl >> 4) & 017) * 10) + (cyl & 017) +
                ((cyl & 0x100) ? 100 : 0);
        if (wr) {
             int da;

             while(uptr->DATAPTR < seg_size) {
                 ddc_buf[ctlr][uptr->DATAPTR] = 0;
                 uptr->DATAPTR++;
             }
             da = ((cyl * ddc_drv_tab[dtype].seg) + seg) * seg_size;
             sim_debug(DEBUG_DETAIL, dptr, "HK %d Write %d %d %d %x %d\n",
                  ctlr, da, cyl, seg, uptr->UFLAGS << 1, uptr->DATAPTR );
             err = sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
             wc = sim_fwrite(&ddc_buf[ctlr][0],sizeof(uint64),
                        seg_size, uptr->fileref);
        }
        uptr->DATAPTR = -1;
        seg++;
        if (seg >= ddc_drv_tab[dtype].seg) {
           seg = 0;
           cyl++;
           if (cyl >= ddc_drv_tab[dtype].cyl)
              cyl = 0;
        }
        /* Convert seg back to bcd */
        tmp = seg % 10;
        seg /= 10;
        seg <<= 4;
        seg += tmp;
        wr = 0;
        if (cyl >= 100) {
            wr = 0x100;
            cyl -= 100;
        }
        tmp = (cyl % 10);
        cyl /= 10;
        cyl <<= 4;
        cyl += wr + tmp;
        uptr->UFLAGS = (uptr->UFLAGS & 7) + (seg << 3) + (cyl << 10);
    }
    if ((df10->status & PI_ENABLE) == 0) {
        sim_activate(uptr, 20);
    }
#endif
    return SCPE_OK;
}


t_stat
ddc_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
#if 0
    int         i;
    if (uptr == NULL) return SCPE_IERR;
    for (i = 0; ddc_drv_tab[i].wd_seg != 0; i++) {
        if ((val >> UNIT_V_DTYPE) == ddc_drv_tab[i].devtype) {
            uptr->capac = ddc_drv_tab[i].size;
            return SCPE_OK;
        }
    }
#endif
    return SCPE_IERR;
}


t_stat
ddc_reset(DEVICE * dptr)
{
    int unit;
    int ctlr;
#if 0
    UNIT *uptr = dptr->units;
    for(unit = 0; unit < NUM_UNITS_DDC; unit++) {
         uptr->UFLAGS  = 0;
         uptr->CUR_CYL = 0;
         uptr++;
    }
    for (ctlr = 0; ctlr < NUM_DEVS_DDC; ctlr++) {
        ddc_ipr[ctlr] = 0;
        ddc_df10[ctlr].status = 0;
        ddc_df10[ctlr].devnum = ddc_dib[ctlr].dev_num;
        ddc_df10[ctlr].nxmerr = 8;
        ddc_df10[ctlr].ccw_comp = 5;
    }
#endif
    return SCPE_OK;
}

/* Device attach */

t_stat ddc_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

//uptr->capac = ddc_drv_tab[GET_DTYPE (uptr->flags)].size;
r = attach_unit (uptr, cptr);
if (r != SCPE_OK || (sim_switches & SIM_SW_REST) != 0)
    return r;
//uptr->CUR_CYL = 0;
//uptr->UFLAGS = 0;
return SCPE_OK;
}

/* Device detach */

t_stat ddc_detach (UNIT *uptr)
{
if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
if (sim_is_active (uptr))                              /* unit active? */
    sim_cancel (uptr);                                  /* cancel operation */
return detach_unit (uptr);
}

t_stat ddc_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "RES-10  Drum  Drives (DDC)\n\n");
fprintf (st, "The DDC controller implements the RES-10 disk controller that talked\n");
fprintf (st, "to drum drives.\n");
fprintf (st, "Options include the ability to set units write enabled or write locked, to\n");
fprintf (st, "set the drive type to one of two disk types\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *ddc_description (DEVICE *dptr)
{
return "RES-10 disk controller";
}

#endif
