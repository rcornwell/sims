/* ridge32_sys.c: Ridge 32 Simulator system interface.

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

#include "ridge32_defs.h"
#include <ctype.h>
#include "sim_imd.h"

extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern uint32 *M;

/* SCP data structures and interface routines

   sim_name            simulator name string
   sim_PC              pointer to saved PC register descriptor
   sim_emax            number of words for examine
   sim_devices         array of pointers to simulated devices
   sim_stop_messages   array of pointers to stop messages
   sim_load            binary loader
*/

char sim_name[] = "Ridge 32";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 16;

DEVICE *sim_devices[] = {
       &cpu_dev,
       &flp_dev,
       &dsk_dev,
       &ct_dev,
       &mono_dev,
       NULL
};


/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"TRAP", DEBUG_TRAP, "Show trap information"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"INST", DEBUG_INST, "Show instruction execution"},
    {0, 0}
};


const char *sim_stop_messages[] = {
       "Unknown error",
       "HALT",
       "Breakpoint",
};



/* Load a image file into memory.  */

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    uint8         buf[1024];
    size_t        len;
    size_t        i;
    uint32        addr = 0x3a000;
    uint32        data;
    uint32        mask;
    uint32        pa;
    int           offset;

    while((len = fread(&buf[0], 1, sizeof(buf), fileref)) > 0) {
       for (i = 0; i < len; i++) {
           offset = 8 * (3 - (addr & 0x3));
           pa = addr >> 2;
           mask = 0xff;
           data = buf[i];
           data <<= offset;
           mask <<= offset;
           M[pa] &= ~mask;
           M[pa] |= data;
           fprintf(stderr, "%02x ", buf[i]);
           addr++;
           if ((i & 0xf) == 0xf)
              fprintf(stderr, "\n %06x ", addr);
       }
    }
#if 0
    DISK_INFO    *disk;
    uint32        flags;
    disk = diskOpen(fileref, TRUE);

    fprintf(stderr, " %06x ", addr);
    for (sect = 1; sect < 17; sect++) {
        if (sectRead(disk, 2, 0, sect, buf, 1024, &flags, &len) == SCPE_OK) {
           for (i = 0; i <len; i++) {
               offset = 8 * (3 - (addr & 0x3));
               pa = addr >> 2;
               mask = 0xff;
               data = buf[i];
               data <<= offset;
               mask <<= offset;
               M[pa] &= ~mask;
               M[pa] |= data;
               fprintf(stderr, "%02x ", buf[i]);
               addr++;
               if ((i & 0xf) == 0xf)
                  fprintf(stderr, "\n %06x ", addr);
            }
         } else {
             fprintf(stderr, "Read error %d\n", sect);
         }
    }
    

    diskClose(&disk);
#endif

return SCPE_OK;
}

/* Symbol tables */
typedef struct _opcode {
       uint8    opbase;
       char     *name;
       uint8    type;
} t_opcode;

#define RZ       0          /* Zero register */
#define R1       1          /* One register */
#define RR       2          /* Register to register */
#define RI       3          /* Short immediate to register */
#define RX       4          /* Register index */
#define RN       5          /* Number */
#define M        6          /* Maint instruction */
#define IND      0x8        /* Indexed */
#define COND     0x10       /* Conditional */
#define PCREL    0x20       /* PC Relative */
#define SHORT    0x40       /* Short displacement */
#define LONG     0x80       /* Long displacment */

