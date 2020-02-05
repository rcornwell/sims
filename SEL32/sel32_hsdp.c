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

extern  t_stat  set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern  t_stat  show_dev_addr(FILE *st, UNIT *uptr, int32 v, CONST void *desc);
extern  void    chan_end(uint16 chan, uint8 flags);
extern  int     chan_read_byte(uint16 chsa, uint8 *data);
extern  int     chan_write_byte(uint16 chsa, uint8 *data);
extern  void    set_devattn(uint16 addr, uint8 flags);
extern  t_stat  chan_boot(uint16 addr, DEVICE *dptr);
extern  int     test_write_byte_end(uint16 chsa);
extern  DEVICE *get_dev(UNIT *uptr);

extern  uint32  M[];                            /* our memory */
extern  uint32  SPAD[];                         /* cpu SPAD memory */

#ifdef NUM_DEVS_HSDP

#define UNIT_HSDP   UNIT_ATTABLE | UNIT_IDLE | UNIT_DISABLE

/* useful conversions */
/* Fill STAR value from cyl, trk, sec data */
#define CHS2STAR(c,h,s)	        (((c<<16) & 0xffff0000)|((h<<8) & 0xff00)|(s & 0xff))
/* convert STAR value to number of sectors */
#define STAR2SEC(star,spt,spc)  ((star&0xff)+(((star>>8)&0xff)*spt)+((star>>16)*spc))
/* convert STAR value to number of heads or tracks */
#define STAR2TRK(star,tpc)      ((star >> 16) * tpc + ((star >> 8) & 0x0ff))
/* convert STAR value to number of cylinders */
#define STAR2CYL(star)          ((star >> 16) & 0xffff)
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
#define DSK_FINC        0xC0                    /* Fake while in srv Initialize channel */
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

#define ATTR    u6
/* u6 holds drive attribute entry */
/* provided by inch command for controller */
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

#define DDATA   up7
/* Pointer held in up7 */
/* sects/cylinder = sects/track * numhds */
/* allocated during attach command for each unit defined */
struct ddata_t
{
    uint16      cyl;                            /* Cylinder head at */
    uint16      tpos;                           /* Track position */
    uint16      spos;                           /* Sector position */
};

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
//  uint32      geom;                           /* disk star geometry cyl(16) hsd(8) sec(8) */
}

hsdp_type[] =
{
    /* Class F Disc Devices */
    /* For MPX */
    {"MH040",   5, 192, 20, 400, 411, 0x40},   /* 0  411   40M XXXX */
    {"MH080",   5, 192, 20, 800, 823, 0x40},   /* 1  823   80M 8138 */
    {"MH160",  10, 192, 20, 800, 823, 0x40},   /* 2  823  160M 8148 */
    {"MH300",  19, 192, 20, 800, 823, 0x40},   /* 3  823  300M 9346 */
    {"MH600",  40, 192, 20, 839, 843, 0x40},   /* 4  843  600M 8155 */
    /* For UTX */
    {"9342",   5, 256, 16, 819, 823, 0x41},    /* 5  823   80M 9342 MH080 */
    {"8146",  10, 256, 16, 819, 823, 0x41},    /* 6  823  160M 8146 MH160 */
    {"9344",  19, 256, 16, 800, 823, 0x41},    /* 7  823  300M 9344 MH300 */
    {"8887",  10, 256, 35, 819, 823, 0x41},    /* 8  823  337M 8887 DP337 */
    {"8858",  24, 256, 16, 704, 711, 0x41},    /* 9  711  340M 8858 DC340 */
    {"8155",  40, 256, 16, 839, 843, 0x41},    /* 10 843  600M 8155 MH600 */
    {NULL, 0}
};

uint8   hsdp_preio(UNIT *uptr, uint16 chan) ;
uint8   hsdp_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) ;
uint8   hsdp_haltio(uint16 addr);
t_stat  hsdp_srv(UNIT *);
t_stat  hsdp_boot(int32, DEVICE *);
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
    hsdp_preio,                                 /* Pre start I/O */
    hsdp_startcmd,                              /* Start a command */
    NULL,                                       /* Stop I/O */
    NULL,                                       /* Test I/O */
    NULL,                                       /* Post I/O */
    hsdp_ini,                                   /* init function */
    dpa_unit,                                   /* Pointer to units structure */
    dpa_chp,                                    /* Pointer to chan_prg structure */
    NUM_UNITS_HSDP,                             /* number of units defined */
    0x0E,                                       /* 8 devices - device mask */
    0x0800,                                     /* parent channel address */
    0,                                          /* fifo input index */
    0,                                          /* fifo output index */
    {0},                                        /* interrupt status fifo for channel */
};

DEVICE          dpa_dev = {
    "DPA", dpa_unit, NULL, hsdp_mod,
    NUM_UNITS_HSDP, 16, 24, 4, 16, 32,
    NULL, NULL, &hsdp_reset, &hsdp_boot, &hsdp_attach, &hsdp_detach,
    &dpa_dib, DEV_DISABLE|DEV_DEBUG|DEV_DIS, 0, dev_debug,
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
    hsdp_preio,                                 /* Pre Start I/O */
    hsdp_startcmd,                              /* Start a command SIO */
    NULL,                                       /* Stop I/O HIO */
    NULL,                                       /* Test I/O TIO */
    NULL,                                       /* Post I/O */
    hsdp_ini,                                   /* init function */
    dpb_unit,                                   /* Pointer to units structure */
    dpb_chp,                                    /* Pointer to chan_prg structure */
    NUM_UNITS_HSDP,                             /* number of units defined */
    0x0E,                                       /* 8 devices - device mask */
    0x0C00,                                     /* parent channel address */
    0,                                          /* fifo input index */
    0,                                          /* fifo output index */
    {0},                                        /* interrupt status fifo for channel */
};

