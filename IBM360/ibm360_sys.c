/* ibm360_sys.c: IBM 360 Simulator system interface.

   Copyright (c) 2017-2020, Richard Cornwell

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
       &chan_dev,
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
#if NUM_DEVS_DASD > 2
       &ddc_dev,
#if NUM_DEVS_DASD > 3
       &ddd_dev,
#endif
#endif
#endif
#endif
#ifdef NUM_DEVS_COM
       &coml_dev,
       &com_dev,
#endif
#ifdef NUM_DEVS_SCOM
       &scoml_dev,
       &scom_dev,
#endif
       NULL };


/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"POS", DEBUG_POS, "Dasd positioning information"},
    {"INST", DEBUG_INST, "Show instruction execution"},
    {"CDATA", DEBUG_CDATA, "Show channel data"},
    {"TRACE", DEBUG_TRACE, "Show instruction history"},
    {0, 0}
};

/* Simulator debug controls */
DEBTAB              crd_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"CARD", DEBUG_CARD, "Show Card read/punches"},
    {"CDATA", DEBUG_CDATA, "Show channel data"},
    {0, 0}
};


const char *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "Uninterruptable wait",
    "Breakpoint"
     };

const uint8 ascii_to_ebcdic[128] = {
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
    0xf8, 0xf9, 0x7a, 0x5e, 0x4c, 0x7e, 0x6e, 0x6f,
   /*  @     A     B     C     D     E     F     G */
    0x7c, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,     /* 100 - 137 */
   /*  H     I     J     K     L     M     N     O */
    0xc8, 0xc9, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
   /*  P     Q     R     S     T     U     V     W */
    0xd7, 0xd8, 0xd9, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6,
   /*  X     Y     Z     [     \     ]     ^     _ */
    0xe7, 0xe8, 0xe9, 0xff, 0xe0, 0xff, 0x5f, 0x6d,
   /*  `     a     b     c     d     e     f     g */
    0x79, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,     /* 140 - 177 */
   /*  h     i     j     k     l     m     n     o */
    0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
   /*  p     q     r     s     t     u     v      w */
    0x97, 0x98, 0x99, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
   /*  x     y     z     {     |     }     ~   del */
    0xa7, 0xa8, 0xa9, 0xc0, 0x47, 0xd0, 0xa1, 0x6d,
};

const uint8 ebcdic_to_ascii[256] = {
/*      0     1     2     3    4     5      6   7 */
    0x00, 0x01, 0x02, 0x03, 0xFF, 0x09, 0x00, 0x7f,      /* 0x */
    0xff, 0xff, 0xff, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x19, 0x0a, 0x08, 0x08, 0xff,      /* 1x */
    0x18, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x1c, 0xff, 0xff, 0x0a, 0xff, 0xff,      /* 2x */
    0xff, 0xff, 0xff, 0xff, 0xff, 0x05, 0x06, 0x07,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x1e, 0x00, 0xff,      /* 3x */
    0xff, 0xff, 0xff, 0xff, 0x14, 0x15, 0xff, 0xff,
    ' ',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,      /* 4x */
    0xff, 0xff, '[',  '.',  '<',  '(',  '+',  '|',
    '&',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,      /* 5x */
    0xff, 0xff, '!',  '$',  '*',  ')',  ';',  '^',
    '-',  '/',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,      /* 6x */
    0xff, 0xff, 0xff, ',',  '%',  '_',  '>',  '?',
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,      /* 7x */
    0xff, '`',  ':',  '#',  '@',  '\'', '=',  '"',
    0xff, 'a',  'b',  'c',  'd',  'e',  'f',  'g',       /* 8x */
    'h',  'i',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 'j',  'k',  'l',  'm',  'n',  'o',  'p',       /* 9x */
    'q',  'r',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, '~',  's',  't',  'u',  'v',  'w',  'x',       /* Ax */
    'y',  'z',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,      /* Bx */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    '{',  'A',  'B',  'C',  'D',  'E',  'F',  'G',       /* Cx */
    'H',  'I',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    '}',  'J',  'K',  'L',  'M',  'N',  'O',  'P',       /* Dx */
    'Q',  'R',  0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    '\\', 0xff, 'S',  'T',  'U',  'V',  'W',  'X',       /* Ex */
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
       uint8       opbase;
       const char *name;
       uint8       type;
} t_opcode;

