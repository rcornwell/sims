/* icl1900_stdio.c: ICL 1900 standard I/O devices

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

*/

#include "icl1900_defs.h"

/* Define style bits */

#define OUTPUT           B0    /* Input/Output flag */
#define BACKWARD         B1    /* Forward/Backword */
#define WORDCCW          B2    /* Character/Word */
#define MULTICHN         B3    /* Single/Multi Channel */
#define CWRECHARGE       B4    /* Recharge CW when empty */
#define GATHER           B5    /* Gather */


DIB *devs[64];                 /* Pointer to device DIB entry */

DIB nul_dib = { 0, NULL, NULL, NULL};


t_stat
chan_set_devs()
{
     unsigned int     i;
     uint32           j;
     int              chan;

     /* Clear device table */
     for (i = 0; i < sizeof(devs)/sizeof(DIB *); i++)
         devs[i] = NULL;

     /* Add in hidden channels */
     devs[22] = &nul_dib;
     devs[23] = &nul_dib;
     /* Scan all devices and assign to channel */
     for(i = 0; sim_devices[i] != NULL; i++) {
         DIB       *dibp = (DIB *) sim_devices[i]->ctxt;

         /* Check if device a channel device */
         if (dibp == NULL)
            continue;
         /* If device is disabled, don't add it */
         if (sim_devices[i]->flags & DEV_DIS)
            continue;
         if (dibp->type & BLK_DEV) {
            int f = 1;

            chan = GET_UADDR(sim_devices[i]->flags);
            /* Make sure it is in range */
            if (chan < 2 || (chan + sim_devices[i]->numunits) > 36)
               continue;
            for (j = 0; j < sim_devices[i]->numunits; j++) {
                if (sim_devices[i]->units[j].flags & UNIT_DIS)
                    continue;
                if (devs[chan+j] != NULL) {
                    sim_printf("Conflict between devices %d %s\n", chan+j, sim_devices[i]->name);
                    f = 0;
                }
            }
            if (f) {
               for (j = 0; j < sim_devices[i]->numunits; j++) {
                   if (sim_devices[i]->units[j].flags & UNIT_DIS)
                       continue;
                   devs[chan+j] = dibp;
               }
            }
         } else if (dibp->type & MULT_DEV) {
            chan = GET_UADDR(sim_devices[i]->flags);
            /* Make sure it is in range */
            if (chan < 2 || chan > 36)
               continue;
            if (devs[chan] != NULL) {
               sim_printf("Conflict between devices %d %s\n", chan, sim_devices[i]->name);
            } else {
               devs[chan] = dibp;
            }
         } else {
            UNIT  *uptr = sim_devices[i]->units;
            uint32 unit;
            for (unit = 0; unit < sim_devices[i]->numunits; unit++, uptr++) {
                /* If disabled, skip it */
                if (uptr->flags & UNIT_DIS)
                   continue;
                chan = GET_UADDR(uptr->flags);
                /* Make sure it is in range */
                if (chan < 2 || chan > 36)
                   continue;
                if (devs[chan] != NULL) {
                   sim_printf("Conflict between devices %d %s%d\n", chan, sim_devices[i]->name, unit);
                } else {
                   devs[chan] = dibp;
                }
            }
         }
     }
     return SCPE_OK;
}


/* Set the device onto a given channel */
t_stat
set_chan(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
     DEVICE         *dptr;
     DIB            *dibp;
     int             new_chan;
     int             cur_chan;
     t_stat          r;
     uint32          i;

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
     new_chan = get_uint(cptr, 10, 37, &r);
     if (r != SCPE_OK)
         return r;
     if (new_chan < 4)     /* Lowest channel is 4 */
         return SCPE_ARG;
     /* Find channel device is current on and remove it */
     if (dibp->type & BLK_DEV) {
        if (new_chan < 2 || (new_chan + dptr->numunits) > 36)
            return SCPE_ARG;
        /* Validate that we can actually set this device. */
        for (i = 0; i < dptr->numunits; i++) {
            if (dptr->units[i].flags & UNIT_DIS)
                continue;
            if (devs[new_chan+i] != dibp && devs[new_chan+i] != NULL)
                return SCPE_ARG;
        }
        /* Free up current device */
        cur_chan = GET_UADDR(dptr->flags);
        for (i = 0; i < dptr->numunits; i++) {
            if (dptr->units[i].flags & UNIT_DIS)
                continue;
            if (devs[cur_chan+i] == dibp)
                devs[cur_chan] = NULL;
        }
     } else if (dibp->type & MULT_DEV) {
         cur_chan = GET_UADDR(dptr->flags);
     } else {
         cur_chan = GET_UADDR(uptr->flags);
     }
     if (dibp == devs[cur_chan])
         devs[cur_chan] = NULL;
     /* If device is disabled, set to whatever the user wants */
     if (dptr->flags & DEV_DIS) {
         if (dibp->type & (MULT_DEV|BLK_DEV)) {
             dptr->flags &= ~UNIT_M_ADDR;
             dptr->flags |= UNIT_ADDR(new_chan);
         } else {
             uptr->flags &= ~UNIT_M_ADDR;
             uptr->flags |= UNIT_ADDR(new_chan);
         }
         return SCPE_OK;
     }

     if (dibp->type & BLK_DEV) {
         dptr->flags &= ~UNIT_M_ADDR;
         dptr->flags |= UNIT_ADDR(new_chan);
         for (i = 0; i < dptr->numunits; i++) {
             if (dptr->units[i].flags & UNIT_DIS)
                 continue;
             devs[new_chan+i] = dibp;
         }
         return SCPE_OK;
     }
     if (devs[new_chan] == NULL) {
         if (dibp->type & MULT_DEV) {
             dptr->flags &= ~UNIT_M_ADDR;
             dptr->flags |= UNIT_ADDR(new_chan);
         } else {
             uptr->flags &= ~UNIT_M_ADDR;
             uptr->flags |= UNIT_ADDR(new_chan);
         }
         devs[new_chan] = dibp;
         return SCPE_OK;
     } else {
        sim_printf("Device already on channel %d\n", new_chan);
     }
     return SCPE_ARG;
}

