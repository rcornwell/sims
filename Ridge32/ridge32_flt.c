/* Ridge32_flp.c: Ridge 32 floating point.

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

#define EMASK  0x7f800000
#define MSIGN  0x80000000
#define MMASK  0x007fffff
#define ONE    0x00800000
#define NMASK  0x01000000
#define XMASK  0x00ffffff
#define CMASK  0xff000000
#define CMASK1 0xfe000000
#define FMASK  0xffffffff
#define DEMSK  0x7ff00000
#define DCMSK  0xfff00000
#define DMMSK  0x000fffff
#define DONE   0x00100000

/* Convert integer to floating point */
int
rfloat(uint32 *res, uint32 src1)
{
    int e1 = 150;       /* Exponent */
    int s = 0;          /* Sign */

    /* Make number positive */
    if (src1 & MSIGN) {
       s = 1;
       src1 = (src1 ^ FMASK) + 1;
    }

    /* Quick exit if zero */
    if (src1 == 0) {
        *res = (s)? MSIGN: 0;
        return 0;
    }

    /* Denomalize the number first */
    while ((src1 & CMASK) != 0) {
       src1 >>= 1;
       e1 ++;
    }

    /* Now normalize number */
    while ((src1 & ONE) == 0) {
       src1 <<= 1;
       e1 --;
    }

    /* Put things together */
    *res = ((s) ? MSIGN: 0) | (e1 << 23) | (src1 & MMASK);
    return 0;
}

/* Convert floating point to integer */
int
rfix(uint32 *res, uint32 src, int round)
{
     int e1;             /* Exponent */
     int s = 0;          /* Sign */

     /* Extract sign and exponent */
     e1 = (src & EMASK) >> 23;
     if (src & MSIGN) {
        s = 1;
        src &= ~MSIGN;
     }

     /* Check if unormalized */
     if (e1 == 0) {
        if (s)
           src = (src ^ FMASK) + 1;
        *res = src;
        return 0;
     }

     /* Check if zero result */
     if (e1 < 119) {
         *res = 0;
         return 0;
     }

     /* Check if out of range */
     if (e1 > 157) {
         *res = (s)? MSIGN : ~MSIGN;
         return 18;   /* Indicate overflow */
     }

     /* Convert to scaled integer */
     src &= MMASK;
     src |= ONE;
     src <<= 8;   /* Add in guard bit */

     while (e1 < 157) {
        src >>= 1;
        e1 ++;
     }

     if (round)
        src++;
     src >>= 1;  /* Remove guard bit */
     if (s)
        src = (src ^ FMASK) + 1;
     *res = src;
     return 0;
}

/* Make single precsion number a double precision number */
void
makerd(uint32 *resl, uint32 *resh,  uint32 src)
{
    int  e;            /* Hold the exponent */

    e =  (src & EMASK) >> 23;

    if (e == 0) {
       *resh = 0;
       *resl = 0;
       return;
    }
    e -= 127;
    e += 1023;
    *resh = (src & 07) << 25;
    *resl = (src & MSIGN) | ((e << 20) & DEMSK) | ((src & MMASK) >> 3);
}


/* Compare to floating point numbers */
int
rcomp(uint32 src1, uint32 src2)
{
    int  e1, e2;        /* Hold the two exponents */
    int  temp;          /* Hold exponent difference */
    uint32  m1, m2;

    /* Extract numbers and adjust */
    e1 = (src1 & EMASK) >> 23;
    m1 = src1 & MMASK;      /* extract mantissa */
    if (e1 != 0)        /* Add in hidden bit if needed */
       m1 |= ONE;
    e2 = (src2 & EMASK) >> 23;
    m2 = src2 & MMASK;      /* extract mantissa */
    if (e2 != 0)        /* Add in hidden bit if needed */
       m2 |= ONE;
    temp = e1 - e2;
    /* Align operands */
    if (temp > 0) {
        if (temp > 24) {
            m2 = 0;
        } else {
            /* Shift src2 right if src1 larger expo - expo */
            m2 >>= temp;
        }
    } else if (temp < 0) {
        if (temp < -24) {
            m1 = 0;
        } else {
        /* Shift src1 right if src2 larger expo - expo */
            m1 >>= temp;
        }
    }

    /* Exponents should be equal now. */
    if (src1 & MSIGN)
        m1 = (m1 ^ FMASK) + 1;

    if ((src2 & MSIGN) == 0)
        m2 = (m2 ^ FMASK) + 1;

    /* Add results */
    m1 = m1 + m2;

    /* Compute result */
    if (m1 & MSIGN) {
        return FMASK;
    } else if (m1 != 0) {
        return 1;
    }
    return 0;
}


