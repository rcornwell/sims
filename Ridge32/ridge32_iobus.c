/* Ridge32_iobus.c: Ridge 32 I/O bus simulation.

   Copyright (c) 2019, Richard Cornwell

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

   The Ridge 32 system allowed for several different I/O controllers to be put
   on the bus. Priority was determined by position on the bus. Because some of
   the devices actually controlled several different unit types, the iobus module
   is used to tell the simulator what boards are installed and what device
   address they are at.

   Devices addressed as 1 and 2 are special as they were the only two that
   could be bootstraped. 

   The simulator supports up to 8 I/O boards consisting of:

    FLP0/1: Floppy disk/terminal/line printer.
        flp: 4 Units.
        term: 8 Units. Unit zero is console.
        lpt: 2 Units.
    DSK0/1: Priam disk controller 60 and 142MB devices.
        dsk0: 4 units.
        dsk1: 4 units.
    SMD0/1: SMD disk controller.
        smd0: 4 units.
        smd1: 4 units.
    DISP:   Monochrome display.
    TAPE:   Tape Controller.
        tp:  4 units.
    DRNIU: DR11 interface to NIU-150 network interface.
        niu: 1 unit.
    Empty: No card installed.

*/

#include "ridge32_defs.h"



int                      empty_read(uint32 dev, uint32 *data);
int                      empty_write(uint32 dev, uint32 data);
int                      empty_iord(uint32 *data);

DIB     *dev_table[256];       /* Device table */
uint8    slot_dev[8];           /* Device in slot */

DIB   null_dev = {0, 0, &empty_read, &empty_write, &empty_iord};



uint8
io_dcbread_byte(UNIT *uptr, int off)
{
     uint32   word;

     word = M[(uptr->DCB + off) >> 2];
     word >>= 8 * (3 - (off & 0x3));
     word &= 0xff;
     return (uint8) word;
}

uint16
io_dcbread_half(UNIT *uptr, int off)
{
     uint32   word1;
     uint32   word2;

     word1 = M[(uptr->DCB + off) >> 2];
     word1 >>= 8 * (3 - (off & 0x3));
     word1 &= 0xff;
     word2 = M[(uptr->DCB + off + 1) >> 2];
     word2 >>= 8 * (3 - ((off + 1) & 0x3));
     word2 &= 0xff;
     return (uint16) ((word1 << 8) | word2);
}

uint32
io_dcbread_addr(UNIT *uptr, int off)
{
     uint32   word1;
     uint32   word2;
     uint32   word3;

     word1 = M[(uptr->DCB + off) >> 2];
     word1 >>= 8 * (3 - (off & 0x3));
     word1 &= 0xff;
     word2 = M[(uptr->DCB + off + 1) >> 2];
     word2 >>= 8 * (3 - ((off + 1) & 0x3));
     word2 &= 0xff;
     word3 = M[(uptr->DCB + off + 2) >> 2];
     word3 >>= 8 * (3 - ((off + 2) & 0x3));
     word3 &= 0xff;
     return ((word1 << 16) | (word2 << 8) | word3);
}

void
io_dcbread_blk(UNIT *uptr, int off, uint8 *data, int sz)
{
     io_read_blk(uptr->DCB + off, data, sz);
}

void
io_read_blk(int addr, uint8 *data, int sz)
{
     uint32   word;
     int      i;

     for (i = 0; sz > 0; sz--, addr++) {
         word = M[(addr & 0xffffff) >> 2];
         word >>= 8 * (3 - (addr & 0x3));
         data[i++] = (uint8)(word & 0xff);
         if ((addr & 3) == 0) {
              word = M[addr >> 2];
              sim_debug(DEBUG_DETAIL, &cpu_dev, "Read %06x, data=%08x '%c%c%c%c'\n",
                   addr, word, isprint((word >> 24) & 0xff)?(word >> 24)& 0xff:'.',
                   isprint((word >> 16) & 0xff)?(word >> 16)& 0xff:'.',
                   isprint((word >> 8) & 0xff)?(word >> 8)& 0xff:'.',
                   isprint(word & 0xff)?word& 0xff:'.');
         }
     }
}