t_opcode  optab[] = {
       { OP_MOVE,       "MOVE",         RR, },
       { OP_NEG,        "NEG",          RR, },
       { OP_ADD,        "ADD",          RR, },
       { OP_SUB,        "SUB",          RR, },
       { OP_MPY,        "MPY",          RR, },
       { OP_DIV,        "DIV",          RR, },
       { OP_REM,        "REM",          RR, },
       { OP_NOT,        "NOT",          RR, },
       { OP_OR,         "OR",           RR, },
       { OP_XOR,        "XOR",          RR, },
       { OP_AND,        "AND",          RR, },
       { OP_CBIT,       "CBIT",         RR, },
       { OP_SBIT,       "SBIT",         RR, },
       { OP_TBIT,       "TBIT",         RR, },
       { OP_CHK,        "CHK",          RR, },
       { OP_NOP,        "NOP",          RR, },
       { OP_MOVEI,      "MOVEI",        RI, },
       { OP_ADDI,       "ADDI",         RI, },
       { OP_SUBI,       "SUBI",         RI, },
       { OP_MPYI,       "MPYI",         RI, },
       { OP_NOTI,       "NOTI",         RI, },
       { OP_ANDI,       "ANDI",         RI, },
       { OP_CHKI,       "CHKI",         RI, },
       { OP_FIXT,       "FIXT",         RR, },
       { OP_FIXR,       "FIXR",         RR, },
       { OP_RNEG,       "RNEG",         RR, },
       { OP_RADD,       "RADD",         RR, },
       { OP_RSUB,       "RSUB",         RR, },
       { OP_RMPY,       "RMPY",         RR, },
       { OP_RDIV,       "RDIV",         RR, },
       { OP_MAKERD,     "MAKERD",       RR, },
       { OP_LCOMP,      "LCOMP",        RR, },
       { OP_FLOAT,      "FLOAT",        RR, },
       { OP_RCOMP,      "RCOMP",        RR, },
       { OP_EADD,       "EADD",         RR, },
       { OP_ESUB,       "ESUB",         RR, },
       { OP_EMPY,       "EMPY",         RR, },
       { OP_EDIV,       "EDIV",         RR, },
       { OP_DFIXT,      "DFIXT",        RR, },
       { OP_DFIXR,      "DFIXR",        RR, },
       { OP_DRNEG,      "DRNEG",        RR, },
       { OP_DRADD,      "DRADD",        RR, },
       { OP_DRSUB,      "DRSUB",        RR, },
       { OP_DRMPY,      "DRMPY",        RR, },
       { OP_DRDIV,      "DRDIV",        RR, },
       { OP_MAKEDR,     "MAKEDR",       RR, },
       { OP_DCOMP,      "DCOMP",        RR, },
       { OP_DFLOAT,     "DFLOAT",       RR, },
       { OP_DRCOMP,     "DRCOMP",       RR, },
       { OP_TRAP,       "TRAP",         RN, },
       { OP_SUS,        "SUS",          RR, },
       { OP_LUS,        "LUS",          RR, },
       { OP_RUM,        "RUM",          RZ, },
       { OP_LDREGS,     "LDREGS",       RR, },
       { OP_TRANS,      "TRANS",        RR, },
       { OP_DIRT,       "DIRT",         RR, },
       { OP_MOVESR,     "MOVESR",       RR, },
       { OP_MOVERS,     "MOVERS",       RR, },
       { OP_MAINT,      "",             M, },
       { OP_READ,       "READ",         RR, },
       { OP_WRITE,      "WRITE",        RR, },
       { 0x50,          "TEST",         RR|COND, },
       { 0x51,          "TEST",         RR|COND, },
       { 0x52,          "TEST",         RR|COND, },
       { 0x54,          "TESTI",        RI|COND, },
       { 0x55,          "TESTI",        RI|COND, },
       { 0x56,          "TESTI",        RI|COND, },
       { 0x58,          "TEST",         RR|COND, },
       { 0x59,          "TEST",         RR|COND, },
       { 0x5A,          "TEST",         RR|COND, },
       { 0x5C,          "TESTI",        RI|COND, },
       { 0x5D,          "TESTI",        RI|COND, },
       { 0x5E,          "TESTI",        RI|COND, },
       { OP_CALLR,      "CALLR",        RR, },
       { OP_RET,        "RET",          RR, },
       { OP_KCALL,      "KCALL",        RN, },
       { OP_LSL,        "LSL",          RR, },
       { OP_LSR,        "LSR",          RR, },
       { OP_ASL,        "ASL",          RR, },
       { OP_ASR,        "ASR",          RR, },
       { OP_DLSL,       "DLSL",         RR, },
       { OP_DLSR,       "DLSR",         RR, },
       { OP_CSL,        "CSL",          RR, },
       { OP_SEB,        "SEB",          RR, },
       { OP_LSLI,       "LSLI",         RI, },
       { OP_LSRI,       "LSRI",         RI, },
       { OP_ASLI,       "ASLI",         RI, },
       { OP_ASRI,       "ASRI",         RI, },
       { OP_DLSLI,      "DLSLI",        RI, },
       { OP_DLSRI,      "DLSRI",        RI, },
       { OP_CSLI,       "CSLI",         RI, },
       { OP_SEH,        "SEH",          RR, },
       { 0x80,          "BR",           RR|COND|PCREL|SHORT, },
       { 0x82,          "BR",           RR|COND|PCREL|SHORT, },
       { 0x83,          "CALL",         R1|PCREL|SHORT, },
       { 0x84,          "BR",           RR|COND|PCREL|SHORT, },
       { 0x85,          "BR",           RI|COND|PCREL|SHORT, },
       { 0x86,          "BR",           RI|COND|PCREL|SHORT, },
       { 0x87,          "LOOP",         RI|PCREL|SHORT, },
       { 0x88,          "BR",           RR|COND|PCREL|SHORT, },
       { 0x8A,          "BR",           RR|COND|PCREL|SHORT, },
       { 0x8B,          "BR",           RZ|PCREL|SHORT, },
       { 0x8C,          "BR",           RR|COND|PCREL|SHORT, },
       { 0x8D,          "BR",           RI|COND|PCREL|SHORT, },
       { 0x8E,          "BR",           RI|COND|PCREL|SHORT, },
       { 0x90,          "BR",           RR|COND|PCREL|LONG, },
       { 0x92,          "BR",           RR|COND|PCREL|LONG, },
       { 0x93,          "CALL",         R1|PCREL|LONG, },
       { 0x94,          "BR",           RR|COND|PCREL|LONG, },
       { 0x95,          "BR",           RI|COND|PCREL|LONG, },
       { 0x96,          "BR",           RI|COND|PCREL|LONG, },
       { 0x97,          "LOOP",         RI|PCREL|LONG, },
       { 0x98,          "BR",           RR|COND|PCREL|LONG, },
       { 0x9A,          "BR",           RR|COND|PCREL|LONG, },
       { 0x9B,          "BR",           RZ|PCREL|LONG, },
       { 0x9C,          "BR",           RR|COND|PCREL|LONG, },
       { 0x9D,          "BR",           RI|COND|PCREL|LONG, },
       { 0x9E,          "BR",           RI|COND|PCREL|LONG, },
       { 0xA0,          "STOREB",       RX|SHORT, },
       { 0xA1,          "STOREB",       RX|IND|SHORT, },
       { 0xA2,          "STOREH",       RX|SHORT, },
       { 0xA3,          "STOREH",       RX|IND|SHORT, },
       { 0xA6,          "STORE",        RX|SHORT, },
       { 0xA7,          "STORE",        RX|IND|SHORT, },
       { 0xA8,          "STORED",       RX|SHORT, },
       { 0xA9,          "STORED",       RX|IND|SHORT, },
       { 0xB0,          "STOREB",       RX|LONG, },
       { 0xB1,          "STOREB",       RX|IND|LONG, },
       { 0xB2,          "STOREH",       RX|LONG, },
       { 0xB3,          "STOREH",       RX|IND|LONG, },
       { 0xB6,          "STORE",        RX|LONG, },
       { 0xB7,          "STORE",        RX|IND|LONG, },
       { 0xB8,          "STORED",       RX|LONG, },
       { 0xB9,          "STORED",       RX|IND|LONG, },
       { 0xC0,          "LOADB",        RX|SHORT, },
       { 0xC1,          "LOADB",        RX|IND|SHORT, },
       { 0xC2,          "LOADH",        RX|SHORT, },
       { 0xC3,          "LOADH",        RX|IND|SHORT, },
       { 0xC6,          "LOAD",         RX|SHORT, },
       { 0xC7,          "LOAD",         RX|IND|SHORT, },
       { 0xC8,          "LOADD",        RX|SHORT, },
       { 0xC9,          "LOADD",        RX|IND|SHORT, },
       { 0xCE,          "LADDR",        RX|SHORT, },
       { 0xCF,          "LADDR",        RX|IND|SHORT, },
       { 0xD0,          "LOADB",        RX|LONG, },
       { 0xD1,          "LOADB",        RX|IND|LONG, },
       { 0xD2,          "LOADH",        RX|LONG, },
       { 0xD3,          "LOADH",        RX|IND|LONG, },
       { 0xD6,          "LOAD",         RX|LONG, },
       { 0xD7,          "LOAD",         RX|IND|LONG, },
       { 0xD8,          "LOADD",        RX|LONG, },
       { 0xD9,          "LOADD",        RX|IND|LONG, },
       { 0xDE,          "LADDR",        RX|LONG, },
       { 0xDF,          "LADDR",        RX|IND|LONG, },
       { 0xE0,          "LOADBP",       RX|PCREL|SHORT, },
       { 0xE1,          "LOADBP",       RX|IND|PCREL|SHORT, },
       { 0xE2,          "LOADHP",       RX|PCREL|SHORT, },
       { 0xE3,          "LOADHP",       RX|IND|PCREL|SHORT, },
       { 0xE6,          "LOADP",        RX|PCREL|SHORT, },
       { 0xE7,          "LOADP",        RX|IND|PCREL|SHORT, },
       { 0xE8,          "LOADDP",       RX|PCREL|SHORT, },
       { 0xE9,          "LOADDP",       RX|IND|PCREL|SHORT, },
       { 0xEE,          "LADDRP",       RX|PCREL|SHORT, },
       { 0xEF,          "LADDRP",       RX|IND|PCREL|SHORT, },
       { 0xF0,          "LOADBP",       RX|PCREL|LONG, },
       { 0xF1,          "LOADBP",       RX|IND|PCREL|LONG, },
       { 0xF2,          "LOADHP",       RX|PCREL|LONG, },
       { 0xF3,          "LOADHP",       RX|IND|PCREL|LONG, },
       { 0xF6,          "LOADP",        RX|PCREL|LONG, },
       { 0xF7,          "LOADP",        RX|IND|PCREL|LONG, },
       { 0xF8,          "LOADDP",       RX|PCREL|LONG, },
       { 0xF9,          "LOADDP",       RX|IND|PCREL|LONG, },
       { 0xFE,          "LADDRP",       RX|PCREL|LONG, },
       { 0xFF,          "LADDRP",       RX|IND|PCREL|LONG, },
       { 0,            NULL, 0 }
};

