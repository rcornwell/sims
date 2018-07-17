/* ka10_ten11.c: Rubin 10-11 interface.

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

   This is a device which interfaces with eight Unibuses.  It's
   specific to the MIT AI lab PDP-10.
*/

#include "ka10_defs.h"

/* Rubin 10-11 pager. */
static uint64 ten11_pager[256];

/* Physical address of 10-11 control page. */
#define T11CPA          03776000

/* Bits in a 10-11 page table entry. */
#define T11VALID        (0400000000000LL)
#define T11WRITE        (0200000000000LL)
#define T11PDP11        (0003400000000LL)
#define T11ADDR         (0000377776000LL)
#define T11LIMIT        (0000000001777LL)

/* Simulator time units for a Unibus memory cycle. */
#define UNIBUS_MEM_CYCLE 100


int ten11_read (int addr)
{
  int offset = addr & 01777;
  int data;

  if (addr >= T11CPA) {
    /* Accessing the control page. */
    if (offset >= 0400) {
      sim_debug (DEBUG_TEN11, &cpu_dev,
                 "Control page read NXM: %o @ %o\r\n",
                 offset, PC);
      return 1;
    }
    MB = ten11_pager[offset];
  } else {
    /* Accessing a memory page. */
    int page = (addr >> 10) & 0377;
    uint64 mapping = ten11_pager[page];
    int unibus, uaddr, limit;

    limit = mapping & T11LIMIT;
    if ((mapping & T11VALID) == 0 || offset > limit) {
      sim_debug (DEBUG_TEN11, &cpu_dev,
                 "(%o) %07o >= 4,,000000 / %llo / %o > %o\r\n",
                 page, addr, (mapping & T11VALID), offset, limit);
      return 1;
    }

    unibus = (mapping & T11PDP11) >> 26;
    uaddr = ((mapping & T11ADDR) >> 10) + offset;
    uaddr <<= 2;

    // Get MB from Unibus.
    MB = 0;
    
    sim_debug (DEBUG_TEN11, &cpu_dev,
               "Read: (%o) %06o -> %012llo\r\n",
               unibus, uaddr, MB);
  }
  return 0;
}

int ten11_write (int addr)
{
  int offset = addr & 01777;

  if (addr >= T11CPA) {
    /* Accessing the control page. */
    if (offset >= 0400) {
      sim_debug (DEBUG_TEN11, &cpu_dev,
                 "Control page write NXM: %o @ %o\r\n",
                 offset, PC);
      return 1;
    }
    ten11_pager[offset] = MB;
    sim_debug (DEBUG_TEN11, &cpu_dev,
               "Page %03o: %s %s (%llo) %06llo/%04llo\r\n",
               offset,
               (MB & T11VALID) ? "V" : "I",
               (MB & T11WRITE) ? "RW" : "R",
               (MB & T11PDP11) >> 26,
               (MB & T11ADDR) >> 10,
               (MB & T11LIMIT));
  } else {
    /* Accessing a memory page. */
    int page = (addr >> 10) & 0377;
    uint64 mapping = ten11_pager[page];
    int unibus, uaddr, limit;
    limit = mapping & T11LIMIT;
    if ((mapping & T11VALID) == 0 || offset > limit) {
      sim_debug (DEBUG_TEN11, &cpu_dev,
                 "(%o) %07o >= 4,,000000 / %llo / %o > %o\r\n",
                 page, addr, (mapping & T11VALID), offset, limit);
      return 1;
    }
    unibus = (mapping & T11PDP11) >> 26;
    uaddr = ((mapping & T11ADDR) >> 10) + offset;
    uaddr <<= 2;
    sim_debug (DEBUG_TEN11, &cpu_dev,
               "Write: (%o) %06o <- %012llo\r\n",
               unibus, uaddr, MB);

    // Send MB to Unibus.
  }
  return 0;
}
