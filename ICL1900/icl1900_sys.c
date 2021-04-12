/* icl1900_sys.c: ICL 1900 Simulator system interface.

   Copyright (c) 2017, Richard Cornwell

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

#include "sim_defs.h"
#include "icl1900_defs.h"
#include "sim_card.h"
#include <ctype.h>

t_stat  parse_sym(CONST char *cptr, t_addr addr, UNIT * uptr, t_value * val, int32 sw);

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char                sim_name[] = "ICL1900";

REG                *sim_PC = &cpu_reg[0];

int32               sim_emax = 1;

DEVICE             *sim_devices[] = {
    &cpu_dev,
    &cty_dev,        /* Must be first device after CPU */
#if NUM_DEVS_PTR > 0
    &ptr_dev,
#endif
#if NUM_DEVS_PTP > 0
    &ptp_dev,
#endif
#if NUM_DEVS_CDR > 0
    &cdr_dev,
#endif
#if NUM_DEVS_CDP > 0
    &cdp_dev,
#endif
#if NUM_DEVS_LPR > 0
    &lpr_dev,
#endif
#if NUM_DEVS_MT > 0
    &mt_dev,
#endif
#if NUM_DEVS_MTA > 0
    &mta_dev,
#endif
#if NUM_DEVS_EDS8 > 0
    &eds8_dev,
#endif
#if NUM_DEVS_EDS30 > 0
    &eds30_dev,
#endif
#if NUM_DEVS_DTC > 0
    &dtc_dev,
#endif
    NULL
};

/* Simulator stop codes */
const char         *sim_stop_messages[SCPE_BASE] = {
    0,
};

/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {"STATUS", DEBUG_STATUS, "Show status conditions"},
    {0, 0}
};

/* Simulator card debug controls */
DEBTAB              card_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show console data"},
    {"STATUS", DEBUG_STATUS, "Show status conditions"},
    {"CARD", DEBUG_CARD, "Show Card read/punches"},
    {0, 0}
};



uint8                parity_table[64] = {
    /* 0    1    2    3    4    5    6    7 */
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000
};

uint8           mem_to_ascii[64] = {
   /* x0   x1   x2   x3   x4   x5   x6   x7 */
     '0', '1', '2', '3', '4', '5', '6', '7',    /* 0x */
     '8', '9', ':', ';', '<', '=', '>', '?',    /* 1x */
     ' ', '!', '"', '#', '~', '%', '&', '\'',   /* 2x */
     '(', ')', '*', '+', ',', '-', '.', '/',    /* 3x */
     '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G',    /* 4x */
     'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',    /* 5x */
     'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',    /* 6x */
     'X', 'Y', 'Z', '[', '$', ']', '^', '_'     /* 7x */
};


const char          ascii_to_mem[128] = {
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,     /* 0 - 37 */
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   /*sp    !    "    #    $    %    &    ' */
    020, 021, 022, 023, 074, 025, 026, 027,     /* 4 -1 77 */
   /* (    )    *    +    ,    -    .    / */
    030, 031, 032, 033, 034, 035, 036, 037,
   /* 0    1    2    3    4    5    6    7 */
    000, 001, 002, 003, 004, 005, 006, 007,
   /* 8    9    :    ;    <    =    >    ? */
    010, 011, 012, 013, 014, 015, 016, 017,
   /* @    A    B    C    D    E    F    G */
    040, 041, 042, 043, 044, 045, 046, 047,     /* 1 -1- 137 */
   /* H    I    J    K    L    M    N    O */
    050, 051, 052, 053, 054, 055, 056, 057,
   /* P    Q    R    S    T    U    V    W */
    060, 061, 062, 063, 064, 065, 066, 067,
   /* X    Y    Z    [    \    ]    ^    _ */
    070, 071, 072, 073,  -1, 075, 076, 077,
   /* `    a    b    c    d    e    f    g */
     -1, 041, 042, 043, 044, 045, 046, 047,     /* 14 -1 177 */
   /* h    i    j    k    l    m    n    o */
    050, 051, 052, 053, 054, 055, 056, 057,
   /* p    q    r    s    t    u    v    w */
    060, 061, 062, 063, 064, 065, 066, 067,
   /* x    y    z    {    |    }    ~   del*/
    070, 071, 072, 024,  -1,  -1, 024,  -1,
};

