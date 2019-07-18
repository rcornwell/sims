/* sel32_clk.c: SEL 32 Class F IOP processor RTOM functions.

   Copyright (c) 2018, James C. Bevier
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

   This module support the real-time clock and the interval timer.
   These are CD/TD class 3 devices.  The RTC can be programmed to
   50/100 HZ or 60/120 HZ rates and creates an interrupt at the
   requested rate.  The interval timer is a 32 bit register that is
   loaded with a value to be down counted.  An interrupt is generated
   when the count reaches zero,  The clock continues down counting
   until read/reset by the programmer.  The rate can be external or
   38.4 microseconds per count.

*/

#include "sel32_defs.h"

#ifdef NUM_DEVS_RTOM

extern t_stat set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat show_dev_addr(FILE *st, UNIT * uptr, int32 v, CONST void *desc);
extern void chan_end(uint16 chan, uint8 flags);
extern int  chan_read_byte(uint16 chan, uint8 *data);
extern int  chan_write_byte(uint16 chan, uint8 *data);
extern void set_devattn(uint16 addr, uint8 flags);
extern void post_extirq(void);

void rtc_setup (uint32 ss, uint32 level);
t_stat rtc_srv (UNIT *uptr);
t_stat rtc_reset (DEVICE *dptr);
t_stat rtc_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat rtc_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

extern int irq_pend;                /* go scan for pending int or I/O */
extern uint32 INTS[];               /* interrupt control flags */
extern uint32 SPAD[];               /* computer SPAD */
extern uint32 M[];                  /* system memory */

int32 rtc_pie = 0;                  /* rtc pulse ie */
int32 rtc_tps = 60;                 /* rtc ticks/sec */
int32 rtc_lvl = 0x18;               /* rtc interrupt level */

/* Clock data structures

   rtc_dev      RTC device descriptor
   rtc_unit     RTC unit
   rtc_reg      RTC register list
*/

/* clock is attached all the time */
/* defailt to 60 HZ RTC */
UNIT rtc_unit = { UDATA (&rtc_srv, UNIT_ATT, 0), 16666, UNIT_ADDR(0x7F06)};

REG rtc_reg[] = {
    { FLDATA (PIE, rtc_pie, 0) },
    { DRDATA (TIME, rtc_unit.wait, 32), REG_NZ + PV_LEFT },
    { DRDATA (TPS, rtc_tps, 8), PV_LEFT + REG_HRO },
    { NULL }
    };

MTAB rtc_mod[] = {
    { MTAB_XTD|MTAB_VDV, 50, NULL, "50HZ",
      &rtc_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 60, NULL, "60HZ",
      &rtc_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 100, NULL, "100HZ",
      &rtc_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 120, NULL, "120HZ",
      &rtc_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "FREQUENCY", NULL,
      NULL, &rtc_show_freq, NULL },
    { 0 }
    };

DEVICE rtc_dev = {
    "RTC", &rtc_unit, rtc_reg, rtc_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &rtc_reset,
    NULL, NULL, NULL
    };

/* The real time clock runs continuously; therefore, it only has
   a unit service routine and a reset routine.  The service routine
   sets an interrupt that invokes the clock counter.
*/

/* service clock signal from simulator */
t_stat rtc_srv (UNIT *uptr)
{
    if (rtc_pie) {                                  /* set pulse intr */
        INTS[rtc_lvl] |= INTS_REQ;                  /* request the interrupt */
        irq_pend = 1;                               /* make sure we scan for int */
    }
//  rtc_unit.wait = sim_rtcn_calb (rtc_tps, TMR_RTC);   /* calibrate */
//  sim_activate (&rtc_unit, rtc_unit.wait);        /* reactivate */
    sim_activate (&rtc_unit, 16667);                /* reactivate */
    return SCPE_OK;
}

/* Clock interrupt start/stop */
/* ss = 1 - starting clock */
/* ss = 0 - stopping clock */
/* level = interrupt level */
void rtc_setup(uint ss, uint32 level)
{
    uint32  val = SPAD[level+0x80];                 /* get SPAD value for interrupt vector */
    rtc_lvl = level;                                /* save the interrupt level */
    uint32 addr = SPAD[0xf1] + (level<<2);          /* vector address in SPAD */
    addr = M[addr>>2];                              /* get the interrupt context block addr */
//fprintf(stderr, "rtc_setup called ss %x level %x SPAD %x icba %x\r\n", ss, level, val, addr);
    if (ss == 1) {                                  /* starting? */
        INTS[level] |= INTS_ENAB;                   /* make sure enabled */
        SPAD[level+0x80] |= SINT_ENAB;              /* in spad too */
        INTS[level] |= INTS_REQ;                    /* request the interrupt */
        sim_activate(&rtc_unit, 20);                /* start us off */
    } else {
        INTS[level] &= ~INTS_ENAB;                  /* make sure disabled */
        SPAD[level+0x80] &= ~SINT_ENAB;             /* in spad too */
    }
    rtc_pie = ss;                                   /* set new state */
}

