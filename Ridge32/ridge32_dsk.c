/* Ridge32_dsk.c: Ridge 32 Priam disk controller.

   Copyright (c) 2020, Richard Cornwell

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

#include "ridge32_defs.h"

#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_M_DTYPE    1
#define UNIT_DTYPE      (UNIT_M_DTYPE << UNIT_V_DTYPE)
#define GET_DTYPE(x)    (((x) >> UNIT_V_DTYPE) & UNIT_M_DTYPE)


#define DCB      u3              /* DCB pointer */
#define STATUS   u4              /* Last status */
#define CYL      u5              /* Current cylinder */
#define CMD      u6


#define WR_INH   0x8000
#define DSK_RD   0               /* Read command */
#define DSK_WR   1               /* Write command */
#define DSK_VFY  2               /* Verify data */
#define DSK_FMT  3               /* Format a track */
#define DSK_SEEK 4               /* Seek to cylinder */
#define DSK_RDH  5               /* Read Highest sector address */
#define DSK_RDF  6               /* Read full sector */
#define DSK_WRF  7               /* Write full sector */
#define DSK_HDR  0xE             /* Read headers */

/* DCB control block.

   DCB + 0C0

   0     GORDER
                        0 - Read
                        1 - Write
                        2 - Verify
                        3 - Format track
                        4 - Seek
                        5 - Return highest sector address.
                        6 - Read full Sector.
                        7 - Write full sector.

    1    SORDER         Not used.

     2   GSTAT       
                       0 - Ok.
                       1 - Not Ready.
                       2 - Timeout
                       3 - Equipment Fault.
                       4 - Write protect
                       5 - Ridge double bit error.
                       6 - Data overrun
                       7 - Missing AM.
                       8 - Can't find header.
                       9 - header CRC
                       A - data CRC
                       B - Seek error.
                      FF - Illegal parameter in DCB order.

    3 SSTAT             reserved.

    4 RETRIES

     5  RIDGE ADDR 
     6
     7

     8 BYTE COUNT
     9

     A BYTE COUNT READ.
     B

     C  Not used
     D  Header 4(bits), Cylinder (4bits).
     E  Cylinder
     F  Sector    0-17
    10 -1B  Data Labels
  */


#define SECT_SZ             1024
#define LBL_SZ                12

#define P142_DTYPE              0
#define P60_DTYPE               1

struct _dsk_type {
      uint32       cyl;
      uint16       hds;
      uint16       sect;
      uint16       bpt; 
      char        *model;
} dsk_type[] = {
      {1121, 7, 18, 20160, "Priam142"},
      {1121, 3, 18, 20160, "Priam60"},
      {0, 0, 0, 0, NULL},
};


int    dsk_read(uint32 dev, uint32 *data);
int    dsk_write(uint32 dev, uint32 data);
int    dsk_iord(uint32 *data);
void   dsk_start(UNIT *uptr, int drive);
t_stat dsk_svc (UNIT *uptr);
t_stat dsk_boot (int32, DEVICE *);
t_stat dsk_set_type(UNIT *, int32, CONST char *, void *);
t_stat dsk_attach(UNIT *uptr, CONST char *cptr);
t_stat dsk_detach(UNIT *uptr);
t_stat dsk_reset(DEVICE *dp);

uint8  dsk_sect_lab[LBL_SZ];
uint8  dsk_buf[SECT_SZ];

/* Device context block */
struct ridge_dib dsk_dib = {2, 2, dsk_read, dsk_write, dsk_iord, 0};

MTAB dsk_mod[] = {
    {UNIT_DTYPE, (P142_DTYPE << UNIT_V_DTYPE), "P142", "P142", &dsk_set_type },
    {UNIT_DTYPE, (P60_DTYPE << UNIT_V_DTYPE), "P60", "P60", &dsk_set_type },
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "SLOT", "SLOT", &set_slot_num,
        &show_slot_num, NULL},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    { 0 }
    };


