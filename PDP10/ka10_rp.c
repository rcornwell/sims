/* ka10_rp.c: Dec RH10 RP04/5/6

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "ka10_defs.h"

#ifndef NUM_DEVS_RP
#define NUM_DEVS_RP 0
#endif

#if (NUM_DEVS_RP > 0)

#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

#define RP_NUMWD        128     /* 36bit words/sec */
#define RP_DEVNUM       0270    /* First device number */
#define NUM_UNITS_RP    8

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    7
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


/* CONI Flags */
#define IADR_ATTN       0000000000040   /* Interrupt on attention */
#define IARD_RAE        0000000000100   /* Interrupt on register access error */
#define DIB_CBOV        0000000000200   /* Control bus overrun */
#define CXR_PS_FAIL     0000000002000   /* Power supply fail (not implemented) */
#define CXR_ILC         0000000004000   /* Illegal function code */
#define CR_DRE          0000000010000   /* Or Data and Control Timeout */
#define DTC_OVER        0000000020000   /* DF10 did not supply word on time (not implemented) */
#define CCW_COMP_1      0000000040000   /* Control word written. */
#define CXR_CHAN_ER     0000000100000   /* Channel Error */
#define CXR_EXC         0000000200000   /* Error in drive transfer */
#define CXR_DBPE        0000000400000   /* Device Parity error (not implemented) */
#define CXR_NXM         0000001000000   /* Channel non-existent memory (not implemented) */
#define CXR_CWPE        0000002000000   /* Channel Control word parity error (not implemented) */
#define CXR_CDPE        0000004000000   /* Channel Data Parity Error (not implemented) */
#define CXR_SD_RAE      0000200000000   /* Register access error */
#define CXR_ILFC        0000400000000   /* Illegal CXR function code */
#define B22_FLAG        0004000000000   /* 22 bit channel */
#define CC_CHAN_PLS     0010000000000   /* Channel transfer pulse (not implemented) */
#define CC_CHAN_ACT     0020000000000   /* Channel in use */
#define CC_INH          0040000000000   /* Disconnect channel */
#define CB_FULL         0200000000000   /* Set when channel buffer is full (not implemented) */
#define AR_FULL         0400000000000   /* Set when AR is full (not implemented) */

/* CONO Flags */
#define ATTN_EN         0000000000040   /* enable attention interrupt. */
#define REA_EN          0000000000100   /* enable register error interrupt */
#define CBOV_CLR        0000000000200   /* Clear CBOV */
#define CONT_RESET      0000000002000   /* Clear All error bits */
#define ILC_CLR         0000000004000   /* Clear ILC and SD RAE */
#define DRE_CLR         0000000010000   /* Clear CR_CBTO and CR_DBTO */
#define OVER_CLR        0000000020000   /* Clear DTC overrun */
#define WRT_CW          0000000040000   /* Write control word */
#define CHN_CLR         0000000100000   /* Clear Channel Error */
#define DR_EXC_CLR      0000000200000   /* Clear DR_EXC */
#define DBPE_CLR        0000000400000   /* Clear CXR_DBPE */

/* DATAO/DATAI */
#define CR_REG          0770000000000   /* Register number */
#define LOAD_REG        0004000000000   /* Load register */
#define CR_MAINT_MODE   0000100000000   /* Maint mode... not implemented */
#define CR_DRIVE        0000007000000   
#define CR_GEN_EVD      0000000400000   /* Enable Parity */
#define CR_DXES         0000000200000   /* Disable DXES errors  */
#define CR_INAD         0000000077600
#define CR_WTEVM        0000000000100   /* Verify Parity */
#define CR_FUNC         0000000000076
#define CR_GO           0000000000001

#define IRQ_VECT        0000000000177   /* Interupt vector */
#define IRQ_KI10        0000002000000   
#define IRQ_KA10        0000001000000

/* u3  low */
/* RPC - 00 - control */

#define CS1_GO          CR_GO                           /* go */
#define CS1_V_FNC       1                               /* function pos */
#define CS1_M_FNC       037                             /* function mask */
#define CS1_FNC         (CS1_M_FNC << CS1_V_FNC)
#define  FNC_NOP        000                             /* no operation */
#define  FNC_UNLOAD     001                             /* unload */
#define  FNC_SEEK       002                             /* seek */
#define  FNC_RECAL      003                             /* recalibrate */
#define  FNC_DCLR       004                             /* drive clear */
#define  FNC_RELEASE    005                             /* port release */
#define  FNC_OFFSET     006                             /* offset */
#define  FNC_RETURN     007                             /* return to center */
#define  FNC_PRESET     010                             /* read-in preset */
#define  FNC_PACK       011                             /* pack acknowledge */
#define  FNC_SEARCH     014                             /* search */
#define FNC_XFER        024                             /* >=? data xfr */
#define  FNC_WCHK       024                             /* write check */
#define  FNC_WCHKH      025                             /* write check headers */
#define  FNC_WRITE      030                             /* write */
#define  FNC_WRITEH     031                             /* write w/ headers */
#define  FNC_READ       034                             /* read */
#define  FNC_READH      035                             /* read w/ headers */
#define CS1_DVA         0004000                         /* drive avail NI */
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)

