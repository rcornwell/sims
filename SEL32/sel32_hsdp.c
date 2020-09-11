/* sel32_hsdp.c: SEL-32 8064 High Speed Disk Processor

   Copyright (c) 2018-2020, James C. Bevier
   Portions provided by Richard Cornwell and other SIMH contributers

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
   JAMES C. BEVIER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "sel32_defs.h"

#if NUM_DEVS_HSDP > 0

#define UNIT_HSDP   UNIT_ATTABLE | UNIT_IDLE | UNIT_DISABLE

/* useful conversions */
/* Fill STAR value from cyl, trk, sec data */
#define CHS2STAR(c,h,s)	        (((c<<16) & LMASK)|((h<<8) & 0xff00)|(s & 0xff))
/* convert STAR value to number of sectors */
#define STAR2SEC(star,spt,spc)  ((star&0xff)+(((star>>8)&0xff)*spt)+((star>>16)*spc))
/* convert STAR value to number of heads or tracks */
#define STAR2TRK(star,tpc)      ((star >> 16) * tpc + ((star >> 8) & 0x0ff))
/* convert STAR value to number of cylinders */
#define STAR2CYL(star)          ((star >> 16) & RMASK)
/* convert byte value to number of sectors mod sector size */
#define BYTES2SEC(bytes,ssize)  (((bytes) + (ssize-1)) >> 10)
/* get sectors per track for specified type */
#define SPT(type)               (hsdp_type[type].spt)
/* get sectors per cylinderfor specified type */
#define SPC(type)               (hsdp_type[type].spt*hsdp_type[type].nhds)
/* get number of cylinders for specified type */
#define CYL(type)               (hsdp_type[type].cyl)
/* get number of heads for specified type */
#define HDS(type)               (hsdp_type[type].nhds)
/* get disk capacity in sectors for specified type */
#define CAP(type)               (CYL(type)*HDS(type)*SPT(type))
/* get number of bytes per sector for specified type */
#define SSB(type)               (hsdp_type[type].ssiz*4)
/* get disk capacity in bytes for specified type */
#define CAPB(type)              (CAP(type)*SSB(type))
/* get disk geometry as STAR value for specified type */
#define GEOM(type)              (CHS2STAR(CYL(type),HDS(type),SPT(type)))

/* INCH command information */
/*
WD 0 - Data address
WD 1 - Flags - 0 -36 byte count

Data - 224 word INCH buffer address (SST)
WD 1 Drive 0 Attribute register
WD 2 Drive 1 Attribute register
WD 3 Drive 2 Attribute register
WD 4 Drive 3 Attribute register
WD 5 Drive 4 Attribute register
WD 6 Drive 5 Attribute register
WD 7 Drive 6 Attribute register
WD 8 Drive 7 Attribute register

Drive attribute register bit assignments (DATR)
Byte 0 bits 0-7 - Flags
        Drive type
        bits 0&1 - 00=Undefined
                 - 01=MHD
                 - 10=Undefined
                 - 11=Undefined
        Optimized seeks
        bit  2&3 - 00=Optimize seeks and post IOCL status out of order
                 - 01=Optimize seeks and post IOCL status in order
                 - 10=Do not optimize seeks
                 - 11=Do not optimize seeks
        bit  4   - 0=Drive is present
                 - 1=Drive not present
        bit  5   - 0=Not Dual Port
                 - 1=Dual Port
        Sector Size
        bit  6&7 - 00=768 bytes
                   01=1024 bytes
                   10=2048 bytes
                   11=Unassigned
Byte 1 bits 8-15 - Sectors per track
Byte 2 bits 16-23 - Number of head
Byte 3 bits 24-31 - Reserved (zero)
*/

/*
Drive status bit assignments (DSR)
Byte 0 bits 0-7
        bit 00 - Seek End
            01 - Unit selected
            02 - Sector pulse counter bit 0
            03 - Sector pulse counter bit 1
            04 - Sector pulse counter bit 2
            05 - Sector pulse counter bit 3
            06 - Sector pulse counter bit 4
            07 - Sector pulse counter bit 5
Byte 1 bits 7-15
        bit 08 - Disc drive fault
            09 - Seek error
            10 - On cylinder
            11 - Unit Ready
            12 - Write protected
            13 - Drive busy
            14 - Reserved (zero)
            15 - Reserved (zero)
*/

/* Subchannel Target Register (STAR) */
/* byte 0 - Cylinder MS byte */
/* byte 1 - Cylinder LS byte */
/* byte 2 - Track count */
/* byte 3 - Sector count */

/* Mode Register (MODE) */
/* Bits 0-7 - bit assignments */

/* Bits 0-3 are for data recovery operations which can be */
/*      tried by the software */
/*  0 - Servo offset 0/1=disable/enable */
/*  1 - Servo offset polarity 0/1=positive/negative */
/*  2 - Data strobe offset 0/1=disable/enable */
/*  3 - Data strobe offset polarity 0/1=positive/negative */
/* Bit 4 enables sector ECC data to be read or written for */
/*     diagnostic commands */
/*  4 - Read/write ECC data 0/1=disable/enable */
/* Bit 5 controls the transfer of an ID during express bus */
/*     read commands */
/*  5 - Express bus ID 0/1=enable/disable */
/* Bit 6 enables auto-retry in accordance with the firmware */
/*     auto-retry algorithms */
/*  6 - Auto retry 0/1=enable/disable */
/* Bit 7 disables the subchannel from interacting with the */
/*     disc drive and is for diagnostic testing only */
/*  7 - Diagnostic mode 0/1=disable/enable */

/* Sense Buffer Register (SBR) */
/* The SBR contains subchannel error status information */
/* Byte 0
 * bit 00 Command rejected (CR)
 *     01 Intervention requested (IR)
 *     02 Unit select error (USEL)
 *     03 Equipment check (EQCK)
 *     04 Reserved (zero)
 *     05 Reserved (zero)
 *     06 Disc format error (DFER)
 *     07 Defective track encountered (DETR)
 * Byte 1
 * bit 08 Reserved (zero)
 *     09 At alternate track (AATT)
 *     10 Write protect error (WPER)
 *     11 Write lock error (WRL)
 *     12 Mode check (MOCK)
 *     13 Invalid address (INAD)
 *     14 Release fault (RELF)
 *     15 Chaining error (CHER)
 * Byte 2
 * bit 16 Revolution lost (REVL)
 *     17 Disc addressing or seek error
 *     18 Reserved (zero)
 *     19 Reserved (zero)
 *     20 ECC error in data (ECCD)
 *     21 Reserved (zero)
 *     22 Reserved (zero)
 *     23 Uncorrectable ECC error (UECC)
 * Byte 3
 * Not used
 *     */

/* 224 word Subchannel Storage Buffer (SST) */
/* 128 words reserved */
/*  66 words (33 DW) of program status queue (PSQ) */
/*   8 words of retry counters (1/channel) */
/*  22 words reserved */

#define CMD     u3
/* u3 */
/* in u3 is device command code and status */
#define DSK_CMDMSK      0x00ff                  /* Command being run */
#define DSK_STAR        0x0100                  /* STAR value in u4 */
#define DSK_NU2         0x0200                  /*                    */
#define DSK_READDONE    0x0400                  /* Read finished, end channel */
#define DSK_ENDDSK      0x0800                  /* Sensed end of disk */
#define DSK_SEEKING     0x1000                  /* Disk is currently seeking */
#define DSK_READING     0x2000                  /* Disk is reading data */
#define DSK_WRITING     0x4000                  /* Disk is writing data */
#define DSK_BUSY        0x8000                  /* Disk is busy */
/* commands */
#define DSK_INCH        0x00                    /* Initialize channel */
#define DSK_INCH2       0xF0                    /* Fake while in srv Initialize channel */
#define DSK_WD          0x01                    /* Write data */
#define DSK_RD          0x02                    /* Read data */
#define DSK_NOP         0x03                    /* No operation */
#define DSK_SNS         0x04                    /* Sense */
#define DSK_SCK         0x07                    /* Seek cylinder, track, sector */
#define DSK_TIC         0x08                    /* Transfer in channel */
#define DSK_FMT         0x0B                    /* Format track */
#define DSK_RE          0x12                    /* Read express bus with ECC */
//#define DSK_LPL         0x13                    /* Lock protected label */
#define DSK_LMR         0x1F                    /* Load mode register */
#define DSK_RENO        0x22                    /* Read express bus with no ECC */
#define DSK_RES         0x23                    /* Reserve */
#define DSK_WSL         0x31                    /* Write sector label */
#define DSK_RSL         0x32                    /* Read sector label */
#define DSK_REL         0x33                    /* Release */
#define DSK_XEZ         0x37                    /* Rezero */
#define DSK_WTF         0x41                    /* Write track format */
#define DSK_RVL         0x42                    /* Read vendor label */
#define DSK_POR         0x43                    /* Priority Override */
#define DSK_IHA         0x47                    /* Increment head address */
//#define DSK_SRM         0x4F                    /* Set reserve track mode */
#define DSK_WTL         0x51                    /* Write track label */
#define DSK_RTL         0x52                    /* Read track label */
//#define DSK_XRM         0x5F                    /* Reset reserve track mode */
#define DSK_RAP         0xA2                    /* Read angular position */
//#define DSK_TESS        0xAB                  /* Test STAR (subchannel target address register) */
#define DSK_REC         0xB2                    /* Read ECC */
#define DSK_INC         0xFF                    /* Initialize Controller */

#define STAR    u4
/* u4 - sector target address register (STAR) */
/* Holds the current cylinder, head(track), sector */
#define DISK_CYL        0xFFFF0000              /* cylinder mask */
#define DISK_TRACK      0x0000FF00              /* track mask */
#define DISK_SECTOR     0x000000ff              /* sector mask */

#define SNS     u5
/* u5 */
/* Sense byte 0  - mode register */
#define SNS_DROFF      0x80000000               /* Drive Carriage will be offset */
#define SNS_TRKOFF     0x40000000               /* Track offset: 0=positive, 1=negative */
#define SNS_RDTMOFF    0x20000000               /* Read timing offset = 1 */
#define SNS_RDSTRBT    0x10000000               /* Read strobe timing: 1=positive, 0=negative */
#define SNS_DIAGMOD    0x08000000               /* Diagnostic Mode ECC Code generation and checking */
#define SNS_RSVTRK     0x04000000               /* Reserve Track mode: 1=OK to write, 0=read only */
#define SNS_FHDOPT     0x02000000               /* FHD or FHD option = 1 */
#define SNS_RESERV     0x01000000               /* Reserved */