/* Print the channel the device is on */
t_stat
get_chan(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
     DEVICE          *dptr;
     DIB             *dibp;
     int             chan;

     if (uptr == NULL)
         return SCPE_IERR;
     dptr = find_dev_from_unit(uptr);
     if (dptr == NULL)
         return SCPE_IERR;
     dibp = (DIB *) dptr->ctxt;
     if (dibp == NULL)
         return SCPE_IERR;
     if (dibp->type & BLK_DEV) {
         chan = GET_UADDR(dptr->flags) + (uptr - dptr->units);
     } else if (dibp->type & MULT_DEV) {
         chan = GET_UADDR(dptr->flags);
     } else {
         chan = GET_UADDR(uptr->flags);
     }
     fprintf(st, "DEV=%d", chan);
     return SCPE_OK;
}

int
get_ccw(int dev, uint32 *addr, uint8 type) {
    int      cw_addr;
    uint32   cw0;            /* Holding registers for current control word */
    uint32   cw1;

    if (io_flags & EXT_IO) {
        cw_addr = 256+4*dev;
        cw0 = M[cw_addr];
        cw1 = M[cw_addr+1];
        *addr = cw1;
        if (type & WORD_DEV)
           cw0 |= WORDCCW;
        if (cw0 & WORDCCW) {
            if (cw0 & BACKWARD) {
                cw1 = ((cw1 + M22) & M22) | (cw1 & CMASK);
                *addr = cw1;
            } else
                cw1 = ((cw1 + 1) & M22) | (cw1 & CMASK);
        } else {
            if (cw0 & BACKWARD) {
                if (cw1 & CMASK) {
                    cw1 -= B1;
                } else {
                    cw1 = ((cw1 - 1) & M22) | CMASK;
                }
                *addr = cw1;
            } else {
                if ((cw1 & CMASK) == CMASK) {
                    cw1 = (cw1 + 1) & M22;
                } else {
                    cw1 += B1;
                }
            }
        }
        cw0 = ((cw0 - 1) & M15)  | (cw0 & CNTMSK);
        if ((cw0 & M15) == 0) {
            if ((cw0 & (CWRECHARGE|GATHER)) == (CWRECHARGE)) {
                cw0 = M[cw_addr+2];
                cw1 = M[cw_addr+3];
            } else if ((cw0 & GATHER) != 0) {
                int a;
                if ((cw0 & CWRECHARGE) != 0)
                    M[cw_addr+3] = M[cw_addr+2];
                a = M[cw_addr+3];
                cw0 = M[a & M22];
                cw1 = M[(a + 1) & M22];
                M[cw_addr+3] = ((a + 2) & M22) | (a & CMASK);
            }
            M[cw_addr] = cw0;
            M[cw_addr+1] = cw1;
            return (cw0 & M15) == 0;
        }
        M[cw_addr] = cw0;
        M[cw_addr+1] = cw1;
    } else {
        cw_addr = 64+dev;
        cw0 = M[cw_addr];
        if (type & WORD_DEV) {
            *addr = cw0 & M15;
            cw0 = ((cw0 + 1) & M15) | ((cw0 + CNTMSK) & CNTMSK);
            cw1 = cw0 & CNTMSK;
        } else {
            *addr = cw0 & (CMASK|M15);
            if ((cw0 & CMASK) == CMASK) {
                cw0 = ((cw0 + 1) & M15) | (cw0 & CHCMSK);
            } else {
                cw0 += B1;
            }
            cw1 = (cw0 + CHCMSK) & CHCMSK;
            cw0 = ((CMASK|M15) & cw0) | cw1;
        }
        M[cw_addr] = cw0;
        if (cw1 == 0) {
            if (type & LONG_BLK) {
                cw1 = (M[cw_addr + 64] - 1)  & FMASK;
                M[cw_addr + 64]  = cw1;
            }
            if (type & SPEC_HES) {
                M[cw_addr] = M[cw_addr+1];
                return 0;
            }
            return (cw1 == 0);
        }
    }
    return 0;
}


