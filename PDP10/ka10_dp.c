/* ka10_dp.c: Dec Data Products Disk Drive.

   Copyright (c) 2013, Richard Cornwell

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "ka10_defs.h"

#ifndef NUM_DEVS_DP
#define NUM_DEVS_DP 0
#endif

#if (NUM_DEVS_DP > 0)

#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

#define RP_NUMWD        128     /* 36bit words/sec */
#define DP_DEVNUM       0250    /* First device number */
#define NUM_UNITS_DP    8

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    3
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write protect */

/* Parameters in the unit descriptor */

#define CUR_CYL         u3              /* current cylinder */
#define DATAPTR         u4              /* data pointer */
#define UFLAGS          u5              /* Function */
#define SEEK_DONE       0100            /* Seek finished */
#define SEEK_STATE      0200            /* Seek in progress */
#define DIRTY           0400            /* Buffer needs writing */
#define CMD_MASK        070
#define CONTROL         007


/* CONI/CONO Flags */
#define SUF_ERR         0000000000100   
#define SEC_ERR         0000000000200
#define ILL_CMD         0000000000400
#define ILL_WR          0000000001000
#define NOT_RDY         0000000002000   /* Clear CXR */
#define PRT_ERR         0000000004000   /* 14-17 Clear CCPE, DSPE, DISK WDPE, CDPE */
#define NXM_ERR         0000000010000   
#define SLW_CHN         0000000020000
#define SRC_ERR         0000000040000
#define PWR_FAIL        0000000100000
#define END_CYL         0000000200000   /* No effect */
#define SRC_DONE        0000000400000   /* No effect */
#define DSK_PRTY        0000001000000   /* No effect */
#define CHN_PRTY        0000002000000   /* No effect */
#define SEC_PRTY        0000004000000   /* No effect */
#define CCW_PRTY        0000010000000   /* No effect */
#define B22_FLAG        0040000000000

#define CLRMSK          0000177710


#define DWPE_STOP       0000000001000
#define SPARE           0000000002000
#define DSPE_STOP       0000000004000
#define SECTOR          0000000170000
#define CYL256          0000000200000
#define SURFACE         0000017400000
#define CYL             0007760000000
#define DRIVE           0070000000000
#define OP              0700000000000

#define RD      0
#define WR      1
#define RV      2
#define WH      3
#define SK      4
#define CL      5
#define NO      6
#define RC      7

#define ATTN            0000000000776
#define DEFECT          0000000001000
#define SEL_RP03        0000000002000
#define SEL_CYL256      0000000004000
#define SEL_SPARE       0000000010000
#define SEL_SEC         0000000760000
#define WR_HD_LK        0000001000000
#define RD_ONLY         0000002000000
#define NO_DRIVE        0000004000000
#define FILE_UNSAFE     0000010000000
#define DRV_ONLINE      0000020000000
#define ON_CYL          0000040000000
#define SEEK_INC        0000100000000
#define SEL_CYL         0077600000000
#define SEL_DRIVE       0700000000000

#define RP01_DTYPE      0
#define RP01_SECT       5
#define RP01_SURF       10
#define RP01_CYL        203
#define RP01_DEV        0
#define RP01_SIZE       (RP01_SECT * RP01_SURF * RP01_CYL * RP_NUMWD)

#define RP02_DTYPE      1
#define RP02_SECT       10
#define RP02_SURF       20
#define RP02_CYL        203
#define RP02_DEV        0
#define RP02_SIZE       (RP02_SECT * RP02_SURF * RP02_CYL * RP_NUMWD)

#define RP03_DTYPE      2
#define RP03_SECT       10
#define RP03_SURF       20
#define RP03_CYL        406
#define RP03_DEV        1
#define RP03_SIZE       (RP03_SECT * RP03_SURF * RP03_CYL * RP_NUMWD)

struct drvtyp {
    int32       sect;                                   /* sectors */
    int32       surf;                                   /* surfaces */
    int32       cyl;                                    /* cylinders */
    int32       size;                                   /* #blocks */
    int32       devtype;                                /* device type */
    };