/* Sense byte 1 */
#define SNS_CMDREJ     0x800000                 /* Command reject */
#define SNS_INTVENT    0x400000                 /* Unit intervention required */
#define SNS_SPARE1     0x200000                 /* Spare */
#define SNS_EQUCHK     0x100000                 /* Equipment check */
#define SNS_DATCHK     0x080000                 /* Data Check */
#define SNS_OVRRUN     0x040000                 /* Data overrun/underrun */
#define SNS_DSKFERR    0x020000                 /* Disk format error */
#define SNS_DEFTRK     0x010000                 /* Defective track encountered */

/* Sense byte 2 */
#define SNS_LAST       0x8000                   /* Last track flag encountered */
#define SNS_AATT       0x4000                   /* At Alternate track */
#define SNS_WPER       0x2000                   /* Write protection error */
#define SNS_WRL        0x1000                   /* Write lock error */
#define SNS_MOCK       0x0800                   /* Mode check */
#define SNS_INAD       0x0400                   /* Invalid memory address */
#define SNS_RELF       0x0200                   /* Release fault */
#define SNS_CHER       0x0100                   /* Chaining error */

/* Sense byte 3 */
#define SNS_REVL       0x80                     /* Revolution lost */
#define SNS_DADE       0x40                     /* Disc addressing or seek error */
#define SNS_BUCK       0x20                     /* Buffer check */
#define SNS_ECCS       0x10                     /* ECC error in sector label */
#define SNS_ECCD       0x08                     /* ECC error iin data */
#define SNS_ECCT       0x04                     /* ECC error in track label */
#define SNS_RTAE       0x02                     /* Reserve track access error */
#define SNS_UESS       0x01                     /* Uncorrectable ECC error */

#define SNS2    us9
/* us9 */
/* us9 holds bytes 4 & 5 of the status for the drive */

/* Sense byte 4 */
#define SNS_SEND        0x8000                  /* Seek End */
#define SNS_USEL        0x4000                  /* Unit Selected */
#define SNS_SPC0        0x2000                  /* Sector Pulse Count B0 */
#define SNS_SPC1        0x1000                  /* Sector Pulse Count B1 */
#define SNS_SPC2        0x0800                  /* Sector Pulse Count B2 */
#define SNS_SPC3        0x0400                  /* Sector Pulse Count B3 */
#define SNS_SPC4        0x0200                  /* Sector Pulse Count B4 */
#define SNS_SPC5        0x0100                  /* Sector Pulse Count B5 */

/* Sense byte 5 */
#define SNS_FLT         0x80                    /* Disk Drive fault */
#define SNS_SKER        0x40                    /* Seek error */
#define SNS_ONC         0x20                    /* On Cylinder */
#define SNS_UNR         0x10                    /* Unit Ready */
#define SNS_WRP         0x08                    /* Write Protected */
#define SNS_BUSY        0x04                    /* Drive is busy */
#define SNS_NU1         0x02                    /* Spare 1 */
#define SNS_NU2         0x01                    /* Spare 2 */

#define CHS     u6
/* u6 holds the current cyl, hd, sec for the drive */

/* this attribute information is provided by the INCH command */
/* for each device and is not used.  It is reconstructed from */
/* the disk_t structure data for the assigned disk */
/*
bits 0-7 - Flags
        bits 0&1 - 00=Reserved, 01=MHD, 10=FHD, 11=MHD with FHD option
        bit  2   - 1=Cartridge module drive
        bit  3   - 0=Reserved
        bit  4   - 1=Drive not present
        bit  5   - 1=Dual Port
        bit  6   - 0=Reserved  00 768 byte sec
        bit  7   - 0=Reserved  01 1024 byte sec
bits 8-15 - sector count (sectors per track)(F16=16, F20=20)
bits 16-23 - MHD Head count (number of heads on MHD)
bits 24-31 - FHD head count (number of heads on FHD or number head on FHD option of
    mini-module)
*/

/* Not Used     up7 */

/* disk definition structure */
struct hsdp_t
{
    const char  *name;                          /* Device ID Name */
    uint16      nhds;                           /* Number of heads */
    uint16      ssiz;                           /* sector size in words */
    uint16      spt;                            /* # sectors per track(head) */
    uint16      ucyl;                           /* Number of cylinders used */
    uint16      cyl;                            /* Number of cylinders on disk */
    uint8       type;                           /* Device type code */
    /* bit 1 mhd */
    /* bits 6/7 = 0 768 byte blk */             /* not used on UDP/DPII */
    /*          = 1 1024 byte blk */            /* not used on UDP/DPII */
}

hsdp_type[] =
{
    /* Class F Disc Devices */
    /* For MPX */
    {"MH040",   5, 192, 20, 407, 411, 0x40},   /* 0  411   40M XXXX */
    {"MH080",   5, 192, 20, 819, 823, 0x40},   /* 1  823   80M 8138 */
    {"MH160",  10, 192, 20, 819, 823, 0x40},   /* 2  823  160M 8148 */
    {"MH300",  19, 192, 20, 819, 823, 0x40},   /* 3  823  300M 9346 */
    {"MH600",  40, 192, 20, 839, 843, 0x40},   /* 4  843  600M 8155 */
    /* For UTX */
    {"9342",   5, 256, 16, 819, 823, 0x41},    /* 5  823   80M 9342 MH080 */
    {"8148",  10, 256, 16, 819, 823, 0x41},    /* 6  823  160M 8146 MH160 */
    {"9346",  19, 256, 16, 819, 823, 0x41},    /* 7  823  300M 9344 MH300 */
    {"8858",  24, 256, 16, 707, 711, 0x41},    /* 8  711  340M 8858 DC340 */
    {"8887",  10, 256, 35, 819, 823, 0x41},    /* 9  823  337M 8887 DP337 */
    {"8155",  40, 256, 16, 839, 843, 0x41},    /* 10 843  600M 8155 MH600 */
    {NULL, 0}
};

uint16  hsdp_preio(UNIT *uptr, uint16 chan) ;
uint16  hsdp_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) ;
uint16  hsdp_haltio(UNIT *uptr);
t_stat  hsdp_srv(UNIT *);
t_stat  hsdp_boot(int32 unitnum, DEVICE *);
void    hsdp_ini(UNIT *, t_bool);
t_stat  hsdp_reset(DEVICE *);
t_stat  hsdp_attach(UNIT *, CONST char *);
t_stat  hsdp_detach(UNIT *);
t_stat  hsdp_set_type(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat  hsdp_get_type(FILE * st, UNIT * uptr, int32 v, CONST void *desc);
t_stat  hsdp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char  *hsdp_description (DEVICE *dptr);

/* channel program information */
CHANP           dpa_chp[NUM_UNITS_HSDP] = {0};

MTAB            hsdp_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "TYPE", "TYPE",
    &hsdp_set_type, &hsdp_get_type, NULL, "Type of disk"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL, "Device channel address"},
    {0},
};

UNIT            dpa_unit[] = {
/* SET_TYPE(3) DM300 */
/* SET_TYPE(8) 8887 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(8), 0), 0, UNIT_ADDR(0x800)},  /* 0 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(8), 0), 0, UNIT_ADDR(0x802)},  /* 1 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(8), 0), 0, UNIT_ADDR(0x804)},  /* 2 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(8), 0), 0, UNIT_ADDR(0x806)},  /* 3 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(8), 0), 0, UNIT_ADDR(0x808)},  /* 4 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(8), 0), 0, UNIT_ADDR(0x80A)},  /* 5 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(8), 0), 0, UNIT_ADDR(0x80C)},  /* 6 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(8), 0), 0, UNIT_ADDR(0x80E)},  /* 7 */
};

