/* sel32_lpr.c: SEL 32 Line Printer

   Copyright (c) 2018-2020, James C. Bevier
   Portions provided by Richard Cornwell and other SIMH contributers

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

   This is the standard line printer.

   These units each buffer one record in local memory and signal
   ready when the buffer is full or empty. The channel must be
   ready to recieve/transmit data when they are activated since
   they will transfer their block during chan_cmd. All data is
   transmitted as BCD characters.
*/

#include "sel32_defs.h"
#include <ctype.h>

/****  COMMANDS TO PRINT BUFFER THEN DO FORMS CONTROL */
/*
LP.CMD1  DATAW     X'01000000'     PRINT ONLY - NO FORMS CONTROL
LP.CMD2  DATAW     X'05000000'     PRINT BUFFER, <CR>
LP.CMD3  DATAW     X'15000000'     PRINT BUFFER, <LF>
LP.CMD4  DATAW     X'25000000'     PRINT BUFFER, <LF> <LF>
LP.CMD5  DATAW     X'35000000'     PRINT BUFFER, <LF> <LF> <LF>
LP.CMD6  DATAW     X'45000000'     PRINT BUFFER, <FF>
LP.CMD7  DATAW     X'85000000'     PRINT BUFFER, <CR>, THEN CLEAR BUFFER
*
****  COMMANDS TO DO FORMS CONTROL AND THEN PRINT BUFFER.
****  NOTE: THESE COMMANDS ARE ARRANGED SO THAT BY USING THE INDEX
****        OF THE FORMS CONTROL TABLE AND A OFFSET INTO THIS TABLE
****        YOU CAN GET THE APPROPRIATE COMMAND FOR THE FC CHAR.
*
LP.CMD8  DATAW     X'0D000000'     <CR>, PRINT BUFFER, <CR>
LP.CMD9  DATAW     X'4D000000'     <FF>, PRINT BUFFER, <CR>
         DATAW     X'4D000000'     <FF>, PRINT BUFFER, <CR>
LP.CMD10 DATAW     X'2D000000'     <LF> <LF>, PRINT BUFFER <CR>
LP.CMD11 DATAW     X'1D000000'     <LF>, PRINT BUFFER, <CR>
LP.CMD12 DATAW     X'3D000000'     <LF> <LF> <LF>, PRINT, <CR>  (SPARE)
*
****  COMMANDS THAT DO ONLY FORMS CONTROL (NO PRINTING)
*
LP.CMD13 DATAW     X'03000000'     <CR>
LP.CMD14 DATAW     X'47000000'     <FF>
         DATAW     X'47000000'     <FF>
LP.CMD15 DATAW     X'27000000'     <LF> <LF>
LP.CMD16 DATAW     X'17000000'     <LF>
LP.CMD17 DATAW     X'37000000'     <LF> <LF> <LF> (SPARE)
*
** LINE PRINTER FORMS CONTROL TABLE
*
LPFCTBL  EQU       $
P0006C  2B                            DATAB     C'+'    0x2b    FORMS CONTROL FOR CR THEN PRINT
P0006D    31                          DATAB     C'1'    0x31    FORMS CONTROL FOR FF THEN PRINT
P0006E      2D                        DATAB     C'-'    0x2d    FORMS CONTROL FOR FF THEN PRINT
P0006F        30                      DATAB     C'0'    0x30    FORMS CONTROL FOR 2 LF'S THEN PRINT
P00070  20                            DATAB     C' '    0x20    FORMS CONTROL FOR LF THEN PRINT
*/
  
#ifdef NUM_DEVS_LPR
//#define UNIT_LPR        UNIT_ATTABLE | UNIT_DISABLE
#define UNIT_LPR        UNIT_ATTABLE | UNIT_IDLE

extern  t_stat  set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern  t_stat  show_dev_addr(FILE *st, UNIT * uptr, int32 v, CONST void *desc);
extern  void    chan_end(uint16 chan, uint8 flags);
extern  int     chan_read_byte(uint16 chan, uint8 *data);
extern  int     chan_write_byte(uint16 chan, uint8 *data);
extern  void    set_devattn(uint16 addr, uint8 flags);

