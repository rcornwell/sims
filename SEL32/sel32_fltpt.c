/* sel32_fltpt.c: SEL 32 floating point instructions processing.

   Copyright (c) 2018-2020, James C. Bevier

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

   This set of subroutines simulate the excess 64 floating point instructions.
   ADFW - add memory float to register
   ADFD - add memory double to register pair
   SUFW - subtract memory float from register
   SUFD - subtract memory double from register pair
   MPFW - multiply register by memory float
   MPFD - multiply register pair by memory double
   DVFW - divide register by memory float
   DVFD - divide register pair by memory double
   FIXW - convert float to integer (32 bit)
   FIXD - convert double to long long (64 bit)
   FLTW - convert integer (32 bit) to float
   FLTD - convert long long (64 bit) to double
   ADRFW - add regist float to register
   SURFW - subtract register float from register
   DVRFW - divide register float by register float
   MPRFW - multiply register float by register float
   ADRFD - add register pair double to register pair double
   SURFD - subtract register pair double from register pair double
   DVRFD - divide register pair double by register pair double
   MPRFD - multiply register pair double by register pair double

   Floating Point Formats
   float
   S - 1 sign bit
   X - 7 bit exponent
   M - 24 bit mantissa
   S XXXXXXX MMMMMMMM MMMMMMMM MMMMMMMM
   double
   S - 1 sign bit
   X - 7 bit exponent
   M - 56 bit mantissa
   S XXXXXXX MMMMMMMM MMMMMMMM MMMMMMMM MMMMMMMM MMMMMMMM MMMMMMMM MMMMMMMM

*/

#include "sel32_defs.h"

uint32   s_fixw(uint32 val, uint32 *cc);
uint32   s_fltw(uint32 val, uint32 *cc);
t_uint64 s_fixd(t_uint64 val, uint32 *cc);
t_uint64 s_fltd(t_uint64 val, uint32 *cc);
uint32   s_nor(uint32 reg, uint32 *exp);
t_uint64 s_nord(t_uint64 reg, uint32 *exp);
uint32   s_adfw(uint32 reg, uint32 mem, uint32 *cc);
uint32   s_sufw(uint32 reg, uint32 mem, uint32 *cc);
t_uint64 s_adfd(t_uint64 reg, t_uint64 mem, uint32 *cc);
t_uint64 s_sufd(t_uint64 reg, t_uint64 mem, uint32 *cc);
uint32   s_mpfw(uint32 reg, uint32 mem, uint32 *cc);
uint32   s_dvfw(uint32 reg, uint32 mem, uint32 *cc);
t_uint64 s_mpfd(t_uint64 reg, t_uint64 mem, uint32 *cc);
t_uint64 s_dvfd(t_uint64 reg, t_uint64 mem, uint32 *cc);
uint32   s_normfw(uint32 mem, uint32 *cc);
t_uint64 s_normfd(t_uint64 mem, uint32 *cc);
uint32   o_adfw(uint32 reg, uint32 mem, uint32 *cc);
t_uint64 o_adfd(t_uint64 reg, t_uint64 mem, uint32 *cc);
t_uint64 n_adfd(t_uint64 reg, t_uint64 mem, uint32 *cc, uint32 type);

#define NORMASK 0xf8000000              /* normalize 5 bit mask */
#define DNORMASK 0xf800000000000000ll   /* double normalize 5 bit mask */
#define EXMASK  0x7f000000              /* exponent mask */
#define FRMASK  0x80ffffff              /* fraction mask */
#define DEXMASK 0x7f00000000000000ll    /* exponent mask */
#define DFSVAL  0xff00000000000000ll    /* minus full scale value */
#define DFRMASK 0x80ffffffffffffffll    /* fraction mask */
#define NEGATE32(val)   ((~val) + 1)    /* negate a value 16/32/64 bits */
#define MEMNEG  1                       /* mem arg is negative */
#define REGNEG  2                       /* reg arg is negative */
#define RESNEG  4                       /* result is negative */

#define USE_ORIG
//#define USE_NEW
//#define USE_DIAG

/* normalize floating point fraction */
uint32 s_nor(uint32 reg, uint32 *exp) {
    uint32 texp = 0;                    /* no exponent yet */

    sim_debug(DEBUG_EXP, &cpu_dev,
        "s_nor entry reg %08x texp %08x\n", reg, texp);
    if (reg != 0) {                     /* do nothing if reg is already zero */
        uint32 mv = reg & NORMASK;      /* mask off bits 0-4 */
        while ((mv == 0) || (mv == NORMASK)) {
            /* not normalized yet, so shift 4 bits left */
            reg <<= 4;                  /* move over 4 bits */
            texp++;                     /* bump shift count */
            mv = reg & NORMASK;         /* just look at bits 0-4 */
        }
        /* bits 0-4 of reg is neither 0 nor all ones */
        /* show that reg is normalized */
        texp = (uint32)(0x40-(int32)texp);  /* subtract shift count from 0x40 */
    }
    *exp = texp;                        /* return exponent */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "s_nor exit reg %08x texp %08x\n", reg, texp);
    return (reg);                       /* return normalized register */
}

/* normalize double floating point number */
t_uint64 s_nord(t_uint64 reg, uint32 *exp) {
    uint32 texp = 0;                    /* no exponent yet */

    sim_debug(DEBUG_EXP, &cpu_dev,
        "s_nord entry regs %016llx texp %08x\n", reg, texp);
    if (reg != 0) {                     /* do nothing if reg is already zero */
        t_uint64 mv = reg & DNORMASK;   /* mask off bits 0-4 */
        while ((mv == 0) || (mv == DNORMASK)) {
            /* not normalized yet, so shift 4 bits left */
            reg <<= 4;                  /* move over 4 bits */
            texp++;                     /* bump shift count */
            mv = reg & DNORMASK;        /* just look at bits 0-4 */
        }
        /* bits 0-4 of reg is neither 0 nor all ones */
        /* show that reg is normalized */
        texp = (uint32)(0x40-(int32)texp);  /* subtract shift count from 0x40 */
    }
    *exp = texp;                        /* return exponent */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "s_nord exit reg %016llx texp %08x\n", reg, texp);
    return (reg);                       /* return normalized double register */
}

/**************************************************************
* This routine unpacks the floating point number in (r6,r7).  *
* The unbiased, right justified, two's complement exponent    *
* is returned in r1.  If xxfw is set, the two's complement    *
* fraction (with the binary point to the left of bit 8) is    *
* returned in r6, and r7 is cleared.  if xxfd is reset, the   *
* two's complement double precision fraction is returned in   *
* (r6,r7).                                                    *
***************************************************************/
/* type of operation action flags */
#define FPWDV 0x0001                    /* floating point word operation */
#define FPADD 0x0002                    /* floating point add */

struct fpnum {
    t_uint64    num;                    /* 64 bit number */
    int32       msw;                    /* most significent word */
    int32       lsw;                    /* least significient word */
    int32       exp;                    /* exponent */
    int32       CCs;                    /* condition codes */
    uint8       sign;                   /* original sign */
    uint8       flags;                  /* action flags for number */
};

/* unpack single precision floating point number */
void unpacks(struct fpnum *np) {
    uint32 ex = np->msw & 0xff000000;   /* get exponent & sign from msw */

    sim_debug(DEBUG_EXP, &cpu_dev,
        "unpacks entry msw %08x exp %08x\n", np->msw, ex);
    np->lsw = 0;                        /* 1 word for single precision */
//  ex = np->msw & 0xff000000;          /* get exponent & sign from msw */
    if (ex & 0x80000000)                /* check for negative */
        ex ^= 0xff000000;               /* reset sign & complement exponent */
    np->msw ^= ex;                      /* replace exponent with sign extension */
    ex = ((uint32)ex) >> 24;            /* srl 24 right justify the exponent */
    ex -= 0x40;                         /* unbias exponent */
    np->exp = ex;                       /* save the exponent */
    np->CCs = 0;                        /* zero CCs for later */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "unpacks return msw %08x exp %08x\n", np->msw, ex);
    return;
}

/* unpack double precision floating point number */
void unpackd(struct fpnum *np) {
    int32 ex = np->msw & 0xff000000;    /* get exponent & sign from msw */
    if (ex & 0x80000000)                /* check for negative */
        ex ^= 0xff000000;               /* reset sign & complement exponent */
    np->msw ^= ex;                      /* replace exponent with sign extension */
    ex = ((uint32)ex) >> 24;            /* srl 24 right justify the exponent */
    ex -= 0x40;                         /* unbias exponent */
    np->exp = ex;                       /* save exponent */
    np->CCs = 0;                        /* zero CCs for later */
    return;
}

/**************************************************************
* Common routine for finishing the various F.P. instruction   *
* simulations.  at this point, r1 = raw exponent, and         *
* (r6,r7) = unnormalized fraction, with the binary point to   *
* the left of r6[8].  The simulated condition codes will be   *
* returned in bits 1 through 4 of "sim.flag."                 *
*                                                             *
* Floating point operations not terminating with an arith-    *
* metic exception produce the following condition codes:      *
*                                                             *
* CC1   CC2   CC3   CC4               Definition              *
* -------------------------------------------------------     *
*  0     1     0     0    no exception, fraction positive     *
*  0     0     1     0    no exception, fraction negative     *
*  0     0     0     1    no exception, fraction = zero       *
*                                                             *
*                                                             *
* an arithmetic exception produces the follwing condition     *
* code settings:                                              *
*                                                             *
* CC1   CC2   CC3   CC4               Definition              *
* --------------------------------------------------------    *
*  1     0     1     0    exp underflow, fraction negative    *
*  1     0     1     1    exp overflow, fraction negative     *
*  1     1     0     0    exp underflow, fraction positive    *
*  1     1     0     1    exp overflow, fraction positive     *
*                                                             *
**************************************************************/  
/*   Normalization and rounding of single precision number */                                 
void packs(struct fpnum *np) {
    uint32  ex, tmp, tmp2;

    t_uint64 num = ((t_uint64)np->msw << 32) | (uint32)np->lsw; /* make 64 bit num */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "pack entry msw %08x lsw %08x exp %08x num %016llx\n",
        np->msw, np->lsw, np->exp, num);

    num = ((t_int64)num) << 3;      /* slad 3 to align for normalization */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "pack pl 0 num %016llx exp %08x\n", num, np->exp);

    num = s_nord(num, &ex);         /* normalize the number with cnt in ex */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "pack pl 1 num %016llx ex %08x exp %08x\n", num, ex, np->exp);

    num = ((t_int64)num) >> 7;      /* srad 7 to align & sign extend number */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "pack pl 2 num %016llx ex %08x exp %08x\n", num, ex, np->exp);

    if (num & DMSIGN)               /* test for negative fraction */
        np->CCs = CC3BIT;           /* show fraction negative */
    else
    if (num == 0) {
        np->CCs = CC4BIT;           /* show fraction zero */
        np->msw = 0;                /* save msw */
        np->lsw = 0;                /* save lsw */
        np->exp = 0;                /* save exp */
        return;                     /* done if zero */
    }
    else
        np->CCs = CC2BIT;           /* show fraction positive */

    sim_debug(DEBUG_EXP, &cpu_dev,
        "pack pl 3 CC %08x num = %016llx ex = %08x\n", np->CCs, num, ex);
    /* we need to round single precision number */
    tmp = (num >> 32) & 0xffffffff; /* get msw */
    tmp2 = num & 0xffffffff;        /* get lsw */
    if ((int32)tmp >= 0x00ffffff) { /* if at max, no rounding */
        if (tmp2 & 0x80000000)      /* round if set */
            tmp += 1;               /* round fraction */
    }
    num = (((t_uint64)tmp) << 32);  /* make 64 bit num with lo 32 bits zero */

    sim_debug(DEBUG_EXP, &cpu_dev,
        "pack pl 4 num %016llx msw %08x exp %08x ex %08x\n", num, np->msw, np->exp, ex);
    if (((t_int64)num) == DFSVAL) {  /* see if > 0xff00000000000000 */
        /* fixup fraction to not have -1 in results */
        num = ((t_int64)num) >> 4;  /* sra 4 shift over 4 bits */
        ex += 1;                    /* bump exponent */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "pack pl 4a num = %016llx exp = %08x ex = %08x\n", num, np->exp, ex);
    }
    /*  end normalization and rounding */                                   
    np->exp += ex;                  /* normalized, biased exp */
    np->exp += 1;                   /* correct shift count */

    sim_debug(DEBUG_EXP, &cpu_dev,
        "pack n&r num %016llx msw %08x exp %08x ex %08x\n", num, np->msw, np->exp, ex);
    if (((int32)(np->exp)) < 0) {   /* check for exp neg underflow */
        np->CCs |= CC1BIT;          /* set CC1 & return zero */
        np->num = 0;
        np->msw = 0;
        np->lsw = 0;
        return;
    }
    sim_debug(DEBUG_EXP, &cpu_dev,
        "pack exp num %016llx msw %08x exp %08x ex %08x\n", num, np->msw, np->exp, ex);
    /* if no exponent overflow merge exp & fraction and return */
    if (((int32)(np->exp)) <= 0x7f) {
        np->msw = (num >> 32);      /* get msw */
        np->lsw = num & 0xffffffff; /* get lsw */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "packs ret msw %08x exp %08x\n", np->msw, np->exp);
        ex = np->exp << 24;         /* put exponent in bits 1-7 */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "packs ret msw %08x exp %08x ex %08x\n", np->msw, np->exp, ex);
        np->msw ^= ex;              /* merge exponent & fraction */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "packs ret CCs %08x msw %08x exp %08x ex %08x\n", np->CCs, np->msw, np->exp, ex);
        return;                     /* CCs already set */
    }
    /* process overflow exponent */
    np->CCs |= CC1BIT;              /* set CC1 */
    np->CCs |= CC4BIT;              /* set CC4 */
    /* do SP max value */
    if (np->CCs & CC2BIT) {         /* see if CC2 is set */
        np->msw = 0x7fffffff;       /* yes, use max pos value */
        np->lsw = 0;                /* zero for SP */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "pack SP xit1 CCs %08x msw %08x exp %08x ex %08x\n",
            np->CCs, np->msw, np->exp, ex);
        return;
    }
    np->msw = 0x80000001;           /* set max neg value */
    np->lsw = 0;                    /* zero for sp */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "pack SP xit2 CCs %08x msw %08x exp %08x ex %08x\n",
        np->CCs, np->msw, np->exp, ex);
    return;
}