/* u3  low */
/* RPDS - 01 - drive status */

#define DS_OFF          0000001                         /* offset mode */
#define DS_VV           0000100                         /* volume valid */
#define DS_DRY          0000200                         /* drive ready */
#define DS_DPR          0000400                         /* drive present */
#define DS_PGM          0001000                         /* programable NI */
#define DS_LST          0002000                         /* last sector */
#define DS_WRL          0004000                         /* write locked */
#define DS_MOL          0010000                         /* medium online */
#define DS_PIP          0020000                         /* pos in progress */
#define DS_ERR          0040000                         /* error */
#define DS_ATA          0100000                         /* attention active */
#define DS_MBZ          0000076

/* u3 high */
/* RPER1 - 02 - error status 1 */

#define ER1_ILF         0000001                         /* illegal func */
#define ER1_ILR         0000002                         /* illegal register */
#define ER1_RMR         0000004                         /* reg mod refused */
#define ER1_PAR         0000010                         /* parity err */
#define ER1_FER         0000020                         /* format err NI */
#define ER1_WCF         0000040                         /* write clk fail NI */
#define ER1_ECH         0000100                         /* ECC hard err NI */
#define ER1_HCE         0000200                         /* hdr comp err NI */
#define ER1_HCR         0000400                         /* hdr CRC err NI */
#define ER1_AOE         0001000                         /* addr ovflo err */
#define ER1_IAE         0002000                         /* invalid addr err */
#define ER1_WLE         0004000                         /* write lock err */
#define ER1_DTE         0010000                         /* drive time err NI */
#define ER1_OPI         0020000                         /* op incomplete */
#define ER1_UNS         0040000                         /* drive unsafe */
#define ER1_DCK         0100000                         /* data check NI */

/* RPMR - 03 - maintenace register */

/* RPAS - 04 - attention summary */

#define AS_U0           0000001                         /* unit 0 flag */

/* u4 high */
/* RPDC - 05 - desired sector */

#define DA_V_SC         16                              /* sector pos */
#define DA_M_SC         077                             /* sector mask */
#define DA_V_SF         24                              /* track pos */
#define DA_M_SF         077                             /* track mask */
#define DA_MBZ          0140300
#define GET_SC(x)       (((x) >> DA_V_SC) & DA_M_SC)
#define GET_SF(x)       (((x) >> DA_V_SF) & DA_M_SF)

/* RPDT - 06 - drive type */

/* RPLA - 07 - look ahead register */

#define LA_V_SC         6                               /* sector pos */

/* RPER2 - 10 - error status 2 - drive unsafe conditions - unimplemented */
/* u5 high */
/* RPOF  - 11 - offset register */
/* u4 low */
/* RPDC  - 12 - desired cylinder */
#define DC_V_CY         0                               /* cylinder pos */
#define DC_M_CY         01777                           /* cylinder mask */
#define DC_MBZ          0176000
#define GET_CY(x)       (((x) >> DC_V_CY) & DC_M_CY)
#define GET_DA(c,d)     ((((GET_CY (c) * rp_drv_tab[d].surf) + \
                        GET_SF (c)) * rp_drv_tab[d].sect) + GET_SC (c))
/* u5 low */
/* RPCC  - 13 - current cylinder */
/* RPSN  - 14 - serial number */
/* RPER3 - 15 - error status 3 - more unsafe conditions - unimplemented */

/* RPDB - 176722 - data buffer */


#define OF_HCI          0002000                         /* hdr cmp inh NI */
#define OF_ECI          0004000                         /* ECC inhibit NI */
#define OF_F22          0010000                         /* format NI */
#define OF_MBZ          0161400


/* RPEC1 - 16 - ECC status 1 - unimplemented */
/* RPEC2 - 17 - ECC status 2 - unimplemented */


/* This controller supports many different disk drive types.  These drives
   are operated in 576 bytes/sector (128 36b words/sector) mode, which gives
   them somewhat different geometry from the PDP-11 variants:

   type         #sectors/       #surfaces/      #cylinders/
                 surface         cylinder        drive

   RP04/5       20              19              411             =88MB
   RP06         20              19              815             =176MB
   RP07         43              32              630             =516MB

   In theory, each drive can be a different type.  The size field in
   each unit selects the drive capacity for each drive and thus the
   drive type.  DISKS MUST BE DECLARED IN ASCENDING SIZE.

   The RP07, despite its name, uses an RM-style controller.
*/

#define RP04_DTYPE      0
#define RP04_SECT       20
#define RP04_SURF       19
#define RP04_CYL        411
#define RP04_DEV        020020
#define RP04_SIZE       (RP04_SECT * RP04_SURF * RP04_CYL * RP_NUMWD)

