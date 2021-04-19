/* ks10_uba.c: KS10 Unibus interface

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

/* UBA Map as stored */
#define PAGE_MASK     000003777000  /* Page mask bits, bits 25-36 on load */
#define MAP_RPV       000400000000  /* Ram parity valid bit */
#define MAP_VALID     001000000000  /* Page valid */
#define MAP_FME       002000000000  /* Fast Mode enable */
#define MAP_EN16      004000000000  /* Disable upper 2 bits UBA */
#define MAP_RPW       010000000000  /* For Read Pause Write */
#define MAP_RAMP      020000000000  /* Parity error in RAM */

/* UBA Stats register */
#define UBST_PIL      000000000007  /* Low level PIA */
#define UBST_PIH      000000000070  /* High level PIA */
#define UBST_INIT     000000000100  /* Initialize UBA */
#define UBST_DXFR     000000000200  /* Disable transfer on uncorrectable */
#define UBST_PWRL     000000001000  /* Power low */
#define UBST_INTL     000000002000  /* Interrupt on Br5 or Br4 */
#define UBST_INTH     000000004000  /* INterrupt on Br7 or Br6 */
#define UBST_NED      000000040000  /* Non-existant device */
#define UBST_PAR      000000100000  /* Parity error */
#define UBST_BAD      000000200000  /* Bad mem transfer */
#define UBST_TIM      000000400000  /* UBA Timout */

uint32  uba_map[2][64];
uint32  uba_status[2]; 
int     uba_device[16] = { -1, 0, -1, 1, -1, -1, -1, -1,
                           -1, -1, -1, -1, -1, -1, -1, -1 };

int
uba_read(t_addr addr, int ctl, uint64 *data, int access)
{
    DEVICE *dptr;
    int     i;
    int     ubm = uba_device[ctl];

    if (ctl == 0 && addr == 0100000) {
        *data = 0;
        return 0;
    }

    if (ubm == -1) {
        sim_debug(DEBUG_EXP, &cpu_dev, "No UBA adaptor %02o %08o\n", ctl, addr);
        return 1;
    }

    /* Check if in UBA map */
    if ((addr & 0777600) == 0763000) {
       if ((addr & 0100) == 0) {
           *data = (uint64)uba_map[ubm][addr & 077];
           return 0;
       } else if ((addr & 077) == 0) {
           int     pih, pil;
           *data = (uint64)uba_status[ubm];
           pih = 0200 >> ((uba_status[ubm] >> 3) & 07);
           pil = 0200 >> (uba_status[ubm] & 07);
           for(i = 0; (dptr = sim_devices[i]) != NULL; i++) {
               DIB *dibp = (DIB *) dptr->ctxt;
               if (dibp == NULL)
                   continue;
               /* If device is pending on this level save */
               if (dibp->uba_ctl == ctl) {
                  if ((dibp->uba_irq_pend & pil) != 0)
                      *data |= UBST_INTL;
                  if ((dibp->uba_irq_pend & pih) != 0)
                      *data |= UBST_INTH;
               }
           }
           return 0;
       } else if ((addr & 077) == 1) {
           *data = 0;
           return 0;
       }
    }

    /* Look for device */
    for(i = 0; (dptr = sim_devices[i]) != NULL; i++) {
        DIB *dibp = (DIB *) dptr->ctxt;
        if (dibp == NULL)
            continue;
        if (ctl == dibp->uba_ctl && 
            dibp->uba_addr == (addr & (~dibp->uba_mask))) {
            uint16 buf;
            int r = dibp->rd_io(dptr, addr, &buf, access);
            *data = (uint64)buf;
            if (r)
                break;
            return r;
        }
    }
    sim_debug(DEBUG_EXP, &cpu_dev, "No UBA device  %02o %08o\n", ctl, addr);
    uba_status[ubm] |= UBST_TIM | UBST_NED;
    return 1;
}

