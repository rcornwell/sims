/* ka10_df.c: DF10 common routines.

   Copyright (c) 2016, Richard Cornwell

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

#include "ka10_defs.h"

void df10_setirq(struct df10 *df) {
      df->status |= PI_ENABLE;
      set_interrupt(df->devnum, df->status);
}

void df10_writecw(struct df10 *df) {
      df->status |= 1 << df->ccw_comp;
      M[df->cia|1] = (((uint64)(df->ccw)) << CSHIFT) | ((uint64)df->cda);
}

void df10_finish_op(struct df10 *df, int flags) {
      df->status &= ~BUSY;
      df->status |= flags;
      df10_writecw(df);
      df10_setirq(df);
}

void df10_setup(struct df10 *df, uint32 addr) {
      df->cia = addr & ICWA;
      df->ccw = df->cia;
      df->wcr = 0;
      df->status |= BUSY;
}

int df10_fetch(struct df10 *df) {
      uint64 data;
      if (df->ccw > MEMSIZE) {
           df10_finish_op(df, df->nxmerr);
           return 0;
      }
      data = M[df->ccw];
      while((data & (WMASK << CSHIFT)) == 0) {
          if ((data & AMASK) == 0) {
                df10_finish_op(df,0);
                return 0;
          }
          df->ccw = data & AMASK;
          if (df->ccw > MEMSIZE) {
                df10_finish_op(df, 1<<df->nxmerr);
                return 0;
          }
          data = M[df->ccw];
      }
      df->wcr = (data >> CSHIFT) & WMASK;
      df->cda = data &AMASK;
      df->ccw = (df->ccw + 1) & AMASK;
      return 1;
}

int df10_read(struct df10 *df) {
     uint64 data;
     if (df->wcr == 0) {
          if (!df10_fetch(df))
                return 0;
     }
    df->wcr = (df->wcr + 1) & WMASK;
     if (df->cda != 0) {
       if (df->cda > MEMSIZE) {
           df10_finish_op(df, 1<<df->nxmerr);
           return 0;
       }
       df->cda = (df->cda + 1) & AMASK;
       data = M[df->cda];
    } else {
       data = 0;
    }
    df->buf = data;
    if (df->wcr == 0) {
        return df10_fetch(df);
    }
    return 1;
}

int df10_write(struct df10 *df) {
     if (df->wcr == 0) {
          if (!df10_fetch(df))
                return 0;
     }
     df->wcr = (df->wcr + 1) & WMASK;
     if (df->cda != 0) {
       if (df->cda > MEMSIZE) {
           df10_finish_op(df, 1<<df->nxmerr);
           return 0;
       }
       df->cda = (df->cda + 1) & AMASK;
       M[df->cda] = df->buf;
     }
     if (df->wcr == 0) {
         return df10_fetch(df);
     }
     return 1;
}