/*
 * Translate internal code to Holerith for punch cards.
 * This uses IBM029 encoding rather then ICL1900 punch codes.
 *
 *   Char     029       ICL1900
 *   #          8+3     8+3
 *   @          8+4     8+4
 *   (       12+8+5     8+5
 *   )       11+8+5     8+6
 *   ]       10+8+6     8+7
 *   +       12+10      12+8+2
 *   .       12+8+3     12+8+3
 *   :          8+5     12+8+4
 *   ;       11+8+6     12+8+5
 *   '          8+5     12+8+6
 *   !       10+8+2     12+8+7
 *   [       12+8+4     11+8+2
 *   $       11+8+3     11+8+3
 *   *       11+8+4     11+8+4
 *   >          8+6     11+8+5
 *   =       10+8+5     11+8+6
 *   ^       10+8+7     11+8+7
 *  lb       11+8+6     10+8+2  \
 *   ,       10+8+3     10+8+3
 *   %       10+8+4     10+8+4
 *   ?          8+2     10+8+5
 *   =       10+8+5     10+8+6
 *   _          8+6     10+8+7
 */

uint16 mem_to_hol[64] = {
   /*  0      1      2      3      4      5      6      7   */
     0x200, 0x100, 0x080, 0x040, 0x020, 0x010, 0x008, 0x004,  /* 0x */
   /*  8      9      :      ;      <      =      >      ?   */
     0x002, 0x001, 0x082, 0x40A, 0x80A, 0x00A, 0x20A, 0x206,  /* 1x */
   /*  bl     !      "      #      ~      %      &      '   */
     0x000, 0x482, 0x006, 0x042, 0x806, 0x222, 0x800, 0x012,  /* 2x */
   /*  (      )      *      +      ,      -      .      /   */
     0x812, 0x412, 0x422, 0x80A, 0x242, 0x400, 0x842, 0x300,  /* 3x */
   /*  @      A      B      C      D      E      F      G   */
     0x022, 0x900, 0x880, 0x840, 0x820, 0x810, 0x808, 0x804,  /* 4x */
   /*  H      I      J      K      L      M      N      O   */
     0x802, 0x801, 0x500, 0x480, 0x440, 0x420, 0x410, 0x408,  /* 5x */
   /*  P      Q      R      S      T      U      V      W   */
     0x404, 0x402, 0x401, 0x280, 0x240, 0x220, 0x210, 0x208,  /* 6x */
   /*  X      Y      Z      [      $      ]      ^      _   */
     0x204, 0x202, 0x201, 0xA00, 0x442, 0x882, 0x406, 0x212,  /* 7x */
};

uint8 hol_to_mem[4096];


/* Load a card image file into memory.  */

