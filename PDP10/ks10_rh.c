/* ks10_rh.c: RH11/RH20 interace routines.

   Copyright (c) 2019-2020, Richard Cornwell

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



#if (NUM_DEVS_RP > 0)
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


DIB rpa_dib = {0776700, 077, 0254, 6, 1, &rp_read, &rp_write, &rp_vect, 0};

int rh_map[] = { 0,   -1,  -1,  05,  -1,  01,  02,  04,  07,  -1, 
                 03,  06,  010, 011, 012, 013, 014, 015, 016, 017};


int
rh_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access) {
    int             i;
    int             r;
    struct pdp_dib  *dibp = (DIB *) dptr->ctxt;
    struct rh_if    *rhc;
    int             reg;

    if (dibp == NULL)
        return 1;
    rhc = dibp->rh11_if;
    /* Check for parity error during access */
    if (rhc->cs2 & CS2_PAT) {
       uba_set_parity(dibp->uba_ctl);
       rhc->status |= ER1_PAR;
    }
    addr &= dibp->uba_mask;
    reg = rh_map[addr >> 1];

    if (access == BYTE && reg >= 0) {
        rhc->dev_read(dptr, rhc, reg, &temp);
        if (addr & 1)
            data = data | (temp & 0377);
        else
            data = (temp & 0177600) | data;
    }
   
    switch(addr) {
    case  000: /* CS1 */
        if (access == BYTE && addr & 1)
           break;
        rhc->cs1 &= ~(CS1_IE);
        rhc->cs1 |= data & (CS1_IE);
        rhc->ba = ((data << 8) & 0600000) | (rhc->ba & 0177777);
        /* Check if we had a go with a data transfer command */
        if (r == 0 && (data & CS1_GO) != 0 && GET_FNC(data) >= FNC_XFER) {
            rhc->status |= BUSY;
        }
        r = rhc->dev_write(dptr, rh, 0, data);
        break;

     case 002: /* RPWC  - 176702 - word count */ /* 1 */
        if ((rhc->status & BUSY) != 0) { 
            rhc->status |= ER1_RMR;
            sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o not ready %02o %06o\n", rp_unit,
                    addr & 077, data);
            return 0;
        }
        if (access == BYTE) {
            if (addr & 1)
                data = data | (rhc->wc & 0377);
            else
                data = (rhc->wc & 0177600) | data;
        }
        rhc->wc = data;
        break;
     case 004: /* RPBA  - 176704 - base address */ /* 2 */
        if ((rhc->status & BUSY) != 0) { 
            rhc->status |= ER1_RMR;
            sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o not ready %02o %06o\n", rp_unit,
                    addr & 077, data);
            return 0;
        }
        if (access == BYTE) {
            if (addr & 1)
                data = data | (rhc->ba & 0377);
            else
                data = (rhc->ba & 0177600) | data;
        }
        rhc->ba = (rhc->ba & 0600000) | (data & 0177776);
        break;

    case  010: /* RPCS2 - 176710 - Control and Status register 2 */ /* 4 */
        if (access == BYTE) {
            if (addr & 1)
               data = data | (rhc->cs2 & 0377);
        }
        rhc->cs2 = ((CS2_DLT|CS2_WCE|CS2_NED|CS2_NEM|CS2_PGE|CS2_MDPE) & rp_cs2) |
                    ((CS2_UAI|CS2_PAT|CS2_UNIT) & data);
        if (data & CS2_CLR) {
            dptr->reset(dptr);
        }
        rhc->cs2 |= CS2_IR;
        rhc->drive = data & 07;
        break;

    case  014: /* RPER1 - 176714 - error status 1 */ /* 6 */
        rhc->status &= ~(07 & data);
        return rh->dev_write(dptr, rh, 2, data);

    case  022: /* RPDB  - 176722 - data buffer */  /* 11 */
        if ((rhc->cs2 & CS2_IR) == 0) {
	    rhc->cs2 |= CS2_DLT;
	    break;
	}
	rhc->dba = rhc->dbb;
        rhc->dbb = data;
	if (rhc->cs2 & CS2_IR)
	    rhc->dba = rhc->dbb;
        rhc->cs2 |= CS2_OR;
        rhc->cs2 &= ~CS2_IR;
        break;

    default:
	return rhc->dev_write(dptr, reg, 5, data);

    }
    sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o write %02o %06o\n", rp_unit,
             addr & 076, data);
    return 0;
}

