/* ka10_dk.c: PDP-10 DK subsystem simulator

   Copyright (c) 1993-2012, Richard Cornwell

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell.

*/

#include "ka10_defs.h"
#include <time.h>

#ifndef NUM_DEVS_DK
#define NUM_DEVS_DK 0
#endif

#if (NUM_DEVS_DK > 0)

#define DK_DEVNUM       070
#define STAT_REG        u3
#define CLK_REG         u4
#define INT_REG         u5

/* CONO */
#define PIA             000007
#define CLK_CLR_FLG     000010
#define CLK_CLR_OVF     000020
#define CLK_SET_EN      000040
#define CLK_CLR_EN      000100
#define CLK_SET_PI      000200
#define CLK_CLR_PI      000400
#define CLK_GEN_CLR     001000
#define CLK_ADD_ONE     002000
#define CLK_SET_FLG     004000
#define CLK_SET_OVF     010000

/* CONI */
#define CLK_FLG         000010
#define CLK_OVF         000020
#define CLK_EN          000040
#define CLK_PI          000200
#define CLK_EXT         001000

/* Invariants */

#define TIM_TPS         100000

/* Exported variables */

int32 clk_tps = TIM_TPS;                            /* clock ticks/sec */
int32 tmr_poll = TIM_WAIT;                          /* clock poll */
int32 tmxr_poll = TIM_WAIT * TIM_MULT;            /* term mux poll */

extern UNIT cpu_unit;

DEVICE dk_dev;
t_stat dk_devio(uint32 dev, uint64 *data);
void tim_incr_base (d10 *base, d10 incr);

/* TIM data structures

   tim_dev      TIM device descriptor
   tim_unit     TIM unit descriptor
   tim_reg      TIM register list
*/

DIB dk_dib = { DK_DEVNUM, 1, &dk_devio };

UNIT dk_unit = { 
        {UDATA (&dk_svc, UNIT_IDLE, 0), TIM_WAIT },
#if (NUM_DEVS_DK > 1)
        {UDATA (&dk_svc, UNIT_IDLE, 0), TIM_WAIT },
#endif
        };

REG dk_reg[] = {
    { NULL }
    };

MTAB dk_mod[] = {
    { 0 }
    };

DEVICE tim_dev = {
    "DK", &dk_unit, dk_reg, dk_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &dk_reset,
    NULL, NULL, NULL,
    &dk_dib, NULL,  0, NULL,
    NULL, NULL, &dk_help, NULL, NULL, &dk_description
    };

t_stat dk_devio(uint32 dev, uint64 *data) {
    uint64      res;
    int         unit = (dev - DK_DEVNUM) >> 2;
    UNIT        *uptr = dk_unit[unit];

    if (unit < 0 || unit > NUM_DEVS_DK)
        return SCPE_OK;
    switch (dev & 3) {
    case CONI:
        *data = uptr->STAT_REG;
        break;

    case CONO:
        /* Adjust U3 */
        clr_interrupt(dev);
        uptr->STAT_REG &= ~07;
        if (CLK_GEN_CLR & *data) {
            uptr->STAT_REG = 0;
            uptr->CLK_REG = 0;
            uptr->INT_REG = 0;
        } else {
            uptr->STAT_REG &= ~((CLK_CLR_FLG|CLK_CLR_OVR) & *data);
            uptr->STAT_REG &= ~(((CLK_CLR_EN|CLK_CLR_PI) & *data) >> 1);
            uptr->STAT_REG |= (CLK_SET_EN|CLK_SET_PI|7) & *data;
            uptr->STAT_REG |= ((CLK_SET_FLG|CLK_SET_OVR) & *data) >> 8; 
        }
        if ((CLK_ADD_ONE & *data) && (uptr->STAT_REG & CLK_EN) == 0) {
            uptr->CLK_REG = (uptr->CLK_REG + 1);
            if (uptr->CLK_REG & LMASK) 
                uptr->STAT_REG |= CLK_OVF;
            uptr->CLK_REG &= RMASK;
        }
        if (uptr->CLK_REG == uptr->INT_REG) {
            uptr->STAT_REG |= CLK_FLG;
        }

        if ((uptr->STAT_REG & CLK_EN) != 0) &&
                (uptr->STAT_REG & (CLK_FLG|CLK_OVF)) {
                set_interrupt(dev, uptr->STAT_REG & 7);
        }
        break;

    case DATAO:
        uptr->INT_REG & RMASK;
        if (uptr->CLK_REG == uptr->INT_REG) {
            uptr->STAT_REG |= CLK_FLG;
        }

        if ((uptr->STAT_REG & CLK_EN) != 0) &&
                (uptr->STAT_REG & (CLK_FLG|CLK_OVF)) {
                set_interrupt(dev, uptr->STAT_REG & 7);
        }
        break;
   
    case DATAI:
        *data = uptr->CLK_REG;
        break;
    }

    return SCPE_OK;
}