t_stat
sim_load(FILE * fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    char                buffer[160];
    int                 i, j, k;
    int                 addr, data;
    uint8               image[80];
    int                 checksum;
    char                ch;

    if (match_ext(fnam, "wush")) {
        while (fgets(buffer, 100, fileref)) {
            char *p = &buffer[0];
            /* Convert bits into image */
            if (*p++ != '*') {
                fprintf(stderr, "Buffer %s\n", buffer);
                return SCPE_FMT;
            }
            addr = 0;
            while (*p != '\0') {
                if (*p == ':')
                    break;
                if (*p < '0' || *p > '7') {
                    break;
                }
                ch = ascii_to_mem[*p++ & 0177];
                addr = (addr << 3) | ch;
            }
            while (*p != '*') {
                if (*p == '\0' || *p == '\n') {
                    fprintf(stderr, "Buffer %s\n", buffer);
                    return SCPE_FMT;
                }
                p++;
            }
            p++;
            data = 0;
            while (*p != '\0') {
                if (*p < '0' || *p > '7') 
                    break;
                data = (data << 3) | (*p++ - '0');
            }
            if (addr == 077777777) {
                RC = data;
                break;
            }
            if (addr < 8)
               XR[addr] = data;
            M[addr] = data;
        }
        return SCPE_OK;

    } else if (match_ext(fnam, "card")) {
        if (fgets(buffer, 100, fileref) == NULL)
            return SCPE_OK;

        addr = 020;
        while (fgets(buffer, 100, fileref)) {
            /* Convert bits into image */
            memset(image, 0, sizeof(image));
            for (j = 0; j < 80; j++) {
                if (buffer[j] == '\r' && buffer[j+1] == '\n')
                   break;
                if (buffer[j] == '\n')
                   break;
                if ((buffer[j] & 0377) == 0243)
                   ch = 024;
                else
                   ch = ascii_to_mem[buffer[j] & 0177];
                if (ch < 0) {
                    fprintf(stderr, "Char %c: %s\n", buffer[j], buffer);
                    return SCPE_FMT;
                }
                image[j] = ch;
            }
            for (j = 0; j < 64; ) {
                     data = 0;
                     for (k = 0; k < 4; k++)
                         data = (data << 6) | image[j++];
                     if (addr < 8)
                         XR[addr++] = data;
                     M[addr++] = data;
            }
        }
        return SCPE_OK;
    } else if (match_ext(fnam, "txt")) {

        while (fgets(buffer, 100, fileref)) {
            /* Convert bits into image */
            memset(image, 0, sizeof(image));
            for (j = 0; j < 80; j++) {
                if (buffer[j] == '\r' && buffer[j+1] == '\n')
                   break;
                if (buffer[j] == '\n')
                   break;
                if ((buffer[j] & 0377) == 0243)
                   ch = 024;
                else
                   ch = ascii_to_mem[buffer[j] & 0177];
                if (ch < 0) {
                    fprintf(stderr, "Char %c: %s", buffer[j], buffer);
                    return SCPE_FMT;
                }
                image[j] = ch;
            }
            if (image[0] != 073) {
                fprintf(stderr, "F: %s", buffer);
                return SCPE_FMT;
            }
            switch(image[3]) {
            case 0:
                 checksum = 0;
                 for (j = 0; j < 4; j++)
                     checksum = (checksum << 6) | image[j];
                 addr = 0;
                 for (; j < 8; j++)
                     addr = (addr << 6) | image[j];
                 checksum = (checksum + addr) & FMASK;
                 for (i = 3; i < image[1]; i++) {
                     data = 0;
                     for (k = 0; k < 4; k++)
                         data = (data << 6) | image[j++];
                     checksum = (checksum + data) & FMASK;
                     M[addr++] = data;
                 }
                 data = 0;
                 for (k = 0; k < 4; k++)
                     data = (data << 6) | image[j++];
                 if ((FMASK & (checksum + data)) != 0)
                     fprintf(stderr, "Check %08o %08o %08o: %s", addr, data, checksum, buffer);
                 break;
            case 1:
                 fprintf(stderr, "%c%c%c%c\n", buffer[4], buffer[5], buffer[6], buffer[7]);
                 break;
            case 2:
            case 3:
                 checksum = 0;
                 for (j = 0; j < 4; j++)
                     checksum = (checksum << 6) | image[j];
                 addr = 0;
                 for (; j < 8; j++)
                     addr = (addr << 6) | image[j];
                 checksum = (checksum + addr) & FMASK;
                 RC = addr;
                 data = 0;
                 for (i = 3; i < image[1]; i++) {
                     data = 0;
                     for (k = 0; k < 4; k++)
                         data = (data << 6) | image[j++];
                     checksum = (checksum + data) & FMASK;
                 }
                 for (k = 0; k < 4; k++)
                     data = (data << 6) | image[j++];
                 data = FMASK & (checksum + data);
                 if (data != 0)
                     fprintf(stderr, "Check %08o %08o: %s", addr, data, buffer);
                 break;
            case 4:
            case 5:
            case 6:
                 fprintf(stderr, "%o %c%c%c%c\n",  image[3],buffer[4], buffer[5], buffer[6], buffer[7]);
                 break;
            default:
                 fprintf(stderr, "B? :%s", buffer);
                 return SCPE_FMT;
            }
        }
        return SCPE_OK;
    }
    return SCPE_NOFNC;
}


#define TYPE_A    0
#define TYPE_B    1
#define TYPE_C    2
#define TYPE_D    3