struct drvtyp dp_drv_tab[] = {
    { RP01_SECT, RP01_SURF, RP01_CYL, RP01_SIZE, RP01_DTYPE},
    { RP02_SECT, RP02_SURF, RP02_CYL, RP02_SIZE, RP02_DTYPE},
    { RP03_SECT, RP03_SURF, RP03_CYL, RP03_SIZE, RP03_DTYPE},
    { 0 }
    };


struct df10   dp_df10[NUM_DEVS_DP];
uint32        dp_cur_unit[NUM_DEVS_DP];
uint64        dp_buf[NUM_DEVS_DP][RP_NUMWD];
int           readin_flag = 0;

t_stat        dp_devio(uint32 dev, uint64 *data);
t_stat        dp_svc(UNIT *);
t_stat        dp_boot(int32, DEVICE *);
void          dp_ini(UNIT *, t_bool);
t_stat        dp_reset(DEVICE *);
t_stat        dp_attach(UNIT *, char *);
t_stat        dp_detach(UNIT *);
t_stat        dp_set_type(UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat        dp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, 
                 const char *cptr);
const char    *dp_description (DEVICE *dptr);

UNIT                dp_unit[] = {
/* Controller 1 */
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },

#if (NUM_DEVS_DP > 1)
/* Controller 2 */
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
#if (NUM_DEVS_DP > 2)
/* Controller 3 */
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
#if (NUM_DEVS_DP > 3)
/* Controller 4 */
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
    { UDATA (&dp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP03_DTYPE << UNIT_V_DTYPE), RP03_SIZE) },
#endif
#endif
#endif
};

DIB dp_dib[] = {
    {DP_DEVNUM+000, 1, &dp_devio},
    {DP_DEVNUM+004, 1, &dp_devio},
    {DP_DEVNUM+010, 1, &dp_devio},
    {DP_DEVNUM+014, 1, &dp_devio}};

MTAB                dp_mod[] = {
    {UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL},
    {UNIT_DTYPE, (RP03_DTYPE << UNIT_V_DTYPE), "RP03", "RP03", &dp_set_type },
    {UNIT_DTYPE, (RP02_DTYPE << UNIT_V_DTYPE), "RP02", "RP02", &dp_set_type },
    {UNIT_DTYPE, (RP01_DTYPE << UNIT_V_DTYPE), "RP01", "RP01", &dp_set_type },

    {0}
};

DEVICE              dpa_dev = {
    "DPA", dp_unit, NULL, dp_mod,
    NUM_UNITS_DP, 8, 18, 1, 8, 36,
    NULL, NULL, &dp_reset, &dp_boot, &dp_attach, &dp_detach,
    &dp_dib[0], 0, 0,
    NULL, NULL, NULL, &dp_help, NULL, NULL, &dp_description
};

#if (NUM_DEVS_DP > 1)
DEVICE              dpb_dev = {
    "DPB", &dp_unit[010], NULL, dp_mod,
    NUM_UNITS_DP, 8, 18, 1, 8, 36,
    NULL, NULL, &dp_reset, &dp_boot, &dp_attach, &dp_detach,
    &dp_dib[1], 0, 0,
    NULL, NULL, NULL, &dp_help, NULL, NULL, &dp_description
};

#if (NUM_DEVS_DP > 2)
DEVICE              dpc_dev = {
    "DPC", &dp_unit[020], NULL, dp_mod,
    NUM_UNITS_DP, 8, 18, 1, 8, 36,
    NULL, NULL, &dp_reset, &dp_boot, &dp_attach, &dp_detach,
    &dp_dib[2], 0, 0,
    NULL, NULL, NULL, &dp_help, NULL, NULL, &dp_description
};

#if (NUM_DEVS_DP > 3)
DEVICE              dpd_dev = {
    "DPD", &dp_unit[030], NULL, dp_mod,
    NUM_UNITS_DP, 8, 18, 1, 8, 36,
    NULL, NULL, &dp_reset, &dp_boot, &dp_attach, &dp_detach,
    &dp_dib[3], 0, 0,
    NULL, NULL, NULL, &dp_help, NULL, NULL, &dp_description
};

#endif
#endif
#endif


