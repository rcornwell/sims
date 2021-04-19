/* ks10_tu.c: DEC Massbus TM03/TU10 tape controller

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
   FITNESS FOR A PARTICULAR PUTUOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "kx10_defs.h"
#include "sim_tape.h"

#ifndef NUM_DEVS_TU
#define NUM_DEVS_TU 0
#endif

#if (NUM_DEVS_TU > 0)

#define NUM_UNITS_TU    4
#define TU_NUMFR        (64*1024)

#define BUF_EMPTY(u)  (u->hwmark == 0xFFFFFFFF)
#define CLR_BUF(u)     u->hwmark = 0xFFFFFFFF

/* Flags in the unit flags word */

#define TU_UNIT         UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE

#define CMD             u3
/* u3  low */
/* TUC - 772440 - control */

#define CS1_GO          1               /* go */
#define CS1_V_FNC       1               /* function pos */
#define CS1_M_FNC       037             /* function mask */
#define CS1_FNC         (CS1_M_FNC << CS1_V_FNC)
#define  FNC_NOP        000             /* no operation */
#define  FNC_UNLOAD     001             /* unload */
#define  FNC_REWIND     003             /* rewind */
#define  FNC_DCLR       004             /* drive clear */
#define  FNC_PRESET     010             /* read-in preset */
#define  FNC_ERASE      012             /* Erase */
#define  FNC_WTM        013             /* Write Tape Mark */
#define  FNC_SPACEF     014             /* Space record forward */
#define  FNC_SPACEB     015             /* Space record backward */
#define FNC_XFER        024             /* >=? data xfr */
#define  FNC_WCHK       024             /* write check */
#define  FNC_WCHKREV    027             /* write check reverse */
#define  FNC_WRITE      030             /* write */
#define  FNC_READ       034             /* read */
#define  FNC_READREV    037             /* read reverse */
#define CS1_IE          0000100         /* Enable interrupts */
#define CS1_RDY         0000200         /* Drive ready */
#define CS1_UBA         0001400         /* High order UBA bits */
#define CS1_PSEL        0002000         /* */
#define CS1_DVA         0004000         /* drive avail NI */
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)
#define CS1_MCPE        0020000         /* */
#define CS1_TRE         0040000         /* */
#define CS1_SC          0100000         /* */

#define CSX_BUSY        02              /* RH11 is doing a transfer */

/* TUWC - 772442 - word count. */

/* TUBA - 772444 - bus address */

/* TUFC - 772446 - Frame count */

/* TUCS2 -772450 - Control Status 2 */
#define CS2_V_UNIT      0               /* unit pos */
#define CS2_M_UNIT      07              /* unit mask */
#define CS2_UNIT        (CS2_M_UNIT << CS2_V_UNIT)
#define CS2_UAI         0000010         /* addr inhibit */
#define CS2_PAT         0000020         /* parity test NI */
#define CS2_CLR         0000040         /* controller clear */
#define CS2_IR          0000100         /* input ready */
#define CS2_OR          0000200         /* output ready */
#define CS2_MDPE        0000400         /* Mbus par err NI  set TRE*/
#define CS2_MXF         0001000         /* missed xfer NI  set TRE*/
#define CS2_PGE         0002000         /* program err  set TRE*/
#define CS2_NEM         0004000         /* nx mem err  set TRE*/
#define CS2_NED         0010000         /* nx drive err set TRE */
#define CS2_PE          0020000         /* parity err NI set TRE */
#define CS2_WCE         0040000         /* write check err set TRE */
#define CS2_DLT         0100000         /* data late NI set TRE */

#define STATUS          u5
/* u5  low */
/* TUDS - 772452 - drive status */

#define DS_SLA          0000001         /* Drive has become ready */
#define DS_BOT          0000002         /* Beginning of tape */
#define DS_TM           0000004         /* Tape mark */
#define DS_IDB          0000010         /* Identification burst */
#define DS_SDWN         0000020         /* Tape stoped */
#define DS_PES          0000040         /* Phase Encoding */
#define DS_SSC          0000100         /* Status change */
#define DS_DRY          0000200         /* drive ready */
#define DS_DPR          0000400         /* drive present */
#define DS_PGM          0001000         /* programable NI */
#define DS_EOT          0002000         /* end of tape */
#define DS_WRL          0004000         /* write locked */
#define DS_MOL          0010000         /* medium online */
#define DS_PIP          0020000         /* pos in progress */
#define DS_ERR          0040000         /* error */
#define DS_ATA          0100000         /* attention active */

/* u5 high */
/* TUER1 - 772454 - error status 1 */

#define ER1_ILF         0000001         /* illegal func */
#define ER1_ILR         0000002         /* illegal register */
#define ER1_RMR         0000004         /* reg mod refused */
#define ER1_PAR         0000010         /* control parity err NI */
#define ER1_FMT         0000020         /* format err */
#define ER1_DPAR        0000040         /* data parity error */
#define ER1_INC         0000100         /* Incorrectable data */
#define ER1_PEF         0000200         /* format error */
#define ER1_NSG         0000400         /* Nonstandard gap NI */
#define ER1_FCE         0001000         /* Frame count error */
#define ER1_ITM         0002000         /* Illegal tape mark */
#define ER1_NEF         0004000         /* Non executable function */
#define ER1_DTE         0010000         /* drive time err NI */
#define ER1_OPI         0020000         /* op incomplete */
#define ER1_UNS         0040000         /* drive unsafe */
#define ER1_DCK         0100000         /* data check NI */