/* Add two single precision numbers */
int
radd(uint32 *res, uint32 src1, uint32 src2)
{
    int  e1, e2;        /* Hold the two exponents */
    int  s;             /* Hold resulting sign */
    int  temp;          /* Hold exponent difference */

    /* Extract numbers and adjust */
    e1 = (src1 & EMASK) >> 23;
    e2 = (src2 & EMASK) >> 23;
    s = 0;
    if (src1 & MSIGN)
       s |= 2;
    if (src2 & MSIGN)
       s |= 1;
    src2 &= MMASK;      /* extract mantissa */
    if (e2 != 0)        /* Add in hidden bit if needed */
       src2 |= ONE;
    src1 &= MMASK;      /* extract mantissa */
    if (e1 != 0)        /* Add in hidden bit if needed */
       src1 |= ONE;
    temp = e1 - e2;
    /* Create guard digit */
    src2 <<= 1;
    src1 <<= 1;
    if (temp > 0) {
        if (temp > 24) {
            src2 = 0;
        } else {
            /* Shift src2 right if src1 larger expo - expo */
            src2 >>= temp;
        }
    } else if (temp < 0) {
        if (temp < -24) {
            src1 = 0;
            e1 = e2;
        } else {
            /* Shift src1 right if src2 larger expo - expo */
            src1 >>= temp;
            e1 += temp;
        }
    }

    /* Exponents should be equal now. */
    if (s & 2)
        src1 = (src1 ^ FMASK) + 1;

    if (s & 1)
        src2 = (src2 ^ FMASK) + 1;

    /* Add results */
    src1 = src1 + src2;

    /* figure sign */
    if (src1 & MSIGN) {
        src1 = (src1 ^ FMASK) + 1;
        s = 1;
    } else {
        s = 0;
    }

    /* Handle overflow */
    while ((src1 & CMASK1) != 0) {
        src1 >>= 1;
        e1++;
    }

    /* Exit if zero result */
    if (src1 == 0) {
       *res = ((s)?MSIGN:0);
       return 0;
    }

    /* Normalize result */
    while ((src1 & NMASK) == 0) {
        src1 <<= 1;
        e1--;
    }

    /* Remove DP Guard bit */
    src1 >>= 1;

    *res = ((s)?MSIGN:0) | ((e1 << 23) & EMASK) | (src1 & MMASK);

    if (e1 > 254)
       return 18;
    else if (e1 < 0)
       return 19;
    return 0;
}

/* Multiply two single precision numbers */
int
rmult(uint32 *res, uint32 src1, uint32 src2)
{
    int  e1, e2;        /* Hold the two exponents */
    int  s;             /* Hold resulting sign */
    int  temp;          /* Hold exponent difference */
    uint32  dest, desth;

    /* Extract numbers and adjust */
    e1 = (src1 & EMASK) >> 23;
    e2 = (src2 & EMASK) >> 23;
    s = 0;
    if ((src1 & MSIGN) != (src2 & MSIGN))
       s = 1;
    src2 &= MMASK;      /* extract mantissa */
    if (e2 != 0)        /* Add in hidden bit if needed */
       src2 |= ONE;
    src1 &= MMASK;      /* extract mantissa */
    if (e1 != 0)        /* Add in hidden bit if needed */
       src1 |= ONE;

    /* Compute exponent */
    e1 = e1 + e2 - 127;

    dest = desth = 0;

    /* Do multiply */
    for (temp = 0; temp < 24; temp++) {
         /* Add if we need too */
         if (src1 & 1) {
             dest += src2;
         }
         /* Shift right by one */
         src1 >>= 1;
         desth >>= 1;
         if (dest & 1)
            desth |= MSIGN;
         dest >>= 1;
    }

    /* Fix result */
    dest <<= 1;
    if (desth & MSIGN)
       dest |= 1;
    desth <<= 1;

    /* Handle overflow */
    while ((dest & CMASK) != 0) {
        desth >>= 1;
        if (dest & 1)
           desth |= MSIGN;
        dest >>= 1;
        e1++;
    }

    /* Exit if zero result */
    if (dest == 0 && desth == 0) {
       *res = ((s)?MSIGN:0);
       return 0;
    }

    /* Normalize result */
    while ((dest & ONE) == 0) {
        dest <<= 1;
        if (desth & MSIGN)
           dest |= 1;
        desth <<= 1;
        e1--;
    }

    *res = ((s)?MSIGN:0) | ((e1 << 23) & EMASK) | (dest & MMASK);

    if (e1 > 254)
       return 18;
    else if (e1 < 0)
       return 19;
    return 0;
}

