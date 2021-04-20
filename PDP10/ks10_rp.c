/* ks10_rp.c: DEC Massbus RP04/5/6

   Copyright (c) 2021, Richard Cornwell

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

#include "kx10_defs.h"
#include "kx10_disk.h"

#ifndef NUM_DEVS_RP
#define NUM_DEVS_RP 0
#endif

#if (NUM_DEVS_RP > 0)
#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

#define RP_NUMWD        128     /* 36bit words/sec */
#define NUM_UNITS_RP    8

/* Flags in the unit flags word */

#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    7
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define DTYPE(x)        (((x) & UNIT_M_DTYPE) << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)

/* Parameters in the unit descriptor */
#define CMD      u3
/* u3  low */
/* RPC - 176700 - control */

#define CS1_GO          1               /* go */
#define CS1_V_FNC       1               /* function pos */
#define CS1_M_FNC       037             /* function mask */
#define CS1_FNC         (CS1_M_FNC << CS1_V_FNC)
#define  FNC_NOP        000             /* no operation */
#define  FNC_UNLOAD     001             /* unload */
#define  FNC_SEEK       002             /* seek */
#define  FNC_RECAL      003             /* recalibrate */
#define  FNC_DCLR       004             /* drive clear */
#define  FNC_RELEASE    005             /* port release */
#define  FNC_OFFSET     006             /* offset */
#define  FNC_RETURN     007             /* return to center */
#define  FNC_PRESET     010             /* read-in preset */
#define  FNC_PACK       011             /* pack acknowledge */
#define  FNC_SEARCH     014             /* search */
#define FNC_XFER        024             /* >=? data xfr */
#define  FNC_WCHK       024             /* write check */
#define  FNC_WCHKH      025             /* write check headers */
#define  FNC_WRITE      030             /* write */
#define  FNC_WRITEH     031             /* write w/ headers */
#define  FNC_READ       034             /* read */
#define  FNC_READH      035             /* read w/ headers */
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)
#define CS1_IE          0000100         /* Enable interrupts */
#define CS1_RDY         0000200         /* Drive ready */
#define CS1_UBA         0001400         /* High order UBA bits */
#define CS1_PSEL        0002000         /* */
#define CS1_DVA         0004000         /* drive avail */
#define CS1_MCPE        0020000         /* */
#define CS1_TRE         0040000         /* Set if CS2 0177400 */
#define CS1_SC          0100000         /* Set if TRE or ATTN */

#define CSX_BUSY        02              /* RH11 is doing a transfer */


/* RPWC - 176702 - word count */

/* RPBA - 176704 - base address */

#define DA           u4
/* u4 high */
/* RPDC - 176706 - desired sector */

#define DA_V_SC         16              /* sector pos */
#define DA_M_SC         077             /* sector mask */
#define DA_V_SF         24              /* track pos */
#define DA_M_SF         077             /* track mask */
#define DA_MBZ          0140300
#define GET_SC(x)       (((x) >> DA_V_SC) & DA_M_SC)
#define GET_SF(x)       (((x) >> DA_V_SF) & DA_M_SF)

/* RPCS2 - 176710 - control/status 2 */

#define CS2_V_UNIT      0               /* unit pos */
#define CS2_M_UNIT      07              /* unit mask */
#define CS2_UNIT        (CS2_M_UNIT << CS2_V_UNIT)
#define CS2_UAI         0000010         /* addr inhibit */
#define CS2_PAT         0000020         /* parity test NI */
#define CS2_CLR         0000040         /* controller clear */
#define CS2_IR          0000100         /* input ready */
#define CS2_OR          0000200         /* output ready */
#define CS2_MDPE        0000400         /* Mbus par err NI */
#define CS2_MXF         0001000         /* missed xfer NI */
#define CS2_PGE         0002000         /* program err */
#define CS2_NEM         0004000         /* nx mem err */
#define CS2_NED         0010000         /* nx drive err */
#define CS2_PE          0020000         /* parity err NI */
#define CS2_WCE         0040000         /* write check err */
#define CS2_DLT         0100000         /* data late NI */

/* RPDS - 176712 - drive status */
#define STATUS    us10

#define DS_DF5          0000001         /* Drive forward 5in/sec */
#define DS_DF20         0000002         /* Drive forward 20in/sec */
#define DS_DIGB         0000004         /* Drive inner gaurd band */
#define DS_GRV          0000010         /* Go reverse read */
#define DS_DL64         0000020         /* Difference less 64 */
#define DS_DE1          0000040         /* Difference equal 1 */
#define DS_VV           0000100         /* volume valid */
#define DS_DRY          0000200         /* drive ready */
#define DS_DPR          0000400         /* drive present */
#define DS_PGM          0001000         /* programable NI */
#define DS_LST          0002000         /* last sector */
#define DS_WRL          0004000         /* write locked */
#define DS_MOL          0010000         /* medium online */
#define DS_PIP          0020000         /* pos in progress */
#define DS_ERR          0040000         /* error */
#define DS_ATA          0100000         /* attention active */

/* u3 high */
/* RPER1 - 176714 - error status 1 */

#define ER1_ILF         0000001         /* illegal func */
#define ER1_ILR         0000002         /* illegal register */
#define ER1_RMR         0000004         /* reg mod refused */
#define ER1_PAR         0000010         /* parity err */
#define ER1_FER         0000020         /* format err NI */
#define ER1_WCF         0000040         /* write clk fail NI */
#define ER1_ECH         0000100         /* ECC hard err NI */
#define ER1_HCE         0000200         /* hdr comp err NI */
#define ER1_HCR         0000400         /* hdr CRC err NI */
#define ER1_AOE         0001000         /* addr ovflo err */
#define ER1_IAE         0002000         /* invalid addr err */
#define ER1_WLE         0004000         /* write lock err */
#define ER1_DTE         0010000         /* drive time err NI */
#define ER1_OPI         0020000         /* op incomplete */
#define ER1_UNS         0040000         /* drive unsafe */
#define ER1_DCK         0100000         /* data check NI */

