/* Ridge32_flp.c: Ridge 32 765 floppy disk controller.

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


*/

#include "ridge32_defs.h"
#include "sim_imd.h"


#define DCB      u3              /* DCB pointer */
#define STATUS   u4              /* Last status */
#define CYL      u5              /* Current cylinder */
#define PHASE    u6              /* Command phase */

#define PHASE_IDLE       0     /* No commands */
#define PHASE_SEEK       1     /* Seek to desired cylinder */
#define PHASE_CMD        2     /* Decode commands */
#define PHASE_EXEC       3     /* Execute the command */
#define PHASE_RES        4     /* Place results */
#define PHASE_IRQ        5     /* Post interrupt */

#define CMD_RDTRK        0x2   /* Read track */
#define CMD_FIXDR        0x3   /* Fix drive */
#define CMD_CHKDR        0x4   /* Check drive */
#define CMD_WRSEC        0x5   /* Write sector */
#define CMD_RDSEC        0x6   /* Read sector */
#define CMD_RECAL        0x7   /* Recalibrate drive */
#define CMD_CHKIR        0x8   /* Check IRQ */
#define CMD_WRDEL        0x9   /* Write delete */
#define CMD_RDSID        0xa   /* Read sector ID */
#define CMD_RDDEL        0xc   /* Read delete */
#define CMD_FMTTK        0xd   /* Format track */
#define CMD_SEEK         0xf   /* Seek */

#define BLD_OP         0x01
#define SEEK_OP        0x02
#define READ_OP        0x04
#define WRIT_OP        0x08
#define SK_OP          0x10
#define ST_OP          0x20
#define INV_OP         0x80

/*
 *  Read Track 0FS00010/xxxxxHDD/Cyl/Head/Sec/Sectsize/tl/lgap3/dataleng.
 *  Write sec  MF000101/same.
 *  Read sec   MFS00110/same.
 *  Write del  MF001001/smae.
 *  Read Del   MF001100/same.
 *  Fmt Track  0F001101/xxxxxHDD/Sect/Track/Lgap3/fill
 *       Data per sector: Track/Head/Sect Num/Sect size.
 * Fix Drive   00000011/step,hd ult/hd lt,ndm   No status
 * Chk Drive   00000100/xxxxxHDD                ST3
 * Cali        00000111/xxxxx0DD                No status
 * Chk Irq     00001000                         ST0,Cyl
 * Rd Sec ID   0F001010/xxxxxHDD
 * Seek        00001111/xxxxxHDD/cyl
 */

/* M = Multi track operations.
   F = FM/MFM 0=single/1=double
   S = skip deleted.
   H = Head number.
   DD = drive.
   Sectsize = 128 * 2^x... 2=512.
   lgap3 = std 42, min = 32, std =27 3.5.
   tl = track length/#sectors.
 */

/* Result phase. 

    ST0
        IC 00= normal.
           01 = abnormal.
           10 = invalid
           11 = abnomral, polling.
        SE     Seek end.
        UC     Unit Check
        NR     Drive not ready
        HD     head
        US     drive number
    ST1
        EN   End Cyl
        0
        DE   Data error.
        TO   Timeout/DMA error.
        NDAT  No data.
        NW   Not writable
        NID  No address mark.
    ST2
        0
        DADM  Deleted address mark
        CRCE  Crc error.
        WCYL  Wrong cylinder
        SEQ   Seek equal
        SERR  Seek error.
        BCYL  Back cylinder.
        NDAM: No data.
    Cyl
    Hd
    S#
    SS.
 
    ST3
       ESIG   Error 
       WPDR   Write protect
       RDY    Controller ready.
       T00    Track zero.
       DSDR   Double sided.
       HD     Head select.
       DD     Drive selected.

*/

