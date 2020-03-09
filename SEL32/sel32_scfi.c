/* sel32_scfi.c: SEL-32 SCFI SCSI Disk controller

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

#if NUM_DEVS_SCFI > 0

#define UNIT_SCFI   UNIT_ATTABLE | UNIT_IDLE | UNIT_DISABLE

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
#define SPT(type)               (scfi_type[type].spt)
/* get sectors per cylinderfor specified type */
#define SPC(type)               (scfi_type[type].spt*scfi_type[type].nhds)
/* get number of cylinders for specified type */
#define CYL(type)               (scfi_type[type].cyl)
/* get number of heads for specified type */
#define HDS(type)               (scfi_type[type].nhds)
/* get disk capacity in sectors for specified type */
#define CAP(type)               (CYL(type)*HDS(type)*SPT(type))
/* get number of bytes per sector for specified type */
#define SSB(type)               (scfi_type[type].ssiz*4)
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
        bit  6   - 0=Reserved
        bit  7   - 0=Reserved
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
struct scfi_t
{
    const char  *name;                          /* Device ID Name */
    uint16      nhds;                           /* Number of heads */
    uint16      ssiz;                           /* sector size in words */
    uint16      spt;                            /* # sectors per track(cylinder) */
    uint16      ucyl;                           /* Number of cylinders used */
    uint16      cyl;                            /* Number of cylinders on disk */
    uint8       type;                           /* Device type code */
    /* bit 1 mhd */
    /* bits 6/7 = 0 768 byte blk */             /* not used on UDP/DPII */
    /*          = 1 1024 byte blk */            /* not used on UDP/DPII */
}

scfi_type[] =
{
    /* Class F Disc Devices */
    /* MPX SCSI disks for SCFI controller */
    {"MH1GB", 1, 192, 40, 34960, 34960, 0x40},   /*0 69920 1000M */
    {"SG038", 1, 192, 20,  2190,  2190, 0x40},   /*1 21900   38M */
    {"SG120", 1, 192, 40, 34970, 34970, 0x40},   /*2 69940 1200M */
    {"SG076", 1, 192, 20, 46725, 46725, 0x40},   /*3 46725  760M */
    {NULL, 0}
};

uint8   scfi_preio(UNIT *uptr, uint16 chan);
uint8   scfi_startcmd(UNIT *uptr, uint16 chan, uint8 cmd);
uint8   scfi_haltio(UNIT *uptr);
t_stat  scfi_srv(UNIT *);
t_stat  scfi_boot(int32 unitnum, DEVICE *);
void    scfi_ini(UNIT *, t_bool);
t_stat  scfi_reset(DEVICE *);
t_stat  scfi_attach(UNIT *, CONST char *);
t_stat  scfi_detach(UNIT *);
t_stat  scfi_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat  scfi_get_type(FILE * st, UNIT *uptr, int32 v, CONST void *desc);
t_stat  scfi_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const   char  *scfi_description (DEVICE *dptr);

/* channel program information */
CHANP           sda_chp[NUM_UNITS_SCFI] = {0};

MTAB            scfi_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "TYPE", "TYPE",
    &scfi_set_type, &scfi_get_type, NULL, "Type of disk"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL, "Device channel address"},
    {0}
};

UNIT            sda_unit[] = {
/* SET_TYPE(2) SG120 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x400)},  /* 0 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x410)},  /* 1 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x420)},  /* 2 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x430)},  /* 3 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x440)},  /* 4 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x450)},  /* 5 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x460)},  /* 6 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(2), 0), 0, UNIT_ADDR(0x470)},  /* 7 */
};

//DIB sda_dib = {scfi_preio, scfi_startcmd, NULL, NULL, NULL, scfi_ini, sda_unit, sda_chp, NUM_UNITS_SCFI, 0x0f, 0x0400, 0, 0, 0};