/* TULA - 772460 - Check Character */

/* TUDB - 772462 - Data buffer */

/* TUMR - 772464 - maintenace register */

/* TUAS - 772456 - attention summary */

#define AS_U0           0000001         /* unit 0 flag */

/* TUDT - 772466 - drive type */

/* TUSN  - 772470 - serial number */

/* TUTC  - 772472 - Tape control register */
#define TC_SS     0000007                    /* Slave select mask */
#define TC_EVPAR  0000010                    /* Even parity */
#define TC_FMTSEL 0000360                    /* Format select */
#define TC_10CORE 000                        /* PDP 10 Core */
   /* 4 8 bit chars + 1 4 bit char */
#define TC_15CORE 001                        /* PDP 15 core */
   /* 3 6 bit chars per word */
#define TC_10NORM 003                        /* PDP 10 Compatible */
   /* 4 8 bit chars per word */
#define TC_11NORM 014                        /* PDP 11 Normal */
   /* 2 8 bit chars per word */
#define TC_11CORE 015                        /* PDP 11 Core */
   /* 4 4 bit chars per word */
#define TC_15NORM 016                        /* PDP 15 Normal */
   /* 2 8 bit chars per word */
#define TC_DENS   0003400                    /* Density (ignored) */
#define TC_800    0001400                    /* 800 BPI */
#define TC_1600   0002000                    /* 1600 BPI */
#define TC_EAODTE 0010000                    /* Enable abort */
#define TC_SAC    0020000                    /* Slave address change */
#define TC_FCS    0040000                    /* Frame count status */
#define TC_ACCL   0100000                    /* Acceleration */


/* TUER3 - 15 - error status 3 - more unsafe conditions - unimplemented */

#define CPOS          u4
#define DATAPTR       u6

uint8         tu_buf[TU_NUMFR];
uint64        tu_cbuf;
uint16        tu_frame;
uint16        tu_tcr;
uint16        tu_wc;
uint16        tu_dba;
uint16        tu_dbb;
uint16        tu_cs2;
t_addr        tu_ba;
uint8         tu_attn;
uint8         tu_ie;
uint16        tu_mr;
static uint64 tu_boot_buffer;

int           tu_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access);
int           tu_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access);
uint16        tu_vect(struct pdp_dib *dibp);
void          tu_rst(DEVICE *dptr);
t_stat        tu_srv(UNIT *);
t_stat        tu_boot(int32, DEVICE *);
void          tu_ini(UNIT *, t_bool);
t_stat        tu_reset(DEVICE *);
t_stat        tu_attach(UNIT *, CONST char *);
t_stat        tu_detach(UNIT *);
t_stat        tu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                    const char *cptr);
const char    *tu_description (DEVICE *dptr);


UNIT                tua_unit[] = {
/* Controller 1 */
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
    { UDATA (&tu_srv, TU_UNIT+CNTRL_RH(0), 0) },
};

DIB tua_dib = {0772440, 037, 0224, 6, 3, &tu_read, &tu_write, &tu_vect, 0};

MTAB                tu_mod[] = {
    {MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL},
    {MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL},
    {MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LENGTH", "LENGTH",
     &sim_tape_set_capac, &sim_tape_show_capac, NULL},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "DENSITY", "DENSITY",
     &sim_tape_set_dens, &sim_tape_show_dens, NULL},
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

REG                 tua_reg[] = {
    {ORDATA(WC, tu_wc, 16)},
    {ORDATA(BA, tu_ba, 18)},
    {ORDATA(ATTN, tu_attn, 8)},
    {ORDATA(UNIT, tu_cs2, 8)},
    {ORDATA(IE, tu_ie, 8), REG_HRO},
    {ORDATA(FRAME, tu_frame, 16)},
    {ORDATA(TCR, tu_tcr, 16)},
    {BRDATA(BUFF, tu_buf, 16, 8, TU_NUMFR), REG_HRO},
    {0}
};

DEVICE              tua_dev = {
    "TU", tua_unit, tua_reg, tu_mod,
    NUM_UNITS_TU, 8, 18, 1, 8, 36,
    NULL, NULL, &tu_reset, &tu_boot, &tu_attach, &tu_detach,
    &tua_dib, DEV_DISABLE | DEV_DEBUG | DEV_TAPE, 0, dev_debug,
    NULL, NULL, &tu_help, NULL, NULL, &tu_description
};

