/* sel32_fltpt.c: SEL 32 floating point instructions processing.

   Copyright (c) 2018, James C. Bevier

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

#define NORMASK 0xf8000000              /* normalize 5 bit mask */
#define DNORMASK 0xf800000000000000ll   /* double normalize 5 bit mask */
#define EXMASK 0x7f000000               /* exponent mask */
#define FRMASK 0x80ffffff               /* fraction mask */
#define DEXMASK 0x7f00000000000000ll    /* exponent mask */
#define DFRMASK 0x80ffffffffffffffll    /* fraction mask */

/* normalize floating point number */
uint32 s_nor(uint32 reg, uint32 *exp) {
    uint32 texp = 0;                    /* no exponent yet */

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
        texp = 0x40 - texp;             /* subtract shift count from 0x40 */
    }
    *exp = texp;                        /* return exponent */
    return (reg);                       /* return normalized register */
}

/* normalize double floating point number */
t_uint64 s_nord(t_uint64 reg, uint32 *exp) {
    uint32 texp = 0;                    /* no exponent yet */

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
        texp = 0x40 - texp;             /* subtract shift count from 0x40 */
    }
    *exp = texp;                        /* return exponent */
    return (reg);                       /* return normalized double register */
}

/* add memory floating point number to register floating point number */
/* set CC1 if overflow/underflow */
uint32 s_adfw(uint32 reg, uint32 mem, uint32 *cc) {
    uint32 mfrac, rfrac, frac, ret=0, oexp;
    uint32 CC, sc, sign, expr, expm, exp;

    *cc = 0;                            /* clear the CC'ss */
    CC = 0;                             /* clear local CC's */
    /* process the memory operand value */
    if (mem == 0) {                     /* test for zero operand */
        ret = reg;                      /* return original register value */
        goto goout;                     /* go set cc's and return */
    }
    expm = mem & EXMASK;                /* extract exponent from operand */
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
        goto goout;                     /* go set cc's and return */
    }
    expr = reg & EXMASK;                /* extract exponent from reg operand */
    rfrac = reg & FRMASK;               /* extract fraction */
    if (rfrac & MSIGN) {                /* test for negative fraction */
        /* negative fraction */
        expr ^= EXMASK;                 /* ones complement the exponent */
        rfrac |= EXMASK;                /* adjust the fraction */
    }
    rfrac = ((int32)rfrac) << 4;        /* do sla 4 of fraction */

    exp = expr - expm;                  /* subtract memory exponent from reg exponent */
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
        sc = exp >> (24 - 2);           /* shift count down to do x4 the count */
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
        sc = exp >> (24 - 2);           /* shift count down to do x4 the count */
        mfrac = ((int32)mfrac) >> sc;
        oexp = expr;                    /* reg is larger exponent, save for final exponent add */
    }
    frac = rfrac + mfrac;               /* add fractions */
    if (frac == 0) {
        /* return the zero value */
        ret = frac;                     /* return zero to caller */
        goto goout;                     /* go set cc's and return */
    }
    if ((int32)frac >= 0x10000000) {    /* check for overflow */
        /* overflow */
        frac = (int32)frac >> 1;        /* sra 1 */
    } else {
        /* no overflow */
        /* check for underflow */
        if ((int32)frac >= 0xf0000000) {    /* underflow? */
            frac = ((int32)frac) << 3;  /* yes, sla 3 */
            oexp -= 0x01000000;         /* adjust exponent */
        } else {
            /* no underflow */
            frac = (int32)frac >> 1;    /* sra 1 */
        }
    }
    /* normalize the frac value and put exponent into exp */
    frac = s_nor(frac, &exp);
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
    exp = exp << 24;                    /* put exponent in upper byte */
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
        CC |= CC4BIT;               /* set CC4 for exponent overflow */
ARUNFLO:
        /* we have exponent underflow from addition */
        CC |= CC1BIT;               /* set CC1 for arithmetic exception */
        ret = frac;                 /* get return value */
        if ((frac & MSIGN) == 0) {
            CC |= CC2BIT;           /* set pos fraction bit CC2 */
        } else {
            CC |= CC3BIT;           /* set neg fraction bit CC3 */
        }
        *cc = CC;                   /* return CC's */
        /* return value is not valid, but return fixup value anyway */
        switch ((CC >> 27) & 3) {   /* rt justify CC3 & CC4 */
        case 0x0:
            return 0;               /* pos underflow */
            break;
        case 0x1:
            return 0x7fffffff;      /* positive overflow */
            break;
        case 0x2:
            return 0;               /* neg underflow */
            break;
        case 0x3:
            return 0x80000001;      /* negative overflow */
            break;
        }
        /* never here */
        goto goout2;                /* go set cc's and return */
    }
    /* no over/underflow */
    frac = (int32)frac >> 7;        /* positive fraction sra r7,7 */
    frac &= FRMASK;                 /* mask out the exponent field */
    if ((int32)frac > 0) {          /* see if positive */
        ret = exp | frac;           /* combine exponent & fraction */
    } else {
        if (frac != 0) {
            exp ^= EXMASK;          /* for neg fraction, complement exponent */
            ret = exp | frac;       /* combine exponent & fraction */
        }
    }
    /* come here to set cc's and return */
    /* ret has return value */
