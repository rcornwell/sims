/* ibm360_sys.c: IBM 360 Simulator system interface.

   Copyright (c) 2005, Richard Cornwell

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

#include "ibm360_defs.h"
#include <ctype.h>
#include "sim_card.h"

extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern uint32 M[MAXMEMSIZE];

/* SCP data structures and interface routines

   sim_name            simulator name string
   sim_PC              pointer to saved PC register descriptor
   sim_emax            number of words for examine
   sim_devices         array of pointers to simulated devices
   sim_stop_messages   array of pointers to stop messages
   sim_load            binary loader
*/

char sim_name[] = "IBM 360";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 16;

DEVICE *sim_devices[] = {
       &cpu_dev,
#ifdef NUM_DEVS_CON
        &con_dev,
#endif
#ifdef NUM_DEVS_CDR
        &cdr_dev,
#endif
#ifdef NUM_DEVS_CDP
        &cdp_dev,
#endif
#ifdef NUM_DEVS_LPR
        &lpr_dev,
#endif
#ifdef NUM_DEVS_MT
        &mta_dev,
#if NUM_DEVS_MT > 1
        &mtb_dev,
#endif
#endif
#ifdef NUM_DEVS_DASD
        &dda_dev,
#if NUM_DEVS_DASD > 1
        &ddb_dev,
#endif
#endif
#ifdef NUM_DEVS_COM
        &com_dev,
#endif
       NULL };


/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"CONI", DEBUG_CONI, "Show coni instructions"},
    {"CONO", DEBUG_CONO, "Show coni instructions"},
    {"DATAIO", DEBUG_DATAIO, "Show datai and datao instructions"},
    {0, 0}
};

/* Simulator debug controls */
DEBTAB              crd_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"CONI", DEBUG_CONI, "Show coni instructions"},
    {"CONO", DEBUG_CONO, "Show coni instructions"},
    {"DATAIO", DEBUG_DATAIO, "Show datai and datao instructions"},
    {"CARD", DEBUG_CARD, "Show Card read/punches"},
    {0, 0}
};


const char *sim_stop_messages[] = {
       "Unknown error",
       "IO device not ready",
       "HALT instruction",
       "Breakpoint",
       "Unknown Opcode",
       "Invalid instruction",
       "Invalid I/O operation",
       "Nested indirects exceed limit",
       "Nested XEC's exceed limit",
       "I/O Check opcode",
       "Memory management trap during trap",
       "Trap instruction not BRM",
       "RTC instruction not MIN or SKR",
       "Interrupt vector zero",
       "Runaway carriage control tape"  };

const char ascii_to_ebcdic[128] = {
   /* Control                              */
    0x01,0x02,0x03,0xFF,0x00,0x00,0x00,0x00,    /*0-37*/
   /*Control*/
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   /*Control*/
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   /*Control*/
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
   /*  sp    !     "     #     $     %     &     ' */
    0x40, 0x5a, 0x7f, 0x7b, 0x5b, 0x6c, 0x50, 0x7d,     /* 40 - 77 */
   /*  (     )     *     +     ,     -     .     / */
    0x4d, 0x5d, 0x5c, 0x4e, 0x6b, 0x60, 0x4b, 0x61,
   /*  0     1     2     3     4     5     6     7 */
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
   /*  8     9     :     ;     <     =     >     ? */
    0xf8, 0xf9, 0x7a, 0x6e, 0x4c, 0x7e, 0x6e, 0x6f,
   /*  @     A     B     C     D     E     F     G */
    0x7c, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,     /* 100 - 137 */
   /*  H     I     J     K     L     M     N     O */
    0xc8, 0xc9, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
   /*  P     Q     R     S     T     U     V     W */
    0xd7, 0xd8, 0xd9, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
   /*  X     Y     Z     [     \     ]     ^     _ */
    0xe7, 0xe8, 0xe9, 0x4a, 0xff, 0x5a, 0x5f, 0x6d,
   /*  `     a     b     c     d     e     f     g */
    0x7c, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,     /* 140 - 177 */
   /*  h     i     j     k     l     m     n     o */
    0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
   /*  p     q     r     s     t     u     v      w */
    0x97, 0x98, 0x99, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
   /*  x     y     z     {     |     }     ~   del */
    0xa7, 0xa8, 0xa9, 0xff, 0x47, 0xff, 0xff, 0x6d,
};