/* u3 hold command and status information */
#define LPR_INCH        0x00        /* INCH command */
/* print buffer then CC commands */
#define LPR_PBNCC       0x01        /* print only, no forms control */
#define LPR_PBC         0x05        /* print buffer, then <CR> */
#define LPR_PBL         0x15        /* print buffer, then <LF> */
#define LPR_PBLL        0x25        /* print buffer, then <LF> <LF> */
#define LPR_PBLLL       0x35        /* print buffer, then <LF> <LF> <LF> */
#define LPR_PBF         0x45        /* print buffer, then <FF> */
#define LPR_PBCCB       0x85        /* print buffer, then <CR> <CLEAR BUFFER> */
    /* Do CC then print commands then CC */
#define LPR_CPBC        0x0d        /* <CR> print buffer <CR> */
#define LPR_LPBC        0x1d        /* <LF> print buffer <CR> */
#define LPR_LLPBC       0x2d        /* <LF> <LF> print buffer <CR> */
#define LPR_LLLPBC      0x3d        /* <LF> <LF> <LF> print buffer <CR> */
#define LPR_FPBC        0x4d        /* <FF> print buffer <CR> */
    /* Do CC only, no print */
#define LPR_NPC         0x03        /* <CR> */
#define LPR_NPL         0x17        /* <LF> */
#define LPR_NPLL        0x27        /* <LF> <LF> */
#define LPR_NPLLL       0x37        /* <LF> <LF> <LF> */
#define LPR_NPF         0x47        /* <FF> */

#define LPR_SNS         0x04        /* Sense command */
#define LPR_CMDMSK      0xff        /* Mask command part. */
#define LPR_FULL        0x100       /* Buffer full (BOF) */
#define LPR_PRE         0x200       /* Apply pre CC */
#define LPR_POST        0x400       /* Apply post CC */

/* u4 holds current line */
/* in u5 packs sense byte 0,1 and 3 */
/* Sense byte 0 */
#define SNS_CMDREJ      0x80        /* Command reject */
#define SNS_INTVENT     0x40        /* Unit intervention required */
#define SNS_BUSCHK      0x20        /* Parity error on bus */
#define SNS_EQUCHK      0x10        /* Equipment check */
#define SNS_DATCHK      0x08        /* Data Check */
#define SNS_OVRRUN      0x04        /* Data overrun */
#define SNS_SEQUENCE    0x02        /* Unusual sequence */
#define SNS_BOF         0x01        /* BOF on printer */
/* u6 hold buffer position */

/* std devices. data structures
    lpr_dev     Line Printer device descriptor
    lpr_unit    Line Printer unit descriptor
    lpr_reg     Line Printer register list
    lpr_mod     Line Printer modifiers list
*/

struct _lpr_data
{
    uint8           lbuff[160];     /* Output line buffer */
}

lpr_data[NUM_DEVS_LPR];

uint8           lpr_startcmd(UNIT *, uint16, uint8);
void            lpr_ini(UNIT *, t_bool);
t_stat          lpr_srv(UNIT *);
t_stat          lpr_reset(DEVICE *);
t_stat          lpr_attach(UNIT *, CONST char *);
t_stat          lpr_detach(UNIT *);
t_stat          lpr_setlpp(UNIT *, int32, CONST char *, void *);
t_stat          lpr_getlpp(FILE *, UNIT *, int32, CONST void *);

/* channel program information */
CHANP           lpr_chp[NUM_DEVS_LPR] = {0};

MTAB            lpr_mod[] = {
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "LINESPERPAGE", "LINESPERPAGE",
        &lpr_setlpp, &lpr_getlpp, NULL, "Number of lines per page"},
    {MTAB_XTD|MTAB_VUN|MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {0}
};

UNIT            lpr_unit[] = {
    {UDATA(&lpr_srv, UNIT_LPR, 66), 300, UNIT_ADDR(0x7EF8)},     /* A */
#if NUM_DEVS_LPR > 1
    {UDATA(&lpr_srv, UNIT_LPR, 66), 300, UNIT_ADDR(0x7EF9)},     /* B */
#endif
};

