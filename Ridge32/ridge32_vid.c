/* Ridge32_vid.c: Ridge 32 monochrome display

   Copyright (c) 2020, Richard Cornwell

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

#include "ridge32_defs.h"                            /* simulator defns */
#include "sim_video.h"

#define STATUS         u3          /* Status/command */
#define MAR            u4          /* Memory address register */
#define DAR            u5          /* Display address, count */
#define KEYBUF         u6          /* keyboard buffer and counter */

#define KEYFLG         u3          /* Display unit keyboard flags */

#define SH_R           0x01
#define SH_L           0x02
#define CTL_R          0x04
#define CTL_L          0x08
#define CAP_L          0x10

#define MONO_XSIZE     1024
#define MONO_YSIZE     800

#define CMD_NOP        0x1f        /* Nop operation command */
#define CMD_WR         0xE         /* Write buffer */
#define CMD_RD         0xD         /* Read buffer */
#define CMD_SU         0xB         /* Scroll up */
#define CMD_SD         0x7         /* Scroll Down */
#define CMD_TERM       0xF         /* Terminate operation */

#define REG_NOP        0x0         /* No register */
#define REG_DAR        0x1         /* Display address register */
#define REG_MAR        0x2         /* Memory address register */
#define REG_CNT        0x4         /* Count */
#define REG_STAT       0x8         /* Status */

#define STS_IE         0x01        /* Interrupt enable */
#define STS_DE         0x02        /* Display enable */
#define STS_IV         0x04        /* Inverse video */
#define STS_TS         0x08        /* Top of screen */
#define STS_KE         0x10        /* Keyboard enable */

#define IOR_C          0x01        /* Command complete */
#define IOR_DT         0x06        /* Display type mask */
#define IOR_TS         0x08        /* Top of screen */

#define KBD_OR         0x10        /* Overrun keyboard buffer */
#define KBD_IRQ        0x200       /* Keyboard has characters */
#define DIS_C          0x400       /* Display complete */
#define DIS_IRQ        0x800       /* Display completed operation */
#define DIS_TS         0x1000      /* Top of screen */

int    mono_read(uint32 dev, uint32 *data);
int    mono_write(uint32 dev, uint32 data);
int    mono_iord(uint32 *data);
void   mono_start(UNIT *uptr, int drive);
t_stat mono_svc (UNIT *uptr);
t_stat mono_disp (UNIT *uptr);
void   mono_key_event(UNIT *uptr, SIM_KEY_EVENT *kev);
t_stat mono_detach(UNIT *uptr);
t_stat mono_reset(DEVICE *dp);

uint32  mono_buf[32*1024];
uint32  *mono_lines;
uint32  mono_palette[2];
int     mono_active;
uint8   mono_updated[MONO_YSIZE];
int     mono_row;
int     mono_ref;

/* Device context block */
struct ridge_dib mono_dib = {0x5, 4, mono_read, mono_write, mono_iord, 1};

MTAB mono_mod[] = {
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "SLOT", "SLOT", &set_slot_num,
        &show_slot_num, NULL},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    { 0 }
    };


UNIT                mono_unit[] = {
    {UDATA(&mono_svc, UNIT_IDLE | UNIT_DISABLE | UNIT_DIS, 0)},
    {UDATA(&mono_disp, UNIT_IDLE | UNIT_DIS, 0)},
};


DEVICE              mono_dev = {
    "VID", mono_unit, NULL, mono_mod,
    2, 16, 24, 1, 16, 8,
    NULL, NULL, &mono_reset, NULL, NULL, &mono_detach,
    &mono_dib, DEV_DEBUG | DEV_DIS | DEV_DISABLE, 0, dev_debug, NULL, NULL, 
};






int
mono_read(uint32 dev, uint32 *data)
{
    UNIT     *uptr = &mono_unit[0];
    int       reg = dev & 0xf;

    *data = 0;
    if (reg & REG_DAR)    /* DAR */
        *data |= uptr->DAR & 0xffff;
    if (reg & REG_MAR)    /* MAR */
        *data |= uptr->MAR;
    if (reg & REG_CNT)
        *data |= uptr->DAR & 0xffff0000;
    if (reg & REG_STAT) 
        *data |= (uptr->STATUS & 0x1f) | ((uptr->STATUS >> 11) & 0xf0) |
                          ((mono_row++ & 0x3ff) << 10);
    sim_debug(DEBUG_EXP, &mono_dev, "read status %08x %08x\n", dev, *data);
    return 0;
}