const char ebcdic_to_ascii[256] = {
/*      0     1     2     3    4     5      6   7 */
    0x00, 0x01, 0x02, 0x03, 0xFF, 0x09, 0xff, 0x7f,      /* 0x */
    0xff, 0xff, 0xff, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x19, 0x0a, 0x08, 0x08, 0xff,      /* 1x */
    0x18, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x1c, 0xff, 0xff, 0x0a, 0xff, 0xff,      /* 2x */
    0xff, 0xff, 0xff, 0xff, 0xff, 0x05, 0x06, 0x07,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x1e, 0xff, 0xff,      /* 3x */
    0xff, 0xff, 0xff, 0xff, 0x14, 0x15, 0xff, 0xff,
    ' ',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,      /* 4x */
    0xff, 0xff, '[',  '.',  '<',  '(',  '+',  '|',
    '&',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,      /* 5x */
    0xff, 0xff, ']',  '$',  '*',  ')',  ';',  '^',
    '-',  '/',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,      /* 6x */
    0xff, 0xff, 0xff, ',',  '%',  '_',  '>',  '?',
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,      /* 7x */
    0xff, 0xff, ':',  '#',  '@',  '\'', '=',  '"',
    0xff, 'a',  'b',  'c',  'd',  'e',  'f',  'g',       /* 8x */
    'h',  'i',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 'j',  'k',  'l',  'm',  'n',  'o',  'p',       /* 9x */
    'q',  'r',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 's',  't',  'u',  'v',  'w',  'x',       /* Ax */
    'y',  'z',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,      /* Bx */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 'A',  'B',  'C',  'D',  'E',  'F',  'G',       /* Cx */
    'H',  'I',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 'J',  'K',  'L',  'M',  'N',  'O',  'P',       /* Dx */
    'Q',  'R',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 'S',  'T',  'U',  'V',  'W',  'X',       /* Ex */
    'Y',  'Z',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',       /* Fx */
    '8',  '9',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};


/* Load a card image file into memory.  */

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
return SCPE_NOFNC;
}

/* Symbol tables */
typedef struct _opcode {
       uint8    opbase;
       char     *name;
       uint8    type;
} t_opcode;

#define RR       01
#define RX       02
#define RS       03
#define SI       04
#define SS       05
#define LNMSK    07
#define ONEOP    010
#define IMDOP    020
#define TWOOP    030
#define ZEROOP   040
#define OPMSK    070