/* Talk to non-standard interface devices */
void
chan_nsi_cmd(int dev, uint32 cmd) {
    DIB            *dibp = devs[dev & 077];

    if (dibp != NULL && dibp->nsi_cmd != NULL) {
       (dibp->nsi_cmd)(dev, cmd);
    }
}

/* Talk to non-standard interface devices */
void
chan_nsi_status(int dev, uint32 *resp) {
    DIB            *dibp = devs[dev & 077];

    *resp = 0;
    if (dibp != NULL && dibp->nsi_cmd != NULL) {
       (dibp->nsi_status)(dev, resp);
    }
}


/* Hessitation operations */
void
chan_send_cmd(int dev, uint32 cmd, uint32 *resp) {
    DIB            *dibp = devs[dev & 077];

    *resp = 0;
    if (dibp != NULL && dibp->si_cmd != NULL) {
       (dibp->si_cmd)(dev, cmd, resp);
    }
}

/* Transfer date between device and memory */
int
chan_input_char(int dev, uint8 *data, int eor) {
    DIB         *dibp = devs[dev & 077];
    int          r;
    int          c;
    uint32       addr;
    uint32       mb;

    /* Make sure device assign here */
    if (dibp == NULL)
        return -1;

    /* Check if right type */
    if ((dibp->type & WORD_DEV) != 0)
        return -2;

    /* Get address of next character. */
    r = get_ccw(dev, &addr, dibp->type);
    c = (addr >> 22) & 3;
    c = 6 * (3 - c);
    addr &= M22;
    if (addr < 8)
       mb = XR[addr];
    else
       mb = M[addr];
    mb &= ~(077 << c);
    mb |= ((uint32)(*data) & 077) << c;
    if (addr < 8)
       XR[addr] = mb;
    M[addr] = mb;
    return r;
}

int
chan_output_char(int dev, uint8 *data, int eor) {
    DIB         *dibp = devs[dev & 077];
    int          r;
    int          c;
    uint32       addr;
    uint32       mb;

    /* Make sure device assigne here */
    if (dibp == NULL)
        return -1;

    /* Check if right type */
    if ((dibp->type & WORD_DEV) != 0)
        return -2;

    /* Get address of next character. */
    r = get_ccw(dev, &addr, dibp->type);
    c = (addr >> 22) & 3;
    c = 6 * (3 - c);
    addr &= M22;
    if (addr < 8)
       mb = XR[addr];
    else
       mb = M[addr];
    *data = (uint8)(mb >> c) & 077;
    return r;
}

int
chan_input_word(int dev, uint32 *data, int eor) {
    DIB         *dibp = devs[dev & 077];
    int          r;
    uint32       addr;

    /* Make sure device assigne here */
    if (dibp == NULL)
        return -1;

    /* Check if right type */
    if ((dibp->type & WORD_DEV) == 0)
        return -2;

    /* Get address of next word. */
    r = get_ccw(dev, &addr, dibp->type);
    addr &= M22;
    if (addr < 8)
       XR[addr] = *data;
    M[addr] = *data;
    return r;
}

int
chan_output_word(int dev, uint32 *data, int eor) {
    DIB         *dibp = devs[dev & 077];
    int          r;
    uint32       addr;

    /* Make sure device assigne here */
    if (dibp == NULL)
        return -1;

    /* Check if right type */
    if ((dibp->type & WORD_DEV) == 0)
        return -2;

    /* Get address of next word. */
    r = get_ccw(dev, &addr, dibp->type);
    addr &= M22;
    if (addr < 8)
       *data = XR[addr];
    else
       *data = M[addr];
    return r;
}

void
chan_set_done(int dev) {
    if (dev < 24)
       SR64 |= B2 >> dev;
    else
       SR65 |= ((io_flags & EXT_IO) ? B1 : B2) >> (dev - 24);
}

void
chan_clr_done(int dev) {
    if (dev < 24)
       SR64 &= ~(B2 >> dev);
    else
       SR65 &= ~(((io_flags & EXT_IO) ? B1 : B2) >> (dev - 24));
}