/* Divide two single precision numbers */
int
rdiv(uint32 *res, uint32 src1, uint32 src2)
{
    int  e1, e2;        /* Hold the two exponents */
    int  s;             /* Hold resulting sign */
    int  temp;          /* Hold exponent difference */
    uint32  dest, desth;
    uint32  src1h;

    /* Extract numbers and adjust */
    e1 = (src1 & EMASK) >> 23;
    e2 = (src2 & EMASK) >> 23;
    s = 0;
    if (e2 == 0)
       return 20;
    if ((src1 & MSIGN) != (src2 & MSIGN))
       s = 1;
    src2 &= MMASK;      /* extract mantissa */
    if (e2 != 0)        /* Add in hidden bit if needed */
       src2 |= ONE;
    src1 &= MMASK;      /* extract mantissa */
    if (e1 != 0)        /* Add in hidden bit if needed */
       src1 |= ONE;

    /* Compute exponent */
    e1 = e1 - e2 + 127;

    dest = desth = 0;

    src1h = src1;
    src1 = 0;


    /* Do divide */
    dest = 0;
    for (temp = 0; temp < 32; temp++) {
         /* Shift left by one */
         src1 <<= 1;
         if (src1h & MSIGN)
             src1 |= 1;
         src1h <<= 1;

         /* Subtract remainder to dividend */
         desth = src1 - src2;

         /* Shift quotent left one bit */
         dest <<= 1;

         /* If remainder larger then divisor replace */
         if ((desth & MSIGN) == 0) {
             src1 = desth;
             dest |= 1;
         }
    }

    *res = ((s)?MSIGN:0) | ((e1 << 23) & EMASK) | (desth & MMASK);

    if (e1 > 254)
       return 18;
    else if (e1 < 0)
       return 19;
    return 0;
}

/* Convert integer to floating point */
int
dfloat(uint32 *resl, uint32 *resh, uint32 src1)
{
    int     e1 = 1043;      /* Exponent */
    int     s = 0;          /* Sign */
    uint32  src1h = 0;      /* High order bits */

    /* Make number positive */
    if (src1 & MSIGN) {
       s = 1;
       src1 = (src1 ^ FMASK) + 1;
    }

    /* Quick exit if zero */
    if (src1 == 0) {
        *resl = (s)? MSIGN: 0;
        *resh = 0;
        return 0;
    }

    /* Denomalize the number first */
    while ((src1 & DCMSK) != 0) {
       src1h >>= 1;
       if (src1 & 1)
          src1h |= MSIGN;
       src1 >>= 1;
       e1 ++;
    }

    /* Now normalize number */
    while ((src1 & DONE) == 0) {
       src1 <<= 1;
       if (src1h & MSIGN)
          src1 |= 1;
       src1h <<= 1;
       e1 --;
    }

    /* Put things together */
    *resl = ((s) ? MSIGN : 0) | ((e1 << 20) & DEMSK) | (src1 & DMMSK);
    *resh = src1h;
    return 0;
}

/* Convert floating point to integer */
int
dfix(uint32 *res, uint32 src, uint32 srch, int round)
{
     int e1;             /* Exponent */
     int s = 0;          /* Sign */

     /* Extract sign and exponent */
     e1 = (src & DEMSK) >> 20;
     if (src & MSIGN) {
        s = 1;
        src &= ~MSIGN;
     }

     /* Check if unormalized */
     if (e1 == 0) {
        if (s)
           src = (src ^ FMASK) + 1;
        *res = src;
        return 0;
     }

     /* Check if zero result */
     if (e1 < 1023) {
         *res = 0;
         return 0;
     }

     /* Check if out of range */
     if (e1 > 1053) {
         *res = (s)? MSIGN : ~MSIGN;
         return 18;   /* Indicate overflow */
     }

     /* Convert to scaled integer */
     src &= DMMSK;
     src |= DONE;
     src <<= 10;
     src |= srch >> 20;
     srch <<= 10;

     while (e1 < 1053) {
        srch >>= 1;
        if (src & 1)
           srch |= MSIGN;
        src >>= 1;
        e1 ++;
     }

     if (round && (srch & MSIGN) != 0)
        src++;
     if (s)
        src = (src ^ FMASK) + 1;
     *res = src;
     return 0;
}

