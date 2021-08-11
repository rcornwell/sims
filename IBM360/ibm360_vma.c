/* ibm360_vma.c: ibm 360 Virtual Memory Assists for VM/370.

   Copyright (c) 2021, Richard Cornwell

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

#include "ibm360_defs.h"                        /* simulator defns */

extern uint32  *M;
extern uint8   key[MAXMEMSIZE / 2048];
extern uint32  PC;                   /* Program counter */
extern uint32  regs[16];             /* CPU Registers */
extern uint32  cregs[16];            /* Control registers /67 or 370 only */
extern uint8   cc;                   /* CC */
extern uint8   pmsk;                 /* Program mask */
extern uint8   st_key;               /* Storage key */
extern int     per_en;               /* PER mode enable */

#define AMASK       0x00ffffff     /* Mask address bits */
#define MSIGN       0x80000000     /* Minus sign */

#define R1(x)   (((x)>>4) & 0xf)
#define R2(x)   ((x) & 0xf)
#define B1(x)   (((x) >> 12) & 0xf)
#define D1(x)   ((x) & 0xfff)
#define X2(x)   R2(x)

extern int ReadFull(uint32 addr, uint32 *data);
extern int ReadByte(uint32 addr, uint32 *data);
extern int ReadHalf(uint32 addr, uint32 *data);
extern int WriteFull(uint32 addr, uint32 data);
extern int WriteByte(uint32 addr, uint32 data);
extern int WriteHalf(uint32 addr, uint32 data);

/* Handle VM Assists for RRB instructions
 * return 0 if assist could not be completed. Return 1 if successful */
int
vma_rrb(uint32 addr1)
{
    uint32    micblok;
    uint32    micrseg;
    uint32    page;
    uint32    seg;
    uint32    segpage;
    uint32    pagswp;
    uint32    swpflg;
    uint32    pagcore;
    uint8     stk;

    sim_debug(DEBUG_VMA, &cpu_dev, "RRB check %08x\n", addr1);
    /* Check if enabled */
    if ((cregs[6] & 0xe0000000) != MSIGN)
       return 0;
    micblok = (cregs[6] & 0xfffff8) >> 2;
    /* Fetch SEGPAGE */
    micrseg = M[micblok];
    sim_debug(DEBUG_VMA, &cpu_dev, "Micrseg %08x\n", micrseg);
    if (micrseg & 0x2)
        return 0;
    /* Compute address to operate on */
    page = addr1 >> 12;
    if (micrseg & 0x1) {
        seg = page >> 7;
        page &= 0x7f;
    } else {
        seg = page >> 4;
        page &= 0xf;
    }
    segpage = M[((micrseg & AMASK) >> 2) + seg];
    sim_debug(DEBUG_VMA, &cpu_dev, "Segpage %08x s=%x p=%x\n", segpage, seg, page);
    if ((segpage >> 24) <= (addr1 >> 20))
        return 0;
    pagswp = M[((segpage & AMASK) >> 2) - 1];
    sim_debug(DEBUG_VMA, &cpu_dev, "pagswp %08x\n", pagswp);
    swpflg = M[((pagswp + (8 * page)) & AMASK) >> 2];
    sim_debug(DEBUG_VMA, &cpu_dev, "swpflg %08x\n", swpflg);
    pagcore = M[((segpage + (2 * page)) & AMASK) >> 2];
    sim_debug(DEBUG_VMA, &cpu_dev, "pagcore %08x\n", pagcore);
    /* Get key value */
    if (addr1 & 0x800) {
        stk = swpflg & 0xfe;
    } else {
        stk = (swpflg >> 8) & 0xfe;
    }
    sim_debug(DEBUG_VMA, &cpu_dev, "stk %02x\n", stk);
    /* Check if page is valid */
    if ((page & 0x1) == 0)
       pagcore >>= 16;
    if ((pagcore & 0xe) == 0) {
       pagcore &= 0xfff0;
       pagcore <<= 8;
       pagcore |= addr1 & 0x800;
       stk |= key[pagcore>>11] & 0x6;
       key[pagcore>>11] &= 0xfb;  /* Clear reference bit */
       sim_debug(DEBUG_VMA, &cpu_dev, "real addr %08x %02x\n", pagcore, stk);
    }
    /* Clear virtual reference bits */
    if (addr1 & 0x800) {
       swpflg &= 0xfcfffffb;
    } else {
       swpflg &= 0xf3fffbff;
    }
    M[((pagswp + (8 * page)) & AMASK) >> 2] = swpflg;
    /* Update the CC */
    cc = (stk >> 1) & 03;
    return 1;
}