int
tu_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access) {
    int         i;
    int         unit = tu_tcr & 07;
    UNIT       *uptr = &tua_unit[unit];

    if (uptr->CMD & CS1_GO) {
       uptr->STATUS |= (ER1_RMR << 16);
       return 0;
    }

    if (tu_cs2 & CS2_PAT || (tu_mr & 05) == 5) {
        uba_set_parity(tua_dib.uba_ctl);
        uptr->STATUS |= (ER1_PAR << 16) | DS_ATA;
	tu_attn = 1;
	fprintf(stderr, "Parity\r\n");
    }
    switch(addr & 036) {
    case  000:  /*  TUCS - control */
        sim_debug(DEBUG_DETAIL, &tua_dev, "TU %d Status=%06o %08o\n", unit, uptr->STATUS, 
                 uptr->CMD);
        if (access == BYTE && addr & 1)
           return 0;

        tu_ie &= ~CS1_IE;
        tu_ie |= data & CS1_IE;
        tu_ba = ((data << 8) & 0600000) | (tu_ba & 0177777);
        uptr->CMD = data & 076;

        if ((data & 1) == 0) {
           sim_debug(DEBUG_DETAIL, &tua_dev, "TU%o no go %06o\n", unit, data);
           return 0;                           /* No, nop */
        }
        if ((uptr->flags & UNIT_ATT) == 0) {
	    if (GET_FNC(data) == FNC_DCLR) {
                uptr->STATUS = 0;
                tu_ie = 0;
                tu_attn = 0;
                for (i = 0; i < NUM_UNITS_TU; i++) {
                    if (tua_unit[i].STATUS & DS_ATA)
                       tu_attn = 1;
                }
           }
           sim_debug(DEBUG_DETAIL, &tua_dev, "TU%o unattached %06o\n", unit, data);
           return 0;                           /* No, nop */
        }

        uba_clr_irq(&tua_dib);
        switch (GET_FNC(data)) {
        case FNC_NOP:
            break;

        case FNC_PRESET:                      /* read-in preset */
	    tu_tcr = 01000;
            unit = 0;
            uptr = &tua_unit[0];
             /* Fall through */

        case FNC_READ:                        /* read */
        case FNC_READREV:                     /* read w/ headers */
            tu_frame = 0;
            tu_tcr |= TC_FCS;
             /* Fall through */

        case FNC_WRITE:                       /* write */
        case FNC_SPACEF:                      /* Space forward */
        case FNC_SPACEB:                      /* Space backward */
             if ((tu_tcr & TC_FCS) == 0) {
                uptr->STATUS |= ER1_NEF << 16;
                break;
             }
             /* Fall through */

        case FNC_ERASE:                       /* Erase gap */
        case FNC_WTM:                         /* Write tape mark */
        case FNC_WCHK:                        /* write check */
        case FNC_REWIND:                      /* rewind */
        case FNC_UNLOAD:                      /* unload */
        case FNC_WCHKREV:                     /* write w/ headers */
            uptr->CMD  |= CS1_GO;
            uptr->STATUS = DS_PIP;
	    tu_tcr |= TC_ACCL;
            tu_attn = 0;
            for (i = 0; i < NUM_UNITS_TU; i++) {
                if (tua_unit[i].STATUS & DS_ATA)
                   tu_attn = 1;
            }
            CLR_BUF(uptr);
            uptr->DATAPTR = 0;
            if (GET_FNC(data) >= FNC_XFER)
                tu_ie |= CSX_BUSY;
            sim_activate(uptr, 100);
            break;

        case FNC_DCLR:                        /* drive clear */
            uptr->CMD &= ~(CS1_GO);
            uptr->STATUS = 0;
            tu_ie = 0;
            tu_attn = 0;
            for (i = 0; i < NUM_UNITS_TU; i++) {
                if (tua_unit[i].STATUS & DS_ATA)
                   tu_attn = 1;
            }
            break;
        default:
            uptr->STATUS |= (ER1_ILF << 16) | DS_ATA;
            tu_attn = 1;
        }
        sim_debug(DEBUG_DETAIL, &tua_dev, "TU %o AStatus=%06o\n", unit, uptr->CMD);
        if (tu_attn && tu_ie)
            uba_set_irq(&tua_dib);
        break;
     case 002: /* TUWC  - 172442 - word count */
        if ((tu_ie & (CSX_BUSY)) != 0) {
            uptr->STATUS |= (ER1_RMR << 16);
            sim_debug(DEBUG_DETAIL, &tua_dev, "TU%o not ready %02o %06o\n", unit,
                    addr & 077, data);
            return 0;
        }
        if (access == BYTE) {
            if (addr & 1)
                data = data | (tu_wc & 0377);
            else
                data = (tu_wc & 0177600) | data;
        }
        tu_wc = data;
        break;
     case 004: /* TUBA  - 172444 - base address */
        if ((tu_ie & (CSX_BUSY)) != 0) {
            uptr->STATUS |= (ER1_RMR << 16);
            sim_debug(DEBUG_DETAIL, &tua_dev, "TU%o not ready %02o %06o\n", unit,
                    addr & 077, data);
            return 0;
        }
        if (access == BYTE) {
            if (addr & 1)
                data = data | (tu_ba & 0377);
            else
                data = (tu_ba & 0177600) | data;
        }
        tu_ba = (tu_ba & 0600000) | (data & 0177776);
        break;

    case  006:  /* 772446 frame count */
        tu_frame = data;
        tu_tcr |= TC_FCS;
        break;

    case  010:  /* 772450 CS2 */
        if (access == BYTE) {
            if (addr & 1)
               data = data | tu_cs2;
        }
        if (data & 040) {
            tu_reset(&tua_dev);
        }
        tu_cs2 = data & (CS2_IR|CS2_PAT|CS2_UAI|CS2_UNIT);
        break;

    case  012:  /* 772452 status */
        break;

    case  014:  /* 772454 error register 1 */
        uptr->STATUS &= 0177777;
        uptr->STATUS |= (uint32)data << 16;
        break;
    case  016:  /* 772456 atten summary */
        tu_attn = 0;
        if (data & 1) {
            for (i = 0; i < 8; i++)
                tua_unit[i].STATUS &= ~DS_ATA;
        }
        break;

    case  020:  /* 772460 TCK maintenance */
	fprintf(stderr, "TCK %06o\n\r", data);
        break;
    case  022:  /* 772462 Data buffer */
	if ((tu_cs2 & CS2_IR) == 0) {
            tu_cs2 |= CS2_DLT;
            break;
        }
        tu_dba = tu_dbb;
        tu_dbb = data;
	if ((tu_cs2 & CS2_OR) == 0)
	    tu_dba = tu_dbb;
        tu_cs2 |= CS2_OR;
        tu_cs2 &= ~CS2_IR;
        break;
    case  024:  /* 772464 Maintenance register */
        tu_mr = data;
        break;
    case  026:  /* 772466 drive type */
        break;
    case  030:  /* 772470 -  serial no */
	break;
    case  032:  /* 772472 tape control register */
	if ((tu_tcr & 07) != (data & 07))
	    data |= TC_SAC;
	else
            data &= ~TC_SAC;
        tu_tcr = data;
	uptr->STATUS &= ~DS_SSC;
        break;
    case  034:
    case  036:
	return 1;
    default:
        uptr->STATUS |= (ER1_ILR << 16) | DS_ATA;
	tu_attn = 1;
    }
    sim_debug(DEBUG_DETAIL, &tua_dev, "TU %o write %02o %06o %06o %06o %06o %06o\n", unit,
             addr & 036, data, PC, tu_tcr, tu_mr, uptr->CMD);
    return 0;
}