/**   Normalization and rounding of double precision number */                                 
void packd(struct fpnum *np) {
    uint32  ex;

    t_uint64 num = ((t_uint64)np->msw << 32) | np->lsw; /* make 64 bit num */

    num = ((t_int64)num) << 3;      /* sla 3 to align for normalization */
    num = s_nord(num, &ex);         /* normalize the number with cnt in ex */
    num = ((t_int64)num) >> 7;      /* align & sign extend number */
    if (num & DMSIGN)               /* test for negative fraction */
        np->CCs = CC3BIT;           /* show fraction negative */
    else
    if (num == 0) {
        np->CCs = CC4BIT;           /* show fraction zero */
        np->msw = 0;                /* save msw */
        np->lsw = 0;                /* save lsw */
        np->exp = 0;                /* save exp */
        return;                     /* done if zero */
    }
    else
        np->CCs = CC2BIT;           /* show fraction positive */

    if ((t_int64)num == DFSVAL) {   /* see if > 0xff00000000000000 */
        num >>= 4;                  /* shift over 4 bits */
        ex += 1;                    /* bump exponent */
    }
    /* end normalization and rounding */                                   
    np->exp += ex;                  /* normalized, biased exp */
    np->exp += 1;                   /* correct shift count */
    if (((int32)(np->exp)) < 0) {   /* check for exp neg underflow */
        np->CCs |= CC1BIT;          /* set CC1 & return zero */
        np->num = 0;
        np->msw = 0;
        np->lsw = 0;
       return;
    }
    /* if no exponent overflow merge exp & fraction & return */
    if (((int32)(np->exp)) <= 0x7f) {
        np->msw = (num >> 32);      /* get msw */
        np->lsw = num & 0xffffffff; /* get lsw */
        ex = np->exp << 24;         /* put exponent in bits 1-7 */
        np->msw ^= ex;              /* merge exponent & fraction */
        return;                     /* CCs already set */
    }
    /* process overflow exponent */
    np->CCs |= CC1BIT;              /* set CC1 & return zero */
    np->CCs |= CC4BIT;              /* set CC4 & return zero */
    /* do DP max value */
    if (np->CCs & CC2BIT) {         /* see if CC2 is set */
        np->msw = 0x7fffffff;       /* CC2=1 set exp overflow, pos frac */
        np->lsw = 0xffffffff;
        return;
    }
    np->msw = 0x80000000;           /* CC2=0 set exp underflow, frac pos */
    np->lsw = 0x00000001;
    return;
}

/* normalize the memory value when adding number to zero */
uint32 s_normfw(uint32 mem, uint32 *cc) {
    struct fpnum fpn = {0};
    uint32  ret;

    if (mem == 0) {                     /* make sure we have a number */
        *cc = CC4BIT;                   /* set the cc's */
        return 0;                       /* return zero */
    }
    sim_debug(DEBUG_EXP, &cpu_dev,
        "NORMFW entry mem %08x\n", mem);
#ifndef OLDWAY
    fpn.msw = mem;                      /* save upper 32 bits */
    fpn.lsw = 0;                        /* clear lower 32 bits */
    unpacks(&fpn);                      /* unpack number */
    packs(&fpn);                        /* pack it back up */
#else
    fpn.sign = 0;

    /* special case 0x80000000 (-0) to set CCs to 1011 * value to 0x80000001 */
    if (mem == 0x80000000) {
        fpn.CCs = CC1BIT|CC3BIT|CC4BIT; /* we have AE, exp overflow, neg frac */
        fpn.msw = 0x80000001;           /* return max neg value */
        goto goret;                     /* return */
    }

    /* special case pos exponent & zero mantissa to be 0 */
    if (((mem & 0x80000000) == 0) && ((mem & 0xff000000) > 0) && ((mem & 0x00ffffff) == 0)) {
        fpn.msw = 0;                    /* 0 to any power is still 0 */
        fpn.CCs = CC4BIT;               /* set zero CC */
        goto goret;                     /* return */
    }

    /* if we have 1xxx xxxx 0000 0000 0000 0000 0000 0000 */
    /* we need to convert to 1yyy yyyy 1111 0000 0000 0000 0000 0000 */
    /* where y = x - 1 */
    if ((mem & 0x80ffffff) == 0x80000000) {
        int nexp = (0x7f000000 & mem) - 0x01000000;
        mem = 0x80000000 | (nexp & 0x7f000000) | 0x00f00000;
    }

    fpn.exp = (mem & 0x7f000000) >> 24; /* get exponent */
    if (mem & 0x80000000) {             /* test for neg */
        fpn.sign = MEMNEG;              /* we are neq */
        mem = NEGATE32(mem);            /* two's complement */
        fpn.exp ^= 0x7f;                /* complement exponent */
    }
    fpn.msw = mem & 0x00ffffff;         /* get mantissa */

    /* now make sure number is normalized */
    if (fpn.msw != 0) {
        while ((fpn.msw & 0x00f00000) == 0) {
            fpn.msw <<= 4;              /* move up a nibble */
            fpn.exp--;                  /* and decrease exponent */
        }
    }
    if (fpn.exp < 0) {
        fpn.CCs = CC1BIT;               /* we have underflow */
        if (fpn.sign & MEMNEG)          /* we are neg (1) */
            fpn.CCs |= CC3BIT;          /* set neg CC */
        else
            fpn.CCs |= CC2BIT;          /* set pos CC */
        fpn.msw = 0;                    /* number too small, make 0 */
        fpn.exp = 0;                    /* exponent too */
        goto goret;                     /* return */
    }

    /* rebuild normalized number */
    fpn.msw = ((fpn.msw & 0x00ffffff) | ((fpn.exp & 0x7f) << 24));
    if (fpn.sign & MEMNEG)              /* we are neg (1) */
        fpn.msw = NEGATE32(fpn.msw);    /* two's complement */
    if (fpn.msw == 0)
        fpn.CCs = CC4BIT;               /* show zero */
    else if (fpn.msw & 0x80000000)      /* neqative? */
        fpn.CCs = CC3BIT;               /* show negative */
    else
        fpn.CCs = CC2BIT;               /* show positive */
goret:
#endif
    ret = fpn.msw;                      /* return normalized number */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "NORMFW return mem %08x result %08x CC's %08x\n", mem, ret, fpn.CCs);
    /* return normalized number */
    *cc = fpn.CCs;                      /* set the cc's */
    return ret;                         /* return result */
}

/* add memory floating point number to register floating point number */
/* set CC1 if overflow/underflow */
uint32 s_adfw(uint32 reg, uint32 mem, uint32 *cc) {
    uint32      retval, CC;

#ifdef USE_ORIG
//  printf("s_adfw entry reg %08x mem %08x\n", reg, mem);

    /* do original SEL floating add */
    retval = o_adfw(reg, mem, &CC);     /* get 32 bit value */
//  printf("s_adfw exit result %08x CC's %08x\n", retval, CC);
    *cc = CC;                           /* return the CC's */
    return retval;                      /* we are done */
#endif

#ifndef USE_ORIG
    u_int64_t   dst, src, ret;
    int32       type = (FPWDV | FPADD); /* do single precision add */

//  printf("s_adfw entry reg %08x mem %08x\n", reg, mem);
    dst = ((u_int64_t)reg) << 32;       /* make 64 bit value */
    src = ((u_int64_t)mem) << 32;       /* make 64 bit value */

#ifdef USE_DIAG
    /* do diagnostic floating add */
    ret = d_adsu(dst, src, &CC, type);
#endif

#ifdef USE_NEW
    /* do new floating add */
    ret = n_adfd(dst, src, &CC, type);
#endif

    retval = (ret >> 32) & 0xffffffff;  /* 32 bits */
//  printf("s_adfw exit ret %016lx result %08x CC's %08x\n", ret, retval, CC);
    *cc = CC;                           /* return the CC's */
    return retval;                      /* we are done */
#endif /* USE_ORIG */
}

