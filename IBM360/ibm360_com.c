/* ibm360_com.c: IBM 360 2703 communications controller

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


*/

#include "ibm360_defs.h"

#ifdef NUM_DEVS_COM
#define UNIT_COM          UNIT_ATTABLE | UNIT_DISABLE | UNIT_ROABLE


/* u3 */
#define DK_NOP             0x03       /* Nop operation */
#define DK_RELEASE         0x17       /* Release from channel */
#define DK_RESTORE         0x13       /* Restore */
#define DK_SEEK            0x07       /* Seek */
#define DK_SEEKCYL         0x0B       /* Seek Cylinder */
#define DK_SEEKHD          0x09       /* Seek Head */
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

#define DK_CYL_DIRTY       0x100      /* Current cylinder dirty */

#define DK_MSK_INHWR0      0x00       /* Inhbit writing of HA/R0 */
#define DK_MSK_INHWRT      0x40       /* Inhbit all writes */
#define DK_MSK_ALLWRU      0x80       /* Allow all updates */
#define DK_MSK_ALLWRT      0xc0       /* Allow all writes */
#define DK_MSK_WRT         0xc0       /* Write mask */

#define DK_MSK_SKALLSKR    0x00       /* Allow all seek/recal */
#define DK_MSK_SKALLCLY    0x01       /* Allow cyl/head only */
#define DK_MSK_SKALLHD     0x02       /* Allow head only */
#define DK_MSK_SKNONE      0x03       /* Allow no seeks */
#define DK_MSK_SK          0x03       /* Seek mask */


/* u4 */
/* Position around disk */

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

/* u6 */

uint8               com_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) ;
uint8               com_haltio(uint16 addr);
t_stat              com_srv(UNIT *);
void                com_ini(UNIT *, t_bool);
t_stat              com_reset(DEVICE *);
t_stat              com_attach(UNIT *, CONST char *);
t_stat              com_detach(UNIT *);
t_stat              com_boot(int32, DEVICE *);



MTAB                com_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {0}
};

UNIT                com_unit[] = {
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x030)},       /* 0 */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x031)},       /* 1 */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x032)},       /* 2 */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x033)},       /* 3 */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x034)},       /* 4 */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x035)},       /* 5 */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x036)},       /* 6 */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x037)},       /* 7 */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x038)},       /* 8 */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x039)},       /* 9 */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x03A)},       /* A */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x03B)},       /* B */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x03C)},       /* C */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x03D)},       /* D */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x03E)},       /* E */
    {UDATA(&com_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x03F)},       /* F */
};

struct dib com_dib = { 0xF0, NUM_UNITS_MT, NULL, com_startcmd, NULL, com_unit, NULL};

DEVICE              com_dev = {
    "COM", com_unit, NULL, com_mod,
    NUM_UNITS_COM, 8, 15, 1, 8, 8,
    NULL, NULL, &com_reset, NULL, &com_attach, &com_detach,
    &com_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug
};



uint8  com_startcmd(UNIT *uptr, uint16 chan,  uint8 cmd) {
    uint16         addr = GET_UADDR(uptr->u3);
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);
    uint8          ch;

    if ((uptr->u3 & 0xff) != 0) {
       return SNS_BSY;
    }

    sim_debug(DEBUG_CMD, dptr, "CMD unit=%d %x", unit, cmd);

    switch (cmd & 0x3) {
    case 0x3:              /* Control */
         if ((cmd & 0xfc) == 0)
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
t_stat com_srv(UNIT * uptr)
{
    uint16              addr = GET_UADDR(uptr->u3);
    DEVICE             *dptr = find_dev_from_unit(uptr);
    struct com_t      *data = (struct com_t *)(uptr->up7);
    int                 unit = (uptr - dptr->units);
    int                 cmd = uptr->u3 & 0xff;
    uint8               ch;


    switch (cmd) {
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
         ch = 0xc0;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 3 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         ch = (uptr->u5 >> 16) & 0xff;
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 4 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         ch = 0;
         chan_write_byte(addr, &ch) ;
         chan_write_byte(addr, &ch);
         uptr->u3 &= ~0xff;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case DK_RELEASE:         /* Release from channel */
    case DK_RESTORE:         /* Restore */
    case DK_SEEK:            /* Seek */
    case DK_SEEKCYL:         /* Seek Cylinder */
    case DK_SEEKHD:          /* Seek Head */
    case DK_SETMSK:          /* Set file mask */
    case DK_SPACE:           /* Space record */
    case DK_SRCH_HAEQ:       /* Search HA equal */
    case DK_SRCH_IDEQ:       /* Search ID equal */
    case DK_SRCH_IDGT:       /* Search ID greater */
    case DK_SRCH_IDGE:       /* Search ID greater or equal */
    case DK_SRCH_KYEQ:       /* Search Key equal */
    case DK_SRCH_KYGT:       /* Search Key greater */
    case DK_SRCH_KYGE:       /* Search Key greater or equal */
    case DK_RD_IPL:          /* Read IPL record */
    case DK_RD_HA:           /* Read home address */
    case DK_RD_CNT:          /* Read count */
    case DK_RD_R0:           /* Read R0 */
    case DK_RD_D:            /* Read Data */
    case DK_RD_KD:           /* Read key and data */
    case DK_RD_CKD:          /* Read count, key and data */
    case DK_WR_HA:           /* Write home address */
    case DK_WR_R0:           /* Write R0 */
    case DK_WR_D:            /* Write Data */
    case DK_WR_KD:           /* Write key and data */
    case DK_WR_CKD:          /* Write count, key and data */
    case DK_WR_SCKD:         /* Write special count, key and data */
    case DK_ERASE:           /* Erase to end of track */
         break;
    }
    return SCPE_OK;
}

void
com_ini(UNIT * uptr, t_bool f)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
}

t_stat
com_reset(DEVICE * dptr)
{
    return SCPE_OK;
}

t_stat
com_attach(UNIT * uptr, CONST char *file)
{
    uint16              addr = GET_UADDR(uptr->u3);
    t_stat              r;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
       return r;

    set_devattn(addr, SNS_DEVEND);
    return SCPE_OK;
}

t_stat
com_detach(UNIT * uptr)
{
    uptr->u3 = 0;
    return detach_unit(uptr);
}

t_stat com_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
    const char *cptr)
{
      int i;
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
return SCPE_OK;
}

const char *com_description (DEVICE *dptr)
{
return "IBM 2703 communications controller";
}

#endif
