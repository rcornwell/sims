/* ka10_iii.c: Triple III display processor.

   Copyright (c) 2013-2019, Richard Cornwell

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

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell

*/

#include "kx10_defs.h"
#ifndef NUM_DEVS_III
#define NUM_DEVS_III 0
#endif

#if NUM_DEVS_III > 0 
#include "display/display.h"
#include "display/iii.h"


#define III_DEVNUM        0430

#define STATUS            u3
#define MAR               u4
#define PIA               u5
#define POS               u6

/* CONO Bits */
#define SET_PIA    000000010    /* Set if this bit is zero */
#define STOP       000000020    /* Stop processor after instruction */
#define CONT       000000040    /* Start execution at address */
#define F          000000100    /* Clear flags */
#define SET_MSK    000360000    /* Set mask */
#define RST_MSK    007400000    /* Reset mask */

/* CONI Bits */
#define PIA_MSK    000000007
#define INST_HLT   000000010    /* Halt instruction */
#define WRAP_ENB   000000020    /* Wrap around mask */
#define EDGE_ENB   000000040    /* Edge interrupt mask */
#define LIGH_ENB   000000100    /* Light pen enable mask */
#define CLK_STOP   000000200    /* Clock stop */
#define CLK_BIT    000001000    /* Clock */
#define NXM_BIT    000002000    /* Non-existent memory */
#define IRQ_BIT    000004000    /* Interrupt pending */
#define DATAO_LK   000010000    /* PDP10 gave DATAO when running */
#define LIGHT_FLG  000020000    /* Light pen flag */
#define WRAP_FLG   000040000    /* Wrap around flag */
#define EDGE_FLG   000100000    /* Edge overflow */
#define HLT_FLG    000200000    /* Not running */

#define WRAP_MSK   00001
#define EDGE_MSK   00002
#define LIGH_MSK   00004
#define HLT_MSK    00010
#define WRP_FBIT   00020
#define EDG_FBIT   00040
#define LIT_FBIT   00100
#define CTL_FBIT   00200
#define HLT_FBIT   00400
#define NXM_FLG    01000
#define DATA_FLG   02000
#define RUN_FLG    04000

#define TSS_INST   012          /* Test  */
#define LVW_INST   006          /* Long Vector */
#define SVW_INST   002          /* Short vector */
#define JMP_INST   000          /* Jump or Halt */
#define JSR_INST   004          /* JSR(1) or JMS(0), SAVE(3) */
#define RES_INST   014          /* Restore */
#define SEL_INST   010          /* Select instruction */

#define CSIZE      00000000007       /* Current char size */
#define CBRT       00000000070       /* Current brightness */
#define POS_Y      00000377700
#define POS_X      01777400000
#define CSIZE_V    0
#define CBRT_V     3
#define POS_X_V    6
#define POS_Y_V    16

/*
 * Character map.
 * M(x,y) moves pointer to x,y.
 * V(x,y) draws a vector between current pointer and x,y.
 * All characters start at 0,6 and end at 8,6.
 * In the map there are up to 18 points per character. For a character a M(0,0) indicates
 * that drawing is done and a move to 8,6 should be done. 
 */
#define M(x,y)   (x << 4)|y|0000
#define V(x,y)   (x << 4)|y|0200