CONST char *cond[] = {
       ">", "<", "=", "", ">", "<", "=", "",
       "<=", ">=", "<>", "", "<=", ">=", "<>", ""};

CONST char *cond_str[] = {"<=", ">=", "<>", ">", "<", "=", NULL };

CONST uint8 cond_val[] = { 0x8,  0x9,  0xA, 0x0, 0x1, 0x2, 0xff };

CONST char *rone[] = {
       "ELOGR", "ELOGW", "MAINT2", "MAINT3", "MAINT4", "TWRITED", "FLUSH", 
       "TRAPEXIT", "ITEST", "MAINT9", "MACHINEID", "VERSION", "CREG", "RDLOG",
       "MAINT14", "MAINT15"};



void fprint_inst(FILE *of, t_addr addr, t_value *val) {
uint8           inst = val[0];
t_opcode        *tab;
uint32          disp = 0;

    for (tab = optab; tab->name != NULL; tab++) {
       if (tab->opbase == inst) {
          if ((tab->type & 0xf) == M)
             fputs(rone[val[1] & 0xF], of);
          else
             fputs(tab->name, of);
          fputc(' ', of);
          if (tab->type & (SHORT|LONG)) {
              disp = (val[2] << 8) | val[3];
              if (tab->type & LONG) {
                   disp <<= 16;
                   disp |= (val[4] << 8) | val[5];
              } else if (disp & 0x8000) {
                   disp |= 0xffff0000;
              }
          }
          if (tab->type & PCREL) {
              disp = (disp + addr) & 0xffffff;
          }
          switch (tab->type & 0x7) {
          case RR:
                    fprintf(of, "R%d", (val[1] >> 4) & 0xF); 
                    if (tab->type & COND) {
                        fputs(cond[inst & 0xF], of);
                    } else {
                        fputc(',',of);
                    }
                    fprintf(of, "R%d", val[1] & 0xF);
                    if (tab->type & PCREL) {
                        fputc(',',of);
                    }
                    break;
          case R1:
                    fprintf(of, "R%d,", (val[1] >> 4) & 0xF); 
                    break;
          case RI:
                    fprintf(of, "R%d", (val[1] >> 4) & 0xF); 
                    if (tab->type & COND) {
                        fputs(cond[inst & 0xF], of);
                    } else {
                        fputc(',',of);
                    }
                    fprintf(of, "%d", val[1] & 0xF);
                    if (tab->type & (LONG|SHORT))
                        fputc(',',of);
                    /* Fall through */
          case RZ:
                    break;
          case M:
                    fprintf(of, "R%d", (val[1] >> 4) & 0xF); 
                    break;
          case RX:
                    fprintf(of, "R%d,", (val[1] >> 4) & 0xF); 
                    if (tab->type & IND) 
                         fprintf(of, "R%d,", val[1] & 0xF);
                    break;
          case RN:
                    fprintf(of, "%d", val[1] & 0xFF); 
                    break;
          }
          if (tab->type & (LONG|PCREL)) {
              fprint_val(of, disp, 16, 32, PV_RZRO);
              if (tab->type & LONG)
                  fputs(",L", of);
          } else if (tab->type & SHORT) {
              fprint_val(of, disp, 16, 16, PV_RZRO);
          }
      }
   }
}