/* DCB control block.

   DCB + 0C0

   0     GORDER
                        0 - Read, FDLP Build, implied seek.
                        1 - Write, FDLP Build, implied seek.
                        2 - Read, implied seek.
                        3 - Write, implied seek.
                        4 - Read, FDLP Build.
                        5 - Write, FDLP Build.
                        6 - Read.
                        7 - Write.
                        8 - Seek.
                        9 - Recalibrate.
                        A - Drive status.

    1    SORDER         bit 0,1,2 Len 0 read/write 6 for format.
                            3,4,5 Density
                                 0 DD 512x16
                                 1 SD 128x26
                                 2 SD 256x15
                                 3 DD 256x26
                                 4 DD 1024x8
                                 5 DD 512x15
                                 6 DD 2048x4
                                 7 DD 4096x2.
                             6 - 1= write.
                             7 - 1= nodma.
 
                                 0 EOT 16   MFM 1 N = 2 GPL = 1b
                                 1 EOT 26   MFM 0 N = 0 GPL = 07
                                 2 EOT 15   MFM 0 N = 1 GPL = 0e
                                 3 EOT 26   MFM 1 N = 1 GPL = 0e
                                 4 EOT 8    MFM 1 N = 3 GPL = 35
                                 5 EOT 15   MFM 1 N = 2 GPL = 35
                                 6 EOT 4    MFM 1 N = 4 GPL = 35
                                 7 EOT 2    MFM 1 N = 6 GPL = 35

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
                      FF - Illegal parameter in DCB order.

    3 SSTAT      
                      bit 01  - unit
                           2  - head
                           3  - two sided
                           4  - track 0
                           5  - ready
                           6  - write protect.
                           7  - fault.

    4 RETRIES

     5  RIDGE ADDR 
     6
     7

     8 BYTE COUNT
     9

     A BYTE COUNT READ.
     B

     C  NEC ORDER 
     D  Head/unit 
     E  Cylinder
     F  Sector
    10 -18  CMD 00-08   Command built by controller.
    19 -1F  STATUS 00-06  Status returned by the drive.
  */

int    flp_read(uint32 dev, uint32 *data);
int    flp_write(uint32 dev, uint32 data);
int    flp_iord(uint32 *data);
void   flp_start(UNIT *uptr, int drive);
t_stat flp_svc (UNIT *uptr);
t_stat flp_boot (int32, DEVICE *);
t_stat flp_attach(UNIT *uptr, CONST char *cptr);
t_stat flp_detach(UNIT *uptr);
void   com_write_char(int line, uint8 ch);
t_stat flp_reset(DEVICE *dp);
t_stat con_svc(UNIT *uptr);

uint8 flp_buf[4096];

/* Device context block */
struct ridge_dib flp_dib = {1, 1, flp_read, flp_write, flp_iord, 0};

MTAB flp_mod[] = {
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "SLOT", "SLOT", &set_slot_num,
        &show_slot_num, NULL},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    { 0 }
    };


UNIT                flp_unit[] = {
    {UDATA(&flp_svc, UNIT_ATTABLE, 0)},
    {UDATA(&flp_svc, UNIT_ATTABLE, 0)},
    {UDATA(&con_svc, UNIT_DIS, 0)},
};


DEVICE              flp_dev = {
    "FLP", flp_unit, NULL, flp_mod,
    3, 16, 24, 1, 16, 8,
    NULL, NULL, &flp_reset, &flp_boot, &flp_attach, &flp_detach,
    &flp_dib, DEV_DEBUG, 0, dev_debug, NULL, NULL,
};





int
flp_read(uint32 dev, uint32 *data)
{
    UNIT     *uptr = &flp_unit[0];

    *data = uptr->STATUS & 0xFFFF02;
    *data |= (flp_dib.dev_num << 24) & 0xff000000;
    if (uptr->STATUS & 0x4)
        uptr->STATUS &= ~7;
    return (uptr->STATUS & 0x2) ? 1 : 0;
}

int
flp_write(uint32 dev, uint32 data)
{
    UNIT     *uptr = &flp_unit[0];
    int       cmd = (data >> 24) & 0xff;

    /* Check if command can be accepted */
    if (uptr->STATUS & 3) {
        return 1;
    }
    sim_debug(DEBUG_EXP, &flp_dev, "Start cmd %2x\n", cmd);
    if (cmd < 0x80) {
       com_write_char(0, cmd);
       uptr->STATUS = (0x80 << 16);
    } else if ((cmd & 0xc0) == 0xc0) {
       switch (cmd) {
       case 0xc0:           /* Boot floppy left */
            (void)flp_boot(0, &flp_dev);
            break;

       case 0xc1:           /* Update DCB */
            uptr->DCB = M[0x3c0fc >> 2];
            /* Fall through */

       case 0xc3:           /* Read one char port 0 no irq */
            uptr->STATUS = (0x80 << 16) | 1;
            ext_irq = 1;
            break;

       case 0xc2:           /* Read one char port 0 no irq */
            uptr->STATUS = 2;
            /* Fall through */
       case 0xff:           /* Read one char port 0 irq */
            flp_unit[2].u3 = cmd;
            break;
       } 
       return 0;
    } else if ((cmd & 0xe0) == 0x80) {
       switch (cmd & 0x1f) {
       case 0x06:           /* Start left floppy */
       case 0x07:           /* Start right floppy */
            flp_start(uptr, cmd & 1);
            sim_activate(uptr, 20);
            break;

       case 0x00:           /* Write on port 0 */
       case 0x01:           /* Write on port 1 */
       case 0x02:           /* Write on port 2 */
       case 0x03:           /* Write on port 3 */
       case 0x04:           /* Write on printer */
       case 0x05:           /* Write on plotter */
       case 0x08:           /* Read on port 0 */
       case 0x09:           /* Read on port 1 */
       case 0x0a:           /* Read on port 2 */
       case 0x0b:           /* Read on port 3 */
       case 0x0c:           /* Control on port 0 */
       case 0x0d:           /* Control on port 1 */
       case 0x0e:           /* Control on port 2 */
       case 0x0f:           /* Control on port 3 */
       case 0x18:           /* Read one char on port 0 */
       case 0x19:           /* Read one char on port 1 */
       case 0x1a:           /* Read one char on port 2 */
       case 0x1b:           /* Read one char on port 3 */
            break;
       }
    } else {
       /* undefined */
    }
    return 0;
}

