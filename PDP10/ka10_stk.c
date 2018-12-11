/* ka10_stk.c: Stanford keyboard.

   Copyright (c) 2018, Lars Brinkhoff

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

   This is a device which interfaces with a Stanford keyboard.  It's
   specific to the MIT AI lab PDP-10.
*/

#include <time.h>
#include "display/display.h"
#include "ka10_defs.h"

#define STK_DEVNUM      070

/* CONI/O bits. */
#define STK_PIA         0000007
#define STK_DONE        0000010

/* Bucky bits. */
#define SHFT   00100
#define CTRL   00200
#define TOP    00400
#define META   01000

static t_stat      stk_svc (UNIT *uptr);
static t_stat      stk_devio(uint32 dev, uint64 *data);
static const char  *stk_description (DEVICE *dptr);

static uint64 status = 0;
static int key_code = 0;

UNIT                stk_unit[] = {
    {UDATA(stk_svc, UNIT_DISABLE, 0)},  /* 0 */
};
DIB stk_dib = {STK_DEVNUM, 1, &stk_devio, NULL};

MTAB stk_mod[] = {
    { 0 }
    };

DEVICE              stk_dev = {
    "STK", stk_unit, NULL, stk_mod,
    1, 8, 0, 1, 8, 36,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &stk_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG, 0, NULL,
    NULL, NULL, NULL, NULL, NULL, &stk_description
};

/* Special key codes. */
#define CR    033
#define BKSL  034
#define LF    035
#define TAB   045
#define FF    046
#define VT    047
#define BS    074
#define ALT   077 /* Not sure if 42, 75, 76, or 77. */

/* This maps ASCII codes to Stanford key codes plus bucky bits. */
static int translate[] = {
  0,       CTRL|001,CTRL|002,CTRL|003,CTRL|004,CTRL|005,CTRL|006,CTRL|007,
  CTRL|010,TAB,     LF,      VT,      FF,      CR,      CTRL|016,CTRL|017,
  CTRL|020,CTRL|021,CTRL|022,CTRL|023,CTRL|024,CTRL|025,CTRL|026,CTRL|027,
  CTRL|030,CTRL|031,CTRL|032,ALT,     CTRL|034,CTRL|035,0,       CTRL|037,
  ' ',     SHFT|',',TOP|031, TOP|022, SHFT|'6',SHFT|'7',TOP|024, TOP|011,
  '(',     ')',     '*',     '+',     ',',     '-',     '.',     '/',
  '0',     '1',     '2',     '3',     '4',     '5',     '6',     '7',
  '8',     '9',     ':',     ';',     TOP|004, TOP|010, TOP|006, TOP|'.',
  TOP|005, SHFT|001,SHFT|002,SHFT|003,SHFT|004,SHFT|005,SHFT|006,SHFT|007,
  SHFT|010,SHFT|011,SHFT|012,SHFT|013,SHFT|014,SHFT|015,SHFT|016,SHFT|017,
  SHFT|020,SHFT|021,SHFT|022,SHFT|023,SHFT|024,SHFT|025,SHFT|026,SHFT|027,
  SHFT|030,SHFT|031,SHFT|032,TOP|'(', BKSL,    TOP|')', 0,       TOP|'9',
  TOP|025, 001,     002,     003,     004,     005,     006,     007,
  010,     011,     012,     013,     014,     015,     016,     017,
  020,     021,     022,     023,     024,     025,     026,     027,
  030,     031,     032,     TOP|017, SHFT|'+',TOP|020, SHFT|'8',BS
};

static t_stat stk_svc (UNIT *uptr)
{
  int c = SCPE_OK;

#ifdef USE_DISPLAY
  if (display_last_char) {
    c = display_last_char | SCPE_KFLAG;
    display_last_char = 0;
  }
#endif

  if (c & SCPE_KFLAG) {
    key_code = translate[c & 0177];
    status |= STK_DONE;
    set_interrupt(STK_DEVNUM, status & STK_PIA);
  }

  sim_activate (uptr, 100000);

  if (c & SCPE_KFLAG)
    return SCPE_OK;
  else
    return c;
}

t_stat stk_devio(uint32 dev, uint64 *data)
{
    DEVICE *dptr = &stk_dev;

    switch(dev & 07) {
    case CONO:
        status &= ~STK_PIA;
        status |= *data & STK_PIA;
        if (status & STK_PIA)
          sim_activate (stk_unit, 1);
        else
          sim_cancel (stk_unit);
        break;
    case CONI:
        *data = status;
        break;
    case DATAO:
        break;
    case DATAI:
        status &= ~STK_DONE;
        clr_interrupt(STK_DEVNUM);
        *data = key_code;
        break;
    }

    return SCPE_OK;
}

const char *stk_description (DEVICE *dptr)
{
    return "Stanford keyboard";
}