/* Handle VM Assists for B2 instructions
 * return 0 if assist could not be completed. Return 1 if successful */
int
vma_370(int reg, uint32 addr1)
{
    uint32     micvpsw;
    uint32     vpsw;

    sim_debug(DEBUG_VMA, &cpu_dev, "B2%02x %08x check\n", reg, addr1);
    switch(reg) {
    default:
    case 0x2: /* STIDP */
    case 0x3: /* STIDC */
    case 0x4: /* SCK */
    case 0x5: /* STCK */
    case 0x6: /* SCKC */
    case 0x7: /* STCKC */
               break;
    case 0x8: /* SPT */
               break;
    case 0x9: /* STPT */
               break;
    case 0xd: /* PTLB */
               break;
    case 0xa:  /* SPKA */
               if ((cpu_unit[0].flags & FEAT_PROT) == 0) {
                   break;
               }
               if (cregs[6] & 0x10000000)
                   break;
               micvpsw = M[((cregs[6] & 0xfffff8) >> 2) + 2];
               vpsw = M[(micvpsw & AMASK) >> 2];
               sim_debug(DEBUG_VMA, &cpu_dev, "VPSW %08x\n", vpsw);
               vpsw &= 0xff0fffff;
               vpsw |= (0xf0 & addr1) << 16;
               M[micvpsw & AMASK] = vpsw;
               st_key = 0xf0 & addr1;
               sim_debug(DEBUG_VMA, &cpu_dev, "New VPSW %08x New key %02x \n", vpsw, st_key);
               return 1;

    case 0xb:  /* IPK */
               if ((cpu_unit[0].flags & FEAT_PROT) == 0) {
                   break;
               }
               if (cregs[6] & 0x10000000)
                   break;
               micvpsw = M[((cregs[6] & 0xfffff8) >> 2) + 2];
               vpsw = M[(micvpsw & AMASK) >> 2];
               sim_debug(DEBUG_VMA, &cpu_dev, "VPSW %08x\n", vpsw);
               regs[2] = (regs[2] & 0xffffff00) | ((vpsw >> 16) & 0xf0);
               sim_debug(DEBUG_VMA, &cpu_dev, "Reg2 %08x\n", regs[2]);
               return 1;

    case 0x13: /* RRB */
               return vma_rrb(addr1);
    }
    return 0;
}

/* Handle VM Assists for SSM instructions
 * return 0 if assist could not be completed. Return 1 if successful */
int
vma_ssm(uint32 addr1)
{
    uint32    micblok;
    uint32    miccreg;
    uint32    micvpsw;
    uint32    vpsw;
    uint32    temp;
    int       flags;

    sim_debug(DEBUG_VMA, &cpu_dev, "SSM check %08x\n", addr1);
    micblok = (cregs[6] & 0xfffff8) >> 2;
    /* Check if function enabled */
    if ((cregs[6] & 0x01000000) != 0) {
        miccreg = M[micblok + 5];
        sim_debug(DEBUG_VMA, &cpu_dev, "SSM micacf %08x\n", miccreg);
        if ((miccreg & 0x00800000) == 0)
           return 0;
    }
    /* Fetch Virtual Cr 0 */
    miccreg = M[micblok + 1];
    sim_debug(DEBUG_VMA, &cpu_dev, "SSM miccreg %08x\n", miccreg);
    temp = M[(miccreg & AMASK) >> 2];
    if ((temp & 0x40000000) != 0)
        return 0;
    /* Fetch the virtual PSW */
    micvpsw = M[micblok + 2];
    vpsw = M[(micvpsw & AMASK) >> 2];
    /* Fetch new mask */
    if (ReadByte(addr1, &temp))
        return 0;
    sim_debug(DEBUG_VMA, &cpu_dev, "VPSW %08x d=%08x\n", vpsw, temp);
    flags = vpsw >> 24;
    flags ^= temp;
    if (vpsw & 0x80000) {
       /* If bits are not zero, exit */
       if (temp & 0xb8)
           return 0;
       /* Check if DAT or PER changed */
       if (flags & 0x44)
          return 0;
       /* Check if IRQ pending and enable interrupt */
       if ((micvpsw & MSIGN) && (flags & temp & 0x3) != 0)
          return 0;
    } else {
       /* Check if IRQ pending and enable interrupt */
       if ((micvpsw & MSIGN) && (flags & temp) != 0)
          return 0;
    }
    vpsw &= 0xffffff;
    vpsw |= temp << 24;
    M[(micvpsw & AMASK) >> 2] = vpsw;
    sim_debug(DEBUG_VMA, &cpu_dev, "new VPSW %08x\n", vpsw);
    return 1;
}