/* Symbolic decode

   Inputs:
       *of      =       output stream
       addr     =       current PC
       *val     =       pointer to values
       *uptr    =       pointer to unit
       sw       =       switches
   Outputs:
       return   =       status code
*/

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
       UNIT *uptr, int32 sw)
{
uint8           inst = *val;
int             i;
int             l = 1;
int             rdx = 16;
uint32          num;

if (sw & SWMASK ('D'))
    rdx = 10;
else if (sw & SWMASK ('O'))
    rdx = 8;
else if (sw & SWMASK ('H'))
    rdx = 16;
if (sw & SWMASK ('M')) {
    l = 2;
    if (inst & 0x80) {
       l += 2;
       if (inst & 0x10)
          l += 2;
    }
    sw &= ~ SWMASK('F'); /* Can't do F and M at same time */
} else if (sw & SWMASK('F')) {
    l = 4;
} else if (sw & SWMASK('W')) {
    l = 2;
} else if (sw & SWMASK('B')) {
    l = 1;
}

if (sw & SWMASK ('C')) {
   fputc('\'', of);
   for(i = 0; i < l; i++) {
      uint8 ch = val[i] & 0xff;
      if (ch >= 0x20 && ch <= 0x7f)
          fprintf(of, "%c", ch);
      else
          fputc('_', of);
   }
   fputc('\'', of);
}
if (sw & SWMASK ('M')) {
   i = 0;
   num = (uint32)(val[i++] << 8);
   num |= (uint32)val[i++];
   fprint_val(of, num, 16, 16, PV_RZRO);
   fputc(' ', of);
   if (inst & 0x80) {
       num = (uint32)(val[i++] << 8);
       num |= (uint32)val[i++];
       fprint_val(of, num, 16, 16, PV_RZRO);
       if ((inst & 0x10) != 0) {
             num = (uint32)(val[i++] << 8);
             num |= (uint32)val[i++];
             fprint_val(of, num, 16, 16, PV_RZRO);
       } else  {
             fputs("    ", of);
       }
   } else {
       fputs("        ", of);
   }
   fputc(' ', of);
   fprint_inst(of, addr, val);
} else {
    num = 0;
    for (i = 0; i < l && i < 4; i++)
       num |= (uint32)val[i] << ((l-i-1) * 8);
    fprint_val(of, num, rdx, l*8, PV_RZRO);
}

return -(l-1);
}