#define RP06_DTYPE      1
#define RP06_SECT       20
#define RP06_SURF       19
#define RP06_CYL        815
#define RP06_DEV        020022
#define RP06_SIZE       (RP06_SECT * RP06_SURF * RP06_CYL * RP_NUMWD)

#define RP07_DTYPE      2
#define RP07_SECT       43
#define RP07_SURF       32
#define RP07_CYL        630
#define RP07_DEV        020042
#define RP07_SIZE       (RP07_SECT * RP07_SURF * RP07_CYL * RP_NUMWD)

struct drvtyp {
    int32       sect;                                   /* sectors */
    int32       surf;                                   /* surfaces */
    int32       cyl;                                    /* cylinders */
    int32       size;                                   /* #blocks */
    int32       devtype;                                /* device type */
    };

struct drvtyp rp_drv_tab[] = {
    { RP04_SECT, RP04_SURF, RP04_CYL, RP04_SIZE, RP04_DEV },
    { RP06_SECT, RP06_SURF, RP06_CYL, RP06_SIZE, RP06_DEV },
    { RP07_SECT, RP07_SURF, RP07_CYL, RP07_SIZE, RP07_DEV },
    { 0 }
    };


struct df10   rp_df10[NUM_DEVS_RP];
uint32        rp_cur_unit[NUM_DEVS_RP];
uint64        rp_buf[NUM_DEVS_RP][RP_NUMWD];
int           rp_reg[NUM_DEVS_RP];
int           rp_ivect[NUM_DEVS_RP];
int           rp_imode[NUM_DEVS_RP];
int           rp_drive[NUM_DEVS_RP];
int           rp_rae[NUM_DEVS_RP];
int           rp_attn[NUM_DEVS_RP];
extern int    readin_flag;

t_stat        rp_devio(uint32 dev, uint64 *data);
void          rp_write(int ctlr, int unit, int reg, uint32 data);
uint32        rp_read(int ctlr, int unit, int reg);
t_stat        rp_svc(UNIT *);
t_stat        rp_boot(int32, DEVICE *);
void          rp_ini(UNIT *, t_bool);
t_stat        rp_reset(DEVICE *);
t_stat        rp_attach(UNIT *, CONST char *);
t_stat        rp_detach(UNIT *);
t_stat        rp_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat        rp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, 
                    const char *cptr);
const char    *rp_description (DEVICE *dptr);