/* RPAS - 176716 - attention summary */

#define AS_U0           0000001         /* unit 0 flag */

/* RPLA - 176720 - look ahead register */
#define LA_REG         u6
#define LA_V_SC         6               /* sector pos */

/* RPDB - 176722 - data buffer */

#define OF_HCI          0002000         /* hdr cmp inh NI */
#define OF_ECI          0004000         /* ECC inhibit NI */
#define OF_F22          0010000         /* format NI */
#define OF_MBZ          0161400

/* RPMR - 176724 - maintenace register */
#define PMR_DMD         0000001         /* Enable Diag mode */
#define PMR_DCLK        0000002         /* Diag Data Clock */
#define PMR_DIND        0000004         /* Diag Index Pulse */
#define PMR_DSCK        0000010         /* Diag Sector Clock */
#define PMR_DRDD        0000020         /* Diag Read Data */
#define PMR_DWRD        0000040         /* Diag Write Data */
#define PMR_ECE         0000100         /* In ECE field */
#define PMR_DFE         0000200         /* In Data Field */
#define PMR_ZD          0000400         /* After ECE field */
#define PMR_MSK         0000037         /* Mask bits in PMR */

/* RPDT - 176726 - drive type */


/* RPSN  - 176730 - serial number */

/* us10 */
/* RPOF  - 176732 - offset register */
#define RPOF_OFF        0000077         /* Offset value */
#define RPOF_OFNU       0000100
#define RPOF_OFD        0000200         /* Offset direction */
#define RPOF_HCI        0002000         /* Head compare inhibited */
#define RPOF_ECI        0004000         /* Error correct inhibit */
#define RPOF_FMT22      0010000         /* Format 22/20 sectors */
#define RPOF_SCG        0100000         /* Sign change */
/* u4 low */
/* RPDC  - 176734 - desired cylinder */
#define DC_V_CY         0               /* cylinder pos */
#define DC_M_CY         01777           /* cylinder mask */
#define DC_MBZ          0176000
#define GET_CY(x)       (((x) >> DC_V_CY) & DC_M_CY)
#define GET_DA(c,d)     ((((GET_CY (c) * rp_drv_tab[d].surf) + GET_SF (c)) \
                                       * rp_drv_tab[d].sect) + GET_SC (c))
#define CCYL          u5
/* u5 low */
/* RPCC  - 176736 - current cylinder */

/* RPER2 - 176740 - error status 2 - drive unsafe conditions - unimplemented */
/* RPER3 - 176742 - error status 3 - more unsafe conditions - unimplemented */
//#define ERR2          us9
/* us9 */
//#define ERR3          us10


#define DATAPTR       us9
/* RPEC1 - 176744 - ECC status 1 - unimplemented */
/* RPEC2 - 176746 - ECC status 2 - unimplemented */


/* This controller supports many different disk drive types.  These drives
   are operated in 576 bytes/sector (128 36b words/sector) mode, which gives
   them somewhat different geometry from the PDP-11 variants:

   type         #sectors/       #surfaces/      #cylinders/
                 surface         cylinder        drive

   RM03         30               5              823             =67MB
   RM05         30              19              823             =256MB
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

#define RM03_DTYPE      3
#define RM03_SECT       30
#define RM03_SURF       5
#define RM03_CYL        823
#define RM03_DEV        020024
#define RM03_SIZE       (RM03_SECT * RM03_SURF * RM03_CYL * RP_NUMWD)

#define RM05_DTYPE      4
#define RM05_SECT       30
#define RM05_SURF       19
#define RM05_CYL        823
#define RM05_DEV        020027
#define RM05_SIZE       (RM05_SECT * RM05_SURF * RM05_CYL * RP_NUMWD)

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
    { RM03_SECT, RM03_SURF, RM03_CYL, RM03_SIZE, RM03_DEV },
    { RM05_SECT, RM05_SURF, RM05_CYL, RM05_SIZE, RM05_DEV },
    { 0 }
    };


int           rp_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access);
int           rp_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access);
uint16        rp_vect(struct pdp_dib *dibp);
void          rp_setattn(UNIT *uptr);

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
uint64        rp_buf[RP_NUMWD];
uint16        rp_wc;
uint16        rp_dba;
uint16        rp_dbb;
t_addr        rp_ba;
uint16        rp_cs2;
uint8         rp_ie;
uint16        rp_err2;
uint16        rp_err3;
uint16        rp_rmr[NUM_UNITS_RP];


UNIT                rpa_unit[] = {
/* Controller 1 */
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE), RP06_SIZE) },
    { UDATA (&rp_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+
                 UNIT_ROABLE+DTYPE(RP06_DTYPE), RP06_SIZE) },
};

DIB rpa_dib = {0776700, 077, 0254, 6, 1, &rp_read, &rp_write, &rp_vect, 0};


MTAB                rp_mod[] = {
    { MTAB_XTD|MTAB_VUN, 0, "write enabled", "WRITEENABLED", 
        &set_writelock, &show_writelock,   NULL, "Write enable drive" },
    { MTAB_XTD|MTAB_VUN, 1, NULL, "LOCKED", 
        &set_writelock, NULL,   NULL, "Write lock drive" },
    {UNIT_DTYPE, (RP07_DTYPE << UNIT_V_DTYPE), "RP07", "RP07", &rp_set_type },
    {UNIT_DTYPE, (RP06_DTYPE << UNIT_V_DTYPE), "RP06", "RP06", &rp_set_type },
    {UNIT_DTYPE, (RP04_DTYPE << UNIT_V_DTYPE), "RP04", "RP04", &rp_set_type },
    {UNIT_DTYPE, (RM03_DTYPE << UNIT_V_DTYPE), "RM03", "RM03", &rp_set_type },
    {UNIT_DTYPE, (RM05_DTYPE << UNIT_V_DTYPE), "RM05", "RM05", &rp_set_type },
    {MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT", NULL, &disk_show_fmt },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "addr", "addr",  &uba_set_addr, uba_show_addr,
              NULL, "Sets address of RH11" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "vect", "vect",  &uba_set_vect, uba_show_vect,
              NULL, "Sets vect of RH11" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "br", "br",  &uba_set_br, uba_show_br,
              NULL, "Sets br of RH11" },
    {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "ctl", "ctl",  &uba_set_ctl, uba_show_ctl,
              NULL, "Sets br of RH11" },
    {0}
};