/* Make single precsion number a double precision number */
int
makedr(uint32 *res, uint32 src, uint32 srch)
{
    int  e;            /* Hold the exponent */

    e =  (src & DEMSK) >> 20;
    if (e == 0) {
       *res = 0;
       return 0;
    }
    /* Adjust bias */
    e -= 1023;
    e += 127;
    src <<= 3;
    src |= srch >> 29;
    /* Check if out of range */
    *res = (src & MSIGN) | ((e << 23) & EMASK) | (src & MMASK);
    return 0;
}


/* Compare to floating point numbers */
int
drcomp(uint32 src1, uint32 src1h, uint32 src2,  uint32 src2h)
{
    int  e1, e2;        /* Hold the two exponents */
    int  temp;          /* Hold exponent difference */
    uint32  m1, m2, mh;

    /* Extract numbers and adjust */
    e1 = (src1 & DEMSK) >> 20;
    m1 = src1 & DMMSK;      /* extract mantissa */
    if (e1 != 0)        /* Add in hidden bit if needed */
       m1 |= DONE;
    e2 = (src2 & DEMSK) >> 20;
    m2 = src2 & DMMSK;      /* extract mantissa */
    if (e2 != 0)        /* Add in hidden bit if needed */
       m2 |= DONE;
    temp = e1 - e2;
    /* Align operands */
    if (temp > 0) {
        if (temp > 56) {
            m2 = src2h = 0;
        } else {
            /* Shift src2 right if src1 larger expo - expo */
            m2 >>= temp;
            while (temp-- != 0) {
                src2h >>= 1;
                if (m2 & 1)
                   src2h |= MSIGN;
                m2 >>= 1;
            }
        }
    } else if (temp < 0) {
        if (temp < -56) {
            m1 = src1h = 0;
        } else {
        /* Shift src1 right if src2 larger expo - expo */
            m1 >>= temp;
            while (temp-- != 0) {
                src1h >>= 1;
                if (m1 & 1)
                   src1h |= MSIGN;
                m1 >>= 1;
            }
        }
    }

    /* Exponents should be equal now. */
    if (src1 & MSIGN) {
        src1h = (src1h ^ FMASK) + 1;
        m1 = m1 ^ FMASK;
        if (src1h == 0)
            m1++;
    }

    if ((src2 & MSIGN) == 0) {
        src2h = (src2h ^ FMASK) + 1;
        m2 = m2 ^ FMASK;
        if (src2h == 0)
            m2++;
    }

    /* Add results */
    mh = src1h + src2h;
    m1 = m1 + m2;
    if (mh < src2h || mh < src1h)
        m1 ++;

    /* Compute result */
    if (m1 & MSIGN) {
        return FMASK;
    } else if (m1 != 0 && mh != 0) {
        return 1;
    }
    return 0;
}