DIB             dpa_dib = {
    hsdp_preio,     /* uint16 (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Pre Start I/O */
    hsdp_startcmd,  /* uint16 (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    hsdp_haltio,    /* uint16 (*halt_io)(UNIT *uptr) */         /* Stop I/O */
    NULL,           /* uint16 (*test_io)(UNIT *uptr) */         /* Test I/O */
    NULL,           /* uint16 (*post_io)(UNIT *uptr) */         /* Post I/O */
    hsdp_ini,       /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    dpa_unit,       /* UNIT* units */                           /* Pointer to units structure */
    dpa_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NUM_UNITS_HSDP, /* uint8 numunits */                        /* number of units defined */
    0x0F,           /* uint8 mask */                            /* 8 devices - device mask */
    0x0800,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE          dpa_dev = {
    "DPA", dpa_unit, NULL, hsdp_mod,
    NUM_UNITS_HSDP, 16, 24, 4, 16, 32,
    NULL, NULL, &hsdp_reset, &hsdp_boot, &hsdp_attach, &hsdp_detach,
    /* ctxt is the DIB pointer */
    &dpa_dib, DEV_DISABLE|DEV_DEBUG|DEV_DIS|DEV_DISK, 0, dev_debug,
    NULL, NULL, &hsdp_help, NULL, NULL, &hsdp_description
};

#if NUM_DEVS_HSDP > 1
/* channel program information */
CHANP           dpb_chp[NUM_UNITS_HSDP] = {0};

UNIT            dpb_unit[] = {
/* SET_TYPE(3) DM300 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC00)},  /* 0 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC02)},  /* 1 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC04)},  /* 2 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC06)},  /* 3 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC08)},  /* 4 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC0A)},  /* 5 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC0C)},  /* 6 */
    {UDATA(&hsdp_srv, UNIT_HSDP|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC0E)},  /* 7 */
};


DIB             dpb_dib = {
    hsdp_preio,     /* uint16 (*pre_io)(UNIT *uptr, uint16 chan)*/  /* Pre Start I/O */
    hsdp_startcmd,  /* uint16 (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start command */
    hsdp_haltio,    /* uint16 (*halt_io)(UNIT *uptr) */         /* Stop I/O */
    NULL,           /* uint16 (*test_io)(UNIT *uptr) */         /* Test I/O */
    NULL,           /* uint16 (*post_io)(UNIT *uptr) */         /* Post I/O */
    hsdp_ini,       /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    dpb_unit,       /* UNIT* units */                           /* Pointer to units structure */
    dpb_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NUM_UNITS_HSDP, /* uint8 numunits */                        /* number of units defined */
    0x0F,           /* uint8 mask */                            /* 8 devices - device mask */
    0x0C00,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE          dpb_dev = {
    "DPB", dpb_unit, NULL, hsdp_mod,
    NUM_UNITS_HSDP, 16, 24, 4, 16, 32,
    NULL, NULL, &hsdp_reset, &hsdp_boot, &hsdp_attach, &hsdp_detach,
    /* ctxt is the DIB pointer */
    &dpb_dib, DEV_DISABLE|DEV_DEBUG|DEV_DIS|DEV_DISK, 0, dev_debug,
    NULL, NULL, &hsdp_help, NULL, NULL, &hsdp_description
};
#endif

/* convert sector disk address to star values (c,h,s) */
uint32 hsdpsec2star(uint32 daddr, int type)
{
    int32 sec = daddr % hsdp_type[type].spt;    /* get sector value */
    int32 spc = hsdp_type[type].nhds * hsdp_type[type].spt; /* sec per cyl */
    int32 cyl = daddr / spc;                    /* cylinders */
    int32 hds = (daddr % spc) / hsdp_type[type].spt;    /* heads */ 

    /* now return the star value */
    return (CHS2STAR(cyl,hds,sec));             /* return STAR */
}

/* start a disk operation */
uint16 hsdp_preio(UNIT *uptr, uint16 chan)
{
    DEVICE      *dptr = get_dev(uptr);
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         unit = (uptr - dptr->units);

    sim_debug(DEBUG_DETAIL, dptr, "hsdp_preio CMD %08x unit %02x\n", uptr->CMD, unit);
    if ((uptr->CMD & 0xff00) != 0) {            /* just return if busy */
        return SNS_BSY;
    }

    sim_debug(DEBUG_DETAIL, dptr, "hsdp_preio unit %02x chsa %04x OK\n", unit, chsa);
    return 0;                                   /* good to go */
}

uint16 hsdp_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd)
{
    uint16      chsa = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int         unit = (uptr - dptr->units);
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    sim_debug(DEBUG_CMD, dptr,
        "hsdp_startcmd chsa %04x unit %02x cmd %02x CMD %08x\n",
        chsa, unit, cmd, uptr->CMD);
    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd unit %02x not attached\n", unit);
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        if (cmd != DSK_SNS)                     /* we are completed with unit check status */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    if ((uptr->CMD & DSK_CMDMSK) != 0) {
        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd unit %02x busy\n", unit);
        uptr->CMD |= DSK_BUSY;                  /* Flag we are busy */
        return SNS_BSY;
    }
    uptr->SNS2 |= SNS_USEL;                     /* unit selected */
    sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd CMD continue unit=%02x cmd %02x\n", unit, cmd);

    /* Unit is online, so process a command */
    switch (cmd) {

    case DSK_INCH:                              /* INCH 0x0 */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_startcmd starting INCH %06x cmd, chsa %04x MemBuf %08x cnt %04x\n",
            uptr->u4, chsa, chp->ccw_addr, chp->ccw_count);

        uptr->SNS &= ~SNS_CMDREJ;               /* not rejected yet */
        uptr->CMD |= DSK_INCH2;                 /* use 0xF0 for inch, just need int */
        sim_activate(uptr, 20);                 /* start things off */
        return SCPE_OK;                         /* good to go */
        break;

    case DSK_NOP:                               /* NOP 0x03 */
        if ((cmd == DSK_NOP) &&
            (chp->chan_info & INFO_SIOCD)) {    /* is NOP 1st IOCD? */
            chp->chan_caw -= 8;                 /* backup iocd address for diags */
            break;                              /* yes, can't be 1st */
        }
    case DSK_SCK:                               /* Seek command 0x07 */
    case DSK_XEZ:                               /* Rezero & Read IPL record 0x1f */
    case DSK_WD:                                /* Write command 0x01 */
    case DSK_RD:                                /* Read command 0x02 */
    case DSK_LMR:                               /* read mode register */
    case DSK_WSL:                               /* WSL 0x31 */
    case DSK_RSL:                               /* RSL 0x32 */
    case DSK_IHA:                               /* 0x47 Increment head address */
    case DSK_WTL:                               /* WTL 0x51 */
    case DSK_RTL:                               /* RTL 0x52 */
    case DSK_RAP:                               /* 0xA2 Read angular positions */
        uptr->SNS &= ~MASK24;                   /* clear data  & leave mode */
        uptr->SNS2 = (SNS_UNR|SNS_ONC|SNS_USEL);/* reset status to on cyl & ready */
    case DSK_SNS:                               /* Sense 0x04 */
        uptr->CMD |= cmd;                       /* save cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_startcmd starting disk cmd %02x chsa %04x\n",
            cmd, chsa);
        sim_activate(uptr, 20);                 /* start things off */
        return SCPE_OK;                         /* good to go */
        break;

    }

    sim_debug(DEBUG_CMD, dptr,
        "hsdp_startcmd done with hsdp_startcmd %02x chsa %04x SNS %08x\n",
        cmd, chsa, uptr->SNS);
    uptr->SNS |= SNS_CMDREJ;                    /* cmd rejected */
    return SNS_CHNEND|SNS_DEVEND|STATUS_PCHK;   /* return error */
}

/* Handle haltio transfers for disk */
uint16  hsdp_haltio(UNIT *uptr) {
    uint16      chsa = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int         cmd = uptr->CMD & DSK_CMDMSK;
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    sim_debug(DEBUG_EXP, dptr, "hsdp_haltio enter chsa %04x cmd = %02x\n", chsa, cmd);

    /* terminate any input command */
    /* UTX wants SLI bit, but no unit exception */
    /* status must not have an error bit set */
    /* otherwise, UTX will panic with "bad status" */
    if ((uptr->CMD & DSK_CMDMSK) != 0) {    /* is unit busy */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_haltio HIO chsa %04x cmd = %02x ccw_count %02x\n", chsa, cmd, chp->ccw_count);
        /* stop any I/O and post status and return error status */
//      chp->chan_byte = BUFF_EMPTY;        /* there is no data to read/store */
//      chp->ccw_count = 0;                 /* zero the count */
        chp->ccw_flags &= ~(FLAG_DC|FLAG_CC);/* stop any chaining */
        uptr->CMD &= LMASK;                 /* make non-busy */
        uptr->SNS2 |= (SNS_ONC|SNS_UNR);    /* on cylinder & ready */
        sim_cancel(uptr);                   /* clear the input timer */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_haltio HIO I/O stop chsa %04x cmd = %02x\n", chsa, cmd);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* force end */
        return SCPE_IOERR;
    }
    uptr->CMD &= LMASK;                     /* make non-busy */
    uptr->SNS2 |= (SNS_ONC|SNS_UNR);        /* on cylinder & ready */
    sim_debug(DEBUG_CMD, dptr,
        "hsdp_haltio HIO I/O not busy chsa %04x cmd = %02x\n", chsa, cmd);
    return SCPE_OK;                         /* not busy */
}

/* Handle processing of hsdp requests. */
t_stat hsdp_srv(UNIT *uptr)
{
    uint16          chsa = GET_UADDR(uptr->CMD);
    DEVICE          *dptr = get_dev(uptr);
    CHANP           *chp = find_chanp_ptr(chsa);    /* get channel prog pointer */
    int             cmd = uptr->CMD & DSK_CMDMSK;
    int             type = GET_TYPE(uptr->flags);
    uint32          tcyl=0, trk=0, cyl=0, sec=0;
    int             unit = (uptr - dptr->units);
    int             len = chp->ccw_count;
    int             i,j;
    uint32          mema;                           /* memory address */
    uint8           ch;
    uint16          ssize = hsdp_type[type].ssiz * 4;   /* disk sector size in bytes */
    uint32          tstart;
    uint8           buf2[1024];
    uint8           buf[1024];

    sim_debug(DEBUG_CMD, dptr,
        "hsdp_srv entry unit %02x CMD %08x chsa %04x count %04x %x/%x/%x \n",
        unit, uptr->CMD, chsa, chp->ccw_count,
        STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));

    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        if (cmd != DSK_SNS) {                   /* we are completed with unit check status */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            return SCPE_OK;
        }
    }

    sim_debug(DEBUG_CMD, dptr,
        "hsdp_srv cmd=%02x chsa %04x count %04x\n", cmd, chsa, chp->ccw_count);

    switch (cmd) {
    case 0:                                     /* No command, stop disk */
        break;

    case DSK_INCH2:                             /* use 0xF0 for inch, just need int */
        len = chp->ccw_count;                   /* INCH command count */
        mema = chp->ccw_addr;                   /* get inch or buffer addr */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv starting INCH cmd, chsa %04x MemBuf %06x cnt %04x\n",
            chsa, chp->ccw_addr, chp->ccw_count);

        /* mema has IOCD word 1 contents.  For the disk processor it contains */
        /* a pointer to the INCH buffer followed by 8 drive attribute words that */
        /* contains the flags, sector count, MHD head count, and FHD count */
        /* len has the byte count from IOCD wd2 and should be 0x24 (36) */
        /* the INCH buffer address must be set for the parent channel as well */
        /* as all other devices on the channel.  Call set_inch() to do this for us */
        /* just return OK and channel software will use u4 as status buffer addr */

        if (len != 36) {
            /* we have invalid count, error, bail out */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ;
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }

        /* read all 36 bytes, stopping every 4 bytes to make words */
        /* the first word has the inch buffer address */
        /* the next 8 words have drive data for each unit */
        /* WARNING 8 drives must be defined for this controller */
        /* so we will not have a map fault */
        for (i=0; i < 36; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                    uptr->SNS |= SNS_INAD;      /* invalid address */
                /* we have error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->SNS |= SNS_CMDREJ;
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (((i+1)%4) == 0) {               /* see if we have a word yet */
                if (i == 3)
                    /* inch buffer address */
                    mema = (buf[0]<<24) | (buf[1]<<16) |
                        (buf[2]<<8) | (buf[3]);
                else
                    /* drive attribute registers */
                    /* may want to use this later */
                    /* clear warning errors */
                    tstart = (buf[i-3]<<24) | (buf[i-2]<<16)
                        | (buf[i-1]<<8) | (buf[i]);
            }
        }
        /* now call set_inch() function to write and test inch bybber addresses */
        i = set_inch(uptr, mema);               /* new address */
        if ((i == SCPE_MEM) || (i == SCPE_ARG)) {   /* any error */
            /* we have error, bail out */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ;
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
        uptr->CMD &= LMASK;                     /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv cmd INCH chsa %04x chsa %06x count %04x completed\n",
            chsa, mema, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_WSL:                               /* WSL 0x31 make into NOP */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv cmd WSL chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);

        sim_debug(DEBUG_DETAIL, dptr, "Sector 1 label");
        /* now read sector label data */
        len = chp->ccw_count;
        for (i = 0; i < len; i++) {
//      for (i = 0; i < 30; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                    uptr->SNS |= SNS_INAD;      /* invalid address */
                /* we have write error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }
            if ((i%16) == 0)
                sim_debug(DEBUG_DETAIL, dptr, "\nSector 1 label");
            sim_debug(DEBUG_DETAIL, dptr, " %02x", buf[i]);
        }
        sim_debug(DEBUG_DETAIL, dptr, "\n");
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_WTL:                               /* WTL 0x51 make into NOP */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv cmd WTL chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);

        sim_debug(DEBUG_DETAIL, dptr, "Track %08x label", uptr->CHS);
        /* now read track label data */
        for (i = 0; i < 30; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                    uptr->SNS |= SNS_INAD;      /* invalid address */
                /* we have read error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
                break;
            }
            if (i == 16)
                sim_debug(DEBUG_DETAIL, dptr, "\nTrack %08x label", uptr->CHS);
            sim_debug(DEBUG_DETAIL, dptr, " %02x", buf[i]);
        }
        sim_debug(DEBUG_DETAIL, dptr, "\n");
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_NOP:                               /* NOP 0x03 */
        /* diags want chan prog check and cmd reject if 1st cmd of IOCL */
        uptr->CMD &= LMASK;                     /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv cmd NOP chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_RAP:                               /* 0xA2 Read angular positions */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        /* get STAR (target sector) data in STAR */
        cyl = STAR2CYL(uptr->CHS);              /* get current cyl */
        trk = (uptr->CHS >> 8) & 0xff;          /* get trk/head */
        sec = uptr->CHS & 0xff;                 /* set sec */

        ch = ((sec * 2) % SPT(type)) & 0x3f;    /* get index cnt */
        uptr->SNS2 = (uptr->SNS2 & 0xc0ff) | ((((uint32)ch) & 0x3f) << 8);
        sim_debug(DEBUG_DETAIL, dptr,
           "hsdp_srv RAP %02x cyl %04x trk %02x sec %02x\n",
           ch, cyl&0xffff, trk, sec);

        if (chan_write_byte(chsa, &ch)) {       /* put a byte to memory */
            sim_debug(DEBUG_DATA, dptr,
                "HSDP RAP %02x for addr /%04x/%02x/%02x\n",
                ch, ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
            if (chp->chan_status & STATUS_PCHK) { /* test for memory error */
                uptr->SNS |= SNS_INAD;          /* invalid address */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
            } else
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
            break;
//          return SCPE_OK;
        }
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_IHA:                               /* 0x47 Increment head address */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        /* get STAR (target sector) data in STAR */
        cyl = STAR2CYL(uptr->CHS);              /* get current cyl */
        trk = (uptr->CHS >> 8) & 0xff;          /* get trk/head */
        sec = 0;                                /* set sec to zero for this head */

        sim_debug(DEBUG_DETAIL, dptr,
           "hsdp_srv IHA cyl %04x trk %02x sec %02x unit=%02x\n",
           cyl&0xffff, trk, sec, unit);

        /* Check if head increment valid */
        trk += 1;                               /* increment the head # */
        if (trk >= hsdp_type[type].nhds) {      /* see if too big */
            trk = 0;                            /* back to trk 0 */
            cyl += 1;                           /* next cylinder */
            if (cyl >= hsdp_type[type].cyl) {   /* see if too big */
                /* set new STAR value using new values */
                uptr->CHS = CHS2STAR(cyl, trk, sec);
                sim_debug(DEBUG_EXP, dptr,
                    "hsdp_srv IHA ERROR cyl %04x trk %02x sec %02x unit=%02x\n",
                    cyl, trk, sec, unit);
                uptr->SNS |= SNS_DADE;          /* set error status */
                uptr->SNS2 |= (SNS_SKER|SNS_SEND);
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);  /* error */
                break;
//              return SCPE_OK;
            }
        }

        /* set new STAR value using new values */
        uptr->CHS = CHS2STAR(cyl, trk, sec);

        /* file offset in bytes */
        tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type)) * SSB(type);

        /* just seek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
            uptr->SNS |= SNS_DADE;              /* set error status */
            uptr->SNS2 |= (SNS_SKER|SNS_SEND);
            sim_debug(DEBUG_EXP, dptr, "disk_srv IHA error on seek to %04x\n", tstart);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
