/* i701_sys.c: IBM 701 Simulator system interface.



   Copyright (c) 2005-2016, Richard Cornwell

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

#include "i7090_defs.h"
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

char                sim_name[] = "IBM 701";

REG                *sim_PC = &cpu_reg[0];

int32               sim_emax = 1;

DEVICE             *sim_devices[] = {
    &cpu_dev,
    &chan_dev,
#ifdef NUM_DEVS_CDR
    &cdr_dev,
#endif
#ifdef NUM_DEVS_CDP
    &cdp_dev,
#endif
#ifdef NUM_DEVS_LPR
    &lpr_dev,
#endif
#ifdef MT_CHANNEL_ZERO
    &mtz_dev,
#endif
#ifdef NUM_DEVS_DR
    &drm_dev,
#endif
    NULL
};

#ifdef NUM_DEVS_CDR
DIB   cdp_dib = { CH_TYP_PIO, 1, 02000, 07777, &cdp_cmd, &cdp_ini };
#endif
#ifdef NUM_DEVS_CDP
DIB   cdr_dib = { CH_TYP_PIO, 1, 04000, 07777, &cdr_cmd, NULL };
#endif
#ifdef NUM_DEVS_DR
DIB   drm_dib = { CH_TYP_PIO, 1, 0200, 07774, &drm_cmd, &drm_ini };
#endif
#ifdef NUM_DEVS_LPR
DIB   lpr_dib = { CH_TYP_PIO, 1, 01000, 07777, &lpr_cmd, &lpr_ini };
#endif
#ifdef MT_CHANNEL_ZERO
DIB   mt_dib = { CH_TYP_PIO, NUM_UNITS_MT, 0400, 07770, &mt_cmd, &mt_ini };
#endif


/* Simulator stop codes */
const char         *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "IO device not ready",
    "HALT instruction",
    "Breakpoint",
    "Unknown Opcode",
    "Nested indirects exceed limit",
    "Nested XEC's exceed limit",
    "I/O Check opcode",
    "Memory management trap during trap",
    "7750 invalid line number",
    "7750 invalid message",
    "7750 No free output buffers",
    "7750 No free input buffers", "Error?", "Error2", 0
};

/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CHANNEL", DEBUG_CHAN, "Debug Channel use"},
    {"TRAP", DEBUG_TRAP, "Show CPU Traps"},
    {"CMD", DEBUG_CMD, "Show device commands"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show detailed device information"},
    {"EXP", DEBUG_EXP, "Show device exceptions"},
    {"SENSE", DEBUG_SNS, "Show sense data on 7909 channel"},
    {0, 0}
};

DEBTAB              crd_debug[] = {
    {"CHAN", DEBUG_CHAN},
    {"CMD", DEBUG_CMD},
    {"DATA", DEBUG_DATA},
    {"DETAIL", DEBUG_DETAIL},
    {"EXP", DEBUG_EXP},
    {"CARD", DEBUG_CARD},
    {0, 0}
};

/* Load a card image file into memory.  */