/* Opcodes */
t_opcode  ops[] = {
       { "LDX",     TYPE_A },       /* Load to X */
       { "ADX",     TYPE_A },       /* Add to X */
       { "NGX",     TYPE_A },       /* Negative to X */
       { "SBX",     TYPE_A },       /* Subtract from X */
       { "LDXC",    TYPE_A },       /* Load into X with carry */
       { "ADXC",    TYPE_A },       /* Add to X with carry */
       { "NGXC",    TYPE_A },       /* Negative to X with carry */
       { "SBXC",    TYPE_A },       /* Subtract from X with carry */
       { "STO",     TYPE_A },       /* Store contents of X */
       { "ADS",     TYPE_A },       /* Add X to store */
       { "NGS",     TYPE_A },       /* Negative into Store */
       { "SBS",     TYPE_A },       /* Subtract from store */
       { "STOC",    TYPE_A },       /* Store contents of X with carry */
       { "ADSC",    TYPE_A },       /* Add X to store with carry */
       { "NGSC",    TYPE_A },       /* Negative into Store with carry */
       { "SBSC",    TYPE_A },       /* Subtract from store with carry */
       { "ANDX",    TYPE_A },       /* Logical AND into X */
       { "ORX",     TYPE_A },       /* Logical OR into X */
       { "ERX",     TYPE_A },       /* Logical XOR into X */
       { "OBEY",    TYPE_A },       /* Obey instruction at N */
       { "LDCH",    TYPE_A },       /* Load Character to X */
       { "LDEX",    TYPE_A },       /* Load Exponent */
       { "TXU",     TYPE_A },       /* Test X unequal */
       { "TXL",     TYPE_A },       /* Test X Less */
       { "ANDS",    TYPE_A },       /* Logical AND into store */
       { "ORS",     TYPE_A },       /* Logical OR into store */
       { "ERS",     TYPE_A },       /* Logical XOR into store */
       { "STOZ",    TYPE_A },       /* Store Zero */
       { "DCH",     TYPE_A },       /* Deposit Character to X */
       { "DEX",     TYPE_A },       /* Deposit Exponent */
       { "DSA",     TYPE_A },       /* Deposit Short Address */
       { "DLA",     TYPE_A },       /* Deposit Long Address */
       { "MPY",     TYPE_A },       /* Multiply */
       { "MPR",     TYPE_A },       /* Multiply and Round */
       { "MPA",     TYPE_A },       /* Multiply and Accumulate */
       { "CDB",     TYPE_A },       /* Convert Decimal to Binary */
       { "DVD",     TYPE_A },       /* Unrounded Double Length Divide */
       { "DVR",     TYPE_A },       /* Rounded Double Length Divide */
       { "DVS",     TYPE_A },       /* Single Length Divide */
       { "CBD",     TYPE_A },       /* Convert Binary to Decimal */
       { "BZE",     TYPE_B },       /* Branch if X is Zero */
       { "BZE",     TYPE_B },
       { "BNZ",     TYPE_B },       /* Branch if X is not Zero */
       { "BNZ",     TYPE_B },
       { "BPZ",     TYPE_B },       /* Branch if X is Positive or zero */
       { "BPZ",     TYPE_B },
       { "BNG",     TYPE_B },       /* Branch if X is Positive or zero */
       { "BNG",     TYPE_B },
       { "BUX",     TYPE_B },       /* Branch on Unit indexing */
       { "BUX",     TYPE_B },
       { "BDX",     TYPE_B },       /* Branch on Double Indexing */
       { "BDX",     TYPE_B },
       { "BCHX",    TYPE_B },       /* Branch on Character Indexing */
       { "BCHX",    TYPE_B },
       { "BCT",     TYPE_B },       /* Branch on Count - BC */
       { "BCT",     TYPE_B },
       { "CALL",    TYPE_B },       /* Call Subroutine */
       { "CALL",    TYPE_B },
       { "EXIT",    TYPE_B },       /* Exit Subroutine */
       { "EXIT",    TYPE_B },
       { NULL,      TYPE_D },       /* Branch unconditional */
       { NULL,      TYPE_D },
       { "BFP",     TYPE_B },       /* Branch state of floating point accumulator */
       { "BFP",     TYPE_B },
       { "LDN",     TYPE_A },       /* Load direct to X */
       { "ADN",     TYPE_A },       /* Add direct to X */
       { "NGN",     TYPE_A },       /* Negative direct to X */
       { "SBN",     TYPE_A },       /* Subtract direct from X */
       { "LDNC",    TYPE_A },       /* Load direct into X with carry */
       { "ADNC",    TYPE_A },       /* Add direct to X with carry */
       { "NGNC",    TYPE_A },       /* Negative direct to X with carry */
       { "SBNC",    TYPE_A },       /* Subtract direct from X with carry */
       { "SL",      TYPE_C },       /* Shift Left */
       { "SLD",     TYPE_C },       /* Shift Left Double */
       { "SR",      TYPE_C },       /* Shift Right */
       { "SRD",     TYPE_C },       /* Shift Right Double */
       { "NORM",    TYPE_A },       /* Nomarlize Single -2 +FP */
       { "NORMD",   TYPE_A },       /* Normalize Double -2 +FP */
       { "MVCH",    TYPE_A },       /* Move Characters - BC */
       { "SMO",     TYPE_A },       /* Supplementary Modifier - BC  */
       { "ANDN",    TYPE_A },       /* Logical AND direct into X */
       { "ORN",     TYPE_A },       /* Logical OR direct into X */
       { "ERN",     TYPE_A },       /* Logical XOR direct into X */
       { "NULL",    TYPE_A },       /* No Operation */
       { "LDCT",    TYPE_A },       /* Load Count */
       { "MODE",    TYPE_A },       /* Set Mode */
       { "MOVE",    TYPE_A },       /* Copy N words */
       { "SUM",     TYPE_A },       /* Sum N words */
       { "FLOAT",   TYPE_A },       /* Convert Fixed to Float +FP */
       { "FIX",     TYPE_A },       /* Convert Float to Fixed +FP */
       { "FAD",     TYPE_A },       /* Floating Point Add +FP */
       { "FSB",     TYPE_A },       /* Floating Point Subtract +FP */
       { "FMPY",    TYPE_A },       /* Floating Point Multiply +FP */
       { "FDVD",    TYPE_A },       /* Floating Point Divide +FP */
       { "LFP",     TYPE_A },       /* Load Floating Point +FP */
       { "SFP",     TYPE_A },       /* Store Floating Point +FP */
       { "140",     TYPE_A },
       { "141",     TYPE_A },
       { "142",     TYPE_A },
       { "143",     TYPE_A },
       { "144",     TYPE_A },
       { "145",     TYPE_A },
       { "146",     TYPE_A },
       { "147",     TYPE_A },
       { "150",     TYPE_A },
       { "151",     TYPE_A },
       { "152",     TYPE_A },
       { "153",     TYPE_A },
       { "154",     TYPE_A },
       { "155",     TYPE_A },
       { "156",     TYPE_A },
       { "157",     TYPE_A },
       { "160",     TYPE_A },
       { "161",     TYPE_A },
       { "162",     TYPE_A },
       { "163",     TYPE_A },
       { "164",     TYPE_A },
       { "165",     TYPE_A },
       { "166",     TYPE_A },
       { "167",     TYPE_A },
       { "170",     TYPE_A },
       { "171",     TYPE_A },
       { "172",     TYPE_A },
       { "173",     TYPE_A },
       { "174",     TYPE_A },
       { "175",     TYPE_A },
       { "176",     TYPE_A },
       { "177",     TYPE_A }
};