//          return SCPE_OK;
        }
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_SNS: /* 0x4 */
        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd CMD sense\n");

        /* count must be 12 or 14, if not prog check */
        if (len != 12 && len != 14) {
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv Sense bad count unit=%02x count%04x\n", unit, len);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK|STATUS_LENGTH);
            break;
        }
        /* bytes 0,1 - Cyl entry from CHS reg */
        ch = (uptr->CHS >> 24) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv sense CHS b0 unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->CHS >> 16) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv sense CHS b1 unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* byte 2 - Track entry from CHS reg */
        ch = (uptr->CHS >> 8) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv sense CHS b2 unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* byte 3 - Sector entry from CHS reg */
        ch = (uptr->CHS) & 0xff;
        sec = ch;                           /* save sec num for later */
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv sense CHS b3 unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        /* bytes 4 - mode reg, byte 0 of SNS */
        ch = (uptr->SNS >> 24) & 0xff;      /* return the sense data */
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv sense unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* bytes 5-7 - status bytes, bytes 1-3 of SNS */
        ch = (uptr->SNS >> 16) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv sense unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->SNS >> 8) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv sense unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->SNS) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv sense unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        /* bytes 8-11 - drive mode register entries from assigned hsdp */
        ch = hsdp_type[type].type & 0xff;       /* type byte */
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv datr unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = hsdp_type[type].spt & 0xff;        /* get sectors per track */
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv datr unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = hsdp_type[type].nhds & 0xff;       /* get # MHD heads */
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv datr unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = 0;                                 /* no FHD heads */
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv datr unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        /* bytes 12 & 13 are optional, so check if read done */
        /* TODO add drive status bits here */
        if ((test_write_byte_end(chsa)) == 0) {
            /* bytes 12 & 13 contain drive related status */
//          ch = 0xc0;                          /* seek end and unit selected for now */
            uptr->SNS2 |= (SNS_SEND|SNS_USEL);  /* selected & seek end */
            /* bits 2-7 have sector pulse count */
            ch = ((sec * 2) % SPT(type)) & 0x3f;/* get index cnt */
            uptr->SNS2 = (uptr->SNS2 & 0xc0ff) | ((((uint32)ch) & 0x3f) << 8);
            ch = (uptr->SNS2 >> 8) & 0xff;      /* seek end and unit selected for now */
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv dsr unit=%02x 1 %02x\n",
                unit, ch);
            chan_write_byte(chsa, &ch);

//          ch = 0x30;                          /* drive on cylinder and ready for now */
            uptr->SNS2 &= uptr->SNS2;           /* clean out old status */
            uptr->SNS2 |= (SNS_ONC|SNS_UNR);    /* on cylinder & ready */
            ch = uptr->SNS2 & 0xff;             /* drive on cylinder and ready for now */
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv dsr unit=%02x 2 %02x\n",
                unit, ch);
            chan_write_byte(chsa, &ch);
        }
        uptr->SNS &= 0xff000000;                /* reset status */
        uptr->SNS2 = (SNS_UNR|SNS_ONC);         /* reset status to on cyl & ready */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_SCK:                               /* Seek cylinder, track, sector 0x07 */
        /* If we are waiting on seek to finish, check if there yet. */
        if (uptr->CMD & DSK_SEEKING) {
            /* see if on cylinder yet */
            if (STAR2CYL(uptr->STAR) == STAR2CYL(uptr->CHS)) {
                /* we are on cylinder, seek is done */
                sim_debug(DEBUG_CMD, dptr,
                    "hsdp_srv seek on cylinder unit=%02x %04x %04x\n",
                    unit, uptr->STAR >> 16, uptr->CHS >> 16);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->SNS2 |= (SNS_SEND|SNS_ONC);   /* On cylinder & seek done */
                /* we have already seeked to the required sector */
                /* we do not need to seek again, so move on */
                chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
//              return SCPE_OK;
                break;
            } else {
                /* we have wasted enough time, we there */
                /* we are on cylinder, seek is done */
                sim_debug(DEBUG_CMD, dptr, "hsdp_srv seek over on cylinder unit=%02x %04x %04x\n",
                    unit, uptr->STAR >> 16, uptr->CHS >> 16);
                uptr->CHS = uptr->STAR;         /* we are there */
                sim_activate(uptr, 15);
                break;
            }
        }

        /* not seeking, so start a new seek */
        /* set buf data to current STAR values */
        tcyl = cyl = STAR2CYL(uptr->CHS);       /* get current cyl */
        buf[0] = (cyl >> 8) & 0xff;             /* split cylinder */
        buf[1] = cyl & 0xff;
        buf[2] = (uptr->CHS >> 8) & 0xff;       /* get trk/head */
        buf[3] = uptr->CHS & 0xff;              /* get sec */

        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv current STAR unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        if (len > 4) {
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv SEEK bad count unit=%02x count%04x\n", unit, len);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK|STATUS_LENGTH);
            break;
        }

        /* Read in 1-4 character seek code */
        for (i = 0; i < 4; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                    uptr->SNS |= SNS_INAD;      /* invalid address */
                if (i == 0) {
                    sim_debug(DEBUG_DETAIL, dptr,
                        "hsdp_srv seek error unit=%02x star %02x %02x %02x %02x\n",
                        unit, buf[0], buf[1], buf[2], buf[3]);
                    /* we have error, bail out */
                    uptr->CMD &= LMASK;         /* remove old status bits & cmd */
                    uptr->SNS |= SNS_DADE;      /* Disc addressing or seek error */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                    chp->ccw_count = len;       /* restore count, huh? */
                    return SCPE_OK;
                    break;
                }
                /* just read the next byte */
                /* done reading, see how many we read */
                if (i == 1) {
                    /* UTX wants to set seek STAR to zero */
                    buf[0] = buf[1] = buf[2] = buf[3] = 0;
                    break;
                }
            }
        }
        chp->ccw_count = len;                   /* restore count for diag, huh? */
        /* else the cyl, trk, and sect are ready to update */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv STAR unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);
