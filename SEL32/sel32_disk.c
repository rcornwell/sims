/* sel32_disk.c: SEL-32 2311/2314 Disk Processor II

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
extern  t_stat  set_inch(UNIT *uptr, uint32 inch_addr); /* set channel inch address */
extern  CHANP  *find_chanp_ptr(uint16 chsa);             /* find chanp pointer */

extern  uint32  M[];                            /* our memory */
extern  uint32  SPAD[];                         /* cpu SPAD memory */

#ifdef NUM_DEVS_DISK

#define UNIT_DISK   UNIT_ATTABLE | UNIT_IDLE | UNIT_DISABLE

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
#define SPT(type)               (disk_type[type].spt)
/* get sectors per cylinderfor specified type */
#define SPC(type)               (disk_type[type].spt*disk_type[type].nhds)
/* get number of cylinders for specified type */
#define CYL(type)               (disk_type[type].cyl)
/* get number of heads for specified type */
#define HDS(type)               (disk_type[type].nhds)
/* get disk capacity in sectors for specified type */
#define CAP(type)               (CYL(type)*HDS(type)*SPT(type))
/* get number of bytes per sector for specified type */
#define SSB(type)               (disk_type[type].ssiz*4)
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

Memory attribute register layout
bits 0-7 - Flags
        bits 0&1 - 00=Reserved, 01=MHD, 10=FHD, 11=MHD with FHD option
        bit  2   - 1=Cartridge module drive
        bit  3   - 0=Reserved
        bit  4   - 1=Drive not present
        bit  5   - 1=Dual Port
        bit  6   - 0=Blk size   00=768 byte blk 
        bit  7   - 0=Blk size   01=1024 byte blk
bits 8-15 - sector count (sectors per track)(F16=16, F20=20)
bits 16-23 - MHD Head count (number of heads on MHD)
bits 24-31 - FHD head count (number of heads on FHD or number head on FHD option of
    mini-module)
*/

/* 224 word INCH Buffer layout */
/* 128 word subchannel status storage (SST) */
/*  66 words of program status queue (PSQ) */
/*  26 words of scratchpad */
/*   4 words of label buffer registers */

/* track label / sector label definations */
/* 
    short lcyl;	        cylinder 
    char ltkn;			track
    char lid;			sector id
    char lflg1;         track/sector status flags
        bit 0           good
            1           alternate
            2           spare
            3           reserved
            4           flaw
            5           last track
            6           start of alternate
    char lflg2;
    short lspar1;
    short lspar2;
    short ldef1;
    int ldeallp;        DMAP block number trk0
    int lumapp;			UMAP block number sec1
    short ladef3;
    short laltcyl;
    char lalttk;        sectors per track
    char ldscnt;        number of heads
    char ldatrflg;		device attributes
        bit 0           n/u
            1           disk is mhd
            2           n/u
            3           n/u
            4           n/u
            5           dual ported
            6/7         00 768 bytes/blk
                        01 1024 bytes/blk
                        10 2048 bytes/blk
    char ldatrscnt;     sectors per track (again)
    char ldatrmhdc;     MHD head count
    char ldatrfhdc;     FHD head count
 */

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
#define DSK_INCH2       0xf0                    /* Initialize channel for processing */
#define DSK_WD          0x01                    /* Write data */
#define DSK_RD          0x02                    /* Read data */
#define DSK_NOP         0x03                    /* No operation */
#define DSK_SNS         0x04                    /* Sense */
#define DSK_SCK         0x07                    /* Seek cylinder, track, sector */
#define DSK_TIC         0x08                    /* Transfer in channel */
#define DSK_FNSK        0x0B                    /* Format for no skip */
#define DSK_LPL         0x13                    /* Lock protected label */
#define DSK_LMR         0x1F                    /* Load mode register */
#define DSK_RES         0x23                    /* Reserve */
#define DSK_WSL         0x31                    /* Write sector label */
#define DSK_RSL         0x32                    /* Read sector label */
#define DSK_REL         0x33                    /* Release */
#define DSK_XEZ         0x37                    /* Rezero */
#define DSK_POR         0x43                    /* Priority Override */
#define DSK_IHA         0x47                    /* Increment head address */
#define DSK_SRM         0x4F                    /* Set reserve track mode */
#define DSK_WTL         0x51                    /* Write track label */
#define DSK_RTL         0x52                    /* Read track label */
#define DSK_XRM         0x5F                    /* Reset reserve track mode */
#define DSK_RAP         0xA2                    /* Read angular positions */
#define DSK_TESS        0xAB                    /* Test STAR (subchannel target address register) */
#define DSK_ICH         0xFF                    /* Initialize Controller */

#define STAR    u4
/* u4 - sector target address register (STAR) */
/* Holds the current cylinder, head(track), sector */
#define DISK_CYL        0xFFFF0000              /* cylinder mask */
#define DISK_TRACK      0x0000FF00              /* track mask */
#define DISK_SECTOR     0x000000ff              /* sector mask */

#define SNS     u5
/* u5 */
/* Sense byte 0  - mode register */
#define SNS_DROFF       0x80000000              /* Drive Carriage will be offset */
#define SNS_TRKOFF      0x40000000              /* Track offset: 0=positive, 1=negative */
#define SNS_RDTMOFF     0x20000000              /* Read timing offset = 1 */
#define SNS_RDSTRBT     0x10000000              /* Read strobe timing: 1=positive, 0=negative */
#define SNS_DIAGMOD     0x08000000              /* Diagnostic Mode ECC Code generation and checking */
#define SNS_RSVTRK      0x04000000              /* Reserve Track mode: 1=OK to write, 0=read only */
#define SNS_FHDOPT      0x02000000              /* FHD or FHD option = 1 */
#define SNS_RESERV      0x01000000              /* Reserved */

/* Sense byte 1 */
#define SNS_CMDREJ      0x800000                /* Command reject */
#define SNS_INTVENT     0x400000                /* Unit intervention required */
#define SNS_SPARE1      0x200000                /* Spare */
#define SNS_EQUCHK      0x100000                /* Equipment check */
#define SNS_DATCHK      0x080000                /* Data Check */
#define SNS_OVRRUN      0x040000                /* Data overrun/underrun */
#define SNS_DSKFERR     0x020000                /* Disk format error */
#define SNS_DEFTRK      0x010000                /* Defective track encountered */

/* Sense byte 2 */
#define SNS_LAST        0x8000                  /* Last track flag encountered */
#define SNS_AATT        0x4000                  /* At Alternate track */
#define SNS_WPER        0x2000                  /* Write protection error */
#define SNS_WRL         0x1000                  /* Write lock error */
#define SNS_MOCK        0x0800                  /* Mode check */
#define SNS_INAD        0x0400                  /* Invalid memory address */
#define SNS_RELF        0x0200                  /* Release fault */
#define SNS_CHER        0x0100                  /* Chaining error */