void
io_dcbwrite_byte(UNIT *uptr, int off, uint8 data)
{
     int      offset;
     int      addr = uptr->DCB + off;

     offset = 8 * (3 - (addr & 0x3));
     M[addr >> 2] &= ~(0xff << offset);
     M[addr >> 2] |= ((uint32)(data)) << offset;
     if ((addr & ~3) == 0x0e1200)
        sim_debug(DEBUG_CMD, &cpu_dev, "Set %08x %08x\n", addr, M[addr>>2]);

     return;
}

void
io_dcbwrite_half(UNIT *uptr, int off, uint16 data)
{
     io_dcbwrite_byte(uptr, off, (uint8)((data >> 8) & 0xff));
     io_dcbwrite_byte(uptr, off+1, (uint8)(data & 0xff));
     return;
}

void
io_dcbwrite_addr(UNIT *uptr, int off, uint32 data)
{
     io_dcbwrite_byte(uptr, off, (uint8)((data >> 16) & 0xff));
     io_dcbwrite_byte(uptr, off+1, (uint8)((data >> 8) & 0xff));
     io_dcbwrite_byte(uptr, off+2, (uint8)(data & 0xff));
     return;
}

void
io_dcbwrite_blk(UNIT *uptr, int off, uint8 *data, int sz)
{
     io_write_blk(uptr->DCB + off, data, sz);
}

void
io_write_blk(int addr, uint8 *data, int sz)
{
     int      offset;
     int      i;

     sim_debug(DEBUG_DETAIL, &cpu_dev, "blkWrite %06x, len=%4x\n", addr, sz);
     for (i = 0; sz > 0; sz--, addr++) {
         offset = 8 * (3 - (addr & 0x3));
         M[addr >> 2] &= ~(0xff << offset);
         M[addr >> 2] |= ((uint32)(data[i++])) << offset;
         if ((addr & 3) == 3) {
              uint32 word = M[addr >> 2];
              sim_debug(DEBUG_DETAIL, &cpu_dev, "Write %06x, data=%08x '%c%c%c%c'\n",
                   addr, word, isprint((word >> 24) & 0xff)?(word >> 24)& 0xff:'.',
                   isprint((word >> 16) & 0xff)?(word >> 16)& 0xff:'.',
                   isprint((word >> 8) & 0xff)?(word >> 8)& 0xff:'.',
                   isprint(word & 0xff)?word& 0xff:'.');
         }
     if ((addr & ~3) == 0x0e1200)
        sim_debug(DEBUG_CMD, &cpu_dev, "Set %08x %08x\n", addr, M[addr>>2]);
     }
}

int
io_read(uint32 dev_data, uint32 *data)
{
    int     dev;
    int     r;

    dev = (dev_data >> 24) & 0xff;
    r = dev_table[dev]->io_read(dev_data, data);
    sim_debug(DEBUG_CMD, &cpu_dev, "Read %02x, dev=%08x data=%08x\n", dev, dev_data, *data);
    return r;
}

int
io_write(uint32 dev_data, uint32 data)
{
    int     dev;
    int     r;

    dev = (dev_data >> 24) & 0xff;
    sim_debug(DEBUG_CMD, &cpu_dev, "Write %02x, dev=%08x data=%08x\n", dev, dev_data, data);
    r = dev_table[dev]->io_write(dev_data, data);
    return r;
}


/*
 * Check if interrupt pending. if not return 0.
 * If pending, find device and set IORD into data.
 * then return 1.
 */
int
io_rd(uint32 *data)
{
    int      i;

    if (ext_irq) {
        for (i = 0; i < 8; i++) {
            if (slot_dev[i] != 0) {
               if (dev_table[slot_dev[i]]->io_iord(data)) {
                  return 1;
               }
            }
         }
         ext_irq = 0;
     }
     return 0;
}


int
empty_read(uint32 dev, uint32 *data)
{
    return 2;
}

int
empty_write(uint32 dev, uint32 data)
{
    return 2;
}