int
tu_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access)
{
    int         tu_drive = tu_tcr & 07;
    UNIT       *uptr = &tua_unit[tu_drive];
    uint16      temp = 0;
    int         i;

#if 0
    if ((tu_cs2 & CS2_UNIT) != 0) {
        *data = 0;
        sim_debug(DEBUG_DETAIL, &tua_dev, "TU %o read %02o %06o %06o %o\n", 
             tu_tcr & 7, addr & 036, *data, PC, tu_cs2);
	return 0;
    }
#endif

    switch(addr & 036) {
    case  000:  /*  772440 control */
        temp = uptr->CMD & 077;
        if ((tu_cs2 & CS2_UNIT) == 0) {
            temp |= CS1_DVA;
            temp |= (uint16)(tu_ie & CS1_IE);
            temp |= (tu_ba & 0600000) >> 8;
            if ((tu_ie & CSX_BUSY) == 0 && (uptr->CMD & CS1_IE) == 0)
               temp |= CS1_RDY;
            if ((uptr->STATUS & (ER1_RMR << 16)) != 0)
               temp |= CS1_TRE;
            if (tu_cs2 & (CS2_MDPE|CS2_MXF|CS2_PGE|CS2_NEM|CS2_NED|CS2_PE|CS2_WCE|CS2_DLT))
               temp |= CS1_TRE;
            if (tu_attn || temp & CS1_TRE)
               temp |= CS1_SC;
	}
        break;
    case  002:  /* 772442 - word count */
        temp = tu_wc;
        break;
    case  004:  /* 772444 - base addresss */
        temp = (uint16)(tu_ba & 0177776);
        break;
    case  006:  /* 772446 - frame count */
        temp = tu_frame; 
        break;
    case  010:  /* 772450 - CS2 */
        temp = tu_cs2;
        if ((tu_cs2 & 07) != 0)
            temp |= CS2_NED;
        break;
    case  012:  /* 772452 - status */
	if ((tu_cs2 & CS2_UNIT) != 0)
           break;
        temp = uptr->STATUS & 0177777;
	if ((tu_tcr & TC_DENS) == TC_1600)
            temp |= DS_PES;
        temp |= ((tu_cs2 & CS2_UNIT) == 0) ? DS_DPR : 0;
        if (((uptr->STATUS >> 16) & 0177777) != 0)
           temp |= DS_ERR;
        if ((uptr->flags & UNIT_ATT) != 0) {
	   if ((uptr->CMD & CS1_IE) == 0) 
              temp |= DS_MOL;
           if (uptr->flags & MTUF_WLK)
              temp |= DS_WRL;
           if ((uptr->CMD & CS1_GO) == 0 && (uptr->STATUS & DS_PIP) == 0)
              temp |= DS_DRY;
           if (sim_tape_bot(uptr))
              temp |= DS_BOT;
           if (sim_tape_eot(uptr))
              temp |= DS_EOT;
        }
        break;
    case  014:  /* 772454 - error register 1 */
        temp = (uptr->STATUS >> 16) & 0177777;
        break;
    case  016:  /* 772456 - atten summary */
        for (i = 0; i < 8; i++) {
            if (tua_unit[i].STATUS & DS_ATA)
                temp |= 1;
        }
        break;
    case  022:  /* 772462 Data buffer */
	if ((tu_cs2 & CS2_OR) == 0) {
            tu_cs2 |= CS2_DLT;
            break;
        }
        temp = tu_dba;
        tu_dba = tu_dbb;
        tu_cs2 &= ~CS2_OR;
        tu_cs2 |= CS2_IR;
        break;
    case  020:  /* 772460 - character check */
        break;
    case  024:  /* 772464 - maintenance */
        temp = tu_mr;
        break;
    case  026:  /* 772466 -  drive type */
        if ((tu_cs2 & CS2_UNIT) == 0 && (uptr->flags & UNIT_DIS) == 0)
            temp = 0142054;
        break;
    case  030:  /* 772470 -  serial no */
        if ((tu_cs2 & CS2_UNIT) == 0)
            temp = 020 + (tu_drive + 1);
        break;
    case  032: /*  772472 - tape control register */
        if ((tu_cs2 & CS2_UNIT) == 0)
            temp = tu_tcr;
        break;
    case  034:
    case  036:
	return 1;
    default:
        uptr->STATUS |= (ER1_ILR << 16) | DS_ATA;
	tu_attn = 1;
    }
    *data = temp;
    sim_debug(DEBUG_DETAIL, &tua_dev, "TU %o read %02o %06o %06o %o %o\n", 
             tu_tcr & 7, addr & 036, *data, PC, tu_cs2, uptr->CMD);
    if (tu_attn && tu_ie)
        uba_set_irq(&tua_dib);
    if (tu_cs2 & CS2_PAT) {
        uba_set_parity(tua_dib.uba_ctl);
        uptr->STATUS |= (ER1_PAR << 16) | DS_ATA;
	tu_attn = 1;
    }
    return 0;
}