DIB             sda_dib = {
    scfi_preio,     /* uint8 (*pre_io)(UNIT *uptr, uint16 chan)*/   /* Pre Start I/O */
    scfi_startcmd,  /* uint8 (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start a command */
    NULL,           /* uint8 (*halt_io)(UNIT *uptr) */          /* Stop I/O */
    NULL,           /* uint8 (*test_io)(UNIT *uptr) */          /* Test I/O */
    NULL,           /* uint8 (*post_io)(UNIT *uptr) */          /* Post I/O */
    scfi_ini,       /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    sda_unit,       /* UNIT* units */                           /* Pointer to units structure */
    sda_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NUM_UNITS_SCFI, /* uint8 numunits */                        /* number of units defined */
    0xF0,           /* uint8 mask */                            /* 16 devices - device mask */
    0x0400,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE          sda_dev = {
    "SDA", sda_unit, NULL, scfi_mod,
    NUM_UNITS_SCFI, 16, 24, 4, 16, 32,
    NULL, NULL, &scfi_reset, &scfi_boot, &scfi_attach, &scfi_detach,
    /* ctxt is the DIB pointer */
    &sda_dib, DEV_DISABLE|DEV_DEBUG|DEV_DIS, 0, dev_debug,
    NULL, NULL, &scfi_help, NULL, NULL, &scfi_description
};

#if NUM_DEVS_SCFI > 1
/* channel program information */
CHANP           sdb_chp[NUM_UNITS_SCFI] = {0};

UNIT            sdb_unit[] = {
/* SET_TYPE(0) DM1GB */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC00)},  /* 0 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC10)},  /* 1 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC20)},  /* 2 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC30)},  /* 3 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC40)},  /* 4 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC50)},  /* 5 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC60)},  /* 6 */
    {UDATA(&scfi_srv, UNIT_SCFI|SET_TYPE(0), 0), 0, UNIT_ADDR(0xC70)},  /* 7 */
};

//DIB sdb_dib = {scfi_preio, scfi_startcmd, NULL, NULL, NULL, scfi_ini, sdb_unit, sdb_chp, NUM_UNITS_SCFI, 0x0f, 0x0c00, 0, 0, 0};