const char  *type_d[] = {   "BRN", "BVS", "BVSR", "BVC", "BVCR", "BCS", "BCC", "BVCI" };

char   type_c[] = {  'C', 'L', 'A', 'V' };


/* Print out an instruction */
void
print_opcode(FILE * of, t_value val)
{
    int        op;
    int        x;
    int        m;
    int        n;
    t_opcode  *tab;

    op = 0177 & (val >> 14);;
    x = 07 & (val >> 21);
    m = 03 & (val >> 12);
    n = 07777 & val;
    tab = &ops[op];
    fprintf(of, "   *%03o  ", op);
    switch(tab->type) {
    case TYPE_A:
            fprintf(of, "%s %o", tab->name, x);
            if (m != 0) {
               fputc(' ',of);
               fputc('0'+m,of);
            }
            fprintf(of, "/%04o", n);
            break;
    case TYPE_B:
            fprintf(of, "%s %o/%05o", tab->name, x, val & 077777);
            break;
    case TYPE_C:
            fprintf(of, "%s %o", tab->name, x);
            if (m != 0) {
               fputc(' ',of);
               fputc('0'+m,of);
            }
            fprintf(of, "/%c+%02o", type_c[(n >> 10) & 3], n & 01777);
            break;
    case TYPE_D:
            fprintf(of, "%s %05o", type_d[x], val & 077777);
            break;
    }
}

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       pointer to values
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        return  =       status code
*/

t_stat
fprint_sym(FILE * of, t_addr addr, t_value * val, UNIT * uptr, int32 sw)
{
    t_value             inst = val[0];
    int                 i;

    fputc(' ', of);
    fprint_val(of, inst, 8, 24, PV_RZRO);

    if (sw & SWMASK('M')) {     /* Symbolic Assembly */
        print_opcode(of, inst);
    }
    if (sw & SWMASK('C')) {     /* Char mode opcodes */
        fputc('\'', of);
        for (i = 18; i >= 0; i-=6) {
            int                 ch;

            ch = (int)(inst >> i) & 077;
            fputc(mem_to_ascii[ch], of);
        }
        fputc('\'', of);
    }
    return SCPE_OK;
}