t_opcode  optab[] = {
       { OP_SPM,       "SPM",  RR|ONEOP },
       { OP_BALR,      "BALR", RR },
       { OP_BCTR,      "BCTR", RR },
       { OP_BCR,       "BCR",  RR },
       { OP_SSK,       "SSK",  RR },
       { OP_ISK,       "ISK",  RR },
       { OP_SVC,       "SVC",  RR|IMDOP },
       { OP_LPR,       "LPR",  RR },
       { OP_LNR,       "LNR",  RR },
       { OP_LTR,       "LTR",  RR },
       { OP_LCR,       "LCR",  RR },
       { OP_NR,        "NR",   RR },
       { OP_OR,        "OR",   RR },
       { OP_XR,        "XR",   RR },
       { OP_CLR,       "CLR",  RR },
       { OP_CR,        "CR",   RR },
       { OP_LR,        "LR",   RR },
       { OP_AR,        "AR",   RR },
       { OP_SR,        "SR",   RR },
       { OP_MR,        "MR",   RR },
       { OP_DR,        "DR",   RR },
       { OP_ALR,       "ALR",  RR },
       { OP_SLR,       "SLR",  RR },
       { OP_LPDR,      "LPDR", RR },
       { OP_LNDR,      "LNDR", RR },
       { OP_LTDR,      "LTDR", RR },
       { OP_LCDR,      "LCDR", RR },
       { OP_HDR,       "HDR",  RR },
       { OP_LRDR,      "LRDR", RR },
       { OP_MXR,       "MXR",  RR },
       { OP_MXDR,      "MXDR", RR },
       { OP_LDR,       "LDR",  RR },
       { OP_CDR,       "CDR",  RR },
       { OP_ADR,       "ADR",  RR },
       { OP_SDR,       "SDR",  RR },
       { OP_MDR,       "MDR",  RR },
       { OP_DDR,       "DDR",  RR },
       { OP_AWR,       "AWR",  RR },
       { OP_SWR,       "SWR",  RR },
       { OP_LPER,      "LPER", RR },
       { OP_LNER,      "LNER", RR },
       { OP_LTER,      "LTER", RR },
       { OP_LCER,      "LCER", RR },
       { OP_HER,       "HER",  RR },
       { OP_LRER,      "LRER", RR },
       { OP_AXR,       "AXR",  RR },
       { OP_SXR,       "SXR",  RR },
       { OP_LER,       "LER",  RR },
       { OP_CER,       "CER",  RR },
       { OP_AER,       "AER",  RR },
       { OP_SER,       "SER",  RR },
       { OP_MER,       "MER",  RR },
       { OP_DER,       "DER",  RR },
       { OP_AUR,       "AUR",  RR },
       { OP_SUR,       "SUR",  RR },
       { OP_STH,       "STH",  RX },
       { OP_LA,        "LA",   RX },
       { OP_STC,       "STC",  RX },
       { OP_IC,        "IC",   RX },
       { OP_EX,        "EX",   RX },
       { OP_BAL,       "BAL",  RX },
       { OP_BCT,       "BCT",  RX },
       { OP_BC,        "BC",   RX },
       { OP_LH,        "LH",   RX },
       { OP_CH,        "CH",   RX },
       { OP_AH,        "AH",   RX },
       { OP_SH,        "SH",   RX },
       { OP_MH,        "MH",   RX },
       { OP_CVD,       "CVD",  RX },
       { OP_CVB,       "CVB",  RX },
       { OP_ST,        "ST",   RX },
       { OP_N,         "N",    RX },
       { OP_CL,        "CL",   RX },
       { OP_O,         "O",    RX },
       { OP_X,         "X",    RX },
       { OP_L,         "L",    RX },
       { OP_C,         "C",    RX },
       { OP_A,         "A",    RX },
       { OP_S,         "S",    RX },
       { OP_M,         "M",    RX },
       { OP_D,         "D",    RX },
       { OP_AL,        "AL",   RX },
       { OP_SL,        "SL",   RX },
       { OP_STD,       "STD",  RX },
       { OP_MXD,       "MXD",  RX },
       { OP_LD,        "LD",   RX },
       { OP_CD,        "CD",   RX },
       { OP_AD,        "AD",   RX },
       { OP_SD,        "SD",   RX },
       { OP_MD,        "MD",   RX },
       { OP_DD,        "DD",   RX },
       { OP_AW,        "AW",   RX },
       { OP_SW,        "SW",   RX },
       { OP_STE,       "STE",  RX },
       { OP_LE,        "LE",   RX },
       { OP_CE,        "CE",   RX },
       { OP_AE,        "AE",   RX },
       { OP_SE,        "SE",   RX },
       { OP_ME,        "ME",   RX },
       { OP_DE,        "DE",   RX },
       { OP_AU,        "AU",   RX },
       { OP_SU,        "SU",   RX },
       { OP_SSM,       "SSM",  SI|ZEROOP },
       { OP_LPSW,      "LPSW", SI|ZEROOP },
       { OP_DIAG,      "DIAG", SI },
       { OP_BXH,       "BXH",  RS },
       { OP_BXLE,      "BXLE", RS },
       { OP_SRL,       "SRL",  RS|ZEROOP },
       { OP_SLL,       "SLL",  RS|ZEROOP },
       { OP_SRA,       "SRA",  RS|ZEROOP },
       { OP_SLA,       "SLA",  RS|ZEROOP },
       { OP_SRDL,      "SRDL", RS|ZEROOP },
       { OP_SLDL,      "SLDL", RS|ZEROOP },
       { OP_SRDA,      "SRDA", RS|ZEROOP },
       { OP_SLDA,      "SLDA", RS|ZEROOP },
       { OP_STM,       "STM",  RS|TWOOP },
       { OP_TM,        "TM",   SI },
       { OP_MVI,       "MVI",  SI },
       { OP_TS,        "TS",   SI|ZEROOP },
       { OP_NI,        "NI",   SI },
       { OP_CLI,       "CLI",  SI },
       { OP_OI,        "OI",   SI },
       { OP_XI,        "XI",   SI },
       { OP_LM,        "LM",   RS|TWOOP },
       { OP_SIO,       "SIO",  SI|ZEROOP },
       { OP_TIO,       "TIO",  SI|ZEROOP },
       { OP_HIO,       "HIO",  SI|ZEROOP },
       { OP_TCH,       "TCH",  SI|ZEROOP },
       { OP_MVN,       "MVN",  SS },
       { OP_MVC,       "MVC",  SS },
       { OP_MVZ,       "MVZ",  SS },
       { OP_NC,        "NC",   SS },
       { OP_CLC,       "CLC",  SS },
       { OP_OC,        "OC",   SS },
       { OP_XC,        "XC",   SS },
       { OP_TR,        "TR",   SS },
       { OP_TRT,       "TRT",  SS },
       { OP_ED,        "ED",   SS },
       { OP_EDMK,      "EDMK", SS },
       { OP_MVO,       "MVO",  SS|TWOOP },
       { OP_PACK,      "PACK", SS|TWOOP },
       { OP_UNPK,      "UNPK", SS|TWOOP },
       { OP_ZAP,       "ZAP",  SS|TWOOP },
       { OP_CP,        "CP",   SS|TWOOP },
       { OP_AP,        "AP",   SS|TWOOP },
       { OP_SP,        "SP",   SS|TWOOP },
       { OP_MP,        "MP",   SS|TWOOP },
       { OP_DP,        "DP",   SS|TWOOP },
       { 0,            NULL, 0 }
};