/* Add two double precision numbers */
int
dradd(uint32 *resl, uint32 *resh, uint32 src1, uint32 src1h, uint32 src2, uint32 src2h)
{
    int  e1, e2;        /* Hold the two exponents */
    int  s;             /* Hold resulting sign */
    int  temp;          /* Hold exponent difference */
    uint32  m1, m2, mh;

    /* Extract numbers and adjust */
    e1 = (src1 & DEMSK) >> 20;
    m1 = src1 & DMMSK;      /* extract mantissa */
    if (e1 != 0)        /* Add in hidden bit if needed */
       m1 |= DONE;
    e2 = (src2 & DEMSK) >> 20;
    m2 = src2 & DMMSK;      /* extract mantissa */
    if (e2 != 0)        /* Add in hidden bit if needed */
       m2 |= DONE;
    temp = e1 - e2;
//printf(" %08x %08x  %08x %08x  %d %d %d\n", m1, src1h, m2, src2h, e1, e2, temp);
    /* Align operands */
    if (temp > 0) {
        if (temp > 56) {
            m2 = src2h = 0;
        } else {
            /* Shift src2 right if src1 larger expo - expo */
            while (temp-- != 0) {
                src2h >>= 1;
                if (m2 & 1)
                   src2h |= MSIGN;
                m2 >>= 1;
            }
        }
    } else if (temp < 0) {
        if (temp < -27) {
            m1 = src1h = 0;
        } else {
        /* Shift src1 right if src2 larger expo - expo */
            while (temp-- != 0) {
                src1h >>= 1;
                if (m1 & 1)
                   src1h |= MSIGN;
                m1 >>= 1;
            }
        }
    }

    /* Exponents should be equal now. */
    if ((src1 & MSIGN) != 0) {
        src1h = (src1h ^ FMASK) + 1;
        m1 = m1 ^ FMASK;
        if (src1h == 0)
            m1++;
    }

    if ((src2 & MSIGN) != 0) {
        src2h = (src2h ^ FMASK) + 1;
        m2 = m2 ^ FMASK;
        if (src2h == 0)
            m2++;
    }

    /* Add results */
    mh = src1h + src2h;
    m1 = m1 + m2;
    if (mh < src1h || mh < src2h)
        m1 ++;

//printf("s %08x %08x \n", m1, mh);
    /* figure sign */
    if (m1 & MSIGN) {
        mh = (mh ^ FMASK) + 1;
        m1 = m1 ^ FMASK;
        if (mh == 0)
            m1++;
        s = 1;
    } else {
        s = 0;
    }

    /* Handle overflow */
    while ((m1 & DCMSK) != 0) {
        mh >>= 1;
        if (m1 & 1)
            mh |= MSIGN;
        m1 >>= 1;
        e1++;
    }

//printf("n  %08x %08x %d \n", m1, mh, e1);
    /* Exit if zero result */
    if (m1 == 0 && mh == 0) {
       *resl = ((s)?MSIGN:0);
       *resh = 0;
       return 0;
    }

    /* Normalize result */
    while ((m1 & DONE) == 0) {
        m1 <<= 1;
        if (mh & MSIGN)
            m1 |= 1;
        mh <<= 1;
        e1--;
    }
//printf("f %08x %08x %d \n", m1, mh, e1);

    *resl = ((s)?MSIGN:0) | ((e1 << 20) & DEMSK) | (m1 & DMMSK);
    *resh = mh;

//printf("r %08x %08x\n", *resl, *resh);
    if (e1 > 1023)
       return 18;
    else if (e1 < 0)
       return 19;
    return 0;
}

/* Multiply two double precision numbers */
int
drmult(uint32 *resl, uint32 *resh, uint32 src1, uint32 src1h, uint32 src2, uint32 src2h)
{
    int  e1, e2;        /* Hold the two exponents */
    int  s = 0;         /* Hold resulting sign */
    int  temp;          /* Hold exponent difference */
    uint32  m1, m2;
    uint32  dest, desth;

    /* Extract numbers and adjust */
    e1 = (src1 & DEMSK) >> 20;
    if ((src1 & MSIGN) != (src2 & MSIGN))
       s = 1;
    m1 = src1 & DMMSK;      /* extract mantissa */
    if (e1 != 0)        /* Add in hidden bit if needed */
       m1 |= DONE;
    e2 = (src2 & DEMSK) >> 20;
    m2 = src2 & DMMSK;      /* extract mantissa */
    if (e2 != 0)        /* Add in hidden bit if needed */
       m2 |= DONE;
    temp = e1 - e2;

    /* Compute exponent */
    e1 = e1 + e2 - 1022;
//printf(" %08x %08x  %08x %08x  %d %d %d\n", m1, src1h, m2, src2h, e1, e2, temp);

    dest = desth = 0;

    /* Do multiply */
    for (temp = 0; temp < 53; temp++) {
         /* Add if we need too */
         if (src1h & 1) {
             desth += src2h;
             dest += m2;
             if (desth < src2h)
                 dest ++;
         }
         /* Shift right by one */
         src1h >>= 1;
         if (m1 & 1)
            src1h |= MSIGN;
         m1 >>= 1;
         desth >>= 1;
         if (dest & 1)
            desth |= MSIGN;
         dest >>= 1;
//printf("m %08x %08x : %08x %08x -> %08x %08x\n", m1, src1h, m2, src2h, dest, desth);
    }

    /* Handle overflow */
    while ((dest & DCMSK) != 0) {
        desth >>= 1;
        if (dest & 1)
           desth |= MSIGN;
        dest >>= 1;
        e1++;
    }

//printf("n  %08x %08x %d \n", dest, desth, e1);
    /* Exit if zero result */
    if (dest == 0 && desth == 0) {
       *resl = ((s)?MSIGN:0);
       *resh = 0;
       return 0;
    }

    /* Normalize result */
    while ((dest & DONE) == 0) {
        dest <<= 1;
        if (desth & MSIGN)
           dest |= 1;
        desth <<= 1;
        e1--;
    }

//printf("f  %08x %08x %d \n", dest, desth, e1);
    *resl = ((s)?MSIGN:0) | ((e1 << 20) & DEMSK) | (dest & DMMSK);
    *resh = desth;
//printf("r  %08x %08x\n", *resl, *resh);

    if (e1 > 1023)
       return 1;
    else if (e1 < 0)
       return 2;
    return 0;
}