uint8 map[128][18] = {
       { 0 }, 
       { M(0,9), V(3,6), V(3,14), M(3,6), V(6,9) }, 
       { M(6,6), V(3,9), V(1,9), V(0,8), V(0,7), V(1,6), V(3,6), V(6,9) }, 
       { V(2,8), V(2,13), V(3,14), V(5,14), V(6,13), V(6,12), V(5,11), V(2,11), M(5,11),
         V(6,10), V(6,9), V(5,8), V(3,8), V(2,9) }, 
       { M(0,8), V(3,11), V(6,8) }, 
       { M(0,10), V(6,10), V(6,7) }, 
       { M(3,9), V(2,10), V(1,10), V(0,9), V(0,7), V(1,6), V(2,6), V(3,7), M(2,8), V(0,8) }, 
       { M(0,10), V(6,10), M(4,10), V(4,6), M(2,6), V(2,10) }, 
       { V(3,9), M(0,11), V(1,11), V(6,6) }, 
       { M(0,11), V(1,12), V(2,12), V(5,9), V(5,7), V(4,6), V(3,6), V(2,7), V(2,8), V(6,12) }, 
       { M(2,10), V(1,10), V(0,9), V(0,7), V(1,6), V(3,6), V(4,7), V(4,9), V(3,10), V(2,10),
         V(2,12), V(4,12) }, 
       { M(0,7), V(1,6), V(2,6), V(3,7), V(3,12), V(4,13), V(5,13), V(6,12) }, 
       { M(0,9), V(4,9), M(2,11), V(2,7), M(0,7), V(4,7) }, 
       { M(0,8), V(0,7), V(1,6), V(3,6), V(4,7), V(4,9), V(3,10), V(1,10), V(0,9), V(0,8),
         V(4,8), M(2,10), V(2,6) }, 
       { M(0,10), V(1,9), V(2,9), V(4,11), V(5,11), V(6,10), V(5,9), V(4,9), V(2,11), V(1,11),
         V(0,10) }, 
       { M(4,8), V(3,9), V(1,9), V(0,8), V(0,7), V(1,6), V(3,6), V(4,7), V(4,10), V(2,12),
         V(1,12) }, 
       { M(3,11), V(1,11), V(0,10), V(0,8), V(1,7), V(3,7) }, 
       { M(0,11), V(2,11), V(3,10), V(3,8), V(2,7), V(0,7) }, 
       { M(0,7), V(0,9), V(1,10), V(3,10), V(4,9), V(4,7) }, 
       { M(0,10), V(0,8), V(1,7), V(3,7), V(4,8), V(4,10) }, 
       { M(0,13), V(0,8), V(2,6), V(4,6), V(6,8), V(6,13), M(0,10), V(6,10) }, 
       { V(6,6), V(6,14), V(0,14), M(2,10), V(6,10) }, 
       { V(4,10), M(0,10), V(4,6), M(3,6), V(1,6), V(0,7), V(0,9), V(1,10), V(3,10), V(4,9),
         V(4,7), V(3,6) }, 
       { M(2,8), V(0,10), V(2,12), M(0,10), V(6,10), M(4,12), V(6,10), V(4,8) }, 
       { M(0,5), V(6,5) }, 
       { M(0,10), V(6,10), M(3,13), V(6,10), V(3,7) }, 
       { M(0,12), V(2,14), V(4,12), V(6,14) }, 
       { V(6,12), M(0,10), V(6,10), M(0,8), V(6,8) }, 
       { V(3,6), M(3,7), V(0,10), V(3,13) }, 
       { V(3,6), M(0,7), V(3,10), V(0,13) }, 
       { M(0,7), V(6,7), M(6,9), V(0,9), M(0,11), V(6,11) }, 
       { M(0,11), V(3,8), V(6,11) }, 
       { 0, }, 
       { M(2,6), V(2,7), M(2,8), V(2,13) }, 
       { M(2,12), V(2,14), M(4,14), V(4,12) }, 
       { M(2,7), V(2,13), M(4,13), V(4,7), M(6,9), V(0,9), M(0,11), V(6,11) }, 
       { M(0,8), V(2,6), V(4,6), V(6,8), V(4,10), V(2,10), V(0,12), V(2,14), V(4,14), V(6,12),
         M(4,14), V(4,6), M(2,6), V(2,14) }, 
       { V(6,12), V(1,12), V(0,11), V(0,10), V(1,9), V(2,9), V(3,10), V(3,11), V(2,12), M(4,9),
         V(3,8), V(3,7), V(4,6), V(5,6), V(6,7), V(6,8), V(5,9), V(4,9) }, 
       { M(6,6), V(1,11), V(1,13), V(2,14), V(3,14), V(4,13), V(0,9), V(0,7), V(1,6), V(3,6), 
        V(5,8) }, 
       { M(2,12), V(4,14) }, 
       { M(2,6), V(0,8), V(0,12), V(2,14) }, 
       { V(2,8), V(2,12), V(0,14) }, 
       { M(1,8), V(5,12), M(3,13), V(3,7), M(5,8), V(1,12), M(0,10), V(6,10) }, 
       { M(2,7), V(2,11), M(0,9), V(4,9) }, 
       { M(0,7), V(1,6), V(1,5), V(0,4) }, 
       { M(0,9), V(4,9) }, 
       { M(2,6), V(3,6), V(3,7), V(2,7), V(2,6) }, 
       { V(6,12) }, 
       { M(0,7), V(6,13), M(6,12), V(4,14), V(2,14), V(0,12), V(0,8), V(2,6), V(4,6), V(6,8),
         V(6,12) }, 
       { M(1,12), V(3,14), V(3,6) }, 
       { M(0,13), V(1,14), V(4,14), V(6,12), V(6,11), V(5,10), V(2,10), V(0,8), V(0,6), V(6,6) }, 
       { M(0,14), V(6,14), V(6,12), V(4,10), V(5,10), V(6,9), V(6,7), V(5,6), V(0,6) }, 
       { M(5,6), V(5,14), V(0,9), V(6,9) }, 
       { M(0,7), V(1,6), V(4,6), V(6,8), V(6,9), V(5,10), V(1,10), V(0,9), V(0,14), V(6,14) }, 
       { M(0,9), V(1,10), V(5,10), V(6,9), V(6,7), V(5,6), V(1,6), V(0,7), V(0,10), V(4,14) }, 
       { V(6,12), V(6,14), V(0,14) }, 
       { M(1,10), V(0,9), V(0,7), V(1,6), V(5,6), V(6,7), V(6,9), V(5,10), V(6,11), V(6,13),
         V(5,14), V(1,14), V(0,13), V(0,11), V(1,10), V(5,10) }, 
       { M(2,6), V(6,10), V(6,13), V(5,14), V(1,14), V(0,13), V(0,11), V(1,10), V(5,10),
         V(6,11) }, 
       { M(2,6), V(3,6), V(3,7), V(2,7), V(2,6), M(2,10), V(3,10), V(3,11), V(2,11), V(2,10) }, 
       { M(2,7), V(3,6), V(3,5), V(2,4), M(2,10), V(3,10), V(3,11), V(2,11), V(2,10) }, 
       { M(3,7), V(0,10), V(3,13) }, 
       { M(0,8), V(6,8), M(6,10), V(0,10) }, 
       { M(0,7), V(3,10), V(0,13) }, 
       { M(0,13), V(1,14), V(2,13), V(2,12), V(1,11), V(1,8), M(1,7), V(1,6) }, 
       { M(1,6), V(0,7), V(0,11), V(1,12), V(5,12), V(6,11), V(6,8), V(5,7), V(4,8), V(4,11),
         M(4,10), V(3,11), V(2,11), V(1,10), V(1,9), V(2,8), V(3,8), V(4,9) }, 
       { V(0,12), V(2,14), V(4,14), V(6,12), V(6,9), V(0,9), V(6,9), V(6,6) }, 
       { V(0,14), V(5,14), V(6,13), V(6,11), V(5,10), V(0,10), V(5,10), V(6,9), V(6,7), V(5,6),
         V(0,6) }, 
       { M(6,13), V(5,14), V(2,14), V(0,12), V(0,8), V(2,6), V(5,6), V(6,7) }, 
       { V(0,14), V(4,14), V(6,12), V(6,8), V(4,6), V(0,6) }, 
       { M(6,6), V(0,6), V(0,10), V(4,10), V(0,10), V(0,14), V(6,14) }, 
       { V(0,10), V(4,10), V(0,10), V(0,14), V(6,14) }, 
       { M(6,13), V(5,14), V(2,14), V(0,12), V(0,8), V(2,6), V(4,6), V(6,8), V(6,10), V(4,10) }, 
       { V(0,14), V(0,10), V(6,10), V(6,14), V(6,6) }, 
       { M(1,6), V(5,6), V(3,6), V(3,14), V(1,14), V(5,14) }, 
       { M(1,9), V(1,7), V(2,6), V(3,6), V(4,7), V(4,14), V(2,14), V(6,14) }, 
       { V(0,14), V(0,8), V(6,14), V(2,10), V(6,6) }, 
       { M(0,14), V(0,6), V(6,6) }, 
       { V(0,14), V(3,11), V(6,14), V(6,6) }, 
       { V(0,14), V(0,13), V(6,7), V(6,6), V(6,14) }, 
       { M(0,8), V(0,12), V(2,14), V(4,14), V(6,12), V(6,8), V(4,6), V(2,6), V(0,8) }, 
       { V(0,14), V(5,14), V(6,13), V(6,11), V(5,10), V(0,10) }, 
       { M(0,8), V(0,12), V(2,14), V(4,14), V(6,12), V(6,8), V(4,6), V(2,6), V(0,8), M(3,9),
         V(6,6) }, 
       { V(0,14), V(5,14), V(6,13), V(6,11), V(5,10), V(0,10), V(2,10), V(6,6) }, 
       { M(0,8), V(2,6), V(4,6), V(6,8), V(4,10), V(2,10), V(0,12), V(2,14), V(4,14), V(6,12) }, 
       { M(3,6), V(3,14), V(0,14), V(6,14) }, 
       { M(0,14), V(0,7), V(1,6), V(5,6), V(6,7), V(6,14) }, 
       { M(0,14), V(0,9), V(3,6), V(6,9), V(6,14) }, 
       { M(0,14), V(0,6), V(3,9), V(6,6), V(6,14) }, 
       { V(0,7), V(6,13), V(6,14), M(0,14), V(0,13), V(6,7), V(6,6) }, 
       { M(0,14), V(3,11), V(6,14), V(3,11), V(3,6) }, 
       { M(0,14), V(6,14), V(6,13), V(0,7), V(0,6), V(6,6) }, 
       { M(3,5), V(0,5), V(0,15), V(3,15) }, 
       { M(0,12), V(6,6) }, 
       { M(0,5), V(3,5), V(3,15), V(0,15) }, 
       { M(0,11), V(3,14), V(6,11), M(3,14), V(3,6) }, 
       { M(3,7), V(0,10), V(3,13), M(0,10), V(6,10) }, 
       { M(2,14), V(4,12) }, 
       { M(0,9), V(1,10), V(3,10), V(4,9), V(4,6), M(4,8), V(3,9), V(1,9), V(0,8), V(0,7),
         V(1,6), V(3,6), V(4,7) }, 
       { V(0,13), M(0,9), V(1,10), V(3,10), V(4,9), V(4,7), V(3,6), V(1,6), V(0,7) }, 
       { M(4,9), V(3,10), V(1,10), V(0,9), V(0,7), V(1,6), V(3,6), V(4,7) }, 
       { M(0,7), V(0,9), V(1,10), V(3,10), V(4,9), V(4,7), V(3,6), V(1,6), V(0,7), M(4,6),
         V(4,13) }, 
       { M(4,7), V(3,6), V(1,6), V(0,7), V(0,9), V(1,10), V(3,10), V(4,9), V(4,8), V(0,8) }, 
       { M(2,6), V(2,12), V(3,13), V(4,13), V(5,12), M(0,11), V(4,11) }, 
       { M(4,9), V(3,10), V(1,10), V(0,9), V(0,7), V(1,6), V(3,6), V(4,7), M(4,10), V(4,5),
         V(3,4), V(1,4), V(0,5) }, 
       { V(0,13), M(0,9), V(1,10), V(3,10), V(4,9), V(4,6) }, 
       { M(3,12), V(3,11), M(3,10), V(3,7), V(4,6), V(5,6) }, 
       { M(3,12), V(3,11), M(3,10), V(3,5), V(2,4), V(1,3) }, 
       { V(0,13), M(0,8), V(2,10), M(0,8), V(2,6) }, 
       { M(2,6), V(2,13) }, 
       { V(0,10), M(0,9), V(1,10), V(2,10), V(3,9), V(3,6), M(3,9), V(4,10), V(5,10), V(6,9),
         V(6,6) }, 
       { V(0,10), M(0,9), V(1,10), V(2,10), V(3,9), V(3,6) }, 
       { M(0,7), V(0,9), V(1,10), V(3,10), V(4,9), V(4,7), V(3,6), V(1,6), V(0,7) }, 
       { M(0,4), V(0,10), M(0,9), V(1,10), V(3,10), V(4,9), V(4,7), V(3,6), V(1,6), V(0,7) }, 
       { M(4,9), V(3,10), V(1,10), V(0,9), V(0,7), V(1,6), V(3,6), V(4,7), M(4,10), V(4,4) }, 
       { V(0,10), M(0,9), V(1,10), V(3,10), V(4,9) }, 
       { M(0,7), V(1,6), V(3,6), V(4,7), V(3,8), V(1,8), V(0,9), V(1,10), V(3,10), V(4,9) }, 
       { M(2,13), V(2,7), V(3,6), V(4,6), V(5,7), M(1,11), V(3,11) }, 
       { M(0,10), V(0,7), V(1,6), V(3,6), V(4,7), V(4,10), V(4,6) }, 
       { M(0,9), V(3,6), V(6,9) }, 
       { M(0,10), V(0,6), V(2,8), V(4,6), V(4,10) }, 
       { V(4,10), M(0,10), V(4,6) }, 
       { M(0,9), V(3,6), M(6,9), V(1,4), V(0,4) }, 
       { M(0,10), V(4,10), V(0,6), V(4,6) }, 
       { M(3,15), V(2,14), V(2,12), V(0,10), V(2,8), V(2,6), V(3,5) }, 
       { M(2,4), V(2,14) }, 
       { M(3,6), V(0,9), V(3,12), V(6,9), V(3,6) }, 
       { M(0,15), V(1,14), V(1,12), V(3,10), V(1,8), V(1,6), V(0,5) },
       { M(0,12), V(6,6) },
};