REG                 rpa_reg[] = {
    {ORDATA(WC, rp_wc, 16)},
    {ORDATA(BA, rp_ba, 18)},
    {ORDATA(UNIT, rp_cs2, 16)},
    {ORDATA(IE, rp_ie, 8), REG_HRO},
    {BRDATA(BUFF, rp_buf, 16, 64, RP_NUMWD), REG_HRO},
    {0}
};  

DEVICE              rpa_dev = {
    "RP", rpa_unit, rpa_reg, rp_mod,
    NUM_UNITS_RP, 8, 18, 1, 8, 36,
    NULL, NULL, &rp_reset, &rp_boot, &rp_attach, &rp_detach,
    &rpa_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &rp_help, NULL, NULL, &rp_description
};

int
rp_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access) {
    int         i;
    int         rp_unit = rp_cs2 & 07;
    UNIT       *uptr = &rpa_unit[rp_unit];
    int         dtype = GET_DTYPE(uptr->flags);
   
    switch(addr & 076) {
/* u3  low */
    case  000: /* RPC   - 176700 - control */
        sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o Status=%06o\n", rp_unit, uptr->CMD);
        if (rp_cs2 & CS2_PAT) {
            uba_set_parity(rpa_dib.uba_ctl);
            uptr->CMD |= (ER1_PAR << 16);
	    break;
        }
        if (access == BYTE && addr & 1)
           break;

        rp_ie &= ~(CS1_IE);
        rp_ie |= data & (CS1_IE);
        if ((uptr->CMD & CS1_GO) != 0) { 
            uptr->CMD |= (ER1_RMR << 16);
            sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o not ready %02o %06o\n", rp_unit,
                    addr & 077, data);
            return 0;
        }
        rp_ba = ((data << 8) & 0600000) | (rp_ba & 0177777);
        uptr->CMD &= ~077;
        uptr->CMD |= data & 076;
        /* Check if GO bit set */
        if ((data & 1) == 0) {
	   if (data & CS1_TRE)
               rp_cs2 &= (CS2_IR|CS2_OR|CS2_UAI|CS2_PAT|CS2_UNIT);
           sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o no go %06o\n", rp_unit, data);
           return 0;                           /* No, nop */
        }
        if ((uptr->flags & UNIT_ATT) == 0) {
           sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o unattached %06o\n", rp_unit, data);
           return 0;                           /* No, nop */
        }
        uba_clr_irq(&rpa_dib);
        uptr->CMD &= 0777;
        switch (GET_FNC(data)) {
        case FNC_NOP:
            break;

        case FNC_RECAL:                       /* recalibrate */
            uptr->DA &= ~0177777;
            uptr->STATUS |= DS_PIP;
            uptr->CMD |= CS1_GO;
            break;

        case FNC_SEEK:                        /* seek */
	    if (GET_FNC(data) == FNC_SEEK && 
	        GET_CY(uptr->DA) == (uptr->CCYL & 01777)) {
                uptr->STATUS |= DS_ATA;
                rp_setattn(uptr);
		break;
            }
             /* Fall through */

        case FNC_SEARCH:                      /* search */
	    if (rp_rmr[rp_unit] & 1) {
		int sect = rp_drv_tab[dtype].sect;
	        if (GET_CY(uptr->DA) != (uptr->CCYL & 01777))  {
                    uptr->STATUS |= DS_PIP;
		}
	        uptr->CCYL = (uptr->CCYL & (0177777 << 16)) | GET_CY(uptr->DA);
		if ((uptr->CCYL & (RPOF_FMT22 << 16)) != 0)
		    sect += 2;
                if ((GET_CY(uptr->DA) >= rp_drv_tab[dtype].cyl) ||
                    (GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf) ||
                    (GET_SC(uptr->DA) >= sect)) {
                    uptr->STATUS |= DS_ATA;
                    uptr->CMD |= (ER1_IAE << 16);
                    rp_setattn(uptr);
	            break;
	        }
	    } else
            if ((GET_CY(uptr->DA) >= rp_drv_tab[dtype].cyl) ||
                (GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf) ||
                (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect)) {
                uptr->STATUS |= DS_ATA;
                uptr->CMD |= (ER1_IAE << 16);
                rp_setattn(uptr);
		break;
	    }
             /* Fall through */

        case FNC_RETURN:                      /* return to center */
        case FNC_OFFSET:                      /* offset */
        case FNC_UNLOAD:                      /* unload */
        case FNC_WCHK:                        /* write check */
        case FNC_WRITE:                       /* write */
        case FNC_WRITEH:                      /* write w/ headers */
        case FNC_READ:                        /* read */
        case FNC_READH:                       /* read w/ headers */
	    if (GET_CY(uptr->DA) != (uptr->CCYL & 01777)) {
                uptr->STATUS |= DS_PIP;
            }
            if (GET_FNC(data) >= FNC_XFER) 
               rp_ie |= CSX_BUSY;
            uptr->CMD |= CS1_GO;
            CLR_BUF(uptr);
            uptr->DATAPTR = 0;
            break;


        case FNC_DCLR:                        /* drive clear */
            uptr->STATUS &= DS_VV;
            uptr->DA &= 003400177777;
            uptr->CCYL &= 0177777;
            rp_rmr[rp_unit] = 0;
	    uptr->CMD &= ~CS1_GO;
            break;

        case FNC_PRESET:                      /* read-in preset */
            uptr->DA = 0;
            uptr->CCYL &= 0177777;
             /* Fall through */
        case FNC_PACK:                        /* pack acknowledge */
            uptr->STATUS |= DS_VV;
             /* Fall through */
        case FNC_RELEASE:                     /* port release */
            break;

        default:
            uptr->STATUS |= DS_ATA;
            uptr->CMD |= (ER1_ILF << 16);
        }
        if (GET_FNC(data) >= FNC_XFER)
            uptr->STATUS &= (DS_VV|DS_PIP);
	rp_cs2 &= (CS2_IR|CS2_OR|CS2_UAI|CS2_PAT|CS2_UNIT);
        if ((uptr->CMD & CS1_GO) != 0 && ((rp_rmr[rp_unit] & 1) == 0))
            sim_activate(uptr, 1000);
        sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o AStatus=%06o %06o %06o\n", rp_unit, uptr->CMD, rp_cs2, uptr->STATUS);
        break;
     case 002: /* RPWC  - 176702 - word count */
        if ((rp_ie & (CSX_BUSY)) != 0) { 
            uptr->CMD |= (ER1_RMR << 16);
            sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o not ready %02o %06o\n", rp_unit,
                    addr & 077, data);
            return 0;
        }
        if (access == BYTE) {
            if (addr & 1)
                data = data | (rp_wc & 0377);
            else
                data = (rp_wc & 0177600) | data;
        }
        rp_wc = data;
        break;
     case 004: /* RPBA  - 176704 - base address */
        if ((rp_ie & (CSX_BUSY)) != 0) { 
            uptr->CMD |= (ER1_RMR << 16);
            sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o not ready %02o %06o\n", rp_unit,
                    addr & 077, data);
            return 0;
        }
        if (access == BYTE) {
            if (addr & 1)
                data = data | (rp_ba & 0377);
            else
                data = (rp_ba & 0177600) | data;
        }
        rp_ba = (rp_ba & 0600000) | (data & 0177776);
        break;
    case  006:  /* RPDC  - 176706 - desired sector */
        if (access == BYTE) {
            if (addr & 1)
                data = data | ((uptr->DA >> 16) & 0377);
            else
                data = ((uptr->DA >> 16) & 0177600) | data; 
        }
        uptr->DA &= 0177777;
        uptr->DA |= data << 16;
        break;

    case  010: /* RPCS2 - 176710 - Control and Status register 2 */
        if (access == BYTE) {
            if (addr & 1)
               data = data | (rp_cs2 & 0377);
        }
        rp_cs2 = ((CS2_DLT|CS2_WCE|CS2_NED|CS2_NEM|CS2_PGE|CS2_MDPE) & rp_cs2) |
		  ((CS2_UAI|CS2_PAT|CS2_UNIT) & data);
        if (data & CS2_CLR) {
            rp_reset(&rpa_dev);
        }
        rp_cs2 |= CS2_IR;
        rp_unit = rp_cs2 & 07;
        if (rpa_unit[rp_cs2 & CS2_UNIT].flags & UNIT_DIS)
            rp_cs2 |= CS2_NED;
        break;

    case  012:  /* RPDS  - 176712 - drive status */
        break;

    case  014: /* RPER1 - 176714 - error status 1 */
        if (access == BYTE) {
            if (addr & 1)
                data = data | ((uptr->CMD >> 16) & 0377);
            else
                data = ((uptr->CMD >> 16) & 0177600) | data;
        }
        uptr->CMD &= 0177777;
        uptr->CMD |= data << 16;
        break;

    case  016:  /* RPAS  - 176716 - attention summary */
        if (access == BYTE && addr & 1)
            break;
        for (i = 0; i < 8; i++) {
            if (data & (1<<i)) {
                UNIT   *u = &rpa_unit[i];
                u->STATUS &= ~DS_ATA;
            }
        }
        break;

    case  024:  /* RPMR  - 176724 - maintenace register */
	/* LA_REG has 10 bits for position in sector */
        if (data & 1) {
	    if ((data & 076) == 0) {
		if (rp_rmr[rp_unit] & 010)
		   uptr->LA_REG ++;
	        if (rp_rmr[rp_unit] & 04)
		   uptr->LA_REG = 0;
	    }
            rp_rmr[rp_unit] = data;
	} else
            rp_rmr[rp_unit] = 0;
        break;
    case  020:  /* RPLA  - 176720 - look ahead register */
    case  026:  /* RPDT  - 176726 - drive type */
        break;
    case  022: /* RPDB  - 176722 - data buffer */
	if ((rp_cs2 & CS2_IR) == 0) {
	    rp_cs2 |= CS2_DLT;
	    break;
	}
	rp_dba = rp_dbb;
        rp_dbb = data;
	if (rp_cs2 & CS2_IR)
	    rp_dba = rp_dbb;
        rp_cs2 |= CS2_OR;
        rp_cs2 &= ~CS2_IR;
        break;
    case  032:  /* RPOF  - 176732 - offset register */
        uptr->CCYL &= 0177777;
        uptr->CCYL |= data << 16;
        break;
    case  034:  /* RPDC  - 176734 - desired cylinder */
        if ((uptr->CMD & CS1_GO) != 0) { 
            uptr->CMD |= (ER1_RMR << 16);
            sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o not ready %02o %06o\n", rp_unit,
                    addr & 077, data);
            break;
        }
        if (access == BYTE) {
            if (addr & 1)
                data = data | (uptr->DA & 0377);
            else
                data = (uptr->DA & 0177600) | data;
        }
        uptr->DA &= ~0177777;
        uptr->DA |= data;
        break;
    case  040:  /* RPER2 - 176740 - error status 2 - drive unsafe conditions */
        rp_err2 = data;
        break;
    case  042:  /* RPER3 - 176742 - error status 3 - more unsafe conditions */
        rp_err3 = data;
        break;
    case  030:  /* RPSN  - 176730 - serial number */
    case  036:  /* RPCC  - 176736 - current cylinder */
    case  044:  /* RPEC1 - 176744 - ECC status 1 - unimplemented */
    case  046:  /* RPEC2 - 176746 - ECC status 2 - unimplemented */
        break;
    }
    sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o write %02o %06o\n", rp_unit,
             addr & 076, data);
    return 0;
}