int
mono_write(uint32 dev, uint32 data)
{
    UNIT     *uptr = &mono_unit[0];
    int       reg = dev & 0xf;
    if (reg & REG_DAR)    /* DAR */
        uptr->DAR = (data & 0x0000ffff) | (uptr->DAR & 0xffff0000);
    if (reg & REG_MAR)    /* MAR */
        uptr->MAR = data >> 6;
    if (reg & REG_CNT)    /* Count */
        uptr->DAR = (data & 0xffff0000) | (uptr->DAR & 0x0000ffff);
    if (reg & REG_STAT)   /* Status */
        uptr->STATUS = (uptr->STATUS & ~0x1f) | (data & 0x1f);
    uptr->STATUS = (uptr->STATUS & 0xfff) | ((dev & 0x1f0) << 12);
    uptr->STATUS &= ~(DIS_IRQ|DIS_C);
    mono_row = (mono_row + 1) &  0x3ff;
    sim_activate(uptr, 200);
    sim_debug(DEBUG_CMD, &mono_dev, "display start %08x %08x %08x\n", dev, data, uptr->STATUS);
    return 0;
}

int
mono_iord(uint32 *data)
{
    UNIT     *uptr = &mono_unit[0];

    *data = (((uint32)mono_dib.dev_num) << 24) | 0x2;
    if (uptr->STATUS & DIS_C) {
        *data |= IOR_C;
         uptr->STATUS &= ~DIS_C;
         sim_debug(DEBUG_TRAP, &mono_dev, "itest comp  %08x %08x\n", *data, uptr->STATUS);
         return 1;
    }
    if (uptr->STATUS & DIS_TS)
        *data |= IOR_TS;
    if ((uptr->STATUS & (DIS_TS|STS_TS)) == (DIS_TS|STS_TS)) {
        uptr->STATUS &= ~(DIS_TS);
        sim_debug(DEBUG_TRAP, &mono_dev, "itest disp ts  %08x %08x\n", *data, uptr->STATUS);
        return 1;
    }
    if ((uptr->STATUS & (KBD_IRQ|STS_KE)) == (KBD_IRQ)) {
        int cnt =  uptr->KEYBUF >> 24;

        *data = ((uint32)mono_dib.dev_num & 0xfe) << 24;
        switch (cnt & 7) {
        case 0:
             /* Can not occur */
             break;
        case 1:
             *data |= uptr->KEYBUF & 0xff0000;
             uptr->KEYBUF = 0;
             break;
        case 3:
             *data |= uptr->KEYBUF & 0xff0000;
             uptr->KEYBUF = (1 << 24) | ((uptr->KEYBUF << 8) & 0xffff00);
             break;
        case 7:
             *data |= uptr->KEYBUF & 0xff0000;
             uptr->KEYBUF = (3 << 24) | ((uptr->KEYBUF << 8) & 0xffff00);
             break;
        }
        if (cnt & 010) 
            *data |= KBD_OR;
        if (cnt == 1)
            uptr->STATUS &= ~(KBD_IRQ);
        sim_debug(DEBUG_TRAP, &mono_dev, "itest key  %08x %08x\n", *data, uptr->STATUS);
        return 1;
    }
    sim_debug(DEBUG_TRAP, &mono_dev, "itest status %08x %08x\n", *data, uptr->STATUS);
    return 0;
}



