/* ibm360_dasd.c: IBM 360 2311/2314 Disk controller

   Copyright (c) 2016, Richard Cornwell

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


/* u3 */
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
#define DK_MT              0x80       /* Multi track flag */

#define DK_INDEX           0x100      /* Index seen in command */
#define DK_NOEQ            0x200      /* Not equal compare */
#define DK_HIGH            0x400      /* High compare */
#define DK_PARAM           0x800      /* Parameter in u4 */
#define DK_MSET            0x1000     /* Mode set command already */
#define DK_SHORTSRC        0x2000     /* Last search was short */
#define DK_SRCOK           0x4000     /* Last search good */
#define DK_CYL_DIRTY       0x8000     /* Current cylinder dirty */

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


/* u4 */
/* Holds the current track and head */
#define DK_V_TRACK         8
#define DK_M_TRACK         0x3ff00    /* Max 1024 cylinders */
#define DK_V_HEAD          0
#define DK_M_HEAD          0xff       /* Max 256 heads */
#define DK_V_FILEMSK       18
#define DK_M_FILEMSK       0xFF       /* File mask */

/* u5 */
/* Sense byte 0 */
#define SNS_CMDREJ         0x01       /* Command reject */
#define SNS_INTVENT        0x02       /* Unit intervention required */
#define SNS_BUSCHK         0x04       /* Parity error on bus */
#define SNS_EQUCHK         0x08       /* Equipment check */
#define SNS_DATCHK         0x10       /* Data Check */
#define SNS_OVRRUN         0x20       /* Data overrun */
#define SNS_TRKCND         0x40       /* Track Condition */
#define SNS_SEEKCK         0x80       /* Seek Check */

/* Sense byte 1 */
#define SNS_DCCNT          0x01       /* Data Check Count */
#define SNS_TRKOVR         0x02       /* Track Overrun */
#define SNS_ENDCYL         0x04       /* End of Cylinder */
#define SNS_INVSEQ         0x08       /* Invalid Sequence */
#define SNS_NOREC          0x10       /* No record found */
#define SNS_WRP            0x20       /* Write Protect */
#define SNS_ADDR           0x40       /* Missing Address Mark */
#define SNS_OVRINC         0x80       /* Overflow Incomplete */

/* Sense byte 2 */
#define SNS_BYTE2          0x00       /* Diags Use */
/* Sense byte 3 */
#define SNS_BYTE3          0x00       /* Diags Use */

/* saved in state field of data */
/* Record position, high 4 bits, low internal short count */
#define DK_POS_INDEX       0x00       /* At Index Mark */
#define DK_POS_HA          0x10       /* In home address (c) */
#define DK_POS_CNT         0x20       /* In count (c) */
#define DK_POS_KEY         0x30       /* In Key area */
#define DK_POS_DATA        0x40       /* In Data area */
#define DK_POS_AM          0x50       /* Address mark before record */
#define DK_POS_END         0x80       /* Past end of data */
#define DK_POS_SEEK        0xF0       /* In seek */

/* u6 holds last command */
/* Held in ccyl entry */

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
     uint16             tsize;   /* Size of one track include rounding */
     uint8              state;   /* Current state */
     uint8              klen;    /* remaining in key */
     uint8              filemsk; /* Current file mask */
     uint8              rec;     /* Current record number */
     uint16             count;   /* Remaining in current operation */
};

struct disk_t
{
    char               *name;         /* Type Name */
    int                 cyl;          /* Number of cylinders */
    int                 heads;        /* Number of heads/cylinder */
    unsigned int        bpt;          /* Max bytes per track */
    uint8               dev_type;     /* Device type code */
}
disk_type[] =
{
       {"2301",   1, 200, 20483, 0x01},       /*   4.1  M */
       {"2302", 250,  46,  4984, 0x02},       /*  57.32 M 50ms, 120ms/10, 180ms> 10 */
       {"2303",  80,  10,  4984, 0x03},       /*   4.00 M */
       {"2305",  48,   8, 14568, 0x05},       /*   5.43 M */
       {"2305-2",96,   8, 14858, 0x05},       /*  11.26 M */
       {"2311",  202, 10,  3625, 0x11},       /*   7.32 M  156k/s 30 ms 145 full */
       {"2314",  203, 20,  7294, 0x14},       /*  29.17 M */
       {"3330",  411, 19, 13165, 0x30},       /* 100.00 M */
       {"3330-2",815, 19, 13165, 0x30},
#if 0
       {"3340",  349, 12,  8535},       /*  34.94 M */
       {"3340-2",698, 12,  8535},       /*  69.89 M */
       {"3350",  560, 12, 19254},       /* */
#endif
       {NULL, 0}
};