UNIT                dsk_unit[] = {
    {UDATA(&dsk_svc, UNIT_ATTABLE | UNIT_DISABLE | UNIT_FIX |
                       (P142_DTYPE << UNIT_V_DTYPE), 0)},
    {UDATA(&dsk_svc, UNIT_ATTABLE | UNIT_DISABLE | UNIT_FIX |
                       (P142_DTYPE << UNIT_V_DTYPE), 0)},
    {UDATA(&dsk_svc, UNIT_ATTABLE | UNIT_DISABLE | UNIT_FIX |
                       (P142_DTYPE << UNIT_V_DTYPE), 0)},
    {UDATA(&dsk_svc, UNIT_ATTABLE | UNIT_DISABLE | UNIT_FIX |
                       (P142_DTYPE << UNIT_V_DTYPE), 0)},
};


DEVICE              dsk_dev = {
    "DSK", dsk_unit, NULL, dsk_mod,
    4, 16, 24, 1, 16, 8,
    NULL, NULL, &dsk_reset, &dsk_boot, &dsk_attach, &dsk_detach,
    &dsk_dib, DEV_DEBUG | DEV_DISABLE, 0, dev_debug, NULL, NULL, 
};




struct _dcb {
    uint8           retries;
    uint32          addr;        /* transfer address */
    uint32          count;       /* byte count */
    uint32          xcount;      /* transfer count */
    uint8           hd;          /* Head */
    uint16          cyl;         /* Cylinder */
    uint8           sect;        /* Sector */
} dsk_dcb[4];


int
dsk_read(uint32 dev, uint32 *data)
{
    UNIT     *uptr = &dsk_unit[0];

    *data = uptr->STATUS & 0x03FF00;
    *data |= (dsk_dib.dev_num << 24) & 0xff000000;
    *data |= 0x400000;
    sim_debug(DEBUG_EXP, &dsk_dev, "read status %8x\n\r", *data);
    return (uptr->STATUS & 0x2) ? 1 : 0;
}

int
dsk_write(uint32 dev, uint32 data)
{
    UNIT     *uptr = &dsk_unit[0];
    UNIT     *duptr;
    int       cmd = (data >> 24) & 0xff;
    int       drive = cmd & 3;
    int       offset = drive << 6;;
    int       i;

    /* Check if command can be accepted */
    if (uptr->STATUS & 1) {
        return 1;
    }
    sim_debug(DEBUG_CMD, &dsk_dev, "Dsk start %02x\n", cmd);
    switch (cmd) {
    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83:

         /* Find actual unit */
         duptr = &dsk_unit[drive];
         /* Read first word of DCB + 0xC0 */
         duptr->CMD &= WR_INH;
         duptr->CMD |= io_dcbread_byte(uptr, offset);
         dsk_dcb[drive].addr = io_dcbread_addr(uptr, offset + 0x5);
         dsk_dcb[drive].count = io_dcbread_half(uptr, offset + 0x8);
         dsk_dcb[drive].xcount = 0;
         dsk_dcb[drive].hd = io_dcbread_byte(uptr, offset + 0xd);
         dsk_dcb[drive].cyl = io_dcbread_byte(uptr, offset + 0xe);
         dsk_dcb[drive].sect = io_dcbread_byte(uptr, offset + 0xf);
         dsk_dcb[drive].cyl |= (dsk_dcb[drive].hd & 0xf) << 8;
         dsk_dcb[drive].hd = (dsk_dcb[drive].hd >> 4) & 0xf;
         
         if (dsk_dcb[drive].count == 0)
             dsk_dcb[drive].count = 0x10000;
         cmd = duptr->CMD & 0xf;
         sim_debug(DEBUG_DETAIL, &dsk_dev,
               "Start Disk %2x %6x %4x c=%4d h=%d s=%2d\n",
               duptr->CMD,  dsk_dcb[drive].addr, dsk_dcb[drive].count,
               dsk_dcb[drive].cyl, dsk_dcb[drive].hd, dsk_dcb[drive].sect);
         if (cmd > 7 && cmd != 0xe) {
             /* Invalid */
             io_dcbwrite_byte(uptr, offset + 0x2, 0xff);
             uptr->STATUS = 0x40FF01 | (drive << 16);
             ext_irq = 1;
             return 0;
         }
         if (cmd == DSK_RDH)
            sim_activate(duptr, 10);
         else
            sim_activate(duptr, 100);
         break;

    case 0xc0:           /* Boot floppy left */
         (void)dsk_boot(0, &dsk_dev);
         break;

    case 0xc1:           /* Update DCB */
         uptr->DCB = (M[0x3c13c >> 2] & 0xffff) << 8;
         uptr->STATUS = 0x400001;
         io_dcbwrite_byte(uptr, offset + 0x2, 0x0);
         ext_irq = 1;
         break;

    case 0xc2:           /* Set write inhibit */
         uptr->CMD |= WR_INH;
         uptr->STATUS = 0x400001;
         ext_irq = 1;
         break;

    case 0xc3:           /* Clear write inhibit */
         uptr->CMD &= ~WR_INH;
         uptr->STATUS = 0x400001;
         ext_irq = 1;
         break;

    case 0xc4:           /* Update DCB */
         M[0x3c13c >> 2] &= 0xffff0000;
         M[0x3c13c >> 2] = (uptr->DCB >> 8) & 0xffff;
         io_dcbwrite_byte(uptr, offset + 0x2, 0x0);
         uptr->STATUS = 0x400001;
         ext_irq = 1;
         break;
    } 
    return 0;
}