uint64         iii_instr;       /* Currently executing instruction */
int            iii_sel;         /* Select mask */

t_stat iii_devio(uint32 dev, uint64 *data);
t_stat iii_svc(UNIT *uptr);
t_stat iii_reset(DEVICE *dptr);
static void draw_point(int x, int y, int b);
static void draw_line(int x1, int y1, int x2, int y2, int b);
t_stat iii_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *iii_description (DEVICE *dptr);

DIB iii_dib = { III_DEVNUM, 1, iii_devio, NULL};

UNIT iii_unit[] = {
    {UDATA (&iii_svc, UNIT_IDLE, 0) },
    { 0 }
    };


MTAB iii_mod[] = {
    { 0 }
    };

DEVICE iii_dev = {
    "III", iii_unit, NULL, iii_mod,
    2, 10, 31, 1, 8, 8,
    NULL, NULL, iii_reset,
    NULL, NULL, NULL, &iii_dib, DEV_DEBUG | DEV_DISABLE | DEV_DIS, 0, dev_debug,
    NULL, NULL, &iii_help, NULL, NULL, &iii_description
    };

int status;
t_stat iii_devio(uint32 dev, uint64 *data) {
     UNIT       *uptr = &iii_unit[0];
     switch(dev & 3) {
     case CONI:
        *data = (((uint64)iii_sel) << 18) | (uint64)(uptr->PIA);
        if ((iii_instr & 037) == 0)
            *data |= INST_HLT;
        *data |= (uptr->STATUS & 07) << 4;
        if (uptr->STATUS & NXM_FLG)
            *data |= NXM_BIT;
        if (uptr->STATUS & DATA_FLG)
            *data |= DATAO_LK;
        if ((uptr->STATUS & RUN_FLG) == 0)
            *data |= HLT_FLG;
        if (uptr->STATUS & WRP_FBIT)
            *data |= WRAP_FLG;
        if (uptr->STATUS & EDG_FBIT)
            *data |= EDGE_FLG;
        if (uptr->STATUS & LIT_FBIT)
            *data |= LIGHT_FLG;
        sim_debug(DEBUG_CONI, &iii_dev, "III %03o CONI %06o\n", dev, (uint32)*data);
        break;
     case CONO:
         if (*data & SET_PIA)
            uptr->PIA = (int)(*data&PIA_MSK);
         if (*data & F)
            uptr->STATUS &= ~(WRP_FBIT|EDG_FBIT|LIT_FBIT|DATA_FLG|NXM_FLG);
         uptr->STATUS &= ~(017 & ((*data >> 14) ^ (*data >> 10)));
         uptr->STATUS ^= (017 & (*data >> 10));
         if (*data & STOP)
             uptr->STATUS |= RUN_FLG;
         if (*data & CONT) {
             uptr->STATUS &= ~RUN_FLG;
             iii_instr = M[uptr->MAR];
             sim_activate(uptr, 10);
         } 
         sim_debug(DEBUG_CONO, &iii_dev, "III %03o CONO %06o\n", dev, (uint32)*data);
         break;
     case DATAI:
         sim_debug(DEBUG_DATAIO, &iii_dev, "III %03o DATAI %06o\n", dev, (uint32)*data);
         break;
    case DATAO:
         if (uptr->STATUS & RUN_FLG)
             uptr->STATUS |= DATA_FLG;
         else {
             iii_instr = *data;
             sim_activate(uptr, 10);
         }
         sim_debug(DEBUG_DATAIO, &iii_dev, "III %03o DATAO %06o\n", dev, (uint32)*data);
         break;
    }
    return SCPE_OK;
}

