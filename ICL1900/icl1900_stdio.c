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


DIB *devs[32];                 /* Pointer to device DIB entry */

/* Fake DIB entries for the CTY devices */
DIB ctyi_dib = { 2, CHAR_DEV, ctyi_cmd};
DIB ctyo_dib = { 3, CHAR_DEV, ctyo_cmd};
DIB nul1_dib = { 22, 0, 0};
DIB nul2_dib = { 23, 0, 0};

uint32   cw0;            /* Holding registers for current control word */
uint32   cw1;

t_stat
chan_set_devs()
{
     int  i;

     /* Clear device table */
     for (i = 0; i < sizeof(devs)/sizeof(DIB *); i++)
         devs[i] = NULL;

     devs[22-4] = &nul1_dib;   /* Add in hidden channels */
     devs[23-4] = &nul2_dib;
     /* Scan all devices and assign to channel */
     for(i = 0; sim_devices[i] != NULL; i++) {
         DIB       *dibp = (DIB *) sim_devices[i]->ctxt;
         int        chan;

         /* Check if device a channel device */
         if (dibp == NULL)
            continue;
         /* If device is disabled, don't add it */
         if (sim_devices[i]->flags & DEV_DIS)
            continue;
         chan = dibp->channel - 4;
         if (chan < 0)
            continue;
         if (devs[chan] != NULL) {
            fprintf(stderr, "Conflict between devices %d %s\n", chan, sim_devices[i]->name);
         } else {
            devs[chan] = dibp;
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
     int             newchan;
     t_stat          r;

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
     newchan = get_uint(cptr, 10, 37, &r);
     if (r != SCPE_OK)
         return r;
     if (newchan < 4)     /* Lowest channel is 4 */
         return SCPE_ARG;
     newchan -= 4;
     if (dibp == devs[dibp->channel-4])
         devs[dibp->channel-4] = NULL;
     /* If device is disabled, set to whatever the user wants */
     if (dptr->flags & DEV_DIS) {
         dibp->channel = newchan;
         return SCPE_OK;
     }
 
     if (devs[newchan] != NULL) {
        devs[newchan] = dibp;
        dibp->channel = newchan+4;
        return SCPE_OK;
     } else {
        fprintf(stderr, "Device already on channel %d\n", newchan+4);
        devs[dibp->channel] = dibp;
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
     fprintf(st, "Chan=%d", dibp->channel);
     return SCPE_OK;
}

int 
get_ccw(int dev, uint32 *addr, uint8 type) {
    int cw_addr = 256+4*dev;
#if 0
    cw0 = M[64+dev];
#endif
    cw0 = M[cw_addr];
    cw1 = M[cw_addr+1];
    *addr = cw1;
    if (cw0 & WORDCCW) {
        if (cw0 & BACKWARD) 
            cw1 = ((cw1 + M22) & M22) | (cw1 & CMASK);
        else
            cw1 = ((cw1 + 1) & M22) | (cw1 & CMASK);
    } else {
        if (cw0 & BACKWARD) {
            if (cw1 & CMASK) {
                cw1 -= B1;
            } else {
                cw1 = ((cw1 - 1) & M22) | CMASK;
            }
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
    return 0;
}

/* Hessitation operations */
void
chan_send_cmd(int dev, uint32 cmd, uint32 *resp) {
    DIB            *dibp = NULL;
    int             d = dev & 077;

    if (dev >= 4 && dev <= 36)
       dibp = devs[d - 4];
    else if (dev == 3)
       dibp = &ctyo_dib;
    else if (dev == 2)
       dibp = &ctyi_dib;

    *resp = 0;
    if (dibp != NULL && dibp->dev_cmd != NULL) {
       t_stat   r;

       r = (dibp->dev_cmd)((dev & 07700) | cmd, resp);
    } 
}

/* Transfer date between device and memory */
int
chan_input_char(int dev, uint8 *data, int eor) {
    int          r;
    int          c;
    DIB         *dibp = NULL;
    uint32       addr;
    uint32       mb;


    /* Figure out DIB to find out type of device */
    if (dev >= 4 && dev <= 36)
        dibp = devs[dev - 4];
    else if (dev == 3)
        dibp = &ctyo_dib;
    else if (dev == 2)
        dibp = &ctyi_dib;

    /* Make sure device assigne here */
    if (dibp == NULL || dibp->dev_cmd == NULL)
        return -1;

    /* Check if right type */
    if ((dibp->type & WORD_DEV) != 0)
        return -2;

    /* Get address of next character. */
    r = get_ccw(dev, &addr, dibp->type);
    c = (addr >> 22) & 3;
    c = 6 * (3 - c);
    mb = M[addr & M22];
    mb &= ~(077 << c);
    mb |= ((uint32)(*data) & 077) << c;
    M[addr & M22] = mb;
    return r;
}

int
chan_output_char(int dev, uint8 *data, int eor) {
    int          r;
    int          c;
    DIB         *dibp = NULL;
    uint32       addr;
    uint32       mb;


    /* Figure out DIB to find out type of device */
    if (dev >= 4 && dev <= 36)
        dibp = devs[dev - 4];
    else if (dev == 3)
        dibp = &ctyo_dib;
    else if (dev == 2)
        dibp = &ctyi_dib;

    /* Make sure device assigne here */
    if (dibp == NULL || dibp->dev_cmd == NULL)
        return -1;

    /* Check if right type */
    if ((dibp->type & WORD_DEV) != 0)
        return -2;

    /* Get address of next character. */
    r = get_ccw(dev, &addr, dibp->type);
    c = (addr >> 22) & 3;
    c = 6 * (3 - c);
    mb = M[addr & M22];
    *data = (uint8)(mb >> c) & 077;
    return r;
}

int
chan_input_word(int dev, uint32 *data, int eor) {
    int          r;
    DIB         *dibp = NULL;
    uint32       addr;


    /* Figure out DIB to find out type of device */
    if (dev >= 4 && dev <= 36)
        dibp = devs[dev - 4];
    else if (dev == 3)
        dibp = &ctyo_dib;
    else if (dev == 2)
        dibp = &ctyi_dib;

    /* Make sure device assigne here */
    if (dibp == NULL || dibp->dev_cmd == NULL)
        return -1;

    /* Check if right type */
    if ((dibp->type & WORD_DEV) == 0)
        return -2;

    /* Get address of next word. */
    r = get_ccw(dev, &addr, dibp->type);
    M[addr & M22] = *data;
    return r;
}

int
chan_output_word(int dev, uint32 *data, int eor) {
    int          r;
    DIB         *dibp = NULL;
    uint32       addr;


    /* Figure out DIB to find out type of device */
    if (dev >= 4 && dev <= 36)
        dibp = devs[dev - 4];
    else if (dev == 3)
        dibp = &ctyo_dib;
    else if (dev == 2)
        dibp = &ctyi_dib;

    /* Make sure device assigne here */
    if (dibp == NULL || dibp->dev_cmd == NULL)
        return -1;

    /* Check if right type */
    if ((dibp->type & WORD_DEV) == 0)
        return -2;

    /* Get address of next word. */
    r = get_ccw(dev, &addr, dibp->type);
    *data = M[addr & M22];
    return r;
}

void
chan_set_done(int dev) {
    if (dev < 22) 
       SR64 |= B2 >> dev;
    else
       SR65 |= B1 >> (dev - 24);
}

void
chan_clr_done(int dev) {
    if (dev < 22) 
       SR64 &= ~(B2 >> dev);
    else
       SR65 &= ~(B1 >> (dev - 24));
}