t_stat
mono_svc (UNIT *uptr)
{
     int     count = (uptr->DAR >> 16) & 0xffff;
     int     dar = uptr->DAR & 0x7fff;
     int     addr;

     switch((uptr->STATUS >> 16) & 0x1f) {

     case CMD_WR:            /* Write buffer */
          addr = uptr->MAR >> 2;
          while (count > 0) {
              mono_buf[dar] = M[addr];
              mono_updated[dar / (MONO_XSIZE / 32)] = 1;
              addr++;
              dar = (dar + 1) & 0x7fff;
              count--;
          }
          break;

     case CMD_RD:            /* Read buffer */
          addr = uptr->MAR >> 2;
          while (count > 0) {
              M[addr] = mono_buf[dar];
              addr++;
              dar = (dar + 1) & 0x7fff;
              count--;
          }
          break;

     case CMD_SU:            /* Scroll up */
          addr = uptr->MAR;
          while (count > 0) {
              mono_buf[addr & 0x7fff] = mono_buf[dar];
              mono_updated[addr / (MONO_XSIZE / 32)] = 1;
              addr++;
              dar = (dar + 1) & 0x7fff;
              count--;
          }
          break;

     case CMD_SD:            /* Scroll Down */
          addr = uptr->MAR;
          while (count > 0) {
              mono_buf[addr & 0x7fff] = mono_buf[dar];
              mono_updated[addr / (MONO_XSIZE / 32)] = 1;
              addr--;
              dar = (dar - 1) & 0x7fff;
              count--;
          }
          break;

     default:
     case CMD_TERM:          /* Terminate operation */
          break;

     case CMD_NOP:           /* Nop operation command */
          return SCPE_OK;
     }
     uptr->STATUS &= ~(0x1f << 12);
     uptr->STATUS |= (0xf << 12) | DIS_C;
     sim_debug(DEBUG_TRAP, &mono_dev, "op term\n");
     ext_irq = 1;
     return SCPE_OK;
}

t_stat
mono_disp (UNIT *uptr)
{
     SIM_KEY_EVENT   kev;
     int             row;
     int             col;
     int             pos = 0;
     int             off = 0;
     int             first = 0;
     int             base = 0;
     int             update = 0;
     int             invert;

     /* Check if any key pressed */
     if (vid_poll_kb (&kev)  == SCPE_OK)
        mono_key_event (uptr, &kev);

     invert = (mono_unit[0].STATUS & STS_IV) != 0;
     /* Scan by row */
     for(row = 0; row < MONO_YSIZE; row++) {
         /* Check if row has been updated */
         if (mono_updated[row]) {
             for(col = 0; col < MONO_XSIZE; col++) {
                mono_lines[pos + col] = mono_palette[((mono_buf[off + (col >> 5)]
                                     >> (0x1f - (col & 0x1F))) & 1) ^ invert];
             }
             mono_updated[row] = 0;
             /* If at ether end of update chunk of screen update */
             if (row == (MONO_YSIZE-1) || mono_updated[row] == 0) {
                 vid_draw (0, first, MONO_XSIZE, row-first, &mono_lines[base]);
                 first = row;
                 base = pos;
                 update = 1;
             }
         }
         /* To next row start */
         off += (MONO_XSIZE/32);
         pos += MONO_XSIZE;
     }
     if (update)
         vid_refresh();
     mono_ref++;
     if (mono_ref == 60) {
        mono_ref = 0;
        mono_unit[0].STATUS |= DIS_TS;
        if ((mono_unit[0].STATUS & STS_TS) == 0)
            ext_irq = 1;
     } else
        mono_unit[0].STATUS &= ~DIS_TS;
     sim_activate (uptr, tmxr_poll);
     return SCPE_OK;
}

