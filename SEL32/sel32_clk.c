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
t_stat rtc_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
const char *rtc_desc(DEVICE *dptr);

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
/* default to 60 HZ RTC */
UNIT rtc_unit = { UDATA (&rtc_srv, UNIT_IDLE, 0), 16666, UNIT_ADDR(0x7F06)};

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
    NULL, NULL, &rtc_reset,         /* examine, deposit, reset */
    NULL, NULL, NULL,               /* boot, attach, detach */
//  NULL, 0, 0, NULL,               /* dib, dev flags, debug flags, debug */
    NULL, DEV_DEBUG, 0, dev_debug,  /* dib, dev flags, debug flags, debug */
    NULL, NULL, &rtc_help,          /* ?, ?, help */
    NULL, NULL, &rtc_desc,          /* ?, ?, description */
    };

/* The real time clock runs continuously; therefore, it only has
   a unit service routine and a reset routine.  The service routine
   sets an interrupt that invokes the clock counter.
*/

/* service clock signal from simulator */
t_stat rtc_srv (UNIT *uptr)
{
    if (rtc_pie) {                                  /* set pulse intr */
        time_t result = time(NULL);
//      fprintf(stderr, "Clock int time %08x\r\n", (uint32)result);
        sim_debug(DEBUG_CMD, &rtc_dev, "RT Clock int time %08x\n", (uint32)result);
        INTS[rtc_lvl] |= INTS_REQ;                  /* request the interrupt */
        irq_pend = 1;                               /* make sure we scan for int */
    }
    rtc_unit.wait = sim_rtcn_calb (rtc_tps, TMR_RTC);   /* calibrate */
    sim_activate_after (&rtc_unit, 1000000/rtc_tps);/* reactivate 16666 tics / sec */
    return SCPE_OK;
}