int
dsk_iord(uint32 *data)
{
    UNIT     *uptr = &dsk_unit[0];
    *data = uptr->STATUS & 0x003FF00;
    *data |= ((uint32)dsk_dib.dev_num) << 24;
    *data |= 0x400000;
    /* Check if irq pending */
    if (uptr->STATUS & 1) {
        uptr->STATUS &= ~1;
        return 1;
    }
    return 0;
}


int
dsk_incsect(UNIT *uptr, int drive)
{
    int type = GET_DTYPE(uptr->flags);

    dsk_dcb[drive].sect++;
    if (dsk_dcb[drive].sect >= dsk_type[type].sect) {
        dsk_dcb[drive].sect = 0;
        dsk_dcb[drive].hd++;
        if (dsk_dcb[drive].hd >= dsk_type[type].hds) {
            dsk_dcb[drive].hd = 0;
            dsk_dcb[drive].cyl++;
            if (dsk_dcb[drive].hd >= dsk_type[type].cyl) {
                return 1;
            }
        }
    }
    return 0;
}

t_stat
dsk_svc (UNIT *uptr)
{
     UNIT *dcb = &dsk_unit[0];
     int drive = uptr - dsk_unit;
     int offset = drive << 6;;
     int type = GET_DTYPE(uptr->flags);
     int da;
     int flags;
     int len;
     int i;
     int sc;

     if ((uptr->CMD & 0xf) != DSK_RDH && uptr->CYL != dsk_dcb[drive].cyl) {
          /* Step in/out based on current cylinder, requested cylinder */
          if (uptr->CYL < dsk_dcb[drive].cyl)
              uptr->CYL++;
          else if (uptr->CYL > dsk_dcb[drive].cyl)
              uptr->CYL--;
          sim_activate(uptr, 1000);
          return SCPE_OK;
     }

     da = ((dsk_dcb[drive].cyl * dsk_type[type].hds + dsk_dcb[drive].hd) 
                   * dsk_type[type].sect) + dsk_dcb[drive].sect;
     switch (uptr->CMD & 0xf) {
     case DSK_RD:                  /* Read command */
          sim_debug(DEBUG_DETAIL, &dsk_dev,
               "read sector %6x %4x %4d %d %2d\n",
                dsk_dcb[drive].addr, dsk_dcb[drive].count, dsk_dcb[drive].cyl,
                 dsk_dcb[drive].hd, dsk_dcb[drive].sect);
          (void)sim_fseek(uptr->fileref, da * SECT_SZ, SEEK_SET);
          len = sim_fread(&dsk_buf, 1, sizeof(dsk_buf), uptr->fileref);
          while (len < sizeof(dsk_buf)) {
              dsk_buf[len++] = 0;
          }
          if (len > dsk_dcb[drive].count)
              len = dsk_dcb[drive].count;
         sim_debug(DEBUG_DETAIL, &dsk_dev, "Disk Read: %d bytes\n", len);
         for (i = 0; i < len; i++)  {
             sim_debug(DEBUG_DATA, &dsk_dev, "%02x ", dsk_buf[i]);
             if ((i & 0x1f) == 0x1f)
                  sim_debug(DEBUG_DATA, &dsk_dev, "\n");
         }
         sim_debug(DEBUG_DATA, &dsk_dev, "\n");
          io_write_blk(dsk_dcb[drive].addr, &dsk_buf[0], len);
          dsk_dcb[drive].count -= len;
          dsk_dcb[drive].xcount += len;
          dsk_dcb[drive].addr = (dsk_dcb[drive].addr + len) & 0xffffff;
          if (dsk_dcb[drive].count != 0) {
              if (dsk_incsect(uptr, drive)) {
                 io_dcbwrite_half(dcb, offset + 0xa, dsk_dcb[drive].xcount);
                 io_dcbwrite_byte(dcb, offset + 0x2, 0xb);
                 dsk_unit[0].STATUS = 0x400001 | (drive << 16);
                 ext_irq = 1;
                 break;
              }
              sim_activate(uptr, 100);
              return SCPE_OK;
          }
          io_dcbwrite_addr(dcb, offset + 0x5, dsk_dcb[drive].addr);
          io_dcbwrite_half(dcb, offset + 0x8, dsk_dcb[drive].count);
          io_dcbwrite_byte(dcb, offset + 0xd, (dsk_dcb[drive].hd << 4) |
                                               ((dsk_dcb[drive].cyl >> 8) & 0xf));
          io_dcbwrite_byte(dcb, offset + 0xe, dsk_dcb[drive].cyl & 0xff);
          io_dcbwrite_byte(dcb, offset + 0xf, dsk_dcb[drive].sect);
          io_dcbwrite_half(dcb, offset + 0xa, dsk_dcb[drive].xcount);
          io_dcbwrite_byte(dcb, offset + 0x2, 0);
          dsk_unit[0].STATUS = 0x400001 | (drive << 16);
          ext_irq = 1;
          break;

     case DSK_WR:                  /* Write command */
          sim_debug(DEBUG_DETAIL, &dsk_dev,
               "write sector %6x %4x %4d %d %2d\n",
                dsk_dcb[drive].addr, dsk_dcb[drive].count, dsk_dcb[drive].cyl,
                 dsk_dcb[drive].hd, dsk_dcb[drive].sect);
          (void)sim_fseek(uptr->fileref, da * SECT_SZ, SEEK_SET);
          len = sizeof(dsk_buf);
          if (len > dsk_dcb[drive].count)
              len = dsk_dcb[drive].count;
          io_read_blk(dsk_dcb[drive].addr, &dsk_buf[0], len);
          while (len < sizeof(dsk_buf)) {
              dsk_buf[len++] = 0;
          }
         sim_debug(DEBUG_DETAIL, &dsk_dev, "Disk Write: %d bytes\n", len);
         for (i = 0; i < len; i++)  {
             sim_debug(DEBUG_DATA, &dsk_dev, "%02x ", dsk_buf[i]);
             if ((i & 0xf) == 0x1f)
                  sim_debug(DEBUG_DATA, &dsk_dev, "\n");
         }
         sim_debug(DEBUG_DATA, &dsk_dev, "\n");
          len = sim_fwrite(&dsk_buf, 1, sizeof(dsk_buf), uptr->fileref);
          if (len > dsk_dcb[drive].count)
              len = dsk_dcb[drive].count;
          dsk_dcb[drive].count -= len;
          dsk_dcb[drive].xcount += len;
          dsk_dcb[drive].addr = (dsk_dcb[drive].addr + len) & 0xffffff;
          if (dsk_dcb[drive].count != 0) {
              if (dsk_incsect(uptr, drive)) {
                 sim_debug(DEBUG_DETAIL, &dsk_dev, "Invalid seek\n");
                 io_dcbwrite_half(dcb, offset + 0xa, dsk_dcb[drive].xcount);
                 io_dcbwrite_byte(dcb, offset + 0x2, 0xb);
                 dsk_unit[0].STATUS = 0x400001 | (drive << 16);
                 ext_irq = 1;
                 break;
              }
              sim_activate(uptr, 100);
              return SCPE_OK;
          }
          io_dcbwrite_addr(dcb, offset + 0x5, dsk_dcb[drive].addr);
          io_dcbwrite_half(dcb, offset + 0x8, dsk_dcb[drive].count);
          io_dcbwrite_byte(dcb, offset + 0xd, (dsk_dcb[drive].hd << 4) |
                                               ((dsk_dcb[drive].cyl >> 8) & 0xf));
          io_dcbwrite_byte(dcb, offset + 0xe, dsk_dcb[drive].cyl & 0xff);
          io_dcbwrite_byte(dcb, offset + 0xf, dsk_dcb[drive].sect);
          io_dcbwrite_half(dcb, offset + 0xa, dsk_dcb[drive].xcount);
          io_dcbwrite_byte(dcb, offset + 0x2, 0);
          dsk_unit[0].STATUS = 0x400001 | (drive << 16);
          ext_irq = 1;
          break;

     case DSK_VFY:                 /* Verify data */
          io_dcbwrite_half(dcb, offset + 0xa, dsk_dcb[drive].xcount);
          io_dcbwrite_byte(dcb, offset + 0x2, 0);
          dsk_unit[0].STATUS = 0x400001 | (drive << 16);
          ext_irq = 1;
          break;

     case DSK_FMT:                 /* Format a track */
              sim_debug(DEBUG_DETAIL, &dsk_dev,
                   "Format %6x %4x %2x %2x %2x\n",
                    dsk_dcb[drive].addr, dsk_dcb[drive].count, dsk_dcb[drive].hd,
                   dsk_dcb[drive].cyl, dsk_dcb[drive].sect);
           da = ((dsk_dcb[drive].cyl * 7 + dsk_dcb[drive].hd) * 18);
           (void)sim_fseek(uptr->fileref, da * SECT_SZ, SEEK_SET);
          memset(&dsk_buf[0], 0, sizeof(dsk_buf));
          for(sc = 0; sc < 18; sc ++) {
              len = sim_fwrite(&dsk_buf, 1, sizeof(dsk_buf), uptr->fileref);
          }
          io_dcbwrite_half(dcb, offset + 0xa, dsk_dcb[drive].count);
          io_dcbwrite_byte(dcb, offset + 0x2, 0);
          dsk_unit[0].STATUS = 0x400001 | (drive << 16);
          ext_irq = 1;
          break;
     case DSK_SEEK:                /* Seek to cylinder */
          io_dcbwrite_byte(dcb, offset + 0x2, 0);
          dsk_unit[0].STATUS = 0x400001 | (drive << 16);
          ext_irq = 1;
          break;
     case DSK_RDH:                 /* Read Highest sector address */
          io_dcbwrite_byte(dcb, offset + 0xd, ((dsk_type[type].hds - 1) << 4) |
                                      (((dsk_type[type].cyl - 1) >> 8) & 0xF));
          io_dcbwrite_byte(dcb, offset + 0xe, (dsk_type[type].cyl - 1) & 0xFF);
          io_dcbwrite_byte(dcb, offset + 0xf, dsk_type[type].sect - 1);
          io_dcbwrite_half(dcb, offset + 0xa, dsk_type[type].bpt / dsk_type[type].sect);
          io_dcbwrite_byte(dcb, offset + 0x2, 0x00);
          dsk_unit[0].STATUS = 0x400001 | (drive << 16);
          ext_irq = 1;
          break;
     case DSK_RDF:                 /* Read full sector */
          sim_debug(DEBUG_DETAIL, &dsk_dev,
               "read fsector %6x %4x %4d %d %2d\n",
                dsk_dcb[drive].addr, dsk_dcb[drive].count, dsk_dcb[drive].cyl,
                 dsk_dcb[drive].hd, dsk_dcb[drive].sect);
          (void)sim_fseek(uptr->fileref, da * SECT_SZ, SEEK_SET);
          memset(&dsk_buf[0], 0, LBL_SZ);
          io_write_blk(dsk_dcb[drive].addr, &dsk_buf[0], LBL_SZ);
          dsk_dcb[drive].addr += LBL_SZ;
          len = sim_fread(&dsk_buf, 1, sizeof(dsk_buf), uptr->fileref);
          while (len < sizeof(dsk_buf)) {
              dsk_buf[len++] = 0;
          }
         sim_debug(DEBUG_DATA, &dsk_dev, "Disk Readfull\n");
         for (i = 0; i < SECT_SZ; i++)  {
             sim_debug(DEBUG_DATA, &dsk_dev, "%02x ", dsk_buf[i]);
             if ((i & 0xf) == 0xf)
                  sim_debug(DEBUG_DATA, &dsk_dev, "\n");
         }
         sim_debug(DEBUG_DATA, &dsk_dev, "\n");
          io_write_blk(dsk_dcb[drive].addr, &dsk_buf[0], len);
          dsk_dcb[drive].xcount += sizeof(dsk_sect_lab) + sizeof(dsk_buf) + 4;
          io_dcbwrite_half(dcb, offset + 0xa, dsk_dcb[drive].xcount);
          io_dcbwrite_byte(dcb, offset + 0x2, 0);
          dsk_unit[0].STATUS = 0x400001 | (drive << 16);
          ext_irq = 1;
          break;

     case DSK_WRF:                 /* Write full sector */
          sim_debug(DEBUG_DETAIL, &dsk_dev,
               "Write fsector %6x %4x %4d %d %2d\n",
                dsk_dcb[drive].addr, dsk_dcb[drive].count, dsk_dcb[drive].cyl,
                 dsk_dcb[drive].hd, dsk_dcb[drive].sect);
          (void)sim_fseek(uptr->fileref, da * SECT_SZ, SEEK_SET);
         io_read_blk(dsk_dcb[drive].addr, &dsk_buf[0], sizeof(dsk_sect_lab));
          dsk_dcb[drive].addr += LBL_SZ;
          io_read_blk(dsk_dcb[drive].addr, &dsk_buf[0], sizeof(dsk_buf));
         sim_debug(DEBUG_DATA, &dsk_dev, "Disk Writefull\n");
         for (i = 0; i < SECT_SZ; i++)  {
             sim_debug(DEBUG_DATA, &dsk_dev, "%02x ", dsk_buf[i]);
             if ((i & 0xf) == 0xf)
                  sim_debug(DEBUG_DATA, &dsk_dev, "\n");
         }
         sim_debug(DEBUG_DATA, &dsk_dev, "\n");
          len = sim_fwrite(&dsk_buf, 1, sizeof(dsk_buf), uptr->fileref);
          dsk_dcb[drive].xcount += len;
          io_dcbwrite_half(dcb, offset + 0xa, dsk_dcb[drive].xcount);
          io_dcbwrite_byte(dcb, offset + 0x2, 0);
          dsk_unit[0].STATUS = 0x400001 | (drive << 16);
          ext_irq = 1;
          break;

     case DSK_HDR:                 /* Read headers */
          io_dcbwrite_byte(dcb, offset + 0x2, 0);
          dsk_unit[0].STATUS = 0x400001 | (drive << 16);
          ext_irq = 1;
          break;
     }
     return SCPE_OK;
}