DIB             sdb_dib = {
    scfi_preio,     /* uint8 (*pre_io)(UNIT *uptr, uint16 chan)*/   /* Pre Start I/O */
    scfi_startcmd,  /* uint8 (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start a command */
    NULL,           /* uint8 (*halt_io)(UNIT *uptr) */          /* Stop I/O */
    NULL,           /* uint8 (*test_io)(UNIT *uptr) */          /* Test I/O */
    NULL,           /* uint8 (*post_io)(UNIT *uptr) */          /* Post I/O */
    scfi_ini,       /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    sdb_unit,       /* UNIT* units */                           /* Pointer to units structure */
    sdb_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NUM_UNITS_SCFI, /* uint8 numunits */                        /* number of units defined */
    0xF0,           /* uint8 mask */                            /* 16 devices - device mask */
    0x0C00,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    0,              /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE          sdb_dev = {
    "SDB", sdb_unit, NULL, scfi_mod,
    NUM_UNITS_SCFI, 16, 24, 4, 16, 32,
    NULL, NULL, &scfi_reset, &scfi_boot, &scfi_attach, &scfi_detach,
    /* ctxt is the DIB pointer */
    &sdb_dib, DEV_DISABLE|DEV_DEBUG|DEV_DIS, 0, dev_debug,
    NULL, NULL, &scfi_help, NULL, NULL, &scfi_description
};
#endif

/* convert sector disk address to star values (c,h,s) */
uint32 scfisec2star(uint32 daddr, int type)
{
    int32 sec = daddr % scfi_type[type].spt;    /* get sector value */
    int32 spc = scfi_type[type].nhds * scfi_type[type].spt; /* sec per cyl */
    int32 cyl = daddr / spc;                    /* cylinders */
    int32 hds = (daddr % spc) / scfi_type[type].spt;    /* heads */ 

    /* now return the star value */
    return (CHS2STAR(cyl,hds,sec));             /* return STAR */
}

/* start a disk operation */
uint8  scfi_preio(UNIT *uptr, uint16 chan)
{
    DEVICE      *dptr = get_dev(uptr);
    uint16      chsa = GET_UADDR(uptr->CMD);
    int         unit = (uptr - dptr->units);

    sim_debug(DEBUG_CMD, dptr, "scfi_preio CMD %08x unit=%02x\n", uptr->CMD, unit);
    if ((uptr->CMD & 0xff00) != 0) {            /* just return if busy */
        return SNS_BSY;
    }
    sim_debug(DEBUG_CMD, dptr, "scfi_preio unit %02x chsa %04x OK\n", unit, chsa);
    return 0;                                   /* good to go */
}

uint8  scfi_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd)
{
    uint16      addr = GET_UADDR(uptr->CMD);
    DEVICE      *dptr = get_dev(uptr);
    int         unit = (uptr - dptr->units);
    CHANP       *chp = find_chanp_ptr(addr);    /* find the chanp pointer */

    sim_debug(DEBUG_CMD, dptr,
        "scfi_startcmd unit %02x cmd %04x CMD %08x\n",
        unit, cmd, uptr->CMD);
    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        if (cmd != DSK_SNS)                     /* we are completed with unit check status */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    if ((uptr->CMD & DSK_CMDMSK) != 0) {
        uptr->CMD |= DSK_BUSY;                  /* Flag we we are busy */
        return SNS_BSY;
    }
    if ((uptr->CMD & 0xff00) != 0) {            /* if any status info, we are busy */
        return SNS_BSY;
    }
    sim_debug(DEBUG_CMD, dptr, "scfi_startcmd CMD 2 unit=%02x cmd %02x\n", unit, cmd);

    /* Unit is online, so process a command */
    switch (cmd) {

    case DSK_INCH:                              /* INCH 0x00 */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_startcmd starting INCH %06x cmd, chsa %04x MemBuf %08x cnt %04x\n",
            uptr->u4, addr, chp->ccw_addr, chp->ccw_count);

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
            "scfi_startcmd starting disk seek r/w cmd %02x addr %04x\n", cmd, addr);
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
        return 0;
        break;
    }
    sim_debug(DEBUG_CMD, dptr,
        "scfi_startcmd done with scfi_startcmd %02x addr %04x SNS %08x\n",
        cmd, addr, uptr->SNS);
    if (uptr->SNS & 0xff)                       /* any other cmd is error */
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    sim_activate(uptr, 20);                     /* start things off */
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle processing of disk requests. */
t_stat scfi_srv(UNIT *uptr)
{
    uint16          chsa = GET_UADDR(uptr->CMD);
    DEVICE          *dptr = get_dev(uptr);
    /* get pointer to Dev Info Blk for this device */
    DIB             *dibp = (DIB *)dptr->ctxt;
    CHANP           *chp = (CHANP *)dibp->chan_prg;     /* get pointer to channel program */
    int             cmd = uptr->CMD & DSK_CMDMSK;
    int             type = GET_TYPE(uptr->flags);
    uint32          trk, cyl, sec;
    int             unit = (uptr - dptr->units);
    int             len=0;
    int             i;
    uint8           ch;
    uint16          ssize = scfi_type[type].ssiz*4;     /* Size of one sector in bytes */
    int32           tstart = 0;                         /* Location of start of cyl/track/sect in data */
    uint8           buf2[1024];
    uint8           buf[1024];

    sim_debug(DEBUG_DETAIL, &sda_dev,
        "scfi_srv entry unit %02x CMD %08x chsa %04x count %04x %x/%x/%x \n",
        unit, uptr->CMD, chsa, chp->ccw_count,
        STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));

    if ((uptr->flags & UNIT_ATT) == 0) {        /* unit attached status */
        uptr->SNS |= SNS_INTVENT;               /* unit intervention required */
        if (cmd != DSK_SNS)                     /* we are completed with unit check status */
            return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    sim_debug(DEBUG_CMD, dptr,
        "scfi_srv cmd=%02x chsa %04x count %04x\n", cmd, chsa, chp->ccw_count);
    switch (cmd) {
    case 0:                                     /* No command, stop disk */
        break;

    case DSK_INCH2:                             /* use 0xff for inch, just need int */
    {
        uint32  mema;                           /* memory address */
//      uint32  daws[8];                        /* drive attribute registers */
//      uint32  i, j;
        uint32  i;   

        len = chp->ccw_count;                   /* INCH command count */
        mema = chp->ccw_addr;                   /* get inch or buffer addr */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_srv starting INCH cmd, chsa %04x MemBuf %06x cnt %04x\n",
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
//                  daws[j++] = (buf[i-3]<<24) | (buf[i-2]<<16)
//                      | (buf[i-1]<<8) | (buf[i]);
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
            "scfi_srv cmd INCH chsa %04x addr %06x count %04x completed\n",
            chsa, mema, chp->ccw_count);
#ifdef FIX4MPX
        chan_end(chsa, SNS_CHNEND);             /* return just channel end OK */
#else
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
#endif
    }
        break;

    case DSK_NOP:                               /* NOP 0x03 */
        uptr->CMD &= ~(0xffff);                 /* remove old cmd */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_srv cmd NOP chsa %04x count %04x completed\n",
            chsa, chp->ccw_count);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* return OK */
        break;

    case DSK_SNS: /* 0x4 */
        ch = uptr->SNS & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv sense unit=%02x 1 %02x\n", unit, ch);
        chan_write_byte(chsa, &ch) ;
        ch = (uptr->SNS >> 8) & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv sense unit=%02x 2 %02x\n", unit, ch);
        chan_write_byte(chsa, &ch) ;
        ch = 0;
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv sense unit=%02x 3 %02x\n", unit, ch);
        chan_write_byte(chsa, &ch) ;
        ch = unit;
        sim_debug(DEBUG_DETAIL, dptr, "scfi_srv sense unit=%02x 4 %02x\n", unit, ch);
        chan_write_byte(chsa, &ch) ;
        ch = 4;
        sim_debug(DEBUG_CMD, dptr, "DISK SENSE %02x chars complete %08x, unit %02x\n",
            ch, uptr->SNS, unit);
        uptr->CMD &= ~(0xff00);
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
        break;

    case DSK_SCK:                               /* Seek cylinder, track, sector 0x07 */
        /* If we are waiting on seek to finish, check if there yet. */
        if (uptr->CMD & DSK_SEEKING) {
            /* see if on cylinder yet */
            if (STAR2CYL(uptr->STAR) == STAR2CYL(uptr->CHS)) {
                /* we are on cylinder, seek is done */
                sim_debug(DEBUG_CMD, dptr, "scfi_srv seek on cylinder unit=%02x %04x %04x\n",
                    unit, uptr->STAR >> 16, uptr->CHS >> 16);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                /* we have already seeked to the required sector */
                /* we do not need to seek again, so move on */
                chan_end(chsa, SNS_DEVEND|SNS_CHNEND);
                sim_activate(uptr, 20);
                break;
            } else {
                /* we have wasted enough time, we there */
                uptr->CHS = uptr->STAR;         /* we are there */
                sim_activate(uptr, 10);
                break;
            }
        }

        /* not seeking, so start a new seek */
        /* Read in 4 character seek code */
        for (i = 0; i < 4; i++) {
            if (chan_read_byte(chsa, &buf[i])) {
                /* we have error, bail out */
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
        }
        /* the value is really a sector offset for the disk */
        /* but will treat as c/h/s for processing */
        /* the cyl, trk, and sect are ready to update */
        sim_debug(DEBUG_CMD, dptr,
            "scfi_srv STAR unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);
rezero:
        sim_debug(DEBUG_DETAIL, dptr,
            "scfi_srv seek unit=%02x star %02x %02x %02x %02x\n",
            unit, buf[0], buf[1], buf[2], buf[3]);

        /* save STAR (target sector) data in STAR */
        uptr->STAR = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3]);
        cyl = STAR2CYL(uptr->STAR);             /* get the cylinder */
        trk = buf[2];                           /* get the track */

        sim_debug(DEBUG_DETAIL, dptr,
            "scfi_srv SEEK %08x cyl %04x trk %02x sec %02x unit=%02x\n",
            uptr->CMD, cyl&0xffff, trk, buf[3], unit);

        /* Check if seek valid */
        if (cyl >= scfi_type[type].cyl ||
            trk >= scfi_type[type].nhds ||
            buf[3] >= scfi_type[type].spt)  {

            sim_debug(DEBUG_CMD, dptr,
                "dsk_srv seek ERROR cyl %04x trk %02x sec %02x unit=%02x\n",
                cyl, trk, buf[3], unit);

            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
            uptr->SNS |= SNS_CMDREJ|SNS_EQUCHK;  /* set error status */

            /* we have an error, tell user */
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);  /* end command */
            break;
        }

        /* calc the new sector address of data */
        /* calculate file position in bytes of requested sector */
        /* file offset in bytes */
        tstart = STAR2SEC(uptr->STAR, SPT(type), SPC(type)) * SSB(type);
        uptr->CHS = CHS2STAR(STAR2CYL(uptr->CHS), trk, buf[3]);

        sim_debug(DEBUG_DETAIL, dptr,
            "scfi_srv seek start %08x trk %04x sec %02x\n",
            tstart, trk, buf[3]);

        /* just seek to the location where we will r/w data */
        if ((sim_fseek(uptr->fileref, tstart, SEEK_SET)) != 0) {  /* seek home */
            sim_debug(DEBUG_DETAIL, dptr, "scfi_srv Error on seek to %08x\n", tstart);
            chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
            return SCPE_OK;
        }

        /* Check if already on correct cylinder */
        /* if not, do a delay to slow things down */
        if (STAR2CYL(uptr->STAR) != STAR2CYL(uptr->CHS)) {
            /* Do a fake seek to kill time */
            uptr->CMD |= DSK_SEEKING;           /* show we are seeking */
            sim_debug(DEBUG_DETAIL, dptr,
                "scfi_srv seeking unit=%02x to cyl %04x trk %04x sec %04x\n",
                unit, cyl, trk, buf[3]);
            sim_activate(uptr, 20);
        } else {
            /* we are on cylinder/track/sector, so go on */
            sim_debug(DEBUG_DETAIL, dptr,
                "scfi_srv calc sect addr seek start %08x cyl %04x trk %04x sec %02x\n",
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
        /* tstart has start of sector address in bytes */
        if ((uptr->CMD & DSK_READING) == 0) {   /* see if we are reading data */
            uptr->CMD |= DSK_READING;           /* read from disk starting */
            sim_debug(DEBUG_CMD, dptr,
                "DISK READ starting unit=%02x CMD %08x count %04x\n",
                unit, uptr->CMD, chp->ccw_count);
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

            sim_debug(DEBUG_CMD, dptr, "scfi_srv after READ chsa %04x count %04x\n",
                chsa, chp->ccw_count);

            /* process the next sector of data */
            for (i=0; i<len; i++) {
                ch = buf[i];                    /* get a char from buffer */
                if (chan_write_byte(chsa, &ch)) {   /* put a byte to memory */
                    sim_debug(DEBUG_DATA, dptr,
                        "DISK Read %04x bytes from diskfile /%04x/%02x/%02x tstart %08x\n",
                        len, cyl, trk, sec, tstart);
                    uptr->CMD &= ~(0xffff);     /* remove old status bits & cmd */
                    chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                    goto rddone;
                }
            }

            sim_debug(DEBUG_CMD, dptr,
                "DISK READ from sec end %04x bytes end %04x from diskfile /%04x/%02x/%02x tstart %08x\n",
                len, ssize, cyl, trk, sec, tstart);

            /* tstart has file offset in sectors */
            tstart++;                           /* bump to next sector */
            /* convert sect back to chs value */
            uptr->CHS = scfisec2star(tstart, type);
            /* see of over end of disk */
//          if (tstart >= CAPB(type)) {
            if (tstart >= CAP(type)) {
                /* EOM reached, abort */
                sim_debug(DEBUG_CMD, dptr,
                    "DISK Read reached EOM for read from disk @ /%04x/%02x/%02x\n",
                    STAR2CYL(uptr->CHS), (uptr->CHS >> 8)&0xff, (uptr->CHS&0xff));
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                uptr->CHS = 0;                  /* reset cylinder position */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }

            /* see if we are done reading data */
            if (test_write_byte_end(chsa)) {
                sim_debug(DEBUG_DATA, dptr,
                    "DISK Read complete Read %04x bytes from diskfile /%04x/%02x/%02x tstart %08x\n",
                    ssize, cyl, trk, sec, tstart);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);
                break;
            } else {
                sim_debug(DEBUG_DATA, dptr,
                    "DISK sector read complete, %x bytes to go from diskfile /%04x/%02x/%02x\n",
                    chp->ccw_count, STAR2CYL(uptr->CHS), ((uptr->CHS) >> 8)&0xff, (uptr->CHS&0xff));
                sim_activate(uptr, 10);         /* wait to read next sector */
                break;
            }
rddone:
            uptr->CMD &= ~(0xffff);             /* remove old status bits & cmd */
        }
        break;

    case DSK_WD:            /* Write Data */
        /* tstart has start of sector address in bytes */

        if ((uptr->CMD & DSK_WRITING) == 0) {   /* see if we are writing data */
            uptr->CMD |= DSK_WRITING;           /* write to disk starting */
            sim_debug(DEBUG_CMD, dptr,
                "DISK WRITE starting unit=%02x CMD %08x bytes %04x\n",
                unit, uptr->CMD, len);
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
                        sim_debug(DEBUG_DATA, dptr,
                            "DISK Wrote %04x bytes to diskfile cyl %04x hds %02x sec %02x tstart %08x\n",
                            ssize, cyl, trk, sec, tstart);
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
                    "Error %08x on write %04x to diskfile cyl %04x hds %02x sec %02x\n",
                    i, ssize, cyl, trk, sec);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                break;
            }
            if (len != 0) {                     /* see if done with write command */
                sim_debug(DEBUG_DATA, dptr,
                    "DISK WroteB %04x bytes to diskfile cyl %04x hds %02x sec %02x tstart %08x\n",
                    ssize, cyl, trk, sec, tstart);
                uptr->CMD &= ~(0xffff);         /* remove old status bits & cmd */
                chan_end(chsa, SNS_CHNEND|SNS_DEVEND);  /* we done */
                break;
            }
            sim_debug(DEBUG_CMD, dptr,
                "DISK WR to sec end %04x bytes end %04x to diskfile cyl %04x hds %02x sec %02x tstart %08x\n",
                len, ssize, cyl, trk, sec, tstart);

            /* tstart has file offset in sectors */
            tstart++;                           /* bump to next sector */
            /* convert sect back to chs value */
            uptr->CHS = scfisec2star(tstart, type);
            /* see of over end of disk */