//rezero:
        sim_debug(DEBUG_DETAIL, dptr,
            "hsdp_srv seek unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        /* save STAR (target sector) data in STAR */
        uptr->STAR = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3]);
        cyl = STAR2CYL(uptr->STAR);             /* get the cylinder */
        trk = buf[2];                           /* get the track */

        sim_debug(DEBUG_DETAIL, dptr,
           "hsdp_srv NEW SEEK cyl %04x trk %02x sec %02x unit=%02x\n",
           cyl&0xffff, trk, buf[3], unit);

        /* Check if seek valid */
        if (cyl >= hsdp_type[type].cyl ||
            trk >= hsdp_type[type].nhds ||
            buf[3] >= hsdp_type[type].spt)  {

            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv seek ERROR cyl %04x trk %02x sec %02x unit=%02x\n",
                cyl, trk, buf[3], unit);

            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_DADE;              /* set error status */
            uptr->SNS2 |= (SNS_SKER|SNS_SEND);

            /* set new STAR value */
            uptr->CHS = CHS2STAR(cyl, trk, buf[3]);

            /* we have an error, tell user */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);  /* end command */
            break;
        }

        /* calc the new sector address of data */
        /* calculate file position in bytes of requested sector */
        /* file offset in bytes */
        tstart = STAR2SEC(uptr->STAR, SPT(type), SPC(type)) * SSB(type);
        /* set new STAR value using old cyl value */
        uptr->CHS = CHS2STAR(STAR2CYL(uptr->CHS), trk, buf[3]);

        sim_debug(DEBUG_DETAIL, dptr,
            "hsdp_srv seek start %04x cyl %04x trk %02x sec %02x\n",
            tstart, cyl, trk, buf[3]);

        /* just seek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* seek home */
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv Error on seek to %08x\n", tstart);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
//          return SCPE_OK;
        }

        /* Check if already on correct cylinder */
        /* if not, do a delay to slow things down */
        if (STAR2CYL(uptr->STAR) != STAR2CYL(uptr->CHS)) {
            int diff = ((int)tcyl - (int)cyl);
            if (diff < 0)
                diff = -diff;
            /* Do a fake seek to kill time */
            uptr->CMD |= DSK_SEEKING;           /* show we are seeking */
            sim_debug(DEBUG_DETAIL, dptr,
                "hsdp_srv seek unit=%02x cyl %04x trk %02x sec %02x\n",
                unit, cyl, trk, buf[3]);
            sim_activate(uptr, 20);             /* start us off */
//          sim_activate(uptr, 20+diff);        /* start us off */
        } else {
            /* we are on cylinder/track/sector, so go on */
            sim_debug(DEBUG_DETAIL, dptr,
                "hsdp_srv done seeking to %04x cyl %04x trk %02x sec %02x\n",
                tstart, cyl, trk, buf[3]);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
        }
        break;