/* Device Information Block */
//DIB lpr_dib = {NULL, lpr_startcmd, NULL, NULL, lpr_ini, lpr_unit, lpr_chp, NUM_DEVS_LPR, 0xff, 0x7e00, 0, 0, 0};
DIB             lpr_dib = {
    NULL,           /* uint8 (*pre_io)(UNIT *uptr, uint16 chan)*/       /* Start I/O */
    lpr_startcmd,   /* uint8 (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd)*/ /* Start a command */
    NULL,           /* uint8 (*halt_io)(UNIT *uptr) */          /* Stop I/O */
    NULL,           /* uint8 (*test_io)(UNIT *uptr) */          /* Test I/O */
    NULL,           /* uint8 (*post_io)(UNIT *uptr) */          /* Post I/O */
    lpr_ini,        /* void  (*dev_ini)(UNIT *, t_bool) */      /* init function */
    lpr_unit,       /* UNIT* units */                           /* Pointer to units structure */
    lpr_chp,        /* CHANP* chan_prg */                       /* Pointer to chan_prg structure */
    NUM_DEVS_LPR,   /* uint8 numunits */                        /* number of units defined */
    0xff,           /* uint8 mask */                            /* 2 devices - device mask */
    0x7e00,         /* uint16 chan_addr */                      /* parent channel address */
    0,              /* uint32 chan_fifo_in */                   /* fifo input index */
    0,              /* uint32 chan_fifo_out */                  /* fifo output index */
    {0}             /* uint32 chan_fifo[FIFO_SIZE] */           /* interrupt status fifo for channel */
};

DEVICE          lpr_dev = {
    "LPR", lpr_unit, NULL, lpr_mod,
    NUM_DEVS_LPR, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, &lpr_attach, &lpr_detach,
    &lpr_dib, DEV_UADDR | DEV_DISABLE | DEV_DEBUG, 0, dev_debug
};

/* initialize the line printer */
void lpr_ini(UNIT *uptr, t_bool f) {
    /* do nothing for now */
}