//          if (tstart >= CAPB(type)) {
            if (tstart >= CAP(type)) {
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

    default:
        sim_debug(DEBUG_CMD, dptr, "invalid command %02x unit %02x\n", cmd, unit);
        uptr->SNS |= SNS_CMDREJ;
        uptr->CMD &= ~(0xffff);                 /* remove old status bits & cmd */
        chan_end(chsa, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
        break;
    }
    sim_debug(DEBUG_CMD, dptr,
        "scfi_srv done cmd %02x chsa %04x count %04x\n", cmd, chsa, chp->ccw_count);
    return SCPE_OK;
}

/* initialize the disk */
void scfi_ini(UNIT *uptr, t_bool f)
{
    DEVICE  *dptr = get_dev(uptr);
    int     i = GET_TYPE(uptr->flags);

    uptr->CMD &= ~0xffff;                       /* clear out the flags but leave ch/sa */
    uptr->SNS = ((uptr->SNS & 0x00ffffff) | (scfi_type[i].type << 24));  /* save mode value */
    /* total sectors on disk */
    uptr->capac = CAP(i);                       /* disk size in sectors */

    sim_debug(DEBUG_EXP, &sda_dev, "SDA init device %s on unit SDA%.1x cap %x\n",
        dptr->name, GET_UADDR(uptr->CMD), uptr->CMD);
}