/* Handle VM Assists for LPSW instructions
 * return 0 if assist could not be completed. Return 1 if successful */
int
vma_lpsw(uint32 addr1)
{
    uint32    micblok;
    uint32    micvpsw;
    uint32    vpsw;
    uint32    npsw1;
    uint32    npsw2;

    sim_debug(DEBUG_VMA, &cpu_dev, "LPSW check %08x\n", addr1);
    /* Quick check to exit */
    if (per_en || (addr1 & 0x7) != 0)
        return 0;
    /* Check if function enabled */
    micblok = (cregs[6] & 0xfffff8) >> 2;
    /* Fetch new PSW */
    if (ReadFull(addr1, &npsw1))
        return 0;
    if (ReadFull(addr1 + 4, &npsw2))
        return 0;
    sim_debug(DEBUG_VMA, &cpu_dev, "new %08x %08x\n", npsw1, npsw2);
    /* New PSW has WAIT or invalid ECmode bits */
    if ((npsw1 & 0x20000) != 0)
        return 0;
    if ((npsw1 & 0x80000) != 0 &&
        ((npsw1 & 0xf800c0ff) != 0 || (npsw2 & 0xff000000) != 0))
       return 0;
    /* Fetch the virtual PSW */
    micvpsw = (M[micblok + 2] & AMASK) >> 2;
    vpsw = M[micvpsw];
    sim_debug(DEBUG_VMA, &cpu_dev, "VPSW %08x %08x\n", vpsw, M[micvpsw + 1]);
    /* Check if old PSW has PER set */
    if ((vpsw & 0x40080000) == 0x40080000)
        return 0;
    /* Check if PSW changing BC/EC mode */
    if (((vpsw ^ npsw1) & 0x80000) != 0)
        return 0;
    /* Check for interrupt */
    if (npsw1 & 0x80000) { /* EC Mode */
       /* Check if DAT changed */
       if (((npsw1 ^ vpsw) & 0x04000000) != 0)
           return 0;
       /* Check if IRQ pending and enable interrupt */
       if ((M[micblok + 2] & MSIGN) && (npsw1 & 0x3000000) != 0
                             && ((vpsw ^ npsw1) & npsw1 & 0x3000000) != 0)
           return 0;
       pmsk = (npsw1 >> 8) & 0xf;
       cc = (npsw1 >> 12) & 3;
    } else {
       /* Check if IRQ pending and enable interrupt */
       if ((M[micblok + 2] & MSIGN) && (((vpsw ^ npsw1) & npsw1) & 0xff000000) != 0)
           return 0;
       pmsk = (npsw2 >> 24) & 0xf;
       cc = (npsw2 >> 28) & 0x3;
    }
    M[micvpsw] = npsw1;
    M[micvpsw + 1] = npsw2;
    st_key = (npsw1 >> 16)  & 0xf0;
    /* Set CR6 to new problem state */
    if (npsw1 & 0x10000)
        cregs[6] |= 0x40000000;
    else
        cregs[6] &= 0xbfffffff;
    PC = npsw2 & AMASK;
    sim_debug(DEBUG_VMA, &cpu_dev, "new VPSW %08x %08x\n", npsw1, cregs[6]);
    return 1;
}

/* Handle VM Assists for SSK instructions
 * return 0 if assist could not be completed. Return 1 if successful */