int
rp_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access) {
    int         rp_unit = rp_cs2 & 07;
    UNIT       *uptr = &rpa_unit[rp_unit];
    uint16      temp = 0;
    int         i;

    switch(addr & 076) {
    case  000:  /* RPC   - 176700 - control */
        temp = uptr->CMD & 077;
        temp |= (uint16)(rp_ie & (CS1_IE));
        temp |= (rp_ba & 0600000) >> 8;
        if ((rp_ie & CSX_BUSY) == 0)
           temp |= CS1_RDY;
        if (uptr->flags & UNIT_ATT)
           temp |= CS1_DVA;
        if (rp_cs2 & (CS2_MDPE|CS2_MXF|CS2_PGE|CS2_NEM|CS2_NED|CS2_PE|CS2_WCE|CS2_DLT))
           temp |= CS1_TRE|CS1_SC;
        for (i = 0; i < 8; i++) {
            UNIT   *u = &rpa_unit[i];
            if (u->STATUS & DS_ATA) {
                temp |= CS1_SC;
                break;
            }
        }
        break;
    case  002:  /* RPWC  - 176702 - word count */
        temp = rp_wc;
        break;
    case  004:  /* RPBA  - 176704 - base address */
        temp = (uint16)(rp_ba & 0177776);
        break;
    case  006:  /* RPDC  - 176706 - desired sector */
        temp = (uptr->DA >> 16) & 0177777;
        break;
    case  010:  /* RPCS2 - 176710 - control/status 2 */
        temp = rp_cs2;
        break;
    case  012:  /* RPDS  - 176712 - drive status */
        temp = uptr->STATUS;
        if (((uptr->CMD >> 16) & 0177777) != 0 || rp_err2 != 0 || rp_err3 != 0)
           temp |= DS_ERR;
        if ((uptr->flags & UNIT_DIS) == 0)
           temp |= DS_DPR;
        if ((uptr->flags & UNIT_ATT) != 0)
           temp |= DS_MOL;
        if ((uptr->flags & UNIT_WPRT) != 0)
           temp |= DS_WRL;
        if ((uptr->CMD & CS1_GO) == 0)
           temp |= DS_DRY;
        break;
    case  014:  /* RPER1 - 176714 - error status 1 */
        temp = (uptr->CMD >> 16) & 0177777;
        break;
    case  016:  /* RPAS  - 176716 - attention summary */
        for (i = 0; i < 8; i++) {
            UNIT   *u = &rpa_unit[i];
            if (u->STATUS & DS_ATA) {
                temp |= 1 << i;
            }
        }
        break;
    case  020:  /* RPLA  - 176720 - look ahead register */
	if ((uptr->LA_REG >> 10) >= 23)
	    uptr->LA_REG = 0;
	temp = GET_SC(uptr->DA) << 6;
        temp ^= ((uptr->LA_REG + 1) >> 4) & 07760;
	if ((rp_rmr[rp_unit] & 1) == 0)
	    uptr->LA_REG+=1024;
        break;
    case  022: /* RPDB  - 176722 - data buffer */
	if ((rp_cs2 & CS2_OR) == 0) {
	    rp_cs2 |= CS2_DLT;
	    break;
	}
        temp = rp_dba;
	rp_dba = rp_dbb;
        rp_cs2 &= ~CS2_OR;
        rp_cs2 |= CS2_IR;
        break;
    case  024:  /* RPMR  - 176724 - maintenace register */
	if (rp_rmr[rp_unit] & 1)
            temp = rp_rmr[rp_unit] & PMR_MSK;
        break;
    case  026:  /* RPDT  - 176726 - drive type */
        temp = rp_drv_tab[GET_DTYPE(uptr->flags)].devtype;
        break;
    case  030:  /* RPSN  - 176730 - serial number */
        temp = (rp_unit + 1);
        break;
    case  032:  /* RPOF  - 176732 - offset register */
        temp = (uptr->CCYL >> 16) & 0177777;
        break;
    case  034:  /* RPDC  - 176734 - desired cylinder */
        temp = GET_CY(uptr->DA);
        break;
    case  036:  /* RPCC  - 176736 - current cylinder */
        temp = uptr->CCYL & 0177777;
        break;
    case  040:  /* RPER2 - 176740 - error status 2 - drive unsafe conditions */
        temp = rp_err2;
        break;
    case  042:  /* RPER3 - 176742 - error status 3 - more unsafe conditions */
        temp = rp_err3;
        break;
    case  044:  /* RPEC1 - 176744 - ECC status 1 - unimplemented */
    case  046:  /* RPEC2 - 176746 - ECC status 2 - unimplemented */
        break;
    }
    *data = temp;
    sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o read %02o %06o %06o\n", rp_unit,
             addr & 076, temp, PC);
    if (rp_cs2 & CS2_PAT) {
        uba_set_parity(rpa_dib.uba_ctl);
        uptr->CMD |= (ER1_PAR << 16);
    }
    return 0;
}