t_stat dp_devio(uint32 dev, uint64 *data) {
     uint64         res;
     int            ctlr = (dev - DP_DEVNUM) >> 2;
     struct df10   *df10 = &dp_df10[ctlr];
     UNIT           *uptr;
     int             unit;
     int             cyl;
     int             tmp;
     int             drv;
     if (ctlr < 0 || ctlr > NUM_DEVS_DP)
        return SCPE_OK;
     switch(dev & 3) {
     case CONI:
        *data = df10->status;
#if KI10
        *data |= B22_FLAG;
#endif
//      fprintf(stderr, "DP CONI %06o PC=%o\n\r", (uint32)*data, PC);
        return SCPE_OK;

     case CONO:
         clr_interrupt(dev);
         df10->status &= ~07LL;
         df10->status |= *data & 07LL;
         /* Clear flags */
         df10->status &= ~(*data & CLRMSK);
         uptr = &dp_unit[ctlr * NUM_UNITS_DP];
         if (*data & BUSY && df10->status & BUSY) {
                /* Stop controller */
             unit = dp_cur_unit[ctlr] & 07;
             drv = unit + (ctlr * NUM_UNITS_DP);
             uptr = &dp_unit[drv];
             sim_cancel(uptr);
             df10_finish_op(df10, 0);
         }
         if (*data & PRT_ERR) 
            df10->status &= ~(0176000000);
         if (*data & CCW_COMP) {
            df10_writecw(df10);
            df10->status &= ~CCW_COMP;
         }
//      fprintf(stderr, "DP CONO %06o %d PC=%o %06o\n\r", (uint32)*data, ctlr, PC, df10->status);
         return SCPE_OK;

     case DATAI:
         res = 0;               /* Can't write headers. */
         uptr = &dp_unit[ctlr * NUM_UNITS_DP];
         for(unit = 0; unit < NUM_UNITS_DP; unit++) {
            if (uptr->UFLAGS & SEEK_DONE)
                res |= 0400>>unit;
            uptr++;
         }
         unit = dp_cur_unit[ctlr] & 07;
         drv = unit + (ctlr * NUM_UNITS_DP);
         uptr = &dp_unit[drv];
         res |= ((uint64)(unit) << 33);
         cyl = uptr->CUR_CYL;
         res |= WR_HD_LK;
         if (GET_DTYPE(uptr->flags)) 
                res |= SEL_RP03;
         if (uptr->flags & UNIT_DIS) {
             res |= NO_DRIVE;
         } else if (uptr->flags & UNIT_ATT) {
             res |= DRV_ONLINE;
             res |= ((uint64)(cyl & 0377)) << 25;
             if (cyl & 0400) 
                res |= SEL_CYL256;
             if ((uptr->UFLAGS & SEEK_STATE) == 0)  
                res |= ON_CYL;
             if (uptr->flags & UNIT_WPRT) 
                res |= RD_ONLY;
         }
 //     fprintf(stderr, "DP DATI %012llo %d  PC=%o F=%o\n\r", res, ctlr, PC, uptr->UFLAGS);
         *data = res;
         return SCPE_OK;

     case DATAO:
  //    fprintf(stderr, "DP DATO %012llo, %d PC=%o\n\r", *data, ctlr, PC);
         if (df10->status & BUSY) {
            df10->status |= ILL_CMD;
            return SCPE_OK;
         }
         clr_interrupt(dev);
         df10->status &= ~(PI_ENABLE|CCW_COMP);
         unit = (*data >> 30) & 07;
         dp_cur_unit[ctlr] = unit;
         drv = unit + (ctlr * NUM_UNITS_DP);
         uptr = &dp_unit[drv];
         cyl = ((*data >> 22) & 0377);
         if (*data & CYL256)
                cyl += 0400;
         tmp = (*data >> 33) & 07;
         switch(tmp) {
            case WR:
                if (uptr->flags & UNIT_WPRT) {
                   df10->status |= ILL_WR;
                   df10_setirq(df10);
                   return SCPE_OK;
                }
            case RD:
            case RV:
                if (uptr->flags & UNIT_DIS) {
                     df10->status |= NO_DRIVE;
                     df10_setirq(df10);
                     return SCPE_OK;
                }
                if ((uptr->flags & UNIT_ATT) == 0) {
                   df10->status |= NOT_RDY;
                   df10_setirq(df10);
                   return SCPE_OK;
                }
                df10->status &= ~(SRC_DONE|END_CYL);
                uptr->UFLAGS = ((*data & (SURFACE|SECTOR)) >> 3) | (cyl << 20);
                uptr->UFLAGS |= (tmp << 3) | ctlr;
                uptr->DATAPTR = 0;      /* Set no data */
                CLR_BUF(uptr);
                df10_setup(df10, (uint32)*data);
                break;
            case WH:
                  if ((uptr->flags & UNIT_ATT) == 0) {
                    df10->status |= NOT_RDY;
                  } else {
                    df10->status |= ILL_WR;
                  }
                  df10_setirq(df10);
                  return SCPE_OK;
            case RC:   cyl = 0;
            case SK:   
                if ((uptr->flags & UNIT_ATT) == 0) {
                   df10->status |= NOT_RDY;
                   return SCPE_OK;
                }
                uptr->UFLAGS = (cyl << 20) | (tmp<<3) | ctlr | SEEK_STATE;
                break;
            case CL:
                uptr = &dp_unit[ctlr * NUM_UNITS_DP];
                for(unit = 0; unit < NUM_UNITS_DP; unit++) {
                   if (*data & (0400 >> unit)) 
                      uptr->UFLAGS &= ~SEEK_DONE;
                   uptr++;
                }
            case NO:
                tmp = 0;
                uptr = &dp_unit[ctlr * NUM_UNITS_DP];
                for(unit = 0; unit < NUM_UNITS_DP; unit++) {
                   if (uptr->UFLAGS & SEEK_DONE) {
                        tmp = 1;
                        break;
                   }
                   uptr++;
                }
                if (tmp) {
                    df10_setirq(df10);
                }
                return SCPE_OK;
         }
         sim_activate(uptr, 100);
         return SCPE_OK;
    }
}