UNIT                rp_unit[] = {
/* Controller 1 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
#if (NUM_DEVS_RP > 1)
/* Controller 2 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
#if (NUM_DEVS_RP > 2)
/* Controller 3 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
#if (NUM_DEVS_RP > 3)
/* Controller 4 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+(RP06_DTYPE << UNIT_V_DTYPE), RP06_SIZE) },
#endif
#endif
#endif
};

DIB rp_dib[] = {
    {RP_DEVNUM+0000, 1, &rp_devio},
    {RP_DEVNUM+0004, 1, &rp_devio},
    {RP_DEVNUM+0100, 1, &rp_devio},
    {RP_DEVNUM+0104, 1, &rp_devio}};

MTAB                rp_mod[] = {
    {UNIT_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {UNIT_WLK, UNIT_WLK, "write locked", "LOCKED", NULL},
    {UNIT_DTYPE, (RP07_DTYPE << UNIT_V_DTYPE), "RP07", "RP07", &rp_set_type },
    {UNIT_DTYPE, (RP06_DTYPE << UNIT_V_DTYPE), "RP06", "RP06", &rp_set_type },
    {UNIT_DTYPE, (RP04_DTYPE << UNIT_V_DTYPE), "RP04", "RP04", &rp_set_type },
    {0}
};

DEVICE              rpa_dev = {
    "RPA", rp_unit, NULL, rp_mod,
    NUM_UNITS_RP, 8, 18, 1, 8, 36,
    NULL, NULL, &rp_reset, &rp_boot, &rp_attach, &rp_detach,
    &rp_dib[0], DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &rp_help, NULL, NULL, &rp_description
};

#if (NUM_DEVS_RP > 1)
DEVICE              rpb_dev = {
    "RPB", &rp_unit[010], NULL, rp_mod,
    NUM_UNITS_RP, 8, 18, 1, 8, 36,
    NULL, NULL, &rp_reset, &rp_boot, &rp_attach, &rp_detach,
    &rp_dib[1], DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &rp_help, NULL, NULL, &rp_description
};

#if (NUM_DEVS_RP > 2)
DEVICE              rpc_dev = {
    "RPC", &rp_unit[020], NULL, rp_mod,
    NUM_UNITS_RP, 8, 18, 1, 8, 36,
    NULL, NULL, &rp_reset, &rp_boot, &rp_attach, &rp_detach,
    &rp_dib[2], DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &rp_help, NULL, NULL, &rp_description
};

#if (NUM_DEVS_RP > 3)
DEVICE              rpd_dev = {
    "RPD", &rp_unit[030], NULL, rp_mod,
    NUM_UNITS_RP, 8, 18, 1, 8, 36,
    NULL, NULL, &rp_reset, &rp_boot, &rp_attach, &rp_detach,
    &rp_dib[3], DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &rp_help, NULL, NULL, &rp_description
};

#endif
#endif
#endif

DEVICE *rp_devs[] = {
    &rpa_dev,
#if (NUM_DEVS_RP > 1)
    &rpb_dev,
#if (NUM_DEVS_RP > 2)
    &rpc_dev,
#if (NUM_DEVS_RP > 3)
    &rpd_dev,
#endif
#endif
#endif
};


t_stat rp_devio(uint32 dev, uint64 *data) {
     int            ctlr = -1;
     DEVICE        *dptr;
     struct df10   *df10;
     UNIT          *uptr;
     int            tmp;
     int            drive;

     for (drive = 0; drive < NUM_DEVS_RP; drive++) {
        if (rp_dib[drive].dev_num == (dev & 0774)) {
            ctlr = drive;
            break;
        }
     }
     if (ctlr < 0)
        return SCPE_OK;
     dptr = rp_devs[ctlr];
     df10 = &rp_df10[ctlr];
     switch(dev & 3) {
     case CONI:
        *data = df10->status & ~(IADR_ATTN|IARD_RAE);
        if (rp_attn[ctlr] != 0 && (df10->status & IADR_ATTN))
           *data |= IADR_ATTN;
        if (rp_rae[ctlr] != 0 && (df10->status & IARD_RAE))
           *data |= IARD_RAE;
#if KI10_22BIT
        *data |= B22_FLAG;
#endif
        sim_debug(DEBUG_CONI, dptr, "RP %03o CONI %06o PC=%o %o\n", 
               dev, (uint32)*data, PC, rp_attn[ctlr]);
        return SCPE_OK;

     case CONO:
         clr_interrupt(dev);
         df10->status &= ~07LL;
         df10->status |= *data & (07LL|IADR_ATTN|IARD_RAE);
         /* Clear flags */
         if (*data & (DBPE_CLR|DR_EXC_CLR|CHN_CLR))
            df10->status &= ~(*data & (DBPE_CLR|DR_EXC_CLR|CHN_CLR));
         if (*data & OVER_CLR)
            df10->status &= ~(DTC_OVER);
         if (*data & CBOV_CLR)
            df10->status &= ~(DIB_CBOV);
         if (*data & CXR_ILC)
            df10->status &= ~(CXR_ILFC|CXR_SD_RAE);
         if (*data & WRT_CW)
            df10_writecw(df10);
         sim_debug(DEBUG_CONO, dptr, "RP %03o CONO %06o %d PC=%06o %06o\n",
               dev, (uint32)*data, ctlr, PC, df10->status);
         return SCPE_OK;

     case DATAI:
        *data = 0;
        if (rp_reg[ctlr] == 040) {
              *data = (t_uint64)(rp_read(ctlr, rp_drive[ctlr], 0) & 077);
              *data |= ((t_uint64)(df10->cia)) << 6;
              *data |= ((t_uint64)(rp_drive[ctlr])) << 18;
        } else if (rp_reg[ctlr] == 044) {
              *data = (t_uint64)rp_ivect[ctlr];
              if (rp_imode[ctlr]) 
                *data |= IRQ_KI10;
              else
                *data |= IRQ_KA10;
        } else if (rp_reg[ctlr] == 054) {
                *data = (t_uint64)(rp_rae[ctlr]);
        } else if ((rp_reg[ctlr] & 040) == 0) {
              *data = (t_uint64)(rp_read(ctlr, rp_drive[ctlr], rp_reg[ctlr]) & 0777777);
              *data |= ((t_uint64)(rp_drive[ctlr])) << 18;
        }
        *data |= ((t_uint64)(rp_reg[ctlr])) << 30;
        sim_debug(DEBUG_DATAIO, dptr, "RP %03o DATI %012llo, %d %d PC=%06o\n\r", 
                    dev, *data, ctlr, rp_drive[ctlr], PC);
        return SCPE_OK;

     case DATAO:
         sim_debug(DEBUG_DATAIO, dptr, "RP %03o DATO %012llo, %d PC=%06o %06o\n\r",
                    dev, *data, ctlr, PC, df10->status);
         clr_interrupt(dev);
         df10->status &= ~(PI_ENABLE|CCW_COMP_1);
         rp_reg[ctlr] = ((int)(*data >> 30)) & 077;
         if (*data & LOAD_REG) {
             if (rp_reg[ctlr] == 040) {
                if (df10->status & BUSY) {
                    df10->status |= CC_CHAN_ACT;
                    return SCPE_OK;
                }
                rp_drive[ctlr] = (int)(*data >> 18) & 07;
                /* Check if access error */
                if (rp_rae[ctlr] & (1 << rp_drive[ctlr])) {
                    return SCPE_OK;
                }

                /* Start command */
                df10_setup(df10, (uint32)(*data >> 6));
                df10->status |= BUSY;
                rp_write(ctlr, rp_drive[ctlr], 0, (uint32)(*data & 077));
                sim_debug(DEBUG_DATAIO, dptr, 
                    "RP %03o command %012llo, %d[%d] PC=%06o %06o\n\r",
                    dev, *data, ctlr, rp_drive[ctlr], PC, df10->status);
             } else if (rp_reg[ctlr] == 044) {  
                /* Set KI10 Irq vector */
                rp_ivect[ctlr] = (int)(*data & IRQ_VECT);
                rp_imode[ctlr] = (*data & IRQ_KI10) != 0;
             } else if (rp_reg[ctlr] == 050) {
                ;    /* Diagnostic access to mass bus. */
             } else if (rp_reg[ctlr] == 054) {
                /* clear flags */
                rp_rae[ctlr] &= ~(*data & 0377);
             } else if ((rp_reg[ctlr] & 040) == 0) {
                rp_drive[ctlr] = (int)(*data >> 18) & 07;
                /* Check if access error */
                if (rp_rae[ctlr] & (1 << rp_drive[ctlr])) {
                    return SCPE_OK;
                }
                rp_drive[ctlr] = (int)(*data >> 18) & 07;
                rp_write(ctlr, rp_drive[ctlr], rp_reg[ctlr] & 037, 
                        (int)(*data & 0777777));
             }
         } else {
             if (rp_reg[ctlr] <= 040) {
                rp_drive[ctlr] = (int)(*data >> 18) & 07;
             } 
         }
         return SCPE_OK;
    }
}