int
flp_iord(uint32 *data)
{
    UNIT     *uptr = &flp_unit[0];
    *data = uptr->STATUS & 0x0FFFF02;
    *data |= ((uint32)flp_dib.dev_num) << 24;
    /* Check if irq pending */
    if (uptr->STATUS & 1) {
        uptr->STATUS &= ~1;
        return 1;
    }
    return 0;
}




 /* Write port DCB. 

    0  GORDER   
                          1  - Write block of up to 256 chars.
                          3  - Single Char (SORDER)
                          5  - assert break.

    1  SORDER             char for 3.

    2  GSTAT              not used.
    3  SSTAT              not used.
    4  RETRIES            not used.

    5  ADDRESS
    6
    7
 
    8 REQUEST BYTE COUNT
    9

    A BYTES TRANSFERED
    B

   Read port DCB. 

    0  GORDER   
                          0  - Read term by count or MAP

    1  SORDER             not used.
    2  GSTAT              not used.
    3  SSTAT              not used.
    4  RETRIES            not used.

    5  ADDRESS
    6
    7
 
    8 REQUEST BYTE COUNT
    9

    A BYTES TRANSFERED
    B

   Control port DCB. 

    0  GORDER   
                          0  - Return termtype in SORDER
                          1 - Set termtype from SORDER
                          2 - Return Baud rate in SORDER
                          3 - Set Baud rate from SORDER
                          4 - Return SIO status 
                          5 - Set SIO registers
                          6 - Abort read in progress
                          7 - abort write in progress.
                          8 - Read map into memory
                          9 - Write map from memory.

    1  SORDER             PARM1
    2  GSTAT              PARM2
    3  SSTAT              PARM3

    Termtype:
     Bit 0  - Uses XOFF.
         1  - Generate XOFF
         2  - Clear DTR.
         3  - Send IRQ when port stat changes.

    Map... bit 0 - port 0.
               1 - port 1.
               2 - port 2.
               3 - port 3.

      Initial zero except 3 and D. If char received on line has bit set, stop read.
*/

struct _dcb {
    uint8           gorder;      /* Order code */
    uint8           sorder;
    uint8           gstat;       /* Output status */
    uint8           sstat;
    uint32          addr;        /* transfer address */
    uint16          count;       /* byte count */
    uint16          xcount;      /* transfer count */
    uint8           retries;
    uint8           order;       /* NEC 765 order code */
    uint8           hd;          /* Head */
    uint8           cyl;         /* Cylinder */
    uint8           sect;        /* Sector */
    uint8           cmd[9];      /* NEC 765 command sequenece */
    uint8           stat[7];     /* Status */
    int             stat_len;    /* Size of status */
    int             sect_sz;     /* Size of sector */
} flp_dcb;

static uint8 order[] = {
      BLD_OP|SEEK_OP|READ_OP,     /* 0 - Read, FDLP Build, implied seek. */
      BLD_OP|SEEK_OP|WRIT_OP,     /* 1 - Write, FDLP Build, implied seek. */
      SEEK_OP|READ_OP,            /* 2 - Read, implied seek. */
      SEEK_OP|WRIT_OP,            /* 3 - Write, implied seek. */
      BLD_OP|READ_OP,             /* 4 - Read, FDLP Build. */
      BLD_OP|WRIT_OP,             /* 5 - Write, FDLP Build. */
      READ_OP,                    /* 6 - Read. */
      WRIT_OP,                    /* 7 - Write. */
      BLD_OP|SEEK_OP|SK_OP,       /* 8 - Seek. */
      BLD_OP|SEEK_OP|SK_OP,       /* 9 - Recalibrate. */
      BLD_OP|ST_OP,               /* A - Drive status. */
};