DEVICE          dpb_dev = {
    "DPB", dpb_unit, NULL, hsdp_mod,
    NUM_UNITS_HSDP, 16, 24, 4, 16, 32,
    NULL, NULL, &hsdp_reset, &hsdp_boot, &hsdp_attach, &hsdp_detach,
    &dpb_dib, DEV_DISABLE|DEV_DEBUG|DEV_DIS, 0, dev_debug,
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
uint8  hsdp_preio(UNIT *uptr, uint16 chan)
{
    DEVICE      *dptr = get_dev(uptr);
    uint16      addr = GET_UADDR(uptr->CMD);
    int         unit = (uptr - dptr->units);

    sim_debug(DEBUG_CMD, dptr, "hsdp_preio CMD %08x unit %02x\n", uptr->CMD, unit);
    if ((uptr->CMD & 0xff00) != 0) {            /* just return if busy */
        return SNS_BSY;
    }

    sim_debug(DEBUG_CMD, dptr, "hsdp_preio unit %02x chsa %04x OK\n", unit, addr);
    return 0;                                   /* good to go */
}

uint8  hsdp_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd)
{
    uint16      addr = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int         unit = (uptr - dptr->units);
    uint8       ch;

    sim_debug(DEBUG_CMD, dptr,
        "hsdp_startcmd addr %04x unit %02x cmd %02x CMD %08x\n",
        addr, unit, cmd, uptr->CMD);
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
    if ((uptr->CMD & 0xff00) != 0) {            /* if any status info, we are busy */
        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd unit %02x busy2\n", unit);
        return SNS_BSY;
    }
    sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd CMD continue unit=%02x cmd %02x\n", unit, cmd);

    if ((uptr->flags & UNIT_ATT) == 0) {        /* see if unit is attached */
        if (cmd == DSK_SNS) {                   /* not attached, is cmd Sense 0x04 */
            sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd CMD sense\n");
            /* bytes 0,1 - Cyl entry from STAR reg in STAR */
            ch = (uptr->STAR >> 24) & 0xff;
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_startcmd sense STAR b0 unit=%02x 1 %02x\n",
                unit, ch);
            chan_write_byte(addr, &ch) ;
            ch = (uptr->STAR >> 16) & 0xff;
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_startcmd sense STAR b1 unit=%02x 1 %02x\n",
                unit, ch);
            chan_write_byte(addr, &ch) ;
            /* byte 2 - Track entry from STAR reg in STAR */
            ch = (uptr->STAR >> 8) & 0xff;
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_startcmd sense STAR b2 unit=%02x 1 %02x\n",
                unit, ch);
            chan_write_byte(addr, &ch) ;
            /* byte 3 - Sector entry from STAR reg in STAR */
            ch = (uptr->STAR) & 0xff;
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_startcmd sense STAR b3 unit=%02x 1 %02x\n",
                unit, ch);
            chan_write_byte(addr, &ch) ;
            /* bytes 4 - mode reg, byte 0 of SNS */
            ch = (uptr->SNS >> 24) & 0xff;      /* return the sense data for device */
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_startcmd sense unit=%02x 1 %02x\n",
                unit, ch);
            chan_write_byte(addr, &ch) ;
            /* bytes 5-7 - status bytes, bytes 1-3 of SNS */
            ch = (uptr->SNS >> 16) & 0xff;
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_startcmd sense unit=%02x %02x\n",
                unit, ch);
            chan_write_byte(addr, &ch) ;
            ch = (uptr->SNS >> 8) & 0xff;
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_startcmd sense unit=%02x 3 %02x\n",
                unit, ch);
            chan_write_byte(addr, &ch) ;
            ch = (uptr->SNS) & 0xff;
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_startcmd sense unit=%02x 4 %02x\n",
                unit, ch);
            chan_write_byte(addr, &ch) ;
            /* bytes 8-11 - drive attribute register (DATR) entries from uptr->ATTR
             * via INCH cmd */
            ch = (uptr->ATTR >> 24) & 0xff;
            chan_write_byte(addr, &ch) ;
            ch = (uptr->ATTR >> 16) & 0xff;
            chan_write_byte(addr, &ch) ;
            ch = (uptr->ATTR >> 8 ) & 0xff;
            chan_write_byte(addr, &ch) ;
            ch = (uptr->ATTR >> 0) & 0xff;
            chan_write_byte(addr, &ch) ;
            /* bytes 12 & 13 contain drive related status */
            ch = 0;                             /* zero for now */
            /* TODO set drive status bits here */
            chan_write_byte(addr, &ch) ;
            chan_write_byte(addr, &ch) ;

            uptr->SNS &= 0xff000000;            /* clear status bytes, but leave mode data */
            return SNS_CHNEND|SNS_DEVEND;
        }
        if (cmd == 0x0)                         /* INCH cmd gives unit check here */
           return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;

        uptr->SNS |= (SNS_INTVENT|SNS_CMDREJ);  /* set new error status */
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;   /* we done */
    }

    /* Unit is online, so process a command */
    switch (cmd) {

    case DSK_INCH:              /* INCH 0x0 */
    {
        uint32  mema;                           /* memory address */
        uint32  i;
        UNIT    *up = dptr->units;              /* first unit for this device */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_startcmd starting inch cmd addr %04x MemBuf %08x\n",
            addr, uptr->u4);
        /* STAR (u4) has IOCD word 1 contents.  For the disk processor it contains */
        /* a pointer to the INCH buffer followed by 8 drive attribute words that */
        /* contains the flags, sector count, MHD head count, and FHD count */
        /* us9 has the byte count from IOCD wd2 and should be 0x24 (36) */
        /* the INCH buffer address must be returned in u4 and us9 left non-zero */
        /* just return OK and channel software will use up8 as status buffer */
        mema = (uint32)uptr->u4;                /* get memory address of buffer */
        uptr->u4 = M[mema>>2];                  /* get status buffer address for XIO return status */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_startcmd starting inch cmd addr %04x StatAdr %06x mema %06x units %02x\n",
            addr, uptr->u4, mema, dptr->numunits);
        /* the next 8 words have drive data for each unit */
        /* WARNING 8 drives must be defined for this controller */
        /* so we will not have a map fault */
        for (i=0; i<dptr->numunits && i<8; i++) {   /* process all drives */
            int type = GET_TYPE(up->flags);     /* get disk type */
            up->ATTR = M[(mema>>2)+i+1];        /* save each unit's drive data */
            /* see if sec/trk and num hds are set, if not set them */
            if ((up->ATTR & 0x00ff0000) == 0)
                up->ATTR = (up->ATTR & 0xff00ffff) | ((hsdp_type[type].spt & 0xff) << 16);
            if ((up->ATTR & 0x0000ff00) == 0)
                up->ATTR = (up->ATTR & 0xffff00ff) | ((hsdp_type[type].nhds & 0xff) << 8);
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_startcmd ATTR data %08x unit %02x flags %02x sec %02x MHD %02x FHD %02x\n",
                up->ATTR, i, (up->ATTR >> 24)&0xff, (up->ATTR >> 16)&0xff,
                (up->ATTR >> 8)&0xff, (up->ATTR&0xff));
            up++;                               /* next unit for this device */
        }
        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd done inch cmd addr %04x\n", addr);
        uptr->CMD |= DSK_FINC;                  /* use 0xc0 for inch, just need int */
        sim_activate(uptr, 20);                 /* start things off */
        return 0;
        break;
    }

    case DSK_SCK:                               /* Seek command 0x07 */
    case DSK_XEZ:                               /* Rezero & Read IPL record 0x1f */