/* Divide two double precision numbers */
int
drdiv(uint32 *resl, uint32 *resh, uint32 src1, uint32 src1h, uint32 src2, uint32 src2h)
{
    int  e1, e2;        /* Hold the two exponents */
    int  s = 0;         /* Hold resulting sign */
    int  temp;          /* Hold exponent difference */
    uint32  m1, m2;
    uint32  dest, desth;

    /* Extract numbers and adjust */
    e1 = (src1 & DEMSK) >> 20;
    if ((src1 & MSIGN) != (src2 & MSIGN))
       s = 1;
    m1 = src1 & DMMSK;      /* extract mantissa */
    if (e1 != 0)        /* Add in hidden bit if needed */
       m1 |= DONE;
    e2 = (src2 & DEMSK) >> 20;
    m2 = src2 & DMMSK;      /* extract mantissa */
    if (e2 != 0)        /* Add in hidden bit if needed */
       m2 |= DONE;
    temp = e1 - e2;

    /* Compute exponent */
    e1 = e1 - e2 + 1022;
    /* Do divide */
    /* Change sign of src2 so we can add */
    src2h ^= FMASK;
    m2 ^= XMASK;
    if (src2h == FMASK)
       m2 ++;
    src2h ++;
    dest = desth = 0;
    /* Do divide */
    for (temp = 53; temp > 0; temp--) {
         uint32    tlow, thigh;

         /* Shift left by one */
         m1 <<= 1;
         if (src1h & MSIGN)
             m1 |= 1;
         src1h <<= 1;
         /* Subtract dividend from remainder */
         thigh = src1h + src2h;
         tlow = m1 + src2;
         if (thigh < src2h)
             tlow ++;

         /* Shift quotent left one bit */
         dest <<= 1;
         if (desth & MSIGN)
             dest |= 1;
         desth <<= 1;

         /* If remainder larger then divisor replace */
         if ((tlow & CMASK) != 0) {
             m1 = tlow;
             src1h = thigh;
             desth |= 1;
         }
    }

    /* Compute one final set to see if rounding needed */
    /* Shift left by one */
    m1 <<= 1;
    if (src1h & MSIGN)
        m1 |= 1;
    src1h <<= 1;
    /* Subtract remainder to dividend */
    src1h += src2h;
    m1 += src2;
    if (src1h <  src2h)
        m1++;

    /* If .5 off, round */
    if (m1 & MSIGN) {
        if (desth == FMASK)
            dest++;
        desth++;
    }

    /* Handle overflow */
    while ((dest & DCMSK) != 0) {
        desth >>= 1;
        if (dest & 1)
           desth |= MSIGN;
        dest >>= 1;
        e1++;
    }

//printf("n  %08x %08x %d \n", dest, desth, e1);
    /* Exit if zero result */
    if (dest == 0 && desth == 0) {
       *resl = ((s)?MSIGN:0);
       *resh = 0;
       return 0;
    }

    /* Normalize result */
    while ((dest & DONE) == 0) {
        dest <<= 1;
        if (desth & MSIGN)
           dest |= 1;
        desth <<= 1;
        e1--;
    }

//printf("f  %08x %08x %d \n", dest, desth, e1);
    *resl = ((s)?MSIGN:0) | ((e1 << 20) & DEMSK) | (dest & DMMSK);
    *resh = desth;
//printf("r  %08x %08x\n", *resl, *resh);

    if (e1 > 1023)
       return 1;
    else if (e1 < 0)
       return 2;
    return 0;
}
#ifdef TEST

typedef union {
   uint32   fr;
   float    f;
} FLOAT;