uint16
tu_vect(struct pdp_dib *dibp)
{
//    tu_ie = 0;
    return dibp->uba_vect;
}

/* Map simH errors into machine errors */
void tu_error(UNIT * uptr, t_stat r)
{
    DEVICE       *dptr = uptr->dptr;

    switch (r) {
    case MTSE_OK:            /* no error */
         break;

    case MTSE_TMK:           /* tape mark */
         uptr->STATUS |= DS_TM;
         break;

    case MTSE_WRP:           /* write protected */
         uptr->STATUS |= (ER1_NEF << 16) | DS_ATA;
         break;

    case MTSE_UNATT:         /* unattached */
    case MTSE_BOT:           /* beginning of tape */
    case MTSE_EOM:           /* end of medium */
         break;

    case MTSE_IOERR:         /* IO error */
    case MTSE_FMT:           /* invalid format */
         uptr->STATUS |= (ER1_PEF << 16) | DS_ATA;
         break;

    case MTSE_RECE:          /* error in record */
         uptr->STATUS |= (ER1_DPAR << 16) | DS_ATA;
         break;

    case MTSE_INVRL:         /* invalid rec lnt */
         uptr->STATUS |= (ER1_FCE << 16) | DS_ATA;
         break;

    }
    if (uptr->STATUS & DS_ATA)
        tu_attn = 1;
    uptr->CMD &= ~(CS1_GO);
    uptr->STATUS &= ~DS_PIP;
    sim_debug(DEBUG_EXP, dptr, "Setting status %d\n", r);
    if ((tu_ie & CSX_BUSY) == 0 && (tu_ie & CS1_IE) != 0)
        uba_set_irq(&tua_dib);
}