uint16
rp_vect(struct pdp_dib *dibp)
{
    return dibp->uba_vect;
}

/* Set the attention flag for a unit */
void rp_setattn(UNIT *uptr)
{
    uptr->STATUS |= DS_ATA;
    uptr->CMD &= ~CS1_GO;
    if ((rp_ie & CSX_BUSY) == 0 && (rp_ie & CS1_IE) != 0)
        uba_set_irq(&rpa_dib);
}



t_stat rp_svc (UNIT *uptr)
{
    int           dtype = GET_DTYPE(uptr->flags);
    int           cyl = GET_CY(uptr->DA);
    int           unit;
    DEVICE       *dptr;
    int           diff, da;
    int           sts;
    uint64        buf;

    dptr = uptr->dptr;
    unit = uptr - dptr->units;
    if ((uptr->flags & UNIT_ATT) == 0) {                 /* not attached? */
        uptr->CMD |= (ER1_UNS << 16);                    /* set drive error */
        uptr->STATUS |= DS_ATA;
        rp_ie &= ~(CSX_BUSY);
        uptr->CMD &= ~CS1_GO;
        if (GET_FNC(uptr->CMD) >= FNC_XFER) {             /* xfr? set done */
            if ((rp_ie & CSX_BUSY) == 0 && (rp_ie & CS1_IE) != 0)
                uba_set_irq(&rpa_dib);
        } else {
            rp_setattn(uptr);
        }
        return (SCPE_OK);
    }

    /* Check if seeking */
    if (uptr->STATUS & DS_PIP) {
        sim_debug(DEBUG_DETAIL, dptr, "%s%o seek %d %d\n", dptr->name, unit, cyl, uptr->CCYL);
        if (cyl >= rp_drv_tab[dtype].cyl) {
            uptr->STATUS &= ~DS_PIP;
            sim_activate(uptr, 10);
	    return SCPE_OK;
        }
        diff = cyl - (uptr->CCYL & 01777);
        if (diff < 0) {
            if (diff < -50) {
                uptr->CCYL -= 50;
                sim_activate(uptr, 500);
            } else if (diff < -10) {
                uptr->CCYL -= 10;
                sim_activate(uptr, 200);
            } else {
                uptr->CCYL -= 1;
                sim_activate(uptr, 100);
            }
            return SCPE_OK;
        } else if (diff > 0) {
            if (diff > 50) {
                uptr->CCYL += 50;
                sim_activate(uptr, 500);
            } else if (diff > 10) {
                uptr->CCYL += 10;
                sim_activate(uptr, 200);
            } else {
                uptr->CCYL += 1;
                sim_activate(uptr, 100);
            }
            return SCPE_OK;
        } else {
            uptr->STATUS &= ~DS_PIP;
            uptr->DATAPTR = 0;
        }
    }

    /* Bump sector counter */
    switch (GET_FNC(uptr->CMD)) {
    case FNC_NOP:
    case FNC_DCLR:                       /* drive clear */
    case FNC_RELEASE:                    /* port release */
    case FNC_PACK:                       /* pack acknowledge */
        break;
    case FNC_UNLOAD:                     /* unload */
        rp_detach(uptr);
        /* Fall through */
    case FNC_OFFSET:                     /* offset */
        /* Fall through */
    case FNC_RETURN:                     /* return to center */
    case FNC_PRESET:                     /* read-in preset */
    case FNC_RECAL:                      /* recalibrate */
    case FNC_SEEK:                       /* seek */
        if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect || 
            GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf ||
	    GET_CY(uptr->DA) >= rp_drv_tab[dtype].cyl)
            uptr->CMD |= (ER1_IAE << 16);
        rp_setattn(uptr);
        sim_debug(DEBUG_DETAIL, dptr, "%s%o seekdone %d %o\n", dptr->name, unit, cyl, uptr->CMD);
        break;

    case FNC_SEARCH:                     /* search */
        if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect ||
            GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf ||
	    GET_CY(uptr->DA) >= rp_drv_tab[dtype].cyl)
            uptr->CMD |= (ER1_IAE << 16);
        rp_setattn( uptr);
        sim_debug(DEBUG_DETAIL, dptr, "%s%o searchdone %d %o\n", dptr->name, unit, cyl, uptr->CMD);
        break;

    case FNC_READ:                       /* read */
    case FNC_READH:                      /* read w/ headers */
    case FNC_WCHK:                       /* write check */

        if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect ||
            GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf) {
            uptr->CMD |= (ER1_IAE << 16);
            uptr->STATUS |= DS_ATA;
            rp_ie &= ~(CSX_BUSY);
            uptr->CMD &= ~CS1_GO;
            if (rp_ie & CS1_IE)
                uba_set_irq(&rpa_dib);
            sim_debug(DEBUG_DETAIL, dptr, "%s%o readx done\n", dptr->name, unit);
            return SCPE_OK;
        }
        sim_debug(DEBUG_DETAIL, dptr, "%s%o read (%d,%d,%d)\n", dptr->name, unit, cyl,
               GET_SF(uptr->DA), GET_SC(uptr->DA));
        da = GET_DA(uptr->DA, dtype);
        (void)disk_read(uptr, &rp_buf[0], da, RP_NUMWD);
        uptr->hwmark = RP_NUMWD;
        uptr->DATAPTR = 0;
        sts = 1;
        /* On read headers, transfer 2 words to start */
        if (GET_FNC(uptr->CMD) == FNC_READH) {
            buf = (((uint64)cyl) << 18) | 
                     ((uint64)((GET_SF(uptr->DA) << 8) | GET_SF(uptr->DA)));
            sim_debug(DEBUG_DATA, dptr, "%s%o read word h1 %012llo %09o %06o\n",
               dptr->name, unit, buf, rp_ba, rp_wc);
            if ((sts = uba_write_npr(rp_ba, rpa_dib.uba_ctl, buf)) == 0) 
                goto rd_end;
            if ((rp_cs2 & CS2_UAI) == 0)
                rp_ba += 4;
            rp_wc = (rp_wc + 2) & 0177777;
            if (rp_wc == 0) {
                sts = 0;
                goto rd_end;
            }
            buf = (((uint64)(unit + 1)) << 18) | (uint64)(unit);
            sim_debug(DEBUG_DATA, dptr, "%s%o read word h2 %012llo %09o %06o\n",
               dptr->name, unit, buf, rp_ba, rp_wc);
            if ((sts = uba_write_npr(rp_ba, rpa_dib.uba_ctl, buf)) == 0)
                goto rd_end;
            if ((rp_cs2 & CS2_UAI) == 0)
                rp_ba += 4;
            rp_wc = (rp_wc + 2) & 0177777;
            if (rp_wc == 0) {
                sts = 0;
                goto rd_end;
            }
        }

        while (uptr->DATAPTR < RP_NUMWD && sts != 0) {
            buf = rp_buf[uptr->DATAPTR++];
            sim_debug(DEBUG_DATA, dptr, "%s%o read word %d %012llo %09o %06o\n",
                   dptr->name, unit, uptr->DATAPTR, buf, rp_ba, rp_wc);
            sts = uba_write_npr(rp_ba, rpa_dib.uba_ctl, buf);
            if ((rp_cs2 & CS2_UAI) == 0)
                rp_ba += 4;
            rp_wc = (rp_wc + 2) & 0177777;
            if (rp_wc == 0) {
                sts = 0;
                goto rd_end;
            }
        }

        if (sts) {
            /* Increment to next sector. Set Last Sector */
            uptr->DATAPTR = 0;
            CLR_BUF(uptr);
            uptr->DA += 1 << DA_V_SC;
            if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect) {
                uptr->DA &= (DA_M_SF << DA_V_SF) | (DC_M_CY << DC_V_CY);
                uptr->DA += 1 << DA_V_SF;
                if (GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf) {
                     uptr->DA &= (DC_M_CY << DC_V_CY);
                     uptr->DA += 1 << DC_V_CY;
                     uptr->STATUS |= DS_PIP;
                }
            }
            uptr->LA_REG = GET_SC(uptr->DA) << 10;
            sim_activate(uptr, 300);
            return SCPE_OK;
        }