int
vma_stssk(uint32 src1, uint32 addr1)
{
    uint32    micblok;
    uint32    micrseg;
    uint32    page;
    uint32    seg;
    uint32    segpage;
    uint32    pagswp;
    uint32    swpflg;
    uint32    pagcore;
    uint8     stk;

    sim_debug(DEBUG_VMA, &cpu_dev, "SSK check %08x %08x\n", src1, addr1);
    /* Check if enabled */
    if ((cregs[6] & 0xe0000000) != MSIGN || (src1 & 0xf) != 0)
       return 0;
    micblok = (cregs[6] & 0xfffff8) >> 2;
    /* Fetch SEGPAGE */
    micrseg = M[micblok];
    sim_debug(DEBUG_VMA, &cpu_dev, "Micrseg %08x\n", micrseg);
    if (micrseg & 0x2)
        return 0;
    /* Compute address to operate on */
    page = addr1 >> 12;
    if (micrseg & 0x1) {
        seg = page >> 7;
        page &= 0x7f;
    } else {
        seg = page >> 4;
        page &= 0xf;
    }
    segpage = M[((micrseg & AMASK) >> 2) + seg];
    sim_debug(DEBUG_VMA, &cpu_dev, "Segpage %08x s=%x p=%x\n", segpage, seg, page);
    if ((segpage >> 24) <= (addr1 >> 20))
        return 0;
    pagswp = M[((segpage & AMASK) >> 2) - 1];
    sim_debug(DEBUG_VMA, &cpu_dev, "pagswp %08x\n", pagswp);
    swpflg = M[((pagswp + (8 * page)) & AMASK) >> 2];
    sim_debug(DEBUG_VMA, &cpu_dev, "swpflg %08x\n", swpflg);
    pagcore = M[((segpage + (2 * page)) & AMASK) >> 2];
    sim_debug(DEBUG_VMA, &cpu_dev, "pagcore %08x\n", pagcore);
    /* Check if page is valid */
    if ((page & 0x1) == 0)
       pagcore >>= 16;
    if (pagcore & 0xe)
       return 0;
    pagcore &= 0xfff0;
    pagcore <<= 8;
    pagcore |= addr1 & 0x800;
    stk = key[pagcore>>11];
    sim_debug(DEBUG_VMA, &cpu_dev, "real addr %08x %02x\n", pagcore, stk);
    key[pagcore>>11] = (stk & 0xf) | (src1 & 0xf0);
    if (addr1 & 0x800) {
       swpflg = (swpflg & 0xfcffff00) | (((uint32)stk & 0x6) << 25) | (src1 & 0xff);
    } else {
       swpflg = (swpflg & 0xf3ff00ff) | (((uint32)stk & 0x6) << 23) | ((src1 & 0xff) << 8);
    }
    sim_debug(DEBUG_VMA, &cpu_dev, "swpflg %08x\n", swpflg);
    M[((pagswp + (8 * page)) & AMASK) >> 2] = swpflg;
    return 1;
}

/* Handle VM Assists for ISK instructions
 * return 0 if assist could not be completed. Return 1 if successful */
int
vma_stisk(uint8 reg1, uint32 addr1)
{
    uint32    micblok;
    uint32    micrseg;
    uint32    page;
    uint32    seg;
    uint32    segpage;
    uint32    pagswp;
    uint32    swpflg;
    uint32    pagcore;
    uint32    micvpsw;
    uint32    vpsw;
    uint8     stk;

    sim_debug(DEBUG_VMA, &cpu_dev, "ISK check %02x %08x\n", reg1, addr1);
    /* Check if enabled */
    if ((cregs[6] & 0xe0000000) != MSIGN)
       return 0;
    micblok = (cregs[6] & 0xfffff8) >> 2;
    /* Fetch SEGPAGE */
    micrseg = M[micblok];
    sim_debug(DEBUG_VMA, &cpu_dev, "Micrseg %08x\n", micrseg);
    if (micrseg & 0x2)
        return 0;
    /* Compute address to operate on */
    page = addr1 >> 12;
    if (micrseg & 0x1) {
        seg = page >> 7;
        page &= 0x7f;
    } else {
        seg = page >> 4;
        page &= 0xf;
    }
    segpage = M[((micrseg & AMASK) >> 2) + seg];
    sim_debug(DEBUG_VMA, &cpu_dev, "Segpage %08x s=%x p=%x\n", segpage, seg, page);
    if ((segpage >> 24) <= (addr1 >> 20))
        return 0;
    pagswp = M[((segpage & AMASK) >> 2) - 1];
    sim_debug(DEBUG_VMA, &cpu_dev, "pagswp %08x\n", pagswp);
    swpflg = M[((pagswp + (8 * page)) & AMASK) >> 2];
    sim_debug(DEBUG_VMA, &cpu_dev, "swpflg %08x\n", swpflg);
    pagcore = M[((segpage + (2 * page)) & AMASK) >> 2];
    sim_debug(DEBUG_VMA, &cpu_dev, "pagcore %08x\n", pagcore);
    /* Get key value */
    if (addr1 & 0x800) {
        stk = swpflg & 0xfe;
    } else {
        stk = (swpflg >> 8) & 0xfe;
    }
    sim_debug(DEBUG_VMA, &cpu_dev, "stk %02x\n", stk);
    /* Check if page is valid */
    if ((page & 0x1) == 0)
       pagcore >>= 16;
    if ((pagcore & 0xe) == 0) {
       pagcore &= 0xfff0;
       pagcore <<= 8;
       pagcore |= addr1 & 0x800;
       stk |= key[pagcore>>11] & 0x6;
       sim_debug(DEBUG_VMA, &cpu_dev, "real addr %08x %02x\n", pagcore, stk);
    }
    /* Fetch the virtual PSW */
    micvpsw = (M[micblok + 2] & AMASK) >> 2;
    vpsw = M[micvpsw];
    sim_debug(DEBUG_VMA, &cpu_dev, "VPSW %08x %08x\n", vpsw, M[micvpsw + 1]);
    if ((vpsw & 0x80000) == 0) { /* BC Mode */
       stk &= 0xf0;
    }
    regs[reg1] = (regs[reg1] & 0xffffff00) | (uint32)stk;
    return 1;
}