t_stat scfi_reset(DEVICE * dptr)
{
    /* add reset code here */
    return SCPE_OK;
}

/* create the disk file for the specified device */
int scfi_format(UNIT *uptr) {
//  struct ddata_t  *data = (struct ddata_t *)uptr->up7;
    uint16      addr = GET_UADDR(uptr->CMD);
    int         type = GET_TYPE(uptr->flags);
    DEVICE      *dptr = get_dev(uptr);
    int32       ssize = scfi_type[type].ssiz * 4;       /* disk sector size in bytes */
    uint32      tsize = scfi_type[type].spt;            /* get track size in sectors */
    uint32      csize = scfi_type[type].nhds * tsize;   /* get cylinder size in sectors */
    uint32      cyl = scfi_type[type].cyl;              /* get # cyl */
//  uint16      spc = scfi_type[type].nhds * scfi_type[type].spt;   /* sectors/cyl */
    uint32      cap = scfi_type[type].cyl * csize;      /* disk capacity in sectors */
    uint32      cylv = cyl;                             /* number of cylinders */
    uint8       *buff;

    /* see if user wants to initialize the disk */
    if (!get_yn("Initialize disk? [Y] ", TRUE)) {
        return 1;
    }

    /* seek to sector 0 */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        fprintf (stderr, "Error on seek to 0\r\n");
        return 1;
    }

    /* get buffer for track data */
    if ((buff = (uint8 *)calloc(tsize*ssize, sizeof(uint8))) == 0) {
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
        if ((sim_fwrite(buff, 1, tsize*ssize, uptr->fileref)) != tsize*ssize) {
            sim_debug(DEBUG_CMD, dptr,
                "Error on write to diskfile cyl %04x\n", cyl);
        }
        if (cyl == 0) {
            buff[0] = 0;
            buff[1] = 0;
            buff[2] = 0;
            buff[3] = 0;
        }
    }
    if ((cyl % 100) == 0)
        fputc('.', stderr);
    fputc('\r', stderr);
    fputc('\n', stderr);
    /* seek home again */
    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        fprintf (stderr, "Error on seek to 0\r\n");
        return 1;
    }
    free(buff);                                 /* free cylinder buffer */
    set_devattn(addr, SNS_DEVEND);              /* start us up */
    return 0;
}

