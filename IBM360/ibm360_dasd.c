/* ibm360_dasd.c: IBM 360 2311/2314 Disk controller

   Copyright (c) 2017-2020, Richard Cornwell

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

   Structure of a disk. See Hercules CKD disks.

    Numbers are stored least to most significant.

     Devid = "CKD_P370"

       uint8    devid[8]        device header.
       uint32   heads           number of heads per cylinder
       uint32   tracksize       size of track
       uint8    devtype         Hex code of last two digits of device type.
       uint8    fileseq         always 0.
       uint16   highcyl         highest cylinder.

       uint8    resv[492]       pad to 512 byte block

   Each Track has:
       uint8    bin             Track header.
       uint16   cyl             Cylinder number
       uint16   head            Head number.

   Each Record has:
       uint16   cyl             Cylinder number  <- tpos
       uint16   head            Head number
       uint8    rec             Record id.
       uint8    klen            Length of key
       uint16   dlen            Length of data

       uint8    key[klen]       Key data.
       uint8    data[dlen]      Data len.

   cpos points to where data is actually read/written from

   Pad to being track to multiple of 512 bytes.
   Last record has cyl and head = 0xffffffff

*/

#include "ibm360_defs.h"

#ifdef NUM_DEVS_DASD
#define UNIT_V_TYPE        (UNIT_V_UF + 0)
#define UNIT_TYPE          (0xf << UNIT_V_TYPE)

#define GET_TYPE(x)        ((UNIT_TYPE & (x)) >> UNIT_V_TYPE)
#define SET_TYPE(x)         (UNIT_TYPE & ((x) << UNIT_V_TYPE))
#define UNIT_DASD          UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE | \
                             UNIT_FIX | SET_TYPE(6)


#define DK_NOP             0x03       /* Nop operation */
#define DK_RELEASE         0x17       /* Release from channel */
#define DK_RESTORE         0x13       /* Restore */
#define DK_SEEK            0x07       /* Seek */
#define DK_SEEKCYL         0x0B       /* Seek Cylinder */
#define DK_SEEKHD          0x1B       /* Seek Head */
#define DK_SETMSK          0x1f       /* Set file mask */
#define DK_SPACE           0x0f       /* Space record */
#define DK_SRCH_HAEQ       0x39       /* Search HA equal */
#define DK_SRCH_IDEQ       0x31       /* Search ID equal */
#define DK_SRCH_IDGT       0x51       /* Search ID greater */
#define DK_SRCH_IDGE       0x71       /* Search ID greater or equal */
#define DK_SRCH_KYEQ       0x29       /* Search Key equal */
#define DK_SRCH_KYGT       0x49       /* Search Key greater */
#define DK_SRCH_KYGE       0x69       /* Search Key greater or equal */
#define DK_RD_IPL          0x02       /* Read IPL record */
#define DK_RD_HA           0x1A       /* Read home address */
#define DK_RD_CNT          0x12       /* Read count */
#define DK_RD_R0           0x16       /* Read R0 */
#define DK_RD_D            0x06       /* Read Data */
#define DK_RD_KD           0x0e       /* Read key and data */
#define DK_RD_CKD          0x1e       /* Read count, key and data */
#define DK_WR_HA           0x19       /* Write home address */
#define DK_WR_R0           0x15       /* Write R0 */
#define DK_WR_D            0x05       /* Write Data */
#define DK_WR_KD           0x0d       /* Write key and data */
#define DK_WR_CKD          0x1d       /* Write count, key and data */
#define DK_WR_SCKD         0x01       /* Write special count, key and data */
#define DK_ERASE           0x11       /* Erase to end of track */
#define DK_RD_SECT         0x22       /* Read sector counter */
#define DK_SETSECT         0x23       /* Set sector */
#define DK_MT              0x80       /* Multi track flag */

/* u3 */
#define DK_INDEX           0x00100    /* Index seen in command */
#define DK_NOEQ            0x00200    /* Not equal compare */
#define DK_HIGH            0x00400    /* High compare */
#define DK_PARAM           0x00800    /* Parameter in u4 */
#define DK_MSET            0x01000    /* Mode set command already */
#define DK_SHORTSRC        0x02000    /* Last search was short */
#define DK_SRCOK           0x04000    /* Last search good */
#define DK_CYL_DIRTY       0x08000    /* Current cylinder dirty */
#define DK_DONE            0x10000    /* Write command done, zero fill */
#define DK_INDEX2          0x20000    /* Second index seen */
#define DK_OVFLOW          0x40000    /* Reading in overflow */

#define DK_MSK_INHWR0      0x00       /* Inhbit writing of HA/R0 */
#define DK_MSK_INHWRT      0x40       /* Inhbit all writes */
#define DK_MSK_ALLWRU      0x80       /* Allow all updates */
#define DK_MSK_ALLWRT      0xc0       /* Allow all writes */
#define DK_MSK_WRT         0xc0       /* Write mask */

#define DK_MSK_SKALLSKR    0x00       /* Allow all seek/recal */
#define DK_MSK_SKALLCLY    0x08       /* Allow cyl/head only */
#define DK_MSK_SKALLHD     0x10       /* Allow head only */
#define DK_MSK_SKNONE      0x18       /* Allow no seeks */
#define DK_MSK_SK          0x18       /* Seek mask */

/* Top 11 bits of u3 hold the device address */

/* u4 */
/* Holds the current track and head */
#define DK_V_TRACK         8
#define DK_M_TRACK         0x3ff00    /* Max 1024 cylinders */
#define DK_V_HEAD          0
#define DK_M_HEAD          0xff       /* Max 256 heads */

/* u5 */
/* Sense byte 0 */
#define SNS_CMDREJ         0x80       /* Command reject */
#define SNS_INTVENT        0x40       /* Unit intervention required */
#define SNS_BUSCHK         0x20       /* Parity error on bus */
#define SNS_EQUCHK         0x10       /* Equipment check */
#define SNS_DATCHK         0x08       /* Data Check */
#define SNS_OVRRUN         0x04       /* Data overrun */
#define SNS_TRKCND         0x02       /* Track Condition */
#define SNS_SEEKCK         0x01       /* Seek Check */

/* Sense byte 1 */
#define SNS_DCCNT          0x80       /* Data Check Count */
#define SNS_TRKOVR         0x40       /* Track Overrun */
#define SNS_ENDCYL         0x20       /* End of Cylinder */
#define SNS_INVSEQ         0x10       /* Invalid Sequence */
#define SNS_NOREC          0x08       /* No record found */
#define SNS_WRP            0x04       /* Write Protect */
#define SNS_ADDR           0x02       /* Missing Address Mark */
#define SNS_OVRINC         0x01       /* Overflow Incomplete */

/* Sense byte 2 */
#define SNS_BYTE2          0x00       /* Diags Use */
/* Sense byte 3 */
#define SNS_BYTE3          0x00       /* Diags Use */

/* saved in state field of data */
/* Record position, high 4 bits, low internal short count */
#define DK_POS_INDEX       0x0        /* At Index Mark */
#define DK_POS_HA          0x1        /* In home address (c) */
#define DK_POS_CNT         0x2        /* In count (c) */
#define DK_POS_KEY         0x3        /* In Key area */
#define DK_POS_DATA        0x4        /* In Data area */
#define DK_POS_AM          0x5        /* Address mark before record */
#define DK_POS_END         0x8        /* Past end of data */
#define DK_POS_SEEK        0xF        /* In seek */

/* u6 holds last command */
/* Held in ccyl entry */

#define CMD    u3
#define CCH    u4
#define SNS    u5
#define LCMD   u6

/* Pointer held in up7 */
struct dasd_t
{
     uint8             *cbuf;    /* Cylinder buffer */
     uint32             cpos;    /* Position of head of cylinder in file */
     uint32             tstart;  /* Location of start of track */
     uint16             ccyl;    /* Current Cylinder number */
     uint16             cyl;     /* Cylinder head at */
     uint16             tpos;    /* Track position */
     uint16             rpos;    /* Start of current record */
     uint16             dlen;    /* remaining in data */
     uint32             tsize;   /* Size of one track include rounding */
     uint8              state;   /* Current state */
     uint8              klen;    /* remaining in key */
     uint8              filemsk; /* Current file mask */
     uint8              rec;     /* Current record number */
     uint8              ovfl;    /* Current record overflow record */
     uint16             count;   /* Remaining in current operation */
};

struct disk_t
{
    const char         *name;         /* Type Name */
    int                 cyl;          /* Number of cylinders */
    int                 heads;        /* Number of heads/cylinder */
    int                 bpt;          /* Max bytes per track */
    uint8               sen_cnt;      /* Number of sense bytes */
    uint8               dev_type;     /* Device type code */
}
disk_type[] =
{
       {"2301",   1, 200, 20483,  6,  0x01},   /*   4.1  M */
       {"2302", 250,  46,  4984,  6,  0x02},   /*  57.32 M 50ms, 120ms/10, 180ms> 10 */
       {"2303",  80,  10,  4984,  6,  0x03},   /*   4.00 M */
       {"2305",  48,   8, 14568,  6,  0x05},   /*   5.43 M */
       {"2305-2",96,   8, 14858,  6,  0x05},   /*  11.26 M */
       {"2311",  203, 10,  3717,  6,  0x11},   /*   7.32 M  156k/s 30 ms 145 full */
       {"2314",  203, 20,  7294,  6,  0x14},   /*  29.17 M */
       {"3330",  411, 19, 13165, 24,  0x30},   /* 100.00 M */
       {"3330-2",815, 19, 13165, 24,  0x30},
       {"3340",  349, 12,  8535, 24,  0x40},   /*  34.94 M */
       {"3340-2",698, 12,  8535, 24,  0x40},   /*  69.89 M */
       {"3350",  560, 30, 19254, 24,  0x50},   /* 304.80 M */
       {"5625",  403, 20,  7294,  6,  0x14},   /*  56.00 M */
       {NULL, 0}
};


/* Header block */
struct dasd_header
{
       char     devid[8];      /* device header. */
       uint32   heads;         /* number of heads per cylinder */
       uint32   tracksize;     /* size of track */
       uint8    devtype;       /* Hex code of last two digits of device type. */
       uint8    fileseq;       /* always 0. */
       uint16   highcyl;       /* highest cylinder. */
       uint8    resv[492];     /* pad to 512 byte block */
};