//      uptr->CMD &= ~(DSK_STAR);               /* show we do not have seek STAR in STAR */
    case DSK_WD:                                /* Write command 0x01 */
    case DSK_RD:                                /* Read command 0x02 */
    case DSK_LMR:                               /* read mode register */

        uptr->CMD |= cmd;                       /* save cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_startcmd starting disk seek r/w cmd %02x addr %04x\n",
            cmd, addr);
        sim_activate(uptr, 20);                 /* start things off */
        return 0;
        break;

    case DSK_NOP:                               /* NOP 0x03 */
        uptr->CMD |= cmd;                       /* save cmd */
        sim_activate(uptr, 20);                 /* start things off */
        return 0;
        break;

    case DSK_SNS:                               /* Sense 0x04 */
        uptr->CMD |= cmd;                       /* save cmd */
        sim_activate(uptr, 20);                 /* start things off */
        break;

    case DSK_WSL:                               /* WSL 0x31 */
        uptr->CMD |= cmd;                       /* save cmd */
        sim_activate(uptr, 20);                 /* start things off */
        return 0;
        break;

    case DSK_RSL:                               /* RSL 0x32 */
        uptr->CMD |= cmd;                       /* save cmd */
        sim_activate(uptr, 20);                 /* start things off */
        return 0;
        break;

    case DSK_WTL:                               /* WTL 0x51 */
        uptr->CMD |= cmd;                       /* save cmd */
        sim_activate(uptr, 20);                 /* start things off */
        return 0;
        break;

    case DSK_RTL:                               /* RTL 0x52 */
        uptr->CMD |= cmd;                       /* save cmd */
        sim_activate(uptr, 20);                 /* start things off */
        return 0;
        break;
    }
    sim_debug(DEBUG_CMD, dptr,
        "hsdp_startcmd done with hsdp_startcmd %02x addr %04x SNS %08x\n",
        cmd, addr, uptr->SNS);
    if (uptr->SNS & 0xff)                       /* any other cmd is error */
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    sim_activate(uptr, 20);                     /* start things off */
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle processing of disk requests. */
t_stat hsdp_srv(UNIT *uptr)
{
    uint16          chsa = GET_UADDR(uptr->CMD);
    DEVICE          *dptr = get_dev(uptr);
    DIB             *dibp = (DIB *)dptr->ctxt;
    /* get pointer to Dev Info Blk for this device */
    CHANP           *chp = (CHANP *)dibp->chan_prg; /* get pointer to channel program */
    struct ddata_t  *data = (struct ddata_t *)(uptr->DDATA);
    int             cmd = uptr->CMD & DSK_CMDMSK;
    int             type = GET_TYPE(uptr->flags);
//  uint32          trk, cyl, sec;
    uint32          trk, cyl;
    int             unit = (uptr - dptr->units);
    int             len;
    int             i;
    uint8           ch;
    uint16          ssize = hsdp_type[type].ssiz * 4;   /* disk sector size in bytes */
    uint32          tsize = hsdp_type[type].spt * ssize; /* get track size in bytes */
    uint32          tstart;
    uint8           buf2[1024];
    uint8           buf[1024];

    sim_debug(DEBUG_CMD, dptr,
        "hsdp_srv entry unit %02x CMD %08x chsa %04x count %04x %x/%x/%x \n",
         unit, uptr->CMD, chsa, chp->ccw_count, data->cyl, data->tpos, data->spos);

    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        if (cmd != DSK_SNS)                     /* we are completed with unit check status */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    sim_debug(DEBUG_CMD, dptr,
        "hsdp_srv cmd=%02x chsa %04x count %04x\n",
        cmd, chsa, chp->ccw_count);
    switch (cmd) {
    case 0:                                     /* No command, stop disk */
        break;

    case DSK_FINC:          /* use 0xc0 for inch, just need int */
        uptr->CMD &= ~(0xffff);                 /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr, "disk_srv cmd INCH chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
#ifdef FIX4MPX
        chan_end(chsa, SNS_CHNEND);             /* return just channel end OK */
#else
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
#endif
        break;

    case DSK_WSL:                               /* WSL 0x31 make into NOP */
        uptr->CMD &= ~(0xffff);                 /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv cmd WSL chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);

        sim_debug(DEBUG_DETAIL, dptr, "Sector 1 label");
        /* now read sector label data */
        len = chp->ccw_count;
//      for (i = 0; i < 30; i++) {
        for (i = 0; i < len; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                /* we have write error, bail out */
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
//          if (i == 16)
            if ((i%16) == 0)
                sim_debug(DEBUG_DETAIL, dptr, "\nSector 1 label");
            sim_debug(DEBUG_DETAIL, dptr, " %02x", buf[i]);
        }
        sim_debug(DEBUG_DETAIL, dptr, "\n");

        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_WTL:                               /* WTL 0x51 make into NOP */
        uptr->CMD &= ~(0xffff);                 /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv cmd WTL chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);

        sim_debug(DEBUG_DETAIL, dptr, "Track 0 label");
        /* now read track label data */
        for (i = 0; i < 30; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                /* we have read error, bail out */
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (i == 16)
                sim_debug(DEBUG_DETAIL, dptr, "\nTrack 0 label");
            sim_debug(DEBUG_DETAIL, dptr, " %02x", buf[i]);
        }
        sim_debug(DEBUG_DETAIL, dptr, "\n");

        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_NOP:                               /* NOP 0x03 */
        uptr->CMD &= ~(0xffff);                 /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv cmd NOP chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_SNS: /* 0x4 */
        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd CMD sense\n");

        /* bytes 0,1 - Cyl entry from STAR reg in STAR */
//        ch = (uptr->STAR >> 24) & 0xff;
        ch = (data->cyl >> 8) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv sense STAR b0 unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
//        ch = (uptr->STAR >> 16) & 0xff;
        ch = (data->cyl) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv sense STAR b1 unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* byte 2 - Track entry from STAR reg in STAR */
//        ch = (uptr->STAR >> 8) & 0xff;
        ch = (data->tpos) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv sense STAR b2 unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* byte 3 - Sector entry from STAR reg in STAR */
//        ch = (uptr->STAR) & 0xff;
        ch = (data->spos) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv sense STAR b3 unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* bytes 4 - mode reg, byte 0 of SNS */
        ch = (uptr->SNS >> 24) & 0xff;      /* return the sense data for device */
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
        /* bytes 8-11 - drive attribute register (DATR) entries from uptr->ATTR via
        * INCH cmd */
        ch = (uptr->ATTR >> 24) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv datr unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->ATTR >> 16) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv datr unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->ATTR >> 8 ) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv datr unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->ATTR >> 0) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv datr unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* bytes 12 & 13 are optional, so check if read done */
        /* TODO add drive status bits here */
        if ((test_write_byte_end(chsa)) == 0) {
            /* bytes 12 & 13 contain drive related status */
            ch = 0;                             /* zero for now */
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv dsr unit=%02x 1 %02x\n",
                unit, ch);
            chan_write_byte(chsa, &ch);

            ch = 030;                           /* drive on cyl and ready for now */
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv dsr unit=%02x 2 %02x\n",
                unit, ch);
            chan_write_byte(chsa, &ch);
        }