/* Sense byte 3 */
#define SNS_REVL        0x80                    /* Revolution lost */
#define SNS_DADE        0x40                    /* Disc addressing or seek error */
#define SNS_BUCK        0x20                    /* Buffer check */
#define SNS_ECCS        0x10                    /* ECC error in sector label */
#define SNS_ECCD        0x08                    /* ECC error iin data */
#define SNS_ECCT        0x04                    /* ECC error in track label */
#define SNS_RTAE        0x02                    /* Reserve track access error */
#define SNS_UESS        0x01                    /* Uncorrectable ECC error */

#define CHS     u6
/* u6 */
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

#ifdef  NOUSED
/* registers */
uint32  star[NUM_UNITS_DISK];
uint32  attr[NUM_UNITS_DISK];
#endif

/* disk definition structure */
struct disk_t
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

disk_type[] =
{
    /* Class F Disc Devices */
    /* For MPX */
    {"MH040",   5, 192, 20, 400, 411, 0x40},   /* 0  411   40M XXXX */
    {"MH080",   5, 192, 20, 800, 823, 0x40},   /* 1  823   80M 8138 */
    {"MH160",  10, 192, 20, 800, 823, 0x40},   /* 2  823  160M 8148 */
    {"MH300",  19, 192, 20, 800, 823, 0x40},   /* 3  823  300M 8127 */
    {"MH600",  40, 192, 20, 800, 843, 0x40},   /* 4  843  600M 8155 */
    /* For UTX */
    {"9342",    5, 256, 16, 819, 823, 0x40},   /* 5  823    80M XXXX */
    {"8148",   10, 256, 16, 819, 823, 0x40},   /* 6  823  160M 8148 */
    {"9346",   19, 256, 16, 819, 823, 0x40},   /* 7  823   300M */
    {"8858",   24, 256, 16, 707, 711, 0x40},   /* 8  711   340M */
    {"8887",   10, 256, 35, 819, 823, 0x40},   /* 9  823   340M */
    {"8155",   40, 256, 16, 839, 843, 0x40},   /* 10 843   675M */
    {NULL, 0}
};

uint8   disk_preio(UNIT *uptr, uint16 chan) ;
uint8   disk_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) ;
uint8   disk_haltio(uint16 addr);
t_stat  disk_srv(UNIT *);
t_stat  disk_boot(int32, DEVICE *);
void    disk_ini(UNIT *, t_bool);
t_stat  disk_reset(DEVICE *);
t_stat  disk_attach(UNIT *, CONST char *);
t_stat  disk_detach(UNIT *);
t_stat  disk_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat  disk_get_type(FILE *st, UNIT *uptr, int32 v, CONST void *desc);
t_stat  disk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const   char  *disk_description (DEVICE *dptr);

/* channel program information */
CHANP           dda_chp[NUM_UNITS_DISK] = {0};

MTAB            disk_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "TYPE", "TYPE",
    &disk_set_type, &disk_get_type, NULL, "Type of disk"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL, "Device channel address"},
    {0},
};

UNIT            dda_unit[] = {
/* SET_TYPE(9) DM300 old */
/* SET_TYPE(3) DM300 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0x800)},  /* 0 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0x801)},  /* 1 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0x802)},  /* 2 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0x803)},  /* 3 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0x804)},  /* 4 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0x805)},  /* 5 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0x806)},  /* 6 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0x807)},  /* 7 */
};