/* Handle processing of tape requests. */
t_stat tu_srv(UNIT * uptr)
{
    int           unit;
    DEVICE       *dptr;
    t_stat        r;
    t_mtrlnt      reclen;
    uint8         ch;
    int           cc;
    int           cc_max;

    /* Find dptr, and df10 */
    dptr = uptr->dptr;
    unit = uptr - dptr->units;
    cc_max = (4 + ((tu_tcr & TC_FMTSEL) == 0));
    tu_tcr &= ~(TC_ACCL);
    if ((uptr->flags & UNIT_ATT) == 0) {
        tu_error(uptr, MTSE_UNATT);      /* attached? */
        return SCPE_OK;
    }

    /* Check if waiting for attachment */
    if ((uptr->CMD & CS1_IE) != 0) {
        uptr->CMD &= ~(CS1_IE|CS1_GO);
        uptr->STATUS = DS_ATA|DS_SSC;
	fprintf(stderr, "Tape online\n\r");
        sim_debug(DEBUG_DETAIL, &tua_dev, "%s%o online\n", dptr->name, unit);
	tu_error(uptr, MTSE_OK);
	return SCPE_OK;
    }

    tu_tcr &= ~TC_ACCL;
    switch (GET_FNC(uptr->CMD)) {
    case FNC_NOP:
    case FNC_DCLR:
         sim_debug(DEBUG_DETAIL, dptr, "%s%o nop\n", dptr->name, unit);
         tu_error(uptr, MTSE_OK);      /* Nop */
//         tu_attn = 1;
         return SCPE_OK;

    case FNC_PRESET:
    case FNC_REWIND:
         sim_debug(DEBUG_DETAIL, dptr, "%s%o rewind\n", dptr->name, unit);
         if (sim_tape_bot(uptr)) {
             uptr->CMD &= ~(CS1_GO);
             uptr->STATUS &= ~(DS_PIP);
             uptr->STATUS |= DS_SSC|DS_ATA;
	     tu_error(uptr, sim_tape_rewind(uptr));
	 } else {
             sim_activate(uptr,4000);
             uptr->CMD |= (CS1_IE);
             (void)sim_tape_rewind(uptr);
         }
         return SCPE_OK;

    case FNC_UNLOAD:
         sim_debug(DEBUG_DETAIL, dptr, "%s%o unload\n", dptr->name, unit);
         uptr->CMD &= ~(CS1_GO);
         uptr->STATUS &= ~(DS_PIP);
         uptr->STATUS |= DS_SSC|DS_ATA;
         tu_error(uptr, sim_tape_detach(uptr));
         return SCPE_OK;

    case FNC_WCHKREV:
    case FNC_READREV:
         if (BUF_EMPTY(uptr)) {
             if ((r = sim_tape_rdrecr(uptr, &tu_buf[0], &reclen,
                                 TU_NUMFR)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, "%s%o read error %d\n", dptr->name, unit, r);
                 tu_ie &= ~(CSX_BUSY);
                 if (r == MTSE_BOT)
                     uptr->STATUS |= ER1_NEF << 16;
                 tu_error(uptr, r);
             } else {
                 sim_debug(DEBUG_DETAIL, dptr, "%s%o read %d\n", dptr->name, unit, reclen);
                 uptr->hwmark = reclen;
                 uptr->DATAPTR = uptr->hwmark-1;
                 uptr->CPOS = cc_max;
                 tu_cbuf = 0;
                 sim_activate(uptr, 120);
             }
             return SCPE_OK;
         }
         if (uptr->DATAPTR >= 0) {
             tu_frame++;
             cc = (8 * (3 - uptr->CPOS)) + 4;
             ch = tu_buf[uptr->DATAPTR];
             if (cc < 0)
                 tu_cbuf |= (uint64)(ch & 0x0f);
             else
                 tu_cbuf |= (uint64)(ch & 0xff) << cc;
             uptr->DATAPTR--;
             uptr->CPOS--;
             if (uptr->CPOS == 0) {
                 uptr->CPOS = cc_max;
                 if (GET_FNC(uptr->CMD) == FNC_READREV && 
                      uba_write_npr(tu_ba, tua_dib.uba_ctl, tu_cbuf) == 0) {
                    tu_ie &= ~(CSX_BUSY);
                    tu_error(uptr, MTSE_OK);
                    return SCPE_OK;
                 }
                 sim_debug(DEBUG_DATA, dptr, "%s%o readrev %012llo\n",
                           dptr->name, unit, tu_cbuf);
                 tu_cbuf = 0;
                 if ((tu_cs2 & CS2_UAI) == 0)
                     tu_ba -= 4;
                 tu_wc = (tu_wc + 2) & 0177777;
                 if (tu_wc == 0) {
                     tu_ie &= ~(CSX_BUSY);
                     tu_error(uptr, MTSE_OK);
                     return SCPE_OK;
                 }
             }
         } else {
             if (uptr->CPOS != cc_max)
                 uba_write_npr(tu_ba, tua_dib.uba_ctl, tu_cbuf);
             tu_ie &= ~(CSX_BUSY);
             tu_error(uptr, MTSE_OK);
             return SCPE_OK;
         }
         break;

    case FNC_WCHK:
    case FNC_READ:
         if (BUF_EMPTY(uptr)) {
             if ((r = sim_tape_rdrecf(uptr, &tu_buf[0], &reclen,
                                 TU_NUMFR)) != MTSE_OK) {
                 sim_debug(DEBUG_DETAIL, dptr, "%s%o read error %d\n", dptr->name, unit, r);
                 tu_ie &= ~(CSX_BUSY);
                 if (r == MTSE_TMK)
                     uptr->STATUS |= ER1_FCE << 16;
                 tu_error(uptr, r);
             } else {
                 sim_debug(DEBUG_DETAIL, dptr, "%s%o read %d %d\n", dptr->name,  unit, reclen, uptr->pos);
                 uptr->hwmark = reclen;
                 uptr->DATAPTR = 0;
                 uptr->CPOS = 0;
                 tu_cbuf = 0;
                 sim_activate(uptr, 120);
             }
             return SCPE_OK;
         }
         if ((uint32)uptr->DATAPTR < uptr->hwmark) {
             tu_frame++;
             cc = (8 * (3 - uptr->CPOS)) + 4;
             ch = tu_buf[uptr->DATAPTR];
             if (cc < 0)
                 tu_cbuf |= (uint64)(ch & 0x0f);
             else
                 tu_cbuf |= (uint64)(ch & 0xff) << cc;
             uptr->DATAPTR++;
             uptr->CPOS++;
             if (uptr->CPOS == cc_max) {
                 uptr->CPOS = 0;
                 if (GET_FNC(uptr->CMD) == FNC_READ && 
                     uba_write_npr(tu_ba, tua_dib.uba_ctl, tu_cbuf) == 0) {
//                     if ((uint32)uptr->DATAPTR == uptr->hwmark) 
 //                        goto rd_end;
                     tu_ie &= ~(CSX_BUSY);
                     tu_error(uptr, MTSE_OK);
                     return SCPE_OK;
                 }
                 sim_debug(DEBUG_DATA, dptr, "%s%o read %012llo %d %06o\n",
                           dptr->name, unit, tu_cbuf, uptr->DATAPTR, tu_tcr);
                 tu_cbuf = 0;
                 if ((tu_cs2 & CS2_UAI) == 0)
                     tu_ba += 4;
                 tu_wc = (tu_wc + 2) & 0177777;
                 if (tu_wc == 0)
                     goto rd_end;
             }
         } else {
rd_end:
             if (uptr->CPOS != 0) {
                 sim_debug(DEBUG_DATA, dptr, "%s%o readf %012llo %d\n",
                              dptr->name, unit, tu_cbuf, uptr->DATAPTR);
                 uba_write_npr(tu_ba, tua_dib.uba_ctl, tu_cbuf);
             }
             if (tu_frame != 0)
                 uptr->STATUS |= ER1_FCE << 16;
             tu_ie &= ~(CSX_BUSY);
             tu_error(uptr, MTSE_OK);
             return SCPE_OK;
         }
         break;

    case FNC_WRITE:
         if (BUF_EMPTY(uptr)) {
             if (tu_frame == 0) {
                 uptr->STATUS |= (ER1_NEF << 16) | DS_ATA;
                 tu_ie &= ~(CSX_BUSY);
                 tu_error(uptr, MTSE_OK);
                 return SCPE_OK;
             }
             if ((uptr->flags & MTUF_WLK) != 0) {
                 tu_ie &= ~(CSX_BUSY);
                 tu_error(uptr, MTSE_WRP);
                 return SCPE_OK;
             }
             sim_debug(DEBUG_EXP, dptr, "%s%o Init write\n", dptr->name, unit);
             uptr->hwmark = 0;
             uptr->CPOS = 0;
             uptr->DATAPTR = 0;
             tu_cbuf = 0;
         }
         if (tu_frame != 0 && uptr->CPOS == 0) {
             if (uba_read_npr(tu_ba, tua_dib.uba_ctl, &tu_cbuf) == 0) {
                 uptr->CPOS = 010;
                 goto wr_end;
             } else {
                 if ((tu_cs2 & CS2_UAI) == 0)
                     tu_ba += 4;
                 tu_wc = (tu_wc + 2) & 0177777;
             }
         }

         if (uptr->CPOS == 0)
              sim_debug(DEBUG_DATA, dptr, "%s%o write %012llo\n",
                         dptr->name, unit, tu_cbuf);
         /* Write next char out */
         cc = (8 * (3 - (uptr->CPOS & 07))) + 4;
         if (cc < 0)
              ch = tu_cbuf & 0x0f;
         else
              ch = (tu_cbuf >> cc) & 0xff;
         tu_buf[uptr->DATAPTR] = ch;
         uptr->DATAPTR++;
         uptr->hwmark = uptr->DATAPTR;
         uptr->CPOS = (uptr->CPOS & 010) | ((uptr->CPOS & 07) + 1);
         if ((uptr->CPOS & 7) == cc_max) {
            uptr->CPOS &= 010;
         if (tu_wc == 0)
             uptr->CPOS = 010;
         }
         tu_frame = 0177777 & (tu_frame + 1);
         if (tu_frame == 0) {
            uptr->CPOS = 010;
            tu_tcr &= ~(TC_FCS);
         }
wr_end:
         if (uptr->CPOS == 010) {
             /* Write out the block */
             reclen = uptr->hwmark;
             r = sim_tape_wrrecf(uptr, &tu_buf[0], reclen);
             sim_debug(DEBUG_DETAIL, dptr, "%s%o Write %d %d\n",
                          dptr->name, unit, reclen, uptr->CPOS);
             uptr->DATAPTR = 0;
             uptr->hwmark = 0;
             tu_ie &= ~(CSX_BUSY);
             tu_error(uptr, r); /* Record errors */
             return SCPE_OK;
         }
         break;

    case FNC_WTM:
         uptr->STATUS &= ~DS_PIP;
         uptr->STATUS |= DS_ATA;
         if ((uptr->flags & MTUF_WLK) != 0) {
             tu_error(uptr, MTSE_WRP);
         } else {
             tu_error(uptr, sim_tape_wrtmk(uptr));
         }
         sim_debug(DEBUG_DETAIL, dptr, "%s%o WTM\n", dptr->name, unit);
         return SCPE_OK;

    case FNC_ERASE:
         uptr->STATUS &= ~DS_PIP;
         uptr->STATUS |= DS_ATA;
         if ((uptr->flags & MTUF_WLK) != 0) {
             tu_error(uptr, MTSE_WRP);
         } else {
             tu_error(uptr, sim_tape_wrgap(uptr, 35));
         }
         sim_debug(DEBUG_DETAIL, dptr, "%s%o ERG\n", dptr->name, unit);
         return SCPE_OK;

    case FNC_SPACEF:
    case FNC_SPACEB:
         sim_debug(DEBUG_DETAIL, dptr, "%s%o space %o\n", dptr->name, unit, GET_FNC(uptr->CMD));
         /* Always skip at least one record */
         if (GET_FNC(uptr->CMD) == FNC_SPACEF)
             r = sim_tape_sprecf(uptr, &reclen);
         else
             r = sim_tape_sprecr(uptr, &reclen);
         switch (r) {
         case MTSE_OK:            /* no error */
  	      tu_frame = 0177777 & (tu_frame + 1);
              break;

         case MTSE_BOT:           /* beginning of tape */
              uptr->STATUS |= ER1_NEF << 16;
              /* Fall Through */

         case MTSE_TMK:           /* tape mark */
         case MTSE_EOM:           /* end of medium */
              if (tu_frame != 0)
                 uptr->STATUS |= ER1_FCE << 16;
              else
                 tu_tcr &= ~(TC_FCS);
              uptr->STATUS |= DS_ATA;
              /* Stop motion if we recieve any of these */
              tu_error(uptr, r);
              return SCPE_OK;
         }
         if (tu_frame == 0) {
            uptr->STATUS |= DS_ATA;
            tu_error(uptr, MTSE_OK);
            return SCPE_OK;
         } else {
            tu_tcr &= ~(TC_FCS);
            sim_activate(uptr, reclen * 100);
         }
         return SCPE_OK;
    }
    sim_activate(uptr, 50);
    return SCPE_OK;
}