//NEW   uptr->SNS &= 0xff000000;                /* clear status bytes, but leave mode data */
//TRY        uptr->CMD &= ~(0xff00);             /* clear busy status */
        uptr->CMD &= ~(0xffff);                 /* remove old status bits & cmd */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_SCK:                               /* Seek cylinder, track, sector 0x07 */

        /* If we are waiting on seek to finish, check if there yet. */
        if (uptr->CMD & DSK_SEEKING) {
            /* see if on cylinder yet */
            if ((int32)(uptr->STAR >> 16) == (int32)data->cyl) {
                /* we are on cylinder, seek is done */
                sim_debug(DEBUG_CMD, dptr, "hsdp_srv seek on cylinder unit=%02x %02x %04x\n",
                    unit, uptr->STAR >> 16, data->cyl);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
//              set_devattn(chsa, SNS_DEVEND);  /* start the operation */
                sim_debug(DEBUG_CMD, dptr, "hsdp_srv seek end unit=%02x %02x %04x\n",
                    unit, uptr->STAR >> 16, data->cyl);
                chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
                sim_activate(uptr, 20);
                break;
            } else {
#ifndef TRYTHIS
                data->cyl = ((uptr->STAR >> 16)&0xffff);
                sim_activate(uptr, 10);
                break;
#else
                /* Compute delay based of difference. */
                /* Set next state = index */
                i = (int32)((uptr->STAR >> 16)&0xffff) - (int32)data->cyl;
                sim_debug(DEBUG_CMD, dptr, "hsdp_srv seek unit=%02x %02x %04x\n",
                    unit, uptr->STAR >> 16, i);
                if (i > 0 ) {
                    if (i > 500) {
                        data->cyl += 500;       /* seek 500 cyl */
                        sim_activate(uptr, 800);
                    } else
                    if (i > 200) {
                        data->cyl += 200;       /* seek 200 cyl */
                        sim_activate(uptr, 400);
                    } else
                    if (i > 50) {
                        data->cyl += 50;        /* seek 50 cyl */
                        sim_activate(uptr, 200);
                    } else {
                        data->cyl = uptr->STAR >> 16;   /* Seek the rest i cyl */
                        sim_activate(uptr, 20);
                    }
                    if (data->cyl >= (int)hsdp_type[type].cyl)   /* test for over max */
                        data->cyl = hsdp_type[type].cyl-1;  /* make max */
                } else {
                    if (i < -500) {
                        data->cyl -= 500;       /* seek 500 cyl */
                        sim_activate(uptr, 800);
                    } else
                    if (i < -200) {
                        data->cyl -= 200;       /* seek 200 cyl */
                        sim_activate(uptr, 400);
                    } else
                    if (i < -50) {
                        data->cyl -= 50;        /* seek 50 cyl */
                        sim_activate(uptr, 200);
                    } else {
                        data->cyl = uptr->STAR >> 16;   /* Seek the rest i cyl */
                        sim_activate(uptr, 20);
                    }
                    if ((int32)data->cyl < 0)   /* test for less than zero */
                        data->cyl = 0;          /* make zero */
                }
#endif
                sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv seek next unit=%02x %02x %04x\n",
                    unit, uptr->STAR >> 16, data->cyl);
#ifdef OLD_WAY
                sim_activate(uptr, 2);
#else
/*BAD?*/        sim_activate(uptr, 10);
#endif
                break;
            }
        }

        /* not seeking, so start a new seek */
        /* set buf data to current STAR values */
        buf[0] = (data->cyl >> 8) & 0xff;
        buf[1] = (data->cyl) & 0xff;
        buf[2] = (data->tpos) & 0xff;
        buf[3] = (data->spos) & 0xff;
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv STAR unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        /* Read in up to 4 character seek code */
        for (i = 0; i < 4; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (i == 0) {
                    sim_debug(DEBUG_DETAIL, dptr,
                        "hsdp_srv seek error unit=%02x star %02x %02x %02x %02x\n",
                        unit, buf[0], buf[1], buf[2], buf[3]);
                    /* we have error, bail out */
                    uptr->CMD &= ~(0xffff);     /* remove old status bits & cmd */
                    uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                    return SCPE_OK;
                    break;
                }
                /* just read the next byte */
            }
        }
        /* done reading, see how many we read */
        if (i == 1) {
            /* UTX wants to set seek STAR to zero */
            buf[0] = buf[1] = buf[2] = buf[3] = 0;
        }
        /* else the cyl, trk, and sect are ready to update */
rezero:
        sim_debug(DEBUG_DETAIL, dptr,
            "hsdp_srv seek unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);
        /* save STAR (target sector) data in STAR */
        uptr->STAR = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3]);
        cyl = (uptr->STAR >> 16) & 0xffff;      /* get the cylinder */
        trk = buf[2];                           /* get the track */
        sim_debug(DEBUG_DETAIL, dptr,
           "hsdp_srv SEEK cyl %04x trk %02x sec %02x unit=%02x\n",
           cyl&0xffff, trk, buf[3], unit);

        /* Check if seek valid */
        if (cyl >= hsdp_type[type].cyl ||
            trk >= hsdp_type[type].nhds ||
            buf[3] >= hsdp_type[type].spt)  {
            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv seek ERROR cyl %04x trk %02x sec %02x unit=%02x\n",
                cyl, trk, buf[3], unit);
            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK; /* set error status */

            /* we have an error, tell user */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);  /* end command */
            break;
        }