#ifdef USE_ORIG
/* add memory floating point number to register floating point number */
/* set CC1 if overflow/underflow */
uint32 o_adfw(uint32 reg, uint32 mem, uint32 *cc) {
    uint32 mfrac, rfrac, frac, ret=0, oexp;
    uint32 CC, sc, sign, expr, expm, exp;
#ifndef DO_NORMALIZE
    struct fpnum fpn = {0};
#endif

    *cc = 0;                            /* clear the CC'ss */
    CC = 0;                             /* clear local CC's */

    sim_debug(DEBUG_EXP, &cpu_dev,
        "ADFW entry mem %08x reg %08x\n", mem, reg);
    /* process the memory operand value */
    if (mem == 0) {                     /* test for zero operand */
        ret = reg;                      /* return original register value */
#ifndef DO_NORMALIZE
        if (ret == 0)
            goto goout;                 /* nothing + nothing = nothing */
        fpn.msw = ret;                  /* save upper 32 bits */
        fpn.lsw = 0;                    /* clear lower 32 bits */
        unpacks(&fpn);                  /* unpack number */
        packs(&fpn);                    /* pack it back up */
        ret = fpn.msw;                  /* return normalized number */
        CC = fpn.CCs;                   /* set the cc's */
        goto goout2;                    /* go set cc's and return */
#else
        ret = s_normfw(reg, &CC);       /* normalize the reg */
        if (CC & CC1BIT)                /* if AE, just exit */
            goto goout2;
#endif
        goto goout;                     /* go set cc's and return */
    }

//#define EXMASK  0x7f000000              /* exponent mask */
    expm = mem & EXMASK;                /* extract exponent from operand */
//#define FRMASK  0x80ffffff              /* fraction mask */
    mfrac = mem & FRMASK;               /* extract fraction */
    if (mfrac & MSIGN)  {               /* test for negative fraction */
        /* negative fraction */
        expm ^= EXMASK;                 /* ones complement the exponent */
        mfrac |= EXMASK;                /* adjust the fraction */
    }
    mfrac = ((int32)mfrac) << 4;        /* do sla 4 of fraction */

    /* process the register operator value */
    if (reg == 0) {
        ret = mem;                      /* return original mem operand value */
#ifndef DO_NORMALIZE
        if (ret == 0)
            goto goout;                 /* nothing + nothing = nothing */
        /* reg is 0 and mem is not zero, normalize mem */
        fpn.msw = ret;                  /* save upper 32 bits */
        fpn.lsw = 0;                    /* clear lower 32 bits */
        unpacks(&fpn);                  /* unpack number */
        packs(&fpn);                    /* pack it back up */
        ret = fpn.msw;                  /* return normalized number */
        CC = fpn.CCs;                   /* set the cc's */
        goto goout2;                    /* go set cc's and return */
#else
        ret = s_normfw(mem, &CC);       /* normalize the mem */
        if (CC & CC1BIT)                /* if AE, just exit */
            goto goout2;
#endif
        goto goout;                     /* go set cc's and return */
    }
    CC = 0;                             /* clear local CC's */

//#define EXMASK  0x7f000000              /* exponent mask */
    expr = reg & EXMASK;                /* extract exponent from reg operand */
//#define FRMASK  0x80ffffff              /* fraction mask */
    rfrac = reg & FRMASK;               /* extract fraction */
    if (rfrac & MSIGN) {                /* test for negative fraction */
        /* negative fraction */
        expr ^= EXMASK;                 /* ones complement the exponent */
        rfrac |= EXMASK;                /* adjust the fraction */
    }
    rfrac = ((int32)rfrac) << 4;        /* do sla 4 of fraction */

    exp = expr - expm;                  /* subtract memory exponent from reg exponent */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "ADFW2 exp calc expr %04x expm %04x exp %04x\n", expr, expm, exp);
    if (exp & MSIGN) {                  /* test for negative */
        /* difference is negative, so memory exponent is larger */
        exp = NEGATE32(exp);            /* make difference positive */
        if (exp > 0x06000000) {         /* test for difference > 6 */
            ret = mem;                  /* assume 0 and return original mem operand value */
            goto goout;                 /* go set cc's and return */
        }
        /* difference is <= 6, so adjust to smaller value for add */
        /* difference is number of 4 bit right shifts to do */
        /* (exp >> 24) * 4 */
        sc = (uint32)exp >> (24 - 2);   /* shift count down to do x4 the count */
        rfrac = ((int32)rfrac) >> sc;
        oexp = expm;                    /* mem is larger exponent, save for final exponent add */
    } else {
        /* difference is zero or positive, so register exponent is larger */
        if (exp > 0x06000000) {         /* test for difference > 6 */
            /* difference is > 6, so just act like add of zero */
            /* memory value is small, so return reg value as result */
            ret = reg;                  /* return original register value */
            goto goout;                 /* go set cc's and return */
        }
        /* difference is <= 6, so adjust to smaller value for add */
        /* difference is number of 4 bit right shifts to do */
        /* (exp >> 24) * 4 */
        sc = (uint32)exp >> (24 - 2);   /* shift count down to do x4 the count */
        mfrac = ((int32)mfrac) >> sc;
        oexp = expr;                    /* reg is larger exponent, save for final exponent add */
    }
    sim_debug(DEBUG_EXP, &cpu_dev,
        "ADFW3 after exp calc exp %04x sc %04x oexp %04x\n", exp, sc, oexp);
    frac = rfrac + mfrac;               /* add fractions */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "ADFW4 frac calc rfrac %06x mfrac %06x frac %04x\n", rfrac, mfrac, frac);
    if (frac == 0) {
        /* return the zero value */
        ret = frac;                     /* return zero to caller */
        goto goout;                     /* go set cc's and return */
    }
    if ((int32)frac >= 0x10000000) {    /* check for overflow */
        /* overflow */
        frac = ((int32)frac) >> 1;      /* sra 1 */
    } else {
        /* no overflow */
        /* check for underflow */
        if ((int32)frac >= 0xf0000000) {    /* underflow? */
            frac = ((int32)frac) << 3;  /* yes, sla 3 */
            oexp -= 0x01000000;         /* adjust exponent */
        } else {
            /* no underflow */
            frac = ((int32)frac) >> 1;  /* sra 1 */
        }
    }
    /* normalize the frac value and put exponent into exp */
    frac = s_nor(frac, (int32 *)&exp);
    if (frac == MSIGN) {                /* check for minus zero */
        frac = NORMASK;                 /* load correct value */
        exp += 1;                       /* adjust the exponent too */
    }
    /* see if the exponent is normalized */
    if (exp == 0x40) {                  /* is result normalized? */
        /* normalized, so round */
        if (frac < 0x7fffffc0)          /* check for special round */
            frac += 0x40;               /* round mantissa */
    } else {
        /* see if exponent and fraction are zero */
        if (exp == 0 && frac == 0) {
            ret = 0;                    /* return zero */
            goto goout;                 /* go set cc's and return */
        }
    }
    exp = (uint32)exp << 24;            /* put exponent in upper byte */
    exp -= 0x3f000000;                  /* adjust the shift count */
    /* rounding complete, compute final exponent */
    /* must check for exponent over/underflow */
    sign = (oexp & MSIGN) != 0;         /* get sign of largest exponent */
    sign |= ((exp & MSIGN) != 0) ? 2 : 0;   /* get sign of nor exp */
    exp = exp + oexp;                   /* compute final exponent */
    if (exp & MSIGN)
        /* we have exponent underflow if result is negative */
        goto ARUNFLO;

    /* if both signs are neg and result sign is positive, overflow */
    /* if both signs are pos and result sign is negative, overflow */
    if ((sign == 3 && (exp & MSIGN) == 0) ||
        (sign == 0 && (exp & MSIGN) != 0)) {
        /* we have exponent overflow from addition */
//AROVFLO:
        CC |= CC4BIT;                   /* set CC4 for exponent overflow */
ARUNFLO:
        /* we have exponent underflow from addition */
        CC |= CC1BIT;                   /* set CC1 for arithmetic exception */
        ret = frac;                     /* get return value */
        if ((frac & MSIGN) == 0) {
            CC |= CC2BIT;               /* set pos fraction bit CC2 */
        } else {
            CC |= CC3BIT;               /* set neg fraction bit CC3 */
        }
        *cc = CC;                       /* return CC's */
#ifdef NOTUSED
        /* return value is not valid, but return fixup value anyway */
        switch ((CC >> 27) & 3) {       /* rt justify CC3 & CC4 */
        case 0x0:
            return 0;                   /* pos underflow */
            break;
        case 0x1:
            return 0x7fffffff;          /* positive overflow */
            break;
        case 0x2:
            return 0;                   /* neg underflow */
            break;
        case 0x3:
            return 0x80000001;          /* negative overflow */
            break;
        }
#endif
        /* never here */
        goto goout2;                    /* go set cc's and return */
    }
    /* no over/underflow */
    frac = (int32)frac >> 7;            /* positive fraction sra r7,7 */
//#define FRMASK  0x80ffffff              /* fraction mask */
    frac &= FRMASK;                     /* mask out the exponent field */
    if ((int32)frac > 0) {              /* see if positive */
        ret = exp | frac;               /* combine exponent & fraction */
    } else {
        if (frac != 0) {
//#define EXMASK  0x7f000000              /* exponent mask */
            exp ^= EXMASK;              /* for neg fraction, complement exponent */
        }
        ret = exp | frac;               /* combine exponent & fraction */
    }
    /* come here to set cc's and return */
    /* ret has return value */
goout:
    if (ret & MSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (ret == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
goout2:
    sim_debug(DEBUG_EXP, &cpu_dev,
        "ADFW return mem %08x reg %08x result %08x CC %08x\n", mem, reg, ret, CC);
    /* return temp to destination reg */
    *cc = CC;                           /* return CC's */
    return ret;                         /* return result */
}
#endif

/* subtract memory floating point number from register floating point number */
uint32 s_sufw(uint32 reg, uint32 mem, uint32 *cc) {
    return s_adfw(reg, NEGATE32(mem), cc);
}

/* normalize the memory value when adding number to zero */
t_uint64 s_normfd(t_uint64 mem, uint32 *cc) {
    struct fpnum fpn = {0};
    t_uint64   ret;

    if (mem == 0) {                     /* make sure we have a number */
        *cc = CC4BIT;                   /* set the cc's */
        return 0;                       /* return zero */
    }
    sim_debug(DEBUG_EXP, &cpu_dev,
        "NORMFD entry mem %016llx\n", mem);
#ifndef OLDWAY
    fpn.msw = (mem >> 32);              /* get msw */
    fpn.lsw = mem & 0xffffffff;         /* get lsw */
    unpackd(&fpn);                      /* unpack number */
    packd(&fpn);                        /* pack it back up */
    ret = ((t_uint64)fpn.msw << 32) | fpn.lsw;  /* make 64 bit num */
#else
    fpn.sign = 0;

    /* special case 0x8000000000000000 (-0) to set CCs to 1011 */
    /* and value to 0x8000000000000001 */
//#define DMSIGN  0x8000000000000000LL  /* 64 bit minus sign */
    if (mem == 0x8000000000000000LL) {
        fpn.CCs = CC1BIT|CC3BIT|CC4BIT; /* we have AE, exp overflow, neg frac */
        ret = 0x8000000000000001LL;     /* return max neg value */
        goto goret;                     /* return */
    }

    /* special case pos exponent & zero mantissa to be 0 */
    if (((mem & 0x8000000000000000LL) == 0) && ((mem & 0xff00000000000000LL) > 0) && 
        (mem & 0x00ffffffffffffffLL) == 0)) {
        ret = 0;                        /* 0 to any power is still 0 */
        fpn.CCs = CC4BIT;               /* set zero CC */
        goto goret;                     /* return */
    }

    /* if we have 1xxx xxxx 0000 0000 0000 0000 0000 0000 */
    /* we need to convert to 1yyy yyyy 1111 0000 0000 0000 0000 0000 */
    /* where y = x - 1 */
    if ((mem & 0x80ffffffffffffffLL) == 0x8000000000000000LL) {
        t_uint64 nexp = (0x7f00000000000000LL & mem) - 0x0100000000000000LL;
        mem = 0x8000000000000000LL | (nexp & 0x7f00000000000000LL) | 0x00f0000000000000LL;
    }

//#define DEXMASK 0x7f00000000000000ll    /* exponent mask */
    fpn.exp = (mem & 0x7f00000000000000LL) >> 56; /* get exponent */
    if (mem & 0x8000000000000000LL) {   /* test for neg */
        fpn.sign = MEMNEG;              /* we are neq */
        mem = NEGATE32(mem);            /* two's complement */
        fpn.exp ^= 0x7f;                /* complement exponent */
    }
    fpn.num = mem & 0x00ffffffffffffffLL;   /* get mantissa */

    /* now make sure number is normalized */
    while ((fpn.num != 0) && ((fpn.num & 0x00f0000000000000LL) == 0)) {
        fpn.num <<= 4;                  /* move up a nibble */
        fpn.exp--;                      /* and decrease exponent */
    }
    if (fpn.exp < 0) {
        fpn.CCs = CC1BIT;               /* we have underflow */
        if (fpn.sign & MEMNEG)          /* we are neg (1) */
            fpn.CCs |= CC3BIT;          /* set neg CC */
        else
            fpn.CCs |= CC2BIT;          /* set pos CC */
        ret = 0;                        /* number too small, make 0 */
        goto goret;                     /* return */
    }

    /* rebuild normalized number */
    ret = ((fpn.num & 0x00ffffffffffffff) | (((t_uint64)fpn.exp & 0x7f) << 56));
    if (fpn.sign & MEMNEG)              /* we were neg (1) */
        ret = NEGATE32(ret);            /* two's complement */
    if (ret == 0)
        fpn.CCs = CC4BIT;               /* show zero */
    else if (ret & 0x8000000000000000LL)    /* neqative? */
        fpn.CCs = CC3BIT;               /* show negative */
    else
        fpn.CCs = CC2BIT;               /* show positive */
goret:
#endif
//  ret = ((t_uint64)fpn.msw << 32) | fpn.lsw;  /* make 64 bit num */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "NORMFD return mem %016llx result %016llx CC's %08x\n", mem, ret, fpn.CCs);
    /* return normalized number */
    *cc = fpn.CCs;                      /* set the cc's */
    return ret;                         /* return normalized result */
}

/* add memory floating point number to register floating point number */
/* do double precision add */
/* set CC1 if overflow/underflow */
t_uint64 s_adfd(t_uint64 reg, t_uint64 mem, uint32 *cc) {
#ifdef USE_ORIG
    t_uint64    retval;
    uint32      CC;
//  printf("s_adfd entry reg %016lx mem %016lx\n", reg, mem);

    /* do original SEL floating add */
    retval = o_adfd(reg, mem, &CC);     /* get 32 bit value */
//  printf("s_adfd exit result %016lx CC's %08x\n", retval, CC);
    *cc = CC;                           /* return the CC's */
    return retval;                      /* we are done */
#endif

#ifndef USE_ORIG
    u_int64_t   dst = reg, src = mem, retval;
    int32       type = (FPADD);         /* do double precision add */
    uint32      CC;

//  printf("s_adfd entry reg %016lx mem %016lx\n", reg, mem);

#ifdef USE_DIAG
    /* do diagnostic floating double add */
    retval = d_adsu(dst, src, &CC, type);
#endif

#ifdef USE_NEW
    /* do new floating add */
    retval = n_adfd(dst, src, &CC, type);
#endif

//  printf("s_adfd exit result %016lx CC's %08x\n", retval, CC);
    *cc = CC;                           /* return the CC's */
    return retval;                      /* we are done */
#endif /* USE_ORIG */
}