//      return SCPE_OK;

    case DSK_XEZ:   /* 0x37 */                  /* Rezero & Read IPL record */

        sim_debug(DEBUG_CMD, dptr, "RD REZERO IPL unit=%02x seek 0\n", unit);
        /* Do a seek to 0 */
        uptr->STAR = 0;                         /* set STAR to 0, 0, 0 */
        uptr->CHS = 0;                          /* set current CHS to 0, 0, 0 */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        uptr->CMD |= DSK_SCK;                   /* show as seek command */
        tstart = 0;                             /* byte offset is 0 */

        /* just seek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
            sim_debug(DEBUG_EXP, dptr, "hsdp_srv Error on seek to %04x\n", tstart);
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
//          return SCPE_OK;
        }
        /* we are on cylinder/track/sector zero, so go on */
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv done seek trk 0\n");
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
//      return SCPE_OK;
        break;

    case DSK_LMR:   /* 0x1F */
        sim_debug(DEBUG_CMD, dptr, "Load Mode Reg unit=%02x\n", unit);
        /* Read in 1 character of mode data */
        if (chan_read_byte(chsa, &buf[0])) {
            if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                uptr->SNS |= SNS_INAD;          /* invalid address */
            /* we have error, bail out */
            uptr->CMD &= LMASK;                 /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ;
            if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
            else
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
            break;
        }
        sim_debug(DEBUG_CMD, dptr, "Load Mode Reg unit=%02x old %x new %x\n",
            unit, (uptr->SNS)&0xff, buf[0]);
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        uptr->SNS &= 0x00ffffff;                /* clear old mode data */
        uptr->SNS |= (buf[0] << 24);            /* save mode value */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_RD:                                /* Read Data */
        if ((uptr->CMD & DSK_READING) == 0) {   /* see if we are reading data */
            uptr->CMD |= DSK_READING;           /* read from disk starting */
            sim_debug(DEBUG_CMD, dptr,
                "HSDP READ starting CMD %08x chsa %04x buffer %06x count %04x\n",
                uptr->CMD, chsa, chp->ccw_addr, chp->ccw_count);
        }

        if (uptr->CMD & DSK_READING) {          /* see if we are reading data */
            /* get sector offset */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));
            /* convert sector number back to chs value to sync disk for diags */
            uptr->CHS = hsdpsec2star(tstart, type);
            /* file offset in bytes */
            tstart = tstart * SSB(type);

            /* just seek to the location where we will r/w data */
            if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
                sim_debug(DEBUG_EXP, dptr, "hsdp_srv READ, Error on seek to %04x\n", tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
//              return SCPE_OK;
            }

            sim_debug(DEBUG_CMD, dptr,
                "HSDP READ reading CMD %08x chsa %04x tstart %04x buffer %06x count %04x\n",
                uptr->CMD, chsa, tstart, chp->ccw_addr, chp->ccw_count);

            /* read in a sector of data from disk */
            if ((len=sim_fread(buf, 1, ssize, uptr->fileref)) != ssize) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error %08x on read %04x of diskfile cyl %04x hds %02x sec %02x\n",
                    len, ssize, ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv after READ chsa %04x buffer %06x count %04x\n",
                chsa, chp->ccw_addr, chp->ccw_count);

            uptr->CHS++;                        /* next sector number */
            /* process the next sector of data */
            for (i=0; i<len; i++) {
                ch = buf[i];                    /* get a char from buffer */
                if (chan_write_byte(chsa, &ch)) {   /* put a byte to memory */
                    if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                        uptr->SNS |= SNS_INAD;  /* invalid address */
                    sim_debug(DEBUG_DATA, dptr,
                        "HSDP Read %04x bytes leaving %04x from diskfile /%04x/%02x/%02x\n",
                        i, chp->ccw_count, ((uptr->CHS)>>16)&0xffff,
                        ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
                    uptr->CMD &= LMASK;         /* remove old status bits & cmd */
                    if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                    else
                        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                    return SCPE_OK;
                }
            }

            sim_debug(DEBUG_CMD, dptr,
                "HSDP READ %04x bytes leaving %4x to be read to %06x from diskfile /%04x/%02x/%02x\n",
                ssize, chp->ccw_count, chp->ccw_addr+4,
                ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);

            /* get sector offset */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));

            /* see if we are done reading data */
            if (test_write_byte_end(chsa)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_DATA, dptr,
                    "HSDP Read complete for read from disk @ /%04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->CHS = 0;                  /* reset cylinder position */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            /* get sector offset */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));

            /* see of over end of disk */
            if (tstart >= (uint32)CAP(type)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_CMD, dptr,
                    "HSDP Read reached EOM for read from disk @ /%04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->CHS = 0;                  /* reset cylinder position */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            /* see if we are done reading data */
            if (test_write_byte_end(chsa)) {
                sim_debug(DEBUG_DATA, dptr,
                    "HSDP Read complete for read from diskfile /%04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;               /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                break;
            }

            sim_debug(DEBUG_DATA, dptr,
                "HSDP sector read complete, %x bytes to go from diskfile /%04x/%02x/%02x\n",
                chp->ccw_count, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
            sim_activate(uptr, 10);             /* wait to read next sector */
            break;
        }
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        break;

    case DSK_WD:            /* Write Data */
        if ((uptr->CMD & DSK_WRITING) == 0) {   /* see if we are writing data */
            uptr->CMD |= DSK_WRITING;           /* write to disk starting */
            sim_debug(DEBUG_CMD, dptr,
                "HSDP WRITE starting unit=%02x CMD %08x write %04x from %06x to %03x/%02x/%02x\n",
                unit, uptr->CMD, chp->ccw_count, chp->ccw_addr,
                ((uptr->CHS)>>16)&0xffff, ((uptr->CHS)>>8)&0xff, (uptr->CHS)&0xff);
        }
        if (uptr->CMD & DSK_WRITING) {          /* see if we are writing data */
            /* get sector offset in sectors */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));
            /* file offset in bytes */
            tstart = tstart * SSB(type);

            /* just seek to the location where we will r/w data */
            if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
                sim_debug(DEBUG_EXP, dptr, "hsdp_srv WRITE, Error on seek to %04x\n", tstart);
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                return SCPE_OK;
            }

            /* process the next sector of data */
            tcyl = 0;                           /* used here as a flag for short read */
            for (i=0; i<ssize; i++) {
                if (chan_read_byte(chsa, &ch)) {    /* get a byte from memory */
                    if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                        uptr->SNS |= SNS_INAD;  /* invalid address */
                    /* if error on reading 1st byte, we are done writing */
                    if ((i == 0) || (chp->chan_status & STATUS_PCHK)) {
                        uptr->CMD &= LMASK;     /* remove old status bits & cmd */
                        sim_debug(DEBUG_CMD, dptr,
                            "HSDP Wrote %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                            ssize, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
                        if (chp->chan_status & STATUS_PCHK) /* test for memory error */
                            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|STATUS_PCHK);
                        else
                            chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                        return SCPE_OK;
                    }
                    ch = 0;                     /* finish out the sector with zero */
                    tcyl++;                     /* show we have no more data to write */
                }
                buf2[i] = ch;                   /* save the char */
            }

            /* get file offset in sectors */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));
            /* convert sector number back to chs value to sync disk for diags */
            uptr->CHS = hsdpsec2star(tstart, type);

            /* write the sector to disk */
            if ((i=sim_fwrite(buf2, 1, ssize, uptr->fileref)) != ssize) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error %08x on write %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                    i, ssize, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            uptr->CHS++;                        /* next sector number */
            if (tcyl != 0) {                    /* see if done with write command */
                sim_debug(DEBUG_DATA, dptr,
                    "HSDP WroteB %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                    ssize, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                break;
            }
            sim_debug(DEBUG_CMD, dptr,
                "HSDP WR to sec end %04x bytes end %04x to diskfile cyl %04x hds %02x sec %02x\n",
                len, ssize, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));

            /* get sector offset */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));

            /* see if over end of disk */
            if (tstart >= (uint32)CAP(type)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_CMD, dptr,
                    "HSDP Write reached EOM for write to disk @ /%04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                uptr->CHS = 0;                  /* reset cylinder position */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            sim_activate(uptr, 10);             /* keep writing */
            break;
         }
         uptr->CMD &= LMASK;                    /* remove old status bits & cmd */
         break;

    case DSK_RSL:                               /* RSL 0x32 */
        /* Read sector label zero to get disk geometry */
        /* write 30 bytes, b0-b1=cyl, b1=trk, b2=sec */
        /* zero the Track Label Buffer */
        for (i = 0; i < 30; i++)
            buf[i] = 0;

        len = chp->ccw_count;                   /* get number of sectors per track */

        /* read a 30 byte track label for each sector on track */
        /* for 16 sectors per track, that is 480 bytes */
        /* for 20 sectors per track, that is 600 bytes */
        for (j=0; j<SPT(type); j++) {

            /* get file offset in sectors */
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));
            /* convert sector number back to chs value to sync disk for diags */
            uptr->CHS = hsdpsec2star(tstart, type);

            /* the dmap pointer is placed by the vendor or diag into the */
            /* track zero label in word 3 of the 30 byte label. */
            /* The disk address is the last track of the user area.  The vendor */
            /* reserves the last cylinder, SEL diags reserve the next two, so the */
            /* addr is CYL-4/HDS-1/0 and is VDT.  The UTX/MPX media table is on */
            /* previous track, so MDT = VDT-SPT is CYL-4/HDS-2/0 */
            /* The UTX flaw map is at DMAP = MDT-SPT CYL-4/HDS-3/0 */
            /* UTX media map is 1 track lower at UMAP=DMAP-SPT CYL-4/HDS-4/0 */
            /* The UTX med map is pointed to by sector label 1 */
            /* simulate pointers here, set wd[3] in label to VDT */

            /* get physical sector address of media defect table */
            /* VDT  286965 (819/9/0) 0x460f5 for 8887 - 823/10/35 */ 
            /* DMAP 286930 (819/8/0) 0x460d2 for 8887 - 823/10/35 Trk 0 ptr */
            /* UMAP 286895 (819/7/0) 0x460af for 8887 - 823/10/35 */ 

            /* get logical sector address of media defect table */
            /* VDT  278766 (819/9/0) 0x440ee for 8887 - 823/10/34 */ 
            /* DMAP 278732 (819/8/0) 0x440cc for 8887 - 823/10/34 */ 
            /* UMAP 278698 (819/7/0) 0x440aa for 8887 - 823/10/34 Sec 0 ptr */ 

            sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd RSL STAR %08x disk geom %08x\n",
                uptr->CHS, GEOM(type));

            /* set buf data to current STAR values */
            cyl = STAR2CYL(uptr->CHS);          /* get current cyl */
            buf[0] = (cyl >> 8) & 0xff;         /* split cylinder */
            buf[1] = cyl & 0xff;
            buf[2] = (uptr->CHS >> 8) & 0xff;   /* get trk/head */
            buf[3] = uptr->CHS & 0xff;          /* get sec */
            buf[4] = 0x80;                      /* set lflg1 to show good sector */

            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv RSL unit=%0x2x star %02x %02x %02x %02x\n",
                unit, buf[0], buf[1], buf[2], buf[3]);

            /* get physical sector address of UMAP */
            /* UMAP 278698 (819/7/0) 0x440aa for 8887 - 823/10/34 Sec 0 ptr */ 
            tstart = ((CYL(type)-4) * SPC(type)) +
                ((HDS(type)-3) * (SPT(type)));

            /* get physical sector address of UMAP */
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv SL1 RSL sector %d %x star %02x %02x %02x %02x\n",
                tstart, tstart, buf[0], buf[1], buf[2], buf[3]);

            /* on HSDP DMAP is in wd 3 on label 0 */
            /* on HSDP UMAP is in wd 4 on label 1 */
            /* UMAP 278698 (819/7/0) 0x440aa for 8887 - 823/10/34 Sec 0 ptr */ 
            tstart = (tstart * (SPT(type)-1))/SPT(type);  /* make logical */

            /* the address must be logical for HSDP */
            /* store into sec 1 label */
            buf[12] = (tstart >> 24) & 0xff;    /* UMAP pointer */
            buf[13] = (tstart >> 16) & 0xff;
            buf[14] = (tstart >> 8) & 0xff;
            buf[15] = (tstart) & 0xff;

            /* diags want these values to be zero for all others*/
            if (uptr->CHS == 1) {               /* test for sec 1, trk 0, cyl 0 */
                buf[16] = (tstart >> 24) & 0xff;    /* UMAP pointer */
                buf[17] = (tstart >> 16) & 0xff;
                buf[18] = (tstart >> 8) & 0xff;
                buf[19] = (tstart) & 0xff;
            } else {
                buf[16] = 0;                    /* UMAP pointer */
                buf[17] = 0;
                buf[18] = 0;
                buf[19] = 0;
            }

            /* the tech doc shows the cyl/trk/sec data is in the first 4 bytes */
            /* of the track label, BUT it is really in the configuration data */
            /* area are too.  That is where UTX looks.  Byte 27 is sectors/track */
            /* and byte 28 is number of heads. Byte 25 is copy of byte 27. */
            buf[25] = hsdp_type[type].spt & 0xff;   /* sect per track 35 */

            /* UTX looks at bit 6 & 7 to determine if UDP or HSDP controller */
            /* The UDP/DPII controllers do not use these bits, so UTX keys */
            /* on these bits to determine type of controller.  Bit 31 is set */
            /* for a HSDP and not set for the UDP/DPII. MPX has this bit cleared. */
            buf[26] = hsdp_type[type].type | 1;     /* mode data is 0x41 */
            buf[27] = hsdp_type[type].spt & 0xff;   /* sec per track 35 */
            buf[28] = hsdp_type[type].nhds & 0xff;  /* num heads 10 */

            sim_debug(DEBUG_DETAIL, dptr, "Sector 1 label");
            /* now write sector label data */
            for (i = 0; i < 30; i++) {
                if (chan_write_byte(chsa, &buf[i])) {
                    /* we have write error, bail out */
                    uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                    break;
                }
                if (i == 16)
                    sim_debug(DEBUG_DETAIL, dptr, "\nSector 1 label");
                sim_debug(DEBUG_DETAIL, dptr, " %02x", buf[i]);
            }
            sim_debug(DEBUG_DETAIL, dptr, "\n");

            /* leave STAR "unnormalized" for diags */
            uptr->CHS++;                        /* bump to next track */
            len -= 30;                          /* count 1 sector label size */
            if (len > 0)
                continue;
            break;                              /* done */
        }

        /* command done */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv cmd RSL done chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_RTL:                               /* RTL 0x52 */
        /* Read track zero to get disk geometry */
        /* write 30 bytes, b0-b1=cyl, b1=trk, b2=sec */

        /* zero the Track Label Buffer */
        for (i = 0; i < 30; i++)
            buf[i] = 0;

        /* get file offset in sectors */
        tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));
        /* convert sector number back to chs value to sync disk for diags */
        uptr->CHS = hsdpsec2star(tstart, type);

        /* set buf data to current CHS values */
        if (uptr->CHS == 0) {                   /* write last address on trk 0 */
            cyl = CYL(type)-1;                  /* lcyl  cyl upper 8 bits */
            trk = HDS(type)-1;                  /* ltkn  trk */
            sec = SPT(type)-1;                  /* lid   sector ID */
        } else {
            /* write current address on other tracks */
            cyl = (uptr->CHS >> 16) & 0xffff;   /* get the cylinder */
            trk = (uptr->CHS >> 8) & 0xff;      /* get the track */
            sec = (uptr->CHS) & 0xff;           /* get the sector */
        }

        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd RTL STAR %08x disk geom %08x\n",
            uptr->CHS, GEOM(type));

        /* set buf data to current STAR values */
        buf[0] = (cyl >> 8) & 0xff;             /* lcyl  cyl upper 8 bits */
        buf[1] = cyl & 0xff;                    /* lcyl  cyl lower 8 bits */
        buf[2] = trk & 0xff;                    /* ltkn  trk */
        buf[3] = sec & 0xff;                    /* lid   sector ID */
        buf[4] = 0x80;                          /* set lflg1 */

        sim_debug(DEBUG_DETAIL, dptr,
            "hsdp_srv RTL unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        /* the dmap pointer is placed by the vendor or diag into the */
        /* track zero label in word 3 of the 30 byte label. */
        /* The disk address is the last track of the user area.  The vendor */
        /* reserves the last cylinder, SEL diags reserve the next two, so the */
        /* addr is CYL-4/HDS-1/0 and is VDT.  The UTX/MPX media table is on */
        /* previous track, so MDT = VDT-SPT is CYL-4/HDS-2/0 */
        /* The UTX flaw map is at FMAP = MDT-SPT CYL-4/HDS-3/0 */
        /* UTX media map is 1 track lower at UTXMM=FMAP-SPT CYL-4/HDS-4/0 */
        /* The UTX media map is pointed to by sector label 1 */
        /* simulate pointers here, set wd[3] in label to VDT */

        /* get physical sector address of media defect table */
        /* VDT  286965 (819/9/0) 0x460f5 for 8887 - 823/10/35 */ 
//      /* MDT  286930 (819/8/0) 0x460d2 for 8887 - 823/10/35 Trk 0 ptr */
        /* DMAP 286930 (819/8/0) 0x460d2 for 8887 - 823/10/35 Trk 0 ptr */
//      /* DMAP 286895 (819/7/0) 0x460af for 8887 - 823/10/35 */ 
        /* UMAP 286895 (819/7/0) 0x460af for 8887 - 823/10/35 */ 
//      /* UMAP 286860 (819/6/0) 0x4608c for 8887 - 823/10/35 */ 
        /* get logical sector address of media defect table */
        /* VDT  278766 (819/9/0) 0x440ee for 8887 - 823/10/34 */ 
//      /* MDT  278732 (819/8/0) 0x440cc for 8887 - 823/10/34 */ 
        /* DMAP 278732 (819/8/0) 0x440cc for 8887 - 823/10/34 */ 
//      /* DMAP 278698 (819/7/0) 0x440aa for 8887 - 823/10/34 */ 
        /* UMAP 278698 (819/7/0) 0x440aa for 8887 - 823/10/34 Sec 0 ptr */ 
//      /* UMAP 278664 (819/6/0) 0x44088 for 8887 - 823/10/34 Sec 0 ptr */ 

        tstart = (CYL(type)-4) * SPC(type) + (HDS(type)-2) * SPT(type);

        cyl = hsdp_type[type].cyl-1;            /* last cyl */
        trk = hsdp_type[type].nhds-1;           /* last head number */
//      sec = hsdp_type[type].spt-1;            /* last sector number */

        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv RTL STAR %04x/%02x/%02x Dmap pointer 0x%08x %d\n",
            cyl-3, trk-1, 0, tstart, tstart);
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv TRK0 RTL sector %d %x star %02x %02x %02x %02x\n",
            tstart, tstart, buf[0], buf[1], buf[2], buf[3]);

        /* write physical address of MDT on disk */
        if (uptr->CHS == 0) {                   /* only write dmap address in trk 0 */
            /* output last sector address of disk */
            buf[12] = (tstart >> 24) & 0xff;    /* ldeallp DMAP pointer */
            buf[13] = (tstart >> 16) & 0xff;
            buf[14] = (tstart >> 8) & 0xff;
            buf[15] = (tstart) & 0xff;
        }

        /* get physical sector address of umap table */
        /* UMAP 286860 (819/6/0) 0x4608c for 8887 - 823/10/35 */ 
        /* get logical sector address of umap table */
        /* UMAP 278664 (819/6/0) 0x44088 for 8887 - 823/10/34 Sec 0 ptr */ 
        tstart -= SPT(type);                    /* calc utxfmap address */

        /* the address must be logical */
        /* 286860 physical becomes 278664 logical */
        tstart = (tstart * (SPT(type) - 1))/SPT(type);  /* make logical */

        /* UMAP 278664 (819/6/0) 0x44088 for 8887 - 823/10/34 Sec 0 ptr */ 
        if (uptr->CHS == 0) {                   /* only write dmap address on trk 0 */
            buf[16] = (tstart >> 24) & 0xff;    /* ldeallp UMAP */
            buf[17] = (tstart >> 16) & 0xff;
            buf[18] = (tstart >> 8) & 0xff;
            buf[19] = (tstart) & 0xff;
        }

        /* the tech doc shows the cyl/trk/sec data is in the first 4 bytes */
        /* of the track label, BUT it is really in the configuration data */
        /* area too.  That is where UTX looks.  Byte 27 is sectors/track */
        /* and byte 28 is number of heads. Byte 25 is copy of byte 27. */
        /* these are physical values, not logical */
        buf[25] = hsdp_type[type].spt & 0xff;   /* sect per track 35 */
        buf[26] = hsdp_type[type].type | 1;     /* sense data set for 1024 byte blk */
        buf[27] = hsdp_type[type].spt & 0xff;   /* sec per track 35 */
        buf[28] = hsdp_type[type].nhds & 0xff;  /* num heads 10 */

        sim_debug(DEBUG_DETAIL, dptr, "Track 0 label");
        /* now write track label data */
        for (i = 0; i < 30; i++) {
            if (chan_write_byte(chsa, &buf[i])) {
                /* we have write error, bail out */
                uptr->CMD &= LMASK;             /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (i == 16)
                sim_debug(DEBUG_DETAIL, dptr, "\nTrack 0 label");
            sim_debug(DEBUG_DETAIL, dptr, " %02x", buf[i]);
        }
        sim_debug(DEBUG_DETAIL, dptr, "\n");

        /* leave STAR "unnormalized" for diags */
        uptr->CHS += 0x10;                      /* bump to next track */

        /* command done */
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv cmd RTL done chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    default:
        sim_debug(DEBUG_CMD, dptr, "invalid command %02x unit %02x\n", cmd, unit);
        uptr->SNS |= SNS_CMDREJ;
        uptr->CMD &= LMASK;                     /* remove old status bits & cmd */
        return SNS_CHNEND|STATUS_PCHK;
        break;
    }
    sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv done cmd=%02x chsa %04x count %04x\n",
        cmd, chsa, chp->ccw_count);
    return SCPE_OK;
}