t_stat
dsk_boot(int32 unit_num, DEVICE *dptr)
{
    UNIT    *dkuptr = &dptr->units[unit_num];
    int     i = 0;

    if (unit_num != 0)
        return SCPE_ARG;

    if ((dkuptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;
    dkuptr->up8 = (void *)dkuptr;
    /* Set up for generic read of track 2 */
    dkuptr->CMD = 0;
    dsk_dcb[unit_num].addr = 0x3e000;
    dsk_dcb[unit_num].count = 4096;
    dsk_dcb[unit_num].xcount = 0;
    dsk_dcb[unit_num].hd = 0;
    dsk_dcb[unit_num].cyl = 0;
    dsk_dcb[unit_num].sect = 4;

    /* Set CPU into idle state */
    cpu_boot(1);
    sim_activate(dkuptr, 20);
    return SCPE_OK;
}



t_stat
dsk_reset(DEVICE *dptr)
{
    int i,t;

    dsk_unit[0].DCB = 0x3c100;
    for (i = 0; i < dptr->numunits; i++) {
        t = GET_DTYPE(dsk_unit[i].flags);
        dsk_unit[i].capac = dsk_type[t].cyl * dsk_type[t].hds *
                     dsk_type[t].sect * 1024;
    }
    return SCPE_OK;
}

t_stat
dsk_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int         i;

    if (uptr == NULL) return SCPE_IERR;
    i = GET_DTYPE(val);
    uptr->capac = dsk_type[i].cyl * dsk_type[i].hds *
                     dsk_type[i].sect * 1024;
    return SCPE_OK;
}


/* Attach routine */
t_stat
dsk_attach(UNIT *uptr, CONST char *cptr)
{
    char header[4];
    t_stat r;

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if ( r != SCPE_OK)              /* error?       */
        return r;

    /* Determine length of this disk */
    uptr->capac = sim_fsize(uptr->fileref);
    return SCPE_OK;
}


/* Detach routine */
t_stat dsk_detach(UNIT *uptr)
{
    t_stat r;

    r = detach_unit(uptr);  /* detach unit */
    return r;
}