/*
 * Collect register name.
 */
t_stat get_reg (CONST char *cptr, CONST char **tptr, int *reg)
{
    while (sim_isspace (*cptr)) cptr++;
    if ((*cptr == 'R') || (*cptr == 'r'))      /* Skip R */
       cptr++;
    if ((*cptr >= '0') && (*cptr <= '9')) {
       *reg = *cptr++ - '0';
       if ((*cptr >= '0') && (*cptr <= '9'))
          *reg = (*reg * 10) + (*cptr++ - '0');
       if (*reg > 0xf)
          return SCPE_ARG;
    } else if ((*cptr >= 'a') && (*cptr <= 'f'))
       *reg = (*cptr++ - 'a') + 10;
    else if ((*cptr >= 'A') && (*cptr <= 'F'))
       *reg = (*cptr++ - 'A') + 10;
    else
       return SCPE_ARG;
    while (sim_isspace (*cptr)) cptr++;
    *tptr = cptr;
    return SCPE_OK;;
}

/*
 * Collect disp in radix.
 */
t_stat get_disp (CONST char *cptr, CONST char **tptr, uint32 radix, uint32 *val)
{
t_stat r;

r = SCPE_OK;
*val = (uint32)strtotv (cptr, tptr, radix);
if (cptr == *tptr)
    r = SCPE_ARG;
else {
    cptr = *tptr;
    while (sim_isspace (*cptr)) cptr++;
    *tptr = cptr;
}
return r;
}
/*
 * Collect n in radix.
 */