/* initialize the disk */
void hsdp_ini(UNIT *uptr, t_bool f)
{
    DEVICE  *dptr = get_dev(uptr);
    int     i = GET_TYPE(uptr->flags);

    /* start out at sector 0 */
    uptr->CHS = 0;                              /* set CHS to cyl/hd/sec = 0 */
    uptr->STAR = 0;                             /* set STAR to cyl/hd/sec = 0 */
    uptr->CMD &= LMASK;                         /* clear out the flags but leave ch/sa */
    /* total sectors on disk */
    uptr->capac = CAP(i);                       /* size in sectors */

    sim_debug(DEBUG_EXP, &dda_dev, "DPA init device %s on unit DPA%.1x cap %x %d\n",
        dptr->name, GET_UADDR(uptr->CMD), uptr->capac, uptr->capac);
}

t_stat hsdp_reset(DEVICE *dptr)
{
    /* add reset code here */
    return SCPE_OK;
}

/* create the disk file for the specified device */
int hsdp_format(UNIT *uptr) {
    int         type = GET_TYPE(uptr->flags);
    DEVICE      *dptr = get_dev(uptr);
    uint32      ssize = hsdp_type[type].ssiz * 4;       /* disk sector size in bytes */
    uint32      tsize = hsdp_type[type].spt;            /* get track size in sectors */
    uint32      csize = hsdp_type[type].nhds * tsize;   /* get cylinder size in sectors */
    uint32      cyl = hsdp_type[type].cyl;              /* get # cylinders */
    uint32      cap = hsdp_type[type].cyl * csize;      /* disk capacity in sectors */
    uint32      cylv = cyl;                             /* number of cylinders */
    uint8       *buff;
    int         i;

                /* last sector address of disk (cyl * hds * spt) - 1 */
    uint32      laddr = CAP(type) - 1;                  /* last sector of disk */
                /* make logical */
    int32       logla = laddr*(SPC(type)-1)/(SPC(type));

                /* get sector address of vendor defect table VDT */
                /* put data = 0xf0000000 0xf4000004 */
    int32       vaddr = (CYL(type)-4) * SPC(type) + (HDS(type)-1) * SPT(type);

                /* get sector address of utx diag map (DMAP) track 0 pointer */
                /* put data = 0xf0000000 + (cyl-1), 0x8a000000 + daddr, */
                /* 0x9a000000 + (cyl-1), 0xf4000008 */
    int32       daddr = vaddr - SPT(type);
                /* make logical */
    int32       logda = daddr*(SPC(type)-1)/(SPC(type));

    int32       uaddr = daddr - SPT(type);

                /* last block available */
    int32       luaddr = (CYL(type)-4) * SPC(type);
                /* make logical */
    int32       logua = luaddr*(SPC(type)-1)/(SPC(type));

                /* get sector address of utx flaw data (1 track long) */
                /* set trace data to zero */

                /* make up a UMAP with the partition for 8887 disk */
    uint32      umap[256] =
                {
                0x4e554d50,logla,logua-1,0,0,0,0,0xe10,
                0,0x5258,0,0x4e5c,0x3e,logua,0,0xd32c,
                0x79,0x187cc,0x118,0x14410,0x23f,0,0,0,
                0,0x3821a2d6,0,0x1102000,0xf4,0,0,0,
                };

                /* NULL vendor flaw map */
    uint32      vmap[2] = {0xf0000004, 0xf4000000};

                /* diag flaw map */
    uint32      pdmap[4] = {0xf0000000 | (cap-1), 0x8a000000 | daddr,
                    0x9a000000 | (cap-1), 0xf4000008};
    uint32      dmap[4] = {0xf0000000 | logla, 0x8a000000 | logda,
                    0x9a000000 | logla, 0xf4000008};

    /* see if user wants to initialize the disk */
    if (!get_yn("Initialize disk? [Y] ", TRUE)) {
        return 1;
    }

        /* get physical sector address of media defect table */
        /* VDT  286965 (819/9/0) 0x460f5 for 8887 - 823/10/35 */ 
//      /* MDT  286930 (819/8/0) 0x460d2 for 8887 - 823/10/35 Trk 0 ptr */
        /* DMAP 286930 (819/8/0) 0x460d2 for 8887 - 823/10/35 Trk 0 ptr */
//      /* DMAP 286895 (819/7/0) 0x460af for 8887 - 823/10/35 */ 
        /* UMAP 286895 (819/7/0) 0x460af for 8887 - 823/10/35 */ 
//      /* UMAP 286860 (819/6/0) 0x4608c for 8887 - 823/10/35 */ 

        /* get logical sector address of media defect table */
        /* VDT  278766 (819/9/0) 0x440ee for 8887 - 823/10/34 */ 
//      /* MDT  278732 (819/8/0) 0x440cc for 8887 - 823/10/34 */ 
        /* DMAP 278732 (819/8/0) 0x440cc for 8887 - 823/10/34 */ 
//      /* DMAP 278698 (819/7/0) 0x440aa for 8887 - 823/10/34 */ 
        /* UMAP 278698 (819/7/0) 0x440aa for 8887 - 823/10/34 Sec 0 ptr */ 
//      /* UMAP 278664 (819/6/0) 0x44088 for 8887 - 823/10/34 Sec 0 ptr */ 

    /* seek to sector 0 */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        fprintf (stderr, "Error on seek to 0\r\n");
        return 1;
    }

    /* get buffer for track data in bytes */
    if ((buff = (uint8 *)calloc(csize*ssize, sizeof(uint8))) == 0) {
        detach_unit(uptr);
        return SCPE_ARG;
    }
    /* put dummy data in first word of disk */
    buff[0] = 'Z';
    buff[1] = 'E';
    buff[2] = 'R';
    buff[3] = 'O';
    sim_debug(DEBUG_CMD, dptr,
        "Creating disk file of trk size %04x bytes, capacity %d\n",
        tsize*ssize, cap*ssize);

    /* write zeros to each track of the disk */
    for (cyl = 0; cyl < cylv; cyl++) {
        if ((sim_fwrite(buff, 1, csize*ssize, uptr->fileref)) != csize*ssize) {
            sim_debug(DEBUG_CMD, dptr,
            "Error on write to diskfile cyl %04x\n", cyl);
            free(buff);                         /* free cylinder buffer */
            buff = NULL;
            return 1;
        }
        if (cyl == 0) {
            buff[0] = 0;
            buff[1] = 0;
            buff[2] = 0;
            buff[3] = 0;
        }
        if ((cyl % 100) == 0)
            fputc('.', stderr);
    }
    fputc('\r', stderr);
    fputc('\n', stderr);
    free(buff);                                 /* free cylinder buffer */
    buff = NULL;

    /* byte swap the buffers for dmap and umap */
    for (i=0; i<2; i++) {
        vmap[i] = (((vmap[i] & 0xff) << 24) | ((vmap[i] & 0xff00) << 8) |
            ((vmap[i] & 0xff0000) >> 8) | ((vmap[i] >> 24) & 0xff));
    }
    for (i=0; i<4; i++) {
        dmap[i] = (((dmap[i] & 0xff) << 24) | ((dmap[i] & 0xff00) << 8) |
            ((dmap[i] & 0xff0000) >> 8) | ((dmap[i] >> 24) & 0xff));
    }
    for (i=0; i<4; i++) {
        pdmap[i] = (((pdmap[i] & 0xff) << 24) | ((pdmap[i] & 0xff00) << 8) |
            ((pdmap[i] & 0xff0000) >> 8) | ((pdmap[i] >> 24) & 0xff));
    }
    for (i=0; i<256; i++) {
        umap[i] = (((umap[i] & 0xff) << 24) | ((umap[i] & 0xff00) << 8) |
            ((umap[i] & 0xff0000) >> 8) | ((umap[i] >> 24) & 0xff));
    }

    /* now seek to end of disk and write the dmap data */
    /* setup dmap pointed to by track label 0 wd[3] = (cyl-4) * spt + (spt - 1) */

    /* write dmap data to last sector on disk */
    if ((sim_fseek(uptr->fileref, laddr*ssize, SEEK_SET)) != 0) { /* seek last sector */
        sim_debug(DEBUG_CMD, dptr,
        "Error on last sector seek to sect %06x offset %06x\n",
        cap-1, (cap-1)*ssize);
        return 1;
    }
    if ((sim_fwrite((char *)&pdmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
        sim_debug(DEBUG_CMD, dptr,
        "Error writing DMAP to sect %06x offset %06x\n",
        cap-1, (cap-1)*ssize);
        return 1;
    }

    /* seek to vendor label area VMAP */
    if ((sim_fseek(uptr->fileref, vaddr*ssize, SEEK_SET)) != 0) { /* seek VMAP */
        sim_debug(DEBUG_CMD, dptr,
        "Error on vendor map seek to sect %06x offset %06x\n",
        vaddr, vaddr*ssize);
        return 1;
    }
    if ((sim_fwrite((char *)&vmap, sizeof(uint32), 2, uptr->fileref)) != 2) {
        sim_debug(DEBUG_CMD, dptr,
        "Error writing VMAP to sect %06x offset %06x\n",
        vaddr, vaddr*ssize);
        return 1;
    }

    /* write DMAP to daddr that is the address in trk 0 label */
    if ((sim_fseek(uptr->fileref, daddr*ssize, SEEK_SET)) != 0) { /* seek DMAP */
        sim_debug(DEBUG_CMD, dptr,
        "Error on diag map seek to sect %06x offset %06x\n",
        daddr, daddr*ssize);
        return 1;
    }
    if ((sim_fwrite((char *)&dmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
        sim_debug(DEBUG_CMD, dptr,
        "Error writing DMAP to sect %06x offset %06x\n",
        daddr, daddr*ssize);
        return 1;
    }

    /* write UTX umap to uaddr */
    if ((sim_fseek(uptr->fileref, uaddr*ssize, SEEK_SET)) != 0) { /* seek UMAP */
        sim_debug(DEBUG_CMD, dptr,
        "Error on umap seek to sect %06x offset %06x\n",
        uaddr, uaddr*ssize);
        return 1;
    }
    if ((sim_fwrite((char *)&umap, sizeof(uint32), 256, uptr->fileref)) != 256) {
        sim_debug(DEBUG_CMD, dptr,
        "Error writing UMAP to sect %06x offsewt %06x\n",
        uaddr, uaddr*ssize);
        return 1;
    }

    printf("writing to vmap sec %x (%d) bytes %x (%d)\n",
        vaddr, vaddr, (vaddr)*ssize, (vaddr)*ssize);
    printf("writing dmap to %x %d %x %d dmap to %x %d %x %d\n",
       cap-1, cap-1, (cap-1)*ssize, (cap-1)*ssize,
       daddr, daddr, daddr*ssize, daddr*ssize);
    printf("writing to umap sec %x (%d) bytes %x (%d)\n",
        uaddr, uaddr, (uaddr)*ssize, (uaddr)*ssize);

    /* seek home again */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        fprintf (stderr, "Error on seek to 0\r\n");
        return 1;
    }
    return SCPE_OK;
}

/* attach the selected file to the disk */
t_stat hsdp_attach(UNIT *uptr, CONST char *file)
{
    uint16          chsa = GET_UADDR(uptr->CMD);
    CHANP           *chp = find_chanp_ptr(chsa);    /* get channel prog pointer */
    int             type = GET_TYPE(uptr->flags);
    DEVICE          *dptr = get_dev(uptr);
    DIB             *dibp = 0;
    t_stat          r;
    uint32          ssize;                      /* sector size in bytes */
    uint8           buff[1024];

    /* see if valid disk entry */
    if (hsdp_type[type].name == 0) {            /* does the assigned disk have a name */
        detach_unit(uptr);                      /* no, reject */
        return SCPE_FMT;                        /* error */
    }

    /* have simulator attach the file to the unit */
    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;

    uptr->capac = CAP(type);                    /* size in sectors */
    ssize = SSB(type);                          /* disk sector size in bytes */

    sim_debug(DEBUG_CMD, dptr, "Disk %s cyl %d hds %d sec %d ssiz %d capacity %d\n",
        hsdp_type[type].name, hsdp_type[type].cyl, hsdp_type[type].nhds, 
        hsdp_type[type].spt, ssize, uptr->capac); /* hsdp capacity */

    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        detach_unit(uptr);                      /* if no space, error */
        return SCPE_UNATT;                      /* error */
    }

    /* see if there is any data on sector zero of disk, if not format it */
    if ((r = sim_fread(buff, sizeof(uint8), ssize, uptr->fileref) != ssize)) {
        sim_debug(DEBUG_CMD, dptr, "Disk format fread ret = %04x\n", r);
        goto fmt;
    }

    if ((buff[0] | buff[1] | buff[2] | buff[3]) == 0) {
        sim_debug(DEBUG_CMD, dptr,
            "Disk format buf0 %02x buf1 %02x buf2 %02x buf3 %02x\n",
            buff[0], buff[1], buff[2], buff[3]);
fmt:
        /* format the drive */
        if (hsdp_format(uptr)) {
            detach_unit(uptr);                  /* if no space, error */
            return SCPE_FMT;                    /* error */
        }
    }

    /* there is data on sector 0, so already formatted */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        detach_unit(uptr);                      /* if no space, error */
        return SCPE_FMT;                        /* error */
    }

    /* start out at sector 0 */
    uptr->CHS = 0;                              /* set CHS to cyl/hd/sec = 0 */

    sim_debug(DEBUG_CMD, dptr,
        "Attach %s cyl %d hds %d spt %d spc %d cap sec %d cap bytes %d\n",
        hsdp_type[type].name, CYL(type), HDS(type), SPT(type), SPC(type),  
        CAP(type), CAPB(type));

    sim_debug(DEBUG_CMD, dptr, "File %s attached to %s\r\n",
        file, hsdp_type[type].name);

    /* check for valid configured disk */
    /* must have valid DIB and Channel Program pointer */
    dibp = (DIB *)dptr->ctxt;                       /* get the DIB pointer */
//??if ((dib_unit[chsa] == NULL) || (dibp == NULL) || (dibp->chan_prg == NULL)) {
    if ((dib_unit[chsa] == NULL) || (dibp == NULL) || (chp == NULL)) {
        sim_debug(DEBUG_CMD, dptr,
            "ERROR===ERROR\nHSDP device %s not configured on system, aborting\n",
            dptr->name);
        printf("ERROR===ERROR\nHSDP device %s not configured on system, aborting\n",
            dptr->name);
        detach_unit(uptr);                          /* detach if error */
        return SCPE_UNATT;                          /* error */
    }
    set_devattn(chsa, SNS_DEVEND);
    return SCPE_OK;
}

