/* Ridge32_ct.c: Ridge 32 Cartridge tape controller.

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

#include "ridge32_defs.h"
#include "sim_tape.h"

#define STATUS   u3              /* Last status */
#define POS      u4              /* Current position in buffer */
#define MODE     u5              /* Command register */

/* Mode register */
#define SPEED    0x0004          /* High speed */
#define MARK     0x0008          /* Create tape mark */
#define EDIT     0x0010          /* Edit */
#define ERASE    0x0020          /* Blank tape */
#define WRITE    0x0040          /* Write block */
#define REV      0x0080          /* Reverse direction */
#define TAD0     0x0100          /* Select unit */
#define TAD1     0x0200
#define FAD      0x0400          /* Select formater */
#define FEN      0x0800          /* Formater enable */
#define DMA      0x1000          /* Transfer data */
#define IE       0x2000          /* Interrupt enable */

#define REWIND   0x10000         /* Rewind command */
#define UNLOAD   0x20000         /* Unload command */

/* Status register */
#define DBSY     0x00001         /* Device busy */
#define FBSY     0x00002         /* Formater busy */
#define RDY      0x00004         /* Device ready */
#define ONL      0x00008         /* Online */
#define FPT      0x00010         /* File protect */
#define LPT      0x00020         /* Load point */
#define EOT      0x00040         /* End of tape */
#define RWD      0x00080         /* Rewinding */
#define HISP     0x00100         /* High speed */
#define IDENT    0x00800         /* PE identification */
#define CER      0x01000         /* Corrected error */
#define HER      0x02000         /* Hard error */
#define FMK      0x04000         /* File mark */
#define CIP      0x08000         /* Command in progress */
#define OUR      0x10000         /* Over/under run */
#define DMAE     0x20000         /* DMA error */
#define TPE      0x40000         /* Tape parity error */
#define BCO      0x80000         /* Byte count overflow */
#define IRQ      0x80000000      /* IRQ pending */

#define MASK     0x00ffffff      /* Data mask */
#define SMASK    0x000fffff      /* Mask for status */
#define CMASK    0x000fffff      /* Mask for count */



int    ct_read(uint32 dev, uint32 *data);
int    ct_write(uint32 dev, uint32 data);
int    ct_iord(uint32 *data);
void   ct_start(UNIT *uptr, int drive);
uint32 ct_mkstatus(UNIT *uptr);
t_stat ct_svc (UNIT *uptr);
t_stat ct_boot (int32, DEVICE *);
t_stat ct_set_type(UNIT *, int32, CONST char *, void *);
t_stat ct_attach(UNIT *uptr, CONST char *cptr);
t_stat ct_detach(UNIT *uptr);
t_stat ct_reset(DEVICE *dp);

uint8  ct_buf[64*1024];

/* Device context block */
struct ridge_dib ct_dib = {0x20, 3, ct_read, ct_write, ct_iord, 0};

MTAB ct_mod[] = {
    {MTAB_XTD | MTAB_VUN, 0, "FORMAT", "FORMAT",
     &sim_tape_set_fmt, &sim_tape_show_fmt, NULL,
      "Set/Display tape format (SIMH, E11, TPC, P7B)" },
    {MTAB_XTD | MTAB_VUN, 0, "LENGTH", "LENGTH",
     &sim_tape_set_capac, &sim_tape_show_capac, NULL,
      "Set unit n capacity to arg MB (0 = unlimited)" },
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "SLOT", "SLOT", &set_slot_num,
        &show_slot_num, NULL},
    {MTAB_XTD | MTAB_VDV | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    { 0 }
    };


UNIT                ct_unit[] = {
    {UDATA(&ct_svc, UNIT_ATTABLE | UNIT_DISABLE, 0)},
};


DEVICE              ct_dev = {
    "CT", ct_unit, NULL, ct_mod,
    1, 16, 24, 1, 16, 8,
    NULL, NULL, &ct_reset, &ct_boot, &ct_attach, &ct_detach,
    &ct_dib, DEV_DEBUG | DEV_DISABLE, 0, dev_debug, NULL, NULL, 
};