struct _flp_data {
      uint8       eot;
      uint8       n;
      uint8       gpl;
      uint8       dtl;
      uint8       mfm;
      int         ssz;
} flp_data[] = {
      {16, 2, 0x1b, 0xff, 1,  512},
      {26, 0, 0x07, 0x80, 0,  128},
      {15, 1, 0x0e, 0xff, 0,  256},
      {26, 1, 0x0e, 0xff, 1,  256},
      { 8, 3, 0x35, 0xff, 1, 1024},
      {15, 2, 0x35, 0xff, 1,  512},
      { 4, 4, 0x35, 0xff, 1, 2048},
      { 2, 6, 0x35, 0xff, 1, 4096},
};

     
void
flp_start(UNIT *uptr, int drive)
{
    UNIT  *fluptr;
    int    gorder;

    /* Find actual unit */
    fluptr = &flp_unit[drive & 1];
    fluptr->up8 = (void *)uptr;
    /* Read first word of DCB + 0xC0 */
    flp_dcb.gorder = io_dcbread_byte(uptr, 0xC0);
    flp_dcb.sorder = io_dcbread_byte(uptr, 0xC1);
    flp_dcb.addr = io_dcbread_addr(uptr, 0xC5);
    flp_dcb.count = io_dcbread_half(uptr, 0xC8);
    flp_dcb.xcount = 0;
    flp_dcb.order = io_dcbread_byte(uptr, 0xCC);
    flp_dcb.hd = io_dcbread_byte(uptr, 0xCD);
    flp_dcb.cyl = io_dcbread_byte(uptr, 0xCE);
    flp_dcb.sect = io_dcbread_byte(uptr, 0xCF);
    sim_debug(DEBUG_DETAIL, &flp_dev,
            "Start floppy go=%2x so=%2x a=%6x c=%4x o=%2x h=%d t=%d s=%d\n\r",
              flp_dcb.gorder, flp_dcb.sorder, flp_dcb.addr, flp_dcb.count, flp_dcb.order,
              flp_dcb.hd, flp_dcb.cyl, flp_dcb.sect);
    if (flp_dcb.gorder > 10) {
       /* Invalid */
       io_dcbwrite_byte(uptr, 0xc2, 0xff);
       uptr->STATUS = ((0x86 + drive) << 16) | (0xff << 8) | 1;
       ext_irq = 1;
       return;
    }
    gorder = order[flp_dcb.gorder];
    if ((gorder & BLD_OP) != 0) {  /* Build command */
        int     den = (flp_dcb.sorder >> 3) & 7;
        int     i = 0;
        if (gorder & SK_OP) {
            flp_dcb.cmd[i++] = CMD_SEEK;
            flp_dcb.cmd[i++] = flp_dcb.hd;
            flp_dcb.cmd[i++] = flp_dcb.cyl;
            if (flp_dcb.gorder & 1)
               flp_dcb.cyl = 0;    /* Force recalbirate */
        } else if (gorder & ST_OP) {
            flp_dcb.cmd[i++] = CMD_CHKDR;
            flp_dcb.cmd[i++] = flp_dcb.hd;
        } else {
            flp_dcb.cmd[i++] = flp_dcb.order;    /* 0 */
            flp_dcb.cmd[i++] = flp_dcb.hd;       /* 1 */
            if ((flp_dcb.cmd[0] & 0x0f) != 0xd) {
                flp_dcb.cmd[i++] = flp_dcb.cyl;    /* 2 */
                flp_dcb.cmd[i++] = (flp_dcb.hd & 0x4) != 0; /* 3 */
                flp_dcb.cmd[i++] = flp_dcb.sect;   /* 4 */
            }
            flp_dcb.cmd[i++] = flp_data[den].n; /* N */  /* 2 or 5 */
            flp_dcb.cmd[i++] = flp_data[den].eot; /* EOT */
            flp_dcb.cmd[i++] = flp_data[den].gpl; /* GPL */
            flp_dcb.cmd[i++] = flp_data[den].dtl; /* DTL */
            if (flp_data[den].mfm)
               flp_dcb.cmd[0] |= 0x40;
        }
    } else {
        io_dcbread_blk(uptr, 0xD0, &flp_dcb.cmd[0], 9);
    }
    if ((gorder & SEEK_OP) != 0) /* Seek first */
        fluptr->PHASE = PHASE_SEEK;
    else
        fluptr->PHASE = PHASE_CMD;
    flp_dcb.stat[0] = (flp_dcb.cmd[1] & 0x7);
    flp_dcb.stat_len = 0;
    if ((fluptr->flags & UNIT_ATT) == 0) {
        flp_dcb.stat[0] |= 0xc8;
        fluptr->PHASE = PHASE_RES;
    }
    flp_dcb.gstat = 0;
    sim_activate(fluptr, 200);
}