t_stat dp_svc (UNIT *uptr) 
{
   int dtype = GET_DTYPE(uptr->flags);
   int ctlr  = uptr->UFLAGS & 03;
   int cyl   = (uptr->UFLAGS >> 20) & 0777;
   int sect  = (uptr->UFLAGS >> 9);
   int surf  = (sect >> 5) & 037;
   struct df10 *df10 = &dp_df10[ctlr];
   int diff, diffs, wc;
   t_stat err, r;
   sect &= 017;

   switch(uptr->UFLAGS & 0070) {
   case WR << 3:
   case RV << 3:
   case RD << 3:
                /* Cylinder, Surface, Sector all ok */
                if (BUF_EMPTY(uptr)) {
        //              fprintf(stderr, "DP %d cmd=%o cyl=%d (%o) sect=%d surf=%d %d\n\r", ctlr, uptr->UFLAGS, cyl, cyl, sect, surf,uptr->CUR_CYL);
                     if (df10->status & END_CYL) {
                          df10_finish_op(df10, 0);
                          return SCPE_OK;
                     }  
                     if (sect >= dp_drv_tab[dtype].sect) {
                          df10_finish_op(df10, SEC_ERR);
                          return SCPE_OK;
                     }
                     if (surf >= dp_drv_tab[dtype].surf) {
                          df10_finish_op(df10, SUF_ERR);
                          return SCPE_OK;
                     }
                     if (cyl != uptr->CUR_CYL) {
                          df10_finish_op(df10, SRC_ERR);
                          return SCPE_OK;
                     }
                     if ((uptr->UFLAGS & 0070) != (WR << 3)) {
                         /* Read the block */
                         int da = ((cyl * dp_drv_tab[dtype].surf + surf) * dp_drv_tab[dtype].sect 
                                        + sect) * RP_NUMWD;
                         err = sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
                         wc = sim_fread (&dp_buf[ctlr][0], sizeof(uint64), RP_NUMWD,
                                uptr->fileref);
//                       err = ferror(uptr->fileref);
                         df10->status |= SRC_DONE;
                        //fprintf(stderr, "Read %d\n\r", da);
                         for (; wc < RP_NUMWD; wc++)
                             dp_buf[ctlr][wc] = 0;
                         uptr->hwmark = RP_NUMWD;
                         uptr->DATAPTR = 0;
                         sect = sect + 1;
                         if (sect >= dp_drv_tab[dtype].sect) {
                             sect = 0;
                             surf = surf + 1;
                             if (surf >= dp_drv_tab[dtype].surf) {
                                df10->status|= END_CYL;
                             } else {
                                uptr->UFLAGS &= ~(01757000);
                                uptr->UFLAGS |= (surf << 14);
                             }
                         } else {
                             uptr->UFLAGS &= ~(017000);
                             uptr->UFLAGS |= (sect << 9);
                         }
                     } else {
                         uptr->DATAPTR = 0;
                         CLR_BUF(uptr);
                     }
                }
                switch(uptr->UFLAGS & 0070) {
                case WR << 3:
                    r = df10_read(df10);
                    if (r)
                        uptr->hwmark = uptr->DATAPTR;
                    dp_buf[ctlr][uptr->DATAPTR] = df10->buf;
                    break;
                case RV << 3:
                    r = 1;
                    break;
                case RD << 3:
                    df10->buf = dp_buf[ctlr][uptr->DATAPTR];
                    r = df10_write(df10);
                    break;
                } 
                uptr->DATAPTR++;
                if (uptr->DATAPTR >= RP_NUMWD || r == 0 ) {
                    if ((uptr->UFLAGS & 0070) == (WR << 3)) {
                         int da = ((cyl * dp_drv_tab[dtype].surf + surf) * dp_drv_tab[dtype].sect 
                                        + sect) * RP_NUMWD;
                 /* write block the block */
                        //fprintf(stderr, "Write %d %d\n\r", uptr->DATAPTR, da);
                         for (; uptr->DATAPTR < RP_NUMWD; uptr->DATAPTR++)
                             dp_buf[ctlr][uptr->DATAPTR] = 0;
                         err = sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
                         wc = sim_fwrite(&dp_buf[ctlr][0],sizeof(uint64), RP_NUMWD,
                                uptr->fileref);
//                       err = ferror(uptr->fileref);
                         df10->status |= SRC_DONE;
                         sect = sect + 1;
                         if (sect >= dp_drv_tab[dtype].sect) {
                             sect = 0;
                             surf = surf + 1;
                             if (surf >= dp_drv_tab[dtype].surf) {
                                df10->status |= END_CYL;
                             } else {
                                uptr->UFLAGS &= ~(01757 << 9);
                                uptr->UFLAGS |= (surf << 14);
                             }
                         } else {
                             uptr->UFLAGS &= ~(017 << 9);
                             uptr->UFLAGS |= (sect << 9);
                         }
                     }
                     uptr->DATAPTR = 0;
                     CLR_BUF(uptr);
                }
                if (r)
                    sim_activate(uptr, 25);
                break;    
                   
                
    case CL<<3:
    case WH<<3: /* Should never see these */
    case NO<<3:
                return SCPE_OK;
    case RC<<3: 
    case SK<<3:   
                diff = cyl - uptr->CUR_CYL;
                diffs = (diff < 0) ? -1 : 1;
        //      fprintf(stderr, "DP Seek %d %d %d %d\n\r", ctlr, cyl, uptr->CUR_CYL, diff);
                if (diff == 0) {
                    uptr->UFLAGS &= 7;
                    uptr->UFLAGS |= (NO << 3) | SEEK_DONE;
                    df10_setirq(df10);
                } else if (diff < 10 && diff > -10) {
                    uptr->CUR_CYL += diffs;
                    if (uptr->CUR_CYL < 0) {
                        uptr->UFLAGS &= 7;
                        uptr->UFLAGS |= (NO << 3) | SEEK_DONE;
                        df10_setirq(df10);
                        uptr->CUR_CYL = 0;
                    } else if (uptr->CUR_CYL > dp_drv_tab[dtype].cyl) {
                        uptr->UFLAGS &= 7;
                        uptr->UFLAGS |= (NO << 3) | SEEK_DONE;
                        df10_setirq(df10);
                        uptr->CUR_CYL = dp_drv_tab[dtype].cyl;
                    } else
                         sim_activate(uptr, 500);
                } else if (diff > 100 || diff < -100) {
                    uptr->CUR_CYL += diffs * 100;
                    sim_activate(uptr, 4000);
                } else {
                    uptr->CUR_CYL += diffs * 10;
                    sim_activate(uptr, 1000);
                }
        }
        return SCPE_OK;
}