int
find_opcode(char *op, int *val)
{
    int        i;
    int        v;

    *val = -1;
    if (*op >= '0' && *op <= '7') {
        for (v = i = 0; op[i] != '\0'; i++) {
            if (op[i] >= '0' && op[i] <= '7')
               v = (v << 3) + (op[i] - '0');
            else
               break;
            if (v > 0177)
               return -1;
        }
        if (op[i] == 0 && v <= 0177)
            return v;
    }
    for(i = 0;  i <= 0177; i++) {
        if (ops[i].name != NULL && sim_strcasecmp(op, ops[i].name) == 0)
            return i;
    }
    for(i = 0; i < 8; i++) {
        if (sim_strcasecmp(op, type_d[i]) == 0) {
            *val = i;
            return 074;
        }
    }
    return -1;
}

/* Symbolic input

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        uptr    =       pointer to unit
        *val    =       pointer to output values
        sw      =       switches
   Outputs:
        status  =       error status
*/

t_stat
parse_sym(CONST char *cptr, t_addr addr, UNIT * uptr, t_value * val, int32 sw)
{
    int                 i;
    int                 n;
    int                 x;
    int                 m;
    int                 op;
    char                gbuf[100];
    t_stat              r;

if (sw & SWMASK ('C')) {
   cptr = get_glyph_quoted(cptr, gbuf, 0);   /* Get string */
   x = 18;
   n = 0;
   m = 0;
   for(i = 0; gbuf[i] != 0; i++) {
      char c = ascii_to_mem[(int)gbuf[i]];
      if (c == -1)
         return SCPE_ARG;
      n |= c << x;
      x -= 6;
      if (x < 0) {
         val[m++] = n;
         n = 0;
         x = 18;
      }
   }
   if (x != 18)
      val[m++] = n;
   return -(m - 1);
}

if (sw & SWMASK ('M')) {
    cptr = get_glyph(cptr, gbuf, 0);         /* Get opcode */
    op = find_opcode(gbuf, &i);
    if (op < 0)
       return SCPE_ARG;
    while (sim_isspace (*cptr)) cptr++;
    n = 0;
    m = -1;
    x = -1;
    if (*cptr >= '0' || *cptr <= '7') {
        x = *cptr++ - '0';
    }
    while (sim_isspace (*cptr)) cptr++;
    if (*cptr > '0' || *cptr <= '3') {
        m = *cptr++ - '0';
    }
    while (sim_isspace (*cptr)) cptr++;
    *val = (0177 & op) << 14;
    if (x >= 0)
       *val |= (07 & x) << 21;

    switch (ops[op].type) {
    case TYPE_A:   /* OP x m/n  or OP x /n */
         if (m > 0)
            *val |= (m & 03) << 12;
         if (*cptr == '/') {
            cptr++;
            n = get_uint(cptr, 8, 07777, &r);
            if (r != SCPE_OK)
               return r;
         }
         break;

    case TYPE_B:   /* OP x /n */
         if (m >= 0)
            return SCPE_ARG;
         *val |= (m & 03) << 12;
         if (*cptr == '/') {
            cptr++;
            n = get_uint(cptr, 8, 077777, &r);
            if (r != SCPE_OK)
               return r;
         }
         break;

    case TYPE_C:   /* OP x m/c+n or OP X /c+n */
         if (m > 0)
            *val |= (m & 03) << 12;
         if (*cptr == '/') {
            cptr++;
            for (i = 0; i< 4; i++) {
                if (type_c[i] == *cptr) {
                   cptr++;
                   break;
                }
            }
            if (*cptr != '+')
               return SCPE_ARG;
            cptr++;
            n = get_uint(cptr, 8, 01777, &r);
            if (r != SCPE_OK)
               return r;
            n |= i << 10;
         }
         break;

    case TYPE_D:   /* type_d /n  */
         if (i < 0 && x >= 0) {
            i = x;
            x = -1;
         }
         if (m >= 0 || x >= 0)
            return SCPE_ARG;
         if (*cptr == '/') {
            cptr++;
            n = get_uint(cptr, 8, 077777, &r);
            if (r != SCPE_OK)
               return r;
         }
         *val |= i << 21;
         break;
    }
    *val |= n;
    return 0;
}
n = get_uint(cptr, 8, 077777777, &r);
if (r != SCPE_OK)
   return r;
*val = n;
return 0;
}