/* start an I/O operation */
uint8 lpr_startcmd(UNIT *uptr, uint16 chan, uint8 cmd)
{
    if ((uptr->u3 & LPR_CMDMSK) != 0) {         /* unit busy */
        return SNS_BSY;                         /* yes, busy (already tested) */
    }

    uptr->u3 &= ~(LPR_POST|LPR_PRE);            /* set no CC */
    if (((cmd & 0x03) == 0x03) || (cmd & 0x0f) == 0x0d) {
        uptr->u3 |= LPR_PRE;                    /* apply pre CC */
    }
    if (((cmd & 0x0f) == 0x05) || (cmd & 0x0f) == 0x0d) {
        uptr->u3 |= LPR_POST;                   /* apply post CC */
    }
    sim_debug(DEBUG_CMD, &lpr_dev, "lpr_startcmd Cmd %02x\n", cmd);

    /* process the command */
    switch (cmd & LPR_CMDMSK) {
    case 0x00:                                  /* INCH command */
        /* the IOP should already have the inch buffer set, so ignore */
        sim_debug(DEBUG_CMD, &lpr_dev, "lpr_startcmd %04x: Cmd INCH\n", chan);
//fprintf(stderr, "lpr_startcmd %04x: Cmd INCH\n", chan);
        return SNS_CHNEND|SNS_DEVEND;           /* all is well */
        break;

    /* No CC */
    case 0x01:                          /* print only, no forms control */
    /* print buffer then CC commands */
    case 0x05:                          /* print buffer, then <CR> */
    case 0x15:                          /* print buffer, then <LF> */
    case 0x25:                          /* print buffer, then <LF> <LF> */
    case 0x35:                          /* print buffer, then <LF> <LF> <LF> */
    case 0x45:                          /* print buffer, then <FF> */
    case 0x85:                          /* print buffer, then <CR> <CLEAR BUFFER> */
    /* Do CC then print commands then CC */
    case 0x0d:                          /* <CR> print buffer <CR> */
    case 0x1d:                          /* <LF> print buffer <CR> */
    case 0x2d:                          /* <LF> <LF> print buffer <CR> */
    case 0x3d:                          /* <LF> <LF> <LF> print buffer <CR> */
    case 0x4d:                          /* <FF> print buffer <CR> */
    /* Do CC only, no print */
    case 0x03:                          /* <CR> */
    case 0x17:                          /* <LF> */
    case 0x27:                          /* <LF> <LF> */
    case 0x37:                          /* <LF> <LF> <LF> */
    case 0x47:                          /* <FF> */
        /* process the command */
        sim_debug(DEBUG_CMD, &lpr_dev, "lpr_startcmd %04x: Cmd %02x print\n", chan, cmd&LPR_CMDMSK);
        uptr->u3 &= ~(LPR_CMDMSK);      /* zero cmd */
        uptr->u3 |= (cmd & LPR_CMDMSK); /* save new command in u3 */
        sim_activate(uptr, 100);        /* Start unit off */
        uptr->u5 = 0;                   /* no status */
        uptr->u6 = 0;                   /* start of buffer */
        return 0;                       /* we are good to go */

    case 0x4:                           /* Sense Status */
        sim_debug(DEBUG_CMD, &lpr_dev, "lpr_startcmd %04x: Cmd %02x sense\n", chan, cmd&LPR_CMDMSK);
        uptr->u3 &= ~(LPR_CMDMSK);      /* zero cmd */
        uptr->u3 |= (cmd & LPR_CMDMSK); /* save new command in u3 */
        sim_activate(uptr, 100);        /* Start unit off */
        uptr->u5 = 0;                   /* no status */
        uptr->u6 = 0;                   /* start of buffer */
        return 0;                       /* we are good to go */

    default:                            /* invalid command */
        sim_debug(DEBUG_CMD, &lpr_dev, "lpr_startcmd %04x: Cmd %02x INVALID\n", chan, cmd&LPR_CMDMSK);
        uptr->u5 |= SNS_CMDREJ;
        break;
    }
    if (uptr->u5 & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/* Handle transfer of data for printer */
t_stat lpr_srv(UNIT *uptr) {
    int         chsa = GET_UADDR(uptr->u3);
    int         u = (uptr - lpr_unit);
    int         cmd = (uptr->u3 & 0xff);

    sim_debug(DEBUG_CMD, &lpr_dev, "lpr_srv called chsa %04x cmd %02x u3 %08x cnt %04x\r\n",
            chsa, cmd, uptr->u3, uptr->u6);

    /* FIXME, need IOP lp status bit assignments */
    if (cmd == 0x04) {                          /* sense? */
        uint8 ch = uptr->u5;                    /* get current status */
        uptr->u3 &= ~(LPR_CMDMSK);              /* clear command */
        chan_write_byte(chsa, &ch);             /* write the status to memory */
        uptr->u6 = 0;                           /* reset to beginning of buffer */
        chan_end(chsa, SNS_DEVEND|SNS_CHNEND);  /* we are done */
        return SCPE_OK;
    }

    /* process any CC before printing buffer */
    if ((uptr->u3 & LPR_PRE) && (((cmd & 0x03) == 0x03) || (cmd & 0x0f) == 0x0d)) {
        uptr->u3 &= ~LPR_PRE;                   /* remove pre flag */
        /* we have CC to do */
        switch ((cmd & 0xf0) >> 4) {
        case 0:                                 /* <CR> (0x0d) */
            lpr_data[u].lbuff[uptr->u6++] = 0x0d;
            break;
        case 3:                                 /* <LF> <LF> <LF> */
            lpr_data[u].lbuff[uptr->u6++] = 0x0a;
            uptr->u4++;                         /* increment the line count */
            /* drop thru */
        case 2:                                 /* <LF> <LF> */
            lpr_data[u].lbuff[uptr->u6++] = 0x0a;
            uptr->u4++;                         /* increment the line count */
            /* drop thru */
        case 1:                                 /* <LF> (0x0a) */
            lpr_data[u].lbuff[uptr->u6++] = 0x0a;
            uptr->u4++;                         /* increment the line count */
            break;
        case 4:                                 /* <FF> (0x0c) */
            lpr_data[u].lbuff[uptr->u6++] = 0x0d;   /* add C/R */
            lpr_data[u].lbuff[uptr->u6++] = 0x0a;   /* add L/F */
            lpr_data[u].lbuff[uptr->u6++] = 0x0c;   /* add FF */
            uptr->u4 = 0;                       /* restart line count */
            break;
        }
    }

    /* Copy next byte from users buffer */
    while ((uptr->u3 & LPR_FULL) == 0) {        /* copy in a char if not full */
        if(chan_read_byte(chsa, &lpr_data[u].lbuff[uptr->u6])) {
            uptr->u3 |= LPR_FULL;               /* end of buffer or error */
            break;                              /* done reading */
        } else {
            /* remove nulls */
            if (lpr_data[u].lbuff[uptr->u6] == '\0') {
                lpr_data[u].lbuff[uptr->u6] = ' ';
            }
            /* remove backspace */
            if (lpr_data[u].lbuff[uptr->u6] == 0x8) {
                lpr_data[u].lbuff[uptr->u6] = ' ';
            }
            uptr->u6++;                         /* next buffer loc */
        }
    }

    /* remove trailing blanks before we apply trailing carriage control */
    while (uptr->u6 > 0) {
        if ((lpr_data[u].lbuff[uptr->u6-1] == ' ') ||
            (lpr_data[u].lbuff[uptr->u6-1] == '\0')) {
            uptr->u6--;
            continue;
        }
        break;
    }

    /* process any CC after printing buffer */
    if ((uptr->u3 & LPR_FULL) && (uptr->u3 & LPR_POST) && ((cmd & 0x0f) == 0x0d)) {
        /* we have CC to do */
        uptr->u3 &= ~LPR_POST;                  /* remove post flag */
        lpr_data[u].lbuff[uptr->u6++] = 0x0d;   /* just a <CR> */
    }

    /* process any CC after printing buffer */
    if ((uptr->u3 & LPR_FULL) && (uptr->u3 & LPR_POST) && ((cmd & 0x0f) == 0x05)) {
        /* we have CC to do */
        uptr->u3 &= ~LPR_POST;                  /* remove post flag */
        switch ((cmd & 0xf0) >> 4) {
        case 0:                                 /* <CR> (0x0d) */
            lpr_data[u].lbuff[uptr->u6++] = 0x0d;
            break;
        case 3:                                 /* <LF> <LF> <LF> */
            lpr_data[u].lbuff[uptr->u6++] = 0x0a;
            uptr->u4++;                         /* increment the line count */
            /* drop thru */
        case 2:                                 /* <LF> <LF> */
            lpr_data[u].lbuff[uptr->u6++] = 0x0a;
            uptr->u4++;                         /* increment the line count */
            /* drop thru */
        case 1:                                 /* <LF> (0x0a) */
            lpr_data[u].lbuff[uptr->u6++] = 0x0a;
            uptr->u4++;                         /* increment the line count */
            break;
        case 4:                                 /* <FF> (0x0c) */
            lpr_data[u].lbuff[uptr->u6++] = 0x0d;   /* add C/R */
            lpr_data[u].lbuff[uptr->u6++] = 0x0a;   /* add L/F */
            lpr_data[u].lbuff[uptr->u6++] = 0x0c;   /* add FF */
            uptr->u4 = 0;                       /* restart line count */
            break;
        }
    }

    /* print the line if buffer is full */
    if (uptr->u3 & LPR_FULL || uptr->u6 >= 156) {
        lpr_data[u].lbuff[uptr->u6] = 0x00;     /* NULL terminate */
        sim_fwrite(&lpr_data[u].lbuff, 1, uptr->u6, uptr->fileref); /* Print our buffer */
        sim_debug(DEBUG_DETAIL, &lpr_dev, "LPR %s", (char*)&lpr_data[u].lbuff);
        uptr->u3 &= ~(LPR_FULL|LPR_CMDMSK);     /* clear old status */
        uptr->u6 = 0;                           /* start at beginning of buffer */
        uptr->u4++;                             /* increment the line count */
        if ((uint32)uptr->u4 > uptr->capac) {   /* see if at max lines/page */
            uptr->u4 = 0;                       /* yes, restart count */
            chan_end(chsa, SNS_DEVEND|SNS_CHNEND|SNS_UNITEXP);  /* we are done */
        } else
            chan_end(chsa, SNS_DEVEND|SNS_CHNEND);  /* we are done */
        /* done, so no time out */
        return SCPE_OK;
    }

    /* should not get here */
    return SCPE_OK;
}

/* Set the number of lines per page on printer */
t_stat lpr_setlpp(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int i;
    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    i = 0;
    while(*cptr != '\0') {
        if (*cptr < '0' || *cptr > '9')
            return SCPE_ARG;
        i = (i * 10) + (*cptr++) - '0';
    }
    if (i < 20 || i > 100)
        return SCPE_ARG;
    uptr->capac = i;
    uptr->u4 = 0;
    return SCPE_OK;
}

/* display the number of lines per page */
t_stat lpr_getlpp(FILE *st, UNIT *uptr, int32 v, CONST void *desc)
{
    if (uptr == NULL)
        return SCPE_IERR;
    fprintf(st, "linesperpage=%02d", uptr->capac);
    return SCPE_OK;
}

/* attach a file to the line printer device */
t_stat lpr_attach(UNIT * uptr, CONST char *file)
{
    t_stat          r;

    if ((r = attach_unit(uptr, file)) != SCPE_OK)
        return r;
    uptr->u3 &= ~(LPR_FULL|LPR_CMDMSK);
    uptr->u4 = 0;
    uptr->u5 = 0;
    return SCPE_OK;
}

/* detach a file from the line printer */
t_stat lpr_detach(UNIT * uptr)
{
    return detach_unit(uptr);
}

#endif