rd_end:
        uptr->LA_REG = GET_SC(uptr->DA) << 10;
        sim_debug(DEBUG_DETAIL, dptr, "%s%o read done\n", dptr->name, unit);
        uptr->CMD &= ~CS1_GO;
        rp_ie &= ~(CSX_BUSY);
        if (rp_ie & CS1_IE)
            uba_set_irq(&rpa_dib);
        return SCPE_OK;

    case FNC_WRITE:                      /* write */
    case FNC_WRITEH:                     /* write w/ headers */
        if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect ||
            GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf) {
            uptr->CMD |= (ER1_IAE << 16);
            uptr->STATUS |= DS_ATA;
            rp_ie &= ~(CSX_BUSY);
            uptr->CMD &= ~CS1_GO;
            if (rp_ie & CS1_IE)
                uba_set_irq(&rpa_dib);
            sim_debug(DEBUG_DETAIL, dptr, "%s%o writex done\n", dptr->name, unit);
            return SCPE_OK;
        }
        sts = 1;
        /* On Write headers, transfer 2 words to start */
        if (GET_FNC(uptr->CMD) == FNC_WRITEH) {
            if (uba_read_npr(rp_ba, rpa_dib.uba_ctl, &buf) == 0)
                goto wr_done;
            if ((rp_cs2 & CS2_UAI) == 0)
                rp_ba += 4;
            rp_wc = (rp_wc + 2) & 0177777;
            if (rp_wc == 0) {
                sts = 0;
                goto wr_done;
            }
            sim_debug(DEBUG_DATA, dptr, "%s%o write word h1 %012llo %07o\n",
                  dptr->name, unit, buf, rp_wc);
            if (uba_read_npr(rp_ba, rpa_dib.uba_ctl, &buf) == 0)
                goto wr_done;
            if ((rp_cs2 & CS2_UAI) == 0)
                rp_ba += 4;
            rp_wc = (rp_wc + 2) & 0177777;
            if (rp_wc == 0) {
                sts = 0;
                goto wr_done;
            }
            sim_debug(DEBUG_DATA, dptr, "%s%o write word h2 %012llo %07o\n",
                  dptr->name, unit, buf, rp_wc);
        }
        uptr->DATAPTR = 0;
        uptr->hwmark = 0;
        buf = 0;
        while (uptr->DATAPTR < RP_NUMWD) {
            if ((sts = uba_read_npr(rp_ba, rpa_dib.uba_ctl, &buf)) == 0)
                break;
            rp_buf[uptr->DATAPTR++] = buf;
            sim_debug(DEBUG_DATA, dptr, "%s%o write word %d %012llo %07o %06o\n",
                     dptr->name, unit, uptr->DATAPTR, buf, rp_ba, rp_wc);
            if ((rp_cs2 & CS2_UAI) == 0)
                rp_ba += 4;
            rp_wc = (rp_wc + 2) & 0177777;
            if (rp_wc == 0) {
                sts = 0;
                goto wr_done;
            }
        }