goout:
    if (ret & MSIGN)
        CC |= CC3BIT;               /* CC3 for neg */
    else if (ret == 0)
        CC |= CC4BIT;               /* CC4 for zero */
    else 
        CC |= CC2BIT;               /* CC2 for greater than zero */
goout2:
    /* return temp to destination reg */
    *cc = CC;                       /* return CC's */
    return ret;                     /* return result */
}

/* subtract memory floating point number from register floating point number */
uint32 s_sufw(uint32 reg, uint32 mem, uint32 *cc) {
    return s_adfw(reg, NEGATE32(mem), cc);
}

/* add memory floating point number to register floating point number */
/* set CC1 if overflow/underflow */
t_uint64 s_adfd(t_uint64 reg, t_uint64 mem, uint32 *cc) {
    t_uint64 temp, dblmem, dblreg, ret;
    uint32 CC, sc, sign, expm, expr, exp;

    *cc = 0;                            /* clear the CC'ss */
    CC = 0;                             /* clear local CC's */
    /* process the memory operand value */
    if (mem == 0) {                     /* test for zero operand */
        ret = reg;                      /* return original reg value */
        goto goout;                     /* go set cc's and return */
    }
    /* separate mem dw into two 32 bit numbers */
    /* mem value is not zero, so extract exponent and mantissa */
    expm = (uint32)((mem & DEXMASK) >> 32); /* extract exponent */
    dblmem = mem & DFRMASK;             /* extract fraction */
    if (dblmem & DMSIGN) {              /* test for negative fraction */
        /* negative fraction */
        expm ^= EXMASK;                 /* ones complement the exponent */
        dblmem |= DEXMASK;              /* adjust the fraction */
    }

    /* process the register operator value */
    if (reg == 0) {                     /* see if reg value is zero */
        ret = mem;                      /* return original mem operand value */
        goto goout;                     /* go set cc's and return */
    }
    /* separate reg dw into two 32 bit numbers */
    /* reg value is not zero, so extract exponent and mantissa */
    expr = (uint32)((reg & DEXMASK) >> 32); /* extract exponent */
    dblreg = reg & DFRMASK;             /* extract fraction */
    if (dblreg & DMSIGN) {              /* test for negative fraction */
        /* negative fraction */
        expr ^= EXMASK;                 /* ones complement the exponent */
        dblreg |= DEXMASK;              /* adjust the fraction */
    }

    exp = expr - expm;                  /* subtract memory exp from reg exp */
    sign = expr;                        /* save register exponent */
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
        /* (exp >> 24) *4 */
        sc = exp >> (24 - 2);           /* shift count down to do x4 the count */
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
        sc = exp >> (24 - 2);           /* shift count down to do x4 the count */
        dblmem = (t_int64)dblmem >> sc; /* shift sra r6,cnt x4 */
    }
    temp = dblreg + dblmem;             /* add operand to operator (fractions) */
    if (temp == 0) {
        /* return the zero value */
        ret = temp;                     /* return zero to caller */
        goto goout;                     /* go set cc's and return */
    }
    exp = sign - 0x3f000000;            /* adjust exponent */
    temp = (t_int64)temp << 3;          /* adjust the mantissa sla 3 */
    /* normalize the value in temp and put exponent into sc */
    temp = s_nord(temp, &sc);
    if (temp == DMSIGN) {
        /* value is neg zero, so fix it up */
        temp = DNORMASK;                /* correct the value */
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

    ret = (t_int64)temp >> 7;           /* position fraction srad 7 */
    ret &= DFRMASK;                     /* mask out exponent field leaving fraction */
    /* test sign of fraction */
    if (ret != 0) {                     /* test for zero, to return zero */
        if (ret & DMSIGN)               /* see if negative */
            /* fraction is negative */
            exp ^= EXMASK;              /* neg fraction, so complement exponent */
        ret = ret | ((t_uint64)exp << 32);  /* position and insert exponent */
    }

    /* come here to set cc's and return */
    /* temp has return value */
goout:
    if (ret & DMSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (ret == 0)
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
}

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
    uint32 temp2, CC = 0, neg = 0, sc = 0;
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
    sign = mem;                         /* save original value for sign */
    if (mem & MSIGN) {                  /* check for negative */
        mem = NEGATE32(mem);            /* make mem positive */
    } else {
        if (mem == 0) {
            temp = 0;                   /* return zero */
            goto setcc;                 /* go set CC's */
        }
        /* gt 0, fall through */
    }
    expm = (mem >> 24);                 /* get operator exponent */
    mem <<= 8;                          /* move fraction to upper 3 bytes */
    mem >>= 1;                          /* adjust fraction */

    /* process operand */
    if (reg & MSIGN) {                  /* check for negative */
        sign ^= reg;                    /* adjust sign */
        reg = NEGATE32(reg);            /* make reg positive */
    } else {
        if (reg == 0) {
            temp = 0;                   /* return zero */
            goto setcc;                 /* go set CC's */
        }
        /* gt 0, fall through */
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
    if ((int32)expr < 0)                /* test for underflow */
        goto DUNFLO;                    /* go process underflow */
    if ((int32)expr > 0x7f)             /* test for overflow */
        goto DOVFLO;                    /* go process overflow */
    expr ^= 0xffffffff;                 /* complement exponent */
    temp2 += 0x40;                      /* round at bit 25 */
    goto RRND3;                         /* go merge code */
RRND1:
    temp2 += 0x40;                      /* round at bit 25 */
RRND2:
    expr += temp;                       /* add exponent */
    if ((int32)expr < 0) {              /* test for underflow */
        goto DUNFLO;                    /* go process underflow */
    }
    if ((int32)expr > 0x7f) {           /* test for overflow */
        goto DOVFLO;                    /* go process overflow */
    }
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
    sign = mem;                         /* save original value for sign */
    if (mem & MSIGN) {                  /* check for negative */
        mem = NEGATE32(mem);            /* make mem positive */
    } else {
        if (mem == 0) {                 /* check for divide by zero */
            goto DOVFLO;                /* go process overflow */
        }
        /* gt 0, fall through */
    }
    expm = (mem >> 24);                 /* get operand exponent */
    mem <<= 8;                          /* move fraction to upper 3 bytes */
    mem >>= 1;                          /* adjust fraction for divide */

    /* process operand */
    if (reg & MSIGN) {                  /* check for negative */
        sign ^= reg;                    /* adjust sign */
        reg = NEGATE32(reg);            /* make reg positive */
    } else {
        if (reg == 0) {
            temp = 0;                   /* return zero */
            goto setcc;                 /* go set CC's */
        }
        /* gt 0, fall through */
    }
    expr = (reg >> 24);                 /* get operator exponent */
    reg <<= 8;                          /* move fraction to upper 3 bytes */
    reg >>= 6;                          /* adjust fraction for divide */

    temp = expr - expm;                 /* subtract exponents */
    dtemp = ((t_uint64)reg) << 32;      /* put reg fraction in upper 32 bits */
    temp2 = (uint32)(dtemp / mem);      /* divide reg fraction by mem fraction */
    temp2 >>= 3;                        /* shift out excess bits */
    temp2 <<= 3;                        /* replace with zero bits */
    if (sign & MSIGN)
        temp2 = NEGATE32(temp2);        /* if negative, negate fraction */
    /* normalize the result in temp and put exponent into expr */
    temp2 = s_nor(temp2, &expr);        /* normalize fraction */
    temp += 1;                          /* adjust exponent */

    if (temp2 >= 0x7fffffc0)            /* check for special rounding */
        goto RRND2;                     /* no special handling */
    /* FIXME dead code */
    if (temp2 == MSIGN) {               /* check for minux zero */
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
    if (expr < 0)                       /* test for underflow */
        goto DUNFLO;                    /* go process underflow */
    if (expr > 0x7f)                    /* test for overflow */
        goto DOVFLO;                    /* go process overflow */
    expr ^= FMASK;                      /* complement exponent */
    temp2 += 0x40;                      /* round at bit 25 */
    goto RRND3;                         /* go merge code */
RRND1:
    temp2 += 0x40;                      /* round at bit 25 */
RRND2:
    expr += temp;                       /* add exponent */
    if ((int32)expr < 0) {              /* test for underflow */
        goto DUNFLO;                    /* go process underflow */
    }
    if (expr > 0x7f) {                  /* test for overflow */
        goto DOVFLO;                    /* go process overflow */
    }
    if (sign & MSIGN)                   /* test for negative */
        expr ^= DMASK;                  /* yes, complement exponent */
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
    else if (temp == 0)
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
    uint32 CC = 0, temp, temp2, sign = 0;
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
    dblreg &= DFRMASK;                  /* mask out exponent field */
    if (dblreg != 0) {                  /* see if 0, if so return 0 */
        if (dblreg & DMSIGN)            /* see if negative */
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
    t_uint64 tr1, tr2, tl1, tl2, dblreg;
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
    tl2 = (reg >> 32) & D32RMASK;       /* get left half of operand */
    tr2 = reg & D32RMASK;               /* get right half of operand */

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
    dblreg &= 0xffffffffffffffe0;       /* fixup quotient */
    /* exp in temp */
    if (sign)                           /* neg input */
        dblreg = NEGATE32(dblreg);      /* yes, negate result */
    /* normalize the value in dblreg and put exponent into expr */
    dblreg = s_nord(dblreg, &expr);     /* normalize fraction */
    if (dblreg == DMSIGN) {             /* check for neg zero */
        dblreg  = DNORMASK;             /* correct the value */
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
    dblreg &= DFRMASK;                  /* mask out exponent field */
    if (dblreg != 0) {                  /* see if 0, if so return 0 */
        if (dblreg & DMSIGN)            /* see if negative */
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
    else if (dblreg == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    *cc = CC;                           /* return CC's */
    return dblreg;                      /* return result */
}