t_stat
iii_svc (UNIT *uptr)
{
     uint64    temp;
     int       A;
     int       ox, oy, nx, ny, br, sz;
     int       i, j, ch;

     iii_cycle (10, 0);

     switch(iii_instr & 017) {
     case 000: /* JMP and HLT */
               if (iii_instr & 020) {
                   uptr->MAR = (iii_instr >> 18) & RMASK;
               } else {
                   uptr->STATUS &= ~RUN_FLG;
               }
               break;
     case 001:
     case 003:
     case 005:
     case 007:
     case 011:
     case 013:
     case 015:
     case 017: /* Draw 4 characters */
               /* Extract X,Y,Bright and Size */
               sz = (uptr->POS & CSIZE) >> CSIZE_V;
               br = (uptr->POS & CBRT) >> CBRT_V;
               ox = (uptr->POS & POS_X) >> POS_X_V;
               oy = (uptr->POS & POS_Y) >> POS_Y_V;
               for (i = 29; i >= 1; i -= 7) {
                   /* Extract character and compute initial point */
                   int cx, cy;
                   uint8 v;
                   ch = (iii_instr >> i) & 0177;
                   cx = ox;
                   cy = oy + (6 * sz);
                   /* Scan map and draw lines as needed */
                   for(j = 0; j < 18; j++) {
                      v = map[i][j];
                      if (v == 0)
                         break;
                      nx = cx + (((v >> 4) & 07) * sz);
                      ny = cy + ((v & 017) * sz);
                      if (v & 0200)
                          draw_line(cx, cy, nx, ny, br);
                      cx = nx;
                      cy = ny;
                   }
                   ox += 8 * sz;
               }
               /* Repack to new position. */
               uptr->POS = (POS_X & (nx << POS_X_V)) |
                           (POS_Y & (ny << POS_Y_V)) |
                           (uptr->POS & (CBRT|CSIZE));
               break;

     case 002: /* Short Vector */
               /* Extract X,Y,Bright and Size */
               sz = (uptr->POS & CSIZE) >> CSIZE_V;
               br = (uptr->POS & CBRT) >> CBRT_V;
               ox = (uptr->POS & POS_X) >> POS_X_V;
               oy = (uptr->POS & POS_Y) >> POS_Y_V;
               /* Do first point */
               nx = (iii_instr >> 29) & 077;
               ny = (iii_instr >> 22) & 077;
               /* Sign extend */
               if (nx & 0100)
                   nx |= 03700;
               if (ny & 0100)
                   ny |= 03700;
               /* Compute relative position. */
               nx += ox;
               ny += oy;
               if ((iii_instr & 010000000LL) == 0) { /* Check if visible */
                   if ((iii_instr & 004000000LL) == 0) { /* Draw a line */
                      draw_line(ox, oy, nx, ny, br);
                   } else {
                      draw_point(nx, ny, br);
                   }
               }
               /* Do second point */
               nx = (iii_instr >> 13) & 0177;
               ny = (iii_instr >> 6) & 0177;
               /* Sign extend */
               if (nx & 0100)
                   nx |= 03700;
               if (ny & 0100)
                   ny |= 03700;
               /* Compute relative position. */
               nx += ox;
               ny += oy;
               if ((iii_instr & 040) == 0) { /* Check if visible */
                   if ((iii_instr & 020) == 0) { /* Draw a line */
                      draw_line(ox, oy, nx, ny, br);
                   } else {
                      draw_point(nx, ny, br);
                   }
               }
               /* Repack to new position. */
               uptr->POS = (POS_X & (nx << POS_X_V)) |
                           (POS_Y & (ny << POS_Y_V)) |
                           (uptr->POS & (CBRT|CSIZE));
               break;

     case 004: /* JSR, JMS, SAVE */
               temp = (((uint64)uptr->MAR) << 18) | 020 /* | CPC */;
               A = (iii_instr >> 18) & RMASK;
               if ((iii_instr & 030) != 030) {
                  M[A] = temp;
                  A++;
               }
               if ((iii_instr & 020) != 020) {
                   temp = uptr->STATUS & 0377;
                   temp |= ((uint64)uptr->POS) << 8;
                   M[A] = temp;
                   A++;
               }
               if ((iii_instr & 030) != 030) {
                  uptr->MAR = A;
               }
               break;

     case 006: /* Long Vector */
               /* Extract X,Y,Bright and Size */
               sz = (uptr->POS & CSIZE) >> CSIZE_V;
               br = (uptr->POS & CBRT) >> CBRT_V;
               ox = (uptr->POS & POS_X) >> POS_X_V;
               oy = (uptr->POS & POS_Y) >> POS_Y_V;
               /* Update sizes if needed */
               if (((iii_instr >> 9) & CSIZE) != 0)
                  sz = (iii_instr >> 9) & CSIZE;
               if (((iii_instr >> 12) & 7) != 0)
                  br = (iii_instr > 12) & 7;
               nx = (iii_instr >> 25) & 03777;
               ny = (iii_instr >> 15) & 03777;
               if ((iii_instr & 0100) == 0) { /* Relative mode */
                  nx += ox;
                  ny += oy;
               }
               if ((iii_instr & 040) == 0) { /* Check if visible */
                   if ((iii_instr & 020) == 0) /* Draw a line */
                      draw_line(ox, oy, nx, ny, br);
                   else
                      draw_point(nx, ny, br);
               }
               /* Repack to new position. */
               uptr->POS = (POS_X & (nx << POS_X_V)) |
                           (POS_Y & (ny << POS_Y_V)) |
                           (CBRT & (br << CBRT_V)) |
                           (CSIZE & (sz << CSIZE_V));
               break;

     case 010: /* Select instruction */
               break;

     case 012: /* Test instruction */
               A = (uptr->STATUS & (int32)(iii_instr >> 12) & 0377) != 0;
               uptr->STATUS &= ~(0377 & ((iii_instr >> 28) ^ (iii_instr >> 20)));
               uptr->STATUS ^= (0377 & (iii_instr >> 20));
               if (A ^ ((iii_instr & 020) != 0)) 
                   uptr->MAR++;
               break;

     case 014: /* Restore */
               A = (iii_instr >> 18) & RMASK;
               temp = M[A];
               if ((iii_instr & 020) != 0) {
                   uptr->STATUS &= ~0377;
                   uptr->STATUS |= temp & 0377;
               }
               if ((iii_instr & 040) != 0) {
                   uptr->POS = (temp >> 8) & (POS_X|POS_Y|CBRT|CSIZE);
               }
               break;

     case 016: /* Nop */
               break;
     }
     if (uptr->STATUS & RUN_FLG) {
         iii_instr = M[uptr->MAR];
         uptr->MAR++;
         uptr->MAR &= RMASK;
         sim_activate(uptr, 10);
     }
     return SCPE_OK;
}