/* Register change decode

   Inputs:
    *of       =       output stream
    inst      =       mask bits
*/

//void fprint_reg (FILE *of, int32 inst)
//{
//int32 i, j, sp;
#if 0
inst = inst & ~(I_M_OP << I_V_OP);                     /* clear opcode */
for (i = sp = 0; opc_val[i] >= 0; i++) {              /* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;              /* get class */
    if ((j == I_V_REG) && (opc_val[i] & inst)) {       /* reg class? */
       inst = inst & ~opc_val[i];                     /* mask bit set? */
       fprintf (of, (sp? " %s": "%s"), opcode[i]);
       sp = 1;  }  }
#endif
//return;
//}

void fprint_inst(FILE *of, uint16 *val) {
uint8           inst = (val[0] >> 8) &  0xff;
int             i;
int             l = 1;
t_opcode        *tab;

    for (tab = optab; tab->name != NULL; tab++) {
       if (tab->opbase == inst) {
          fputs(tab->name, of);
          fputc(' ', of);
          switch (tab->type & LNMSK) {
          case RR:
                    if (tab->type & IMDOP) {
                        fprint_val(of, val[0] & 0xff, 16, 8, PV_RZRO);
                    } else {
                        if (tab->type & ONEOP)
                            fprintf(of, "%d", (val[0] >> 4) & 0xf);
                        else
                            fprintf(of, "%d,%d", (val[0] >> 4) & 0xf, val[0] & 0xf);
                    }
                    break;
          case RX:
                    fprintf(of, "%d,", (val[0] >> 4) & 0xf);
                    fprint_val(of, val[1] & 0xfff, 16, 12, PV_RZRO);
                    fprintf(of, "(%d,%d)", val[0] & 0xf, (val[1] >> 12) & 0xf);
                    break;
          case RS:
                    fprintf(of, "%d,", (val[0] >> 4) & 0xf);
                    if ((tab->type & ZEROOP) == 0)
                        fprintf(of, "%d,", val[0] & 0xf);
                    fprint_val(of, val[1] & 0xfff, 16, 12, PV_RZRO);
                    if (val[1] & 0xf000)
                        fprintf(of, "(%d)", (val[1] >> 12) & 0xf);
                    break;
          case SI:
                    fprint_val(of, val[1] & 0xfff, 16, 12, PV_RZRO);
                    if (val[1] & 0xf000)
                        fprintf(of, "(%d)", (val[1] >> 12) & 0xf);
                    if ((tab->type & ZEROOP) == 0)
                        fprintf(of, ",%02x", val[0] & 0xff);
                    break;
          case SS:
                    fprint_val(of, val[1] & 0xfff, 16, 12, PV_RZRO);
                    if (tab->type & TWOOP) {
                       fprintf(of, "(%d", (val[0] >> 4) & 0xf);
                    } else {
                       fprintf(of, "(%d", val[0] & 0xff);
                    }
                    if (val[1] & 0xf000)
                        fprintf(of, ",%d", (val[1] >> 12) & 0xf);
                    fprintf(of, "),");
                    fprint_val(of, val[2] & 0xfff, 16, 12, PV_RZRO);
                    if (tab->type & TWOOP) {
                       fprintf(of, "(%d,", val[0] & 0xf);
                    } else {
                       fprintf(of, "(");
                    }
                    fprintf(of, "%d)", (val[2] >> 12) & 0xf);
                    break;
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
t_opcode        *tab;

if (sw & SWMASK ('M')) {
    for (tab = optab; tab->name != NULL; tab++) {
       if (tab->opbase == inst) {
          switch (tab->type & LNMSK) {
          case RR:
                    l = 2;
                    break;
          case RX:
          case RS:
          case SI:
                    l = 4;
                    break;
          case SS:
                    l = 6;
          }
       }
     }
     sw &= ~ SWMASK('F'); /* Can't do F and M at same time */
} else if (sw & SWMASK('F')) {
     l = 4;
} else if (sw & SWMASK('W')) {
     l = 2;
}

for(i = 0; i < l; i++) {
   fprintf(of, "%02x ", val[i] & 0xFF);
}

if (sw & SWMASK ('C')) {
   fputc('\'', of);
   for(i = 0; i < l; i++) {
      char ch = ebcdic_to_ascii[val[i] & 0xff];
      if (ch >= 0x20 && ch <= 0x7f)
          fprintf(of, "%c", ch);
      else
          fputc('_', of);
   }
   fputc('\'', of);
}
if (sw & SWMASK ('W')) {
   if (sw & SWMASK('M')) {
       for(i = l; i <= 6; i++) {
          fputs("   ", of);
          if (sw & SWMASK('C'))
              fputs(" ", of);
       }
       if (sw & SWMASK('C'))
          fputs("   ", of);
   }
   for(i = 0; i < l; i+=2)
       fprintf(of, "%02x%02x ", val[i] & 0xff, val[i+1] & 0xff);
   if (sw & SWMASK('M')) {
       for(i = l; i <= 6; i+=2) {
          fputs("     ", of);
       }
   }
}

if (sw & SWMASK ('F')) {
   fprintf(of, "%02x%02x%02x%02x ", val[0] & 0xff, val[1] & 0xff, val[2] & 0xff, val[3] & 0xff);
   return -3;
}

if (sw & SWMASK ('M')) {
    fputs("   ", of);
    if ((sw & SWMASK('W')) == 0) {
   if (sw & SWMASK('M')) {
       for(i = l; i <= 6; i++) {
          fputs("   ", of);
          if (sw & SWMASK('C'))
              fputs(" ", of);
       }
       if (sw & SWMASK('C'))
          fputs("   ", of);
   }
   }
    for (tab = optab; tab->name != NULL; tab++) {
       if (tab->opbase == inst) {
          fputs(tab->name, of);
          fputc(' ', of);
          switch (tab->type & LNMSK) {
          case RR:
                    if (tab->type & IMDOP) {
                        fprint_val(of, val[1], 16, 8, PV_RZRO);
                    } else {
                        if (tab->type & ONEOP)
                            fprintf(of, "%d", (val[1] >> 4) & 0xf);
                        else
                            fprintf(of, "%d,%d", (val[1] >> 4) & 0xf, val[1] & 0xf);
                    }
                    break;
          case RX:
                    fprintf(of, "%d, %x(", (val[1] >> 4) & 0xf, ((val[2] << 8) & 0xf00) | val[3]);
                    fprintf(of, "%d,%d)", val[1] & 0xf, (val[2] >> 4) & 0xf);
                    break;
          case RS:
                    fprintf(of, "%d,", (val[1] >> 4) & 0xf);
                    if ((tab->type & ZEROOP) == 0)
                        fprintf(of, "%d,", val[1] & 0xf);
                    fprintf(of, "%x", ((val[2] << 8) & 0xf00) | val[3]);
                    if (val[2] & 0xf0)
                        fprintf(of, "(%d)", (val[2] >> 4) & 0xf);
                    break;
          case SI:
                    fprintf(of, "%x", ((val[2] << 8) & 0xf00) | val[3]);
                    if (val[2] & 0xf0)
                        fprintf(of, "(%d)", (val[2] >> 4) & 0xf);
                    if ((tab->type & ZEROOP) == 0)
                       fprintf(of, ",%2x", val[1]);
                    break;
          case SS:
                    fprintf(of, "%x", ((val[2] << 8) & 0xf00) | val[3]);
                    if (tab->type & TWOOP) {
                       fprintf(of, "(%d", (val[1] >> 4) & 0xf);
                    } else {
                       fprintf(of, "(%d", val[1] & 0xff);
                    }
                    if (val[2] & 0xf0)
                        fprintf(of, ",%d", (val[1] >> 4) & 0xf);
                    fprintf(of, "),");
                    fprintf(of, "%x", ((val[4] << 8) & 0xf00) | val[5]);
                    if (tab->type & TWOOP) {
                       fprintf(of, "(%d,", val[1] & 0xf);
                    } else {
                       fprintf(of, "(");
                    }
                    fprintf(of, "%d)", (val[4] >> 4) & 0xf);
                    break;
          }
      }
   }
}

//fprint_val (of, inst, 16, 8, PV_RZRO);
return -(l-1);

return SCPE_OK;
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
int32 i, j, k;
t_value d, tag;
t_stat r;

while (isspace (*cptr)) cptr++;
    return SCPE_OK;

/* Symbolic input, continued */

if (*cptr != 0) return SCPE_ARG;                     /* junk at end? */
return SCPE_OK;
}