uint8               dasd_startio(UNIT *uptr, uint16 chan) ;
uint8               dasd_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) ;
uint8               dasd_haltio(uint16 addr);
t_stat              dasd_srv(UNIT *);
t_stat              dasd_boot(int32, DEVICE *);
void                dasd_ini(UNIT *, t_bool);
t_stat              dasd_reset(DEVICE *);
t_stat              dasd_attach(UNIT *, CONST char *);
t_stat              dasd_detach(UNIT *);
t_stat              dasd_boot(int32, DEVICE *);
t_stat              dasd_set_type(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              dasd_setd_type(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              dasd_get_type(FILE * st, UNIT * uptr, int32 v,
                                 CONST void *desc);
t_stat              dasd_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *dasd_description (DEVICE *dptr);

MTAB                dasd_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "TYPE", "TYPE",
     &dasd_set_type, &dasd_get_type, NULL, "Type of disk"},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, NULL, "MODEL",
     &dasd_setd_type, NULL, NULL, "Set all drives to type"},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {0}
};

UNIT                dda_unit[] = {
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x130)},       /* 0 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x131)},       /* 1 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x132)},       /* 2 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x133)},       /* 3 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x134)},       /* 4 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x135)},       /* 5 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x136)},       /* 6 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x137)},       /* 7 */
};

struct dib dda_dib = { 0xF8, NUM_UNITS_MT, dasd_startio, dasd_startcmd, NULL,
                        dda_unit, dasd_ini};

DEVICE              dda_dev = {
    "DA", dda_unit, NULL, dasd_mod,
    NUM_UNITS_DASD, 8, 15, 1, 8, 8,
    NULL, NULL, &dasd_reset, &dasd_boot, &dasd_attach, &dasd_detach,
    &dda_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dasd_help, NULL, NULL, &dasd_description
};

#if NUM_DEVS_DASD > 1
UNIT                ddb_unit[] = {
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x230)},       /* 0 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x231)},       /* 1 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x232)},       /* 2 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x233)},       /* 3 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x234)},       /* 4 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x235)},       /* 5 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x236)},       /* 6 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x237)},       /* 7 */
};

struct dib ddb_dib = { 0xF8, NUM_UNITS_MT, dasd_startio, dasd_startcmd, NULL,
                        ddb_unit, dasd_ini};

DEVICE              ddb_dev = {
    "DB", ddb_unit, NULL, dasd_mod,
    NUM_UNITS_DASD, 8, 15, 1, 8, 8,
    NULL, NULL, &dasd_reset, &dasd_boot, &dasd_attach, &dasd_detach,
    &ddb_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dasd_help, NULL, NULL, &dasd_description
};

#if NUM_DEVS_DASD > 2

UNIT                ddc_unit[] = {
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x150)},       /* 0 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x151)},       /* 1 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x152)},       /* 2 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x153)},       /* 3 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x154)},       /* 4 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x155)},       /* 5 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x156)},       /* 6 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x157)},       /* 7 */
};

struct dib ddc_dib = { 0xF8, NUM_UNITS_MT, dasd_startio, dasd_startcmd, NULL,
                        ddc_unit, dasd_ini};

DEVICE              ddc_dev = {
    "DC", ddc_unit, NULL, dasd_mod,
    NUM_UNITS_DASD, 8, 15, 1, 8, 8,
    NULL, NULL, &dasd_reset, &dasd_boot, &dasd_attach, &dasd_detach,
    &ddc_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dasd_help, NULL, NULL, &dasd_description
};

#if NUM_DEVS_DASD > 3
UNIT                ddd_unit[] = {
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x250)},       /* 0 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x251)},       /* 1 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x252)},       /* 2 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x253)},       /* 3 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x254)},       /* 4 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x255)},       /* 5 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x256)},       /* 6 */
    {UDATA(&dasd_srv, UNIT_DASD, 0), 0, UNIT_ADDR(0x257)},       /* 7 */
};

struct dib ddd_dib = { 0xF8, NUM_UNITS_MT, dasd_startio, dasd_startcmd, NULL,
                        ddd_unit, dasd_ini};

DEVICE              ddd_dev = {
    "DD", ddd_unit, NULL, dasd_mod,
    NUM_UNITS_DASD, 8, 15, 1, 8, 8,
    NULL, NULL, &dasd_reset, &dasd_boot, &dasd_attach, &dasd_detach,
    &ddd_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dasd_help, NULL, NULL, &dasd_description
};
#endif
#endif
#endif

uint8  dasd_startio(UNIT *uptr, uint16 chan) {
    uint16         addr = GET_UADDR(uptr->CMD);
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);

    if ((uptr->CMD & 0xff) != 0) {
       return SNS_BSY;
    }
    uptr->CMD &= ~(DK_INDEX|DK_NOEQ|DK_HIGH|DK_PARAM|DK_MSET|DK_DONE|DK_INDEX2);
    if ((uptr->flags & UNIT_ATT) != 0) {
        struct dasd_t  *data = (struct dasd_t *)(uptr->up7);
        data->filemsk = 0;
    }
    sim_debug(DEBUG_CMD, dptr, "start io unit=%d\n", unit);
    return 0;
}

uint8  dasd_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) {
    uint16         addr = GET_UADDR(uptr->CMD);
    DEVICE         *dptr = find_dev_from_unit(uptr);
    struct dasd_t      *data = (struct dasd_t *)(uptr->up7);
    int            unit = (uptr - dptr->units);
    uint8          ch;

    if ((uptr->CMD & 0xff) != 0) {
       return SNS_BSY;
    }

    sim_debug(DEBUG_CMD, dptr, "CMD unit=%d %02x\n", unit, cmd);
    if ((uptr->flags & UNIT_ATT) == 0) {
       if (cmd == 0x4) {  /* Sense */
           int     type = GET_TYPE(uptr->flags);
           int     i;
           sim_debug(DEBUG_CMD, dptr, "CMD sense\n");
           ch = uptr->SNS & 0xff;
           sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 1 %x\n", unit, ch);
           if (chan_write_byte(addr, &ch))
               goto sense_end;
           ch = (uptr->SNS >> 8) & 0xff;
           sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 2 %x\n", unit, ch);
           if (chan_write_byte(addr, &ch))
               goto sense_end;
           ch = 0;
           sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 3 %x\n", unit, ch);
           if (chan_write_byte(addr, &ch))
               goto sense_end;
           if (disk_type[type].sen_cnt > 6) {
               ch = (unit & 07) | ((~unit & 07) << 3);
               sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 4 %x\n", unit, ch);
               if (chan_write_byte(addr, &ch))
                   goto sense_end;
               ch = unit;
               sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 5 %x\n", unit, ch);
               if (chan_write_byte(addr, &ch))
                   goto sense_end;
               ch = (uptr->CCH >> 8) & 0xff;
               sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 6 %x\n", unit, ch);
               if (chan_write_byte(addr, &ch))
                   goto sense_end;
               ch = (uptr->CCH & 0x1f) | ((uptr->CCH & 0x10000) ? 0x40 : 0);
               sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 7 %x\n", unit, ch);
               if (chan_write_byte(addr, &ch))
                   goto sense_end;
               ch = 0;              /* Compute message code */
               sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 8 %x\n", unit, ch);
               if (chan_write_byte(addr, &ch))
                   goto sense_end;
               i = 8;
           } else {
               if (disk_type[type].dev_type == 0x11)
                   ch = 0xc8;
               else
                   ch = 0x40;
               if ((uptr->CCH >> 8) & SNS_ENDCYL)
                  ch |= 4;
               sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 4 %x\n", unit, ch);
               if (chan_write_byte(addr, &ch))
                   goto sense_end;
               ch = unit;
               sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 5 %x\n", unit, ch);
               if (chan_write_byte(addr, &ch))
                   goto sense_end;
               i = 5;
           }
           ch = 0;
           for (; i < disk_type[type].sen_cnt; i++) {
               sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d %d %x\n", unit, i, ch);
               if (chan_write_byte(addr, &ch))
                   goto sense_end;
           }
sense_end:
           uptr->SNS = 0;
           return SNS_CHNEND|SNS_DEVEND;
       }
       if (cmd == 0x0)
           return 0;

       uptr->SNS = SNS_INTVENT|SNS_CMDREJ;
       return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    }

    switch (cmd & 0x3) {
    case 0x3:              /* Control */
         if ((cmd & 0xfc) == 0 ||  cmd == DK_RELEASE)
            return SNS_CHNEND|SNS_DEVEND;
         /* Fall Through */

    case 0x1:              /* Write command */
    case 0x2:              /* Read command */
         uptr->CMD &= ~(DK_PARAM);
         uptr->CMD |= cmd;
         uptr->SNS = 0;
         return 0;

    case 0x0:               /* Status */
         if ((cmd & 0xF) == 0x4) {  /* Sense */
            uptr->CMD |= cmd;
            return 0;
         }
         break;
    }
    if (uptr->SNS & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Compute position on new track. */
void dasd_adjpos(UNIT * uptr)
{
    uint16              addr = GET_UADDR(uptr->CMD);
    struct dasd_t      *data = (struct dasd_t *)(uptr->up7);
    uint8               *rec;
    int                 pos;

    /* Save current position */
    pos = data->tpos;

    /* Set ourselves to start of track */
    data->state = DK_POS_HA;
    data->rec = data->klen = 0;
    data->rpos = data->count = data->dlen = 0;
    data->tstart = (uptr->CCH & 0xff) * data->tsize;
    rec = &data->cbuf[data->rpos + data->tstart];
    /* Skip forward until we reach pos */
    for (data->tpos = 0; data->tpos < pos; data->tpos++) {
        switch(data->state) {
         case DK_POS_HA:                /* In home address (c) */
              if (data->count == 4) {
                  data->tpos = data->rpos = 5;
                  data->state = DK_POS_CNT;
                  rec = &data->cbuf[data->rpos + data->tstart];
                  /* Check for end of track */
                  if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff)
                     data->state = DK_POS_END;
              }
              break;
         case DK_POS_CNT:               /* In count (c) */
              if (data->count == 0) {
                  /* Check for end of track */
                  if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff) {
                     data->state = DK_POS_END;
                  }
                  data->klen = rec[5];
                  data->dlen = (rec[6] << 8) | rec[7];
              }
              if (data->count == 7) {
                     data->state = DK_POS_KEY;
                     if (data->klen == 0)
                        data->state = DK_POS_DATA;
              }
              break;
         case DK_POS_KEY:               /* In Key area */
              if (data->count == data->klen) {
                  data->state = DK_POS_DATA;
                  data->count = 0;
              }
              break;
         case DK_POS_DATA:              /* In Data area */
              if (data->count == data->dlen) {
                  data->state = DK_POS_AM;
              }
              break;
         case DK_POS_AM:                /* Beginning of record */
              data->rpos += data->dlen + data->klen + 8;
              data->tpos = data->rpos;
              data->rec++;
              data->state = DK_POS_CNT;
              data->count = 0;
              rec = &data->cbuf[data->rpos + data->tstart];
              /* Check for end of track */
              if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff)
                 data->state = DK_POS_END;
              break;
         case DK_POS_END:               /* Past end of data */
              data->tpos+=10;
              data->count = 0;
              data->klen = 0;
              data->dlen = 0;
              return;
         }
     }
}