t_stat iii_reset (DEVICE *dptr)
{
    if (dptr->flags & DEV_DIS) {
        display_close(dptr);
    } else {
        display_reset();
        iii_init(dptr);
    }
    return SCPE_OK;
}


/* Draw a point at x,y with intensity b. */
/* X and Y runs from -512 to 512. */
static void
draw_point(int x, int y, int b)
{
   iii_point(x, y, b, 0);
}

/* Draw a line between two points */
static void
draw_line(int x1, int y1, int x2, int y2, int b)
{
    int                 dx, px;
    int                 dy, py;
    int                 i, p;
    int                 minx, miny;
    int                 maxx, maxy;

    dx = x1 - x2;
    dy = y1 - y2;
    if (x1 < 0) {
        dx = -1;
        px = -x1;
    } else if (x1 > 0) {
        dx = 1;
        px = x1;
    } else {
        dx = 0;
        px = 0;
    }

    if (y1 < 0) {
        dy = -1;
        py = -y1;
    } else if (y1 > 0) {
        dy = 1;
        py = y1;
    } else {
        dy = 0;
        py = 0;
    }

    if (dx == 0) {
        /* Vertical line */
        for (i = 1; i < py; i++) {
            dy += dy;
            draw_point(dx, dy, b);
        }
    } else if (dy == 0) {
        /* Horizontal line */
        for (i = 1; i < px; i++) {
            dx += dx;
            draw_point(dx, dy, b);
        }
    } else if (py > px) {
        /* More horizontal */
        p = 2 * px - py;
        for (i = 1; i < py; i++) {
            if (p > 0) {
                dx += dx;
                p += (2 * px) - (2 * py);
            } else {
                p += 2 * px;
            }
            dy += dy;
            draw_point(dx, dy, b);
        }
    } else {
        /* More vertical */
        p = 2 * py - px;
        for (i = 1; i < px; i++) {
            if (p > 0) {
                dy += dy;
                p += (2 * py) - (2 * px);
            } else {
                p += 2 * py;
            }
            dx += dx;
            draw_point(dx, dy, b);
        }
    }
}

t_stat iii_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
return SCPE_OK;
}

const char *iii_description (DEVICE *dptr)
{
    return "Triple III Display";
}
#endif