typedef union {
   struct {  uint32    h; uint32 l; } r;
   uint64_t  dr;
   double    d;
} DFLOAT;


FLOAT Zero;
FLOAT Half;
FLOAT One;
FLOAT Two;
FLOAT Three;
FLOAT Four;
FLOAT Five;
FLOAT Eight;
FLOAT Nine;
FLOAT TwentySeven;
FLOAT ThirtyTwo;
FLOAT TwoForty;
FLOAT MinusOne;
FLOAT Half;
FLOAT OneAndHalf;

DFLOAT DZero;
DFLOAT DHalf;
DFLOAT DOne;
DFLOAT DTwo;
DFLOAT DThree;
DFLOAT DFour;
DFLOAT DFive;
DFLOAT DEight;
DFLOAT DNine;
DFLOAT DTwentySeven;
DFLOAT DThirtyTwo;
DFLOAT DTwoForty;
DFLOAT DMinusOne;
DFLOAT DHalf;
DFLOAT DOneAndHalf;
#define PRINT(a, b) (void)makerd(&rl, &rh, b.fr); temp.r.h = rh; temp.r.l = rl; \
       printf("V= %f %f\n", a, temp.d);
#define DPRINT(a, b) printf("V= %f %f\n", a, b.d);

int
main(int argc, char *argv)
{

    uint32 v;
    int    i;
    uint32 r;
    uint32 r2;
    uint32 rl, rh;
    uint32 hlf = 0xBf400000;
    uint32 one = 0x3f800000;
    FLOAT stmp;
    DFLOAT temp;

    Zero.fr = 0;
    One.fr = 0x3f800000;
    DZero.r.l = 0;
    DZero.r.h = 0;
    DOne.r.l = 0x3ff00000;
    DOne.r.h = 0;

    (void)radd(&Two.fr, One.fr, One.fr);     /* Two = One + One */
    (void)radd(&Three.fr, Two.fr, One.fr);   /* Three = Two + One */
    (void)radd(&Four.fr, Three.fr, One.fr);  /* Four = Three + One */
    (void)radd(&Five.fr, Four.fr, One.fr);   /* Five = Four + One */
    (void)radd(&Eight.fr, Four.fr, Four.fr); /* Eight = Four + Four */
    (void)rmult(&Nine.fr, Three.fr, Three.fr); /* Nine = Three * Three */
    (void)rmult(&TwentySeven.fr, Nine.fr, Three.fr); /* TwentySeven = Nine * Three */
    (void)rmult(&ThirtyTwo.fr, Four.fr, Eight.fr); /* ThirtyTwo = Four * Eight */
    (void)rmult(&stmp.fr, Four.fr, Five.fr); /* TwoForty = Four * Five * Three * Four */
    (void)rmult(&stmp.fr, stmp.fr, Three.fr);
    (void)rmult(&TwoForty.fr, stmp.fr, Four.fr);
    MinusOne.fr = MSIGN ^ One.fr;            /* MinusOne = -One */
    (void)rdiv(&Half.fr, One.fr, Two.fr);    /* Half = One / Two */
    (void)radd(&OneAndHalf.fr, One.fr, Half.fr); /* OneAndHalf = One + Half */

    PRINT(0.0f, Zero);
    PRINT(1.0f, One);
    PRINT(2.0f, Two);
    PRINT(3.0f, Three);
    PRINT(4.0f,  Four);
    PRINT(5.0f,  Five);
    PRINT(8.0f,  Eight);
    PRINT(9.0f,  Nine);
    PRINT(27.0f, TwentySeven);
    PRINT(32.0f, ThirtyTwo);
    PRINT(240.0, TwoForty);
    PRINT(-1.0f,  MinusOne);
    PRINT(0.5f, Half);
    PRINT(1.5f, OneAndHalf);

    (void)radd(&stmp.fr, Zero.fr, Zero.fr);
    r = rcomp(stmp.fr, Zero.fr);
    if (r != 0)
        printf("0+0 != 0\n");
    (void)radd(&stmp.fr, One.fr, MSIGN ^ One.fr);
    r = rcomp(stmp.fr, Zero.fr);
    if (r != 0)
        printf("1-1 != 0\n");
    r = rcomp(One.fr, Zero.fr);
    if (r != 1)
        printf("1 <= 0  %d\n", r);
    (void)radd(&stmp.fr, One.fr, One.fr);
    r = rcomp(stmp.fr, Two.fr);
    if (r != 0)
        printf("1+1 != 2\n");
    r = rcomp(Zero.fr, MSIGN ^ Zero.fr);
    if (r != 0)
        printf("0 != -0\n");

    (void)radd(&stmp.fr, Two.fr, One.fr);
    r = rcomp(stmp.fr, Three.fr);
    if (r != 0)
        printf("2+1 != 3\n");
    (void)radd(&stmp.fr, Three.fr, One.fr);
    r = rcomp(stmp.fr, Four.fr);
    if (r != 0)
        printf("3+1 != 4\n");
    (void)rmult(&stmp.fr, Two.fr, MSIGN ^ Two.fr);
    (void)radd(&stmp.fr, stmp.fr, Four.fr);
    r = rcomp(stmp.fr, Zero.fr);
    if (r != 0)
        printf("4+2*(-2) != 0\n");
    (void)radd(&stmp.fr, Four.fr, MSIGN ^ Three.fr);
    (void)radd(&stmp.fr, stmp.fr, MSIGN ^ One.fr);
    r = rcomp(stmp.fr, Zero.fr);
    if (r != 0)
        printf("4-3-1 != 0\n");
    for(i = 0; i < 31; i++) {
       v = 1 << i;
       (void) dfloat( &temp.r.l, &temp.r.h, v);
       (void) dradd(&temp.r.l, &temp.r.h, temp.r.l, temp.r.h, DOne.r.l, DOne.r.h);
//       (void) rdiv( &r, r2, hlf);
 //      (void) makerd( &rl, &rh, r);
  //     x.dr = ((uint64_t)rl << 32)  | (uint64_t)rh;
   //    printf("%d v=%15d %08x r=%08x %14f %016lx ", i, v, v, r, x.d, x.dr);
    //   (void) rfix( &r2, r, 0);
    //   temp.r.h |= MSIGN;
     //  temp.r.l |= 1;
       (void) dfix ( &r2, temp.r.l, temp.r.h, 0);
       printf("%d r=%15d %08x %08x %f -> %d\n", i, v, temp.r.l, temp.r.h, temp.d, r2);
   }

#define DADD(a, b, c)  (void)dradd(&a.r.l, &a.r.h, b.r.l, b.r.h, c.r.l, c.r.h)
#define DMUL(a, b, c)  (void)drmult(&a.r.l, &a.r.h, b.r.l, b.r.h, c.r.l, c.r.h)
#define DDIV(a, b, c)  (void)drdiv(&a.r.l, &a.r.h, b.r.l, b.r.h, c.r.l, c.r.h)
    DPRINT(0.0f, DZero);
    DPRINT(1.0f, DOne);
    DADD(DTwo, DOne, DOne);     /* Two = One + One */
    DPRINT(2.0f, DTwo);
    DADD(DThree, DTwo, DOne);   /* Three = Two + One */
    DPRINT(3.0f, DThree);
    DADD(DFour, DThree, DOne);  /* Four = Three + One */
    DPRINT(4.0f, DFour);
    DADD(DFive, DFour, DOne);   /* Five = Four + One */
    DPRINT(5.0f, DFive);
    DADD(DEight, DFour, DFour); /* Eight = Four + Four */
    DPRINT(8.0f, DEight);
    DMUL(DNine, DThree, DThree); /* Nine = Three * Three */
    DPRINT(9.0f, DNine);
    DMUL(DTwentySeven, DNine, DThree); /* TwentySeven = Nine * Three */
    DPRINT(27.0f, DTwentySeven);
    DMUL(DThirtyTwo, DFour, DEight); /* ThirtyTwo = Four * Eight */
    DPRINT(32.0f, DThirtyTwo);
    DMUL(temp, DFour, DFive); /* TwoForty = Four * Five * Three * Four */
    DMUL(temp, temp, DThree);
    DMUL(DTwoForty, temp, DFour);
    DPRINT(240.0, DTwoForty);
    DMinusOne.r.l = MSIGN ^ DOne.r.l;            /* MinusOne = -One */
    DMinusOne.r.h = DOne.r.h;
    DPRINT(-1.0f, DMinusOne);
    DDIV(DHalf, DOne, DTwo);    /* Half = One / Two */
    DPRINT(0.5f, DHalf);
    DADD(DOneAndHalf, DOne, DHalf); /* OneAndHalf = One + Half */
    DPRINT(1.5f, DOneAndHalf);
}
#endif