struct _keytab {
    int8    syms;            /* Key symbol */
    char    norm;            /* Normal unshifted key */
    char    shift;           /* Shifted key */
    char    cap;             /* Capslock on */
    char    shcap;           /* Shift and caps */
    char    cntrl;           /* Control */
} mono_keytab[] = {
        {SIM_KEY_0,               '0', ')', '0', ')', '0', },
        {SIM_KEY_1,               '1', '!', '1', '!', '0', },
        {SIM_KEY_2,               '2', '@', '2', '@', '\000', },
        {SIM_KEY_3,               '3', '#', '3', '#', '0', },
        {SIM_KEY_4,               '4', '$', '4', '$', '0', },
        {SIM_KEY_5,               '5', '%', '5', '%', '0', },
        {SIM_KEY_6,               '6', '^', '6', '^', '0', },
        {SIM_KEY_7,               '7', '&', '7', '&', '0', },
        {SIM_KEY_8,               '8', '*', '8', '*', '0', },
        {SIM_KEY_9,               '9', '(', '9', '(', '0', },
        {SIM_KEY_A,               'a', 'A', 'A', 'a', '\001', },
        {SIM_KEY_B,               'b', 'B', 'B', 'b', '\002', },
        {SIM_KEY_C,               'c', 'C', 'C', 'c', '\003', },
        {SIM_KEY_D,               'd', 'D', 'D', 'd', '\004', },
        {SIM_KEY_E,               'e', 'E', 'E', 'e', '\005', },
        {SIM_KEY_F,               'f', 'F', 'F', 'f', '\006', },
        {SIM_KEY_G,               'g', 'G', 'G', 'g', '\007', },
        {SIM_KEY_H,               'h', 'H', 'H', 'h', '\010', },
        {SIM_KEY_I,               'i', 'I', 'I', 'i', '\011', },
        {SIM_KEY_J,               'j', 'J', 'J', 'j', '\012', },
        {SIM_KEY_K,               'k', 'K', 'K', 'k', '\013', },
        {SIM_KEY_L,               'l', 'L', 'L', 'l', '\014', },
        {SIM_KEY_M,               'm', 'M', 'M', 'm', '\015', },
        {SIM_KEY_N,               'n', 'N', 'N', 'n', '\016', },
        {SIM_KEY_O,               'o', 'O', 'O', 'o', '\017', },
        {SIM_KEY_P,               'p', 'P', 'P', 'p', '\020', },
        {SIM_KEY_Q,               'q', 'Q', 'Q', 'q', '\021', },
        {SIM_KEY_R,               'r', 'R', 'R', 'r', '\022', },
        {SIM_KEY_S,               's', 'S', 'S', 's', '\023', },
        {SIM_KEY_T,               't', 'T', 'T', 't', '\024', },
        {SIM_KEY_U,               'u', 'U', 'U', 'u', '\025', },
        {SIM_KEY_V,               'v', 'V', 'V', 'v', '\026', },
        {SIM_KEY_W,               'w', 'W', 'W', 'w', '\027', },
        {SIM_KEY_X,               'x', 'X', 'X', 'x', '\030', },
        {SIM_KEY_Y,               'y', 'Y', 'Y', 'y', '\031', },
        {SIM_KEY_Z,               'z', 'Z', 'Z', 'z', '\032', },
        {SIM_KEY_BACKQUOTE,       '`', '~', '`', '~', '0', },
        {SIM_KEY_MINUS,           '-', '_', '-', '_', '0', },
        {SIM_KEY_EQUALS,          '=', '+', '=', '+', '0', },
        {SIM_KEY_LEFT_BRACKET,    '[', '{', '[', '{', '0', },
        {SIM_KEY_RIGHT_BRACKET,   ']', '}', ']', '}', '0', },
        {SIM_KEY_SEMICOLON,       ';', ':', ';', ':', '0', },
        {SIM_KEY_SINGLE_QUOTE,    '\'', '"', '\'', '"', '0', },
        {SIM_KEY_BACKSLASH,       '/', '?', '/', '?', '0', },
        {SIM_KEY_LEFT_BACKSLASH,  '\\', '|', '\\', '|', '0', },
        {SIM_KEY_COMMA,           ',', '<', ',', '<', '0', },
        {SIM_KEY_PERIOD,          '.', '>', '.', '>', '0', },
        {SIM_KEY_SLASH,           '/', '?', '/', '?', '0', },
        {SIM_KEY_ESC,             '\033', '\033', '\033', '\033', '\033', },
        {SIM_KEY_BACKSPACE,       '\b', '\b', '\b', '\b', '\b', },
        {SIM_KEY_TAB,             '\t', '\t', '\t', '\t', '\t', },
        {SIM_KEY_ENTER,           '\r', '\r', '\r', '\r', '\r', },
        {SIM_KEY_SPACE,           ' ', ' ', ' ', ' ', '0', },
        {-1,                      ' ', ' ', ' ', ' ', '0', },
};