/* Clock reset */
t_stat rtc_reset(DEVICE *dptr)
{
    rtc_pie = 0;                                    /* disable pulse */
    rtc_unit.wait = sim_rtcn_init (rtc_unit.wait, TMR_RTC); /* initialize clock calibration */
    sim_activate (&rtc_unit, rtc_unit.wait);        /* activate unit */
    return SCPE_OK;
}

/* Set frequency */
t_stat rtc_set_freq(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (cptr)                                       /* if chars, bad */
        return SCPE_ARG;                            /* ARG error */
    if ((val != 50) && (val != 60) && (val != 100) && (val != 120))
        return SCPE_IERR;                           /* scope error */
    rtc_tps = val;                                  /* set the new frequency */
    return SCPE_OK;                                 /* we done */
}

/* Show frequency */
t_stat rtc_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    /* print the cirrent frequency setting */
    if (rtc_tps < 100)
        fprintf (st, (rtc_tps == 50)? "50Hz": "60Hz");
    else
        fprintf (st, (rtc_tps == 100)? "100Hz": "120Hz");
    return SCPE_OK;
}

/************************************************************************/

/* Interval Timer support */
int32 itm_pie = 0;                                  /* itm pulse enable */
//int32 itm_tps = 38;                               /* itm 26041 ticks/sec = 38.4 us per tic */
///int32 itm_tps = 48;                              /* itm 26041 ticks/sec = 38.4 us per tic */
int32 itm_tps = 64;                                 /* itm 26041 ticks/sec = 38.4 us per tic */
int32 itm_lvl = 0x5f;                               /* itm interrupt level */
int32 itm_cnt = 26041;                              /* value that we are downcounting */
int32 itm_run = 0;                                  /* set when timer running */
t_stat itm_srv (UNIT *uptr);
t_stat itm_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat itm_reset (DEVICE *dptr);
t_stat itm_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

#define TMR_ITM 2

/* Clock data structures

   itm_dev      Interval Timer ITM device descriptor
   itm_unit     Interval Timer ITM unit
   itm_reg      Interval Timer ITM register list
*/

/* clock is attached all the time */
/* defailt to 60 HZ RTC */
//UNIT itm_unit = { UDATA (&itm_srv, UNIT_ATT, 0), 38, UNIT_ADDR(0x7F04)};
//UNIT itm_unit = { UDATA (&itm_srv, UNIT_ATT, 0), 48, UNIT_ADDR(0x7F04)};
//UNIT itm_unit = { UDATA (&itm_srv, UNIT_ATT, 0), 26042, UNIT_ADDR(0x7F04)};
UNIT itm_unit = { UDATA (&itm_srv, UNIT_ATT, 0), 26042, UNIT_ADDR(0x7F04)};

REG itm_reg[] = {
    { FLDATA (PIE, itm_pie, 0) },
    { DRDATA (TIME, itm_unit.wait, 32), REG_NZ + PV_LEFT },
    { DRDATA (TPS, itm_tps, 32), PV_LEFT + REG_HRO },
    { NULL }
    };

MTAB itm_mod[] = {
    { MTAB_XTD|MTAB_VDV, 384, NULL, "38.4us",
      &itm_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 768, NULL, "76.86us",
      &itm_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "FREQUENCY", NULL,
      NULL, &itm_show_freq, NULL },
    { 0 }
    };

DEVICE itm_dev = {
    "ITM", &itm_unit, itm_reg, itm_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &itm_reset,
    NULL, NULL, NULL
    };

/* The interval timer downcounts the value it is loaded with and
   runs continuously; therefore, it has a read/write routine,
   a unit service routine and a reset routine.  The service routine
   sets an interrupt that invokes the clock counter.
*/

/* service clock signal from simulator */
/* for 38.4 us/tic we get 26041 ticks per second */
/* downcount the loaded value until zero and then cause interrupt */
t_stat itm_srv (UNIT *uptr)
{
//  uint32  val = SPAD[itm_lvl+0x80];               /* get SPAD value for interrupt vector */
//  uint32 addr = SPAD[0xf1] + (itm_lvl<<2);        /* vector address in SPAD */
//  addr = M[addr>>2];                              /* get the interrupt context block addr */
//fprintf(stderr, "itm_srv level %x itm_pie %x wait %x spad %x icba %x\r\n",
//  itm_lvl, itm_pie, itm_unit.wait, val, addr);

    /* count down about 48 instructions per tick ~38.4 us */
    /* we will be called once for each instructon */
    itm_unit.wait -= 1;                             /* subtract 1 from wait count */
    if (itm_unit.wait > 0)
        return SCPE_OK;                             /* not time yet */
    itm_unit.wait = itm_tps;                        /* reset wait count */

    if (itm_run) {                                  /* see if timer running */
        itm_cnt--;                                  /* down count by one */
        if ((itm_cnt == 0) && itm_pie) {            /* see if reached 0 yet */
//      if (itm_cnt == 0) {                         /* see if reached 0 yet */
//fprintf(stderr, "itm_srv REQ itm_pie %x wait %x itm_cnt %x\r\n", itm_pie, itm_unit.wait, itm_cnt);
            INTS[itm_lvl] |= INTS_REQ;              /* request the interrupt on zero value */
            irq_pend = 1;                           /* make sure we scan for int */
        }   
    }
#if 0
    itm_unit.wait = sim_rtcn_calb (itm_tps, TMR_ITM);   /* calibrate */
    sim_activate (&itm_unit, itm_unit.wait);        /* reactivate */
#endif
    return SCPE_OK;
}