//      uptr->CMD |= DSK_STAR;                  /* show we have seek STAR in STAR */
        /* calc the sector address of data */
        /* calculate file position in bytes of requested sector */
        tstart = (cyl * hsdp_type[type].nhds * tsize) + (trk * tsize) + (buf[3] * ssize);
        data->tpos = trk;                       /* save the track/head number */
        data->spos = buf[3];                    /* save the sector number */
        sim_debug(DEBUG_DETAIL, dptr,
            "hsdp_srv seek start %04x cyl %04x trk %02x sec %02x\n",
            tstart, cyl, trk, buf[3]);
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* seek home */
            sim_debug(DEBUG_DETAIL, dptr, "hsdp_srv Error on seek to %08x\n", tstart);
        }

        /* Check if already on correct cylinder */
        if (cyl != data->cyl) {
            /* Do seek */
            uptr->CMD |= DSK_SEEKING;           /* show we are seeking */
            sim_debug(DEBUG_DETAIL, dptr,
                "hsdp_srv seek unit=%02x cyl %04x trk %02x sec %02x\n",
                unit, cyl, trk, buf[3]);
            sim_activate(uptr, 20);             /* start us off */
//WHAT?     chan_end(chsa, SNS_CHNEND);
        } else {
            /* Yes, we done */
            sim_debug(DEBUG_DETAIL, dptr,
                "hsdp_srv calc sect addr seek end %04x cyl %04x trk %02x sec %02x\n",
                tstart, cyl, trk, buf[3]);
            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
//          sim_activate(uptr, 20);
            chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
        }
        return SCPE_OK;

    case DSK_XEZ:                               /* Rezero & Read IPL record */

        sim_debug(DEBUG_CMD, dptr, "RD REZERO IPL unit=%02x seek 0\n", unit);
        /* Do a seek to 0 */
        uptr->STAR = 0;                         /* set STAR to 0, 0, 0 */
        uptr->CMD &= ~(0xffff);                 /* remove old cmd */
        uptr->CMD |= DSK_SCK;                   /* show as seek command */
        tstart = 0;                             /* byte offset is 0 */
        /* Read in 1 dummy character for length to inhibit SLI posting */
        if (chan_read_byte(chsa, &buf[0])) {
            /* we have error, bail out */
            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
        /* zero stuff */
        buf[0] = buf[1] = buf[2] = buf[3] = 0;
        goto rezero;                            /* merge with seek code */
        break;

    case DSK_LMR:
        sim_debug(DEBUG_CMD, dptr, "Load Mode Reg unit=%02x\n", unit);
        /* Read in 1 character of mode data */
        if (chan_read_byte(chsa, &buf[0])) {
            /* we have error, bail out */
            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
        sim_debug(DEBUG_CMD, dptr, "Load Mode Reg unit=%02x old %x new %x\n",
            unit, (uptr->SNS)&0xff, buf[0]);
        uptr->CMD &= ~(0xffff);                 /* remove old cmd */
        uptr->SNS &= 0x00ffffff;                /* clear old mode data */
        uptr->SNS |= (buf[0] << 24);            /* save mode value */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_RD:                                /* Read Data */
        if ((uptr->CMD & DSK_READING) == 0) {   /* see if we are reading data */
            uptr->CMD |= DSK_READING;           /* read from disk starting */
            sim_debug(DEBUG_CMD, dptr,
                "DISK READ starting unit=%02x CMD %02x\n", unit, uptr->CMD);
        }

        if (uptr->CMD & DSK_READING) {          /* see if we are reading data */
            /* read in a sector of data from disk */
            if ((len=sim_fread(buf, 1, ssize, uptr->fileref)) != ssize) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error %08x on read %04x of diskfile cyl %04x hds %02x sec %02x\n",
                     len, ssize, data->cyl, data->tpos, data->spos);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            sim_debug(DEBUG_CMD, dptr,
                "hsdp_srv after READ chsa %04x buffer %06x count %04x\n",
                chsa, chp->ccw_addr, chp->ccw_count);
            /* process the next sector of data */
            for (i=0; i<len; i++) {
                ch = buf[i];                    /* get a char from buffer */
                if (chan_write_byte(chsa, &ch)) {   /* put a byte to memory */
                    sim_debug(DEBUG_DATA, dptr,
                      "DISK Read %04x bytes leaving %04x from diskfile /%04x/%02x/%02x\n",
                       i, chp->ccw_count, data->cyl, data->tpos, data->spos);
                    uptr->CMD &= ~(0xffff);     /* remove old status bits & cmd */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                    goto rddone;
                }
            }

            sim_debug(DEBUG_CMD, dptr,
                "DISK READ %04x bytes leaving %4x to be read from diskfile /%04x/%02x/%02x\n",
                ssize, chp->ccw_count, data->cyl, data->tpos, data->spos);
            data->spos++;
            /* set sector to read next one */
            if (data->spos >= (hsdp_type[type].spt)) {
                data->spos = 0;                 /* number of sectors per track */
                data->tpos++;                   /* track position */
                if (data->tpos >= (hsdp_type[type].nhds)) {
                    data->tpos = 0;             /* number of tracks per cylinder */
                    data->cyl++;                /* cylinder position */
                    if (data->cyl >= (int)(hsdp_type[type].cyl)) {
                        /* EOM reached, abort */
                        sim_debug(DEBUG_CMD, dptr,
                            "DISK Read reached EOM for read from disk @ /%04x/%02x/%02x\n",
                            data->cyl, data->tpos, data->spos);
                        uptr->CMD &= ~(0xffff); /* remove old status bits & cmd */
                        data->cyl = 0;          /* reset cylinder position */
                        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                        break;
                    }
                }
            }
            /* see if we are done reading data */
            if (test_write_byte_end(chsa)) {
                sim_debug(DEBUG_DATA, dptr,
                    "DISK Read complete for read from disk @ /%04x/%02x/%02x\n",
                    data->cyl, data->tpos, data->spos);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                break;
            } else {
                sim_debug(DEBUG_DATA, dptr,
                    "DISK sector read complete, %x bytes to go from diskfile /%04x/%02x/%02x\n",
                    chp->ccw_count, data->cyl, data->tpos, data->spos);
                sim_activate(uptr, 10);         /* wait to read next sector */
                break;
            }
rddone:
            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
        }
        break;

    case DSK_WD:            /* Write Data */
        if ((uptr->CMD & DSK_WRITING) == 0) {   /* see if we are writing data */
            uptr->CMD |= DSK_WRITING;           /* write to disk starting */
            sim_debug(DEBUG_CMD, dptr,
                "DISK WRITE starting unit=%02x CMD %02x\n", unit, uptr->CMD);
        }
        if (uptr->CMD & DSK_WRITING) {          /* see if we are writing data */
            /* process the next sector of data */
            len = 0;                            /* used here as a flag for short read */
            for (i=0; i<ssize; i++) {
                if (chan_read_byte(chsa, &ch)) {    /* get a byte from memory */
                    /* if error on reading 1st byte, we are done writing */
                    if (i == 0) {
                        uptr->CMD &= ~(0xffff);  /* remove old status bits & cmd */
                        sim_debug(DEBUG_CMD, dptr,
                            "DISK Wrote %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                            ssize, data->cyl, data->tpos, data->spos);
                        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                        goto wrdone;
                    }
                    ch = 0;                     /* finish out the sector with zero */
                    len++;                      /* show we have no more data to write */
                }
                buf2[i] = ch;                   /* save the char */
            }
            /* write the sector to disk */
            if ((i=sim_fwrite(buf2, 1, ssize, uptr->fileref)) != ssize) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error %08x on write %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                    i, ssize, data->cyl, data->tpos, data->spos);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (len != 0) {                     /* see if done with write command */
                sim_debug(DEBUG_DATA, dptr,
                    "DISK WroteB %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                    ssize, data->cyl, data->tpos, data->spos);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                break;
            }
            sim_debug(DEBUG_CMD, dptr,
                "DISK WR to sec end %0x4x bytes end %04x to diskfile cyl %04x hds %02x sec %02x\n",
                len, ssize, data->cyl, data->tpos, data->spos);
            data->spos++;
            if (data->spos >= (hsdp_type[type].spt)) {
                data->spos = 0;                 /* number of sectors per track */
                data->tpos++;                   /* track position */
                if (data->tpos >= (hsdp_type[type].nhds)) {
                    data->tpos = 0;             /* number of tracks per cylinder */
                    data->cyl++;                /* cylinder position */
                    if (data->cyl >= (int)(hsdp_type[type].cyl)) {
                        /* EOM reached, abort */
                        sim_debug(DEBUG_DETAIL, dptr,
                            "Error %08x on write %04x to diskfile cyl %04x hds %02x sec %02x\n",
                            i, ssize, data->cyl, data->tpos, data->spos);
                        uptr->CMD &= ~(0xffff);  /* remove old status bits & cmd */
                        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                        break;
                    }
                }
            }
            sim_activate(uptr, 10);             /* keep writing */
            break;
wrdone:
            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
         }
         break;

    case DSK_RSL:                               /* RSL 0x32 */
#ifdef DO_DYNAMIC_DEBUG
                cpu_dev.dctrl |= (DEBUG_INST | DEBUG_CMD | DEBUG_EXP | DEBUG_IRQ);
#endif
        /* Read sector label zero to get disk geometry */
        /* write 30 bytes, b0-b1=cyl, b1=trk, b2=sec */
        /* zero the Track Label Buffer */
        for (i = 0; i < 30; i++)
            buf[i] = 0;

        /* the dmap pointer is placed by the vendor or diag into the */
        /* track zero label in word 3 of the 30 byte label. */
        /* The disk address is the last track of the user area.  The vendor */
        /* reserves the last cylinder, SEL diags reserve the next two, so the */
        /* addr is CYL-4/HDS-1/0 and is VDT.  The UTX/MPX media table is on */
        /* previous track, so MDT = VDT-SPT is CYL-4/HDS-2/0 */
        /* The UTX flaw map is at FMAP = MDT-SPT CYL-4/HDS-3/0 */
        /* UTX media map is 1 track lower at UTXMM=FMAP-SPT CYL-4/HDS-4/0 */
        /* The UTX med map is pointed to by sector label 1 */
        /* simulate pointers here, set wd[3] in label to VDT */

        /* set buf data to current STAR values */
//      cyl = (uptr->STAR >> 16) & 0xffff;      /* get the cylinder */
//      trk = (uptr->STAR >> 8) & 0xff;         /* get the track */
//      sec = (uptr->STAR) & 0xff;              /* get the sector */

        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd RSL STAR %08x disk geom %08x\n",
            uptr->STAR, GEOM(type));

#ifdef OLD_WAY
        /* set buf data to current STAR values */
        buf[0] = (data->cyl >> 8) & 0xff;       /* lcyl  cyl upper 8 bits */
        buf[1] = data->cyl & 0xff;              /* lcyl  cyl lower 8 bits */
        buf[2] = data->tpos & 0xff;             /* ltkn  trk */
        buf[3] = data->spos & 0xff;             /* lid   sector ID */
        buf[4] = 0x80;                          /* set lflg1 to show good sector */

        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv RSL unit=%0x2x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        cyl = hsdp_type[type].cyl-1;            /* last cyl */
        trk = hsdp_type[type].nhds-1;           /* last head number */
        sec = hsdp_type[type].spt-1;            /* last sector number */

//      daddr = (cap-1) - (3 * csize) - ((nhds - 2) * tsize);
        tstart = (((cyl * hsdp_type[type].nhds) * hsdp_type[type].spt) +
            (trk * hsdp_type[type].spt) + sec); /* last sector address */
        tstart = tstart - (3 * hsdp_type[type].nhds * hsdp_type[type].spt);
        tstart = tstart - (2 * hsdp_type[type].spt) + 1;
        /* UTX dmap at daddr - 32 sec (2 tracks) */
        tstart = tstart - (2 * hsdp_type[type].spt);
#else
        /* get sector address of UTX media descriptor */
        tstart = ((CYL(type)-4) * SPC(type)) +
            ((HDS(type)-2) * SPT(type)) - SPT(type);
#endif

        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv SL1 RSL sector %d %x star %02x %02x %02x %02x\n",
            tstart, tstart, buf[0], buf[1], buf[2], buf[3]);