void
mono_key_event(UNIT *uptr, SIM_KEY_EVENT *kev)
{
     char     ch;
     int      i;
     int      cnt;

     sim_debug(DEBUG_CMD, &mono_dev, "keypress %d %d\n", kev->state, kev->key);
     switch(kev->state) {
     case SIM_KEYPRESS_DOWN:
         switch (kev->key) {
         case SIM_KEY_CAPS_LOCK:
                uptr->KEYFLG |= CAP_L;
                break;

         case SIM_KEY_CTRL_L:
                uptr->KEYFLG |= CTL_L;
                break;

         case SIM_KEY_CTRL_R:
                uptr->KEYFLG |= CTL_R;
                break;

         case SIM_KEY_SHIFT_L:
                uptr->KEYFLG |= SH_L;
                break;

         case SIM_KEY_SHIFT_R:
                uptr->KEYFLG |= SH_R;
                break;
         default:
                for(i = 0; mono_keytab[i].syms != -1; i++) {
                    if (mono_keytab[i].syms == kev->key) {
                        if ((uptr->KEYFLG & (CTL_L|CTL_R)) != 0) {
                            ch = mono_keytab[i].cntrl;
                        } else if ((uptr->KEYFLG & CAP_L) != 0) {
                             if ((uptr->KEYFLG & (SH_L|SH_R)) != 0)
                                 ch = mono_keytab[i].shcap;
                             else
                                 ch = mono_keytab[i].cap;
                        } else if ((uptr->KEYFLG & (SH_L|SH_R)) != 0) {
                             ch = mono_keytab[i].shift;
                        } else {
                             ch = mono_keytab[i].norm;
                        }
                        cnt = mono_unit[0].KEYBUF >> 24;
                        switch (cnt) {
                        case 0:
                            mono_unit[0].KEYBUF = (ch << 16) | (1 << 24);
                            break;
                        case 1:
                            mono_unit[0].KEYBUF |= (ch << 8) | (2 << 24);
                            break;
                        case 3:
                            mono_unit[0].KEYBUF |= ch | (4 << 24);
                            break;
                        default:
                            mono_unit[0].KEYBUF |= ch | (8 << 24);
                            break;
                        }
                        mono_unit[0].STATUS |= KBD_IRQ;
                        ext_irq = 1;
                        break;
                    }
                }
                break;
         }
         break;

     case SIM_KEYPRESS_UP:
         switch (kev->key) {
         case SIM_KEY_CAPS_LOCK:
                uptr->KEYFLG &= ~CAP_L;
                break;

         case SIM_KEY_CTRL_L:
                uptr->KEYFLG &= ~CTL_L;
                break;

         case SIM_KEY_CTRL_R:
                uptr->KEYFLG &= ~CTL_R;
                break;

         case SIM_KEY_SHIFT_L:
                uptr->KEYFLG &= ~SH_L;
                break;

         case SIM_KEY_SHIFT_R:
                uptr->KEYFLG &= ~SH_R;
                break;
         }
         break;
     case SIM_KEYPRESS_REPEAT:
         break;
     }
}


t_stat
mono_reset(DEVICE *dptr)
{
    t_stat   r;
    sim_cancel(&mono_unit[1]);

    mono_unit[0].STATUS = 0;
    if (dptr->flags & DEV_DIS) {
        if (mono_active) {
           free (mono_lines);
           mono_lines = NULL;
           return vid_close();
        }
        return SCPE_OK;
    }

    if (!vid_active && !mono_active) {
        r = vid_open (dptr, NULL, MONO_XSIZE, MONO_YSIZE, 0);
        if (r != SCPE_OK)
           return r;
        mono_lines = (uint32 *) calloc(MONO_XSIZE * MONO_YSIZE, sizeof(uint32));
        if (mono_lines == NULL) {
            vid_close ();
            return SCPE_MEM;
        }
        mono_palette[0] = vid_map_rgb (0x00, 0x00, 0x00);    /* Black */
        mono_palette[1] = vid_map_rgb (0xff, 0xff, 0xff);    /* White */
        sim_printf ("Monochrome Video Display Created.\n");
        mono_active = TRUE;
    }
    sim_activate_abs (&mono_unit[1], tmxr_poll);
    return SCPE_OK;
}


/* Detach routine */
t_stat mono_detach(UNIT *uptr)
{
    if ((mono_dev.flags & DEV_DIS) == 0) {
         mono_dev.flags |= DEV_DIS;
         mono_reset(&mono_dev);
    }
    return SCPE_OK;
}