int
uba_write(t_addr addr, int ctl, uint64 data, int access)
{
    DEVICE *dptr;
    int     i;
    int     ubm = uba_device[ctl];

    if (ctl == 0 && addr == 0100000) {
        return 1;
    }
    if (ubm == -1) {
        sim_debug(DEBUG_EXP, &cpu_dev, "No UBA adaptor %02o %08o %012llo\n", ctl, addr, data);
        return 1;
    }

    if (access == BYTE) {
        if ((addr & 1) != 0)
            data = (data & 0377) << 8;
        else
            data = (data & 0377);
    }

    /* Check if in UBA map */
    if ((addr & 0777400) == 0763000) {
       if ((addr & 0100) == 0) {
           uint32 map = (uint32)(data & 03777) << 9;
           map |= (uint32)(data & 0740000) << 13;
           uba_map[ubm][addr & 077] = map;
           sim_debug(DEBUG_EXP, &cpu_dev, "Wr MAP %02o %012llo %06o\n\r", 
                 addr & 077, data, map);
           return 0;
       } else if ((addr & 077) == 0) {
           uba_status[ubm] &= (uint32)(074000 ^ data) | 0746000;
           if (data & 0100) {
               uba_status[ubm] = 0;
               for(i = 0; (dptr = sim_devices[i]) != NULL; i++) {
                   DIB *dibp = (DIB *) dptr->ctxt;
                   if (dibp == NULL)
                       continue;
                   if (ctl == dibp->uba_ctl && dptr->reset != NULL)
                       (void)(dptr->reset)(dptr);
                   if (ctl == dibp->uba_ctl)
                      dibp->uba_irq_pend = 0;
               }
               clr_interrupt(ctl<<2);
           }
           uba_status[ubm] |= (uint32)(0277 & data);
           return 0;
       } else if ((addr & 077) == 1) {
           return 0;
       }
       uba_status[ubm] |= UBST_TIM | UBST_NED;
    }

    /* Look for device */
    for(i = 0; (dptr = sim_devices[i]) != NULL; i++) {
        DIB *dibp = (DIB *) dptr->ctxt;
        if (dibp == NULL)
            continue;
        if (ctl == dibp->uba_ctl && dibp->uba_addr == (addr & (~dibp->uba_mask))) {
            uint16 buf = (uint16)(data & 0177777);
            int r = dibp->wr_io(dptr, addr, buf, access);
            if (r)
                break;
            return r;
        }
    }
    sim_debug(DEBUG_EXP, &cpu_dev, "No UBA device write %02o %08o %012llo\n", ctl, addr, data);
    uba_status[ubm] |= UBST_TIM | UBST_NED;
    return 1;
}

int
uba_read_npr(t_addr addr, uint16 ctl, uint64 *data)
{
    int ubm = uba_device[ctl];
    uint32  map = uba_map[ubm][(077) & (addr >> 11)];
    if ((addr & 0400000) != 0)
        return 0;
    if ((map & MAP_VALID) == 0)
        return 0;
    addr = (map & PAGE_MASK) | (addr >> 2) & 0777;
    *data = M[addr];
    if (map & MAP_EN16)
        *data &= 0177777177777;
    return 1;
}

int
uba_write_npr(t_addr addr, uint16 ctl, uint64 data)
{
    int ubm = uba_device[ctl];
    t_addr oaddr = addr;
    uint32  map = uba_map[ubm][(077) & (addr >> 11)];
    if ((addr & 0400000) != 0)
        return 0;
    if ((map & MAP_VALID) == 0)
        return 0;
    addr = (map & PAGE_MASK) | (addr >> 2) & 0777;
    if (map & MAP_EN16)
        data &= 0177777177777;
    sim_debug(DEBUG_DATA, &cpu_dev, "Wr NPR %08o %08o %012llo\n\r", oaddr, addr, data);
    M[addr] = data;
    return 1;
}

int
uba_read_npr_byte(t_addr addr, uint16 ctl, uint16 *data)
{
    int ubm = uba_device[ctl];
    return 0;
}

int
uba_write_npr_byte(t_addr addr, uint16 ctl, uint16 data)
{
    int ubm = uba_device[ctl];
    return 0;
}

int
uba_read_npr_word(t_addr addr, uint16 ctl, uint16 *data)
{
    int ubm = uba_device[ctl];
    return 0;
}

int
uba_write_npr_word(t_addr addr, uint16 ctl, uint16 data)
{
    int ubm = uba_device[ctl];
    return 0;
}

void
uba_set_irq(DIB *dibp)
{
    int ubm = uba_device[dibp->uba_ctl];
    int pi;
    
    if (ubm < 0)
       return;
    /* Figure out what channel device should IRQ on */
    if (dibp->uba_br > 5) {
       pi = uba_status[ubm] >> 3;
    } else {
       pi = uba_status[ubm];
    }
    /* Save in device temp the irq value */
    dibp->uba_irq_pend = 0200 >> (pi & 07);
    set_interrupt(dibp->uba_ctl<<2, pi);
}

void
uba_clr_irq(DIB *idev)
{
    DEVICE *dptr;
    int ubm = uba_device[idev->uba_ctl];
    int pi;
    int i;
    int     high = 0;
    
    if (ubm < 0)
       return;
    /* Figure out what channel device should IRQ on */
    if (idev->uba_br > 5) {
       pi = uba_status[ubm] >> 3;
    } else {
       pi = uba_status[ubm];
    }
    /* Save in device temp the irq value */
    idev->uba_irq_pend = 0;
    for(i = 0; (dptr = sim_devices[i]) != NULL; i++) {
        DIB *dibp = (DIB *) dptr->ctxt;
        if (dibp == NULL)
            continue;
        /* If device is pending on this level save */
        if (dibp->uba_ctl == idev->uba_ctl &&
                dibp->uba_irq_pend != 0) {
            /* At least one, no need to continue */
            return;
        }
    }
    /* Nothing, so clear it */
    clr_interrupt(idev->uba_ctl<<2);
}