#ifdef USE_ORIG
/* add memory floating point number to register floating point number */
/* set CC1 if overflow/underflow */
t_uint64 o_adfd(t_uint64 reg, t_uint64 mem, uint32 *cc) {
    t_uint64 dfrac, dblmem, dblreg, ret;
    uint32 CC, sc, sign, expm, expr, exp;
#ifndef DO_NORMALIZE
    struct fpnum fpn = {0};
#endif

    *cc = 0;                            /* clear the CC'ss */
    CC = 0;                             /* clear local CC's */

    sim_debug(DEBUG_EXP, &cpu_dev,
        "ADFD entry mem %016llx reg %016llx\n", mem, reg);
    /* process the memory operand value */
    if (mem == 0) {                     /* test for zero operand */
        ret = reg;                      /* return original reg value */
#ifndef DO_NORMALIZE
        if (ret == 0)
            goto goout;                 /* nothing + nothing = nothing */
        /* reg is 0 and mem is not zero, normalize mem */
        fpn.msw = (reg >> 32) & 0xffffffff; /* get msw */
        fpn.lsw = reg & 0xffffffff;     /* get lsw */
        unpackd(&fpn);                  /* unpack number */
        packd(&fpn);                    /* pack it back up */
        ret = ((t_uint64)fpn.msw << 32) | fpn.lsw;  /* make 64 bit num */
        CC = fpn.CCs;                   /* set the cc's */
        goto goout2;
#else
        ret = s_normfd(reg, &CC);       /* normalize the reg */
        if (CC & CC1BIT)                /* if AE, just exit */
            goto goout2;
#endif
        goto goout;                     /* go set cc's and return */
    }
//? mem = s_normfd(mem, &CC);           /* normalize the mem */

    /* separate mem dw into two 32 bit numbers */
    /* mem value is not zero, so extract exponent and mantissa */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    expm = (uint32)((mem & DEXMASK) >> 32); /* extract exponent */
//#define DFRMASK 0x80ffffffffffffffll    /* fraction mask */
    dblmem = mem & DFRMASK;             /* extract fraction */
    if (dblmem & DMSIGN) {              /* test for negative fraction */
        /* negative fraction */
//#define EXMASK  0x7f000000              /* exponent mask */
        expm ^= EXMASK;                 /* ones complement the exponent */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
        dblmem |= DEXMASK;              /* adjust the fraction */
    }

    /* process the register operator value */
    if (reg == 0) {                     /* see if reg value is zero */
        ret = mem;                      /* return original mem operand value */
#ifndef DO_NORMALIZE
        if (ret == 0)
            goto goout;                 /* nothing + nothing = nothing */
        /* reg is 0 and mem is not zero, normalize mem */
        fpn.msw = (mem >> 32);          /* get msw */
        fpn.lsw = mem & 0xffffffff;     /* get lsw */
        unpackd(&fpn);                  /* unpack number */
        packd(&fpn);                    /* pack it back up */
        ret = ((t_uint64)fpn.msw << 32) | fpn.lsw;  /* make 64 bit num */
        CC = fpn.CCs;                   /* set the cc's */
        goto goout2;
#else
        ret = s_normfd(mem, &CC);       /* normalize the mem */
        if (CC & CC1BIT)                /* if AE, just exit */
            goto goout2;
#endif
        goto goout;                     /* go set cc's and return */
    }
//? reg = s_normfd(reg, &CC);           /* normalize the reg */
    CC = 0;                             /* clear local CC's */

    /* separate reg dw into two 32 bit numbers */
    /* reg value is not zero, so extract exponent and mantissa */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    expr = (uint32)((reg & DEXMASK) >> 32); /* extract exponent */
//#define DFRMASK 0x80ffffffffffffffll    /* fraction mask */
    dblreg = reg & DFRMASK;             /* extract fraction */
    if (dblreg & DMSIGN) {              /* test for negative fraction */
        /* negative fraction */
//#define EXMASK  0x7f000000              /* exponent mask */
        expr ^= EXMASK;                 /* ones complement the exponent */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
        dblreg |= DEXMASK;              /* adjust the fraction */
    }

    exp = expr - expm;                  /* subtract memory exp from reg exp */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "ADFD2 exp calc expr %04x expm %04x exp %04x\n", expr, expm, exp);
	sign = expr;						/* save register exponent */
    if (exp & MSIGN) {
        /* exponent difference is negative */
        exp = NEGATE32(exp);            /* make exponent difference positive */
        if (exp > 0x0d000000) {
            /* shift count is > 13, so return operand and set cc's */
            ret = mem;                  /* return the original mem operand */
            goto goout;                 /* go set cc's and return */
        }
        /* difference is <= 13, so adjust to smaller value for add */
        /* difference is number of 4 bit right shifts to do */
        /* (exp >> 24) * 4 */
        sc = (uint32)exp >> (24 - 2);   /* shift count down to do x4 the count */
        dblreg = (t_int64)dblreg >> sc; /* shift sra r4,cnt x4 */
        sign = expm;                    /* mem is larger exponent, so save it */
    }
    else
    {
        /* exponent difference is zero or positive */
        if (exp > 0x0d000000) {
            /* difference is > 13 */
            /* operand is small, so return reg value as result */
            ret = reg;                  /* get original reg value and return */
            goto goout;                 /* go set cc's and return */
        }
        /* diff is <= 13, normalize */
        /* (exp >> 24) * 4 */
        sc = (uint32)exp >> (24 - 2);   /* shift count down to do x4 the count */
        dblmem = (t_int64)dblmem >> sc; /* shift sra r6,cnt x4 */
        sign = expr;                    /* reg is larger exponent, so save it */
    }
    sim_debug(DEBUG_EXP, &cpu_dev,
        "ADFD3 after exp calc exp %04x sc %04x sign %04x\n", exp, sc, sign);
    dfrac = dblreg + dblmem;            /* add operand to operator (fractions) */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "ADFD4 frac calc dbkreg %014llx dblmem %014llx dfrac %014llx\n",
        dblreg, dblmem, dfrac);
    if (dfrac == 0) {
        /* return the zero value */
        ret = dfrac;                    /* return zero to caller */
        goto goout;                     /* go set cc's and return */
    }
    exp = (int32)sign - 0x3f000000;      /* adjust exponent */
    dfrac = (t_int64)dfrac << 3;         /* adjust the mantissa sla 3 */

    /* normalize the value in dfrac and put exponent into sc */
    dfrac = s_nord(dfrac, &sc);
    if (dfrac == DMSIGN) {
        /* value is neg zero, so fix it up */
        dfrac = DNORMASK;               /* correct the value */
        sc++;                           /* adjust exponent too */
    }
    sc = (sc & 0xff) << 24;             /* put nord exp in upper byte */
    sign = (exp & MSIGN) != 0;
    sign |= ((sc & MSIGN) != 0) ? 2 : 0;
    exp += sc;                          /* compute final exponent */
    /* if both signs are neg and result sign is positive, overflow */
    /* if both signs are pos and result sign is negative, overflow */
    if ((sign == 3 && (exp & MSIGN) == 0) ||
        (sign == 0 && (exp & MSIGN) != 0)) {
            /* we have exponent overflow from addition */
            goto DOVFLO;
    }
    if (exp & MSIGN)
        /* We have exponent underflow if result negative */
        goto DUNFLO;                    /* branch on underflow. */

    ret = (t_int64)dfrac >> 7;          /* position fraction srad 7 */
//#define DFRMASK 0x80ffffffffffffffll    /* fraction mask */
    ret &= DFRMASK;                     /* mask out exponent field leaving fraction */
    /* test sign of fraction */
    if (ret != 0) {                     /* test for zero, to return zero */
        if (ret & DMSIGN)               /* see if negative */
            /* fraction is negative */
            exp ^= EXMASK;              /* neg fraction, so complement exponent */
        ret = ret | ((t_uint64)exp << 32);  /* position and insert exponent */
    }

    /* come here to set cc's and return */
    /* dfrac has return value */