/* attach the selected file to the disk */
t_stat scfi_attach(UNIT *uptr, CONST char *file) {
    uint16          addr = GET_UADDR(uptr->CMD);
    int             type = GET_TYPE(uptr->flags);
    DEVICE          *dptr = get_dev(uptr);
    t_stat          r;
    uint32          ssize;                      /* sector size in bytes */
    uint8           buff[1024];

    if (scfi_type[type].name == 0) {            /* does the assigned disk have a name */
        detach_unit(uptr);                      /* no, reject */
        return SCPE_FMT;                        /* error */
    }

    /* have simulator attach the file to the unit */
    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;

    uptr->capac = CAP(type);                    /* disk capacity in sectors */
    ssize = SSB(type);                          /* get sector size in bytes */

    sim_debug(DEBUG_CMD, dptr, "Disk %s cyl %d hds %d sec %d ssiz %d capacity %d\n",
        scfi_type[type].name, scfi_type[type].cyl, scfi_type[type].nhds, 
        scfi_type[type].spt, ssize, uptr->capac); /* disk capacity */


    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        detach_unit(uptr);                      /* if no space, error */
        return SCPE_FMT;                        /* error */
    }

    /* read in the 1st sector of the 'disk' */
    if ((r = sim_fread(&buff[0], sizeof(uint8), ssize, uptr->fileref) != ssize)) {
        sim_debug(DEBUG_CMD, &sda_dev, "Disk format fread ret = %04x\n", r);
        goto fmt;
    }

    if ((buff[0] | buff[1] | buff[2] | buff[3]) == 0) {
        sim_debug(DEBUG_CMD, &sda_dev,
        "Disk format buf0 %02x buf1 %02x buf2 %02x buf3 %02x\n",
        buff[0], buff[1], buff[2], buff[3]);
fmt:
        /* format the drive */
        if (scfi_format(uptr)) {
            detach_unit(uptr);                  /* if no space, error */
            return SCPE_FMT;                    /* error */
        }
    }

    if ((sim_fseek(uptr->fileref, 0, SEEK_SET)) != 0) { /* seek home */
        detach_unit(uptr);                      /* if no space, error */
        return SCPE_FMT;                        /* error */
    }

    uptr->CHS = 0;                              /* set CHS to cyl/hd/sec = 0 */

    sim_debug(DEBUG_CMD, &sda_dev,
        "Attach %s cyl %d hds %d spt %d spc %d cap sec %d cap bytes %d\n",
        scfi_type[type].name, CYL(type), HDS(type), SPT(type), SPC(type),  
        CAP(type), CAPB(type));

    sim_debug(DEBUG_CMD, &sda_dev, "File %s attached to %s\r\n",
        file, scfi_type[type].name);

    set_devattn(addr, SNS_DEVEND);
    return SCPE_OK;
}