t_stat get_n (CONST char *cptr, CONST char **tptr, uint32 radix, uint32 *val)
{
t_stat r;

r = SCPE_OK;
*val = (uint32)strtotv (cptr, tptr, radix);
if ((cptr == *tptr) || (*val > 0xff))
    r = SCPE_ARG;
else {
    cptr = *tptr;
    while (sim_isspace (*cptr)) cptr++;
    *tptr = cptr;
}
return r;
}

/*
 * Collect immediate in radix.
 */
t_stat get_imm (CONST char *cptr, CONST char **tptr, uint32 radix, uint32 *val)
{
t_stat r;

r = SCPE_OK;
*val = (uint32)strtotv (cptr, tptr, radix);
if ((cptr == *tptr) || (*val > 0xf))
    r = SCPE_ARG;
else {
    cptr = *tptr;
    while (sim_isspace (*cptr)) cptr++;
    *tptr = cptr;
}
return r;
}

/* Symbolic input

   Inputs:
       *cptr       =       pointer to input string
       addr       =       current PC
       uptr       =       pointer to unit
       *val       =       pointer to output values
       sw       =       switches
   Outputs:
       status       =       error status
*/

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int             i;
int             l = 1;
int             rdx = 16;
t_opcode        *tab;
t_stat          r;
uint32          num;
uint32          max[5] = { 0, 0xff, 0xffff, 0, 0xffffffff };
CONST char      *tptr;
char            gbuf[CBUFSIZE];

if (sw & SWMASK ('D'))
    rdx = 10;
else if (sw & SWMASK ('O'))
    rdx = 8;
else if (sw & SWMASK ('H'))
    rdx = 16;
if (sw & SWMASK('F')) {
     l = 4;
} else if (sw & SWMASK('W')) {
     l = 2;
}

if (sw & SWMASK ('C')) {
   cptr = get_glyph_quoted(cptr, gbuf, 0);   /* Get string */
   for(i = 0; gbuf[i] != 0; i++) {
      val[i] = gbuf[i];
   }
   return -(i - 1);
}