/* Header block */
struct dasd_header
{
       uint8    devid[8];       /* device header. */
       uint32   heads;          /* number of heads per cylinder */
       uint32   tracksize;      /* size of track */
       uint8    devtype;        /* Hex code of last two digits of device type. */
       uint8    fileseq;        /* always 0. */
       uint16   highcyl;        /* highest cylinder. */
       uint8    resv[492];      /* pad to 512 byte block */
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
t_stat              dasd_get_type(FILE * st, UNIT * uptr, int32 v,
                                 CONST void *desc);
t_stat              dasd_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *dasd_description (DEVICE *dptr);

MTAB                dasd_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "TYPE", "TYPE",
     &dasd_set_type, &dasd_get_type, NULL, "Type of disk"},
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
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

struct dib dda_dib = { 0xF8, NUM_UNITS_MT, NULL, dasd_startcmd, NULL, dda_unit, dasd_ini};

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

struct dib ddb_dib = { 0xF8, NUM_UNITS_MT, NULL, dasd_startcmd, NULL, ddb_unit, dasd_ini};

DEVICE              ddb_dev = {
    "DB", ddb_unit, NULL, dasd_mod,
    NUM_UNITS_DASD, 8, 15, 1, 8, 8,
    NULL, NULL, &dasd_reset, &dasd_boot, &dasd_attach, &dasd_detach,
    &ddb_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &dasd_help, NULL, NULL, &dasd_description
};
#endif

uint8  dasd_startio(UNIT *uptr, uint16 chan) {
    uint16         addr = GET_UADDR(uptr->u3);
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);

    if ((uptr->u3 & 0xff) != 0) {
       return SNS_BSY;
    }
    uptr->u3 &= ~(DK_INDEX|DK_NOEQ|DK_HIGH|DK_PARAM|DK_MSET);
    uptr->u4 &= ~(DK_M_FILEMSK << DK_V_FILEMSK);
    sim_debug(DEBUG_CMD, dptr, "start io unit=%d\n", unit);
    return 0;
}