t_stat
flp_svc (UNIT *uptr)
{
     int flags;
     int len;
     int i;
     /* Read status to decide action */
     switch (uptr->PHASE) {
     case PHASE_IDLE:    /* Done with commands, just idle out */
          return SCPE_OK;
     case PHASE_SEEK:
          /* Step in/out based on current cylinder, requested cylinder */
          /* Match, PHASE = PHASE_CMD */
          if (uptr->CYL < flp_dcb.cyl) 
              uptr->CYL++;
          else if (uptr->CYL > flp_dcb.cyl)
              uptr->CYL--;
          else 
              uptr->PHASE = PHASE_CMD;
          sim_debug(DEBUG_DETAIL, &flp_dev, "Seek n=%2d c=%2d\n\r", flp_dcb.cyl, uptr->CYL);
          sim_activate(uptr, 2000);
          return SCPE_OK;

     case PHASE_CMD:
          /* Decode command */
          switch(flp_dcb.cmd[0] & 0xf) {
          case  CMD_RECAL:       /* Recalibrate drive */
                flp_dcb.cmd[2] = 0;
                /* Fall through */
          case  CMD_SEEK:        /* Seek */
                if (uptr->CYL == flp_dcb.cmd[2]) {
                     flp_dcb.stat[0] |= 0x20;
                     uptr->PHASE = PHASE_RES;

                     sim_activate(uptr, 10);
                     return SCPE_OK;
                 }
                 flp_dcb.cyl = flp_dcb.cmd[2];
                 uptr->PHASE = PHASE_SEEK;
                 sim_activate(uptr, 10);
                 return SCPE_OK;

          case  CMD_CHKIR:       /* Check IRQ */
                 flp_dcb.stat[0] = 0;
                 flp_dcb.stat_len = 2;
                 uptr->PHASE = PHASE_RES;
                 sim_activate(uptr, 10);
                 return SCPE_OK;

          case  CMD_CHKDR:       /* Check drive */
                 uptr->PHASE = PHASE_RES;
                 sim_activate(uptr, 10);
                 return SCPE_OK;

          case  CMD_RDSEC:       /* Read sector */
          case  CMD_RDTRK:       /* Read track */
          case  CMD_WRSEC:       /* Write sector */
          case  CMD_WRDEL:       /* Write delete */
          case  CMD_RDDEL:       /* Read delete */
                 /* Make sure cylinder is correct */
                 flp_dcb.stat[1] = 0;
                 flp_dcb.stat[2] = 0;
                 flp_dcb.stat[3] = flp_dcb.cmd[2];  /* C */
                 flp_dcb.stat[4] = flp_dcb.cmd[3];  /* H */
                 flp_dcb.stat[5] = flp_dcb.cmd[4];  /* R */
                 flp_dcb.stat[6] = flp_dcb.cmd[5];  /* N */
                 flp_dcb.stat_len = 7;
                 if (uptr->CYL != flp_dcb.cmd[2]) {
                     flp_dcb.stat[0] |= 0x40;
                     flp_dcb.stat[1] = 0;
                     flp_dcb.stat[2] = 0x10;
                     uptr->PHASE = PHASE_RES;
                 } else {
                     uptr->PHASE = PHASE_EXEC;
                 }
                 sim_activate(uptr, 100);
                 return SCPE_OK;
                     
          case  CMD_FIXDR:       /* Fix drive */
          case  CMD_RDSID:       /* Read sector ID */
          case  CMD_FMTTK:       /* Format track */
                 uptr->PHASE = PHASE_EXEC;
                 sim_activate(uptr, 10);
                 return SCPE_OK;
          default:               /* Invalid command */
                 return SCPE_OK;
          }
     case PHASE_EXEC:
          /* Transfer data to/from memory */
          switch(flp_dcb.cmd[0] & 0xf) {
          case  CMD_RDSEC:       /* Read sector */
                             /*   cyl head sect */
                flags = 0;
                if (sectRead((DISK_INFO *)uptr->up7, flp_dcb.stat[3], 
                              flp_dcb.stat[4], flp_dcb.stat[5], 
                    &flp_buf[0], sizeof(flp_buf), &flags, &len) != SCPE_OK) {
                    uptr->PHASE = PHASE_RES;
                    flp_dcb.stat[0] = 0x40;
                    sim_activate(uptr, 1000);
                    return SCPE_OK;
                }
    sim_debug(DEBUG_DETAIL, &flp_dev, "Read a=%6x c=%4x h=%x t=%d s=%d l=%d\n\r",
              flp_dcb.addr, flp_dcb.count, flp_dcb.stat[4], flp_dcb.stat[3], flp_dcb.stat[5], len);

         sim_debug(DEBUG_DATA, &dsk_dev, "Disk Read: %d bytes\n", len);
         for (i = 0; i < len; i++)  {
             sim_debug(DEBUG_DATA, &dsk_dev, "%02x ", flp_buf[i]);
             if ((i & 0xf) == 0xf)
                  sim_debug(DEBUG_DATA, &dsk_dev, "\n");
         }
         sim_debug(DEBUG_DATA, &dsk_dev, "\n");

                if (len > flp_dcb.count)
                    len = flp_dcb.count;
                io_write_blk(flp_dcb.addr, &flp_buf[0], len);
                flp_dcb.count -= len;
                flp_dcb.xcount += len;
                flp_dcb.addr += len;
                if (flp_dcb.stat[5] == flp_dcb.cmd[6]) {
                    flp_dcb.stat[5] = 1;
                    if(flp_dcb.stat[4]) {
                       flp_dcb.stat[3]++;
                       flp_dcb.stat[4] = 0;
                    } else {
                       flp_dcb.stat[4] = 1;
                    }
                } else {
                    flp_dcb.stat[5]++;
                }
                if (flp_dcb.count == 0) {
//                    flp_dcb.stat[0] = 0x20 | (flp_dcb.cmd[1] & 0x7);
                    uptr->PHASE = PHASE_RES;
                    sim_activate(uptr, 10);
                } else {
                    sim_activate(uptr, 100);
                }
                return SCPE_OK;

          case  CMD_WRSEC:       /* Write sector */
                flags = 0;
                len = flp_dcb.cmd[5];
                if (len == 0) {
                    len = flp_dcb.cmd[8];
                } else {
                    len *= 512;
                }
                if (len > flp_dcb.count)
                    len = flp_dcb.count;
                io_read_blk(flp_dcb.addr, &flp_buf[0], len);
         sim_debug(DEBUG_DATA, &dsk_dev, "Disk Write: %d bytes\n", len);
         for (i = 0; i < len; i++)  {
             sim_debug(DEBUG_DATA, &dsk_dev, "%02x ", flp_buf[i]);
             if ((i & 0xf) == 0xf)
                  sim_debug(DEBUG_DATA, &dsk_dev, "\n");
         }
         sim_debug(DEBUG_DATA, &dsk_dev, "\n");
                if (sectWrite((DISK_INFO *)uptr->up7, flp_dcb.stat[3], 
                              flp_dcb.stat[4], flp_dcb.stat[5], 
                    &flp_buf[0], sizeof(flp_buf), &flags, &len) != SCPE_OK) {
                    uptr->PHASE = PHASE_RES;
                    sim_activate(uptr, 1000);
                    return SCPE_OK;
                }
                flp_dcb.count -= len;
                flp_dcb.xcount += len;
                flp_dcb.addr += len;
                if (flp_dcb.stat[5] == flp_dcb.cmd[6]) {
                    flp_dcb.stat[5] = 1;
                    if(flp_dcb.stat[4]) {
                       flp_dcb.stat[3]++;
                       flp_dcb.stat[4] = 0;
                    } else {
                       flp_dcb.stat[4] = 1;
                    }
                } else {
                    flp_dcb.stat[5]++;
                }
                if (flp_dcb.count == 0) {
                    uptr->PHASE = PHASE_RES;
                    flp_dcb.stat[0] = 0x20 | (flp_dcb.cmd[1] & 0x7);
                    sim_activate(uptr, 10);
                } else {
                    sim_activate(uptr, 100);
                }
                return SCPE_OK;

          case  CMD_RDTRK:       /* Read track */
          case  CMD_WRDEL:       /* Write delete */
          case  CMD_RDDEL:       /* Read delete */
                 /* Make sure cylinder is correct */
                 flp_dcb.stat[1] = 0;
                 flp_dcb.stat[2] = 0;
                 flp_dcb.stat[3] = flp_dcb.cmd[2];  /* C */
                 flp_dcb.stat[4] = flp_dcb.cmd[3];  /* H */
                 flp_dcb.stat[5] = flp_dcb.cmd[4];  /* R */
                 flp_dcb.stat[6] = flp_dcb.cmd[5];  /* N */
                 flp_dcb.stat_len = 7;
                 if (uptr->CYL != flp_dcb.cmd[2]) {
                     flp_dcb.stat[0] |= 0x40;
                     flp_dcb.stat[1] = 0;
                     flp_dcb.stat[2] = 0x10;
                     uptr->PHASE = PHASE_RES;
                 } else {
                     uptr->PHASE = PHASE_EXEC;
                 }
                 sim_activate(uptr, 10);
                 return SCPE_OK;
                     
          case  CMD_FIXDR:       /* Fix drive */
          case  CMD_RDSID:       /* Read sector ID */
          case  CMD_FMTTK:       /* Format track */
                 uptr->PHASE = PHASE_EXEC;
                 sim_activate(uptr, 10);
                 return SCPE_OK;
          default:               /* Invalid command */
                 return SCPE_OK;
          }
     case PHASE_RES:
          /* Save results back to memory */
          io_dcbwrite_blk(uptr, 0xD9, &flp_dcb.stat[0], flp_dcb.stat_len);
          io_dcbwrite_byte(uptr, 0xc2, flp_dcb.gstat);
          flags = (flp_dcb.hd & 7);
          flags |= 0x28; /* Ready & Two sided */
          if (uptr->CYL == 0)
             flags |= 0x10;
          io_dcbwrite_byte(uptr, 0xc3, flags);
          io_dcbwrite_half(uptr, 0xca, flp_dcb.xcount);
          io_dcbwrite_byte(uptr, 0xCE, flp_dcb.stat[3]);
          io_dcbwrite_byte(uptr, 0xCD, (flp_dcb.stat[4] << 2) | 0);
          io_dcbwrite_byte(uptr, 0xCF, flp_dcb.stat[5]);
          io_dcbwrite_addr(uptr, 0xC5, flp_dcb.addr);
          io_dcbwrite_half(uptr, 0xC8, flp_dcb.count);
          sim_debug(DEBUG_DETAIL, &flp_dev,"Stop floppy %2x %4x %2x\n\r", 
                  flags, flp_dcb.xcount, flp_dcb.gstat);
          uptr->PHASE = PHASE_IRQ;
          /* Fall Through */

     case PHASE_IRQ:
          if (((UNIT *)(uptr->up8))->STATUS & 1) {
                 sim_activate(uptr, 100);
                 return SCPE_OK;
          }
          ((UNIT *)(uptr->up8))->STATUS = ((0x86 + (flp_dcb.hd & 1)) << 16) |
                   (flp_dcb.gstat << 8) | 1;
          uptr->PHASE = PHASE_IDLE;
          ext_irq = 1;
     }
     return SCPE_OK;
}