#ifdef  NOUSED
REG             dda_reg = {
    (BRDATAD(STAR, astar, 16, 32, NUM_UNITS_DISK, "Current STAR position"), REG_FIT,
    (BRDATAD(ATTR, aattr, 16, 32, NUM_UNITS_DISK, "Disk Attributes"), RET_FIT,
    {NULL}
};
#endif

DIB             dda_dib = {
    disk_preio,                                 /* Pre start I/O */
    disk_startcmd,                              /* Start a command */
    NULL,                                       /* Stop I/O */
    NULL,                                       /* Test I/O */
    NULL,                                       /* Post I/O */
    disk_ini,                                   /* init function */
    dda_unit,                                   /* Pointer to units structure */
    dda_chp,                                    /* Pointer to chan_prg structure */
    NUM_UNITS_DISK,                             /* number of units defined */
    0x07,                                       /* 8 devices - device mask */
    0x0800,                                     /* parent channel address */
    0,                                          /* fifo input index */
    0,                                          /* fifo output index */
    {0},                                        /* interrupt status fifo for channel */
};

DEVICE          dda_dev = {
    "DMA", dda_unit, NULL/*dda_reg*/, disk_mod,
    NUM_UNITS_DISK, 16, 24, 4, 16, 32,
    NULL, NULL, &disk_reset, &disk_boot, &disk_attach, &disk_detach,
    &dda_dib, DEV_DISABLE|DEV_DEBUG|DEV_DIS, 0, dev_debug,
    NULL, NULL, &disk_help, NULL, NULL, &disk_description
};

#if NUM_DEVS_DISK > 1
/* channel program information */
CHANP           ddb_chp[NUM_UNITS_DISK] = {0};

UNIT            ddb_unit[] = {
/* SET_TYPE(9) DM300 old */
/* SET_TYPE(3) DM300 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC00)},  /* 0 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC01)},  /* 1 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC02)},  /* 2 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC03)},  /* 3 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC04)},  /* 4 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC05)},  /* 5 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC06)},  /* 6 */
    {UDATA(&disk_srv, UNIT_DISK|SET_TYPE(3), 0), 0, UNIT_ADDR(0xC07)},  /* 7 */
};

#ifdef  NOUSED
REG             ddb_reg = {
    (BRDATAD(STAR, bstar, 16, 32, NUM_UNITS_DISK, "Current STAR position"), REG_FIT,
    (BRDATAD(ATTR, battr, 16, 32, NUM_UNITS_DISK, "Disk Attributes"), RET_FIT,
    {NULL}
};
#endif

DIB             ddb_dib = {
    disk_preio,                                 /* Pre Start I/O */
    disk_startcmd,                              /* Start a command SIO */
    NULL,                                       /* Stop I/O HIO */
    NULL,                                       /* Test I/O TIO */
    NULL,                                       /* Post I/O */
    disk_ini,                                   /* init function */
    ddb_unit,                                   /* Pointer to units structure */
    ddb_chp,                                    /* Pointer to chan_prg structure */
    NUM_UNITS_DISK,                             /* number of units defined */
    0x07,                                       /* 8 devices - device mask */
    0x0C00,                                     /* parent channel address */
    0,                                          /* fifo input index */
    0,                                          /* fifo output index */
    {0},                                        /* interrupt status fifo for channel */
};

DEVICE          ddb_dev = {
    "DMB", ddb_unit, NULL, /*ddb_reg*/, disk_mod,
    NUM_UNITS_DISK, 16, 24, 4, 16, 32,
    NULL, NULL, &disk_reset, &disk_boot, &disk_attach, &disk_detach,
    &ddb_dib, DEV_DISABLE|DEV_DEBUG|DEV_DIS, 0, dev_debug,
    NULL, NULL, &disk_help, NULL, NULL, &disk_description
};
#endif

/* convert sector disk address to star values (c,h,s) */
uint32 disksec2star(uint32 daddr, int type)
{
    int32 sec = daddr % disk_type[type].spt;    /* get sector value */
    int32 spc = disk_type[type].nhds * disk_type[type].spt; /* sec per cyl */
    int32 cyl = daddr / spc;                    /* cylinders */
    int32 hds = (daddr % spc) / disk_type[type].spt;    /* heads */ 

    /* now return the star value */
    return (CHS2STAR(cyl,hds,sec));             /* return STAR */
}

/* start a disk operation */
uint8  disk_preio(UNIT *uptr, uint16 chan)
{
    DEVICE      *dptr = get_dev(uptr);
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         unit = (uptr - dptr->units);

    sim_debug(DEBUG_CMD, dptr, "disk_preio CMD %08x unit %02x\n", uptr->CMD, unit);
    if ((uptr->CMD & 0xff00) != 0) {            /* just return if busy */
        return SNS_BSY;
    }

    sim_debug(DEBUG_CMD, dptr, "disk_preio unit %02x chsa %04x OK\n", unit, chsa);
    return 0;                                   /* good to go */
}

uint8  disk_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd)
{
    uint16      chsa = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int         unit = (uptr - dptr->units);
    CHANP       *chp = find_chanp_ptr(chsa);    /* find the chanp pointer */

    sim_debug(DEBUG_CMD, dptr,
        "disk_startcmd chsa %04x unit %02x cmd %02x CMD %08x\n",
        chsa, unit, cmd, uptr->CMD);
    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        if (cmd != DSK_SNS)                     /* we are completed with unit check status */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    if ((uptr->CMD & DSK_CMDMSK) != 0) {
        uptr->CMD |= DSK_BUSY;                  /* Flag we are busy */
        return SNS_BSY;
    }
    if ((uptr->CMD & 0xff00) != 0) {            /* if any status info, we are busy */
        return SNS_BSY;
    }
    sim_debug(DEBUG_CMD, dptr, "disk_startcmd CMD continue unit=%02x cmd %02x\n", unit, cmd);

    /* Unit is online, so process a command */
    switch (cmd) {

    case DSK_INCH:                              /* INCH cmd 0x0 */
        sim_debug(DEBUG_CMD, dptr,
            "disk_startcmd starting INCH %06x cmd, chsa %04x MemBuf %06x cnt %04x\n",
            uptr->u4, chsa, chp->ccw_addr, chp->ccw_count);

        uptr->CMD |= DSK_INCH2;                 /* use 0xf0 for inch, just need int */
        sim_activate(uptr, 20);                 /* start things off */
        return 0;
        break;

    case DSK_SCK:                               /* Seek command 0x07 */
    case DSK_XEZ:                               /* Rezero & Read IPL record 0x1f */
    case DSK_WD:                                /* Write command 0x01 */
    case DSK_RD:                                /* Read command 0x02 */
    case DSK_LMR:                               /* read mode register */

        uptr->CMD |= cmd;                       /* save cmd */
        sim_debug(DEBUG_CMD, dptr,
            "disk_startcmd starting disk seek r/w cmd %02x chsa %04x\n",
            cmd, chsa);
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
        "disk_startcmd done with disk_startcmd %02x chsa %04x SNS %08x\n",
        cmd, chsa, uptr->SNS);
    if (uptr->SNS & 0xff)                       /* any other cmd is error */
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    sim_activate(uptr, 20);                     /* start things off */
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle processing of disk requests. */
t_stat disk_srv(UNIT *uptr)
{
    uint16          chsa = GET_UADDR(uptr->CMD);
    DEVICE          *dptr = get_dev(uptr);
    DIB             *dibp = (DIB *)dptr->ctxt;          /* get DIB address */
    CHANP           *chp = (CHANP *)dibp->chan_prg;     /* get pointer to channel program */
    int             cmd = uptr->CMD & DSK_CMDMSK;
    int             type = GET_TYPE(uptr->flags);
    uint32          trk, cyl, sec;
    int             unit = (uptr - dptr->units);
    int             len;
    int             i;
    uint8           ch;
    uint16          ssize = disk_type[type].ssiz * 4;   /* disk sector size in bytes */
    uint32          tstart;
    uint8           buf2[1024];
    uint8           buf[1024];

    sim_debug(DEBUG_CMD, dptr,
        "disk_srv entry unit %02x CMD %08x chsa %04x count %04x %x/%x/%x \n",
        unit, uptr->CMD, chsa, chp->ccw_count,
        STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));

    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        if (cmd != DSK_SNS)                     /* we are completed with unit check status */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    sim_debug(DEBUG_CMD, dptr,
        "disk_srv cmd=%02x chsa %04x count %04x\n", cmd, chsa, chp->ccw_count);

    switch (cmd) {
    case 0:                                     /* No command, stop disk */
        break;

    case DSK_INCH2:                             /* used 0xff for inch, just need int */
    {
        uint32  mema;                           /* memory address */
//      uint32  daws[8];                        /* drive attribute registers */
//      uint32  i, j;
        uint32  i;

        len = chp->ccw_count;                   /* INCH command count */
        mema = chp->ccw_addr;                   /* get inch or buffer addr */
        sim_debug(DEBUG_CMD, dptr,
            "disk_srv starting INCH cmd, chsa %04x MemBuf %06x cnt %04x\n",
            chsa, chp->ccw_addr, chp->ccw_count);

        /* mema has IOCD word 1 contents.  For the disk processor it contains */
        /* a pointer to the INCH buffer followed by 8 drive attribute words that */
        /* contains the flags, sector count, MHD head count, and FHD count */
        /* len has the byte count from IOCD wd2 and should be 0x24 (36) */
        /* the INCH buffer address must be set for the parrent channel as well */
        /* as all other devices on the channel.  Call set_inch() to do this for us */
        /* just return OK and channel software will use u4 as status buffer addr */

        len = chp->ccw_count;                   /* INCH command count */

        if (len != 36) {
                /* we have invalid count, error, bail out */
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
        }

        /* read all 36 bytes, stopping every 4 bytes to make words */
        /* the first word has the inch buffer address */
        /* the next 8 words have drive data for each unit */
        /* WARNING 8 drives must be defined for this controller */
        /* so we will not have a map fault */
//      for (i=0, j=0; i < 36; i++) {
        for (i=0; i < 36; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                /* we have error, bail out */
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
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
 //                 daws[j++] = (buf[i-3]<<24) | (buf[i-2]<<16)
 //                     | (buf[i-1]<<8) | (buf[i]);
                    /* may want to use this later */    
                    /* clear warning errors */
                    tstart = (buf[i-3]<<24) | (buf[i-2]<<16)
                        | (buf[i-1]<<8) | (buf[i]);
            }
        }
        /* now call set_inch() function to write and test inch bybber addresses */
        i = set_inch(uptr, mema);               /* new address */
#ifdef NOTYET
        if ((i == SCPE_MEM) || (i == SCPE_ARG)) {   /* any error */
            /* we have error, bail out */
            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            break;
        }
#endif
        uptr->CMD &= ~(0xffff);                 /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "disk_srv cmd INCH chsa %04x addr %06x count %04x completed\n",
            chsa, mema, chp->ccw_count);
#ifdef FIX4MPX
        chan_end(chsa, SNS_CHNEND);             /* return just channel end OK */
#else
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
#endif
    }
        break;

    case DSK_WSL:                               /* WSL 0x31 make into NOP */
        uptr->CMD &= ~(0xffff);                 /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "disk_srv cmd WSL chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);

        sim_debug(DEBUG_DETAIL, dptr, "Sector 1 label");
        /* now read sector label data */
        len = chp->ccw_count;
        for (i = 0; i < len; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                /* we have write error, bail out */
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
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
        uptr->CMD &= ~(0xffff);                 /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "disk_srv cmd WTL chsa %04x count %04x completed\n",
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
            "disk_srv cmd NOP chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_SNS: /* 0x4 */
        sim_debug(DEBUG_CMD, dptr, "disk_startcmd CMD sense\n");

        /* bytes 0,1 - Cyl entry from CHS reg */
        ch = (uptr->CHS >> 24) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "disk_srv sense CHS b0 unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->CHS >> 16) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "disk_srv sense CHS b1 unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* byte 2 - Track entry from CHS reg */
        ch = (uptr->CHS >> 8) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "disk_srv sense CHS b2 unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* byte 3 - Sector entry from CHS reg */
        ch = (uptr->CHS) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "disk_srv sense CHS b3 unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        /* bytes 4 - mode reg, byte 0 of SNS */
        ch = (uptr->SNS >> 24) & 0xff;      /* return the sense data */
        sim_debug(DEBUG_DETAIL, dptr, "disk_srv sense unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        /* bytes 5-7 - status bytes, bytes 1-3 of SNS */
        ch = (uptr->SNS >> 16) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "disk_srv sense unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->SNS >> 8) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "disk_srv sense unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = (uptr->SNS) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "disk_srv sense unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        /* bytes 8-11 - drive mode register entries from assigned disk */
        ch = disk_type[type].type & 0xff;       /* type byte */
        sim_debug(DEBUG_DETAIL, dptr, "disk_srv datr unit=%02x 1 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = disk_type[type].spt & 0xff;        /* get sectors per track */
        sim_debug(DEBUG_DETAIL, dptr, "disk_srv datr unit=%02x 2 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = disk_type[type].nhds & 0xff;       /* get # MHD heads */
        sim_debug(DEBUG_DETAIL, dptr, "disk_srv datr unit=%02x 3 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);
        ch = 0;                                 /* no FHD heads */
        sim_debug(DEBUG_DETAIL, dptr, "disk_srv datr unit=%02x 4 %02x\n",
            unit, ch);
        chan_write_byte(chsa, &ch);

        /* bytes 12 & 13 are optional, so check if read done */
        /* TODO add drive status bits here */
        if ((test_write_byte_end(chsa)) == 0) {
            /* bytes 12 & 13 contain drive related status */
            ch = 0;                             /* zero for now */
            sim_debug(DEBUG_DETAIL, dptr, "disk_srv dsr unit=%02x 1 %02x\n",
                unit, ch);
            chan_write_byte(chsa, &ch);

            ch = 0x30;                          /* drive on cylinder and ready for now */
            sim_debug(DEBUG_DETAIL, dptr, "disk_srv dsr unit=%02x 2 %02x\n",
                unit, ch);
            chan_write_byte(chsa, &ch);
        }
        uptr->CMD &= ~(0xffff);                 /* remove old status bits & cmd */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_SCK:                               /* Seek cylinder, track, sector 0x07 */
        /* If we are waiting on seek to finish, check if there yet. */
        if (uptr->CMD & DSK_SEEKING) {
            /* see if on cylinder yet */
            if (STAR2CYL(uptr->STAR) == STAR2CYL(uptr->CHS)) {
                /* we are on cylinder, seek is done */
                sim_debug(DEBUG_CMD, dptr, "disk_srv seek on cylinder unit=%02x %04x %04x\n",
                    unit, uptr->STAR >> 16, uptr->CHS >> 16);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                /* we have already seeked to the required sector */
                /* we do not need to seek again, so move on */
                chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
                sim_activate(uptr, 20);
                break;
            } else {
                /* we have wasted enough time, we are there */
#ifndef DO_SEEK_AGAIN
        /* calculate file position in bytes of requested sector */
        /* file offset in bytes */
        tstart = STAR2SEC(uptr->STAR, SPT(type), SPC(type)) * SSB(type);
        /* just reseek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
            sim_debug(DEBUG_DETAIL, dptr, "disk_srv Error on seek to %04x\n", tstart);
        }
#endif
                uptr->CHS = uptr->STAR;         /* we are there */
                sim_activate(uptr, 10);
                break;
            }
        }

        /* not seeking, so start a new seek */
        /* set buf data to current STAR values */
        cyl = STAR2CYL(uptr->CHS);              /* get current cyl */
        buf[0] = (cyl >> 8) & 0xff;             /* split cylinder */
        buf[1] = cyl & 0xff;
        buf[2] = (uptr->CHS >> 8) & 0xff;       /* get trk/head */
        buf[3] = uptr->CHS & 0xff;              /* get sec */

        sim_debug(DEBUG_CMD, dptr,
            "disk_srv current STAR unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        /* Read in 1-4 character seek code */
        for (i = 0; i < 4; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                if (i == 0) {
                    sim_debug(DEBUG_DETAIL, dptr,
                        "disk_srv seek error unit=%02x star %02x %02x %02x %02x\n",
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
        sim_debug(DEBUG_CMD, dptr,
            "disk_srv STAR unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);
rezero:
        sim_debug(DEBUG_DETAIL, dptr,
            "disk_srv seek unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        /* save STAR (target sector) data in STAR */
        uptr->STAR = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3]);
        cyl = STAR2CYL(uptr->STAR);             /* get the cylinder */
        trk = buf[2];                           /* get the track */

        sim_debug(DEBUG_DETAIL, dptr,
           "disk_srv NEW SEEK cyl %04x trk %02x sec %02x unit=%02x\n",
           cyl&0xffff, trk, buf[3], unit);

        /* Check if seek valid */
        if (cyl >= disk_type[type].cyl ||
            trk >= disk_type[type].nhds ||
            buf[3] >= disk_type[type].spt) {

            sim_debug(DEBUG_CMD, dptr,
                "disk_srv seek ERROR cyl %04x trk %02x sec %02x unit=%02x\n",
                cyl, trk, buf[3], unit);

            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK; /* set error status */

            /* we have an error, tell user */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);  /* end command */
            break;
        }

        /* calc the new sector address of data */
        /* calculate file position in bytes of requested sector */
        /* file offseet in bytes */
        tstart = STAR2SEC(uptr->STAR, SPT(type), SPC(type)) * SSB(type);
        /* set new STAR value using old cyl value */
        uptr->CHS = CHS2STAR(STAR2CYL(uptr->CHS), trk, buf[3]);

        sim_debug(DEBUG_DETAIL, dptr,
            "disk_srv seek start %04x cyl %04x trk %02x sec %02x\n",
            tstart, cyl, trk, buf[3]);

        /* just seek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* do seek */
            sim_debug(DEBUG_DETAIL, dptr, "disk_srv Error on seek to %04x\n", tstart);
        }

        /* Check if already on correct cylinder */
        /* if not, do a delay to slow things down */
        if (STAR2CYL(uptr->STAR) != STAR2CYL(uptr->CHS)) {
            /* Do a fake seek to kill time */
            uptr->CMD |= DSK_SEEKING;           /* show we are seeking */
            sim_debug(DEBUG_DETAIL, dptr,
                "disk_srv seeking unit=%02x to cyl %04x trk %02x sec %02x\n",
                unit, cyl, trk, buf[3]);
            sim_activate(uptr, 20);             /* start us off */
        } else {
            /* we are on cylinder/track/sector, so go on */
            sim_debug(DEBUG_DETAIL, dptr,
                "disk_srv done seeking to %04x cyl %04x trk %02x sec %02x\n",
                tstart, cyl, trk, buf[3]);
            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
            chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
        }
        return SCPE_OK;

    case DSK_XEZ:                               /* Rezero & Read IPL record */

        sim_debug(DEBUG_CMD, dptr, "RD REZERO IPL unit=%02x seek 0\n", unit);
        /* Do a seek to 0 */
        uptr->STAR = 0;                         /* set STAR to 0, 0, 0 */
        uptr->CHS = 0;                          /* set current CHS to 0, 0, 0 */
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
                "DISK READ starting CMD %08x chsa %04x buffer %06x count %04x\n",
                uptr->CMD, chsa, chp->ccw_addr, chp->ccw_count);
        }

        if (uptr->CMD & DSK_READING) {          /* see if we are reading data */
            cyl = STAR2CYL(uptr->CHS);          /* get current cyl */
            trk = (uptr->CHS >> 8) & 0xff;      /* get trk/head */
            sec = uptr->CHS & 0xff;             /* get sec */
            /* get sector offset */
//          tstart = STAR2SEC(uptr->STAR, SPT(type), SPC(type));
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));

            /* read in a sector of data from disk */
            if ((len=sim_fread(buf, 1, ssize, uptr->fileref)) != ssize) {
                sim_debug(DEBUG_CMD, dptr,
                    "Error %08x on read %04x of diskfile cyl %04x hds %02x sec %02x\n",
                    len, ssize, cyl, trk, sec);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            sim_debug(DEBUG_CMD, dptr,
                "disk_srv after READ chsa %04x buffer %06x count %04x\n",
                chsa, chp->ccw_addr, chp->ccw_count);

            /* process the next sector of data */
            for (i=0; i<len; i++) {
                ch = buf[i];                    /* get a char from buffer */
                if (chan_write_byte(chsa, &ch)) {   /* put a byte to memory */
                    sim_debug(DEBUG_DATA, dptr,
                        "DISK Read %04x bytes leaving %04x from diskfile /%04x/%02x/%02x\n",
                        i, chp->ccw_count, cyl, trk, sec);
                    uptr->CMD &= ~(0xffff);     /* remove old status bits & cmd */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                    goto rddone;
                }
            }

            sim_debug(DEBUG_CMD, dptr,
                "DISK READ %04x bytes leaving %4x to be read to %06x from diskfile /%04x/%02x/%02x\n",
                ssize, chp->ccw_count, chp->ccw_addr+4, cyl, trk, sec);

            /* see if we are done reading data */
            if (test_write_byte_end(chsa)) {
                sim_debug(DEBUG_DATA, dptr,
                    "DISK Read complete for read from diskfile /%04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                break;
            }

            /* tstart has file offset in sectors */
            tstart++;                           /* bump to next sector */
            /* convert sect back to chs value */
            uptr->CHS = disksec2star(tstart, type);
            /* see of over end of disk */
//          if (tstart >= CAPB(type)) {
            if (tstart >= (uint32)CAP(type)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_CMD, dptr,
                    "DISK Read reached EOM for read from disk @ /%04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                uptr->CHS = 0;                  /* reset cylinder position */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            sim_debug(DEBUG_DATA, dptr,
                "DISK sector read complete, %x bytes to go from diskfile /%04x/%02x/%02x\n",
                chp->ccw_count, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
            sim_activate(uptr, 10);         /* wait to read next sector */
            break;
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
            cyl = STAR2CYL(uptr->CHS);          /* get current cyl */
            trk = (uptr->CHS >> 8) & 0xff;      /* get trk/head */
            sec = uptr->CHS & 0xff;             /* get sec */
            /* get sector offset */
//          tstart = STAR2SEC(uptr->STAR, SPT(type), SPC(type));
            tstart = STAR2SEC(uptr->CHS, SPT(type), SPC(type));

            /* process the next sector of data */
            len = 0;                            /* used here as a flag for short read */
            for (i=0; i<ssize; i++) {
                if (chan_read_byte(chsa, &ch)) {    /* get a byte from memory */
                    /* if error on reading 1st byte, we are done writing */
                    if (i == 0) {
                        uptr->CMD &= ~(0xffff);  /* remove old status bits & cmd */
                        sim_debug(DEBUG_CMD, dptr,
                            "DISK Wrote %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                            ssize, cyl, trk, sec);
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
                    i, ssize, cyl, trk, sec);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (len != 0) {                     /* see if done with write command */
                sim_debug(DEBUG_DATA, dptr,
                    "DISK WroteB %04x bytes to diskfile cyl %04x hds %02x sec %02x\n",
                    ssize, cyl, trk, sec);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                break;
            }
            sim_debug(DEBUG_CMD, dptr,
                "DISK WR to sec end %04x bytes end %04x to diskfile cyl %04x hds %02x sec %02x\n",
                len, ssize, cyl, trk, sec);

            /* tstart has file offset in sectors */
            tstart++;                           /* bump to next sector */
            /* convert sect back to chs value */
            uptr->CHS = disksec2star(tstart, type);
            /* see if over end of disk */
//          if (tstart >= CAPB(type)) {
            if (tstart >= (uint32)CAP(type)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_CMD, dptr,
                    "DISK Write reached EOM for write to disk @ /%04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                uptr->CHS = 0;                  /* reset cylinder position */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            sim_activate(uptr, 10);             /* keep writing */
            break;
wrdone:
            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
         }
         break;

    case DSK_RSL:                               /* RSL 0x32 */
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
        /* The UTX flaw map is at DMAP = MDT-SPT CYL-4/HDS-3/0 */
        /* UTX media map is 1 track lower at UMAP=DMAP-SPT CYL-4/HDS-4/0 */
        /* The UTX media map is pointed to by sector label 1 */
        /* simulate pointers here, set wd[3] in label to UMAP */

        /* VDT  249264 (819/18/0) 0x3cdb0 for 9346 - 823/19/16 */ 
        /* MDT  249248 (819/17/0) 0x3cda0 for 9346 - 823/19/16 */ 
        /* DMAP 249232 (819/16/0) 0x3cd90 for 9346 - 823/19/16 */ 
        /* UMAP 249216 (819/15/0) 0x3cd80 for 9346 - 823/19/16 */ 

        sim_debug(DEBUG_CMD, dptr, "disk_startcmd RSL STAR %08x disk geom %08x\n",
            uptr->CHS, GEOM(type));

        /* set buf data to current STAR values */
        cyl = STAR2CYL(uptr->CHS);              /* get current cyl */
        buf[0] = (cyl >> 8) & 0xff;             /* split cylinder */
        buf[1] = cyl & 0xff;
        buf[2] = (uptr->CHS >> 8) & 0xff;       /* get trk/head */
        buf[3] = uptr->CHS & 0xff;              /* get sec */
        buf[4] = 0x80;                          /* set lflg1 to show good sector */

        sim_debug(DEBUG_DETAIL, dptr,
            "disk_srv RSL unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        /* get sector address of UTX media descriptor */
        /* 819/6/0 is right for 8887, 819/15/0 for 9346  */
        /* UMAP 249216 (819/15/0) 0x3cd80 for 9346 - 823/19/16 */ 
        tstart = ((CYL(type)-4) * SPC(type)) +
            ((HDS(type)-4) * SPT(type)) - SPT(type);
//WAS   tstart = ((CYL(type)-4) * SPC(type)) +
//WAS       ((HDS(type)-2) * SPT(type)) - SPT(type);

        sim_debug(DEBUG_CMD, dptr,
            "disk_srv SL1 RSL sector %d %x star %02x %02x %02x %02x\n",
            tstart, tstart, buf[0], buf[1], buf[2], buf[3]);

        /* on HSDP DMAP is in wd 3 on label 0 */
        /* on HSDP UMAP is in wd 4 on label 1 */
        /* on UDP & DPII DMAP is in wd 3 on label 0 */
        /* on UDP & DPII UMAP is in wd 4 on label 0 */
//WAS   tstart = 0x3cd90;                       /* 819/16/0 physical 249232 */
        /* UMAP 249216 (819/15/0) 0x3cd80 for 9346 - 823/19/16 */ 
        /* the address must be physical for UDP */
        /* store into sec 1 label */
        buf[16] = (tstart >> 24) & 0xff;        /* UMAP pointer */
        buf[17] = (tstart >> 16) & 0xff;
        buf[18] = (tstart >> 8) & 0xff;
        buf[19] = (tstart) & 0xff;

        /* the tech doc shows the cyl/trk/sec data is in the first 4 bytes */
        /* of the track label, BUT it is really in the configuration data */
        /* area are too.  That is where UTX looks.  Byte 27 is sectors/track */
        /* and byte 28 is number of heads. Byte 25 is copy of byte 27. */
        buf[25] = disk_type[type].spt & 0xff;   /* sect per track 35 */

        /* UTX looks at bit 6 & 7 to determine if UDP or HSDP controller */
        /* The UDP/DPII controllers do not use these bits, so UTX keys */
        /* on these bits to determine type of controller.  Bit 31 is set */
        /* for a HSDP and not set for the UDP/DPII */
        buf[26] = disk_type[type].type & 0xfc;  /* sector size bits not used in UDP/DPII */
        buf[27] = disk_type[type].spt & 0xff;   /* sec per track 35 */
        buf[28] = disk_type[type].nhds & 0xff;  /* num heads 10 */

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

        /* zero the Track Label Buffer */
        for (i = 0; i < 30; i++)
            buf[i] = 0;

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

        sim_debug(DEBUG_CMD, dptr, "disk_startcmd RTL STAR %08x disk geom %08x\n",
            uptr->CHS, GEOM(type));

        /* set buf data to current STAR values */
        buf[0] = (cyl >> 8) & 0xff;             /* lcyl  cyl upper 8 bits */
        buf[1] = cyl & 0xff;                    /* lcyl  cyl lower 8 bits */
        buf[2] = trk & 0xff;                    /* ltkn  trk */
        buf[3] = sec & 0xff;                    /* lid   sector ID */
        buf[4] = 0x80;                          /* show good sector */

        sim_debug(DEBUG_DETAIL, dptr,
            "disk_srv RTL unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        /* the dmap pointer is placed by the vendor or diag into the */
        /* track zero label in word 3 of the 30 byte label. */
        /* The disk address is the last track of the user area.  The vendor */
        /* reserves the last cylinder, SEL diags reserve the next two, so the */
        /* addr is CYL-4/HDS-1/0 and is VDT.  The UTX/MPX media table is on */
        /* previous track, so MDT = VDT-SPT is CYL-4/HDS-2/0 */
        /* The UTX flaw map is at DMAP = MDT-SPT CYL-4/HDS-3/0 */
        /* UTX media map is 1 track lower at UMAP=FMAP-SPT CYL-4/HDS-4/0 */
        /* The UTX media map is pointed to by sector label 1 */
        /* simulate pointers here, set wd[3] in label to VDT */

        /* get sector address of media defect table */
        /* VDT  249264 (819/18/0) 0x3cdb0 for 9346 - 823/19/16 */ 
        /* MDT  249248 (819/17/0) 0x3cda0 for 9346 - 823/19/16 */ 
        /* DMAP 249232 (819/16/0) 0x3cd90 for 9346 - 823/19/16 */ 
        /* UMAP 249216 (819/15/0) 0x3cd80 for 9346 - 823/19/16 */ 
        tstart = (CYL(type)-4) * SPC(type) + (HDS(type)-2) * SPT(type);

        cyl = disk_type[type].cyl-1;            /* last cyl */
        trk = disk_type[type].nhds-1;           /* last head number */
//      sec = disk_type[type].spt-1;            /* last sector number */

        sim_debug(DEBUG_CMD, dptr,
            "disk_srv RTL STAR %04x/%02x/%02x Dmap pointer 0x%08x %d\n",
            cyl-3, trk-1, 0, tstart, tstart);
        sim_debug(DEBUG_CMD, dptr,
            "disk_srv TRK0 RTL sector %d %x star %02x %02x %02x %02x\n",
            tstart, tstart, buf[0], buf[1], buf[2], buf[3]);

        if (uptr->CHS == 0) {                   /* only write dmap address in trk 0 */
            /* output last sector address of disk */
            buf[12] = (tstart >> 24) & 0xff;    /* DMAP pointer */
            buf[13] = (tstart >> 16) & 0xff;
            buf[14] = (tstart >> 8) & 0xff;
            buf[15] = (tstart) & 0xff;
        }

        /* get sector address of umap table */
        /* UMAP 249216 (819/15/0) 0x3cd80 for 9346 - 823/19/16 */ 
//WAS   tstart -= SPT(type);                    /* calc umap address */
        tstart -= (2*SPT(type));                /* calc umap address */
        /* the address must be physical for UDP */
        if (uptr->CHS == 0) {                   /* only write umap address on trk 0 */
            buf[16] = (tstart >> 24) & 0xff;    /* ldeallp UMAP */
            buf[17] = (tstart >> 16) & 0xff;
            buf[18] = (tstart >> 8) & 0xff;
            buf[19] = (tstart) & 0xff;
        }

        /* the tech doc shows the cyl/trk/sec data is in the first 4 bytes */
        /* of the track label, BUT it is really in the configuration data */
        /* area too.  That is where UTX looks.  Byte 27 is sectors/track */
        /* and byte 28 is number of heads. Byte 26 is mode. */
        /* Byte 25 is copy of byte 27. */
        buf[25] = disk_type[type].spt & 0xff;
        buf[26] = disk_type[type].type;         /* mode data in UDP/DPII */
        buf[27] = disk_type[type].spt & 0xff;
        buf[28] = disk_type[type].nhds & 0xff;

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
        sim_debug(DEBUG_CMD, dptr, "disk_srv cmd RTL done chsa %04x count %04x completed\n",
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
    sim_debug(DEBUG_DETAIL, dptr,
        "disk_srv done cmd=%02x chsa %04x count %04x\n", cmd, chsa, chp->ccw_count);
    return SCPE_OK;
}

/* initialize the disk */
void disk_ini(UNIT *uptr, t_bool f)
{
    DEVICE  *dptr = get_dev(uptr);
    int     i = GET_TYPE(uptr->flags);

    uptr->CMD &= ~0x7fff;                       /* clear out the flags but leave ch/sa */
    uptr->SNS = ((uptr->SNS & 0x00ffffff) | (disk_type[i].type << 24));  /* save mode value */
    /* total sectors on disk */
    uptr->capac = CAP(i);                       /* size in sectors */

    sim_debug(DEBUG_EXP, &dda_dev, "DMA init device %s on unit DMA%.1x cap %x %d\n",
        dptr->name, GET_UADDR(uptr->CMD), uptr->capac, uptr->capac);
}

t_stat disk_reset(DEVICE *dptr)
{
    /* add reset code here */
    return SCPE_OK;
}

/* create the disk file for the specified device */
int disk_format(UNIT *uptr) {
    int         type = GET_TYPE(uptr->flags);
    DEVICE      *dptr = get_dev(uptr);
    uint32      ssize = disk_type[type].ssiz * 4;       /* disk sector size in bytes */
    uint32      tsize = disk_type[type].spt;            /* get track size in sectors */
    uint32      csize = disk_type[type].nhds * tsize;   /* get cylinder size in sectors */
    uint32      cyl = disk_type[type].cyl;              /* get # cylinders */
//  uint32      spc = disk_type[type].nhds * disk_type[type].spt;   /* sectors/cyl */
    uint32      cap = disk_type[type].cyl * csize;      /* disk capacity in sectors */
    uint32      cylv = cyl;                             /* number of cylinders */
    uint8       *buff;
                /* last sector address of disk (cyl * hds * spt) - 1 */
    uint32      laddr = CAP(type) - 1;                  /* last sector of disk */
                /* get sector address of vendor defect table */
                /* put data = 0xf0000000 0xf4000004 */
    int32       vaddr = (CYL(type)-4) * SPC(type) + (HDS(type)-1) * SPT(type);
                /* get sector address of utx diag map track 0 pointer */
                /* put data = 0xf0000000 + (cyl-1), 0x8a000000 + daddr, */
                /* 0x9a000000 + (cyl-1), 0xf4000008 */
    int32       daddr = vaddr - SPT(type);
                /* get sector address of utx flaw map sec 1 pointer */
                /* use this address for sec 1 label pointer */
//WASint32       uaddr = daddr - SPT(type);
    int32       uaddr = daddr - (2*SPT(type));
                /* last block available */
    int32       luaddr = (CYL(type)-4) * SPC(type);
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
                    0x4e554d50,(cap-1),luaddr-1,0,0,0,0,0xe10,
                    0,0x5320,0,0x4e60,0x46,luaddr,0,0xd360,
                    0x88,0x186b0,0x13a,0xd100,0x283,0,0,0,
                    0,0x22c2813e,0,0x06020000,0xf4,0,0x431b1c,0,
                    /* try to makeup a utx dmap */
//                  0xf003d14f,0x8a03cda0,0x9a03cdbf,0x8903cdc0,
//                  0x9903d01f,0x8c03d020,0x9c03d14f,0xf4000000,
///                 0xf0000000 | (cap-1), 0x8a000000 | daddr,
///                     0x9a000000 | (daddr + ((2 * tsize) - 1)),
///                     0x89000000 | (daddr + (2 * tsize)),
///                     0x99000000 | ((cap-1)-spc),
///                     0x8c000000 | (cap-spc),
///                     0x9c000000 | (cap-1), 0xf4000000,
                };
#endif
                /*            250191     249248      250191     0   */
//  uint32      dmap[4] = {0xf003d14f, 0x8a03cda0, 0x9a03d14f, 0xf4000000};
    int         i;
                /* vendor flaw map */
    uint32      vmap[2] = {0xf0000004, 0xf4000000};
    uint32      dmap[4] = {0xf0000000 | (cap-1), 0x8a000000 | daddr,
                    0x9a000000 | (cap-1), 0xf4000008};
//TRY               0x9a000000 | (cap-1), 0xf4000000};

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
    for (i=0; i<256; i++) {
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
    sim_fseek(uptr->fileref, (vaddr)*ssize, SEEK_SET);  /* seek VMAP */
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

    sim_fseek(uptr->fileref, (uaddr)*ssize, SEEK_SET);   /* seek UMAP */
    if ((sim_fwrite((char *)&umap, sizeof(uint32), 256, uptr->fileref)) != 256) {
        sim_debug(DEBUG_CMD, dptr,
        "Error on umap write to diskfile sect %06x\n", uaddr * ssize);
    }

    printf("writing to vmap sec %x (%d) bytes %x (%d)\n",
        vaddr, vaddr, (vaddr)*ssize, (vaddr)*ssize);
    printf("writing to umap sec %x (%d) bytes %x (%d)\n",
        uaddr, uaddr, (uaddr)*ssize, (uaddr)*ssize);
    printf("writing dmap to %x %d %x %d dmap to %x %d %x %d\n",
       cap-1, cap-1, (cap-1)*ssize, (cap-1)*ssize,
       daddr, daddr, daddr*ssize, daddr*ssize);

    /* seek home again */
    sim_fseek(uptr->fileref, 0, SEEK_SET);      /* seek home */
    free(buff);                                 /* free cylinder buffer */
    return 0;
}

/* attach the selected file to the disk */
t_stat disk_attach(UNIT *uptr, CONST char *file)
{
    uint16          chsa = GET_UADDR(uptr->CMD);
    int             type = GET_TYPE(uptr->flags);
    DEVICE          *dptr = get_dev(uptr);
    t_stat          r;
    uint32          ssize;                      /* sector size in bytes */
    uint8           buff[1024];

    /* see if valid disk entry */
    if (disk_type[type].name == 0) {            /* does the assigned disk have a name */
        detach_unit(uptr);                      /* no, reject */
        return SCPE_FMT;                        /* error */
    }

    /* have simulator attach the file to the unit */
    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;

    uptr->capac = CAP(type);                    /* disk capacity in sectors */
    ssize = SSB(type);                          /* get sector size in bytes */

    sim_debug(DEBUG_CMD, dptr, "Disk %s cyl %d hds %d sec %d ssiz %d capacity %d\n",
        disk_type[type].name, disk_type[type].cyl, disk_type[type].nhds, 
        disk_type[type].spt, ssize, uptr->capac); /* disk capacity */

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
        if (disk_format(uptr)) {
            detach_unit(uptr);                  /* if no space, error */
            return SCPE_FMT;                    /* error */
        }
    }

    /* there is data on sector 0, so already formatted */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        detach_unit(uptr);                      /* detach if error */
        return SCPE_FMT;                        /* error */
    }

    /* start out at sector 0 */
    uptr->CHS = 0;                              /* set CHS to cyl/hd/sec = 0 */

    sim_debug(DEBUG_CMD, dptr,
        "Attach %s cyl %d hds %d spt %d spc %d cap sec %d cap bytes %d\n",
        disk_type[type].name, CYL(type), HDS(type), SPT(type), SPC(type),  
        CAP(type), CAPB(type));

    sim_debug(DEBUG_CMD, dptr, "File %s attached to %s\r\n",
        file, disk_type[type].name);

    set_devattn(chsa, SNS_DEVEND);
    return SCPE_OK;
}

/* detach a disk device */
t_stat disk_detach(UNIT *uptr) {
    uptr->SNS = 0;                              /* clear sense data */
    uptr->CMD &= ~0xffff;                       /* no cmd and flags */
    return detach_unit(uptr);                   /* tell simh we are done with disk */
}

/* boot from the specified disk unit */
t_stat disk_boot(int32 unit_num, DEVICE *dptr) {
    UNIT    *uptr = &dptr->units[unit_num];     /* find disk unit number */

    sim_debug(DEBUG_CMD, dptr, "Disk Boot dev/unit %x\n", GET_UADDR(uptr->CMD));
    SPAD[0xf4] = GET_UADDR(uptr->CMD);          /* put boot device chan/sa into spad */
    SPAD[0xf8] = 0xF000;                        /* show as F class device */
    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;                      /* attached? */
    /* now boot the disk */
    return chan_boot(GET_UADDR(uptr->CMD), dptr); /* boot the ch/sa */
}

/* Disk option setting commands */
/* set the disk type attached to unit */
t_stat disk_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int     i;

    if (cptr == NULL)                           /* any disk name input? */
        return SCPE_ARG;                        /* arg error */
    if (uptr == NULL)                           /* valid unit? */
        return SCPE_IERR;                       /* no, error */
    if (uptr->flags & UNIT_ATT)                 /* is unit attached? */
        return SCPE_ALATT;                      /* no, error */

    /* now loop through the units and find named disk */
    for (i = 0; disk_type[i].name != 0; i++) {
        if (strcmp(disk_type[i].name, cptr) == 0) {
            uptr->flags &= ~UNIT_TYPE;          /* clear the old UNIT type */
            uptr->flags |= SET_TYPE(i);         /* set the new type */
            /* set capacity of disk in sectors */
            uptr->capac = CAP(i);
            return SCPE_OK;
        }
    }
    return SCPE_ARG;
}

t_stat disk_get_type(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fputs("TYPE=", st);
    fputs(disk_type[GET_TYPE(uptr->flags)].name, st);
    return SCPE_OK;
}

/* help information for disk */
t_stat disk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    int i;
    fprintf (st, "SEL 2314 Disk Processor II\r\n");
    fprintf (st, "Use:\r\n");
    fprintf (st, "    sim> SET %sn TYPE=type\r\n", dptr->name);
    fprintf (st, "Type can be: ");
    for (i = 0; disk_type[i].name != 0; i++) {
        fprintf(st, "%s", disk_type[i].name);
        if (disk_type[i+1].name != 0)
        fprintf(st, ", ");
    }
    fprintf (st, ".\nEach drive has the following storage capacity:\r\n");
    for (i = 0; disk_type[i].name != 0; i++) {
        int32   size = CAPB(i);                     /* disk capacity in bytes */
        size /= 1024;                               /* make KB */
        size = (10 * size) / 1024;                  /* size in MB * 10 */
        fprintf(st, "      %-8s %4d.%1d MB cyl %3d hds %3d sec %3d blk %3d\r\n",
            disk_type[i].name, size/10, size%10, CYL(i), HDS(i), SPT(i), SSB(i));
    }
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *disk_description (DEVICE *dptr)
{
    return "SEL 2314 Disk Processor II";
}

#endif