int
rh_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access) {
    int             i;
    int             r = 1;
    struct pdp_dib  *dibp = (DIB *) dptr->ctxt;
    struct rh_if    *rhc;
    int             reg;

    if (dibp == NULL)
        return 1;
    rhc = dibp->rh11_if;
    addr &= dibp->uba_mask;
    reg = rh_map[addr >> 1];

    if (reg >= 0)
	r = rhc->dev_read(dptr, rhc, reg, &temp);

    /* Check for parity error during access */
    if (rhc->cs2 & CS2_PAT) {
       uba_set_parity(dibp->uba_ctl);
       rhc->status |= ER1_PAR;
    }
    switch(addr) {
    case  000:  /* RPC   - 176700 - control */
        temp |= (uint16)(rhc->cs1 & (CS1_IE));
        temp |= (rhc->ba & 0600000) >> 8;
        if ((rhc->status & BUSY) == 0)
           temp |= CS1_RDY;
        if (rhc->cs2 & (CS2_MDPE|CS2_MXF|CS2_PGE|CS2_NEM|CS2_NED|CS2_PE|CS2_WCE|CS2_DLT))
           temp |= CS1_TRE|CS1_SC;
        if (rhc->attn)
           temp |= CS1_SC;
        break;
    case  002:  /* RPWC  - 176702 - word count */
        temp = rhc->wc;
	r = 0;
        break;
    case  004:  /* RPBA  - 176704 - base address */
        temp = (uint16)(rhc->ba & 0177776);
	r = 0;
        break;
    default:
	break;
    case  010:  /* RPCS2 - 176710 - control/status 2 */
        temp = rp_cs2;
	r = 0;
        break;
    case  014:  /* RPER1 - 176714 - error status 1 */
	temp |= rhc->status & 07;
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
    }
    *data = temp;
    sim_debug(DEBUG_DETAIL, &rpa_dev, "RP%o read %02o %06o %06o\n", rp_unit,
             addr & 076, temp, PC);
    return r;
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





/* Handle KI and KL style interrupt vectors */
t_addr
rh_devirq(uint32 dev, t_addr addr) {
    struct rh_if  *rhc = NULL;
    int            drive;

    for (drive = 0; rh[drive].dev_num != 0; drive++) {
       if (rh[drive].dev_num == (dev & 0774)) {
           rhc = rh[drive].rh;
           break;
       }
    }
    if (rhc != NULL) {
        if (rhc->imode == 1) /* KI10 Style */
           addr = RSIGN | rhc->ivect;
        else if (rhc->imode == 2) /* RH20 style */
           addr = rhc->ivect;
    } else {
       sim_printf("Unable to find device %03o\n\r", dev);
    }
    return  addr;
}

/* Set the attention flag for a unit */
void rh_setattn(struct rh_if *rhc, int unit)
{
#if KS
    if ((rhc->status & BUSY) == 0 && (rhc->cs2 & CS1_IE) != 0)
        uba_set_irq(rhc->dib);
#else
    rhc->attn |= 1<<unit;
    if ((rhc->status & BUSY) == 0 && (rhc->status & IADR_ATTN) != 0) 
        set_interrupt(rhc->devnum, rhc->status);
#endif
}

void rh_error(struct rh_if *rhc)
{
#if !KS
    if (rhc->imode == 2)
       rhc->status |= RH20_DR_EXC;
#endif
}

/* Decrement block count for RH20, nop for RH10 */
int rh_blkend(struct rh_if *rhc)
{
     return 0;
}

/* Set an IRQ for a DF10 device */
void rh_setirq(struct rh_if *rhc) {
      rhc->status |= PI_ENABLE;
#if KS
      uba_set_irq(rhc->dib);
#else
      set_interrupt(rhc->devnum, rhc->status);
#endif
}

/* Generate the DF10 complete word */
void rh_writecw(struct rh_if *rhc, int nxm) {
}

/* Finish off a DF10 transfer */
void rh_finish_op(struct rh_if *rhc, int nxm) {
     rhc->status &= ~BUSY;
     rh_writecw(rhc, nxm);
     rh_setirq(rhc);
}


/* Setup for a DF10 transfer */
void rh_setup(struct rh_if *rhc, uint32 addr)
{
     rhc->status |= BUSY;
}


/* Fetch the next IO control word */
int rh_fetch(struct rh_if *rhc) {
     return 1;
}

/* Read next word */
int rh_read(struct rh_if *rhc) {
     if (uba_read_npr(rhc->ba, rhc->ctl, &rhc->buf) == 0)
        return 0;
     if ((rhc->cs2 & CS2_UAI) == 0)
        rhc->ba += 4;
     rhc->wc (rhc->wc + 2) & 0177777;
     if (rhc->wc == 0)
        return 0;
     return 1;
}

/* Write next word */
int rh_write(struct rh_if *rhc) {
     if (uba_write_npr(rhc->ba, rhc->ctl, rhc->buf) == 0)
        return 0;
     if ((rhc->cs2 & CS2_UAI) == 0)
        rhc->ba += 4;
     rhc->wc (rhc->wc + 2) & 0177777;
     if (rhc->wc == 0)
        return 0;
     return 1;
}