void
com_write_char(int line, uint8 ch)
{
    sim_putchar(ch);
}

t_stat
con_svc(UNIT *uptr)
{
    t_stat     r;
    uint8      ch;

    sim_clock_coschedule (uptr, 1000);
    r = sim_poll_kbd ();
    if (r & SCPE_KFLAG) { 
        ch = r & 0377;
        if (uptr->u3 != 0) {
            if (uptr->u3 == 0xFF) {
                flp_unit[0].STATUS = (0x88 << 16) | (ch << 8) | 1;
                ext_irq = 1;
            } else {
                flp_unit[0].STATUS = ((0x80 | ch) << 16) | 6;
            }
            sim_debug(DEBUG_CMD, &flp_dev, "Read cmd %2x %2x\n", uptr->u3, ch);
            uptr->u3 = 0;
        } else {
            if (ch == '\003')
                cpu_unit.flags ^= (0x1 << (UNIT_V_UF + 4));
        }
        return SCPE_OK;
    }
    return r;
}


/* 
 *    00-01 Invalid
 *    02 Read Diag
 *    03 specify
 *    04 sense drive status
 *    05 Read
 *    06 Write
 *    x7 Recalibrate
 *    08 sense irq
 *    09 write deleted
 *    0a read id
 *    0b Invalid
 *    0c read delated
 *    0d format
 *    0e Invalid
 *    0f seek
 *    10 version
 *    11 scan equal
 *    12-6 Invalid
 *    17 recalibrate
 *    18 invalid
 *    19 scan low or equal
 *    1a-c invalid
 *    1d scan high or equal
 *    1e invalid
 *    1f recalibrate
 */