void
rp_write(int ctlr, int unit, int reg, uint32 data) {
    UNIT        *uptr = &rp_unit[(ctlr * 8) + unit];
    int          i;
    DEVICE      *dptr = rp_devs[ctlr];

    switch(reg) {
    case  000:  /* control */
        sim_debug(DEBUG_DETAIL, dptr, "RPA%o %d Status=%06o\n", unit, ctlr, uptr->u3);
        if (uptr->flags & UNIT_WLK) 
           uptr->u3 |= DS_WRL;
        if ((uptr->u3 & DS_DRY) && data & 01) {
            uptr->u3 &= DS_ATA|DS_VV|DS_DPR|DS_MOL|DS_WRL;
            uptr->u3 |= data & 076;
            switch (GET_FNC(data)) {
            case FNC_NOP:
                uptr->u3 |= DS_DRY;
                break;
            case FNC_PRESET:                               /* read-in preset */
                uptr->u4 = 0;
                uptr->u3 |= DS_VV;
            case FNC_RECAL:                                /* recalibrate */
                uptr->u4 &= ~0177777;
            case FNC_SEARCH:                               /* search */
            case FNC_SEEK:                                 /* seek */
            case FNC_RETURN:                               /* return to center */
            case FNC_OFFSET:                               /* offset */
            case FNC_UNLOAD:                               /* unload */
                uptr->u3 &= ~DS_OFF;
            case FNC_WCHK:                                 /* write check */
            case FNC_WRITE:                                /* write */
            case FNC_WRITEH:                               /* write w/ headers */
            case FNC_READ:                                 /* read */
            case FNC_READH:                                /* read w/ headers */
                uptr->u3 |= DS_PIP|CR_GO;
                uptr->u6 = 0;
                break;
            case FNC_DCLR:                                 /* drive clear */
                uptr->u3 |= DS_DRY;
                uptr->u3 &= ~(DS_ATA|CR_GO);
                rp_attn[ctlr] &= ~(1<<unit);
                break;
            case FNC_RELEASE:                              /* port release */
                uptr->u3 |= DS_DRY;
                break;
            case FNC_PACK:                                 /* pack acknowledge */
                uptr->u3 |= DS_VV|DS_DRY;
                break;
            default:
                uptr->u3 |= DS_DRY|DS_ERR;
                uptr->u3 |= (ER1_ILF << 16);
            }
            if (uptr->u3 & DS_PIP)
                sim_activate(uptr, 100);
            sim_debug(DEBUG_DETAIL, dptr, "RPA%o AStatus=%06o\n", unit, uptr->u3);
        }
        return;
    case  001:  /* status */
        break;
    case  002:  /* error register 1 */
        uptr->u3 &= 0177777;
        uptr->u3 |= data << 16;
        if (data != 0)
           uptr->u3 |= DS_ERR;
        break;
    case  003:  /* maintenance */
        break;
    case  004:  /* atten summary */
        for (i = 0; i < 8; i++) {
            if (data & (1<<i)) {
                rp_unit[(ctlr * 8) + i].u3 &= ~DS_ATA;
                rp_attn[ctlr] &= ~(1<<i);
            }
        }
        break;
    case  005:  /* sector/track */
        uptr->u4 &= 0177777;
        uptr->u4 |= data << 16;
        break;
    case  010:  /* error register 2 */
        if (data != 0)
           uptr->u3 |= DS_ERR;
        uptr->u5 &= 0177777;
        uptr->u5 |= data << 16;
    case  006:  /* drive type */
    case  007:  /* look ahead */
    case  011:  /* offset */
        break;
    case  012:  /* desired cylinder */
        uptr->u4 &= ~0177777;
        uptr->u4 |= data;
        break;
    case  013:  /* current cylinder */
    case  014:  /* serial no */
    case  015:  /* error register 3 */
    case  016:  /* ecc position */
    case  017:  /* ecc pattern */
        break;
    default:
        uptr->u3 |= (ER1_ILR<<16)|DS_ERR;
        rp_rae[ctlr] &= ~(1<<unit);
    }
}