/* Timer instructions */

/* Timer - if the timer is running at less than hardware frequency,
   need to interpolate the value by calculating how much of the current
   clock tick has elapsed, and what that equates to in msec. */

t_bool rdtim (a10 ea, int32 prv)
{
d10 tempbase[2];

ReadM (INCA (ea), prv);                                 /* check 2nd word */
tempbase[0] = tim_base[0];                              /* copy time base */
tempbase[1] = tim_base[1];
if (tim_mult != TIM_MULT_T20) {                         /* interpolate? */
    int32 used;
    d10 incr;
    used = tmr_poll - (sim_is_active (&tim_unit) - 1);
    incr = (d10) (((double) used * TIM_HW_FREQ) /
        ((double) tmr_poll * (double) clk_tps));
    tim_incr_base (tempbase, incr);
    }
tempbase[0] = tempbase[0] & ~((d10) TIM_HWRE_MASK);     /* clear low 12b */
Write (ea, tempbase[0], prv);
Write (INCA(ea), tempbase[1], prv);
return FALSE;
}

/* Timer service - the timer is only serviced when the 'ttg' register
   has reached 0 based on the expected frequency of clock interrupts. */

t_stat dk_svc (UNIT *uptr)
{
tmr_poll = sim_rtc_calb (clk_tps);                      /* calibrate */
sim_activate (uptr, tmr_poll);                          /* reactivate unit */
tmxr_poll = tmr_poll * tim_mult;                        /* set mux poll */
tim_incr_base (tim_base, tim_period);                   /* incr time base */
tim_ttg = tim_period;                                   /* reload */
apr_flg = apr_flg | APRF_TIM;                           /* request interrupt */
if (Q_ITS) {                                            /* ITS? */
    if (pi_act == 0)
            quant = (quant + TIM_ITS_QUANT) & DMASK;
    if (TSTS (pcst)) {                                  /* PC sampling? */
        WriteP ((a10) pcst & AMASK, pager_PC);          /* store sample */
        pcst = AOB (pcst);                              /* add 1,,1 */
        }
    }                                                   /* end ITS */
else if (t20_idlelock && PROB (100 - tim_t20_prob))
    t20_idlelock = 0;
return SCPE_OK;
}

/* Clock coscheduling routine */

int32 clk_cosched (int32 wait)
{
int32 t;

if (tim_mult == TIM_MULT_T20)
    return wait;
t = sim_is_active (&tim_unit);
return (t? t - 1: wait);
}

/* Timer reset */

t_stat dk_reset (DEVICE *dptr)
{
tim_period = 0;                                         /* clear timer */
tim_ttg = 0;
apr_flg = apr_flg & ~APRF_TIM;                          /* clear interrupt */
tmr_poll = sim_rtc_init (tim_unit.wait);                /* init timer */
sim_activate (&tim_unit, tmr_poll);                     /* activate unit */
tmxr_poll = tmr_poll * tim_mult;                        /* set mux poll */
return SCPE_OK;
}


#endif