/* detach a disk device */
t_stat scfi_detach(UNIT *uptr) {
    uptr->SNS = 0;                              /* clear sense data */
    uptr->CMD &= ~0xffff;                       /* no cmd and flags */
    return detach_unit(uptr);                   /* tell simh we are done with disk */
}

/* boot from the specified disk unit */
t_stat scfi_boot(int32 unit_num, DEVICE *dptr) {
    UNIT    *uptr = &dptr->units[unit_num];     /* find disk unit number */

    sim_debug(DEBUG_CMD, &sda_dev, "SCFI Disk Boot dev/unit %04x\n", GET_UADDR(uptr->CMD));
    SPAD[0xf4] = GET_UADDR(uptr->CMD);          /* put boot device chan/sa into spad */
    SPAD[0xf8] = 0xF000;                        /* show as F class device */
    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;                      /* attached? */
    return chan_boot(GET_UADDR(uptr->CMD), dptr);    /* boot the ch/sa */
}

/* Disk option setting commands */
t_stat scfi_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int     i;

    if (cptr == NULL)                           /* any disk name input? */
        return SCPE_ARG;                        /* arg error */
    if (uptr == NULL)                           /* valid unit? */
        return SCPE_IERR;                       /* no, error */
    if (uptr->flags & UNIT_ATT)                 /* is unit attached? */
        return SCPE_ALATT;                      /* no, error */

    /* now loop through the units and find named disk */
    for (i = 0; scfi_type[i].name != 0; i++) {
        if (strcmp(scfi_type[i].name, cptr) == 0) {
            uptr->flags &= ~UNIT_TYPE;          /* clear the old UNIT type */
            uptr->flags |= SET_TYPE(i);         /* set the new type */
            /* set capacity of disk in sectors */
            uptr->capac = CAP(i);
            return SCPE_OK;
        }
    }
    return SCPE_ARG;
}