uint32
rp_read(int ctlr, int unit, int reg) {
    UNIT          *uptr = &rp_unit[(ctlr * 8) + unit];
    struct df10   *df10;
    uint32        temp = 0;
    int           i;

    switch(reg) {
    case  000:  /* control */
        df10 = &rp_df10[ctlr];
        temp = uptr->u3 & 076;
        if (uptr->flags & UNIT_ATT) 
           temp |= CS1_DVA;
        if (df10->status & BUSY || uptr->u3 & CR_GO) 
           temp |= CS1_GO;
        break;
    case  001:  /* status */
        temp = uptr->u3 & 0177700;
        break;
    case  002:  /* error register 1 */
        temp = (uptr->u3 >> 16) & 0177777;
        break;
    case  004:  /* atten summary */
        for (i = 0; i < 8; i++) {
            if (rp_unit[(ctlr * 8) + i].u3 & DS_ATA) {
                temp |= 1 << i;
            }
        }
        break;
    case  005:  /* sector/track */
        temp = (uptr->u4 >> 16) & 0177777;
        break;
    case  006:  /* drive type */
        temp = rp_drv_tab[GET_DTYPE(uptr->flags)].devtype;
        break;
    case  011:  /* offset */
        temp = (uptr->u5 >> 16) & 0177777;
        break;
    case  012:  /* desired cylinder */
        temp = uptr->u4 & 0177777;
        break;
    case  013:  /* current cylinder */
        temp = uptr->u5 & 0177777;
        break;
    case  014:  /* serial no */
    case  003:  /* maintenance */
    case  007:  /* look ahead */
    case  010:  /* error register 2 */
    case  015:  /* error register 3 */
    case  016:  /* ecc position */
    case  017:  /* ecc pattern */
        break;
    default:
        uptr->u3 |= (ER1_ILR<<16);
        rp_rae[ctlr] &= ~(1<<unit);
    }
    return temp;
}