#ifdef OLD_WAY
        cyl = hsdp_type[type].cyl-3;            /* cyl */
        trk = hsdp_type[type].nhds-2;           /* head */
        sec = 0;                                /* last sector number */
//      daddr = (cap-1) - (3 * csize) - ((nhds - 2) * tsize);
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv RSL sector %d %x star %02x %02x %02x %02x\n",
            tstart, tstart, cyl>>8, cyl&0xff, trk, sec);
#endif

        /* on HSDP DMAP is in wd 3 on label 0 */
        /* on HSDP UMAP is in wd 3 on label 1 */
        /* on UDP & DPII DMAP is in wd 3 on label 0 */
        /* on UDP & DPII UMAP is in wd 4 on label 0 */
//      tstart = 0x440aa;                       /* 819/0/7 logical 278698 */
        tstart = (tstart * (SPT(type) - 1))/SPT(type);  /* make logical */
        /* store into sec 1 label */
        buf[12] = (tstart >> 24) & 0xff;        /* UMAP pointer */
        buf[13] = (tstart >> 16) & 0xff;
        buf[14] = (tstart >> 8) & 0xff;
        buf[15] = (tstart) & 0xff;

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
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (i == 16)
                sim_debug(DEBUG_DETAIL, dptr, "\nSector 1 label");
            sim_debug(DEBUG_DETAIL, dptr, " %02x", buf[i]);
        }
        sim_debug(DEBUG_DETAIL, dptr, "\n");

        /* command done */
        uptr->CMD &= ~(0xffff);                 /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv cmd RSL done chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_RTL:                               /* RTL 0x52 */
        /* Read track zero to get disk geometry */
        /* write 30 bytes, b0-b1=cyl, b1=trk, b2=sec */

#ifdef DO_DYNAMIC_DEBUG
                cpu_dev.dctrl |= (DEBUG_INST | DEBUG_CMD | DEBUG_EXP | DEBUG_IRQ | DEBUG_XIO);
#endif
        /* zero the Track Label Buffer */
        for (i = 0; i < 30; i++)
            buf[i] = 0;

        /* set buf data to current STAR values */
//      cyl = (uptr->STAR >> 16) & 0xffff;      /* get the cylinder */
//      trk = (uptr->STAR >> 8) & 0xff;         /* get the track */
//      sec = (uptr->STAR) & 0xff;              /* get the sector */

        sim_debug(DEBUG_CMD, dptr, "hsdp_startcmd RTL STAR %08x disk geom %08x\n",
            uptr->STAR, GEOM(type));

        /* set buf data to current STAR values */
        buf[0] = (data->cyl >> 8) & 0xff;       /* lcyl  cyl upper 8 bits */
        buf[1] = data->cyl & 0xff;              /* lcyl  cyl lower 8 bits */
        buf[2] = data->tpos & 0xff;             /* ltkn  trk */
        buf[3] = data->spos & 0xff;             /* lid   sector ID */
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
        /* The UTX med map is pointed to by sector label 1 */
        /* simulate pointers here, set wd[3] in label to VDT */
#ifdef OLD_WAY
        /* the dmap pointer is placed by the vendor of diag into track 0 */
        /* simulate pointer here */
//      i = 250191;  /* 0x3d14f 337/13/0F */    /* dmap sector address */
        cyl = hsdp_type[type].cyl-1;            /* last cyl */
        trk = hsdp_type[type].nhds-1;           /* last head number */
        sec = hsdp_type[type].spt-1;            /* last sector number */
//      daddr = (cap) - (3 * csize) - (2 * tsize);
        tstart = (((cyl * hsdp_type[type].nhds) * hsdp_type[type].spt) +
            (trk * hsdp_type[type].spt) + sec); /* last sector address */
        tstart = tstart - (3 * hsdp_type[type].nhds * hsdp_type[type].spt);
        tstart = tstart - (2 * hsdp_type[type].spt) + 1;
//      tstart = 249248;
        /* vaddr is (cap) - 3 cyl - 8 sec */
//BAD   tstart = ((cyl-3) * (trk+1) * (sec+1)) - 8;   /* 249256 0x3cda8 */
//      tstart = ((cyl-3) * (trk+1) * (sec+1)) -
//         (2 * (trk+1) * (sec+1));             /* 249248 0x3cda0 */
#else
        /* get sector address of vendor defect table */
        /* 249264 (819/17/0) for 9346 - 823/19/16 */ 
        tstart = (CYL(type)-4) * SPC(type) + (HDS(type)-2) * SPT(type);

        cyl = hsdp_type[type].cyl-1;            /* last cyl */
        trk = hsdp_type[type].nhds-1;           /* last head number */
//      sec = hsdp_type[type].spt-1;            /* last sector number */
#endif

        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv RTL STAR %04x/%02x/%02x Dmap pointer 0x%08x %d\n",
            cyl-3, trk-1, 0, tstart, tstart);
        sim_debug(DEBUG_CMD, dptr,
            "hsdp_srv TRK0 RTL sector %d %x star %02x %02x %02x %02x\n",
            tstart, tstart, buf[0], buf[1], buf[2], buf[3]);

        if (uptr->STAR == 0) {                  /* only write dmap address in trk 0 */
            /* output last sector address of disk */
            buf[12] = (tstart >> 24) & 0xff;    /* ldeallp DMAP pointer */
            buf[13] = (tstart >> 16) & 0xff;
            buf[14] = (tstart >> 8) & 0xff;
            buf[15] = (tstart) & 0xff;
        }