wr_done:
        while (uptr->DATAPTR < RP_NUMWD) {
            rp_buf[uptr->DATAPTR++] = 0;
        }
        sim_debug(DEBUG_DETAIL, dptr, "%s%o write (%d,%d,%d)\n", dptr->name,
               unit, cyl, GET_SF(uptr->DA), GET_SC(uptr->DA));
        da = GET_DA(uptr->DA, dtype);
        (void)disk_write(uptr, &rp_buf[0], da, RP_NUMWD);
        uptr->DATAPTR = 0;
        CLR_BUF(uptr);
        uptr->DA += 1 << DA_V_SC;
        if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect) {
            uptr->DA &= (DA_M_SF << DA_V_SF) | (DC_M_CY << DC_V_CY);
            uptr->DA += 1 << DA_V_SF;
            if (GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf) {
                 uptr->DA &= (DC_M_CY << DC_V_CY);
                 uptr->DA += 1 << DC_V_CY;
                 uptr->STATUS |= DS_PIP;
            }
        }

        uptr->LA_REG = GET_SC(uptr->DA) << 10;
        if (sts) {
            sim_activate(uptr, 300);
        } else {
            sim_debug(DEBUG_DETAIL, dptr, "RP%o write done\n", unit);
            uptr->STATUS &= ~DS_PIP;
            rp_ie &= ~(CSX_BUSY);
            uptr->CMD &= ~CS1_GO;
            if (rp_ie & CS1_IE)
                uba_set_irq(&rpa_dib);
        }
        return SCPE_OK;
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
rp_reset(DEVICE * dptr)
{
    int   i;
    rp_ba = 0;
    rp_wc = 0;
    rp_ie = 0;
    rp_err2 = 0;
    rp_err3 = 0;
    rp_cs2 = CS2_IR;
    for (i = 0; i < 8; i++) {
        UNIT   *u = &rpa_unit[i];
        u->STATUS &= DS_VV;
        u->CMD &= 0177776;
        u->CCYL &= ((OF_HCI|OF_ECI|OF_F22) << 16) | 0177777;
	if (rp_rmr[i] & 1) {
	    u->CCYL = GET_CY(u->DA) | (((OF_HCI|OF_ECI|OF_F22) << 16) & u->CCYL); 
	}
        rp_rmr[i] = 0;
    }
    uba_clr_irq(&rpa_dib);
    sim_debug(DEBUG_DETAIL, dptr, "RP reset done\n");
    return SCPE_OK;
}