t_stat
sim_load(FILE * fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    t_uint64            wd;
    t_uint64            mask;
    int                 addr = 0;
    int                 dlen = 0;
    char               *p;
    char                buf[160];

    if (match_ext(fnam, "crd")) {
        int                 firstcard = 1;
        uint16              cbuf[80];
        t_uint64            lbuff[24];
        int                 i;

        while (addr < MAXMEMSIZE && sim_fread(cbuf, 2, 80, fileref) == 80) {

            /* Bit flip into read buffer */
            for (i = 0; i < 24; i++) {
                int                 bit = 1 << (i / 2);
                int                 b = 36 * (i & 1);
                int                 col;

                mask = 1;
                wd = 0;
                for (col = 35; col >= 0; mask <<= 1) {
                    if (cbuf[col-- + b] & bit)
                        wd |= mask;
                }
                lbuff[i] = wd;
            }
            i = 2;
            if (firstcard) {
                addr = 0;
                dlen = 3 + (int)((lbuff[0] >> 18) & 077777);
                firstcard = 0;
                i = 0;
            } else if (dlen == 0) {
                addr = (int)(lbuff[0] & 077777);
                dlen = (int)(lbuff[0] >> 18) & 077777;
            }
            for (; i < 24 && dlen > 0; i++) {
                if (addr >= MAXMEMSIZE) /* safety check! */
                    break;
                M[addr++] = lbuff[i]; /* no swap necessary... */
                dlen--;
            }
        }
    } else if (match_ext(fnam, "oct")) {
        while (fgets((char *)buf, 80, fileref) != 0) {
            /* Grab address; these are may be half or full word addresses! */
            /* advance past white space... */
            for(p = (char *)buf; *p == ' ' || *p == '\t'; p++);
            /* any lines with first meaningful char of ';' are comment lines */
            if (*p == ';' || *p == '\n' || *p == '\r')
                continue; /* skip lines with no meaningful content! */
            /* Grab address; these are may be half or full word addresses! */
            for (addr = 0; *p >= '0' && *p <= '7'; p++)
                addr = (addr << 3) + *p - '0';
            /* advance past white space... */
            for(; *p == ' ' || *p == '\t'; p++);
            /* any lines containing ';' after a data field
               ignore the rest of the line as a comment */
            while (*p != ';' && *p != '\n' && *p != '\r' && *p != '\0') {
                for (wd = 0; *p >= '0' && *p <= '7'; p++)
                    wd = (wd << 3) + *p - '0';
                if (addr > 07777) {
                    if (dlen < MAXMEMSIZE)
                        /* `addr` & 03777 is full-word address!... */
                        M[addr++ & 03777] = wd & 0777777777777; /* no swap necessary... */
                } else {
                    /* `addr` is half-word address! */
                    dlen = addr >> 1; /* `dlen` is now a full-word address! */
                    if (dlen < MAXMEMSIZE) {
                        if (addr & 1) {
                            M[dlen] &= LMASK;
                            M[dlen] |= wd & RMASK;
                        } else {
                            M[dlen] &= RMASK;
                            M[dlen] |= (wd << 18) & LMASK;
                        }
                        addr++;
                   }
                }
                /* advance past white space... */
                for(; *p == ' ' || *p == '\t'; p++);
            }
        }
    } else if (match_ext(fnam, "sym")) {
        while (fgets((char *)buf, 80, fileref) != 0) {
            for (p = (char *)buf; *p == ' ' || *p == '\t'; p++);
            /* any lines with first meaningful char of ';' are comment lines */
            if (*p == ';' || *p == '\n' || *p == '\r' || *p == '\0')
                continue; /* skip lines with no meaningful content! */
            /* Grab address; this is a half-word address! */
            for (addr = 0; *p >= '0' && *p <= '7'; p++)
               addr = (addr << 3) + *p - '0';
            while (*p == ' ' || *p == '\t') p++;
            if (sim_strncasecmp(p, "BCD", 3) == 0) {
                p += 3;
                parse_sym(++p, addr, &cpu_unit, &wd, SWMASK('C'));
            } else if (sim_strncasecmp(p, "OCT", 3) == 0) {
                p += 3;
                for(; *p == ' ' || *p == '\t'; p++);
                parse_sym(p, addr, &cpu_unit, &wd, 0);
            } else {
                parse_sym(p, addr, &cpu_unit, &wd, SWMASK('M'));
            }
            /* `addr` is half-word address! */
            dlen = addr >> 1; /* `dlen` is full-word address! */
            if (dlen < MAXMEMSIZE)
                if (addr & 1) {
                    M[dlen] &= LMASK;
                    M[dlen] |= wd & RMASK;
                } else {
                    M[dlen] &= RMASK;
                    M[dlen] |= (wd << 18) & LMASK;
                }
        }
    } else
        return SCPE_ARG;
    return SCPE_OK;
}

/* Symbol tables */
typedef struct _opcode
{
    uint16              opbase;
    CONST char          *name;
}
t_opcode;

/* Opcodes */
t_opcode            base_ops[] = {
    {0, "STOP"},
    {1, "TR"},
    {2, "TRO"},
    {3, "TRP"},
    {4, "TRZ"},
    {5, "SUB"},
    {6, "R SUB"},
    {7, "SUB AB"},
    {8, "NO OP"},
    {9, "ADD"},
    {10, "R ADD"},
    {11, "ADD AB"},
    {12, "STORE"},
    {13, "STORE A"},
    {14, "STORE MQ"},
    {15, "LOAD MQ"},
    {16, "MPY"},
    {17, "MPY R"},
    {18, "DIV"},
    {19, "ROUND"},
    {20, "L LEFT"},
    {21, "L RIGHT"},
    {22, "A LEFT"},
    {23, "A RIGHT"},
    {24, "READ"},
    {25, "READ B"},
    {26, "WRITE"},
    {27, "WRITE EF"},
    {28, "REWIND"},
    {29, "SET DR"},
    {30, "SENSE"},
    {31, "COPY"},
    {13 + 040, "EXTR"},
    {0, NULL}
};

const char *chname[] = { "*" };



/* Parse address

   Inputs:
        *dptr   =       pointer to device.
        *cptr   =       pointer to string to scan.
        **tptr  =       pointer past the last scanned character.
   Outputs:
        address with sign.
*/

t_addr
parse_addr(DEVICE *dptr, const char *cptr, const char **tptr) {
    t_addr      v = 0;
    int         s = 0;
    if (dptr != &cpu_dev)
        return 0;
    /* Skip white space */
    while (isspace(*cptr))
        cptr++;
    if (*cptr == '-') {
        cptr++;
        s = 1;
    } else
        if (*cptr == '+')
            cptr++;
    *tptr = cptr;
    while(*cptr >= '0' && *cptr <= '7') {
        v <<= 3;
        v += *cptr++ - '0';
    }
    if (v >= 6144) /* >= half-word size of MEMSIZE * 1.5 (with sign)  */
        return 0; /* indicate error?  */
    if (s)
        v |= 0400000; /* sign in instruction format sign bit! */
    /* if valid characters have been processed, *tptr == cptr means success! */
    *tptr = cptr;
    return v;
}