/* ITM read/load function called from CD command processing */
/* level = interrupt level */
/* cmd = 0x39 load and enable interval timer, no return value */
/*     = 0x40 read timer value */
/*     = 0x60 read timer value and stop timer */
/*     = 0x79 read/reload and start timer */
/* cnt = value to write to timer */
/* ret = return value read from timer */
int32 itm_rdwr(uint32 cmd, int32 cnt, uint32 level)
{
    uint32  temp;
//  uint32  val = SPAD[level+0x80];                 /* get SPAD value for interrupt vector */

//  itm_lvl = level;                                /* save the interrupt level */
//  uint32 addr = SPAD[0xf1] + (level<<2);          /* vector address in SPAD */
//  addr = M[addr>>2];                              /* get the interrupt context block addr */
//fprintf(stderr, "itm_rdwr called ss %x level %x SPAD %x icba %x\r\n", ss, level, val, addr);
//fprintf(stderr, "itm_rdwr called cmd %x count %x (%d) level %x return cnt %x (%d)\r\n",
//  cmd, cnt, cnt, level, itm_cnt, itm_cnt);
    switch (cmd) {
    case 0x39:                                      /* load timer with new value and start*/
        if (cnt < 0)
            cnt = 26042;                            /* TRY ??*/
        itm_cnt = cnt;                              /* load timer with value from user to down count */
        itm_run = 1;                                /* start timer */
        return 0;                                   /* does not matter, no value returned  */
    case 0x60:                                      /* read and stop timer */
        temp = itm_cnt;                             /* get timer value and stop timer */
        itm_run = 0;                                /* stop timer */
//      itm_cnt = 0;                                /* reset with timer value from user to down count */
        return temp;                                /* return current count value */
    case 0x79:                                      /* read the current timer value */
        temp = itm_cnt;                             /* get timer value, load new value and start timer */
        itm_cnt = cnt;                              /* load timer with value from user to down count */
        itm_run = 1;                                /* start timer */
        return temp;                                /* return current count value */
    case 0x40:                                      /* read the current timer value */
        return itm_cnt;                             /* return current count value */
        break;
    }
    return 0;                                       /* does not matter, no value returned  */
}

/* Clock interrupt start/stop */
/* ss = 1 - clock interrupt enabled */
/* ss = 0 - clock interrupt disabled */
/* level = interrupt level */
void itm_setup(uint ss, uint32 level)
{
    itm_lvl = level;                                /* save the interrupt level */
// fprintf(stderr, "itm_setup called ss %x level %x\r\n", ss, level);
    if (ss == 1) {                                  /* starting? */
        INTS[level] |= INTS_ENAB;                   /* make sure enabled */
        SPAD[level+0x80] |= SINT_ENAB;              /* in spad too */
        INTS[level] |= INTS_REQ;                    /* request the interrupt */
        itm_cnt = 26042;                            /* start with 1 sec */
        itm_run = 0;                                /* not running yet */
///     sim_activate(&itm_unit, 48);                /* start us off */
    } else {
        INTS[level] &= ~INTS_ENAB;                  /* make sure disabled */
        SPAD[level+0x80] &= ~SINT_ENAB;             /* in spad too */
    }
    itm_pie = ss;                                   /* set new state */
}

/* Clock reset */
t_stat itm_reset (DEVICE *dptr)
{
//  int intlev = 0x5f;                              /* interrupt level for itm */
//fprintf(stderr, "itm_reset called\r\n");
    itm_pie = 0;                                    /* disable pulse */
    itm_cnt = 26042;                                /* start with 1 sec */
    itm_run = 0;                                    /* not running yet */
#if 0
    rtc_unit.wait = sim_rtcn_init (itm_unit.wait, TMR_ITM); /* initialize clock calibration */
    sim_activate (&itm_unit, itm_unit.wait);        /* activate unit */
#endif
    return SCPE_OK;
}

/* Set frequency */
t_stat itm_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (cptr)                                       /* if chars, bad */
        return SCPE_ARG;                            /* ARG error */
    if ((val != 384) && (val != 768))
        return SCPE_IERR;                           /* scope error */
    itm_tps = val/10;                               /* set the new frequency */
    return SCPE_OK;                                 /* we done */
}

/* Show frequency */
t_stat itm_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    /* print the cirrent frequency setting */
    fprintf (st, (itm_tps == 38)? "38.4us": "76.8us");
    return SCPE_OK;
}
#endif