#define RR       01
#define RX       02
#define RS       03
#define SI       04
#define SS       05
#define XX       06
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
       { OP_BASR,      "BASR", RR },
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
       { OP_BAS,       "BAS",  RX },
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
       { OP_BXH,       "BXH",  RS|TWOOP },
       { OP_BXLE,      "BXLE", RS|TWOOP },
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
       { OP_STMC,      "STMC", RS|TWOOP },
       { OP_LRA,       "LRA",  RX },
       { OP_LMC,       "LMC",  RS|TWOOP },
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
       { OP_MVCL,      "MVCL", RR },
       { OP_CLCL,      "CLCL", RR },
       { OP_STNSM,     "STNSM", SI },
       { OP_STOSM,     "STOSM", SI },
       { OP_SIGP,      "SIGP",  RS },
       { OP_MC,        "MC",    SI },
       { OP_370,       "",      XX },
       { OP_STCTL,     "STCTL", RS },
       { OP_LCTL,      "LCTL",  RS },
       { OP_CS,        "CS",    RS },
       { OP_CDS,       "CDS",   RS },
       { OP_CLM,       "CLM",   RS },
       { OP_STCM,      "STCM",  RS },
       { OP_ICM,       "ICM",   RS },
       { OP_SRP,       "SRP",   SS|TWOOP },
       { 0,            NULL, 0 }
};

t_opcode  soptab[] = {
       { 0x02,        "STIDP", RS },
       { 0x03,        "STIDC", RS },
       { 0x04,        "SCK",   RS },
       { 0x05,        "STCK",  RS },
       { 0x06,        "SCKC",  RS },
       { 0x07,        "STCKC", RS },
       { 0x08,        "SPT",   RS },
       { 0x09,        "STPT",  RS },
       { 0x0A,        "SPKA",  RS },
       { 0x0B,        "IPK",   RS },
       { 0x0D,        "PTLB",  RS|ZEROOP },
       { 0x10,        "SPX",   RS },
       { 0x11,        "STPX",  RS },
       { 0x12,        "STAP",  RS },
       { 0x13,        "RRB",   RS },
       { 0,           NULL,    0}
};