struct _dcb {
    uint32          addr;        /* transfer address */
    uint32          count;       /* byte count */
} ct_dcb;


int
ct_read(uint32 dev, uint32 *data)
{
    UNIT     *uptr = &ct_unit[0];
    int       reg = dev & 03;


    switch (reg) {
    case 0:
             *data = ct_mkstatus(uptr);
             break;
    case 1:
             *data = uptr->MODE & 0xffff;
             break;
    case 2:
             *data = ct_dcb.addr & MASK;
             break;
    case 3:
             *data = ct_dcb.count & MASK;
             break;
    }
    *data |= (ct_dib.dev_num << 24) & 0xff000000;
    sim_debug(DEBUG_EXP, &ct_dev, "read status %08x %08x\n", dev, *data);
    return (uptr->STATUS & 0x2) ? 1 : 0;
}

int
ct_write(uint32 dev, uint32 data)
{
    UNIT     *uptr = &ct_unit[0];
    int       reg = dev & 03;
    switch (reg) {
    case 0:
             uptr->MODE |= (data & 3) << 16;
             uptr->STATUS &= ~(LPT|EOT|RWD|CER|HER|OUR|DMAE|TPE|BCO|IRQ|RDY|FMK);
             uptr->STATUS |= DBSY|FBSY|CIP;
             sim_activate(uptr, 20);
             break;
    case 1:
             uptr->MODE = data & 0xffff;
             break;
    case 2:
             ct_dcb.addr = data;
             break;
    case 3:
             ct_dcb.count = data;
             break;
    }
    sim_debug(DEBUG_CMD, &ct_dev, "CT start %08x %08x\n", dev, data);
    return 0;
}

int
ct_iord(uint32 *data)
{
    UNIT     *uptr = &ct_unit[0];

    *data = ct_mkstatus(uptr);
    *data |= ((uint32)ct_dib.dev_num) << 24;
    sim_debug(DEBUG_EXP, &ct_dev, "itest status %08x\n", *data);
    /* Check if irq pending */
    if (uptr->STATUS & IRQ) {
        uptr->STATUS &= ~(IRQ);
        return 1;
    }
    return 0;
}

/* Generate status register */
uint32
ct_mkstatus(UNIT *uptr)
{
    uint32  sts;

    sts = uptr->STATUS & SMASK;
    sts |= ONL;
    if ((uptr->STATUS & CIP) == 0) {
        if (uptr->flags & UNIT_ATT) {
            sts |= RDY;
            if ((uptr->flags & MTUF_WLK) != 0)
                sts |= FPT;
            if (sim_tape_bot(uptr))
                sts |= LPT;
            if (sim_tape_eot(uptr))
                sts |= EOT;
        }
    }
    return sts;
}