/* Handle VM Assists for SVC instructions
 * return 0 if assist could not be completed. Return 1 if successful */
int
vma_stsvc(uint8 reg)
{
    uint32    micblok;
    uint32    micrseg;
    uint32    micvpsw;
    uint32    segpage;
    uint32    pagcore;
    uint32    psa;
    uint32    vpsw;
    uint32    npsw1;
    uint32    npsw2;

    sim_debug(DEBUG_VMA, &cpu_dev, "SVC check %02x\n", reg);
    if (per_en || reg == 76)
        return 0;
    micblok = (cregs[6] & 0xfffff8) >> 2;
    /* Fetch the virtual PSW */
    micvpsw = (M[micblok + 2] & AMASK) >> 2;
    vpsw = M[micvpsw];
    sim_debug(DEBUG_VMA, &cpu_dev, "VPSW %08x %08x\n", vpsw, M[micvpsw + 1]);
    /* Check if old PSW has PER set */
    if ((vpsw & 0x40080000) == 0x40080000)
        return 0;
    /* Fetch SEGPAGE */
    micrseg = M[micblok];
    segpage = M[(micrseg & AMASK) >> 2];
    sim_debug(DEBUG_VMA, &cpu_dev, "Segpage %08x\n", segpage);
    pagcore = M[(segpage & AMASK) >> 2];
    sim_debug(DEBUG_VMA, &cpu_dev, "pagcore %08x\n", pagcore);
    psa = pagcore >> 16;
    sim_debug(DEBUG_VMA, &cpu_dev, "psa %08x\n", psa);
    /* determine if page is valid */
    /* Check if 4k or 2k paging */
    if ((micrseg & 02) != 0) {
       /* 2K paging */
       if (psa & 0x6)
           return 0;
       psa &= 0xfff8;
       psa <<= 7;
    } else {
       /* 4K paging */
       if (psa & 0xe)
           return 0;
       psa &= 0xfff0;
       psa <<= 8;
    }
    /* PSA now points correctly */
    npsw1 = M[(psa + 0x60) >> 2];
    npsw2 = M[(psa + 0x64) >> 2];
    sim_debug(DEBUG_VMA, &cpu_dev, "new PSW %08x %08x\n", npsw1, npsw2);
    /* New PSW has WAIT or invalid ECmode bits */
    if ((npsw1 & 0x20000) != 0)
        return 0;
    if ((npsw1 & 0x80000) != 0 &&
        ((npsw1 & 0xf800c0ff) != 0 || (npsw2 & 0xff000000) != 0))
       return 0;
    /* Check if PSW changing BC/EC mode */
    if (((vpsw ^ npsw1) & 0x80000) != 0)
        return 0;
    /* Check for interrupt */
    if (npsw1 & 0x80000) {
       /* Check if DAT changed */
       if (((npsw1 ^ vpsw) & 0x04000000) != 0)
           return 0;
       /* Check if IRQ pending and enable interrupt */
       if ((M[micblok + 2] & MSIGN) && (npsw1 & 0x3000000) != 0
                             && ((vpsw ^ npsw1) & npsw1 & 0x3000000) != 0)
           return 0;
    } else {
       /* Check if IRQ pending and enable interrupt */
       if ((M[micblok + 2] & MSIGN) && (((vpsw ^ npsw1) & npsw1) & 0xff000000) != 0)
           return 0;
    }
    /* Construct old PSW */
    if (vpsw & 0x80000) {
        uint32  temp1;
        /* Generate first word */
        temp1 = (vpsw & 0xff0f0f00) |
               (((uint32)st_key) << 16) |
               (((uint32)cc) << 12) |
               (((uint32)pmsk) << 8);
        /* Save old PSW */
        M[(psa + 0x20) >> 2] = temp1;
        M[(psa + 0x24) >> 2] = PC & AMASK;
        M[(psa + 0x88) >> 2] = (1 << 17) | reg;
        sim_debug(DEBUG_VMA, &cpu_dev, "Old PSW %08x %08x\n", temp1, PC);
    } else {
        uint32  temp1;
        uint32  temp2;
        /* Generate first word */
        temp1 = (vpsw & 0xff0f0000) |
               (((uint32)st_key) << 16) |
               ((uint32)reg);
        /* Generate second word. */
        temp2 = (((uint32)1) << 30) |
               (((uint32)cc) << 28) |
               (((uint32)pmsk) << 24) |
               (PC & AMASK);
        /* Save old PSW */
        M[(psa + 0x20) >> 2] = temp1;
        M[(psa + 0x24) >> 2] = temp2;
        sim_debug(DEBUG_VMA, &cpu_dev, "Old PSW %08x %08x\n", temp1, temp2);
    }
    /* Set machine for new PSW */
    M[micvpsw] = npsw1;
    M[micvpsw + 1] = npsw2;
    if (npsw1 & 0x80000) {
       pmsk = (npsw1 >> 8) & 0xf;
       cc = (npsw1 >> 12) & 3;
    } else {
       pmsk = (npsw2 >> 24) & 0xf;
       cc = (npsw2 >> 28) & 0x3;
    }
    st_key = (npsw1 >> 16)  & 0xf0;
    /* Set CR6 to new problem state */
    if (npsw1 & 0x10000)
        cregs[6] |= 0x40000000;
    else
        cregs[6] &= 0xbfffffff;
    PC = npsw2 & AMASK;
    key[psa >> 11] |= 0x6;
    sim_debug(DEBUG_VMA, &cpu_dev, "new VPSW %08x %08x %08x\n", npsw1, npsw2, cregs[6]);
    return 1;
}