/* Boot from given device */
t_stat
rp_boot(int32 unit_num, DEVICE * rptr)
{
    UNIT         *uptr = &rptr->units[unit_num];
    int           dtype = GET_DTYPE(uptr->flags);
    DEVICE       *dptr = uptr->dptr;
    uint32        addr;
    uint32        ptr = 0;
    uint64        len;
    int           i;
    int           da;
    t_stat        r;

    if ((r = rp_reset(dptr) )!= SCPE_OK)
        return r;
    /* Read in block 1 and see if it is a home block */
    disk_read(uptr, &rp_buf[0], 1, RP_NUMWD);
    if (rp_buf[0] != 0505755000000LL) {
        /* Try blocks 10 and 12 if fail */
        disk_read(uptr, &rp_buf[0], 010, RP_NUMWD);
        if (rp_buf[0] != 0505755000000LL) {
           disk_read(uptr, &rp_buf[0], 012, RP_NUMWD);
           if (rp_buf[0] != 0505755000000LL)
              return SCPE_IERR;
        }
    }

    /* Word 103 and 102 contain pointer to SMFILE block */
    uptr->DA = (int32)((rp_buf[0103] & 077) << DA_V_SC) | 
               (int32)(((rp_buf[0103] >> 8) & 077) << DA_V_SF) |
               (int32)((rp_buf[0103] >> 24) << DC_V_CY);
    len = (int)(rp_buf[0102] & RMASK);
    da = GET_DA(uptr->DA, dtype);
    disk_read(uptr, &rp_buf[0], da, RP_NUMWD);
    /* For diagnostics use locations 6 and 7 */
    if (sim_switches & SWMASK ('D')) {   
       sim_messagef(SCPE_OK, "Diags boot\n");
       uptr->DA = (int32)((rp_buf[06] & 077) << DA_V_SC) | 
                  (int32)(((rp_buf[06] >> 8) & 077) << DA_V_SF) |
                  (int32)((rp_buf[06] >> 24) << DC_V_CY);
       len = (int)(((rp_buf[07] & 077) * 4) & RMASK);
    } else {
    /* Normal is at 4 and 5*/
       uptr->DA = (int32)((rp_buf[04] & 077) << DA_V_SC) | 
                  (int32)(((rp_buf[04] >> 8) & 077) << DA_V_SF) |
                  (int32)((rp_buf[04] >> 24) << DC_V_CY);
       len = (int)(((rp_buf[05] & 077) * 4) & RMASK);
    }

    /* Read len sectors into address 1000 */
    addr = 01000;
    for (; len > 0; len--) {
        da = GET_DA(uptr->DA, dtype);
        disk_read(uptr, &rp_buf[0], da, RP_NUMWD);
        for (i = 0; i < RP_NUMWD; i++) {
            M[addr++] = rp_buf[i];
        }
        uptr->DA += 1 << DA_V_SC;
        if (GET_SC(uptr->DA) >= rp_drv_tab[dtype].sect) {
            uptr->DA &= (DA_M_SF << DA_V_SF) | (DC_M_CY << DC_V_CY);
            uptr->DA += 1 << DA_V_SF;
            if (GET_SF(uptr->DA) >= rp_drv_tab[dtype].surf) {
                 uptr->DA &= (DC_M_CY << DC_V_CY);
                 uptr->DA += 1 << DC_V_CY;
            }
        }
    }
    /* Start location, and set up load infor */
    PC = 01000;
    M[036] = rpa_dib.uba_addr | (rpa_dib.uba_ctl << 18);
    M[037] =  unit_num;
    return cty_reset(&cty_dev);
}

/* Device attach */

t_stat rp_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    DEVICE *rptr;
    DIB *dib;

    uptr->capac = rp_drv_tab[GET_DTYPE (uptr->flags)].size;
    r = disk_attach (uptr, cptr);
    if (r != SCPE_OK)
        return r;
    rptr = find_dev_from_unit(uptr);
    if (rptr == 0)
        return SCPE_OK;
    dib = (DIB *) rptr->ctxt;
    if (sim_switches & SIM_SW_REST)
        return SCPE_OK;
    uptr->DA = 0;
    uptr->STATUS = DS_ATA;
    if ((rp_ie & CSX_BUSY) == 0 && (rp_ie & CS1_IE) != 0)
        uba_set_irq(&rpa_dib);
    return SCPE_OK;
}

/* Device detach */

t_stat rp_detach (UNIT *uptr)
{
    if (!(uptr->flags & UNIT_ATT))                          /* attached? */
        return SCPE_OK;
    if (sim_is_active (uptr))                              /* unit active? */
        sim_cancel (uptr);                                  /* cancel operation */
    uptr->STATUS = 0;
    return disk_detach (uptr);
}

t_stat rp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "RP04/05/06/07 Disk Pack Drives (RP)\n\n");
fprintf (st, "The RP controller implements the Massbus family of large disk drives.  RP\n");
fprintf (st, "options include the ability to set units write enabled or write locked, to\n");
fprintf (st, "set the drive type to one of six disk types or autosize, and to write a DEC\n");
fprintf (st, "standard 044 compliant bad block table on the last track.\n\n");
disk_attach_help(st, dptr, uptr, flag, cptr);
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