if (sw & SWMASK ('M')) {
    cptr = get_glyph(cptr, gbuf, 0);         /* Get opcode */
    for (tab = optab; tab->name != NULL; tab++) {
       if (sim_strcasecmp(tab->name, gbuf) == 0)
          break;
    }
    if (tab->name == NULL) {
        int  j;
        for (j = 0; j <= 0xF; j++) {
           if (sim_strcasecmp(rone[j], gbuf) == 0) {
               if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                   return r;
               val[0] = OP_MAINT;
               val[1] = (j << 4) | i;
               return -1;
           }
        }
        return SCPE_ARG;
    }
    val[0] = tab->opbase;
    l = 0;
    switch (tab->type & 7) {
    case RR:
            if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                return r;
            cptr = tptr;
            val[1] = i << 4;
            if (tab->type & COND) {
                int  j;
                for (j = 0; cond_str[j] != NULL; j++) {
                   if (sim_strcasecmp(cond_str[j], gbuf) == 0) {
                       val[0] |= cond_val[j];
                       cptr += (cond_val[j] & 0x8) ? 2 : 1;
                       break;
                   }
                }
                if (cond_val[j] == 0xff)
                    return SCPE_ARG;
            } else {
                if (*cptr != ',')
                    return SCPE_ARG;
                cptr++;
            }
            while (sim_isspace (*cptr)) cptr++;
            if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                 return r;
            val[1] |= i;
            cptr = tptr;
            if (tab->type & PCREL) {
                if (*cptr != ',')
                    return SCPE_ARG;
                cptr++;
                while (sim_isspace (*cptr)) cptr++;
                if ((r = get_disp(cptr, &tptr, rdx, &num)) != SCPE_OK)
                   return SCPE_ARG;
                cptr = tptr;
                if (*cptr == ',') {
                    cptr++;
                    while (sim_isspace (*cptr)) cptr++;
                    if (*cptr != 'L' && *cptr != 'l')
                        return SCPE_ARG;
                    val[0] |= 0x10;
                    l = 4;
                } else {
                    l = 2;
                    if (num > 0x10000)
                        return SCPE_ARG;
                }
                for (i = 0; i < l && i < 4; i++)
                    val[i+2] = (num >> (((l - 1) - i) * 8)) & 0xff;
            }
            return -(l-1);

    case R1:
            return SCPE_ARG;

    case RI:
            if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                return r;
            val[1] = i << 4;
            cptr = tptr;
            if (tab->type & COND) {
                int  j;
                for (j = 0; cond_str[j] != NULL; j++) {
                   if (sim_strcasecmp(cond_str[j], gbuf) == 0) {
                       val[0] |= cond_val[j];
                       cptr += (cond_val[j] & 0x8) ? 2 : 1;
                       break;
                   }
                }
                if (cond_val[j] == 0xff)
                    return SCPE_ARG;
            } else {
                if (*cptr != ',')
                    return SCPE_ARG;
                cptr++;
            }
            while (sim_isspace (*cptr)) cptr++;
            if ((r = get_imm(cptr, &tptr, rdx, &num)) != SCPE_OK)
                 return r;
            val[1] |= num;
            cptr = tptr;
            if (tab->type & PCREL) {
                if (*cptr != ',')
                    return SCPE_ARG;
                cptr++;
                while (sim_isspace (*cptr)) cptr++;
                if ((r = get_disp(cptr, &tptr, rdx, &num)) != SCPE_OK)
                   return SCPE_ARG;
                cptr = tptr;
                if (*cptr == ',') {
                    cptr++;
                    while (sim_isspace (*cptr)) cptr++;
                    if (*cptr != 'L' && *cptr != 'l')
                        return SCPE_ARG;
                    val[0] |= 0x10;
                    l = 4;
                } else {
                    l = 2;
                    if (num > 0x10000)
                        return SCPE_ARG;
                }
                for (i = 0; i < l && i < 4; i++)
                    val[i+2] = (num >> (((l - 1) - i) * 8)) & 0xff;
              }
              return -(l-1);
    case RZ:
            if (tab->type & PCREL) {
                if ((r = get_disp(cptr, &tptr, rdx, &num)) != SCPE_OK)
                   return SCPE_ARG;
                cptr = tptr;
                if (*cptr == ',') {
                    cptr++;
                    while (sim_isspace (*cptr)) cptr++;
                    if (*cptr != 'L' && *cptr != 'l')
                        return SCPE_ARG;
                    val[0] |= 0x10;
                    l = 4;
                } else {
                    l = 2;
                    if (num > 0x10000)
                        return SCPE_ARG;
                }
                for (i = 0; i < l && i < 4; i++)
                    val[i+2] = (num >> (((l - 1) - i) * 8)) & 0xff;
              }
              return -(l-1);
    
    case RX:
            if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                return r;
            val[1] = i << 4;
            cptr = tptr;
            while (sim_isspace (*cptr)) cptr++;
            if (*cptr != ',')
               return SCPE_ARG;
            cptr++;
            r = get_reg(cptr, &tptr, &i);
            if (r == SCPE_OK) {
                val[1] |= num;
                cptr = tptr;
                if (*cptr != ',')
                    return SCPE_ARG;
                cptr++;
            }
            if ((r = get_disp(cptr, &tptr, rdx, &num)) != SCPE_OK)
               return SCPE_ARG;
            cptr = tptr;
            if (*cptr == ',') {
                cptr++;
                while (sim_isspace (*cptr)) cptr++;
                if (*cptr != 'L' && *cptr != 'l')
                    return SCPE_ARG;
                val[0] |= 0x10;
                l = 4;
            } else {
                l = 2;
                if (num > 0x10000)
                    return SCPE_ARG;
            }
            for (i = 0; i < l && i < 4; i++)
                 val[i+2] = (num >> (((l - 1) - i) * 8)) & 0xff;
            return -(l-1);
    case RN:
            if ((r = get_n(cptr, &tptr, rdx, &num)) != SCPE_OK)
                return r;
            val[1] = num;
            return -1;
    }
}
num = get_uint(cptr, rdx, max[l], &r);
for (i = 0; i < l && i < 4; i++)
    val[i] = (num >> (((l - 1) - i) * 8)) & 0xff;
return -(l-1);
}