uint8  dasd_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) {
    uint16         addr = GET_UADDR(uptr->u3);
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);
    uint8          ch;

    if ((uptr->u3 & 0xff) != 0) {
       return SNS_BSY;
    }

    sim_debug(DEBUG_CMD, dptr, "CMD unit=%d %02x\n", unit, cmd);

    switch (cmd & 0x3) {
    case 0x3:              /* Control */
         if ((cmd & 0xfc) == 0 ||  cmd == DK_RELEASE)
            return SNS_CHNEND|SNS_DEVEND;
    case 0x1:              /* Write command */
    case 0x2:              /* Read command */
         uptr->u3 |= cmd;
         return 0;

    case 0x0:               /* Status */
         if (cmd == 0x4) {  /* Sense */
            uptr->u3 |= cmd;
            return 0;
         }
         break;
    }
    if (uptr->u5 & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle processing of disk requests. */
t_stat dasd_srv(UNIT * uptr)
{
    uint16              addr = GET_UADDR(uptr->u3);
    DEVICE             *dptr = find_dev_from_unit(uptr);
    struct dasd_t      *data = (struct dasd_t *)(uptr->up7);
    int                 unit = (uptr - dptr->units);
    int                 cmd = uptr->u3 & 0xff;
    int                 type = GET_TYPE(uptr->flags);
    int                 state = data->state;
    int                 trk;
    int                 i;
    int                 rd = ((cmd & 0x3) == 0x1) | ((cmd & 0x3) == 0x2);
    uint8               *rec;
    uint8               ch;
    uint8               buf[8];

    /* Check if read or write command, if so grab correct cylinder */
    if (rd && data->cyl != data->ccyl) {
        int tsize = data->tsize * disk_type[type].heads;
        if (uptr->u3 & DK_CYL_DIRTY) {
              sim_fseek(uptr->fileref, data->cpos, SEEK_SET);
              sim_fwrite(data->cbuf, 1, tsize, uptr->fileref);
              uptr->u3 &= ~DK_CYL_DIRTY;
        }
        data->ccyl = data->cyl;
        data->cpos = sizeof(struct dasd_header) + (data->ccyl * tsize);
        sim_fseek(uptr->fileref, data->cpos, SEEK_SET);
        sim_fread(data->cbuf, 1, tsize, uptr->fileref);
    }

    switch(state & 0xF0) {
    case DK_POS_INDEX:             /* At Index Mark */
         /* Read and multi-track advance to next head */
         if ((cmd & 0x83) == 0x81) {
             data->tstart += data->tsize;
             uptr->u4 ++;
             uptr->u3 &= ~DK_INDEX;
         }
         if (data->tstart > (data->tsize * disk_type[type].heads)) {
             uptr->u5 |= (SNS_ENDCYL << 8);
             data->tstart = 0;
             uptr->u4 &= ~0xff;
             uptr->u3 &= ~0xff;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
         /* If INDEX set signal no record if read */
         if (rd && uptr->u3 & DK_INDEX) {
             uptr->u5 |= (SNS_NOREC << 8);
             uptr->u3 &= ~0xff;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }
         uptr->u3 |= DK_INDEX;
         data->tpos = data->rpos = 0;
         data->state = DK_POS_HA;
         data->rec = 0;
         sim_activate(uptr, 100);
         break;

    case DK_POS_HA:                /* In home address (c) */
         data->tpos = data->count;
         if (data->count == 5) {
             data->rpos = 5;
             data->state = DK_POS_CNT;
             rec = &data->cbuf[data->rpos];
             /* Check for end of track */
             if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff)
                data->state = DK_POS_END;
             sim_activate(uptr, 100);
         } else
             sim_activate(uptr, 20);
         break;
    case DK_POS_CNT:               /* In count (c) */
         data->tpos++;
         if (data->count == 8) {
             rec = &data->cbuf[data->rpos];
             /* Check for end of track */
             if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff) {
                state = DK_POS_END;
                data->state = DK_POS_END;
             }
             data->klen = rec[5];
             data->dlen = (rec[6] << 8) | rec[7];
             data->state = DK_POS_KEY;
             if (data->klen == 0)
                data->state = DK_POS_DATA;
             sim_activate(uptr, 100);
         } else {
             sim_activate(uptr, 20);
         }
         break;
    case DK_POS_KEY:               /* In Key area */
         if (data->count == data->klen) {
             data->state = DK_POS_DATA;
             sim_activate(uptr, 100);
         } else {
             sim_activate(uptr, 20);
             data->tpos++;
         }
         break;
    case DK_POS_DATA:              /* In Data area */
         if (data->count == data->dlen) {
             data->state = DK_POS_AM;
             sim_activate(uptr, 100);
         } else {
             data->tpos++;
             sim_activate(uptr, 20);
         }
         break;
    case DK_POS_AM:                /* Beginning of record */
         data->rpos += data->dlen + data->klen + 8;
         data->rec++;
         data->state = DK_POS_CNT;
         rec = &data->cbuf[data->rpos];
         /* Check for end of track */
         if ((rec[0] & rec[1] & rec[2] & rec[3]) == 0xff)
            data->state = DK_POS_END;
         sim_activate(uptr, 100);
         break;
    case DK_POS_END:               /* Past end of data */
         data->tpos++;
         data->count = 0;
         data->klen = 0;
         data->dlen = 0;
         if (data->tpos >= data->tsize) {
             data->state = DK_POS_INDEX;
             sim_activate(uptr, 100);
         } else
             sim_activate(uptr, 20);
         break;
    case DK_POS_SEEK:                  /* In seek */
         /* Compute delay based of difference. */
         /* Set next state = index */
         i = (uptr->u4 >> 8) - data->cyl;
         if (i == 0) {
             data->state = DK_POS_INDEX;
             set_devattn(addr, SNS_DEVEND);
             sim_activate(uptr, 100);
         } else if (i > 0 ) {
             if (i > 10) {
                data->cyl += 10;
                sim_activate(uptr, 4000);
             } else {
                data->cyl ++;
                sim_activate(uptr, 500);
             }
         } else {
             if (i < 10) {
                data->cyl -= 10;
                sim_activate(uptr, 4000);
             } else {
                data->cyl --;
                sim_activate(uptr, 500);
             }
         }
         break;
    }

    switch (cmd & 0x7f) {
    case 0:                               /* No command, stop tape */
         sim_debug(DEBUG_DETAIL, dptr, "Idle unit=%d\n", unit);
         break;

    case 0x4:
         ch = uptr->u5 & 0xff;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 1 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         ch = (uptr->u5 >> 8) & 0xff;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 2 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         ch = 0;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 3 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         ch = unit;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 4 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         ch = 0;
         chan_write_byte(addr, &ch) ;
         uptr->u3 &= ~0xff;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case DK_SEEK:            /* Seek */
    case DK_SEEKCYL:         /* Seek Cylinder */
    case DK_SEEKHD:          /* Seek Head */
         if ((uptr->u3 & DK_PARAM) != 0) {
             uptr->u6 = uptr->u3 & 0xff;
             uptr->u3 &= ~(0xff);
             chan_end(addr, SNS_DEVEND);
             break;
         }

         /* Check if seek valid */
         i = data->filemsk & DK_MSK_SK;
         if (i == DK_MSK_SKNONE) { /* No seeks allowed, error out */
             uptr->u6 = uptr->u3 & 0xff;
             uptr->u3 &= ~(0xff);
             uptr->u5 |= SNS_CMDREJ;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }

         if (i != DK_MSK_SKALLSKR) { /* Some restrictions */
             if ((cmd == DK_SEEKHD && i != DK_MSK_SKALLHD) || (cmd == DK_SEEK)) {
                 uptr->u6 = uptr->u3 & 0xff;
                 uptr->u3 &= ~(0xff);
                 uptr->u5 |= SNS_CMDREJ;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
         }

         /* Read in 6 character seek code */
         for (i = 0; i < 6; i++) {
             if (chan_read_byte(addr, &buf[i])) {
                 uptr->u6 = uptr->u3 & 0xff;
                 uptr->u3 &= ~(0xff);
                 uptr->u5 |= SNS_CMDREJ|SNS_SEEKCK;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
         }
         trk = (buf[2] << 8) | buf[3];

         /* Check if seek valid */
         if ((buf[0] | buf[1] | buf[4]) != 0 || trk == 0 ||
             trk > disk_type[type].cyl || buf[5] > disk_type[type].heads)  {
             uptr->u6 = uptr->u3 & 0xff;
             uptr->u3 &= ~(0xff);
             uptr->u5 |= SNS_CMDREJ|SNS_SEEKCK;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }

         if (cmd == DK_SEEKHD && ((uptr->u4 >> 8) & 0x7fff) != trk) {
             uptr->u6 = uptr->u3 & 0xff;
             uptr->u3 &= ~(0xff);
             uptr->u5 |= SNS_CMDREJ|SNS_SEEKCK;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             break;
         }

         chan_end(addr, SNS_CHNEND);
         uptr->u3 |= DK_PARAM;
         data->tstart = buf[5] * data->tsize;   /* Point to start of record */
         uptr->u4 = (trk << 8) | buf[5];
         /* Check if on correct cylinder */
         if (trk != data->cyl) {
             /* Do seek */
             data->state = DK_POS_SEEK;
         }
         return SCPE_OK;

    case DK_RESTORE:         /* Restore */
         if ((uptr->u3 & DK_PARAM) != 0) {
             uptr->u6 = uptr->u3 & 0xff;
             uptr->u3 &= ~(0xff);
             chan_end(addr, SNS_DEVEND);
             break;
         }
         if ((data->filemsk & DK_MSK_SK) != DK_MSK_SKALLSKR) {
             uptr->u5 |= SNS_CMDREJ;
             uptr->u6 = 0;
             uptr->u3 &= ~(0xff|DK_PARAM);
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
         uptr->u3 |= DK_PARAM;
         uptr->u4 = 0;
         data->tstart = 0;
         chan_end(addr, SNS_CHNEND);
         if (data->cyl != 0) {
             /* Do a seek */
             data->state = DK_POS_SEEK;
         }
         return SCPE_OK;

    case DK_SETMSK:          /* Set file mask */
         /* If mask already set, error */
         uptr->u6 = uptr->u3 & 0xff;
         uptr->u3 &= ~(0xff|DK_PARAM);
         if (uptr->u3 & DK_MSET) {
             uptr->u6 = 0;
             uptr->u5 |= SNS_CMDREJ | (SNS_INVSEQ << 8);
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
         /* Grab mask */
         if (chan_read_byte(addr, &ch)) {
             uptr->u6 = 0;
             uptr->u5 |= SNS_CMDREJ;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
         /* Save */
         if ((ch & ~(DK_MSK_SK|DK_MSK_WRT)) != 0) {
             uptr->u6 = 0;
             uptr->u5 |= SNS_CMDREJ;
             chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
         }
         data->filemsk = ch;
         uptr->u3 |= DK_MSET;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case DK_SPACE:           /* Space record */
         break;
    case DK_SRCH_HAEQ:       /* Search HA equal */
         if (state == DK_POS_INDEX) {
             uptr->u3 &= ~DK_SRCOK;
             uptr->u3 |= DK_PARAM;
             break;
         }

         if (uptr->u3 & DK_PARAM && state == DK_POS_HA) {
             uptr->u3 &= ~DK_INDEX;
             if (chan_read_byte(addr, &ch)) {
                  if (data->count != 0x5)
                      uptr->u3 |= DK_SHORTSRC;
             } else if (ch != data->cbuf[data->tpos]) {
                  uptr->u3 |= DK_NOEQ;
             }
             if (data->count == 5 || uptr->u3 & DK_SHORTSRC) {
                uptr->u6 = uptr->u3 & 0xff;
                uptr->u3 &= ~(0xff|DK_PARAM);
                if (uptr->u3 & DK_NOEQ)
                    chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                else {
                    uptr->u3 |= DK_SRCOK;
                    chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_SMS);
                }
             }
         }
         break;

    case DK_RD_CNT:          /* Read count */
         if (state == DK_POS_AM) {
             uptr->u3 |= DK_PARAM;
         }

         if (uptr->u3 & DK_PARAM && state == DK_POS_CNT) {
             uptr->u3 &= ~DK_INDEX;
             ch = data->cbuf[data->tpos];
             if (chan_write_byte(addr, &ch) || data->count == 8) {
                uptr->u6 = uptr->u3 & 0xff;
                uptr->u3 &= ~(0xff);
                chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             }
         }
         break;

    case DK_SRCH_IDEQ:       /* Search ID equal */
    case DK_SRCH_IDGT:       /* Search ID greater */
    case DK_SRCH_IDGE:       /* Search ID greater or equal */
         if (state == DK_POS_CNT) {
             uptr->u3 &= ~(DK_SRCOK|DK_SHORTSRC);
             uptr->u3 |= DK_PARAM;
         }

         if (uptr->u3 & DK_PARAM) {
             uptr->u3 &= ~DK_INDEX;
         /* Wait for start of record */
             if (chan_read_byte(addr, &ch)) {
                  uptr->u3 |= DK_SHORTSRC;
             } else if (ch != data->cbuf[data->tpos]) {
                  if ((uptr->u3 & DK_NOEQ) == 0) {
                      uptr->u3 |= DK_NOEQ;
                      if (ch > *rec)
                         uptr->u3 |= DK_HIGH;
                  }
             }
             if (data->count == 5 || uptr->u3 & DK_SHORTSRC) {
                 uptr->u6 = uptr->u3 & 0xff;
                 uptr->u3 &= ~(0xff);
                 i = 0;
                 if ((cmd & 0x2) && (uptr->u3 & DK_NOEQ) == 0)
                      i = SNS_SMS;
                 if ((cmd & 0x4) && (uptr->u3 & DK_HIGH))
                      i = SNS_SMS;
                 if (i)
                     uptr->u3 |= DK_SRCOK;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|i);
             }
         }
         break;

    case DK_SRCH_KYEQ:       /* Search Key equal */
    case DK_SRCH_KYGT:       /* Search Key greater */
    case DK_SRCH_KYGE:       /* Search Key greater or equal */
         if (state == DK_POS_AM)
             uptr->u3 &= ~DK_SRCOK;
         if (state == DK_POS_KEY && data->count == 0) {
             if (data->rec == 0 && (uptr->u3 & DK_SRCOK) == 0)
                 break;
             uptr->u3 &= ~(DK_SRCOK|DK_SHORTSRC);
             uptr->u3 |= DK_PARAM;
         }
         if (uptr->u3 & DK_PARAM) {
         /* Wait for key */
             if (chan_read_byte(addr, &ch)) {
                  uptr->u3 |= DK_SHORTSRC;
             } else if (ch != data->cbuf[data->tpos]) {
                  if ((uptr->u3 & DK_NOEQ) == 0) {
                      uptr->u3 |= DK_NOEQ;
                      if (ch > *rec)
                         uptr->u3 |= DK_HIGH;
                  }
             }
             if (data->count == data->klen || uptr->u3 & DK_SHORTSRC) {
                 uptr->u6 = uptr->u3 & 0xff;
                 uptr->u3 &= ~(0xff);
                 i = 0;
                 if ((cmd & 0x2) && (uptr->u3 & DK_NOEQ) == 0)
                     i = SNS_SMS;
                 if ((cmd & 0x4) && (uptr->u3 & DK_HIGH))
                     i = SNS_SMS;
                 if (i)
                     uptr->u3 |= DK_SRCOK;
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|i);
             }
         }
         break;

    case DK_RD_HA:           /* Read home address */
         if (state == DK_POS_INDEX) {
             uptr->u3 |= DK_PARAM;
         }

         if (uptr->u3 & DK_PARAM && (state & 0xF0) == DK_POS_HA) {
             uptr->u3 &= ~DK_INDEX;
             ch = data->cbuf[data->tpos];
             if (chan_write_byte(addr, &ch) || (state & 0xF) == 5) {
                uptr->u6 = uptr->u3 & 0xff;
                uptr->u3 &= ~(0xff);
                chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             }
         }
         break;

    case DK_RD_IPL:          /* Read IPL record */
         if (data->count == 0 && state == DK_POS_CNT && data->rec == 1) {
             uptr->u3 &= ~DK_INDEX;
             uptr->u3 |= DK_PARAM;
         }

    case DK_RD_R0:           /* Read R0 */
         if (data->count == 0 && state == DK_POS_CNT && data->rec == 0) {
             uptr->u3 |= DK_PARAM;
             uptr->u3 &= ~DK_INDEX;
         }
         goto rd;

    case DK_RD_CKD:          /* Read count, key and data */
         if (data->count == 0 && state == DK_POS_CNT) {
             uptr->u3 |= DK_PARAM;
             uptr->u3 &= ~DK_INDEX;
         }

    case DK_RD_KD:           /* Read key and data */
         if (data->count == 0 && state == DK_POS_KEY) {
             uptr->u3 |= DK_PARAM;
             uptr->u3 &= ~DK_INDEX;
         }

    case DK_RD_D:            /* Read Data */
         if (data->count == 0 && state == DK_POS_DATA) {
             uptr->u3 |= DK_PARAM;
             uptr->u3 &= ~DK_INDEX;
         }

rd:
         if (uptr->u3 & DK_PARAM) {
             /* Check for end of file */
             if (state == DK_POS_DATA && data->count == 0 && data->dlen == 0) {
                uptr->u6 = uptr->u3 & 0xff;
                uptr->u3 &= ~(0xff|DK_PARAM);
                chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                break;
             }
             ch = data->cbuf[data->tpos];
             if (chan_write_byte(addr, &ch)) {
                uptr->u6 = uptr->u3 & 0xff;
                uptr->u3 &= ~(0xff|DK_PARAM);
                chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             }
             if (state == DK_POS_DATA && data->count == data->dlen) {
                uptr->u6 = uptr->u3 & 0xff;
                uptr->u3 &= ~(0xff|DK_PARAM);
                chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             }
         }
         break;

    case DK_WR_HA:           /* Write home address */
         if (state == DK_POS_INDEX) {
             /* Check if command ok based on mask */
             if ((data->filemsk & DK_MSK_WRT) != DK_MSK_ALLWRT) {
                 uptr->u5 |= SNS_CMDREJ;
                 uptr->u6 = 0;
                 uptr->u3 &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }

             uptr->u3 |= DK_PARAM;
             break;
         }

         if (uptr->u3 & DK_PARAM) {
             uptr->u3 &= ~DK_INDEX;
             if (chan_read_byte(addr, &ch)) {
                 ch = 0;
             }
             data->cbuf[data->tpos] = ch;
             if (data->count == 5) {
                  uptr->u6 = uptr->u3 & 0xff;
                  uptr->u3 &= ~(0xff|DK_PARAM);
                  chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                  for(i = 0; i < 4; i++)
                     data->cbuf[data->tpos+i] = 0xff;
                  for(; i < 8; i++)
                     data->cbuf[data->tpos+i] = 0;
             }
         }
         break;

    case DK_WR_R0:           /* Write R0 */
         if ((state == DK_POS_CNT || state == DK_POS_END)
                  && data->rec == 0 && data->count == 0) {
             /* Check if command ok based on mask */
             if ((data->filemsk & DK_MSK_WRT) != DK_MSK_ALLWRT) {
                 uptr->u5 |= SNS_CMDREJ;
                 uptr->u6 = 0;
                 uptr->u3 &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (uptr->u6 == DK_WR_HA ||
                (uptr->u6 == DK_SRCH_HAEQ &&
                     (uptr->u3 & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 state = data->state = DK_POS_CNT;
                 uptr->u3 |= DK_PARAM;
             } else {
                 uptr->u5 |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->u6 = 0;
                 uptr->u3 &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }
         goto wrckd;

    case DK_WR_SCKD:         /* Write special count, key and data */
    case DK_WR_CKD:          /* Write count, key and data */
         if ((state == DK_POS_CNT || state == DK_POS_END)
                  && data->rec != 0 && data->count == 0) {
             /* Check if command ok based on mask */
             i = data->filemsk & DK_MSK_WRT;
             if (i != DK_MSK_ALLWRT || i != DK_MSK_INHWR0) {
                 uptr->u5 |= SNS_CMDREJ;
                 uptr->u6 = 0;
                 uptr->u3 &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (uptr->u6 == DK_WR_R0 || uptr->u6 == DK_WR_CKD ||
                ((uptr->u6 & 0x3) == 1 && (uptr->u6 & 0xE0) != 0 &&
                     (uptr->u3 & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 state = data->state = DK_POS_CNT;
                 uptr->u3 |= DK_PARAM;
             } else {
                 uptr->u5 |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->u6 = 0;
                 uptr->u3 &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }

wrckd:
         if (uptr->u3 & DK_PARAM) {
             uptr->u3 &= ~DK_INDEX;
             if (chan_read_byte(addr, &ch)) {
                 ch = 0;
             }
             data->cbuf[data->tpos] = ch;
             if (state == DK_POS_CNT && data->count == 8) {
                 rec = &data->cbuf[data->rpos];
                 data->klen = rec[5];
                 data->dlen = (rec[6] << 8) | rec[7];
                 data->state = DK_POS_KEY;
                 if (data->klen == 0)
                     data->state = DK_POS_DATA;
             } else if (state == DK_POS_DATA && data->count == data->dlen) {
                  uptr->u6 = uptr->u3 & 0xff;
                  uptr->u3 &= ~(0xff|DK_PARAM);
                  chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                  for(i = 0; i < 4; i++)
                     data->cbuf[data->tpos+i] = 0xff;
                  for(; i < 8; i++)
                     data->cbuf[data->tpos+i] = 0;
             }
         }
         break;

    case DK_WR_KD:           /* Write key and data */
         if ((state == DK_POS_KEY) && data->rec != 0 && data->count == 0) {
             /* Check if command ok based on mask */
             if ((data->filemsk & DK_MSK_WRT) == DK_MSK_INHWRT) {
                 uptr->u5 |= SNS_CMDREJ;
                 uptr->u6 = 0;
                 uptr->u3 &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (((uptr->u6 & 0x13) == 0x11 &&
                     (uptr->u3 & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 uptr->u3 |= DK_PARAM;
             } else {
                 uptr->u5 |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->u6 = 0;
                 uptr->u3 &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }
         goto wr;

    case DK_WR_D:            /* Write Data */
         if ((state == DK_POS_DATA) && data->rec != 0 && data->count == 0) {
             /* Check if command ok based on mask */
             if ((data->filemsk & DK_MSK_WRT) == DK_MSK_INHWRT) {
                 uptr->u5 |= SNS_CMDREJ;
                 uptr->u6 = 0;
                 uptr->u3 &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (((uptr->u6 & 0x3) == 1 && (uptr->u6 & 0xE0) != 0 &&
                     (uptr->u3 & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 uptr->u3 |= DK_PARAM;
             } else {
                 uptr->u5 |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->u6 = 0;
                 uptr->u3 &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }

wr:
         if (uptr->u3 & DK_PARAM) {
             uptr->u3 &= ~DK_INDEX;
             /* Check for end of file */
             if (state == DK_POS_DATA && data->count == 0 && data->dlen == 0) {
                uptr->u6 = uptr->u3 & 0xff;
                uptr->u3 &= ~(0xff|DK_PARAM);
                chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITEXP);
                break;
             }
             if (chan_read_byte(addr, &ch)) {
                 ch = 0;
             }
             data->cbuf[data->tpos] = ch;
             if (state == DK_POS_DATA && data->count == data->dlen) {
                  uptr->u6 = uptr->u3 & 0xff;
                  uptr->u3 &= ~(0xff|DK_PARAM);
                  chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             }
         }
         break;

    case DK_ERASE:           /* Erase to end of track */
         if (state == DK_POS_AM || state == DK_POS_END) {
             /* Check if command ok based on mask */
             i = data->filemsk & DK_MSK_WRT;
             if (i != DK_MSK_ALLWRT || i != DK_MSK_INHWR0) {
                 uptr->u5 |= SNS_CMDREJ;
                 uptr->u6 = 0;
                 uptr->u3 &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
                 break;
             }
             if (uptr->u6 == DK_WR_R0 || uptr->u6 == DK_WR_CKD ||
                ((uptr->u6 & 0x3) == 1 && (uptr->u6 & 0xE0) != 0 &&
                     (uptr->u3 & (DK_SHORTSRC|DK_SRCOK)) == DK_SRCOK)) {
                 state = data->state = DK_POS_END;
                 uptr->u3 |= DK_PARAM;
             } else {
                 uptr->u5 |= SNS_CMDREJ | (SNS_INVSEQ << 8);
                 uptr->u6 = 0;
                 uptr->u3 &= ~(0xff);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK);
             }
         }

         if (uptr->u3 & DK_PARAM) {
             uptr->u3 &= ~DK_INDEX;
             uptr->u6 = uptr->u3 & 0xff;
             uptr->u3 &= ~(0xff|DK_PARAM);
             chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             /* Write end mark */
             for(i = 0; i < 4; i++)
                data->cbuf[data->rpos+i] = 0xff;
             for(; i < 8; i++)
                data->cbuf[data->rpos+i] = 0;
         }
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
}

t_stat
dasd_reset(DEVICE * dptr)
{
    return SCPE_OK;
}

int
dasd_format(UNIT * uptr) {
    struct dasd_header  hdr;
    struct dasd_t       *data;
    int                 type = GET_TYPE(uptr->flags);
    int                 tsize;
    int                 cyl;
    int                 hd;
    int                 pos;

    if (!get_yn("Initialize dasd? [Y]", TRUE)) {
        return 1;
    }
    memset(&hdr, 0, sizeof(struct dasd_header));
    strncpy(&hdr.devid[0], "CKD_P370", 8);
    hdr.heads = disk_type[type].heads;
    hdr.tracksize = (disk_type[type].bpt | 0x1ff) + 1;
    hdr.devtype = disk_type[type].dev_type;
    sim_fseek(uptr->fileref, 0, SEEK_SET);
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
            data->cbuf[pos++] = 0;
            data->cbuf[pos++] = (cyl >> 8);
            data->cbuf[pos++] = (cyl & 0xff);
            data->cbuf[pos++] = (hd >> 8);
            data->cbuf[pos++] = (hd & 0xff);
            data->cbuf[pos++] = 0xff;
            data->cbuf[pos++] = 0xff;
            data->cbuf[pos++] = 0xff;
            data->cbuf[pos++] = 0xff;
            pos += data->tsize - 9;
        }
        sim_fwrite(data->cbuf, 1, tsize, uptr->fileref);
        if ((cyl % 10) == 0)
           fputc('.', stderr);
    }
    sim_fseek(uptr->fileref, sizeof(struct dasd_header), SEEK_SET);
    sim_fread(data->cbuf, 1, tsize, uptr->fileref);
    data->cpos = sizeof(struct dasd_header);
    data->ccyl = 0;
    data->ccyl = 0;
    data->cyl = 2000;
    data->state = DK_POS_SEEK;
    sim_activate(uptr, 100);
    fputc('\n', stderr);
    fputc('\r', stderr);
    return 0;
}

t_stat
dasd_attach(UNIT * uptr, CONST char *file)
{
    uint16              addr = GET_UADDR(uptr->u3);
    t_stat              r;
    int                 i;
    struct dasd_header  hdr;
    struct dasd_t       *data;
    int                 tsize;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
       return r;

    if (sim_fread(&hdr, 1, sizeof(struct dasd_header), uptr->fileref) !=
          sizeof(struct dasd_header) || strncmp(&hdr.devid[0], "CKD_P370", 8) != 0) {
        if (dasd_format(uptr)) {
            detach_unit(uptr);
            return SCPE_FMT;
        }
        return SCPE_OK;
    }

    fprintf(stderr, "%8s %d %d %02x %d\n\r", hdr.devid, hdr.heads, hdr.tracksize,
            hdr.devtype, hdr.highcyl);
    for (i = 0; disk_type[i].name != 0; i++) {
         tsize = (disk_type[i].bpt | 0x1ff) + 1;
         if (hdr.devtype == disk_type[i].dev_type && hdr.tracksize == tsize &&
             hdr.heads == disk_type[i].heads) {
             if (GET_TYPE(uptr->flags) != i) {
                  /* Ask if we should change */
                  fprintf(stderr, "Wrong type %s\n\r", disk_type[i].name);
                  if (!get_yn("Update dasd type? [N]", FALSE)) {
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
    sim_fseek(uptr->fileref, sizeof(struct dasd_header), SEEK_SET);
    sim_fread(data->cbuf, 1, tsize * hdr.heads, uptr->fileref);
    data->cpos = sizeof(struct dasd_header);
    data->ccyl = 0;
    data->cyl = 2000;
    data->state = DK_POS_SEEK;
    sim_activate(uptr, 100);
    return SCPE_OK;
}

t_stat
dasd_detach(UNIT * uptr)
{
    struct dasd_t       *data = (struct dasd_t *)uptr->up7;
    int                 type = GET_TYPE(uptr->flags);

    if (uptr->u3 & DK_CYL_DIRTY) {
        sim_fseek(uptr->fileref, data->cpos, SEEK_SET);
        sim_fwrite(data->cbuf, 1, data->tsize * disk_type[type].heads, uptr->fileref);
        uptr->u3 &= ~DK_CYL_DIRTY;
    }
    if (data != 0) {
        free(data->cbuf);
        free(data);
    }
    uptr->up7 = 0;
    uptr->u3 = 0;
    return detach_unit(uptr);
}

t_stat
dasd_boot(int32 unit_num, DEVICE * dptr)
{
    UNIT               *uptr = &dptr->units[unit_num];
    t_stat              r;

    if ((uptr->flags & UNIT_ATT) == 0)
        return SCPE_UNATT;      /* attached? */
    return chan_boot(GET_UADDR(uptr->u3), dptr);
}

/* Disk option setting commands */

t_stat
dasd_set_type(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
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
            uptr->flags &= ~UNIT_TYPE;
            uptr->flags |= SET_TYPE(i);
            uptr->capac = disk_type[i].bpt * disk_type[i].heads * disk_type[i].cyl;
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
        char  sm = 'K';
        size /= 1024;
        size = (10 * size) / 1024;
        fprintf(st, "      %-8s %4d.%1dMB\n", disk_type[i].name, size/10, size%10);
    }
    fprint_set_help (st, dptr);
    fprint_show_help (st, dptr);
    return SCPE_OK;
}

const char *dasd_description (DEVICE *dptr)
{
    return "IBM 2840 disk file controller";
}

#endif