t_addr
uba_get_vect(t_addr addr, int lvl, int *dev, int *new_lvl)
{
    DEVICE *dptr;
    DIB    *idev = NULL;
    uint64  buffer;
    uint16  ivect;
    int     i;
    int     ctl = 17;
    int     pi;
    int     ubm;

    *dev = 0;   /* Adaptor 0 is special */
    /* Look for device */
    for(i = 0; (dptr = sim_devices[i]) != NULL; i++) {
        DIB *dibp = (DIB *) dptr->ctxt;
        if (dibp == NULL)
            continue;
        /* If device is pending on this level save */
        if ((dibp->uba_irq_pend & lvl) != 0) {
            /* But only if it is highest UBA */
            if (dibp->uba_ctl < ctl) {
                ctl = dibp->uba_ctl;
                idev = dibp;
            }
        }
    }
    /* Should have a device */
    if (idev != NULL) {
        *new_lvl = 0;
        *dev = idev->uba_ctl;
        /* Figure out what pi channel this will interrupt on */
        ubm = uba_device[idev->uba_ctl];
        /* Clear interrupts */
        idev->uba_irq_pend = 0; 
        /* Fetch vector */
        if (Mem_read_word(0100 | idev->uba_ctl, &buffer, 1))
           return addr;
        idev->uba_irq_pend = 0;
        ivect = idev->uba_vect;
        if (idev->irqv != NULL)
            ivect = (idev->irqv)(idev);
        addr = (buffer + (ivect >> 2)) & RMASK;
        /* Any other devices waiting on this PI channel? */
        for(pi = i = 0; (dptr = sim_devices[i]) != NULL; i++) {
            DIB *dibp = (DIB *) dptr->ctxt;
            if (dibp == NULL)
                continue;
            /* If device is pending on this level save */
            if (dibp->uba_ctl == idev->uba_ctl &&
                    dibp->uba_irq_pend != 0) {
                /* At least one, no need to continue */
                *new_lvl |= dibp->uba_irq_pend;
            }
        }
    }
    return addr;
}

void
uba_set_parity(uint16 ctl)
{
    int ubm = uba_device[ctl];
    if (ubm >= 0)
        uba_status[ubm] |= UBST_PAR;
}

t_stat
uba_set_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE  *dptr;
    DIB     *dibp;
    t_value  newaddr;
    t_stat   r;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;

    dibp = (DIB *) dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;

    newaddr = get_uint (cptr, 18, 0777777, &r);

    if (r != SCPE_OK)
        return r;
    dibp->uba_addr = (uint32)(newaddr & RMASK);
    return SCPE_OK;
}

t_stat
uba_show_addr (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    DEVICE  *dptr = find_dev_from_unit(uptr);
    DIB     *dibp = (DIB *) dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;
    fprintf(st, "addr=%07o", dibp->uba_addr);
    return SCPE_OK;
}

t_stat
uba_set_br(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE  *dptr;
    DIB     *dibp;
    t_value  br;
    t_stat   r;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;

    dibp = (DIB *) dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;

    br = get_uint (cptr, 3, 07, &r);

    if (r != SCPE_OK)
        return r;

    if (br < 4 || br > 7)
        return SCPE_ARG;
    dibp->uba_br = (uint16)br;
    return SCPE_OK;
}

t_stat
uba_show_br (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    DEVICE  *dptr = find_dev_from_unit(uptr);
    DIB *dibp = (DIB *) dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;
    fprintf(st, "br=%o", dibp->uba_br);
    return SCPE_OK;
}

t_stat
uba_set_vect(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE  *dptr;
    DIB     *dibp;
    t_value  vect;
    t_stat   r;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;

    dibp = (DIB *) dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;

    vect = get_uint (cptr, 8, 0377, &r);

    if (r != SCPE_OK)
        return r;

    dibp->uba_vect = (uint16)vect;
    return SCPE_OK;
}

t_stat
uba_show_vect (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    DEVICE  *dptr = find_dev_from_unit(uptr);
    DIB *dibp = (DIB *) dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;
    fprintf(st, "vect=%03o", dibp->uba_vect);
    return SCPE_OK;
}

t_stat
uba_set_ctl(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE  *dptr;
    DIB     *dibp;
    t_value  ctl;
    t_stat   r;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;

    dibp = (DIB *) dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;

    ctl = get_uint (cptr, 4, 017, &r);

    if (r != SCPE_OK)
        return r;

    if (ctl != 1 && ctl != 3)
       return SCPE_ARG;
    dibp->uba_ctl = (uint16)ctl;
    return SCPE_OK;
}

t_stat
uba_show_ctl (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    DEVICE  *dptr = find_dev_from_unit(uptr);
    DIB *dibp = (DIB *) dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;
    fprintf(st, "uba%o", dibp->uba_ctl);
    return SCPE_OK;
}