/* Clock interrupt start/stop */
/* ss = 1 - starting clock */
/* ss = 0 - stopping clock */
/* level = interrupt level */
void rtc_setup(uint32 ss, uint32 level)
{
//  uint32 val = SPAD[level+0x80];                  /* get SPAD value for interrupt vector */
    uint32 addr = SPAD[0xf1] + (level<<2);          /* vector address in SPAD */

    rtc_lvl = level;                                /* save the interrupt level */
    addr = M[addr>>2];                              /* get the interrupt context block addr */
    if (ss == 1) {                                  /* starting? */
        INTS[level] |= INTS_ENAB;                   /* make sure enabled */
        SPAD[level+0x80] |= SINT_ENAB;              /* in spad too */
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
    rtc_unit.wait = sim_rtcn_init_unit(&rtc_unit, rtc_unit.wait, TMR_RTC); /* initialize clock calibration */
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
    /* print the current frequency setting */
    if (rtc_tps < 100)
        fprintf (st, (rtc_tps == 50)? "50Hz": "60Hz");
    else
        fprintf (st, (rtc_tps == 100)? "100Hz": "120Hz");
    return SCPE_OK;
}

/* sho help rtc */
t_stat rtc_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
    fprintf(st, "SEL 32 IOP realtime clock at 0x7F06\r\n");
    fprintf(st, "Use:\r\n");
    fprintf(st, "    sim> SET RTC [50][60][100][120]\r\n");
    fprintf(st, "to set clock interrupt rate in HZ\r\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

/* device description */
const char *rtc_desc(DEVICE *dptr)
{
    return "SEL IOP realtime clock @ address 0x7F06";
}

/************************************************************************/

/* Interval Timer support */
int32 itm_pie = 0;                                  /* itm pulse enable */
int32 itm_cmd = 0;                                  /* itm last user cmd */
int32 itm_cnt = 0;                                  /* itm pulse count enable */
int32 itm_tick_size_x_100 = 3840;                   /* itm 26041 ticks/sec = 38.4 us per tic */
int32 itm_lvl = 0x5f;                               /* itm interrupt level */
t_stat itm_srv (UNIT *uptr);
t_stat itm_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat itm_reset (DEVICE *dptr);
t_stat itm_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat itm_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr);
const char *itm_desc(DEVICE *dptr);

/* Clock data structures

   itm_dev      Interval Timer ITM device descriptor
   itm_unit     Interval Timer ITM unit
   itm_reg      Interval Timer ITM register list
*/

UNIT itm_unit = { UDATA (&itm_srv, UNIT_IDLE, 0), 26042, UNIT_ADDR(0x7F04)};

REG itm_reg[] = {
    { FLDATA (PIE, itm_pie, 0) },
    { FLDATA (CNT, itm_cnt, 0) },
    { FLDATA (CMD, itm_cmd, 0) },
    { DRDATA (TICK_SIZE, itm_tick_size_x_100, 32), PV_LEFT + REG_HRO },
    { NULL }
    };

MTAB itm_mod[] = {
    { MTAB_XTD|MTAB_VDV, 3840, NULL, "3840us",
      &itm_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 7680, NULL, "7680us",
      &itm_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "RESOLUTION", NULL,
      NULL, &itm_show_freq, NULL },
    { 0 }
    };

DEVICE itm_dev = {
    "ITM", &itm_unit, itm_reg, itm_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &itm_reset,         /* examine, deposit, reset */
    NULL, NULL, NULL,               /* boot, attach, detach */
//  NULL, 0, 0, NULL,               /* dib, ?, ?, debug */
    NULL, DEV_DEBUG, 0, dev_debug,  /* dib, dev flags, debug flags, debug */
    NULL, NULL, &itm_help,          /* ?, ?, help */
    NULL, NULL, &itm_desc,          /* ?, ?, description */
    };

/* The interval timer downcounts the value it is loaded with and
   runs continuously; therefore, it has a read/write routine,
   a unit service routine and a reset routine.  The service routine
   sets an interrupt that invokes the clock counter.
*/

/* service clock expiration from simulator */
/* cause interrupt */
t_stat itm_srv (UNIT *uptr)
{
    if (itm_pie) {                              /* interrupt enabled? */
        time_t result = time(NULL);
//        fprintf(stderr, "Clock int time %08x\r\n", (uint32)result);
        sim_debug(DEBUG_CMD, &itm_dev, "Interval Timer expired interrupt time %08x\n", (uint32)result);
        INTS[itm_lvl] |= INTS_REQ;              /* request the interrupt on zero value */
        irq_pend = 1;                           /* make sure we scan for int */
        if (itm_cmd == 0x3d) {
            /* restart timer with value from user */
            sim_activate_after_abs_d (&itm_unit, ((double)itm_cnt * itm_tick_size_x_100) / 100.0);
        }
    }   
    return SCPE_OK;
}

/* ITM read/load function called from CD command processing */
/* level = interrupt level */
/* cmd = 0x20 stop timer, do not transfer any value */
/*     = 0x39 load and enable interval timer, no return value */
/*     = 0x3d load and enable interval timer, countdown to zero, interrupt and reload */
/*     = 0x40 read timer value */
/*     = 0x60 read timer value and stop timer */
/*     = 0x79 read/reload and start timer */
/* cnt = value to write to timer */
/* ret = return value read from timer */
int32 itm_rdwr(uint32 cmd, int32 cnt, uint32 level)
{
    uint32  temp;

    itm_cmd = cmd;                                  /* save last cmd */
    switch (cmd) {
    case 0x20:                                      /* stop timer */
//      fprintf(stderr, "clk kill value %08x (%08d)\r\n", cnt, cnt);
        sim_debug(DEBUG_CMD, &itm_dev, "clk kill value %08x (%08d)\n", cnt, cnt);
        sim_cancel (&itm_unit);                     /* cancel itc */
        itm_cnt = 0;                                /* no count reset value */
        return 0;                                   /* does not matter, no value returned  */
    case 0x39:                                      /* load timer with new value and start*/
//      sim_debug(DEBUG_CMD, &itm_dev, "clk 0x39 init value %08x (%08d)\n", cnt, cnt);
        if (cnt <= 0)
            cnt = 26042;                            /* 0x65ba TRY 1,000,000/38.4 */
//      fprintf(stderr, "clk 0x39 init value %08x (%08d)\r\n", cnt, cnt);
        sim_debug(DEBUG_CMD, &itm_dev, "clk 0x39 init value %08x (%08d)\n", cnt, cnt);
        /* start timer with value from user */
        sim_activate_after_abs_d (&itm_unit, ((double)cnt * itm_tick_size_x_100) / 100.0);
        itm_cnt = 0;                                /* no count reset value */
        return 0;                                   /* does not matter, no value returned  */
    case 0x3d:                                      /* load timer with new value and start*/
//      fprintf(stderr, "clk 0x3d init value %08x (%08d)\r\n", cnt, cnt);
        sim_debug(DEBUG_CMD, &itm_dev, "clk 0x3d init value %08x (%08d)\n", cnt, cnt);
        /* start timer with value from user, reload on zero time */
        sim_activate_after_abs_d (&itm_unit, ((double)cnt * itm_tick_size_x_100) / 100.0);
        itm_cnt = cnt;                              /* count reset value */
        return 0;                                   /* does not matter, no value returned  */
    case 0x60:                                      /* read and stop timer */
        /* get timer value and stop timer */
        temp = (uint32)(100.0 * sim_activate_time_usecs (&itm_unit) / itm_tick_size_x_100);
//      fprintf(stderr, "clk 0x60 temp value %08x (%08d)\r\n", temp, temp);
        sim_debug(DEBUG_CMD, &itm_dev, "clk 0x60 temp value %08x (%08d)\n", temp, temp);
        sim_cancel (&itm_unit);
        return temp;                                /* return current count value */
    case 0x79:                                      /* read the current timer value */
        /* get timer value, load new value and start timer */
        temp = (uint32)(100.0 * sim_activate_time_usecs (&itm_unit) / itm_tick_size_x_100);
//      fprintf(stderr, "clk 0x79 temp value %08x (%08d)\r\n", temp, temp);
//      fprintf(stderr, "clk 0x79 init value %08x (%08d)\r\n", cnt, cnt);
        sim_debug(DEBUG_CMD, &itm_dev, "clk 0x79 temp value %08x (%08d)\n", temp, temp);
        sim_debug(DEBUG_CMD, &itm_dev, "clk 0x79 init value %08x (%08d)\n", cnt, cnt);
        /* start timer to fire after cnt ticks */
        sim_activate_after_abs_d (&itm_unit, ((double)cnt * itm_tick_size_x_100) / 100.0);
        itm_cnt = 0;                                /* no count reset value */
        return temp;                                /* return current count value */
    case 0x40:                                      /* read the current timer value */
        /* return current count value */
        temp = (uint32)(100.0 * sim_activate_time_usecs (&itm_unit) / itm_tick_size_x_100);
//      fprintf(stderr, "clk 0x40 temp value %08x (%08d)\r\n", temp, temp);
        sim_debug(DEBUG_CMD, &itm_dev, "clk 0x40 temp value %08x (%08d)\n", temp, temp);
        return temp;
        break;
    }
    return 0;                                       /* does not matter, no value returned  */
}

/* Clock interrupt start/stop */
/* ss = 1 - clock interrupt enabled */
/* ss = 0 - clock interrupt disabled */
/* level = interrupt level */
void itm_setup(uint32 ss, uint32 level)
{
    itm_lvl = level;                                /* save the interrupt level */
    if (ss == 1) {                                  /* starting? */
        INTS[level] |= INTS_ENAB;                   /* make sure enabled */
        SPAD[level+0x80] |= SINT_ENAB;              /* in spad too */
//DIAG  INTS[level] |= INTS_REQ;                    /* request the interrupt */
        sim_cancel (&itm_unit);                     /* not running yet */
    } else {
        INTS[level] &= ~INTS_ENAB;                  /* make sure disabled */
        SPAD[level+0x80] &= ~SINT_ENAB;             /* in spad too */
    }
    itm_pie = ss;                                   /* set new state */
}

/* Clock reset */
t_stat itm_reset (DEVICE *dptr)
{
    itm_pie = 0;                                    /* disable pulse */
    sim_cancel (&itm_unit);                         /* not running yet */
    return SCPE_OK;
}

/* Set frequency */
t_stat itm_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (cptr)                                       /* if chars, bad */
        return SCPE_ARG;                            /* ARG error */
    if ((val != 3840) && (val != 7680))
        return SCPE_IERR;                           /* scope error */
    itm_tick_size_x_100 = val;                      /* set the new frequency */
    return SCPE_OK;                                 /* we done */
}

/* Show frequency */
t_stat itm_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    /* print the current interval count setting */
    fprintf (st, "%0.2fus", (itm_tick_size_x_100 / 100.0));
    return SCPE_OK;
}

/* sho help rtc */
t_stat itm_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, CONST char *cptr)
{
    fprintf(st, "SEL 32 IOP interval timer at 0x7F04\r\n");
    fprintf(st, "Use:\r\n");
    fprintf(st, "    sim> SET ITM [3840][7680]\r\n");
    fprintf(st, "to set interval timer clock rate in us x 100\r\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

/* device description */
const char *itm_desc(DEVICE *dptr)
{
    return "SEL IOP Interval Timer @ address 0x7F04";
}

#endif