t_stat rp_svc (UNIT *uptr) 
{
    int          dtype = GET_DTYPE(uptr->flags);
    int          ctlr;
    int          unit;
    DEVICE      *dptr;
    struct df10 *df;
    int          cyl = uptr->u4 & 01777;
    int          diff, da;
    t_stat       err, r;

    /* Find dptr, and df10 */
    for (ctlr = 0; ctlr < NUM_DEVS_RP; ctlr++) {
        dptr = rp_devs[ctlr];
        unit = uptr - dptr->units;
        if (unit < 8)
           break;
    }
    if (unit > 8)
       return SCPE_OK;
    df = &rp_df10[ctlr];
    /* Check if seeking */
    if (uptr->u3 & DS_PIP) {
        sim_debug(DEBUG_DETAIL, dptr, "RPA%o seek %d %d\n", unit, cyl, uptr->u5);
        if (cyl > rp_drv_tab[dtype].cyl) {
            uptr->u3 &= ~DS_PIP;
            uptr->u3 |= (ER1_IAE << 16)|DS_ERR|DS_DRY|DS_ATA;
        }
        diff = cyl - (uptr->u5 & 01777);
        if (diff < 0) {
            if (diff < -50) {
                uptr->u5 -= 50;
                sim_activate(uptr, 500);
            } else if (diff < -10) {
                uptr->u5 -= 10;
                sim_activate(uptr, 200);
            } else {
                uptr->u5 -= 1;
                sim_activate(uptr, 100);
            }
            return SCPE_OK;
        } else if (diff > 0) {
            if (diff > 50) {
                uptr->u5 += 50;
                sim_activate(uptr, 500);
            } else if (diff > 10) {
                uptr->u5 += 10;
                sim_activate(uptr, 200);
            } else {
                uptr->u5 += 1;
                sim_activate(uptr, 100);
            }
            return SCPE_OK;
        } else {
            uptr->u3 &= ~DS_PIP;
            uptr->u6 = 0;
        }
    }

    switch (GET_FNC(uptr->u3)) {
    case FNC_NOP:
    case FNC_DCLR:                                 /* drive clear */
    case FNC_RELEASE:                              /* port release */
    case FNC_PACK:                                 /* pack acknowledge */
        break;
    case FNC_UNLOAD:                               /* unload */
        rp_detach(uptr);
    case FNC_OFFSET:                               /* offset */
        uptr->u3 |= DS_OFF;
    case FNC_RETURN:                               /* return to center */
    case FNC_PRESET:                               /* read-in preset */
    case FNC_RECAL:                                /* recalibrate */
    case FNC_SEEK:                                 /* seek */
        rp_attn[ctlr] |= 1<<unit;
        uptr->u3 |= DS_DRY|DS_ATA;
        uptr->u3 &= ~CR_GO;
        df->status &= ~BUSY;
        if (df->status & IADR_ATTN)
            df10_setirq(df);
        sim_debug(DEBUG_DETAIL, dptr, "RPA%o seekdone %d %o\n", unit, cyl, uptr->u3);
        break;

    case FNC_SEARCH:                               /* search */
        if (GET_SC(uptr->u4) > rp_drv_tab[dtype].sect ||
            GET_SF(uptr->u4) > rp_drv_tab[dtype].surf) 
            uptr->u3 |= (ER1_IAE << 16)|DS_ERR;
        rp_attn[ctlr] |= 1<<unit;
        uptr->u3 |= DS_DRY|DS_ATA;
        uptr->u3 &= ~CR_GO;
        df->status &= ~BUSY;
        if (df->status & IADR_ATTN)
            df10_setirq(df);
        sim_debug(DEBUG_DETAIL, dptr, "RPA%o searchdone %d %o\n", unit, cyl, uptr->u3);
        break;

    case FNC_READ:                                 /* read */
    case FNC_READH:                                /* read w/ headers */
    case FNC_WCHK:                                 /* write check */

        if (uptr->u6 == 0) {
            int wc;
            if (GET_SC(uptr->u4) > rp_drv_tab[dtype].sect ||
                GET_SF(uptr->u4) > rp_drv_tab[dtype].surf) {
                uptr->u3 |= (ER1_IAE << 16)|DS_ERR|DS_DRY|DS_ATA;
                rp_attn[ctlr] |= 1<<unit;
                df->status &= ~BUSY;
                uptr->u3 &= ~CR_GO;
        sim_debug(DEBUG_DETAIL, dptr, "RPA%o readx done\n", unit);
                if (df->status & IADR_ATTN)
                    df10_setirq(df);
                return SCPE_OK;
            }
        sim_debug(DEBUG_DETAIL, dptr, "RPA%o read (%d,%d,%d)\n", unit, cyl, 
                   GET_SC(uptr->u4), GET_SF(uptr->u4));
            da = GET_DA(uptr->u4, dtype) * RP_NUMWD;
            sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
            wc = sim_fread (&rp_buf[ctlr][0], sizeof(uint64), RP_NUMWD,
                                uptr->fileref);
            while (wc < RP_NUMWD) 
                rp_buf[ctlr][wc++] = 0;
            uptr->hwmark = RP_NUMWD;
        }

        df->buf = rp_buf[ctlr][uptr->u6++];
        sim_debug(DEBUG_DATA, dptr, "RPA%o read word %d %012llo\n", unit, uptr->u6, df->buf);
        if (df10_write(df)) {
            if (uptr->u6 == uptr->hwmark) {
                /* Increment to next sector. Set Last Sector */
                uptr->u6 = 0;
                uptr->u4 += 0x10000;
                if (GET_SC(uptr->u4) > rp_drv_tab[dtype].sect) {
                    uptr->u4 &= 0xff00ffff;
                    uptr->u4 += 0x01000000;
                    uptr->u3 |= DS_LST;
                    if (GET_SF(uptr->u4) > rp_drv_tab[dtype].surf) {
                         uptr->u4 &= 0x0000ffff;
                         uptr->u4 += 1;
                         uptr->u3 |= DS_PIP;
                    }
                }
            }
            sim_activate(uptr, 20);
        } else {
        sim_debug(DEBUG_DETAIL, dptr, "RPA%o read done\n", unit);
            uptr->u3 |= DS_DRY;
            uptr->u3 &= ~CR_GO;
            df->status &= ~BUSY;
            df10_setirq(df);
            return SCPE_OK;
        }
        break;

    case FNC_WRITE:                                /* write */
    case FNC_WRITEH:                               /* write w/ headers */
        if (uptr->u6 == 0) {
            if (GET_SC(uptr->u4) > rp_drv_tab[dtype].sect ||
                GET_SF(uptr->u4) > rp_drv_tab[dtype].surf) {
                uptr->u3 |= (ER1_IAE << 16)|DS_ERR|DS_DRY|DS_ATA;
                rp_attn[ctlr] |= 1<<unit;
                df->status &= ~BUSY;
                uptr->u3 &= ~CR_GO;
        sim_debug(DEBUG_DETAIL, dptr, "RPA%o writex done\n", unit);
                if (df->status & IADR_ATTN)
                    df10_setirq(df);
                return SCPE_OK;
            }
        }
        r = df10_read(df);
        rp_buf[ctlr][uptr->u6++] = df->buf;
        sim_debug(DEBUG_DATA, dptr, "RPA%o write word %d %012llo\n", unit, uptr->u6, df->buf);
        if (r == 0 || uptr->u6 == RP_NUMWD) {
            while (uptr->u6 < RP_NUMWD) 
                rp_buf[ctlr][uptr->u6++] = 0;
        sim_debug(DEBUG_DETAIL, dptr, "RPA%o write (%d,%d,%d)\n", unit, cyl, 
                   GET_SC(uptr->u4), GET_SF(uptr->u4));
            da = GET_DA(uptr->u4, dtype) * RP_NUMWD;
            sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
            sim_fwrite (&rp_buf[ctlr][0], sizeof(uint64), RP_NUMWD,
                                uptr->fileref);
            uptr->u6 = 0;
            if (r) {
                uptr->u4 += 0x10000;
                /* Increment to next sector. Set Last Sector */
                if (GET_SC(uptr->u4) > rp_drv_tab[dtype].sect) {
                    uptr->u4 &= 0xff00ffff;
                    uptr->u4 += 0x01000000;
                    uptr->u3 |= DS_LST;
                    if (GET_SF(uptr->u4) > rp_drv_tab[dtype].surf) {
                         uptr->u4 &= 0x0000ffff;
                         uptr->u4 += 1;
                         uptr->u3 |= DS_PIP;
                    }
                }
             }
        }
        if (r) {
            sim_activate(uptr, 20);
        } else {
        sim_debug(DEBUG_DETAIL, dptr, "RPA%o write done\n", unit);
            uptr->u3 |= DS_DRY;
            uptr->u3 &= ~CR_GO;
            df->status &= ~BUSY;
            df10_setirq(df);
            return SCPE_OK;
        }
        break;
    }   
    return SCPE_OK;
}


