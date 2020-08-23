/* ridge32_defs.h: Ridge 32 simulator definitions

   Copyright (c) 2019, Richard Cornwell

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "sim_defs.h"                     /* simulator defns */

/* Simulator stop codes */
#define STOP_HALT       1                               /* halted */
#define STOP_IBKPT      2                               /* breakpoint */

/* Conditional error returns */

/* Memory */
#define MAXMEMSIZE        (8*1024*1024)       /* max memory size */
#define PAMASK            (MAXMEMSIZE - 1)    /* physical addr mask */
#define MEMSIZE           (cpu_unit.capac)    /* actual memory size */
#define MEM_ADDR_OK(x)    (((x)) < MEMSIZE)
extern uint32    *M;


/* Opcode definitions */
#define OP_MOVE           0x01
#define OP_NEG            0x02
#define OP_ADD            0x03
#define OP_SUB            0x04
#define OP_MPY            0x05
#define OP_DIV            0x06
#define OP_REM            0x07
#define OP_NOT            0x08
#define OP_OR             0x09
#define OP_XOR            0x0A
#define OP_AND            0x0B
#define OP_CBIT           0x0C
#define OP_SBIT           0x0D
#define OP_TBIT           0x0E
#define OP_CHK            0x0F
#define OP_NOP            0x10
#define OP_MOVEI          0x11
#define OP_ADDI           0x13
#define OP_SUBI           0x14
#define OP_MPYI           0x15
#define OP_NOTI           0x18
#define OP_ANDI           0x1B
#define OP_CHKI           0x1F
#define OP_FIXT           0x20
#define OP_FIXR           0x21
#define OP_RNEG           0x22
#define OP_RADD           0x23
#define OP_RSUB           0x24
#define OP_RMPY           0x25
#define OP_RDIV           0x26
#define OP_MAKERD         0x27
#define OP_LCOMP          0x28
#define OP_FLOAT          0x29
#define OP_RCOMP          0x2A
#define OP_EADD           0x2C
#define OP_ESUB           0x2D
#define OP_EMPY           0x2E
#define OP_EDIV           0x2F
#define OP_DFIXT          0x30
#define OP_DFIXR          0x31
#define OP_DRNEG          0x32
#define OP_DRADD          0x33
#define OP_DRSUB          0x34
#define OP_DRMPY          0x35
#define OP_DRDIV          0x36
#define OP_MAKEDR         0x37
#define OP_DCOMP          0x38
#define OP_DFLOAT         0x39
#define OP_DRCOMP         0x3A
#define OP_TRAP           0x3B
#define OP_SUS            0x40
#define OP_LUS            0x41
#define OP_RUM            0x42
#define OP_LDREGS         0x43
#define OP_TRANS          0x44
#define OP_DIRT           0x45
#define OP_MOVESR         0x46
#define OP_MOVERS         0x47
#define OP_MAINT          0x4C
#define OP_READ           0x4E
#define OP_WRITE          0x4F
#define OP_CALLR          0x53
#define OP_RET            0x57
#define OP_KCALL          0x5B
#define OP_LSL            0x60
#define OP_LSR            0x61
#define OP_ASL            0x62
#define OP_ASR            0x63
#define OP_DLSL           0x64
#define OP_DLSR           0x65
#define OP_CSL            0x68
#define OP_SEB            0x6A
#define OP_LSLI           0x70
#define OP_LSRI           0x71
#define OP_ASLI           0x72
#define OP_ASRI           0x73
#define OP_DLSLI          0x74
#define OP_DLSRI          0x75
#define OP_CSLI           0x78
#define OP_SEH            0x7A

/* Device context block */
struct ridge_dib {
    uint8               dev_num;                        /* device address */
    uint8               slot_num;                       /* Slot number */
    int                 (*io_read)(uint32 dev, uint32 *data);
    int                 (*io_write)(uint32 dev, uint32 data);
    int                 (*io_iord)(uint32 *data);
    int                 dev_mask;                       /* Mask for device */
};

typedef struct ridge_dib DIB;

/* Debuging controls */
#define DEBUG_CMD       0x0000001       /* Show device commands */
#define DEBUG_DATA      0x0000002       /* Show data transfers */
#define DEBUG_DETAIL    0x0000004       /* Show details */
#define DEBUG_EXP       0x0000008       /* Show error conditions */
#define DEBUG_TRAP      0x0000010       /* Show Trap requests */
#define DEBUG_INST      0x0000020       /* Show instruction execution */

#define DCB         u3              /* DCB pointer */

extern DEBTAB       dev_debug[];
extern DEVICE       cpu_dev;
extern DEVICE       flp_dev;
extern DEVICE       dsk_dev;
extern DEVICE       ct_dev;
extern DEVICE       mono_dev;
extern uint8        ext_irq;
extern UNIT         cpu_unit;
extern int32        tmxr_poll;

void   cpu_boot(int);
uint8  io_dcbread_byte(UNIT *uptr, int off);
uint16 io_dcbread_half(UNIT *uptr, int off);
uint32 io_dcbread_addr(UNIT *uptr, int off);
void   io_dcbread_blk(UNIT *uptr, int off, uint8 *data, int sz);
void   io_read_blk(int addr, uint8 *data, int sz);
void   io_dcbwrite_byte(UNIT *uptr, int off, uint8 data);
void   io_dcbwrite_half(UNIT *uptr, int off, uint16 data);
void   io_dcbwrite_addr(UNIT *uptr, int off, uint32 data);
void   io_dcbwrite_blk(UNIT *uptr, int off, uint8 *data, int sz);
void   io_write_blk(int addr, uint8 *data, int sz);
int    io_read(uint32 dev_data, uint32 *data);
int    io_write(uint32 dev_data, uint32 data);
int    io_rd(uint32 *data);
t_stat chan_set_devs();
t_stat set_dev_addr(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_dev_addr(FILE * st, UNIT * uptr, int32 v, CONST void *desc);
t_stat set_slot_num(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_slot_num(FILE * st, UNIT * uptr, int32 v, CONST void *desc);
void   fprint_inst(FILE *, t_addr addr, t_value *);