t_stat scfi_get_type(FILE * st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fputs("TYPE=", st);
    fputs(scfi_type[GET_TYPE(uptr->flags)].name, st);
    return SCPE_OK;
}

/* help information for disk */
t_stat scfi_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
    const char *cptr)
{
    int i;
    fprintf (st, "SEL-32 SCFI Disk Processor\r\n");
    fprintf (st, "Use:\r\n");
    fprintf (st, "    sim> SET %sn TYPE=type\r\n", dptr->name);
    fprintf (st, "Type can be: ");
    for (i = 0; scfi_type[i].name != 0; i++) {
        fprintf(st, "%s", scfi_type[i].name);
        if (scfi_type[i+1].name != 0)
        fprintf(st, ", ");
    }
    fprintf (st, ".\nEach drive has the following storage capacity:\r\n");
    for (i = 0; scfi_type[i].name != 0; i++) {
        int32   size = CAPB(i);                     /* disk capacity in bytes */
        size /= 1024;                               /* make KB */
        size = (10 * size) / 1024;                  /* size in MB * 10 */
        fprintf(st, "      %-8s %4d.%1d MB cyl %3d hds %3d sec %3d blk %3d\r\n",
            scfi_type[i].name, size/10, size%10, CYL(i), HDS(i), SPT(i), SSB(i));
    }
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *scfi_description (DEVICE *dptr)
{
    return "SEL-32 SCFI Disk Processor";
}

#endif