#ifdef NOTNOW
        tstart -= SPT(type);                    /* calc utxfmap address */
        if (uptr->STAR == 0) {                  /* only write dmap address on trk 0 */
            buf[16] = (tstart >> 24) & 0xff;    /* ldeallp UMAP */
            buf[17] = (tstart >> 16) & 0xff;
            buf[18] = (tstart >> 8) & 0xff;
            buf[19] = (tstart) & 0xff;
        }
#endif

        /* the tech doc shows the cyl/trk/sec data is in the first 4 bytes */
        /* of the track label, BUT it is really in the configuration data */
        /* area too.  That is where UTX looks.  Byte 27 is sectors/track */
        /* and byte 28 is number of heads. Byte 25 is copy of byte 27. */
        buf[25] = hsdp_type[type].spt & 0xff;   /* sect per track 35 */
        buf[26] = hsdp_type[type].type | 1;     /* sense data set for 1024 byte blk */
        buf[27] = hsdp_type[type].spt & 0xff;   /* sec per track 35 */
        buf[28] = hsdp_type[type].nhds & 0xff;  /* num heads 10 */

        sim_debug(DEBUG_DETAIL, dptr, "Track 0 label");
        /* now write track label data */
        for (i = 0; i < 30; i++) {
            if (chan_write_byte(chsa, &buf[i])) {
                /* we have write error, bail out */
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (i == 16)
                sim_debug(DEBUG_DETAIL, dptr, "\nTrack 0 label");
            sim_debug(DEBUG_DETAIL, dptr, " %02x", buf[i]);
        }
        sim_debug(DEBUG_DETAIL, dptr, "\n");

        /* command done */
        uptr->CMD &= ~(0xffff);                 /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr, "hsdp_srv cmd RTL done chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    default:
        sim_debug(DEBUG_CMD, dptr, "invalid command %02x unit %02x\n", cmd, unit);
        uptr->SNS |= SNS_CMDREJ;
        uptr->CMD &= ~(0xffff);                 /* remove old status bits & cmd */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
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

    uptr->CMD &= ~0x7fff;                       /* clear out the flags but leave ch/sa */
    uptr->SNS = ((uptr->SNS & 0x00ffffff) | (hsdp_type[i].type << 24));  /* save mode value */
    /* total sectors on disk */
    uptr->capac = CAP(i);                       /* size in sectors */

    sim_debug(DEBUG_EXP, &dda_dev, "DMA init device %s on unit DMA%.1x cap %x %d\n",
        dptr->name, GET_UADDR(uptr->CMD), uptr->capac, uptr->capac);
}

t_stat hsdp_reset(DEVICE *dptr)
{
    /* add reset code here */
    return SCPE_OK;
}