int
empty_iord(uint32 *data)
{
    /* Should never occur */
    return 0;
}


t_stat
chan_set_devs()
{
    int                 i;

    for (i = 0; i < 256; i++) {
        dev_table[i] = &null_dev;        /* Empty device */
    }
    for (i = 0; i < 8; i++) {            /* Slots are empty */
        slot_dev[i] = 0;
    }

    /* Build channel array */
    for (i = 0; sim_devices[i] != NULL; i++) {
        DEVICE             *dptr = sim_devices[i];
        DIB                *dibp = (DIB *) dptr->ctxt;
        int                 addr;
        int                 slot;

        /* If no DIB, not channel device */
        if (dibp == NULL)
            continue;
        /* Skip disabled devices */
        if (dptr->flags & DEV_DIS)
            continue;
        addr = dibp->dev_num;
        slot = dibp->slot_num;
        if (dev_table[addr] != &null_dev) {
            sim_printf("Device conflict %02x\n\r", addr);
            return SCPE_IERR;
        }
        if (slot_dev[slot] != 0) {
            sim_printf("Slot error %02x %x\n\r", addr, slot);
            return SCPE_IERR;
        }
        if (dibp->dev_mask != 0) {
            int        addr2 = dibp->dev_num & ~dibp->dev_mask;
            int        i;

            /* Check for conflict */
            for (i = addr2; i != addr; i++) {
                if (dev_table[i] != &null_dev) {
                    sim_printf("Device conflict %02x\n\r", i);
                    return SCPE_IERR;
                }
            }
            /* Assign devices */
            for (i = addr2; i != addr; i++)
                dev_table[i] = dibp;
        }
        dev_table[addr] = dibp;
        slot_dev[slot] = addr;
    }
    return SCPE_OK;
}

/* Sets the address of a device */
t_stat
set_dev_addr(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE             *dptr;
    DIB                *dibp;
    t_value             newdev;
    t_stat              r;
    int                 addr;
    int                 slot;

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

    newdev = get_uint (cptr, 16, 0xff, &r);

    if (r != SCPE_OK)
        return r;

    addr = dibp->dev_num;
    slot = dibp->slot_num;

    /* Clear out existing entry */
    dev_table[addr] = &null_dev;

    /* Check if device already at newdev */
    if (dev_table[newdev] != &null_dev)
        r = SCPE_ARG;

    /* If not, point to new dev, else restore old */
    if (r == SCPE_OK)
       addr = newdev;

    dibp->dev_num = addr;
    /* Update device entry */
    dev_table[addr] = dibp;
    return r;
}

t_stat
show_dev_addr(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    DEVICE             *dptr;
    DIB                *dibp;
    int                 addr;


    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    dibp = (DIB *) dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;

    addr = dibp->dev_num;
    fprintf(st, "dev=%02x", addr);

    /* Multiunit device */ 
    if (dibp->dev_mask != 0) {
        int        addr2 = dibp->dev_num & ~dibp->dev_mask;

        /* Check for conflict */
        for (; addr2 != addr; addr2++)
            fprintf(st, ",%02x", addr2);
    }
    return SCPE_OK;
}



/* Put a device at a specific slot */
t_stat
set_slot_num(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE             *dptr;
    DIB                *dibp;
    t_value             newslot;
    t_stat              r;
    int                 addr;
    int                 slot;

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

    newslot = get_uint (cptr, 16, 0x8, &r);

    if (r != SCPE_OK)
        return r;

    if (newslot > 8)
        return SCPE_ARG;

    addr = dibp->dev_num;
    slot = dibp->slot_num;

    dibp->slot_num = newslot;
    slot_dev[slot] = 0;
    slot_dev[newslot] = addr;
    return r;
}

t_stat
show_slot_num(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    DEVICE             *dptr;
    DIB                *dibp;
    int                 slot;

    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit(uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    dibp = (DIB *) dptr->ctxt;
    if (dibp == NULL)
        return SCPE_IERR;
    slot = dibp->slot_num;
    fprintf(st, "slot=%d", slot);
    return SCPE_OK;
}