t_stat
rp_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int         i;

    if (uptr == NULL) return SCPE_IERR;
    uptr->flags &= ~(UNIT_DTYPE);
    uptr->flags |= val;
    i = GET_DTYPE(val);
    uptr->capac = rp_drv_tab[i].size;
    return SCPE_OK;
}


t_stat
rp_reset(DEVICE * rptr)
{
    int ctlr;
    for (ctlr = 0; ctlr < NUM_DEVS_RP; ctlr++) {
        rp_df10[ctlr].devnum = rp_dib[ctlr].dev_num;
        rp_df10[ctlr].nxmerr = 19;
        rp_df10[ctlr].ccw_comp = 14;
        rp_attn[ctlr] = 0;
        rp_rae[ctlr] = 0;
    }
    return SCPE_OK;
}

/* Boot from given device */
t_stat
rp_boot(int32 unit_num, DEVICE * rptr)
{
    UNIT               *uptr = &rptr->units[unit_num];
    uint32              addr;
    uint32              ptr;
    int                 sect;
    int                 wc;

    sim_fseek(uptr->fileref, 0, SEEK_SET);
    sim_fread (&rp_buf[0][0], sizeof(uint64), RP_NUMWD, uptr->fileref);
    addr = rp_buf[0][0] & RMASK;
    wc = (rp_buf[0][0] >> 18) & RMASK;
    ptr = 1;
    for(; wc > 0; wc--) 
        M[addr++] = rp_buf[0][ptr++];
    addr = rp_buf[0][ptr++] & RMASK;
    M[addr] = rp_buf[0][ptr];
    PC = addr;
    return SCPE_OK;
}

/* Device attach */

t_stat rp_attach (UNIT *uptr, CONST char *cptr)
{
    int32 drv, i, p;
    t_stat r;
    DEVICE *rptr;
    DIB *dib;
    int ctlr;

    uptr->capac = rp_drv_tab[GET_DTYPE (uptr->flags)].size;
    r = attach_unit (uptr, cptr);
    if (r != SCPE_OK)
        return r;
    rptr = find_dev_from_unit(uptr);
    if (rptr == 0)
        return SCPE_OK;
    dib = (DIB *) rptr->ctxt;
    ctlr = dib->dev_num & 014;
    uptr->u4 = 0;
    uptr->u3 &= ~DS_VV;
    uptr->u3 |= DS_DPR|DS_MOL|DS_DRY;
    if (uptr->flags & UNIT_WLK) 
         uptr->u3 |= DS_WRL;
    rp_df10[ctlr].status |= PI_ENABLE;
    set_interrupt(dib->dev_num, rp_df10[ctlr].status);
    return SCPE_OK;
}

/* Device detach */

t_stat rp_detach (UNIT *uptr)
{
    int32 drv;

    if (!(uptr->flags & UNIT_ATT))                          /* attached? */
        return SCPE_OK;
    if (sim_is_active (uptr))                              /* unit active? */
        sim_cancel (uptr);                                  /* cancel operation */
    uptr->u3 &= ~(DS_VV|DS_WRL|DS_DPR|DS_DRY);
    return detach_unit (uptr);
}

t_stat rp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "RP04/05/06/07 Disk Pack Drives (RP)\n\n");
fprintf (st, "The RP controller implements the Massbus family of large disk drives.  RP\n");
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

const char *rp_description (DEVICE *dptr)
{
    return "RP04/05/06/07 Massbus disk controller";
}


#endif