t_stat
flp_boot(int32 unit_num, DEVICE *dptr)
{
    UNIT    *fluptr = &dptr->units[unit_num];
    int     i = 0;

    if (unit_num != 0)
        return SCPE_ARG;

    if ((fluptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;
    fluptr->up8 = (void *)fluptr;
    /* Set up for generic read of track 2 */
    flp_dcb.gorder = 0;
    flp_dcb.sorder = 0;
    flp_dcb.addr = 0x3e000;
    flp_dcb.count = 8192;
    flp_dcb.xcount = 0;
    flp_dcb.order = 0x46;
    flp_dcb.hd = 0;
    flp_dcb.cyl = 2;
    flp_dcb.sect = 1;
    sim_debug(DEBUG_DETAIL, dptr,
           "Boot floppy go=%2x so=%2x a=%6x c=%4x o=%2x h=%2x t=%2x s=%2x\n",
            flp_dcb.gorder, flp_dcb.sorder, flp_dcb.addr, flp_dcb.count, flp_dcb.order, flp_dcb.hd,
            flp_dcb.cyl, flp_dcb.sect);
    flp_dcb.cmd[i++] = flp_dcb.order;
    flp_dcb.cmd[i++] = flp_dcb.hd;
    flp_dcb.cmd[i++] = flp_dcb.cyl;
    flp_dcb.cmd[i++] = 0;
    flp_dcb.cmd[i++] = flp_dcb.sect;
    flp_dcb.cmd[i++] = flp_data[0].n; /* N */
    flp_dcb.cmd[i++] = flp_data[0].eot; /* EOT */
    flp_dcb.cmd[i++] = flp_data[0].gpl; /* GPL */
    flp_dcb.cmd[i++] = flp_data[0].dtl; /* DTL */
    fluptr->PHASE = PHASE_SEEK;
    flp_dcb.stat[0] = (flp_dcb.cmd[1] & 0x7);
    flp_dcb.stat_len = 0;

    /* Set CPU into idle state */
    cpu_boot(0);
    sim_activate(fluptr, 20);
    return SCPE_OK;
}



t_stat
flp_reset(DEVICE *dp)
{
    flp_unit[0].DCB = 0x3c000;
    flp_unit[0].STATUS = 0;
    flp_unit[0].PHASE = PHASE_IDLE;
    flp_unit[1].PHASE = PHASE_IDLE;
    flp_unit[2].u3 = 0;
    sim_clock_coschedule (&flp_unit[2], 1000);
    return SCPE_OK;
}

/* Attach routine */
t_stat
flp_attach(UNIT *uptr, CONST char *cptr)
{
    char header[4];
    t_stat r;

    r = attach_unit(uptr, cptr);    /* attach unit  */
    if ( r != SCPE_OK)              /* error?       */
        return r;

    /* Determine length of this disk */
    uptr->capac = sim_fsize(uptr->fileref);

    if(uptr->capac > 0) {
        char *rtn = fgets(header, 4, uptr->fileref);
        if((rtn != NULL) && strncmp(header, "IMD", 3)) {
            sim_printf("Only IMD disk images are supported\n");
            return SCPE_OPENERR;
        }
    } else {
        /* create a disk image file in IMD format. */
        if (diskCreate(uptr->fileref, "Ridge32") != SCPE_OK) {
            sim_printf("Failed to create IMD disk.\n");
            return SCPE_OPENERR;
        }
        uptr->capac = sim_fsize(uptr->fileref);
    }

    uptr->up7 = (void *)diskOpen(uptr->fileref, TRUE);
    uptr->CYL = 0;
    return SCPE_OK;
}


/* Detach routine */
t_stat flp_detach(UNIT *uptr)
{
    t_stat r;
    int8 i;

    r = diskClose((DISK_INFO **)&uptr->up7);
    if (r != SCPE_OK)
        return r;

    uptr->up7 = NULL;
    r = detach_unit(uptr);  /* detach unit */
    if (r != SCPE_OK)
        return r;

    return SCPE_OK;
}