goout:
    if (ret & DMSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else
    if (ret == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
goout2:
    sim_debug(DEBUG_EXP, &cpu_dev,
        "ADFD return mem %016llx reg %016llx result %016llx CC %08x\n", mem, reg, ret, CC);
    /* return dfrac to destination reg */
#ifdef NOT_VALID
    /* return value is not valid, but return fixup value anyway */
    switch ((CC >> 27) & 3) {           /* rt justify CC3 & CC4 */
    case 0x0:
        ret = 0;                        /* pos underflow */
        break;
    case 0x1:
        ret = 0x7fffffffffffffffLL;     /* positive overflow */
        break;
    case 0x2:
        ret = 0;                        /* neg underflow */
        break;
    case 0x3:
        ret = 0x8000000000000001;       /* negative overflow */
        break;
    }
#endif
    *cc = CC;                           /* return CC's */
    return ret;                         /* return result */

DOVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
DUNFLO:
    ret = dfrac;                        /* get return value */
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (dfrac & DMSIGN) {
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    } else {
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    }
    goto goout2;                        /* go set cc's and return */
}
#endif

/* subtract memory floating point number from register floating point number */
t_uint64 s_sufd(t_uint64 reg, t_uint64 mem, uint32 *cc) {
    return s_adfd(reg, NEGATE32(mem), cc);
}

/* convert from 32 bit float to 32 bit integer */
/* set CC1 if overflow/underflow exception */
uint32 s_fixw(uint32 fltv, uint32 *cc) {
    uint32 CC = 0, temp, temp2, sc;
    uint32 neg = 0;                     /* clear neg flag */

    if (fltv & MSIGN) {                 /* check for negative */
        fltv = NEGATE32(fltv);          /* make src positive */
        neg = 1;                        /* set neg val flag */
    } else {
        if (fltv == 0) {
            temp = 0;                   /* return zero */
            goto setcc;                 /* go set CC's */
        }
        /* gt 0, fall through */
    }
    temp2 = (fltv >> 24);               /* get exponent */
    fltv <<= 8;                         /* move src to upper 3 bytes */
    temp2 -= 64;                        /* take off excess notation */
    if ((int32)temp2 <= 0) {
        /* set CC1 for underflow */
        temp = 0;                       /* assume zero for small values */
        goto UNFLO;                     /* go set CC's */
    }
    temp2 -= 8;                         /* see if in range */
    if ((int32)temp2 > 0) {
        /* set CC1 for overflow */
        temp = 0x7fffffff;              /* too big, set to max value */
        goto OVFLO;                     /* go set CC's */
    }
    sc = (NEGATE32(temp2) * 4);         /* pos shift cnt * 4 */
    fltv >>= sc;                        /* do 4 bit shifts */
    /* see if overflow to neg */
    /* set CC1 for overflow */
    if (fltv & MSIGN) {
        /* set CC1 for overflow */
        temp = 0x7fffffff;              /* too big, set max */
        goto OVFLO;                     /* go set CC's */
    }
    /* see if original value was negative */
    if (neg)
        fltv = NEGATE32(fltv);          /* put back to negative */
    temp = fltv;                        /* return integer value */
    /* come here to set cc's and return */
    /* temp has return value */
setcc:
    if (temp & MSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (temp == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    /* return temp for destination reg */
    *cc = CC;                           /* return CC's */
    return temp;                        /* return result */

    /* handle underflow/overflow */
OVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
UNFLO:
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (neg)                            /* test for negative */
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    else
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    *cc = CC;                           /* return CC's */
    return temp;                        /* return result */
}

/* convert from 32 bit integer to 32 bit float */
/* No overflow (CC1) can be generated */
uint32 s_fltw(uint32 intv, uint32 *cc) {
    uint32 CC = 0, temp;
    uint32 neg = 0;                     /* zero sign flag */
    uint32 sc = 0;                      /* zero shift count */

    if (intv & MSIGN) {
        intv = NEGATE32(intv);          /* make src positive */
        neg = 1;                        /* set neg flag */
    } else {
        if (intv == 0) {                /* see if zero */
            temp = 0;                   /* return zero */
            goto setcc;                 /* set cc's and exit */
        }
        /* gt 0, fall through */
    }
    temp = intv;
    while ((temp & FSIGN) == 0) {       /* shift the val until bit 0 is set */
        temp <<= 1;                     /* shift left 1 bit */
        sc++;                           /* incr shift count */
    }
    if (sc & 0x2)                       /* see if shift count mod 2 */
        temp >>= 2;                     /* two more zeros */
    if (sc & 0x1)                       /* see if shift count odd */
        temp >>= 1;                     /* one more zeros */
    sc >>= 2;                           /* normalize radix */
    sc -= 72;                           /* make excess 64 notation */
    sc = NEGATE32(sc);                  /* make positive */
    temp = (temp >> 8) | (sc << 24);    /* merge exp with fraction */
    if (neg)                            /* was input negative */
        temp = NEGATE32(temp);          /* make neg again */
    /* come here to set cc's and return */
    /* temp has return value */
setcc:
    if (temp & MSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (temp == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    /* return temp for destination reg */
    *cc = CC;                           /* return CC's */
    return temp;                        /* return result */
}

/* convert from 64 bit double to 64 bit integer */
/* set CC1 if overflow/underflow exception */
t_uint64 s_fixd(t_uint64 dblv, uint32 *cc) {
//  uint32 temp2, CC = 0, neg = 0, sc = 0;
    int32 temp2, CC = 0, neg = 0, sc = 0;
    t_uint64 dest;

    /* neg and CC flags already set to zero */
    if ((t_int64)dblv < 0) {
        dblv = NEGATE32(dblv);          /* make src positive */
        neg = 1;                        /* set neg val flag */
    } else {
        if (dblv == 0) {
            dest = 0;                   /* return zero */
            goto dodblcc;               /* go set CC's */
        }
        /* gt 0, fall through */
    }
    temp2 = (uint32)(dblv >> 56);       /* get exponent */
    dblv <<= 8;                         /* move fraction to upper 7 bytes */
    temp2 -= 64;                        /* take off excess notation */
    if ((int32)temp2 <= 0) {
        /* set CC1 for underflow */
        dest = 0;                       /* assume zero for small values */
        goto DUNFLO;                    /* go set CC's */
    }
    temp2 -= 16;                        /* see if in range */
    if ((int32)temp2 > 0) {
        /* set CC1 for overflow */
        dest = 0x7fffffffffffffffll;    /* too big, set max */
        goto DOVFLO;                    /* go set CC's */
    }
    sc = (NEGATE32(temp2) * 4);         /* pos shift cnt * 4 */
    dblv >>= sc;                        /* do 4 bit shifts */
    /* see if overflow to neg */
    /* FIXME set CC1 for overflow? */
    if (dblv & DMSIGN) {
        /* set CC1 for overflow */
        dest = 0x7fffffffffffffffll;    /* too big, set max */
        goto DOVFLO;                    /* go set CC's */
    }
    /* see if original values was negative */
    if (neg)
        dblv = NEGATE32(dblv);          /* put back to negative */
    dest = dblv;                        /* return integer value */
dodblcc:
    /* dest has return value */
    if (dest & DMSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (dest == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    *cc = CC;                           /* return CC's */
    return dest;                        /* return result */

    /* handle underflow/overflow */
DOVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
DUNFLO:
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (neg)                            /* test for negative */
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    else
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    *cc = CC;                           /* return CC's */
    return dest;                        /* return result */
}

/* convert from 64 bit integer to 64 bit double */
/* No overflow (CC1) can be generated */
t_uint64 s_fltd(t_uint64 intv, uint32 *cc) {
    t_uint64 temp, sc = 0;              /* zero shift count */
    uint32 neg = 0;                     /* zero sign flag */
    uint32 CC = 0;                      /* n0 CC's yet */

    if (intv & DMSIGN) {
        intv = NEGATE32(intv);          /* make src positive */
        neg = 1;                        /* set neg flag */
    } else {
        if (intv == 0) {                /* see if zero */
            temp = 0;                   /* return zero */
            goto setcc;                 /* set cc's and exit */
        }
        /* gt 0, fall through */
    }
    temp = intv;                        /* get input t_uint64 */
    if ((temp & 0xff00000000000000ll) != 0) {
        temp >>= 8;                     /* very large, make room for exponent */
        sc = -2;                        /* set exp count to 2 nibbles */
    }
    while ((temp & 0x00f0000000000000ll) == 0) {    /* see if normalized */
        temp <<= 4;                     /* zero, shift in next nibble */
        sc++;                           /* incr shift count */
    }
    sc = (NEGATE32(sc) + 78);           /* normalized, make into excess 64 */
    temp = (sc << 56) | temp;           /* merge exponent into fraction */
    if (neg)                            /* was input negative */
        temp = NEGATE32(temp);          /* make neg again */
    /* come here to set cc's and return */
    /* temp has return dbl value */
setcc:
    if (temp & DMSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (temp == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    /* return temp for destination regs */
    *cc = CC;                           /* return CC's */
    return temp;                        /* return result */
}

/* multiply register float by memory float, return float */
uint32 s_mpfw(uint32 reg, uint32 mem, uint32 *cc) {
    uint32 CC = 0, temp, temp2, sign;
    uint32 expm, expr;
    t_uint64 dtemp;

    /* process operator */
    sign = mem & MSIGN;                 /* save original value for sign */
    if (mem == 0) {
        temp = 0;                       /* return zero */
        goto setcc;                     /* go set CC's */
    }

    if (mem & MSIGN)                    /* check for negative */
        mem = NEGATE32(mem);            /* make mem positive */

    expm = (mem >> 24);                 /* get operator exponent */
    mem <<= 8;                          /* move fraction to upper 3 bytes */
    mem >>= 1;                          /* adjust fraction */

    /* process operand */
    if (reg == 0) {
        temp = 0;                       /* return zero */
        goto setcc;                     /* go set CC's */
    }
    if (reg & MSIGN) {                  /* check for negative */
        reg = NEGATE32(reg);            /* make reg positive */
        sign ^= MSIGN;                  /* adjust sign */
    }
    expr = (reg >> 24);                 /* get operand exponent */
    reg <<= 8;                          /* move fraction to upper 3 bytes */
    reg >>= 1;                          /* adjust fraction */

    temp = expm + expr;                 /* add exponents */
    dtemp = (t_uint64)mem * (t_uint64)reg;  /* multiply fractions */
    dtemp <<= 1;                        /* adjust fraction */

    if (sign & MSIGN)
        dtemp = NEGATE32(dtemp);        /* if negative, negate fraction */

    /* normalize the value in dtemp and put exponent into expr */
    dtemp = s_nord(dtemp, &expr);       /* normalize fraction */
    temp -= 0x80;                       /* resize exponent */

//RROUND:
    /* temp2 has normalized fraction */
    /* expr has exponent from normalization */
    /* temp has exponent from divide */
    /* sign has final sign of result */
    temp2 = (uint32)(dtemp >> 32);      /* get upper 32 bits */
    if ((int32)temp2 >= 0x7fffffc0)     /* check for special rounding */
        goto RRND2;                     /* no special handling */

    if (temp2 == MSIGN) {               /* check for minux zero */
        temp2 = 0xF8000000;             /* yes, fixup value */
        expr++;                         /* bump exponent */
    }
    if (expr != 0x40) {                 /* result normalized? */
        goto RRND2;                     /* if not, don't round */
    }
    /* result normalized */
    if ((sign & MSIGN) == 0)
        goto RRND1;                     /* if sign not set, don't round yet */
    expr += temp;                       /* add exponent */

    if (expr & MSIGN)                   /* test for underflow */
        goto DUNFLO;                    /* go process underflow */

    if ((int32)expr > 0x7f)             /* test for overflow */
        goto DOVFLO;                    /* go process overflow */

    expr ^= FMASK;                      /* complement exponent */
    temp2 += 0x40;                      /* round at bit 25 */
    goto RRND3;                         /* go merge code */

RRND1:
    temp2 += 0x40;                      /* round at bit 25 */
RRND2:
    expr += temp;                       /* add exponent */

    if (expr & MSIGN)                   /* test for underflow */
        goto DUNFLO;                    /* go process underflow */

    if ((int32)expr > 0x7f)             /* test for overflow */
        goto DOVFLO;                    /* go process overflow */

    if (sign & MSIGN)                   /* test for negative */
        expr ^= FMASK;                  /* yes, complement exponent */
RRND3:
    temp2 <<= 1;                        /* adjust fraction */
    temp = (expr << 24) | (temp2 >> 8); /* merge exp & fraction */
    goto setcc;                         /* go set CC's */

DOVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
DUNFLO:
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (sign & MSIGN)                   /* test for negative */
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    else
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    *cc = CC;                           /* return CC's */
    /* return value is not valid, but return fixup value anyway */
    switch ((CC >> 27) & 3) {           /* rt justify CC3 & CC4 */
    case 0x0:
        return 0;                       /* pos underflow */
        break;
    case 0x1:
        return 0x7fffffff;              /* positive overflow */
        break;
    case 0x2:
        return 0;                       /* neg underflow */
        break;
    case 0x3:
        return 0x80000001;              /* negative overflow */
        break;
    }
setcc:
    /* come here to set cc's and return */
    /* temp has return value */
    if (temp & MSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (temp == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    /* return temp to destination reg */
    *cc = CC;                           /* return CC's */
    return temp;                        /* return result */
}

/* divide register float by memory float */
uint32 s_dvfw(uint32 reg, uint32 mem, uint32 *cc) {
    uint32 CC = 0, temp, temp2, sign;
    uint32 expm, expr;
    t_uint64 dtemp;

    /* process operator */
    sign = mem & MSIGN;                 /* save original value for sign */
    if (mem == 0)                       /* check for divide by zero */
        goto DOVFLO;                    /* go process divide overflow */

    if (mem & MSIGN)                    /* check for negative */
        mem = NEGATE32(mem);            /* make mem positive */

    expm = (mem >> 24);                 /* get operand exponent */
    mem <<= 8;                          /* move fraction to upper 3 bytes */
    mem >>= 1;                          /* adjust fraction for divide */

    /* process operand */
    if (reg == 0) {
        temp = 0;                       /* return zero */
        goto setcc;                     /* go set CC's */
    }
    if (reg & MSIGN) {                  /* check for negative */
        reg = NEGATE32(reg);            /* make reg positive */
        sign ^= MSIGN;                  /* complement sign */
    }
    expr = (reg >> 24);                 /* get operator exponent */
    reg <<= 8;                          /* move fraction to upper 3 bytes */
    reg >>= 6;                          /* adjust fraction for divide */

    temp = expr - expm;                 /* subtract exponents */
//BAD here temp = NEGATE32(temp);       /* make reg positive */
    dtemp = ((t_uint64)reg) << 32;      /* put reg fraction in upper 32 bits */
    temp2 = (uint32)(dtemp / mem);      /* divide reg fraction by mem fraction */
    temp2 >>= 3;                        /* shift out excess bits */
    temp2 <<= 3;                        /* replace with zero bits */

    if (sign & MSIGN)
        temp2 = NEGATE32(temp2);        /* if negative, negate fraction */
    /* normalize the result in temp and put exponent into expr */
    temp2 = s_nor(temp2, &expr);        /* normalize fraction */
    temp += 1;                          /* adjust exponent */

//RROUND:
    if ((int32)temp2 >= 0x7fffffc0)     /* check for special rounding */
        goto RRND2;                     /* no special handling */

    if (temp2 == MSIGN) {               /* check for minus zero */
        temp2 = 0xF8000000;             /* yes, fixup value */
        expr++;                         /* bump exponent */
    }

    if (expr != 0x40) {                 /* result normalized? */
        goto RRND2;                     /* if not, don't round */
    }
    /* result normalized */
    if ((sign & MSIGN) == 0)
        goto RRND1;                     /* if sign set, don't round yet */
    expr += temp;                       /* add exponent */

    if (expr & MSIGN)                   /* test for underflow */
        goto DUNFLO;                    /* go process underflow */

    if ((int32)expr > 0x7f)             /* test for overflow */
        goto DOVFLO;                    /* go process overflow */

    expr ^= FMASK;                      /* complement exponent */
    temp2 += 0x40;                      /* round at bit 25 */
    goto RRND3;                         /* go merge code */

RRND1:
    temp2 += 0x40;                      /* round at bit 25 */
RRND2:
    expr += temp;                       /* add exponent */

    if (expr & MSIGN)                   /* test for underflow */
        goto DUNFLO;                    /* go process underflow */

    if ((int32)expr > 0x7f)             /* test for overflow */
        goto DOVFLO;                    /* go process overflow */

    if (sign & MSIGN)                   /* test for negative */
        expr ^= FMASK;                  /* yes, complement exponent */
RRND3:
    temp2 <<= 1;                        /* adjust fraction */
    temp = (expr << 24) | (temp2 >> 8); /* merge exp & fraction */
    goto setcc;                         /* go set CC's */

DOVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
DUNFLO:
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (sign & MSIGN)                   /* test for negative */
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    else
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    *cc = CC;                           /* return CC's */
    /* return value is not valid, but return fixup value anyway */
    switch ((CC >> 27) & 3) {           /* rt justify CC3 & CC4 */
    case 0:
        return 0;                       /* pos underflow */
        break;
    case 1:
        return 0x7fffffff;              /* positive overflow */
        break;
    case 2:
        return 0;                       /* neg underflow */
        break;
    case 3:
        return 0x80000001;              /* negative overflow */
        break;
    }
setcc:
    /* come here to set cc's and return */
    /* temp has return value */
    if (temp & MSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else
    if (temp == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    /* return temp to destination reg */
    *cc = CC;                           /* return CC's */
    return temp;                        /* return result */
}

/* multiply register double by memory double */
t_uint64 s_mpfd(t_uint64 reg, t_uint64 mem, uint32 *cc) {
    t_uint64 tr1, tr2, tl1, tl2, dblreg;
    uint32 CC = 0, temp = 0, temp2, sign = 0;
    uint32 expm, expr;
    t_uint64 dtemp1, dtemp2;

    /* process operator */
    if (mem & DMSIGN) {                 /* check for negative */
        mem = NEGATE32(mem);            /* make mem positive */
        sign = 1;                       /* save original value for sign */
    } else {
        if (mem == 0) {                 /* check for zero */
            dblreg = 0;                 /* we have zero operator */
            goto setcc;                 /* go set CC's */
        }
        /* gt 0, fall through */
    }
    /* operator is positive here */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    dblreg = mem & DEXMASK;             /* extract exponent */
    mem ^= dblreg;                      /* zero exponent to make fraction */
    expm = (uint32)(dblreg >> 32);      /* get operator exponent as 32 bit value */
    expm -= 0x40000000;                 /* adjust exponent bias */
    mem <<= 7;                          /* adjust fraction position */

    /* process operand */
    if (reg & DMSIGN) {                 /* check for negative */
        sign ^= 1;                      /* adjust sign */
        reg = NEGATE32(reg);            /* make reg positive */
    } else {
        if (reg == 0) {
            dblreg = 0;                 /* return zero */
            goto setcc;                 /* go set CC's */
        }
        /* gt 0, fall through */
    }
    /* operand is positive here */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    dblreg = reg & DEXMASK;             /* extract exponent */
    reg ^= dblreg;                      /* zero exponent to make fraction */
    expr = (uint32)(dblreg >> 32);      /* get operand exponent as 32 bit value */
    expr -= 0x40000000;                 /* adjust exponent bias */
    reg <<= 7;                          /* adjust fraction position */

    temp = expr + expm;                 /* add exponents */

    tl1 = (mem >> 32) & D32RMASK;       /* get left half of operator */
    tr1 = mem & D32RMASK;               /* get right half of operator */
    tl2 = (reg >> 32) & D32RMASK;       /* get left half of operand */
    tr2 = reg & D32RMASK;               /* get right half of operand */

    dtemp2 = tl1 * (tr2 >> 1);          /* operator left half * operand right half */
    dtemp2 <<= 1;                       /* readjust result */
    dtemp1 = tl2 * (tr1 >> 1);          /* operand left half * operator right half */
    dtemp1 <<= 1;                       /* readjust result */
    dblreg = dtemp2 >> 32;              /* save partial product */
    dtemp2 = tl2 * tl1;                 /* operand left half * operator left half */
    dtemp2 += dblreg;                   /* add partial product */
    dblreg = dtemp1 >> 32;              /* save partial product */
    dtemp2 += dblreg;                   /* add other partial product */
    dblreg = (t_int64)dtemp2 << 1;      /* position for normalize */
    if (sign)                           /* see if negative */
        dblreg = NEGATE32(dblreg);      /* make negative */
    /* normalize the value in dblreg and put exponent into expr */
    dblreg = s_nord(dblreg, &expr);     /* normalize fraction */
    if (expr != 0x40)                   /* is result normalized */
        dblreg &= 0xfffffffffffff87fll; /* no, adjust value */
    if (dblreg == DMSIGN) {             /* check for neg zero */
        dblreg = DNORMASK;              /* correct the value */
        expr++;                         /* adjust exponent too */
    }
    expr <<= 24;                        /* reposition normalization count */
    temp2 = (expr & MSIGN) != 0;        /* collect signs */
    temp2 |= ((temp & MSIGN) != 0) ? 2 : 0;
    temp += expr;                       /* compute final exponent */
    /* if both signs are neg and result sign is positive, overflow */
    /* if both signs are pos and result sign is negative, overflow */
    if (((temp2 == 3) && ((temp & MSIGN) == 0)) ||
        ((temp2 == 0) && ((temp & MSIGN) != 0))) {
        /* we have exponent overflow from addition */
        goto DOVFLO;                    /* process overflow */
    }
    if (temp & MSIGN)                   /* see if negative */
        /* We have exponent underflow if result negative */
        goto DUNFLO;                    /* process underflow. */
    dtemp2 = ((t_uint64)temp) << 32;    /* move exp into upper 32 bits */
    dblreg = ((t_int64)dblreg) >> 7;    /* adjust fraction */
//#define DFRMASK 0x80ffffffffffffffll    /* fraction mask */
    dblreg &= DFRMASK;                  /* mask out exponent field */
    if (dblreg != 0) {                  /* see if 0, if so return 0 */
        if (dblreg & DMSIGN)            /* see if negative */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
            dtemp2 ^= DEXMASK;          /* negative fraction, complement exp */
        dblreg |= dtemp2;               /* combine exponent and fraction */
    }
    goto setcc;                         /* go set CC's & exit */

DOVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
DUNFLO:
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (sign & MSIGN)                   /* test for negative */
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    else
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    *cc = CC;                           /* return CC's */
    /* return value is not valid, but return fixup value anyway */
    switch ((CC >> 27) & 3) {           /* rt justify CC3 & CC4 */
    case 0:
        return 0;                       /* pos underflow */
        break;
    case 1:
        return 0x7fffffffffffffffll;    /* positive overflow */
        break;
    case 2:
        return 0;                       /* neg underflow */
        break;
    case 3:
        return 0x8000000000000001ll;    /* negative overflow */
        break;
    }
setcc:
    /* come here to set cc's and return */
    /* temp has return value */
    if (dblreg & DMSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (temp == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    *cc = CC;                           /* return CC's */
    return dblreg;                      /* return result */
}

/* divide register double by memory double */
t_uint64 s_dvfd(t_uint64 reg, t_uint64 mem, uint32 *cc) {
    t_uint64 tr1, /*tr2,*/ tl1, /*tl2,*/ dblreg;
    uint32 CC = 0, temp, temp2, sign = 0;
    uint32 expm, expr;
    t_uint64 dtemp1, dtemp2;

    /* process operator */
    if (mem & DMSIGN) {                 /* check for negative */
        mem = NEGATE32(mem);            /* make mem positive */
        sign = 1;                       /* save original value for sign */
    } else {
        if (mem == 0) {                 /* check for divide by zero */
            goto DOVFLO;                /* go process overflow */
        }
        /* gt 0, fall through */
    }
    /* operator is positive here */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    dblreg = mem & DEXMASK;             /* extract exponent */
    mem ^= dblreg;                      /* zero exponent to make fraction */
    expm = (uint32)(dblreg >> 32);      /* get operator exponent as 32 bit value */
    mem <<= 7;                          /* adjust fraction position */
    dtemp1 = mem & D32RMASK;            /* get lower 32 bits */
    dtemp1 >>= 1;                       /* shift the rightmost 32 bits right 1 bit */
    mem = (mem & D32LMASK) | dtemp1;    /* insert shifted value back into mem */

    /* process operand */
    if (reg & DMSIGN) {                 /* check for negative */
        sign ^= 1;                      /* adjust sign */
        reg = NEGATE32(reg);            /* make reg positive */
    } else {
        if (reg == 0) {
            dblreg = 0;                 /* return zero */
            goto setcc;                 /* go set CC's */
        }
        /* gt 0, fall through */
    }
    /* operand is positive here */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    dblreg = reg & DEXMASK;             /* extract exponent */
    reg ^= dblreg;                      /* zero exponent to make fraction */
    expr = (uint32)(dblreg >> 32);      /* get operand exponent as 32 bit value */

    temp = expr - expm;                 /* subtract exponents */
    temp2 = (0x02000000 & MSIGN) != 0;  /* collect signs */
    temp2 |= ((temp & MSIGN) != 0) ? 2 : 0;
    temp += 0x02000000;                 /* adjust exponent (abr bit 6)*/
    /* if both signs are neg and result sign is positive, overflow */
    /* if both signs are pos and result sign is negative, overflow */
    if (((temp2 == 3) && ((temp & MSIGN) == 0)) ||
        ((temp2 == 0) && ((temp & MSIGN) != 0))) {
        /* we have exponent overflow from addition */
        goto DOVFLO;                    /* process overflow */
    }
    reg = ((t_int64)reg) >> 1;          /* adjust fraction position */

    tl1 = (mem >> 32) & D32RMASK;       /* get left half of operator */
    tr1 = mem & D32RMASK;               /* get right half of operator */
//  tl2 = (reg >> 32) & D32RMASK;       /* get left half of operand */
//  tr2 = reg & D32RMASK;               /* get right half of operand */

    dtemp2 = reg / tl1;                 /* operand / left half of operator */
    dtemp2 = (dtemp2 & D32RMASK) << 32; /* move quotient to upper 32 bits */
    dtemp1 = reg % tl1;                 /* get remainder */
    dtemp1 = (dtemp1 & D32RMASK) << 32; /* move remainder to upper 32 bits */
    dtemp1 >>= 1;                       /* shift down by 1 */
    dtemp1 &= D32LMASK;                 /* make sure lower 32 bits are zero */

    dtemp1 = dtemp1 / tl1;              /* remainder / left half of operator */
    dtemp1 <<= 1;                       /* shift result back by 1 */
    dtemp1 &= D32RMASK;                 /* just lower 32 bits */
    dblreg = dtemp2 + dtemp1;           /* sum of quotients */
    dtemp2 = dblreg >> 32;              /* get upper 32 bits of sum */
    dblreg = ((t_int64)dblreg) >> 1;    /* shift down sum */
    dtemp1 = tr1 * dtemp2;              /* right half operator * (l opd/l opr) */
    dtemp1 = ((t_int64)dtemp1) >> 3;    /* adjust sub total */
    dtemp1 = dtemp1 / tl1;              /* r orp*(l opd/l opr)/l opr */
    dtemp1 = ((t_int64)dtemp1) << 3;    /* adjust sub total */
    dblreg -= dtemp1;                   /* subtract from quotient */
    /* changing this mask by 2 bits gives mostly same result as real V6 */
    /* changed 04/20/20 */
//  dblreg &= 0xffffffffffffffe0;       /* fixup quotient */
    dblreg &= 0xfffffffffffffff8;       /* fixup quotient */
    /* exp in temp */
    if (sign)                           /* neg input */
        dblreg = NEGATE32(dblreg);      /* yes, negate result */
    /* normalize the value in dblreg and put exponent into expr */
    dblreg = s_nord(dblreg, &expr);     /* normalize fraction */
    if (dblreg == DMSIGN) {             /* check for neg zero */
        dblreg = DNORMASK;              /* correct the value */
        expr++;                         /* adjust exponent too */
    }
    expr <<= 24;                        /* reposition normalization count */
    temp2 = (expr & MSIGN) != 0;        /* collect signs */
    temp2 |= ((temp & MSIGN) != 0) ? 2 : 0;
    temp += expr;                       /* compute final exponent */
    /* if both signs are neg and result sign is positive, overflow */
    /* if both signs are pos and result sign is negative, overflow */
    if ((temp2 == 3 && (temp & MSIGN) == 0) ||
        (temp2 == 0 && (expr & MSIGN) != 0)) {
        /* we have exponent overflow from addition */
        goto DOVFLO;                    /* process overflow */
    }
    if (temp & MSIGN)                   /* see if negative */
        /* We have exponent underflow if result is negative */
        goto DUNFLO;                    /* process underflow. */
    dtemp2 = ((t_uint64)temp) << 32;    /* move exp into upper 32 bits */
    dblreg = ((t_int64)dblreg) >> 7;    /* adjust fraction */
//#define DFRMASK 0x80ffffffffffffffll    /* fraction mask */
    dblreg &= DFRMASK;                  /* mask out exponent field */
    if (dblreg != 0) {                  /* see if 0, if so return 0 */
        if (dblreg & DMSIGN)            /* see if negative */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
            dtemp2 ^= DEXMASK;          /* negative fraction, complement exp */
        dblreg |= dtemp2;               /* combine exponent and fraction */
    }
    goto setcc;                         /* go set CC's & exit */

DOVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
DUNFLO:
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (sign & MSIGN)                   /* test for negative */
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    else
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    *cc = CC;                           /* return CC's */
    /* return value is not valid, but return fixup value anyway */
    /* Why not use an Array here? RPC */
#if 0
    static retval[4] = { 0, MSIGN-1, 0, MSIGN}; /* At top of function */
    return retval[((CC >> 27) & 3];
#endif
    switch ((CC >> 27) & 3) {           /* rt justify CC3 & CC4 */
    case 0:
        return 0;                       /* pos underflow */
        break;
    case 1:
        return 0x7fffffffffffffffll;    /* positive overflow */
        break;
    case 2:
        return 0;                       /* neg underflow */
        break;
    case 3:
        return 0x8000000000000001ll;    /* negative overflow */
        break;
    }
setcc:
    /* come here to set cc's and return */
    /* temp has return value */
    if (dblreg & DMSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (dblreg == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    *cc = CC;                           /* return CC's */
    return dblreg;                      /* return result */
}

//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
#define DMMASK  0x00ffffffffffffffLL    /* double mantissa mask */
#define DCMASK  0x1000000000000000LL    /* double carry mask */
#define DIBMASK 0x0fffffffffffffffLL    /* double fp nibble mask */
#define DUMASK  0x0ffffffffffffff0LL    /* double fp mask */
#define DNMASK  0x0f00000000000000LL    /* double nibble mask */
#define DZMASK  0x00f0000000000000LL    /* shifted nibble mask */

#ifdef USE_NEW
/* add memory floating point number to register floating point number */
/* set CC1 if overflow/underflow */
/* use revised normalization code */
t_uint64 n_adfd(t_uint64 reg, t_uint64 mem, uint32 *cc, uint32 type)
{
    u_int64_t   res;
    uint8       sign = 0;
    int         er, em, temp;
    uint32      CC = 0;

    *cc = 0;                            /* clear the CC'ss */

    sim_debug(DEBUG_EXP, &cpu_dev,
        "ADFD entry mem %016lx reg %016lx\n", mem, reg);
    /* process the memory operand value */
    /* extract exponent and mantissa */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    em = (mem & DEXMASK) >> 56;         /* extract mem exponent */
    if (mem & DMSIGN) {                 /* mem negative */
        sign |= MEMNEG;                 /* set neg flag (1) */
        mem = NEGATE32(mem);            /* negate number */
        em ^= 0x7f;                     /* complement exp */
    }
//#define DMMASK  0x00ffffffffffffffLL    /* double mantissa mask */
    mem &= DMMASK;                      /* get mem mantissa */

    /* normalize the memory mantissa */
    if (mem != 0) {                     /* check for zero value */
//#define DNMASK  0x0f00000000000000LL  /* double nibble mask */
        while ((mem != 0) && (mem & DNMASK) == 0) {
            mem <<= 4;                  /* adjust mantisa by a nibble */
            em--;                       /* and adjust exponent smaller by 1 */
        }
    }

    /* process the register operand value */
    /* extract exponent and mantissa */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    er = (reg & DEXMASK) >> 56;         /* extract reg exponent */
    if (reg & DMSIGN) {                 /* reg negative */
        sign |= REGNEG;                 /* set neg flag (2) */
        reg = NEGATE32(reg);            /* negate number */
        er ^= 0x7f;                     /* complement exp */
    }
//#define DMMASK  0x00ffffffffffffffLL    /* double mantissa mask */
    reg &= DMMASK;                      /* get reg mantissa */

    /* normalize the register mantissa */
    if (reg != 0) {                     /* check for zero value */
//#define DNMASK  0x0f00000000000000LL  /* double nibble mask */
        while ((reg != 0 ) && (reg & DNMASK) == 0) {
            reg <<= 4;                  /* adjust mantisa by a nibble */
            er--;                       /* and adjust exponent smaller by 1 */
        }
    }

    mem = mem << 4;                     /* align mem for normalization */
    reg = reg << 4;                     /* align reg for normalization */

    /* subtract memory exp from reg exp */
    temp = er - em;                     /* get the signed exp difference */

    if (temp > 0) {                     /* reg exp > mem exp */
        if (temp > 15) {  
            mem = 0;                    /* if too much difference, make zero */
            sign &= ~MEMNEG;            /* is reg value negative */
        } else
            /* Shift mem right if reg larger */
            mem >>= (4 * temp);         /* adjust for exponent difference */
    } else
    if (temp < 0) {                     /* reg < mem exp */
        if (temp < -15) {
            reg = 0;                    /* if too much difference, make zero */
            sign &= ~REGNEG;            /* is reg value negative */
        } else
            /* Shift reg right if mem larger */
            reg >>= (4 * (-temp));      /* adjust for exponent difference */
        er = em;                        /* make exponents the same */
    }

    /* er now has equal exponent for both values */
    /* add results */
//  if (sign == 2 || sign == 1) {
    if (sign == REGNEG || sign == MEMNEG) {
        /* different signs so do subtract */
//#define DIBMASK 0x0fffffffffffffffLL    /* double fp nibble mask */
        mem ^= DIBMASK;                 /* complement the value and inc */
        mem++;                          /* negate all but upper nibble */
        res = reg + mem;                /* add the values */
//#define DCMASK  0x1000000000000000LL    /* double carry mask */
        if (res & DCMASK) {             /* see if carry */
            res &= DIBMASK;             /* clear off the carry bit */
        } else {
//          sign ^= 2;                  /* flip the sign */
            sign ^= REGNEG;             /* flip the sign (2) */
            res ^= DIBMASK;             /* and negate the value */
            res++;                      /* negate all but the upper nibble */
        }
    } else {
        res = reg + mem;                /* same sign, just add */
    }

    sim_debug(DEBUG_EXP, &cpu_dev,
        "ADFD test OVF res %016lx er %02x sign %01x\n", res, er, sign);
    /* If overflow, shift right 4 bits */
    CC = 0;
//#define DCMASK  0x1000000000000000LL    /* double carry mask */
    if (res & DCMASK) {                 /* see if overflow carry */
        res >>= 4;                      /* move mantissa down 4 bits */
        er++;                           /* and adjust exponent */
        if (er >= 0x80) {               /* if exponent is too large, overflow */
            /* OVERFLOW */
            CC = CC1BIT|CC4BIT;         /* set arithmetic overflow */
            /* set CC2 & CC3 on exit */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "OVERFLOW res %016lx er %02x sign %01x\n", res, er, sign);
            CC |= (sign & 2)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
            if (CC & CC3BIT) {          /* NEG overflow? */
                if (type & FPADD)       /* is this single FP instruction */
                    res = 0x8000000100000000;   /* yes */
                else
                    res = 0x8000000000000001;   /* doouble yes */
             } else
                res = 0x7FFFFFFFFFFFFFFF;   /* no, pos */
            goto goout;
        }
    }

#ifdef MOVE
    /* Set condition codes */
    if (type & FPADD) {                 /* was this an add instruction */
        if (res != 0)                   /* see if non zero */
//          CC |= (sign & 2) ? 1 : 2;
            CC |= (sign & 2)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
        else {
            er = sign = 0;              /* we have zero CC4 */
            CC |= CC4BIT;               /* set zero cc */
        }
    } else {                            /* must be subtract */
//#define DUMASK  0x0ffffffffffffff0LL    /* double fp mask */
        if ((res & DUMASK) != 0)        /* mantissa not zero */
            CC |= (sign & 2)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
        else {
            res = er = sign = 0;        /* we have zero CC4 */
            CC |= CC4BIT;                /* set zero cc */
        }
    }
#endif

    /* normalize the fraction */
    if (res != 0) {                     /* check for zero value */
        while ((res != 0) && (res & DNMASK) == 0) {
            res <<= 4;                  /* adjust mantisa by a nibble */
            er--;                       /* and adjust exponent smaller by 1 */
        }
        /* Check if exponent underflow */
        if (er < 0) {
            /* UNDERFLOW */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "UNDERFLOW res %016lx er %02x sign %01x\n", res, er, sign);
            CC |= CC1BIT;               /* set arithmetic exception */
            CC |= (sign & 2)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
            res = 0;                    /* make all zero */
            sign = er = 0;              /* zero */
            goto goout;
        }
    } else {
        /* result is zero */
        sign = er = 0;                  /* make abs zero */
        CC = CC4BIT;                    /* zero value */
    }

    res >>= 4;                          /* remove the carryout nibble */
//#define DMMASK  0x00ffffffffffffffLL  /* double mantissa mask */
    res &= DMMASK;                      /* clear exponent */

//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    res |= ((((u_int64_t)er) << 56) & DEXMASK); /* merge exp and mantissa */
#ifndef MOVE
    /* Set condition codes */
    if (type & FPADD) {                 /* was this an add instruction */
        if (res != 0)                   /* see if non zero */
//          CC |= (sign & 2) ? 1 : 2;
            CC |= (sign & 2)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
        else {
            er = sign = 0;              /* we have zero CC4 */
            CC |= CC4BIT;               /* set zero cc */
        }
    } else {                            /* must be subtract */
//#define DUMASK  0x0ffffffffffffff0LL    /* double fp mask */
        if ((res & DUMASK) != 0)        /* mantissa not zero */
            CC |= (sign & 2)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
        else {
            res = er = sign = 0;        /* we have zero CC4 */
            CC |= CC4BIT;                /* set zero cc */
        }
    }
#endif
goout:
    /* store results */
    *cc = CC;                           /* save CC's */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
//  res |= ((((u_int64_t)er) << 56) & DEXMASK); /* merge exp and mantissa */
    /* if result not zero and reg or mem is negative, make negative */
    //FIXME ??
//  if (((CC & CC4BIT) == 0) && (sign & 3))  /* is result to be negative */
//      res = NEGATE32(res);            /* make value negative */
    if (((CC & CC4BIT) == 0) && ((sign == 0) || (sign == 3)))
        if (!((res > 0) && (sign == 0)))
            res = NEGATE32(res);        /* make negative */
    return res;                         /* return results */

#ifdef NOTNOW
    /* come here to set cc's and return */
    /* temp has return value */
//goout:
    if (ret & DMSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else
    if (ret == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
goout2:
    /* return temp to destination reg */
    *CC = cc;                           /* return CC's */
    return ret;                         /* return result */

DOVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
DUNFLO:
    ret = temp;                         /* get return value */
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (temp & DMSIGN) {
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    } else {
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    }
    goto goout2;                        /* go set cc's and return */
#endif
}
#endif

#ifdef USE_NEW_MUL
/* multiply register floating point number by memory floating point number */
/* set CC1 if overflow/underflow */
/* use revised normalization code */
t_uint64 n_mpfd(t_uint64 reg, t_uint64 mem, uint32 *cc) {
    u_int64_t   res;
    uint8       sign;
    int         er, em, temp;
    uint32      CC;

    *cc = 0;                            /* clear the CC'ss */
    CC = 0;                             /* clear local CC's */

    sim_debug(DEBUG_EXP, &cpu_dev,
        "MPFD entry mem %016lx reg %016lx\n", mem, reg);
    sign = 0;
    /* process the memory operand value */
    /* extract exponent and mantissa */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    em = (mem & DEXMASK) >> 56;         /* extract mem exponent */
    if (mem & DMSIGN) {                 /* mem negative */
//      sign |= 1;                      /* set neg flag */
        sign |= MEMNEG;                 /* set neg flag */
        mem = NEGATE32(mem);            /* complement exp */
        em ^= 0x7f;                     /* complement exp */
    }
//#define DMMASK  0x00ffffffffffffffLL    /* double mantissa mask */
    mem &= DMMASK;                      /* get mem mantissa */
//  mem = mem << 4;                     /* align mem for normalization */

    /* process the register operand value */
    /* extract exponent and mantissa */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    er = (reg & DEXMASK) >> 56;         /* extract reg exponent */
    if (reg & DMSIGN) {                 /* reg negative */
//      sign |= 2;                      /* set neg flag */
        sign |= REGNEG;                 /* set neg flag (2) */
        reg = NEGATE32(reg);            /* complement exp */
        er ^= 0x7f;                     /* complement exp */
    }
//#define DMMASK  0x00ffffffffffffffLL    /* double mantissa mask */
    reg &= DMMASK;                      /* get reg mantissa */
//  reg = reg << 4;                     /* align reg for normalization */

    /* normalize the memory mantissa */
    if (mem != 0) {                     /* check for zero value */
//#define DNMASK  0x0f00000000000000LL  /* double nibble mask */
        while ((mem != 0) && (mem & DNMASK) == 0) {
            mem <<= 4;                  /* adjust mantisa by a nibble */
            em--;                       /* and adjust exponent smaller by 1 */
        }
    }

    /* normalize the register mantissa */
    if (reg != 0) {                     /* check for zero value */
//#define DNMASK  0x0f00000000000000LL  /* double nibble mask */
        while ((mem != 0) && (reg & DNMASK) == 0) {
            reg <<= 4;                  /* adjust mantisa by a nibble */
            er--;                       /* and adjust exponent smaller by 1 */
        }
    }

    er = er + em - 0x40;                /* get the exp value */

    res = 0;                            /* zero result for multiply */
    /* multiply by doing shifts and adds */
//  for (temp = 0; temp < 56; temp++) {
    for (temp = 0; temp < 60; temp++) {
        /* Add if we need too */
        if (reg & 1)
            res += mem;
        /* Shift right by one */
        reg >>= 1;
        res >>= 1;
    }
    er++;                               /* adjust exp for extra nible shift */

    /* If overflow, shift right 4 bits */
//#define DCMASK  0x1000000000000000LL  /* double carry mask */
//  if (res & DCMASK) {                 /* see if overflow carry */
//#define DEXMASK 0x7f00000000000000LL  /* double exponent mask */
    if (res & DEXMASK) {                /* see if overflow carry */
        res >>= 4;                      /* move mantissa down 4 bits */
        er++;                           /* and adjust exponent */
        if (er >= 0x80) {               /* if exponent is too large, overflow */
            /* OVERFLOW */
            res = 0x7fffffffffffffffll; /* too big, set max */
            CC = CC1BIT;                /* set arithmetic exception */
        }
    }

    /* Align the results */
    if (res != 0) {
//#define DZMASK  0x00f0000000000000LL  /* double nibble mask */
        while ((res != 0) && (res & DZMASK) == 0) {
            res <<= 4;                  /* move over mantessa */
            er--;                       /* reduce exponent cocunt by 1 */
        }
        if (er < 0) {                   /* check if rxponent underflow */
            /* UNDERFLOW */
            res = 0;                    /* make return value zero */
            sign = er = 0;
            CC |= CC1BIT;               /* set arithmetic exception */
            CC |= CC4BIT;               /* set zero value CC */
        }
    } else {
        er = sign = 0;
        CC |= CC4BIT;                   /* set zero value CC */
    }

//#define DMMASK  0x00ffffffffffffffLL  /* double mantissa mask */
    res &= DMMASK;                      /* clear exponent */
//#define DEXMASK 0x7f00000000000000LL  /* double exponent mask */
    res |= ((((u_int64_t)er) << 56) & DEXMASK); /* merge exp and mantissa */
    if (sign == 2 || sign == 1)         /* is result to be negative */
        res = NEGATE32(res);            /* make value negative */

    /* determine CC's for result */
    CC = 0;
    if (res != 0)                       /* see if non zero */
        CC = (sign & 2)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
    else {
        er = sign = 0;                  /* we have zero CC4 */
        CC = CC4BIT;                    /* set zero cc */
    }
    *cc = CC;                           /* save CC's */
    return res;                         /* return results */

#ifdef NOTNOW
    /* come here to set cc's and return */
    /* temp has return value */
goout:
    if (ret & DMSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else
    if (ret == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
goout2:
    /* return temp to destination reg */
    *cc = CC;                           /* return CC's */
    return ret;                         /* return result */

DOVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
DUNFLO:
    ret = temp;                         /* get return value */
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (temp & DMSIGN) {
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    } else {
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    }
    goto goout2;                        /* go set cc's and return */
#endif
}
#endif

#ifdef USE_NEW_DIV
/* divide register floating point number by memory floating point number */
/* set CC1 if overflow/underflow */
/* use revised normalization code */
t_uint64 n_dvfd(t_uint64 reg, t_uint64 mem, uint32 *cc) {
    t_uint64   res;
    char        sign;
    int         er, em, temp;
    uint32      CC;

    *cc = 0;                            /* clear the CC'ss */
    CC = 0;                             /* clear local CC's */

    sign = 0;
    /* process the memory operand value */
    /* extract exponent and mantissa */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    em = (mem & DEXMASK) >> 56;         /* extract mem exponent */
    if (mem & DMSIGN) {                 /* mem negative */
        sign |= 1;                      /* set neg flag */
        mem = NEGATE32(mem);            /* complement exp */
        em ^= 0x7f;                     /* complement exp */
    }
//#define DMMASK  0x00ffffffffffffffLL    /* double mantissa mask */
    mem &= DMMASK;                      /* get mem mantissa */
//  mem = mem << 4;                     /* align mem for normalization */

    /* process the register operand value */
    /* extract exponent and mantissa */
//#define DEXMASK 0x7f00000000000000LL    /* double exponent mask */
    er = (reg & DEXMASK) >> 56;         /* extract reg exponent */
    if (reg & DMSIGN) {                 /* reg negative */
        sign |= 2;                      /* set neg flag */
        reg = NEGATE32(reg);            /* complement exp */
        er ^= 0x7f;                     /* complement exp */
    }
//#define DMMASK  0x00ffffffffffffffLL    /* double mantissa mask */
    reg &= DMMASK;                      /* get reg mantissa */
//  reg = reg << 4;                     /* align reg for normalization */

//printf("DV etr mem %016lx em %02x %.12e\n\r", mem, em, dfpval(mem));
//printf("DV etr reg %016lx er %02x %.12e\n\r", reg, er, dfpval(reg));
    /* normalize the memory mantissa */
    if (mem != 0) {                     /* check for zero value */
//#define DNMASK  0x0f00000000000000LL  /* double nibble mask */
        while ((mem & DNMASK) == 0) {
            mem <<= 4;                  /* adjust mantisa by a nibble */
            em--;                       /* and adjust exponent smaller by 1 */
        }
    }
//printf("DV nor mem %016lx em %02x %.12e\n\r", mem, em, dfpval(mem));

    /* see if division by zero */
    if (mem == 0) {
        printf("DV Division by zero\n\r");
    }

    /* normalize the register mantissa */
    if (reg != 0) {                     /* check for zero value */
//#define DNMASK  0x0f00000000000000LL  /* double nibble mask */
        while ((reg & DNMASK) == 0) {
            reg <<= 4;                  /* adjust mantisa by a nibble */
            er--;                       /* and adjust exponent smaller by 1 */
        }
    }
//printf("DV nor reg %016lx er %02x em %02x %.12e\n\r", reg, er, em, dfpval(reg));

    er = er - em + 0x40;                /* get the exp value */

    /* move left 1 nubble for divide */
//? reg <<= 4;
//? mem <<= 4;

    /* see if we need to adjust divisor if larger that dididend */
    if (reg > mem) {
        reg >>= 4;
        er++;
    }

//#define DIBMASK 0x0fffffffffffffffLL  /* double nibble mask */
//  mem ^= XMASKL;                      /* change sign of mem val to do add */
    mem ^= DIBMASK;                     /* change sign of mem val to do add */
    mem++;                              /* comp & incr */

//printf("DV exp er %02x\n\r",er);
    res = 0;                            /* zero result for multiply */
    /* do divide by using shift & add (subt) */
//  for (temp = 0; temp < 56; temp++) {
//  for (temp = 56; temp > 0; temp--) {
//  for (temp = 0; temp < 60; temp++) {
    for (temp = 56; temp > 0; temp--) {
        t_uint64   tmp;

//printf("DV div reg %016lx mem %016lx res %016lx temp %02x\n\r", reg, mem, res, temp);
        /* Add if we need too */
        /* Shift left by one */
        reg <<= 1;
        /* Subtract remainder to dividend */
        tmp = reg + mem;

//printf("DV div reg %016lx mem %016lx res %016lx temp %02x\n\r", reg, mem, res, temp);
        /* Shift quotent left one bit */
        res <<= 1;

        /* If remainder larger then divisor replace */
//      if ((tmp & CMASKL) != 0) {
//#define DCMASK  0x1000000000000000LL  /* double carry mask */
        if ((tmp & DCMASK) != 0) {
            reg = tmp;
            res |= 1;
        }
    }

    /* Compute one final set to see if rounding needed */
    /* Shift left by one */
    reg <<= 1;
    /* Subtract remainder to dividend */
    reg += mem;

#ifndef TRUNCATE
    /* If .5 off, round */
//#define DMSIGN  0x8000000000000000LL  /* 64 bit minus sign */
//  if ((reg & MSIGNL) != 0) {
    if ((reg & DMSIGN) != 0) {
//printf("DV diva reg %016lx mem %016lx res %016lx temp %02x\n\r", reg, mem, res, temp);
        res++;
    }
#endif
//printf("DV aft mpy res %016lx er %02x\n\r",res, er);

    /* If overflow, shift right 4 bits */
//#define DCMASK  0x1000000000000000LL  /* double carry mask */
//  if (res & DCMASK) {                 /* see if overflow carry */
//  if (res & 0x0100000000000000LL) {   /* see if overflow carry */
//  if (res & 0x0300000000000000LL) {   /* see if overflow carry */
//  if (res & EMASKL) {                 /* see if overflow carry */
//#define DEXMASK 0x7f00000000000000ll  /* exponent mask */
    if (res & DEXMASK) {                /* see if overflow carry */
        res >>= 4;                      /* move mantissa down 4 bits */
        er++;                           /* and adjust exponent */
        if (er >= 0x80) {               /* if exponent is too large, overflow */
            /* OVERFLOW */
            res = 0x7fffffffffffffffll; /* too big, set max */
            CC = CC1BIT;                /* set arithmetic exception */
        }
    }

    /* Align the results */
    if ((res) != 0) {
//#define DZMASK  0x00f0000000000000LL  /* double nibble mask */
        while ((res & DZMASK) == 0) {
            res <<= 4;
            er--;
        }
        /* Check if underflow */
        if (er < 0) {
            /* UNDERFLOW */
            res = 0;                    /* make return value zero */
            sign = er = 0;
            CC |= CC1BIT;               /* set arithmetic exception */
            CC |= CC4BIT;               /* set zero value CC */
        }
    } else {
        er = sign = 0;
        CC |= CC4BIT;                   /* set zero value CC */
    }

//#define DMMASK  0x00ffffffffffffffLL  /* double mantissa mask */
//  res &= DMMASK;                      /* clear exponent */
    res &= 0x00fffffffffffff0LL;        /* clear exponent */
//#define DEXMASK 0x7f00000000000000LL  /* double exponent mask */
    res |= ((((t_uint64)er) << 56) & DEXMASK); /* merge exp and mantissa */
    if (sign == 2 || sign == 1)         /* is result to be negative */
        res = NEGATE32(res);            /* make value negative */

    /* determine CC's for result */
    CC = 0;
    if (res != 0)                       /* see if non zero */
        CC = (sign & 2)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
    else {
        er = sign = 0;                  /* we have zero CC4 */
        CC = CC4BIT;                    /* set zero cc */
    }
    *cc = CC;                           /* save CC's */
    return res;                         /* return results */

#ifdef NOTNOW
    /* come here to set cc's and return */
    /* temp has return value */
goout:
    if (ret & DMSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else
    if (ret == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
goout2:
    /* return temp to destination reg */
    *cc = CC;                           /* return CC's */
    return ret;                         /* return result */

DOVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
DUNFLO:
    ret = temp;                         /* get return value */
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (temp & DMSIGN) {
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    } else {
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    }
    goto goout2;                        /* go set cc's and return */
#endif
}
#endif