t_stat
ct_svc (UNIT *uptr)
{
     int         len;
     t_mtrlnt    reclen;
     t_stat      r;

     switch((uptr->MODE >> 16) & 03) {
     case 0:
         if (uptr->MODE & FEN) {
             if (uptr->MODE & DMA) {
                 len = (ct_dcb.count ^ CMASK) + 1;
                 /* Write a block to the tape. */
                 if (uptr->MODE & WRITE) {
                     io_read_blk(ct_dcb.addr, &ct_buf[0], len);
                     r = sim_tape_wrrecf(uptr, &ct_buf[0], len);
                     ct_dcb.addr += len;
                     ct_dcb.count = 0;
                     sim_debug(DEBUG_CMD, &ct_dev, "CT write %d\n", len);
                 } else if (uptr->MODE & MARK) {
                 } else if (uptr->MODE & ERASE) {
                 } else if (uptr->MODE & REV) {
                 } else {
                     r = sim_tape_rdrecf(uptr, &ct_buf[0], &reclen, sizeof(ct_buf));
                     switch(r) {
                     case MTSE_OK:
                           /* Copy record */
                           if (reclen < len) {
                               len = reclen;
                           }
                           io_write_blk(ct_dcb.addr, &ct_buf[0], len);
                           ct_dcb.addr += len;
                           ct_dcb.count = ct_dcb.count + reclen;
                           if (ct_dcb.count & ~CMASK)
                               uptr->STATUS |= BCO;
                           sim_debug(DEBUG_CMD, &ct_dev, "CT read %d %d\n", reclen, len);
                           break;
                     case MTSE_TMK:
                           uptr->STATUS |= FMK;
                           sim_debug(DEBUG_CMD, &ct_dev, "CT read mark\n");
                           break;
                     default:
                           break;
                     }
                 }
             } else if (uptr->MODE & WRITE) {
                  if (uptr->MODE & MARK) {
                      sim_tape_wrtmk(uptr);
                  }
             } else if (uptr->MODE & REV) {
                     r = sim_tape_sprecr(uptr, &reclen);
                     switch(r) {
                     case MTSE_OK:
                           ct_dcb.count = ct_dcb.count + reclen;
                           if (ct_dcb.count & ~CMASK)
                               uptr->STATUS |= BCO;
                           sim_debug(DEBUG_CMD, &ct_dev, "CT spaceb %d %d\n", reclen, len);
                           if (uptr->MODE & MARK) {
                              sim_activate(uptr, 1000);
                              return SCPE_OK;
                           }
                           break;
                     case MTSE_TMK:
                           sim_debug(DEBUG_CMD, &ct_dev, "CT spaceb mark\n");
                           uptr->STATUS |= FMK;
                           break;
                     default:
                           break;
                     }
      
             } else {
                     r = sim_tape_sprecf(uptr, &reclen);
                     switch(r) {
                     case MTSE_OK:
                           ct_dcb.count = ct_dcb.count + reclen;
                           if (ct_dcb.count & ~CMASK)
                               uptr->STATUS |= BCO;
                           sim_debug(DEBUG_CMD, &ct_dev, "CT spacef %d\n", reclen);
                           if (uptr->MODE & MARK) {
                              sim_activate(uptr, 1000);
                              return SCPE_OK;
                           }
                           break;
                     case MTSE_TMK:
                           sim_debug(DEBUG_CMD, &ct_dev, "CT spacef mark\n");
                           uptr->STATUS |= FMK;
                           break;
                     default:
                           break;
                     }
             }
         }
         break;
     case 1:
         sim_tape_rewind(uptr);
         break;
     case 2:
         sim_tape_detach(uptr);
         break;
     case 3:
         break;
     }
     uptr->STATUS &= ~(DBSY|FBSY|CIP);
     if (uptr->MODE & IE) {
         uptr->STATUS |= IRQ;
         ext_irq =1;
     }
     return SCPE_OK;
}




t_stat
ct_reset(DEVICE *dptr)
{
    int i,t;

    return SCPE_OK;
}

t_stat
ct_boot (int32 unit, DEVICE *dptr)
{
    t_mtrlnt    reclen;
    UNIT        *uptr = &ct_unit[0];
    extern uint32   PC;
    extern int      user;
    extern int      boot_sw;
    ct_dcb.addr = 0x40000;
    /* Skip over records until file mark */
    while (MTSE_OK == sim_tape_sprecf(uptr, &reclen));
    while (MTSE_OK == sim_tape_rdrecf(uptr, &ct_buf[0], &reclen, sizeof(ct_buf))) {
         io_write_blk(ct_dcb.addr, &ct_buf[0], reclen);
         ct_dcb.addr += reclen;
         sim_debug(DEBUG_CMD, &ct_dev, "CT boot read %d\n", reclen);
   }
   PC = 0x40000;
   user = 0;
   boot_sw = 1;
   return SCPE_OK;
}

/* Attach routine */
t_stat
ct_attach(UNIT *uptr, CONST char *cptr)
{
    char header[4];
    t_stat r;

    r = sim_tape_attach_ex(uptr, cptr, 0, 0);    /* attach unit  */
    if ( r != SCPE_OK)              /* error?       */
        return r;
    uptr->STATUS = IRQ|ONL|RDY;
    return SCPE_OK;
}


/* Detach routine */
t_stat ct_detach(UNIT *uptr)
{
    t_stat r;

    r = sim_tape_detach(uptr);  /* detach unit */
    return r;
}