/* Handle VM Assists for LRA instructions
 * return 0 if assist could not be completed. Return 1 if successful */
int
vma_lra(uint8 reg, uint32 addr1)
{
    sim_debug(DEBUG_VMA, &cpu_dev, "LRA check %02x %08x\n", reg, addr1);
    return 0;
}


/* Handle VM Assists for STNSM instructions
 * return 0 if assist could not be completed. Return 1 if successful */
int
vma_stnsm(uint8 reg, uint32 addr1)
{
    uint32    micblok;
    uint32    miccreg;
    uint32    micvpsw;
    uint32    vpsw;
    uint32    temp;

    sim_debug(DEBUG_VMA, &cpu_dev, "STNSM check %02x %08x\n", reg, addr1);
    micblok = (cregs[6] & 0xfffff8) >> 2;
    /* Check if function enabled */
    if ((cregs[6] & 0x01000000) != 0) {
        miccreg = M[micblok + 5];
        sim_debug(DEBUG_VMA, &cpu_dev, "SSM micacf %08x\n", miccreg);
        if ((miccreg & 0x00800000) == 0)
           return 0;
    }
    /* Fetch Virtual Cr 0 */
    miccreg = M[micblok + 1];
    sim_debug(DEBUG_VMA, &cpu_dev, "SSM miccreg %08x\n", miccreg);
    temp = M[(miccreg & AMASK) >> 2];
    /* Fetch the virtual PSW */
    micvpsw = M[micblok + 2];
    vpsw = M[(micvpsw & AMASK) >> 2];
    /* Don't allow change of PER or DAT */
    if (vpsw & 0x80000 && (reg & 0x44) != 0x44)
        return 0;
    /* Update Mask */
    temp = (vpsw >> 24) & 0xff;
    vpsw &= ((uint32)reg << 24) | 0xffffff;
    sim_debug(DEBUG_VMA, &cpu_dev, "VPSW %08x d=%08x\n", vpsw, temp);
    /* Save old mask in addr1 */
    if (WriteByte(addr1, temp))
        return 0;
    M[(micvpsw & AMASK) >> 2] = vpsw;
    sim_debug(DEBUG_VMA, &cpu_dev, "new VPSW %08x\n", vpsw);
    return 1;
}