void sys_init(void) {
        sim_vm_parse_addr = &parse_addr;
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
    t_uint64            inst = *val;

/* Print value in octal first */
    fputc(' ', of);
    fprint_val(of, inst, 8, addr > 07777 ? 36 : 18, PV_RZRO);

    if (sw & SWMASK('M')) {
        int     op = (int)(inst >> 12);
        int     i;
        if (op != (040 + 13)) /* if EXTR instruction... */
           op &= 037;
        fputs("      ", of); /* space between octal value and sym value */
        if (addr > 07777)
            return SCPE_ARG;
        for(i = 0; base_ops[i].name != NULL; i++) {
            if (base_ops[i].opbase == op) {
                fputs(base_ops[i].name, of);
                break;
            }
        }
        fputc(' ', of);
        if (inst & 0400000L)
            fputc('-', of);
        else
            fputc(' ', of);
        fprint_val(of, inst & 0000000007777L, 8, 12, PV_RZRO);
    }

    if (sw & SWMASK('C')) {
        int                 i;

        fputs("   '", of);
        for (i = addr > 07777 ? 5 : 2; i >= 0; i--) {
            int                 ch;

            ch = (int)(inst >> (6 * i)) & 077;
            fputc(sim_six_to_ascii[ch], of);
        }
        fputc('\'', of);
    }
    return SCPE_OK;
}

t_opcode           *
find_opcode(char *op, t_opcode * tab)
{
    while (tab->name != NULL) {
        if (*tab->name != '\0' && strcmp(op, tab->name) == 0)
            return tab;
        tab++;
    }
    return NULL;
}

/* extract opcode string that may contain spaces

   Inputs:
        iptr        =   pointer to input string
        optr        =   pointer to output string

   Outputs
        result      =   pointer to next character in input string
*/

CONST char        *
get_opcode(CONST char *iptr, char *optr)
{

while ((*iptr != 0) && (*iptr != ',') &&
       (*iptr == ' ' || ((*iptr >= 'A') && (*iptr <= 'Z')) ||
                        ((*iptr >= 'a') && (*iptr <= 'z')))) {
    if (*iptr >= 'a')                   /* force to be upper case */
        *optr++ = *iptr++ - 32;
    else *optr++ = *iptr++;
    }
if (*iptr == ',')                       /* skip input terminator */
    iptr++;
while (*--optr == ' ');                 /* remove trailing spaces */
optr++;
*optr = 0;                              /* terminate result string */
while (isspace (*iptr))                 /* absorb additional input spaces */
    iptr++;
return iptr;
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
    t_value             d;
    t_addr              tag;
    int                 sign;
    char                opcode[100];
    const char          *arg;

    while (isspace(*cptr))
        cptr++;
    d = 0;
    if (sw & SWMASK('M')) { /* symbolic code... */
        t_opcode           *op;

        i = 0;
        sign = 0;
        
        /* Grab opcode */
        cptr = get_opcode(cptr, opcode);

        if ((op = find_opcode(opcode, base_ops)) != 0) {
            d |= (t_uint64) op->opbase << 12;
        } else {
            return STOP_UUO;
        }

        cptr = get_glyph(cptr, opcode, ',');
        tag = parse_addr(&cpu_dev, opcode, &arg);
        if (*arg != opcode[0])
            d += (t_value)tag;
        /* ignore any following characters, which can be any form of comments */
        *val = d & 0777777; /* safety - exactly one instructions per line! */
        return SCPE_OK;
    } else if (sw & SWMASK('C')) { /* character string... */
        i = 0;
        while (*cptr != '\0' && i < 6) {
            d <<= 6;
            if (sim_ascii_to_six[0177 & *cptr] != (const char)-1)
                d |= sim_ascii_to_six[0177 & *cptr];
            cptr++;
            i++;
        }
        while (i < 6) {
            d <<= 6;
            d |= 060;
            i++;
        }
        d &= 0777777777777; /* in case of too many digits, take right ones! */
    } else { /* octal... */
        if (*cptr == '-') {
            sign = 1;
            cptr++;
        } else {
            sign = 0;
            if (*cptr == '+')
                cptr++;
        }
        while (*cptr >= '0' && *cptr <= '7') {
            d <<= 3;
            d |= *cptr++ - '0';
        }
        if (addr & 010000) { /* full word... */
            d &= 0777777777777; /* if too many digits, take right ones! */
            if (sign)
                d |= 00400000000000L;
        } else { /* half word... */
            d &= 0777777; /* if too many digits, take right ones! */
            if (sign)
                d |= 00400000L;
        }
    }
    *val = d;
    return SCPE_OK;
}