void fprint_inst(FILE *of, uint16 *val) {
uint8           inst = (val[0] >> 8) &  0xff;
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
          case XX:
                    inst = val[0] & 0xff;
                    for (tab = soptab; tab->name != NULL; tab++) {
                       if (tab->opbase == inst) {
                          fputs(tab->name, of);
                          if ((tab->type & ZEROOP) == 0) {
                              fputc(' ', of);
                              fprint_val(of, val[1] & 0xfff, 16, 12, PV_RZRO);
                              if (val[1] & 0xf000)
                                  fprintf(of, "(%d)", (val[1] >> 12) & 0xf);
                          }
                          break;
                       }
                    }
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
          break;
      }
   }
   if (tab->name == NULL)
       fprintf(of, "?%02x?", inst);
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
    uint16          sval[4];
    int             i;
    int             l = 1;
    int             rdx = 16;
    t_opcode        *tab;
    uint32          num;

    if (sw & SWMASK ('D'))
        rdx = 10;
    else if (sw & SWMASK ('O'))
        rdx = 8;
    else if (sw & SWMASK ('H'))
        rdx = 16;
    if (sw & SWMASK ('M')) {
        for (tab = optab; tab->name != NULL; tab++) {
           if (tab->opbase == inst) {
              switch (tab->type & LNMSK) {
              case RR:
                        l = 2;
                        break;
              case XX:
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
    } else if (sw & SWMASK('B')) {
         l = 1;
    }

    if (sw & SWMASK ('C')) {
       fputc('\'', of);
       for(i = 0; i < l; i++) {
          uint8 ch = ebcdic_to_ascii[val[i] & 0xff];
          if (ch >= 0x20 && ch <= 0x7f)
              fprintf(of, "%c", ch);
          else
              fputc('_', of);
       }
       fputc('\'', of);
    } else if (sw & SWMASK ('M')) {
       i = 0;
       l = 0;
       if ((inst & 0xC0) == 0xC0) {
           num = (uint32)(val[i++] << 8);
           num |= (uint32)val[i++];
           sval[l++] = num;
           fprint_val(of, num, 16, 16, PV_RZRO);
           fputc(' ', of);
       }
       if ((inst & 0xC0) != 0) {
           num = (uint32)(val[i++] << 8);
           num |= (uint32)val[i++];
           sval[l++] = num;
           fprint_val(of, num, 16, 16, PV_RZRO);
           fputc(' ', of);
       }
       num = (uint32)(val[i++] << 8);
       num |= (uint32)val[i++];
       sval[l++] = num;
       fprint_val(of, num, 16, 16, PV_RZRO);
       fputc(' ', of);
       l = i;
       for(; i < 6; i+=2)
           fputs("     ", of);
       fputc(' ', of);
       fprint_inst(of, sval);
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
 * Collect offset in radix.
 */
t_stat get_off (CONST char *cptr, CONST char **tptr, uint32 radix, uint32 *val, char *m)
{
    t_stat r;

    r = SCPE_OK;
    *m = 0;
    *val = (uint32)strtotv (cptr, tptr, radix);
    if ((cptr == *tptr) || (*val > 0xfff))
        r = SCPE_ARG;
    else {
        cptr = *tptr;
        while (sim_isspace (*cptr)) cptr++;
        if (*cptr++ == '(') {
           *m = 1;
            while (sim_isspace (*cptr)) cptr++;
        }
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
    if ((cptr == *tptr) || (*val > 0xff))
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
    char            mod = 0;
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
          val[i] = ascii_to_ebcdic[(int)gbuf[i]];
       }
       return -(i - 1);
    }

    if (sw & SWMASK ('M')) {
        cptr = get_glyph(cptr, gbuf, 0);         /* Get opcode */
        for (tab = optab; tab->name != NULL; tab++) {
           if (sim_strcasecmp(tab->name, gbuf) == 0)
              break;
        }
        if (tab->name == NULL)
           return SCPE_ARG;
        val[0] = tab->opbase;
        switch (tab->type & LNMSK) {
        case RR:
               if (tab->type & IMDOP) {         /* Op number */
                   if ((r = get_imm(cptr, &tptr, rdx, &num)) != SCPE_OK)
                       return r;
                   val[1] = num;
               } else {                         /* Op r,r or Op r */
                   if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                        return r;
                   cptr = tptr;
                   val[1] = i << 4;
                   if (tab->type & ONEOP)
                       return -1;
                   if (*cptr != ',')
                       return SCPE_ARG;
                   cptr++;
                   if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                        return r;
                   val[1] |= i;
               }
               return -1;
        case RX:                                   /* Op r,off(r,r) */
               if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                   return r;
               cptr = tptr;
               if (*cptr != ',')
                   return SCPE_ARG;
               cptr++;
               val[1] = i << 4;
               if ((r = get_off(cptr, &tptr, rdx, &num, &mod)) != SCPE_OK)
                   return r;
               cptr = tptr;
               if (mod) {
                   if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                        return r;
                   cptr = tptr;
                   val[1] |= i;
                   if (*cptr == ',') {
                      cptr++;
                      while (sim_isspace (*cptr)) cptr++;
                      if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                          return r;
                      cptr = tptr;
                      num |= (i << 12);
                   }
                   if (*cptr != ')')
                     return SCPE_ARG;
               }
               val[2] = (num >> 8) & 0xff;
               val[3] = num & 0xff;
               return -3;
        case RS:                            /* Op r,r,off(r) or Op r,off(r) */
               if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                   return r;
               cptr = tptr;
               val[1] = i << 4;
               if (*cptr != ',')
                   return SCPE_ARG;
               cptr++;
               if ((tab->type & ZEROOP) == 0) {
                   if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                       return r;
                   cptr = tptr;
                   val[1] |= i;
                   if (*cptr != ',')
                       return SCPE_ARG;
                   cptr++;
                   while (sim_isspace (*cptr)) cptr++;
               }
               if ((r = get_off(cptr, &tptr, rdx, &num, &mod)) != SCPE_OK)
                   return r;
               cptr = tptr;
               if (mod) {
                   if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                       return r;
                   cptr = tptr;
                   num |= (i << 12);
                   if (*cptr != ')')
                       return SCPE_ARG;
               }
               val[2] = (num >> 8) & 0xff;
               val[3] = num & 0xff;
               return -3;
        case SI:                               /* Op off(r),num */
               if ((r = get_off(cptr, &tptr, rdx, &num, &mod)) != SCPE_OK)
                   return r;
               cptr = tptr;
               val[1] = 0;
               if (mod) {
                   if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                       return r;
                   cptr = tptr;
                   num |= (i << 12);
                   if (*cptr != ')')
                       return SCPE_ARG;
                   cptr++;
               }
               if ((tab->type & ZEROOP) == 0) {
                   if (*cptr != ',')
                      return SCPE_ARG;
                   cptr++;
                   if ((r = get_imm(cptr, &tptr, rdx, &num)) != SCPE_OK)
                      return r;
                   val[1] = num;
               }
               val[2] = (num >> 8) & 0xff;
               val[3] = num & 0xff;
               return -3;
        case SS:                          /* Op off(l,r),off(l,r) or Op off(l,r),off(r) */
               if ((r = get_off(cptr, &tptr, rdx, &num, &mod)) != SCPE_OK)
                   return r;
               cptr = tptr;
               if (mod) {
                   uint32  imm;
                   if ((r = get_imm(cptr, &tptr, rdx, &imm)) != SCPE_OK)
                      return r;
                   cptr = tptr;
                   if (tab->type & TWOOP) {
                       if (imm > 0xf)
                          return SCPE_ARG;
                       imm <<= 4;
                   }
                   val[1] = imm;
                   if (*cptr == ',') {
                       cptr++;
                       if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                           return r;
                       cptr = tptr;
                       num |= (i << 12);
                   }
                   if (*cptr++ != ')')
                     return SCPE_ARG;
                   while (sim_isspace (*cptr)) cptr++;
               }
               if (*cptr++ != ',')
                  return SCPE_ARG;
               val[2] = (num >> 8) & 0xff;
               val[3] = num & 0xff;
               if ((r = get_off(cptr, &tptr, rdx, &num, &mod)) != SCPE_OK)
                   return r;
               cptr = tptr;
               if (mod) {
                   if (tab->type & TWOOP) {
                       uint32  imm;
                       if ((r = get_imm(cptr, &tptr, rdx, &imm)) != SCPE_OK)
                          return r;
                       cptr = tptr;
                       if (imm > 0xf)
                           return SCPE_ARG;
                       val[1] |= imm;
                       if (*cptr++ != ',')
                          return SCPE_ARG;
                   }
                   if (*cptr != ')') {
                        if ((r = get_reg(cptr, &tptr, &i)) != SCPE_OK)
                            return r;
                        cptr = tptr;
                        num |= (i << 12);
                   }
                   if (*cptr != ')')
                       return SCPE_ARG;
               }
               val[4] = (num >> 8) & 0xff;
               val[5] = num & 0xff;
               return -5;
        }
    }
    num = get_uint(cptr, rdx, max[l], &r);
    for (i = 0; i < l && i < 4; i++)
        val[i] = (num >> (((l - 1) - i) * 8)) & 0xff;
    return -(l-1);
}