/* detach a disk device */
t_stat hsdp_detach(UNIT *uptr) {
    uptr->SNS = 0;                              /* clear sense data */
    uptr->CMD &= LMASK;                         /* remove old status bits & cmd */
    return detach_unit(uptr);                   /* tell simh we are done with disk */
}

/* boot from the specified disk unit */
t_stat hsdp_boot(int32 unit_num, DEVICE * dptr) {
    UNIT    *uptr = &dptr->units[unit_num];     /* find disk unit number */

    sim_debug(DEBUG_CMD, dptr, "HSDP Boot dev/unit %x\n", GET_UADDR(uptr->CMD));

    if ((uptr->flags & UNIT_ATT) == 0) {
        sim_debug(DEBUG_EXP, dptr, "HSDP Boot attach error dev/unit %04x\n",
            GET_UADDR(uptr->CMD));
        return SCPE_UNATT;                      /* attached? */
    }
    SPAD[0xf4] = GET_UADDR(uptr->CMD);          /* put boot device chan/sa into spad */
    SPAD[0xf8] = 0xF000;                        /* show as F class device */

    /* now boot the disk */
    uptr->CMD &= LMASK;                         /* remove old status bits & cmd */
    return chan_boot(GET_UADDR(uptr->CMD), dptr); /* boot the ch/sa */
}

/* Disk option setting commands */
/* set the disk type attached to unit */
t_stat hsdp_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int     i;

    if (cptr == NULL)                           /* any disk name input? */
        return SCPE_ARG;                        /* arg error */
    if (uptr == NULL)                           /* valid unit? */
        return SCPE_IERR;                       /* no, error */
    if (uptr->flags & UNIT_ATT)                 /* is unit attached? */
        return SCPE_ALATT;                      /* no, error */

    /* now loop through the units and find named disk */
    for (i = 0; hsdp_type[i].name != 0; i++) {
        if (strcmp(hsdp_type[i].name, cptr) == 0) {
            uptr->flags &= ~UNIT_TYPE;          /* clear old type */
            uptr->flags |= SET_TYPE(i);         /* set new type */
            /* set capacity of disk in sectors */
            uptr->capac = CAP(i);
            return SCPE_OK;
        }
    }
    return SCPE_ARG;
}

t_stat hsdp_get_type(FILE *st, UNIT * uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
            return SCPE_IERR;
    fputs("TYPE=", st);
    fputs(hsdp_type[GET_TYPE(uptr->flags)].name, st);
    return SCPE_OK;
}

/* help information for disk */
t_stat hsdp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    int i;
    fprintf (st, "SEL 8064 High Speed Disk Processor\r\n");
    fprintf (st, "Use:\r\n");
    fprintf (st, "    sim> SET %sn TYPE=type\r\n", dptr->name);
    fprintf (st, "Type can be: ");
    for (i = 0; hsdp_type[i].name != 0; i++) {
        fprintf(st, "%s", hsdp_type[i].name);
        if (hsdp_type[i+1].name != 0)
        fprintf(st, ", ");
    }
    fprintf (st, ".\nEach drive has the following storage capacity:\r\n");
    for (i = 0; hsdp_type[i].name != 0; i++) {
        int32   size = CAPB(i);                     /* disk capacity in bytes */
        size /= 1024;                               /* make KB */
        size = (10 * size) / 1024;                  /* size in MB * 10 */
        fprintf(st, "      %-8s %4d.%1d MB cyl %3d hds %3d sec %3d blk %3d\r\n",
            hsdp_type[i].name, size/10, size%10, CYL(i), HDS(i), SPT(i), SSB(i));
    }
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *hsdp_description (DEVICE *dptr)
{
    return "SEL 8064 High Speed Disk Processor";
}

#endif