t_stat
dp_set_type(UNIT *uptr, int32 val, char *cptr, void *desc)
{
    int         i;
    if (uptr == NULL) return SCPE_IERR;
    for (i = 0; dp_drv_tab[i].sect != 0; i++) {
        if (GET_DTYPE(val) == dp_drv_tab[i].devtype) {
            uptr->flags &= ~(UNIT_DTYPE);
            uptr->flags |= val;
            uptr->capac = dp_drv_tab[i].size;
            return SCPE_OK;
        }
    }
    return SCPE_IERR;
}


t_stat
dp_reset(DEVICE * dptr)
{
    int unit;
    int ctlr;
    UNIT *uptr = dptr->units;
    for(unit = 0; unit < NUM_UNITS_DP; unit++) {
         uptr->UFLAGS  = 0;
         uptr->CUR_CYL = 0;
         uptr++;
    }
    for (ctlr = 0; ctlr < NUM_DEVS_DP; ctlr++) {
        dp_df10[ctlr].devnum = dp_dib[ctlr].dev_num;
        dp_df10[ctlr].nxmerr = 12;
        dp_df10[ctlr].ccw_comp = 5;
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
dp_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    uint32              addr;
    uint32              ptr;
    int                 sect;
    int                 wc;

    addr = MEMSIZE - 512;
    for (sect = 4; sect <= 7; sect++) {
        sim_fseek(uptr->fileref, (sect * RP_NUMWD) * sizeof(uint64), SEEK_SET);
        sim_fread (&dp_buf[0][0], sizeof(uint64), RP_NUMWD, uptr->fileref);
        ptr = 0;
        for(wc = RP_NUMWD; wc > 0; wc--) 
            M[addr++] = dp_buf[0][ptr++];
    }
    PC = MEMSIZE - 512;
    return SCPE_OK;
}

/* Device attach */

t_stat dp_attach (UNIT *uptr, char *cptr)
{
int32 drv, i, p;
t_stat r;
DEVICE *dptr;
DIB *dib;
int ctlr;

uptr->capac = dp_drv_tab[GET_DTYPE (uptr->flags)].size;
r = attach_unit (uptr, cptr);
if (r != SCPE_OK)
    return r;
dptr = find_dev_from_unit(uptr);
if (dptr == 0)
    return SCPE_OK;
dib = (DIB *) dptr->ctxt;
ctlr = dib->dev_num & 014;
uptr->CUR_CYL = 0;
uptr->UFLAGS = (NO << 3) | SEEK_DONE | (ctlr >> 2);
dp_df10[ctlr].status |= PI_ENABLE;
set_interrupt(DP_DEVNUM + (ctlr), dp_df10[ctlr >> 2].status & 7);
return SCPE_OK;
}

/* Device detach */

t_stat dp_detach (UNIT *uptr)
{
int32 drv;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
if (sim_is_active (uptr))                              /* unit active? */
    sim_cancel (uptr);                                  /* cancel operation */
return detach_unit (uptr);
}

t_stat dp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "RP10 RP01/2/3  Disk Pack Drives (DP)\n\n");
fprintf (st, "The DP controller implements the Massbus family of large disk drives.  RP\n");
fprintf (st, "options include the ability to set units write enabled or write locked, to\n");
fprintf (st, "set the drive type to one of six disk types or autosize, and to write a DEC\n");
fprintf (st, "standard 044 compliant bad block table on the last track.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprintf (st, "The RP device supports the BOOT command.\n");
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *dp_description (DEVICE *dptr)
{
return "RP10 disk controller";
}

#endif