/* Handle processing of disk requests. */
t_stat dasd_srv(UNIT * uptr)
{
    uint16              addr = GET_UADDR(uptr->CMD);
    DEVICE             *dptr = find_dev_from_unit(uptr);
    struct dasd_t      *data = (struct dasd_t *)(uptr->up7);
    int                 unit = (uptr - dptr->units);
    int                 cmd = uptr->CMD & 0x7f;
    int                 type = GET_TYPE(uptr->flags);
    int                 state = data->state;
    int                 count = data->count;
    int                 trk;
    int                 i;
    int                 rd = ((cmd & 0x3) == 0x1) | ((cmd & 0x3) == 0x2);
    uint8               *rec;
    uint8               *da;
    uint8               ch;
    uint8               buf[8];

    /* Check if read or write command, if so grab correct cylinder */
    if (rd && data->cyl != data->ccyl) {
        uint32 tsize = data->tsize * disk_type[type].heads;
        if (uptr->CMD & DK_CYL_DIRTY) {
              (void)sim_fseek(uptr->fileref, data->cpos, SEEK_SET);
              (void)sim_fwrite(data->cbuf, 1, tsize, uptr->fileref);
              uptr->CMD &= ~DK_CYL_DIRTY;
        }
        data->ccyl = data->cyl;
        sim_debug(DEBUG_DETAIL, dptr, "Load unit=%d cyl=%d\n", unit, data->cyl);
        data->cpos = sizeof(struct dasd_header) + (data->ccyl * tsize);
        (void)sim_fseek(uptr->fileref, data->cpos, SEEK_SET);
        (void)sim_fread(data->cbuf, 1, tsize, uptr->fileref);
    }
    sim_debug(DEBUG_POS, dptr, "state unit=%d %02x %d\n", unit, state, data->tpos);

    rec = &data->cbuf[data->rpos + data->tstart];
    da = &data->cbuf[data->tpos + data->tstart];
    if (state != DK_POS_SEEK && data->tpos >= data->tsize) {
        sim_debug(DEBUG_POS, dptr, "state end unit=%d %d\n", unit, data->tpos);
        state = DK_POS_INDEX;
    }
    switch(state) {
    case DK_POS_INDEX:             /* At Index Mark */
         /* Read and multi-track advance to next head */
         if ((uptr->CMD & 0x83) == 0x82 || (uptr->CMD & 0x83) == 0x81) {
             sim_debug(DEBUG_DETAIL, dptr, "adv head unit=%d %02x %d %d %02x\n",
                   unit, state, data->tpos, uptr->CCH & 0xff, data->filemsk);
             if ((data->filemsk & DK_MSK_SK) == DK_MSK_SKNONE) {
                 sim_debug(DEBUG_DETAIL, dptr, "end cyl skmsk unit=%d %02x %d %02x\n",
                           unit, state, data->tpos, data->filemsk);
                 uptr->SNS = (SNS_WRP << 8);
                 uptr->CMD &= ~0xff;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 goto index;
             }
             uptr->CCH ++;
             if ((uptr->CCH & 0xff) >= disk_type[type].heads) {
endcyl:
                 sim_debug(DEBUG_DETAIL, dptr, "end cyl unit=%d %02x %d\n",
                           unit, state, data->tpos);
                 uptr->SNS = (SNS_ENDCYL << 8);
                 data->tstart = 0;
                 uptr->CCH &= ~0xff;
                 uptr->CMD &= ~0xff;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 goto index;
             }
             if ((uptr->CMD & 0x7) == 1 && (uptr->CMD & 0x60) != 0)
                 uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
         }
         /* If INDEX set signal no record if read */
         if ((cmd & 0x03) == 0x01 && uptr->CMD & DK_INDEX2) {
             sim_debug(DEBUG_DETAIL, dptr, "index unit=%d %02x %d %04x\n",
                   unit, state, data->tpos, uptr->SNS);
             /* Unless command is Read Header, return No record found */
             if (cmd != DK_RD_HA)
                 uptr->SNS |= (SNS_NOREC << 8);
             uptr->CMD &= ~0xff;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
         /* Check for overflow record */
         if (data->ovfl) {
             data->ovfl = 0;
             if (uptr->CMD & DK_OVFLOW) {
                  uptr->CCH ++;
                  sim_debug(DEBUG_DETAIL, dptr, "over unit=%d %04x\n", unit, uptr->CCH);
                  uptr->CMD &= ~(DK_INDEX);
                  if ((uptr->CCH & 0xff) >= disk_type[type].heads)
                      goto endcyl;
             }
         }
index:
         uptr->CMD |= (uptr->CMD & DK_INDEX) ? DK_INDEX2 : DK_INDEX;
         uptr->CMD &= ~DK_SRCOK;
         data->tstart = data->tsize * (uptr->CCH & 0xff);
         data->tpos = data->rpos = 0;
         data->state = DK_POS_HA;
         data->rec = 0;
         sim_activate(uptr, 50);
         break;

    case DK_POS_HA:                /* In home address (c) */
         data->tpos++;
         if (data->count == 4) {
             data->tpos = data->rpos = 5;
             data->state = DK_POS_CNT;
             sim_debug(DEBUG_POS, dptr, "state HA unit=%d %d %d\n", unit, data->count,
                      data->tpos);
             rec = &data->cbuf[data->rpos + data->tstart];
             /* Check for end of track */
             if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff)
                data->state = DK_POS_END;
             sim_activate(uptr, 10);
         } else
             sim_activate(uptr, 1);
         break;
    case DK_POS_CNT:               /* In count (c) */
         data->tpos++;
         if (data->count == 0) {
             /* Check for end of track */
             if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff) {
                state = DK_POS_END;
                data->state = DK_POS_END;
                if (data->ovfl == 0) {
                    data->klen = rec[5];
                    data->dlen = (rec[6] << 8) | rec[7];
                }
                sim_debug(DEBUG_POS, dptr, "state end tr unit=%d\n", unit);
             } else {
                if (rec[0] & 0x80)
                   data->ovfl = 1;
                else
                   data->ovfl = 0;
                data->klen = rec[5];
                data->dlen = (rec[6] << 8) | rec[7];
                sim_debug(DEBUG_POS, dptr,
                         "state count unit=%d r=%d k=%d d=%d %d\n",
                         unit, data->rec, data->klen, data->dlen, data->tpos);
             }
         }
         if (data->count == 7) {
             data->state = DK_POS_KEY;
             if (data->klen == 0)
                 data->state = DK_POS_DATA;
             sim_activate(uptr, 10);
         } else {
             sim_activate(uptr, 1);
         }
         break;
    case DK_POS_KEY:               /* In Key area */
         data->tpos++;
         if (data->count == data->klen) {
             sim_debug(DEBUG_POS, dptr, "state key unit=%d %d %d\n", unit, data->rec,
                      data->count);
             data->state = DK_POS_DATA;
             data->count = 0;
             count = 0;
             state = DK_POS_DATA;
             sim_activate(uptr, 10);
         } else {
             sim_activate(uptr, 1);
         }
         break;
    case DK_POS_DATA:              /* In Data area */
         data->tpos++;
         if (data->count == data->dlen) {
             sim_debug(DEBUG_POS, dptr, "state data unit=%d %d %d\n", unit, data->rec,
                      data->count);
             data->state = DK_POS_AM;
             sim_activate(uptr, 10);
         } else {
             sim_activate(uptr, 1);
         }
         break;
    case DK_POS_AM:                /* Beginning of record */
         data->rpos += data->dlen + data->klen + 8;
         data->tpos = data->rpos;
         data->rec++;
         sim_debug(DEBUG_POS, dptr, "state am unit=%d %d %d\n", unit, data->rec,
                data->count);
         data->state = DK_POS_CNT;
         data->count = 0;
         rec = &data->cbuf[data->rpos + data->tstart];
         /* Check for end of track */
         if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff)
            data->state = DK_POS_END;
         sim_activate(uptr, 20);
         break;
    case DK_POS_END:               /* Past end of data */
         data->tpos+=10;
         data->count = 0;
         data->klen = 0;
         data->dlen = 0;
         sim_activate(uptr, 50);
         break;
    case DK_POS_SEEK:                  /* In seek */
         /* Compute delay based of difference. */
         /* Set next state = index */
         i = (uptr->CCH >> 8) - data->cyl;
         sim_debug(DEBUG_DETAIL, dptr, "seek unit=%d %d %d s=%x\n", unit, uptr->CCH >> 8, i,
                 data->state);
         if (i == 0) {
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             data->state = DK_POS_INDEX;
             sim_activate(uptr, 20);
         } else if (i > 0 ) {
             if (i > 20) {
                data->cyl += 20;
                sim_activate(uptr, 1000);
             } else {
                data->cyl ++;
                sim_activate(uptr, 200);
             }
         } else {
             if (i < -20) {
                data->cyl -= 20;
                sim_activate(uptr, 1000);
             } else {
                data->cyl --;
                sim_activate(uptr, 200);
             }
         }
         sim_debug(DEBUG_DETAIL, dptr, "seek next unit=%d %d %d %x\n", unit, uptr->CCH >> 8,
                data->cyl, data->state);
         break;
    }

    switch (cmd) {
    case 0:                               /* No command */
         break;
    case 0x14:
    case 0x34:
    case 0x4:                 /* Sense */
         ch = uptr->SNS & 0xff;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 1 %x\n", unit, ch);
         if (chan_write_byte(addr, &ch))
             goto sense_end;
         ch = (uptr->SNS >> 8) & 0xff;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 2 %x\n", unit, ch);
         if (chan_write_byte(addr, &ch))
             goto sense_end;
         ch = 0;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 3 %x\n", unit, ch);
         if (chan_write_byte(addr, &ch))
             goto sense_end;
         if (disk_type[type].sen_cnt > 6) {
             ch = (unit & 07) | ((~unit & 07) << 3);
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 4 %x\n", unit, ch);
             if (chan_write_byte(addr, &ch))
                 goto sense_end;
             ch = unit;
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 5 %x\n", unit, ch);
             if (chan_write_byte(addr, &ch))
                 goto sense_end;
             ch = (uptr->CCH >> 8) & 0xff;
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 6 %x\n", unit, ch);
             if (chan_write_byte(addr, &ch))
                 goto sense_end;
             ch = (uptr->CCH & 0x1f) | ((uptr->CCH & 0x10000) ? 0x40 : 0);
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 7 %x\n", unit, ch);
             if (chan_write_byte(addr, &ch))
                 goto sense_end;
             ch = 0;              /* Compute message code */
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 8 %x\n", unit, ch);
             if (chan_write_byte(addr, &ch))
                 goto sense_end;
             i = 8;
         } else {
             if (disk_type[type].dev_type == 0x11)
                 ch = 0xc8;
             else
                 ch = 0x40;
             if ((uptr->CCH >> 8) & SNS_ENDCYL)
                ch |= 4;
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 4 %x\n", unit, ch);
             if (chan_write_byte(addr, &ch))
                 goto sense_end;
             ch = unit;
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 5 %x\n", unit, ch);
             if (chan_write_byte(addr, &ch))
                 goto sense_end;
             i = 5;
         }
         ch = 0;
         for (; i < disk_type[type].sen_cnt; i++) {
             sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d %d %x\n", unit, i, ch);
             if (chan_write_byte(addr, &ch))
                 goto sense_end;
         }
sense_end:
         uptr->CMD &= ~(0xff|DK_INDEX|DK_INDEX2);
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case DK_SETSECT:
         /* Not valid for drives before 3330 */
         sim_debug(DEBUG_DETAIL, dptr, "setsector unit=%d\n", unit);
         if (disk_type[type].sen_cnt > 6) {
             if (chan_read_byte(addr, &ch)) {
                 sim_debug(DEBUG_DETAIL, dptr, "setsector rdr\n");
                 uptr->LCMD = 0;
                 uptr->CMD &= ~(0xff);
                 uptr->SNS |= SNS_CMDREJ;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             /* Treat as NOP */
             uptr->LCMD = cmd;
             uptr->CMD &= ~(0xff);
             chan_end(addr, SNS_DEVEND|SNS_CHNEND);
             sim_debug(DEBUG_DETAIL, dptr, "setsector %02x\n", ch);
             break;
          }
          /* Otherwise report as invalid command */
          uptr->LCMD = 0;
          uptr->CMD &= ~(0xff);
          uptr->SNS |= SNS_CMDREJ;
          chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
          break;

    case DK_SEEK:            /* Seek */
    case DK_SEEKCYL:         /* Seek Cylinder */
    case DK_SEEKHD:          /* Seek Head */

         /* If we are waiting on seek to finish, check if there yet. */
         if (uptr->CMD & DK_PARAM) {
             if ((uptr->CCH >> 8) == data->cyl) {
                 uptr->LCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 set_devattn(addr, SNS_CHNEND | SNS_DEVEND);
                 sim_debug(DEBUG_DETAIL, dptr, "seek end unit=%d %d %d %x\n", unit,
                      uptr->CCH >> 8, data->cyl, data->state);
              }
              break;
         }

         /* Check if seek valid */
         i = data->filemsk & DK_MSK_SK;
         if (i == DK_MSK_SKNONE) { /* No seeks allowed, error out */
             sim_debug(DEBUG_DETAIL, dptr, "seek unit=%d not allow\n", unit);
             uptr->LCMD = cmd;
             uptr->CMD &= ~(0xff);
             uptr->SNS |= SNS_WRP << 8;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }

         if (i != DK_MSK_SKALLSKR) { /* Some restrictions */
             if ((cmd == DK_SEEKHD && i == DK_MSK_SKNONE) || 
                 (cmd == DK_SEEKCYL && (i & DK_MSK_SKALLHD) != 0) ||
                 (cmd == DK_SEEK)) {
                 sim_debug(DEBUG_DETAIL, dptr, "seek unit=%d not allow\n", unit);
                 uptr->LCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 uptr->SNS |= SNS_WRP << 8;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
         }

         /* Read in 6 character seek code */
         for (i = 0; i < 6; i++) {
             if (chan_read_byte(addr, &buf[i])) {
                 uptr->LCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 uptr->SNS |= SNS_CMDREJ|SNS_SEEKCK;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
         }
         sim_debug(DEBUG_DETAIL, dptr,
             "seek unit=%d %02x %02x %02x %02x %02x %02x\n", unit,
              buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
         trk = (buf[2] << 8) | buf[3];
         sim_debug(DEBUG_DETAIL, dptr, "seek unit=%d %d %d\n", unit, trk, buf[5]);

         /* Check if seek valid */
         if ((buf[0] | buf[1] | buf[4]) != 0 || trk > disk_type[type].cyl
                  || buf[5] >= disk_type[type].heads)  {
             uptr->LCMD = cmd;
             uptr->CMD &= ~(0xff);
             uptr->SNS |= SNS_CMDREJ|SNS_SEEKCK;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }

         if (cmd == DK_SEEKHD && ((uptr->CCH >> 8) & 0x7fff) != trk) {
             uptr->LCMD = cmd;
             uptr->CMD &= ~(0xff);
             uptr->SNS |= SNS_CMDREJ|SNS_SEEKCK;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }

         uptr->CCH = (trk << 8) | buf[5];

         /* Check if on correct cylinder */
         if (trk != data->cyl) {
             /* Do seek */
             uptr->CMD |= DK_PARAM;
             data->state = DK_POS_SEEK;
             sim_debug(DEBUG_DETAIL, dptr, "seek unit=%d doing\n", unit);
             chan_end(addr, SNS_CHNEND);
         } else {
             dasd_adjpos(uptr);
             uptr->LCMD = cmd;
             uptr->CMD &= ~(0xff);
             chan_end(addr, SNS_DEVEND|SNS_CHNEND);
         }
         return SCPE_OK;

    case DK_RESTORE:         /* Restore */

         /* If we are waiting on seek to finish, check if there yet. */
         if (uptr->CMD & DK_PARAM) {
             if ((uptr->CCH >> 8) == data->cyl) {
                 uptr->LCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 set_devattn(addr, SNS_DEVEND);
                 sim_debug(DEBUG_DETAIL, dptr, "seek end unit=%d %d %d %x\n", unit,
                          uptr->CCH >> 8, data->cyl, data->state);
              }
              break;
         }

         sim_debug(DEBUG_DETAIL, dptr, "restore unit=%d\n", unit);
         /* Check if restore is valid */
         if ((data->filemsk & DK_MSK_SK) != DK_MSK_SKALLSKR) {
             uptr->SNS |= SNS_CMDREJ;
             uptr->LCMD = 0;
             uptr->CMD &= ~(0xff|DK_PARAM);
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
         uptr->CCH = 0;
         data->tstart = 0;
         /* Check if on correct cylinder */
         if (0 != data->cyl) {
             /* Do seek */
             uptr->CMD |= DK_PARAM;
             data->state = DK_POS_SEEK;
             chan_end(addr, SNS_CHNEND);
         } else {
             uptr->LCMD = cmd;
             uptr->CMD &= ~(0xff);
             chan_end(addr, SNS_DEVEND|SNS_CHNEND);
         }
         return SCPE_OK;

    case DK_SETMSK:          /* Set file mask */
         /* If mask already set, error */
         sim_debug(DEBUG_DETAIL, dptr, "setmsk unit=%d\n", unit);
         uptr->LCMD = cmd;
         uptr->CMD &= ~(0xff|DK_PARAM);
         if (uptr->CMD & DK_MSET) {
             sim_debug(DEBUG_DETAIL, dptr, "setmsk dup\n");
             uptr->LCMD = 0;
             uptr->SNS |= SNS_CMDREJ | (SNS_INVSEQ << 8);
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
         /* Grab mask */
         if (chan_read_byte(addr, &ch)) {
             sim_debug(DEBUG_DETAIL, dptr, "setmsk rdr\n");
             uptr->LCMD = 0;
             uptr->SNS |= SNS_CMDREJ;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }
         /* Save */
         if (disk_type[type].dev_type >= 0x30) {
             /* Clear bits which have no meaning in simulator */
             ch &= 0xFC;
         }
         if ((ch & ~(DK_MSK_SK|DK_MSK_WRT)) != 0) {
             sim_debug(DEBUG_DETAIL, dptr, "setmsk inv\n");
             uptr->LCMD = 0;
             uptr->SNS |= SNS_CMDREJ;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }
         sim_debug(DEBUG_DETAIL, dptr, "setmsk unit=%d %x\n", unit, ch);
         data->filemsk = ch;
         uptr->CMD |= DK_MSET;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;


    case DK_SRCH_HAEQ:       /* Search HA equal */

         /* Wait until home address is found */
         if (state == DK_POS_HA && count == 0) {
             sim_debug(DEBUG_DETAIL, dptr, "search HA unit=%d %x %d %x\n",
                  unit, state, count, uptr->CCH);
             uptr->CMD &= ~DK_SRCOK;
             uptr->CMD |= DK_PARAM;
             break;
         }

         /* In home address, do compare */
         if (uptr->CMD & DK_PARAM) {
             if (chan_read_byte(addr, &ch)) {
                  if (count < 4)
                      uptr->CMD |= DK_SHORTSRC;
             } else if (ch != *da) {
                  uptr->CMD |= DK_NOEQ;
             }
             sim_debug(DEBUG_DETAIL, dptr,
                 "search HA unit=%d %d %x %02x=%02x %d\n", unit,
                 count, state, ch, *da, data->tpos);
             /* At end of count */
             if (count == 4 || uptr->CMD & DK_SHORTSRC) {
                uptr->LCMD = cmd;
                uptr->CMD &= ~(0xff|DK_PARAM);
                if (uptr->CMD & DK_NOEQ)
                    chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                else {
                    uptr->CMD |= DK_SRCOK;
                    chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_SMS);
                }
             }
         }
         break;

    case DK_RD_CNT:          /* Read count */
         /* Wait for next address mark */
         if (state == DK_POS_AM)
             uptr->CMD |= DK_PARAM;

         /* When we are at count segment and passed address mark */
         if (uptr->CMD & DK_PARAM && state == DK_POS_CNT && data->rec != 0) {
             ch = *da;
             sim_debug(DEBUG_DETAIL, dptr, "readcnt ID unit=%d %d %x %02x %x %d %x\n",
                 unit, count, state, ch, uptr->CCH, data->tpos, uptr->CCH);
             if (count == 0)   /* Mask off overflow bit */
                ch &= 0x7f;
             if (chan_write_byte(addr, &ch) || count == 7) {
                uptr->LCMD = cmd;
                uptr->CMD &= ~(0xff);
                chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             }
         }
         break;

    case DK_SRCH_IDEQ:       /* Search ID equal */
    case DK_SRCH_IDGT:       /* Search ID greater */
    case DK_SRCH_IDGE:       /* Search ID greater or equal */
         /* Wait for beginning of count segment */
         if (state == DK_POS_CNT && count == 0) {
             sim_debug(DEBUG_DETAIL, dptr, "search ID unit=%d %x %d %x %d\n",
                           unit, state, count, uptr->CCH, data->rec);
             sim_debug(DEBUG_DETAIL, dptr, "ID unit=%d %02x %02x %02x %02x %02x %02x %02x %02x\n",
                 unit, da[0], da[1], da[2], da[3], da[4], da[5], da[6], da[7]);
             uptr->CMD &= ~(DK_SRCOK|DK_SHORTSRC|DK_NOEQ|DK_HIGH);
             uptr->CMD |= DK_PARAM;
         }

         /* In count segment */
         if (uptr->CMD & DK_PARAM) {
             /* Wait for start of record */
             if (chan_read_byte(addr, &ch)) {
                  uptr->CMD |= DK_SHORTSRC;
             } else {
                  uint8   chx = *da;
                  if (count == 0)   /* Mask off overflow bit */
                     chx &= 0x7f;
                  if (ch != chx) {
                      if ((uptr->CMD & DK_NOEQ) == 0) {
                          uptr->CMD |= DK_NOEQ;
                          if (ch < chx)
                             uptr->CMD |= DK_HIGH;
                      }
                  }
             }
             sim_debug(DEBUG_DETAIL, dptr,
                  "search ID unit=%d %d %x %02x=%02x %d %c %c\n", unit, count,
                      state, ch, *da, data->tpos,
                         ((uptr->CMD & DK_NOEQ) ? '!' : '='),
                         ((uptr->CMD & DK_HIGH) ? 'h' : 'l'));
             if (count == 4 || uptr->CMD & DK_SHORTSRC) {
                 uptr->LCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 i = 0;
                 if ((cmd & 0x20) && (uptr->CMD & DK_NOEQ) == 0)
                      i = SNS_SMS;
                 if ((cmd & 0x40) && (uptr->CMD & DK_HIGH))
                      i = SNS_SMS;
                 if (i) {
                     uptr->CMD |= DK_SRCOK;
                 }
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|i);
             }
         }
         break;

    case DK_SRCH_KYEQ:       /* Search Key equal */
    case DK_SRCH_KYGT:       /* Search Key greater */
    case DK_SRCH_KYGE:       /* Search Key greater or equal */
         /* Check if at beginning of key */
         if (state == DK_POS_KEY && count == 0) {
             /* Check proper sequence */
                sim_debug(DEBUG_DETAIL, dptr, "search Key cn unit=%d %x %d %x %d %x\n",
                          unit, state, count, uptr->CCH, data->rec, uptr->LCMD);
             if (uptr->LCMD == DK_RD_CNT || uptr->LCMD == 0x100
                 || ((uptr->LCMD & 0x1F) == 0x11 && data->rec != 0)
                 || ((uptr->LCMD & 0x1F) == 0x11 && /* Search ID */
               (uptr->CMD & (DK_SRCOK|DK_SHORTSRC)) == DK_SRCOK))  {
                uptr->CMD &= ~(DK_SRCOK|DK_SHORTSRC|DK_NOEQ|DK_HIGH);
                uptr->CMD |= DK_PARAM;
             }
         }
         /* Check if previous record had zero length key */
         if (state == DK_POS_DATA && count == 0 && data->klen == 0) {
             if (uptr->LCMD == DK_RD_CNT || ((uptr->LCMD & 0x1F) == 0x11 &&
               (uptr->CMD & (DK_SRCOK|DK_SHORTSRC)) == DK_SRCOK ))  {
                sim_debug(DEBUG_DETAIL, dptr, "search Key da unit=%d %x %d %x %d\n",
                          unit, state, count, uptr->CCH, data->rec);
                 uptr->LCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                 break;
             }
         }
         /* If we hit address mark, see if over */
         if (state == DK_POS_AM) {
             if (uptr->LCMD == DK_RD_CNT || ((uptr->LCMD & 0x1F) == 0x11 &&
               (uptr->CMD & (DK_SRCOK|DK_SHORTSRC)) == DK_SRCOK ))  {
                sim_debug(DEBUG_DETAIL, dptr, "search Key am unit=%d %x %d %x %d\n",
                          unit, state, count, uptr->CCH, data->rec);
                 uptr->LCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                 break;
             } else {
                 uptr->LCMD = 0x100;
             }
         }
         if (uptr->CMD & DK_PARAM) {
         /* Wait for key */
             if (chan_read_byte(addr, &ch)) {
                  uptr->CMD |= DK_SHORTSRC;
             } else if (ch != *da) {
                  if ((uptr->CMD & DK_NOEQ) == 0) {
                      uptr->CMD |= DK_NOEQ;
                      if (ch < *da)
                         uptr->CMD |= DK_HIGH;
                  }
             }
             sim_debug(DEBUG_DETAIL, dptr,
                  "search Key unit=%d %d %x %02x=%02x %d %c %c\n", unit, count,
                      state, ch, *da, data->tpos,
                         ((uptr->CMD & DK_NOEQ) ? '!' : '='),
                         ((uptr->CMD & DK_HIGH) ? 'h' : 'l'));
             if (count == data->klen-1 || uptr->CMD & DK_SHORTSRC) {
                 uptr->LCMD = cmd;
                 uptr->CMD &= ~(0xff);
                 i = 0;
                 if ((cmd & 0x20) && (uptr->CMD & DK_NOEQ) == 0)
                     i = SNS_SMS;
                 if ((cmd & 0x40) && (uptr->CMD & DK_HIGH))
                     i = SNS_SMS;
                 if (i) {
                     uptr->CMD |= DK_SRCOK;
                 }
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|i);
             }
         }
         break;

    case DK_RD_HA:           /* Read home address */
         /* Wait until next index pulse */
         if (state == DK_POS_INDEX) {
             uint8 *dax = &data->cbuf[data->tstart];
             sim_debug(DEBUG_DETAIL, dptr, "RD HA f unit=%d %02x %02x %02x %02x %02x\n",
                    unit, dax[0], dax[1], dax[2], dax[3], dax[4]);
             uptr->CMD |= DK_PARAM;
         }

         /* Read while we are in the home address */
         if (uptr->CMD & DK_PARAM && state == DK_POS_HA) {
             ch = *da;
             if (chan_write_byte(addr, &ch) || count == 4) {
                uptr->LCMD = cmd;
                uptr->CMD &= ~(0xff);
                chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             }
         }
         break;

    case DK_RD_IPL:          /* Read IPL record */

         /* If we are not on cylinder zero, issue a seek */
         if (uptr->CCH != 0) {
             /* Do a seek */
             uptr->CCH = 0;
             data->tstart = 0;
             data->state = DK_POS_SEEK;
             sim_debug(DEBUG_DETAIL, dptr, "RD IPL unit=%d seek\n", unit);
             break;
         }

         /* Wait for seek to finish */
         if (data->cyl != 0)
             break;

         /* Read in the first record on track zero */
         if (count == 0 && state == DK_POS_DATA && data->rec == 1) {
             uptr->CMD |= DK_PARAM;
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             sim_debug(DEBUG_DETAIL, dptr, "RD IPL unit=%d %d k=%d d=%d %02x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state,
                 8 + data->klen + data->dlen);
         }
         goto rd;

    case DK_RD_R0:           /* Read R0 */
         /* Wait for record zero count */
         if (count == 0 && state == DK_POS_CNT && data->rec == 0) {
             uptr->CMD |= DK_PARAM;
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             sim_debug(DEBUG_DETAIL, dptr, "RD R0 unit=%d %d k=%d d=%d %02x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state,
                 8 + data->klen + data->dlen);
         }
         goto rd;

    case DK_RD_CKD:          /* Read count, key and data */
         if (uptr->CMD & DK_OVFLOW) {
             if (count == 0 && state == DK_POS_DATA && data->rec != 0) {
                uptr->CMD |= DK_PARAM;
                uptr->CMD &= ~DK_OVFLOW;
                sim_debug(DEBUG_DETAIL, dptr, "RD CKD ov unit=%d %d k=%d d=%d %02x %04x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen);
              }
              if (data->ovfl == 0 && state == DK_POS_END) {
                uptr->CMD |= DK_PARAM;
                uptr->CMD &= ~DK_OVFLOW;
                sim_debug(DEBUG_DETAIL, dptr, "RD CKD ov end unit=%d %d k=%d d=%d %02x %04x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen);
              }
              goto rd;
         }

         /* Wait for any count */
         if (count == 0 && state == DK_POS_CNT && data->rec != 0) {
             uptr->CMD |= DK_PARAM;
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             sim_debug(DEBUG_DETAIL, dptr, "RD CKD unit=%d %d k=%d d=%d %02x %04x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen);
         }
         goto rd;

    case DK_RD_KD:           /* Read key and data */
         if (uptr->CMD & DK_OVFLOW) {
             if (count == 0 && state == DK_POS_DATA && data->rec != 0) {
                uptr->CMD |= DK_PARAM;
                uptr->CMD &= ~DK_OVFLOW;
                sim_debug(DEBUG_DETAIL, dptr, "RD KD ov unit=%d %d k=%d d=%d %02x %04x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen);
              }
              if (data->ovfl == 0 && state == DK_POS_END) {
                uptr->CMD |= DK_PARAM;
                uptr->CMD &= ~DK_OVFLOW;
                sim_debug(DEBUG_DETAIL, dptr, "RD CKD ov end unit=%d %d k=%d d=%d %02x %04x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen);
              }
              goto rd;
         }
         /* Wait for next key */
         if (count == 0 && ((data->klen != 0 && state == DK_POS_KEY) ||
                            (data->klen == 0 && state == DK_POS_DATA))) {
             if ((uptr->CMD & DK_INDEX) && data->rec == 0 &&
                 (uptr->CMD & DK_SRCOK) == 0)
                 break;
             uptr->CMD |= DK_PARAM;
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             sim_debug(DEBUG_DETAIL, dptr, "RD KD unit=%d %d k=%d d=%d %02x %04x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen);
         }
         goto rd;

    case DK_RD_D:            /* Read Data */
         if (uptr->CMD & DK_OVFLOW) {
             if (count == 0 && state == DK_POS_DATA && data->rec != 0) {
                uptr->CMD |= DK_PARAM;
                uptr->CMD &= ~DK_OVFLOW;
                sim_debug(DEBUG_DETAIL, dptr, "RD D ov unit=%d %d k=%d d=%d %02x %04x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen);
              }
              if (data->ovfl == 0 && state == DK_POS_END) {
                uptr->CMD |= DK_PARAM;
                uptr->CMD &= ~DK_OVFLOW;
                sim_debug(DEBUG_DETAIL, dptr, "RD CKD ov end unit=%d %d k=%d d=%d %02x %04x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen);
              }
              goto rd;
         }

         /* Wait for next data */
         if (count == 0 && state == DK_POS_DATA) {
             /* Skip R0 */
             if ((uptr->CMD & DK_INDEX) && data->rec == 0 &&
                 (uptr->CMD & DK_SRCOK) == 0)
                 break;
             uptr->CMD |= DK_PARAM;
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             sim_debug(DEBUG_DETAIL, dptr,
                 "RD D unit=%d %d k=%d d=%d %02x %04x %04x %d\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen, count);
         }

rd:
         if (uptr->CMD & DK_PARAM) {
             /* Check for end of file */
             if (state == DK_POS_DATA && data->dlen == 0) {
                 sim_debug(DEBUG_DETAIL, dptr, "RD EOF unit=%d %x %d %d d=%d\n",
                          unit, state, count, data->rec, data->dlen);
                uptr->CMD &= ~(0xff|DK_PARAM);
                chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                break;
             }
             if (state == DK_POS_INDEX) {
                 uptr->SNS = SNS_TRKOVR << 8;
                 uptr->CMD &= ~(0xff|DK_PARAM|DK_OVFLOW);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (state == DK_POS_DATA && count == data->dlen) {
                 sim_debug(DEBUG_DETAIL, dptr,
                     "RD next unit=%d %02x %02x %02x %02x %02x %02x %02x %02x\n",
                     unit, da[0], da[1], da[2], da[3], da[4], da[5], da[6], da[7]);
                 if (data->ovfl == 0) {
                     uptr->CMD &= ~(0xff|DK_PARAM|DK_OVFLOW);
                     chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                 } else {
                     uptr->CMD &= ~(DK_PARAM);  /* Start a new search */
                     uptr->CMD |= DK_OVFLOW;
                 }
                 break;
             }
             ch = *da;
             if (state == DK_POS_CNT && count == 0) /* Mask off overflow bit */
                ch &= 0x7f;
             sim_debug(DEBUG_DATA, dptr, "RD Char %02x %02x %d %d\n",
                    ch, state, count, data->tpos);
             if (chan_write_byte(addr, &ch)) {
                 sim_debug(DEBUG_DETAIL, dptr,
                     "RD next unit=%d %02x %02x %02x %02x %02x %02x %02x %02x\n",
                     unit, da[0], da[1], da[2], da[3], da[4], da[5], da[6], da[7]);
                 uptr->CMD &= ~(0xff|DK_PARAM);
                 data->ovfl = 0;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             }
         }
         break;

    case DK_RD_SECT:         /* Read sector */
         /* Not valid for drives before 3330 */
         sim_debug(DEBUG_DETAIL, dptr, "readsector unit=%d\n", unit);
         uptr->LCMD = 0;
         if (disk_type[type].sen_cnt > 6) {
             ch = data->tpos / 110;
             if (chan_write_byte(addr, &ch)) {
                 sim_debug(DEBUG_DETAIL, dptr, "readsector rdr\n");
                 uptr->SNS |= SNS_CMDREJ;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             } else {
                 /* Nothing more to do */
                 uptr->LCMD = cmd;
                 chan_end(addr, SNS_DEVEND|SNS_CHNEND);
                 sim_debug(DEBUG_DETAIL, dptr, "readsector %02x\n", ch);
             }
         } else {
             /* Otherwise report as invalid command */
             uptr->SNS |= SNS_CMDREJ;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
         uptr->CMD &= ~(0xff);
         break;

    case DK_WR_HA:           /* Write home address */
         /* Wait for index */
         if (state == DK_POS_INDEX) {
             uint8 *dax = &data->cbuf[data->tstart];
             sim_debug(DEBUG_DETAIL, dptr, "WR HA unit=%d %02x %02x %02x %02x %02x\n",
                    unit, dax[0], dax[1], dax[2], dax[3], dax[4]);
             /* Check if command ok based on mask */
             if ((data->filemsk & DK_MSK_WRT) != DK_MSK_ALLWRT) {
                 uptr->SNS |= SNS_CMDREJ;
                 uptr->LCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }

             uptr->CMD |= DK_PARAM;
             break;
         }

         if (uptr->CMD & DK_PARAM) {
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             sim_debug(DEBUG_DETAIL, dptr, "WR HA unit=%d %x %d %d\n", unit,
                         state, count, data->rec);
             if (chan_read_byte(addr, &ch)) {
                 ch = 0;
             }
             *da = ch;
             uptr->CMD |= DK_CYL_DIRTY;
             if (count == 4) {
                  uint8 *dax = &data->cbuf[data->tstart];
                  ch = 0;
                  /* Check if we wrote all zeros, */
                  for (i = 0; i < 5; i++)
                     ch |= dax[i];
                  if (ch == 0) {  /* If we did reset HA to correct value */
                     dax[0] = 0;
                     dax[1] = (uptr->CCH >> 16) & 0xff;
                     dax[2] = (uptr->CCH >>  8) & 0xff;
                     dax[3] = 0;
                     dax[4] = uptr->CCH & 0xff;
                  }
             sim_debug(DEBUG_DETAIL, dptr, "WR HA f unit=%d %02x %02x %02x %02x %02x\n",
                    unit, dax[0], dax[1], dax[2], dax[3], dax[4]);
                  uptr->LCMD = cmd;
                  uptr->CMD &= ~(0xff|DK_PARAM);
                  /* Write end of track marker */
                  for(i = 1; i < 9; i++)
                     da[i] = 0xff;
                  chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             }
         }
         break;

    case DK_WR_R0:           /* Write R0 */

         /* Wait for first record or end of disk */
         if ((state == DK_POS_CNT || state == DK_POS_END)
                  && data->rec == 0 && count == 0) {
             sim_debug(DEBUG_DETAIL, dptr, "WR R0 unit=%d %x %d\n", unit,
                   state, count);
             /* Check if command ok based on mask */
             if ((data->filemsk & DK_MSK_WRT) != DK_MSK_ALLWRT) {
                 uptr->SNS |= SNS_CMDREJ | (SNS_WRP << 8);
                 uptr->LCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (uptr->LCMD == DK_WR_HA ||
                (uptr->LCMD == DK_SRCH_HAEQ &&
                     (uptr->CMD & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 data->tpos = data->rpos;
                 da = &data->cbuf[data->tpos + data->tstart];
                 data->tpos++;
                 state = data->state = DK_POS_CNT;
                 uptr->CMD |= DK_PARAM;
             } else {
                 uptr->SNS |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->LCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }
         goto wrckd;

    case DK_WR_SCKD:         /* Write special count, key and data */
    case DK_WR_CKD:          /* Write count, key and data */
         /* Wait for next non-zero record, or end of disk */
         if ((state == DK_POS_CNT || state == DK_POS_END)
                  && data->rec != 0 && count == 0) {
             sim_debug(DEBUG_DETAIL, dptr, "WR %s unit=%d %x %d\n",
                      (cmd == DK_WR_SCKD)? "SCKD": "CKD", unit, state, count);
             /* Check if command ok based on mask */
             i = data->filemsk & DK_MSK_WRT;
             if (i == DK_MSK_INHWRT || i == DK_MSK_ALLWRU) {
                 sim_debug(DEBUG_DETAIL, dptr, "WR CKD unit=%d mask\n", unit);
                 uptr->SNS |= SNS_CMDREJ | (SNS_WRP << 8);
                 uptr->LCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (uptr->LCMD == DK_WR_R0 || uptr->LCMD == DK_WR_CKD || uptr->LCMD == DK_WR_SCKD ||
                ((uptr->LCMD & 0x7) == 1 && (uptr->LCMD & 0x60) != 0 &&
                     (uptr->CMD & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 sim_debug(DEBUG_DETAIL, dptr, "WR CKD unit=%d ok\n", unit);
                 data->tpos = data->rpos;
                 da = &data->cbuf[data->tpos + data->tstart];
                 data->tpos++;
                 state = data->state = DK_POS_CNT;
                 uptr->CMD |= DK_PARAM;
                 uptr->CMD &= ~DK_DONE;
             } else {
                 sim_debug(DEBUG_DETAIL, dptr, "WR CKD unit=%d seq\n", unit);
                 uptr->SNS |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->LCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }
         goto wrckd;

    case DK_WR_KD:           /* Write key and data */
         if (uptr->CMD & DK_OVFLOW) {
             if (count == 0 && state == DK_POS_DATA && data->rec != 0) {
                uptr->CMD |= DK_PARAM;
                uptr->CMD &= ~DK_OVFLOW;
                sim_debug(DEBUG_DETAIL, dptr, "WR KD ov unit=%d %d k=%d d=%d %02x %04x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen);
              }
              goto wrckd;
         }
         /* Wait for beginning of next key */
         if (count == 0 && ((data->klen != 0 && state == DK_POS_KEY) ||
                            (data->klen == 0 && state == DK_POS_DATA))) {
             /* Check if command ok based on mask */
             if ((data->filemsk & DK_MSK_WRT) == DK_MSK_INHWRT) {
                 uptr->SNS |= SNS_CMDREJ | (SNS_WRP << 8);
                 uptr->LCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (((uptr->LCMD & 0x13) == 0x11 &&
                     (uptr->CMD & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 uptr->CMD |= DK_PARAM;
                 uptr->CMD &= ~DK_DONE;
             sim_debug(DEBUG_DETAIL, dptr, "WR KD unit=%d %d k=%d d=%d %02x %04x %d\n",
                 unit, data->rec, data->klen, data->dlen, data->state,
                 8 + data->klen + data->dlen, count);
             } else {
                 uptr->SNS |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->LCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }
         goto wrckd;

    case DK_WR_D:            /* Write Data */
         if (uptr->CMD & DK_OVFLOW) {
             if (count == 0 && state == DK_POS_DATA && data->rec != 0) {
                uptr->CMD |= DK_PARAM;
                uptr->CMD &= ~DK_OVFLOW;
                sim_debug(DEBUG_DETAIL, dptr, "WR D ov unit=%d %d k=%d d=%d %02x %04x %04x\n",
                 unit, data->rec, data->klen, data->dlen, data->state, data->dlen,
                 8 + data->klen + data->dlen);
              }
              goto wrckd;
         }
         /* Wait for beginning of next data */
         if ((state == DK_POS_DATA) && count == 0) {
             /* Check if command ok based on mask */
             if ((data->filemsk & DK_MSK_WRT) == DK_MSK_INHWRT) {
                 uptr->SNS |= SNS_CMDREJ | (SNS_WRP << 8);
                 uptr->LCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (((uptr->LCMD & 0x3) == 1 && (uptr->LCMD & 0xE0) != 0 &&
                     (uptr->CMD & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 uptr->CMD |= DK_PARAM;
                 uptr->CMD &= ~DK_DONE;
             sim_debug(DEBUG_DETAIL, dptr, "WR D unit=%d %d k=%d d=%d %02x %04x %d\n",
                 unit, data->rec, data->klen, data->dlen, data->state,
                 8 + data->klen + data->dlen, count);
             } else {
                 uptr->SNS |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->LCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }

wrckd:
         if (uptr->CMD & DK_PARAM) {
             uptr->CMD &= ~(DK_INDEX|DK_INDEX2);
             if (state == DK_POS_INDEX) {
                 uptr->SNS = SNS_TRKOVR << 8;
                 uptr->CMD &= ~(0xff|DK_PARAM|DK_DONE);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             } else if ((cmd == DK_WR_KD || cmd == DK_WR_D) && state == DK_POS_DATA
                   && data->dlen == 0) {
                 sim_debug(DEBUG_DETAIL, dptr, "WR EOF unit=%d %x %d %d d=%d\n",
                            unit, state, count, data->rec, data->dlen);
                 uptr->CMD &= ~(0xff|DK_PARAM|DK_DONE);
                 uptr->LCMD = cmd;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                 break;
             } else if (state == DK_POS_DATA && data->count == data->dlen) {
                 if (cmd == DK_WR_HA || cmd == DK_WR_R0 || cmd == DK_WR_CKD || cmd == DK_WR_SCKD) {
                      /* Write end of track marker */
                      for(i = 0; i < 8; i++)
                         da[i] = 0xff;
                      sim_debug(DEBUG_DETAIL, dptr, "WCKD eot unit=%d\n",unit);
                      data->ovfl = 0;     /* Kill overflow if any */
                 }
                 if (data->ovfl == 0 || cmd == DK_WR_CKD || cmd == DK_WR_SCKD) {
                     uptr->LCMD = cmd;
                     uptr->CMD &= ~(0xff|DK_PARAM|DK_DONE);
                     chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                 } else {
                     uptr->CMD &= ~(DK_PARAM);  /* Start a new search */
                     uptr->CMD |= DK_OVFLOW;
                 }
                 sim_debug(DEBUG_DETAIL, dptr,
                           "WCKD end unit=%d %d %d %04x %x\n",
                          unit, data->tpos+8, count, data->tpos - data->rpos,
                           data->ovfl);
                 break;
             }
             if (uptr->CMD & DK_DONE || chan_read_byte(addr, &ch)) {
                 ch = 0;
                 uptr->CMD |= DK_DONE;
             }
             sim_debug(DEBUG_DATA, dptr, "Char %02x, %02x %d %d\n", ch, state,
                   count, data->tpos);
             if (state == DK_POS_CNT && count == 0 && cmd == DK_WR_SCKD)
                   ch |= 0x80; /* Set overflow flag */
             *da = ch;
             uptr->CMD |= DK_CYL_DIRTY;
             if (state == DK_POS_CNT && count == 7) {
//                 if (cmd == DK_WR_SCKD)
 //                    rec[0] |= 0x80; /* Set overflow flag */
                 data->klen = rec[5];
                 data->dlen = (rec[6] << 8) | rec[7];
                 sim_debug(DEBUG_DETAIL, dptr,
                     "WCKD count unit=%d %d k=%d d=%d %02x %04x - %02x\n",
                     unit, data->rec, data->klen, data->dlen, data->state,
                     8 + data->klen + data->dlen, rec[0]);
                 if (data->klen == 0)
                     data->state = DK_POS_DATA;
                 else
                     data->state = DK_POS_KEY;
                 data->count = 0;
             }
         }
         break;

    case DK_ERASE:           /* Erase to end of track */
         if ((state == DK_POS_AM || state == DK_POS_END) && data->count == 0) {
             sim_debug(DEBUG_DETAIL, dptr, "Erase unit=%d %d %d\n",
                     unit, data->rec, data->rpos);
             /* Check if command ok based on mask */
             i = data->filemsk & DK_MSK_WRT;
             if (i == DK_MSK_INHWRT || i == DK_MSK_ALLWRU) {
                 uptr->SNS |= SNS_CMDREJ;
                 uptr->LCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (uptr->LCMD == DK_WR_R0 || uptr->LCMD == DK_WR_CKD ||
                ((uptr->LCMD & 0x3) == 1 && (uptr->LCMD & 0x70) != 0 &&
                     (uptr->CMD & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 state = data->state = DK_POS_END;
                 /* Write end mark */
                 for(i = 0; i < 8; i++)
                    rec[i] = 0xff;

                 for(i = 0; i < 8; i++) {
                    if(chan_read_byte(addr, &ch))
                       break;
                 }
                 uptr->LCMD = cmd;
                 uptr->CMD &= ~(0xff|DK_PARAM|DK_INDEX|DK_INDEX2);
                 uptr->CMD |= DK_CYL_DIRTY;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             } else {
                 uptr->SNS |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->LCMD = 0;
                 uptr->CMD &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
         }

         break;

    case DK_NOP:
         sim_debug(DEBUG_DETAIL, dptr, "NOP=%d %x\n", unit, cmd);
         (void)chan_read_byte(addr, &ch);
         uptr->LCMD = 0;
         uptr->CMD &= ~(0xff);
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case DK_SPACE:           /* Space record */
         /* Not implemented yet */
    default:
         sim_debug(DEBUG_DETAIL, dptr, "invalid command=%d %x\n", unit, cmd);
         uptr->SNS |= SNS_CMDREJ;
         uptr->LCMD = 0;
         uptr->CMD &= ~(0xff);
         chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         break;
    }
    if (state == data->state)
        data->count++;
    else
        data->count = 0;
    return SCPE_OK;
}

void
dasd_ini(UNIT * uptr, t_bool f)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                i = GET_TYPE(uptr->flags);
    uptr->capac = disk_type[i].bpt * disk_type[i].heads * disk_type[i].cyl;
    if ((uptr->flags & UNIT_ATT) != 0)
        sim_activate(uptr, 100);
}

t_stat
dasd_reset(DEVICE * dptr)
{
    return SCPE_OK;
}


static uint8  ipl1rec[28] = {0xC9,0xD7,0xD3,0xF1,   /* IPL1 */
                             0x00,0x06,0x00,0x00,0x00,0x00,0x00,0x0F,
                             0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x01,
                             0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
static uint8  ipl2key[4] = {0xC9,0xD7,0xD3,0xF2};   /* IPL2 */
static uint8  volrec[84] = {0xE5,0xD6,0xD3,0xF1,    /* VOL1, key */
                            0xE5,0xD6,0xD3,0xF1,    /* VOL1 */
                            0xF1,0xF1,0xF1,0xF1,0xF1,0xF1,  /* volid */
                            0x40,0x00,0x00,0x00,0x01,0x01,  /* CCHHR */
                            0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
                            0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
                            0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,  
                            0x40,0xE2,0xC9,0xD4,0xC8,0x40,  /* SIMH */
                            0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
                            0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
                            0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40, 
                            0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
                            0x40,0x40};

int
dasd_format(UNIT * uptr, int flag) {
    struct dasd_header  hdr;
    struct dasd_t       *data;
    uint16              addr = GET_UADDR(uptr->CMD);
    int                 type = GET_TYPE(uptr->flags);
    int                 tsize;
    int                 cyl;
    int                 hd;
    int                 pos;

    if (flag || get_yn("Initialize dasd? [Y] ", TRUE)) {
        memset(&hdr, 0, sizeof(struct dasd_header));
        memcpy(&hdr.devid[0], "CKD_P370", 8);
        hdr.heads = disk_type[type].heads;
        hdr.tracksize = (disk_type[type].bpt | 0x1ff) + 1;
        hdr.devtype = disk_type[type].dev_type;
        (void)sim_fseek(uptr->fileref, 0, SEEK_SET);
        sim_fwrite(&hdr, 1, sizeof(struct dasd_header), uptr->fileref);
        if ((data = (struct dasd_t *)calloc(1, sizeof(struct dasd_t))) == 0)
            return 1;
        uptr->up7 = (void *)data;
        tsize = hdr.tracksize * hdr.heads;
        data->tsize = hdr.tracksize;
        if ((data->cbuf = (uint8 *)calloc(tsize, sizeof(uint8))) == 0)
            return 1;
        for (cyl = 0; cyl < disk_type[type].cyl; cyl++) {
            pos = 0;
            for (hd = 0; hd < disk_type[type].heads; hd++) {
                int cpos = pos;
                data->cbuf[pos++] = 0;            /* HA */
                data->cbuf[pos++] = (cyl >> 8);
                data->cbuf[pos++] = (cyl & 0xff);
                data->cbuf[pos++] = (hd >> 8);
                data->cbuf[pos++] = (hd & 0xff);
                data->cbuf[pos++] = (cyl >> 8);   /* R0 */
                data->cbuf[pos++] = (cyl & 0xff);
                data->cbuf[pos++] = (hd >> 8);
                data->cbuf[pos++] = (hd & 0xff);
                data->cbuf[pos++] = 0;              /* Rec */
                data->cbuf[pos++] = 0;              /* keylen */
                data->cbuf[pos++] = 0;              /* dlen */
                data->cbuf[pos++] = 8;              /*  */
                pos += 8;
                data->cbuf[pos++] = (cyl >> 8);   /* R1 */
                data->cbuf[pos++] = (cyl & 0xff);
                data->cbuf[pos++] = (hd >> 8);
                data->cbuf[pos++] = (hd & 0xff);
                data->cbuf[pos++] = 1;              /* Rec */
                if (cyl == 0 && hd == 0 && (sim_switches & SWMASK ('V')) != 0) {
                    int p;
                    /* R1, IPL1 */
                    data->cbuf[pos++] = 4;              /* keylen */
                    data->cbuf[pos++] = 0;              /* dlen */
                    data->cbuf[pos++] = 24;              /*  */
                    for (p = 0; p < sizeof (ipl1rec); p++) 
                        data->cbuf[pos++] = ipl1rec[p];
                    data->cbuf[pos++] = (cyl >> 8);   /* R2 */
                    data->cbuf[pos++] = (cyl & 0xff);
                    data->cbuf[pos++] = (hd >> 8);
                    data->cbuf[pos++] = (hd & 0xff);
                    data->cbuf[pos++] = 2;              /* Rec */
                    /* R2, IPL2 */
                    data->cbuf[pos++] = 4;              /* keylen */
                    data->cbuf[pos++] = 0;              /* dlen */
                    data->cbuf[pos++] = 144;            /*  */
                    for (p = 0; p < sizeof (ipl2key); p++) 
                        data->cbuf[pos++] = ipl2key[p];
                    pos += 144;
                    data->cbuf[pos++] = (cyl >> 8);   /* R3 */
                    data->cbuf[pos++] = (cyl & 0xff);
                    data->cbuf[pos++] = (hd >> 8);
                    data->cbuf[pos++] = (hd & 0xff);
                    data->cbuf[pos++] = 3;              /* Rec */
                    /* R3, VOL1 */
                    data->cbuf[pos++] = 4;              /* keylen */
                    data->cbuf[pos++] = 0;              /* dlen */
                    data->cbuf[pos++] = 80;             /*  */
                    for (p = 0; p < sizeof (volrec); p++) 
                        data->cbuf[pos++] = volrec[p];
                } else {
                    data->cbuf[pos++] = 0;              /* keylen */
                    data->cbuf[pos++] = 0;              /* dlen */
                    data->cbuf[pos++] = 0;              /*  */
                }
                data->cbuf[pos++] = 0xff;           /* End record */
                data->cbuf[pos++] = 0xff;
                data->cbuf[pos++] = 0xff;
                data->cbuf[pos++] = 0xff;
                pos = cpos + data->tsize;
            }
            sim_fwrite(data->cbuf, 1, tsize, uptr->fileref);
            if ((cyl % 10) == 0)
               fputc('.', stderr);
        }
        (void)sim_fseek(uptr->fileref, sizeof(struct dasd_header), SEEK_SET);
        (void)sim_fread(data->cbuf, 1, tsize, uptr->fileref);
        data->cpos = sizeof(struct dasd_header);
        data->ccyl = 0;
        data->ccyl = 0;
        set_devattn(addr, SNS_DEVEND);
        sim_activate(uptr, 100);
        fputc('\n', stderr);
        fputc('\r', stderr);
        return 0;
    } else
        return 1;
}

t_stat
dasd_attach(UNIT * uptr, CONST char *file)
{
    uint16              addr = GET_UADDR(uptr->CMD);
    int                 flag = (sim_switches & SWMASK ('I')) != 0;
    t_stat              r;
    int                 i;
    struct dasd_header  hdr;
    struct dasd_t       *data;
    int                 tsize;
    size_t              isize;
    size_t              dsize;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
       return r;

    if (sim_fread(&hdr, 1, sizeof(struct dasd_header), uptr->fileref) !=
          sizeof(struct dasd_header) || strncmp(&hdr.devid[0], "CKD_P370", 8) != 0 || flag) {
        if (dasd_format(uptr, flag)) {
            detach_unit(uptr);
            return SCPE_FMT;
        }
        return SCPE_OK;
    }

    isize = sim_fsize(uptr->fileref);
    sim_messagef(SCPE_OK, "Drive %03x=%d %d %02x %02x %d\n\r",  addr,
             hdr.heads, hdr.tracksize, hdr.devtype, hdr.fileseq, hdr.highcyl);
    for (i = 0; disk_type[i].name != 0; i++) {
         tsize = (disk_type[i].bpt | 0x1ff) + 1;
         dsize = 512 + (tsize * disk_type[i].heads * disk_type[i].cyl);
         if (hdr.devtype == disk_type[i].dev_type && hdr.tracksize == tsize &&
             hdr.heads == disk_type[i].heads && dsize == isize) {
             if (GET_TYPE(uptr->flags) != i) {
                  /* Ask if we should change */
                  fprintf(stderr, "Wrong type %s\n\r", disk_type[i].name);
                  if (!get_yn("Update dasd type? [N] ", FALSE)) {
                      detach_unit(uptr);
                      return SCPE_FMT;
                  }
                  uptr->flags &= ~UNIT_TYPE;
                  uptr->flags |= SET_TYPE(i);
                  uptr->capac = disk_type[i].bpt * disk_type[i].heads * disk_type[i].cyl;
             }
             break;
         }
    }
    if (disk_type[i].name == 0) {
         detach_unit(uptr);
         return SCPE_FMT;
    }
    if ((data = (struct dasd_t *)calloc(1, sizeof(struct dasd_t))) == 0)
        return 0;
    uptr->up7 = (void *)data;
    tsize = hdr.tracksize * hdr.heads;
    data->tsize = hdr.tracksize;
    if ((data->cbuf = (uint8 *)calloc(tsize, sizeof(uint8))) == 0) {
        detach_unit(uptr);
        return SCPE_ARG;
    }
    (void)sim_fseek(uptr->fileref, sizeof(struct dasd_header), SEEK_SET);
    (void)sim_fread(data->cbuf, 1, tsize, uptr->fileref);
    data->cpos = sizeof(struct dasd_header);
    data->ccyl = 0;
    set_devattn(addr, SNS_DEVEND);
    sim_activate(uptr, 100);
    return SCPE_OK;
}

t_stat
dasd_detach(UNIT * uptr)
{
    struct dasd_t       *data = (struct dasd_t *)uptr->up7;
    int                 type = GET_TYPE(uptr->flags);
    uint16              addr = GET_UADDR(uptr->CMD);
    int                 cmd = uptr->CMD & 0x7f;

    if (uptr->CMD & DK_CYL_DIRTY) {
        (void)sim_fseek(uptr->fileref, data->cpos, SEEK_SET);
        (void)sim_fwrite(data->cbuf, 1,
               data->tsize * disk_type[type].heads, uptr->fileref);
        uptr->CMD &= ~DK_CYL_DIRTY;
    }
    if (cmd != 0)
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
    sim_cancel(uptr);
    if (data)
        free(data->cbuf);
    free(data);
    uptr->up7 = 0;
    uptr->CMD &= ~0xffff;
    return detach_unit(uptr);
}

t_stat
dasd_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    struct dasd_t      *data = (struct dasd_t *)(uptr->up7);

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */
    data->filemsk = 0;
    return chan_boot(GET_UADDR(uptr->CMD), dptr);
}

/* Disk option setting commands */

t_stat
dasd_set_type(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int                 i;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    if (uptr->flags & UNIT_ATT)
        return SCPE_ALATT;
    for (i = 0; disk_type[i].name != 0; i++) {
        if (strcmp(disk_type[i].name, cptr) == 0) {
            uptr->flags &= ~UNIT_TYPE;
            uptr->flags |= SET_TYPE(i);
            uptr->capac = disk_type[i].bpt * disk_type[i].heads * disk_type[i].cyl;
            return SCPE_OK;
        }
    }
    return SCPE_ARG;
}

t_stat
dasd_setd_type(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int                 i, u;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    if (uptr->flags & UNIT_ATT)
        return SCPE_ALATT;
    for (i = 0; disk_type[i].name != 0; i++) {
        if (strcmp(disk_type[i].name, cptr) == 0) {
            for (u = 0; u < 8; u++) {
                uptr->flags &= ~UNIT_TYPE;
                uptr->flags |= SET_TYPE(i);
                uptr->capac = disk_type[i].bpt * disk_type[i].heads * disk_type[i].cyl;
                uptr++;
            }
            return SCPE_OK;
        }
    }
    return SCPE_ARG;
}

t_stat
dasd_get_type(FILE * st, UNIT * uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fputs("TYPE=", st);
    fputs(disk_type[GET_TYPE(uptr->flags)].name, st);
    return SCPE_OK;
}


t_stat dasd_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
    const char *cptr)
{
    int i;
    fprintf (st, "IBM 2840 Disk File Controller\n\n");
    fprintf (st, "Use:\n\n");
    fprintf (st, "    sim> SET %sn TYPE=type\n", dptr->name);
    fprintf (st, "Type can be: ");
    for (i = 0; disk_type[i].name != 0; i++) {
        fprintf(st, "%s", disk_type[i].name);
        if (disk_type[i+1].name != 0)
        fprintf(st, ", ");
    }
    fprintf (st, ".\nEach drive has the following storage capacity:\n\n");
    for (i = 0; disk_type[i].name != 0; i++) {
        int32 size = disk_type[i].bpt * disk_type[i].heads * disk_type[i].cyl;
        size /= 1024;
        size = (10 * size) / 1024;
        fprintf(st, "      %-8s %4d.%1dMB\n", disk_type[i].name, size/10, size%10);
    }
    fprintf (st, "Attach command switches\n");
    fprintf (st, "    -I          Initialize the drive. No prompting.\n");
    fprintf (st, "    -V          Adds in a volume label of 11111\n");
    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    return SCPE_OK;
}

const char *dasd_description (DEVICE *dptr)
{
    return "IBM 2840 disk file controller";
}

#endif