t_stat
tu_reset(DEVICE * dptr)
{
    int   i;

    tu_attn = 0;
    tu_ie = 0;
    tu_ba = tu_frame = 0;
    tu_wc = 0;
    tu_mr = 0;
    tu_cs2 = CS2_IR;
    for (i = 0; i < 8; i++) {
        tua_unit[i].STATUS = 0;
	tua_unit[i].CMD = 0;
    }
    uba_clr_irq(&tua_dib);
    sim_debug(DEBUG_DETAIL, dptr, "%s reset\n", dptr->name);
    return SCPE_OK;
}

void tu_read_word(UNIT *uptr) {
     int i, cc, ch;

     tu_cbuf = 0;
     for(i = 0; i <= 4; i++) {
        cc = (8 * (3 - i)) + 4;
        ch = tu_buf[uptr->DATAPTR];
        if (cc < 0)
            tu_cbuf |=  (uint64)(ch & 0x0f);
        else
            tu_cbuf |= (uint64)(ch & 0xff) << cc;
        uptr->DATAPTR++;
     }
}

/* Boot from given device */
t_stat
tu_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    t_mtrlnt            reclen;
    t_stat              r;
    uint32              addr;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */

    uptr->CMD = 0;
    tu_tcr = unit_num;
    r = sim_tape_rewind(uptr);
    if (r != MTSE_OK)
        return r;
    /* Skip first file, which is micro code */
    while (r == MTSE_OK)
        r = sim_tape_rdrecf(uptr, &tu_buf[0], &reclen, TU_NUMFR);

    if (r != MTSE_TMK)
        return r;
    /* Next read in the boot block */
    r = sim_tape_rdrecf(uptr, &tu_buf[0], &reclen, TU_NUMFR);
    if (r != MTSE_OK)
        return r;
    uptr->DATAPTR = 0;
    uptr->hwmark = reclen;

    addr = 01000;
    while ((uint32)uptr->DATAPTR < uptr->hwmark) {
        tu_read_word(uptr);
        M[addr] = tu_cbuf;
        addr ++;
    }
    M[036] = tua_dib.uba_addr | (tua_dib.uba_ctl << 18);
    M[037] = 0;
    M[040] = tu_tcr;
    PC = 01000;
    return cty_reset(&cty_dev);
}


