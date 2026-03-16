/* ka10_cart.c: Stanford cart, with audiovisual system indicators.

   Copyright (c) 2021, Lars Brinkhoff

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
   LARS BRINKHOFF BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This is a device which interfaces with the Stanford cart.  It also
   controls three lights and a solenoid to ring a bell.  It's specific
   to the SAIL PDP-10.  The hardware interface is documented in the
   UUO manual.
*/

#include "kx10_defs.h"

#if NUM_DEVS_CART > 0

#define CART_DEVNUM 0354

/* CONO bits. */
#define CART_UDP    0000001LL /* UDP in use. */
#define CART_RED    0000004LL /* System crash. */
#define CART_YEL    0000010LL /* System crash. */
#define CART_TUN    0000720LL /* TV tuner. */
#define CART_BEL    0001000LL /* System being debugged. */
#define CART_DRV    0001000LL /* Cart drive direction. */
#define CART_ON     0002000LL /* Cart drive on. */
#define CART_STR    0004000LL /* Cart steer right. */
#define CART_STL    0010000LL /* Cart steer left. */
#define CART_PNR    0020000LL /* Cart pan right. */
#define CART_PNL    0040000LL /* Cart pan left. */
#define CART_MASK   0177777LL
#define CART_GRN    0200000LL /* One-shot: system running. */
#define CART_OFF    0400000LL

/* Delay for one-shot action, in microseconds. */
#define CART_ONESHOT  1000000

/* Device state is 18 bits. */
#define cart_bits cart_unit.u3

static t_stat cart_devio(uint32 dev, uint64 *data);
static t_stat cart_svc(UNIT *uptr);
static t_stat cart_reset (DEVICE *dptr);
static const char  *cart_description (DEVICE *dptr);

DIB cart_dib = { CART_DEVNUM, 1, &cart_devio, NULL };

UNIT cart_unit = {
    UDATA (&cart_svc, UNIT_IDLE, 0)
};

REG cart_reg[] = {
    {ORDATA(BITS, cart_bits, 18)},
    {0}
};

DEVICE cart_dev = {
    "CART", &cart_unit, cart_reg, NULL,
    1, 8, 0, 1, 8, 36,
    NULL, NULL, &cart_reset, NULL, NULL, NULL,
    &cart_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, NULL,
    NULL, NULL, NULL, NULL, NULL, &cart_description
};

void cart_on (uint64 bits)
{
    bits &= ~cart_bits;
    cart_bits |= bits;
    if (bits & CART_UDP)
        sim_debug (DEBUG_CMD, &cart_dev, "UDP in use lamp on.");
    if (bits & CART_RED)
        sim_debug (DEBUG_CMD, &cart_dev, "Red lamp on.");
    if (bits & CART_YEL)
        sim_debug (DEBUG_CMD, &cart_dev, "Yellow lamp on.");
    if (bits & CART_BEL)
        sim_debug (DEBUG_CMD, &cart_dev, "Bell!");
    if (bits & CART_GRN)
        sim_debug (DEBUG_CMD, &cart_dev, "Green lamp on.");
    if (bits & CART_TUN)
        sim_debug (DEBUG_CMD, &cart_dev, "Frobbing TV tuner.");
}

void cart_off (uint64 bits)
{
    bits &= cart_bits;
    cart_bits &= ~bits;
    if (bits & CART_UDP)
        sim_debug (DEBUG_CMD, &cart_dev, "UDP in use lamp off.");
    if (bits & CART_RED)
        sim_debug (DEBUG_CMD, &cart_dev, "Red lamp off.");
    if (bits & CART_YEL)
        sim_debug (DEBUG_CMD, &cart_dev, "Yellow lamp off.");
    if (bits & CART_GRN)
        sim_debug (DEBUG_CMD, &cart_dev, "Green lamp off.");
    if (bits & CART_TUN)
        sim_debug (DEBUG_CMD, &cart_dev, "Frobbing TV tuner.");
}

void cart_oneshot (void)
{
    /* This is a "one-shot" action. */
    sim_debug(DEBUG_DETAIL, &cart_dev, "Trigger one shot.");
    cart_on (CART_GRN);
    cart_off (CART_GRN << 1);
    sim_cancel (&cart_unit);
    sim_activate_after (&cart_unit, CART_ONESHOT);
}

static t_stat cart_svc(UNIT *uptr)
{
    sim_debug(DEBUG_DETAIL, &cart_dev, "One shot expired.");
    cart_off (CART_GRN);
    cart_on (CART_GRN << 1);
    return SCPE_OK;
}

t_stat cart_devio(uint32 dev, uint64 *data)
{
    uint64 bits;

    switch(dev & 07) {
    case CONO|4:
        bits = *data;
        sim_debug(DEBUG_CONO, &cart_dev, "%06llo", bits);
        if (bits & CART_GRN)
            cart_oneshot ();
        bits &= CART_MASK;
        if (*data & CART_OFF)
            cart_off (bits);
        else
            cart_on (bits);
        break;
    case CONI|4:
        *data = cart_bits & CART_MASK;
        break;
    /* This device doesn't respond to DATAI/O. */
    }

    return SCPE_OK;
}

static t_stat cart_reset (DEVICE *dptr)
{
    if (sim_switches & SWMASK('P'))
        cart_bits = 0;
    return SCPE_OK;
}

const char *cart_description (DEVICE *dptr)
{
    return "Stanford cart";
}
#endif