/* create the disk file for the specified device */
int hsdp_format(UNIT *uptr) {
//  struct ddata_t  *data = (struct ddata_t *)uptr->up7;
//  uint16      addr = GET_UADDR(uptr->CMD);
    int         type = GET_TYPE(uptr->flags);
    DEVICE      *dptr = get_dev(uptr);
    uint32      ssize = hsdp_type[type].ssiz * 4;       /* disk sector size in bytes */
    uint32      tsize = hsdp_type[type].spt;            /* get track size in sectors */
    uint32      csize = hsdp_type[type].nhds * tsize;   /* get cylinder size in sectors */
    uint32      cyl = hsdp_type[type].cyl;              /* get # cylinders */
    uint16      spc = hsdp_type[type].nhds * hsdp_type[type].spt;   /* sectors/cyl */
    uint32      cap = hsdp_type[type].cyl * csize;      /* disk capacity in sectors */
    uint32      cylv = cyl;                             /* number of cylinders */
    uint8       *buff;
                /* last sector address of disk (cyl * hds * spt) - 1 */
    uint32      laddr = CAP(type) - 1;                  /* last sector of disk */
                /* get sector address of vendor defect table */
                /* put data = 0xf0000004 0xf4000000 */
    int32       vaddr = (CYL(type)-4) * SPC(type) + (HDS(type)-1) * SPT(type);
                /* get sector address of utx diag map track 0 pointer */
                /* put data = 0xf0000000 + cyl-1, 0x8a000000 + daddr, */
                /* 0x9a000000 + cyl-1, 0xf4000000 */
    int32       daddr = vaddr - SPT(type);
                /* get sector address of utx flaw map sec 1 pointer */
                /* use this address for sec 1 label pointer */
    int32       uaddr = daddr - SPT(type);
#ifdef MAYBE
                /* get sector address of utx flaw data (1 track long) */
                /* set trace data to zero */
    int32       faddr = uaddr - SPT(type);
#endif
    uint32      umap[256] =
#ifdef OLDWAY
//              {0x4e554d50,0x4450b,0x43fbb,0,0,0,0xffffffff,0xe10,
                {0x4e554d50,0x3d14f,0x00000,0,0,0,0xffffffff,0xe10,
//              7,0x5258,0,0x4e5c,0x3e,0x43fbc,0,0xd32c,
                7,0x5258,0,0x4e5c,0x3e,0x43fbc,0,0xd32c,
                0x79,0x187cc,0x118,0x14410,0x23f,0,0,0,
                0,0x3821a2d6,0x4608c,0x1102000,0,0,0x4608c,0,
                0,0x46069,0,0,0x46046,0,0,0x46023,
                0,0,0x46000,0,0,0x45fdd,0,0,
                0x45fba,0,0,0,0,0,0,0,
                };
#else
                {
//                  0xf003d14f,0x8a03cda0,0x9a03cdbf,0x8903cdc0,
//                  0x9903d01f,0x8c03d020,0x9c03d14f,0xf4000000,
                    0xf0000000 | (cap-1), 0x8a000000 | daddr,
                        0x9a000000 | (daddr + ((2 * tsize) - 1)),
                        0x89000000 | (daddr + (2 * tsize)),
                        0x99000000 | ((cap-1)-spc),
                        0x8c000000 | (cap-spc),
                        0x9c000000 | (cap-1), 0xf4000000,
                };
#endif
                /*            250191     249248      250191     0   */
//  uint32      dmap[4] = {0xf003d14f, 0x8a03cda0, 0x9a03d14f, 0xf4000000};
    int         i;
                /* vendor flaw map */
    uint32      vmap[2] = {0xf0000004, 0xf4000000};
    uint32      dmap[4] = {0xf0000000 | (cap-1), 0x8a000000 | daddr,
                    0x9a000000 | (cap-1), 0xf4000000};

    /* see if user wants to initialize the disk */
    if (!get_yn("Initialize disk? [Y] ", TRUE)) {
        return 1;
    }

#ifdef OLD_WAY
    /* dmap data */
    /* the diag map (dmap) is in last sec of disk */
    /* (cyl * trk * sec) - 1  = cap - 1*/
    /* 0xf0046531 - 288049 - 822/9/34 */
    /* 0x8a0460d2 - 286930 - 819/8/0 */
    /* 0x9a046531 - 288049 - 822/9/34 */
    /* 0xf4000000 */
    dmap[0] = 0xf0000000 | (cap-1);         /* 0xf003d14f */
    dmap[1] = 0x8a000000 | daddr;           /* 0x8a03ccaf */
    dmap[2] = 0x9a000000 | (cap-1);         /* 0x9a03d14f */
    dmap[3] = 0xf4000000;
#endif

    /* seek to sector 0 */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        fprintf (stderr, "Error on seek to 0\r\n");
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

    /* byte swap the buffers for dmap and umap */
//  for (i=0; i<256; i++) {
    for (i=0; i<8; i++) {
        umap[i] = (((umap[i] & 0xff) << 24) | ((umap[i] & 0xff00) << 8) |
            ((umap[i] & 0xff0000) >> 8) | ((umap[i] >> 24) & 0xff));
    }
    for (i=0; i<4; i++) {
        dmap[i] = (((dmap[i] & 0xff) << 24) | ((dmap[i] & 0xff00) << 8) |
            ((dmap[i] & 0xff0000) >> 8) | ((dmap[i] >> 24) & 0xff));
    }
    for (i=0; i<2; i++) {
        vmap[i] = (((vmap[i] & 0xff) << 24) | ((vmap[i] & 0xff00) << 8) |
            ((vmap[i] & 0xff0000) >> 8) | ((vmap[i] >> 24) & 0xff));
    }

    /* now seek to end of disk and write the dmap data */
    /* setup dmap pointed to by track label 0 wd[3] = (cyl-4) * spt + (spt - 1) */

    /* write dmap data to last sector on disk */
    sim_fseek(uptr->fileref, laddr*ssize, SEEK_SET);    /* seek last sector */
    if ((sim_fwrite((char *)&dmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
        sim_debug(DEBUG_CMD, dptr,
        "Error on vendor map write to diskfile sect %06x\n", (cap-1) * ssize);
    }

    /* 1 cylinder is saved for disk manufacture */
    /* 2 cylinders are saved for diagnostics */
    /* write vmap to vaddr that is the address in trk 0 label + 1 track */
    /* daddr is (cap) - 3 cyl - 2 tracks */
    /* vaddr is (cap) - 3 cyl - 1 track */

    /* seek to vendor label area */
    sim_fseek(uptr->fileref, (vaddr)*ssize, SEEK_SET);  /* seek UMAP */
    if ((sim_fwrite((char *)&vmap, sizeof(uint32), 2, uptr->fileref)) != 2) {
        sim_debug(DEBUG_CMD, dptr,
        "Error on vendor map write to diskfile sect %06x\n", vaddr * ssize);
    }

    /* write dmap to daddr that is the address in trk 0 label */
    /* daddr is (cap) - 3 cyl - 2 tracks */
    /* vaddr is daddr - spt */

    sim_fseek(uptr->fileref, (daddr)*ssize, SEEK_SET);   /* seek DMAP */
    if ((sim_fwrite((char *)&dmap, sizeof(uint32), 4, uptr->fileref)) != 4) {
        sim_debug(DEBUG_CMD, dptr,
        "Error on dmap write to diskfile sect %06x\n", daddr * ssize);
    }

#ifdef MAYBE
    sim_fseek(uptr->fileref, (uaddr)*ssize, SEEK_SET);   /* seek UMAP */
    if ((sim_fwrite((char *)&umap, sizeof(uint32), 4, uptr->fileref)) != 4) {
        sim_debug(DEBUG_CMD, dptr,
        "Error on umap write to diskfile sect %06x\n", uaddr * ssize);
    }
#endif

    printf("writing to vmap sec %x bytes %x\n",
        vaddr, (vaddr)*ssize);
    printf("writing zeros to umap sec %x bytes %x\n",
        uaddr, (uaddr)*ssize);
    printf("writing dmap to %x %d %x %d dmap to %x %d %x %d\n",
       cap-1, cap-1, (cap-1)*ssize, (cap-1)*ssize,
       daddr, daddr, daddr*ssize, daddr*ssize);

    /* seek home again */
    sim_fseek(uptr->fileref, 0, SEEK_SET);      /* seek home */
    free(buff);                                 /* free cylinder buffer */
//  set_devattn(addr, SNS_DEVEND);              /* start us up */
    return 0;
}

/* attach the selected file to the disk */
t_stat hsdp_attach(UNIT *uptr, CONST char *file)
{
    uint16          addr = GET_UADDR(uptr->CMD);
    int             type = GET_TYPE(uptr->flags);
    DEVICE          *dptr = get_dev(uptr);
    t_stat          r;
    uint32          ssize;                      /* sector size in bytes */
    struct ddata_t  *data;
    uint8           buff[1024];

    /* see if valid disk entry */
    if (hsdp_type[type].name == 0) {            /* does the assigned disk have a name */
        detach_unit(uptr);                      /* no, reject */
        return SCPE_FMT;                        /* error */
    }

    /* have simulator attach the file to the unit */
    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;

    /* get a buffer to hold hsdp_t structure */
    /* extended data structure per unit */
    if ((data = (struct ddata_t *)calloc(1, sizeof(struct ddata_t))) == 0) {
        detach_unit(uptr);
        return SCPE_FMT;
    }

    uptr->DDATA = (void *)data;                 /* save pointer to structure in DDATA */
    uptr->capac = CAP(type);                    /* size in sectors */
    ssize = SSB(type);                          /* disk sector size in bytes */

    sim_debug(DEBUG_CMD, dptr, "Disk %s cyl %d hds %d sec %d ssiz %d capacity %d\n",
        hsdp_type[type].name, hsdp_type[type].cyl, hsdp_type[type].nhds, 
        hsdp_type[type].spt, ssize, uptr->capac); /* hsdp capacity */

    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        detach_unit(uptr);                      /* if no space, error */
        return SCPE_FMT;                        /* error */
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
    data->cyl = 0;                              /* current cylinder position */
    data->tpos = 0;                             /* current track position */
    data->spos = 0;                             /* current sector position */

    sim_debug(DEBUG_CMD, dptr,
        "Attach %s cyl %d hds %d spt %d spc %d cap sec %d cap bytes %d\n",
        hsdp_type[type].name, CYL(type), HDS(type), SPT(type), SPC(type),  
        CAP(type), CAPB(type));

    sim_debug(DEBUG_CMD, dptr, "File %s attached to %s\r\n",
        file, hsdp_type[type].name);

    set_devattn(addr, SNS_DEVEND);
    return SCPE_OK;
}

/* detach a disk device */
t_stat hsdp_detach(UNIT *uptr) {
    struct ddata_t       *data = (struct ddata_t *)uptr->DDATA;

    if (data != 0) {
        free(data);                             /* free disk data structure */
    }
    uptr->DDATA = 0;                            /* no pointer to disk data */
    uptr->CMD &= ~0xffff;                       /* no cmd and flags */
    return detach_unit(uptr);                   /* tell simh we are done with disk */
}

/* boot from the specified disk unit */
t_stat hsdp_boot(int32 unit_num, DEVICE * dptr) {
    UNIT    *uptr = &dptr->units[unit_num];     /* find disk unit number */

    sim_debug(DEBUG_CMD, dptr, "Disk Boot dev/unit %x\n", GET_UADDR(uptr->CMD));
    SPAD[0xf4] = GET_UADDR(uptr->CMD);          /* put boot device chan/sa into spad */
    SPAD[0xf8] = 0xF000;                        /* show as F class device */
    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;                      /* attached? */
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
//      int32   capac = CAP(i);                     /* disk capacity in sectors */
//      int32   ssize = SSB(i);                     /* disk sector size in bytes */
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