t_stat
tu_attach(UNIT * uptr, CONST char *file)
{   t_stat   r;

    uptr->CMD = 0;
    uptr->STATUS = 0;
    r = sim_tape_attach_ex(uptr, file, 0, 0);
    if (r == SCPE_OK && (sim_switches & SIM_SW_REST) == 0) {
//	uptr->CMD = CS1_IE|CS1_GO;
//	sim_activate_after(uptr, 500000);
        uptr->STATUS = DS_ATA|DS_SSC;
        tu_attn = 1;
        if ((tu_ie & CSX_BUSY) == 0 && (tu_ie & CS1_IE) != 0)
            uba_set_irq(&tua_dib);
    }
    return r;
}

t_stat
tu_detach(UNIT * uptr)
{
    uptr->STATUS = DS_ATA|DS_SSC;
    tu_attn = 1;
    if ((tu_ie & CSX_BUSY) == 0 && (tu_ie & CS1_IE) != 0)
        uba_set_irq(&tua_dib);
    return sim_tape_detach(uptr);
}



t_stat tu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "TU Tape Drives with TM03 formatter. (TU)\n\n");
fprintf (st, "The TU controller implements the Massbus tape formatter the TM03. TU\n");
fprintf (st, "options include the ability to set units write enabled or write locked\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprintf (st, "The TU device supports the BOOT command.\n");
sim_tape_attach_help (st, dptr, uptr, flag, cptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *tu_description (DEVICE *dptr)
{
    return "TU04/05/06/07 Massbus disk controller";
}


#endif