/* Handle VM Assists for STOSM instructions
 * return 0 if assist could not be completed. Return 1 if successful */
int
vma_stosm(uint8 reg, uint32 addr1)
{
    uint32    micblok;
    uint32    miccreg;
    uint32    micvpsw;
    uint32    vpsw;
    uint32    temp;
    int       flags;

    sim_debug(DEBUG_VMA, &cpu_dev, "STOSM check %02x %08x\n", reg, addr1);
    micblok = (cregs[6] & 0xfffff8) >> 2;
    /* Check if function enabled */
    if ((cregs[6] & 0x01000000) != 0) {
        miccreg = M[micblok + 5];
        sim_debug(DEBUG_VMA, &cpu_dev, "SSM micacf %08x\n", miccreg);
        if ((miccreg & 0x00800000) == 0)
           return 0;
    }
    /* Fetch Virtual Cr 0 */
    miccreg = M[micblok + 1];
    sim_debug(DEBUG_VMA, &cpu_dev, "SSM miccreg %08x\n", miccreg);
    temp = M[(miccreg & AMASK) >> 2];
    if ((temp & 0x40000000) != 0)
        return 0;
    /* Fetch the virtual PSW */
    micvpsw = M[micblok + 2];
    vpsw = M[(micvpsw & AMASK) >> 2];
    flags = (vpsw >> 24) ^ reg;
    if (vpsw & 0x80000) {
       /* Don't allow change of PER or DAT */
       if ((flags & 0xfc) != 0x0)
           return 0;
       /* If enabling an interrupt and one pending, abort */
       if ((micvpsw & MSIGN) && (flags & reg & 0x3) != 0)
           return 0;
    } else {
       /* Check if IRQ pending and enable interrupt */
       if ((vpsw & MSIGN) && ((flags & reg) & 0xff) != 0)
           return 0;
    }
    /* Save old mask */
    temp = (vpsw >> 24) & 0xff;
    /* Update Mask */
    vpsw |= ((uint32)reg << 24);
    sim_debug(DEBUG_VMA, &cpu_dev, "VPSW %08x d=%08x\n", vpsw, temp);
    if (WriteByte(addr1, temp))
        return 0;
    M[(micvpsw & AMASK) >> 2] = vpsw;
    sim_debug(DEBUG_VMA, &cpu_dev, "new VPSW %08x\n", vpsw);
    return 1;
}

/* Handle VM Assists for STCTL instructions
 * return 0 if assist could not be completed. Return 1 if successful */
int
vma_stctl(uint8 reg, uint32 addr1)
{
    int       reg1 = R1(reg);
    uint32    miccreg;
    uint32    micblok;

    sim_debug(DEBUG_VMA, &cpu_dev, "STCL check %02x %08x\n", reg, addr1);
    if ((addr1 & 0x3) != 0)
        return 0;
    micblok = (cregs[6] & 0xfffff8) >> 2;
    /* Check if function enabled */
    if ((cregs[6] & 0x01000000) != 0) {
        miccreg = M[micblok + 5];
        sim_debug(DEBUG_VMA, &cpu_dev, "STCL micacf %08x\n", miccreg);
        if ((miccreg & 0x00800000) == 0)
           return 0;
    }
    /* Point to CR0 */
    miccreg = M[micblok + 1];
    reg = R2(reg);
    for (;;) {
        uint32     temp;
        temp = M[miccreg + reg1];
        if (WriteFull(addr1, temp))
            return 0;
        if (reg1 == reg)
            break;
        reg1++;
        reg1 &= 0xf;
        addr1 += 4;
    } ;
    return 1;
}

