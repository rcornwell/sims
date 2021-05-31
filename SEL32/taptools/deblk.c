/*
 * deblk.c
 *
 * This program reads MPX blocked and/or MPX compressed files and
 * deblocks blocked files and uncompresses compressed files and
 * deletes trailing blanks from a source file.  The program will
 * also process standard ASCII newline ('\n') terminated files.
 * input - [filename] or stdin
 * output - stdout
 */

/*
 * MPX blocked file format
 * bytes 0-1 - not used and are zero
 * bytes 2-3 - next record position in buffer
 * byte 4    - start/end of block flag
 *           - 0x20 - EOB end of block
 *           - 0x40 - BOB start of block
 * byte 5    - last record byte count
 * byte 6    - this blocks flags
 *           - 0x00 - valid data
 *           - 0x20 - EOB end of block
 *           - 0x80 - EOF end of file
 *           - 0xa0 - EOB/EOF end of file
 * byte 7    - bytes in this record
 * data byte - cnt data bytes
 * byte cnt+4- 0x20 EOB status
 * byte cnt+5- last record count
 */

/*
 * Compressed record
 * byte 0 - 0xbf -> start of record
 *          0x9f -> start of next record
 * byte 1 - record count
 * byte 2 - high order byte of 16 bit checksum
 * byte 3 - low order byte of 16 bit checksum
 *
 */

/*
*
* An O.S. compressed record consists of 120 bytes:
*
*        6 Control bytes and 114 bytes of compressed source
*
*        (The last record can be less than 120 bytes, on that
*         record the compressed source will be from 4-114 bytes).
*
*        6 Control bytes
*
*        1 Byte- Data type code bf or 9f (9f means this is last record)
*        1 Byte- Size of compressed record (- 6 for control bytes)
*                (Usually 114 (72(x)) is the size except last record)
*        2 Byte- Checksum
*        2 Byte- Record sequence number (starting from zero)
*
*    4-114 Bytes of one or more groups of compressed source as follows:
*
* A compressed group consists of:
* A blank count byte, a data count byte, data.
*
* Compressed groups are repeated and terminated by an ff char.
* Compressed groups are usually terminated at 114 chars by
* the ff char unless this is the last record in the file.
*                                                                 
* A line of text usually is compressed as follows:
* A blank count byte, a data count byte, compressed data
* (one or more of these compressed groups for up to 72 chars of source)
* followed by a blank count byte,a data count byte (of 8),
* data (8 char sequence number), terminated by a ff char.
*
* A workfile logical compressed line is similiar to the O.S.
* logical compressed line except that an 8 char sequence number
* always exists in the workfile format and it is always first
* rather than at the end of the record (if sequence numbers did
* not exist in columns 73-80 in the O.S. original compressed
* records than the editor generates them). Preceding the workfile
* compressed record is a 2 byte previous record in the page pointer.
* Also note that workfiles are not blocked by the O.S., but have
* their own structure of headers, data space, and free space.
*
* If the sequence number does not exist or the period is not in
* the proper place (nnnn.nnn) or the sequence number contains
* anything other than numbers, then the editor will generate
* its own sequence number.
*
* The first blank count can range from 0-80 blank chars
* subsequent blank counts can range from 3-79 max.  That is
* since it takes 2 bytes to do blank compression (a blank count
* and a data count), only 3 or more blank chars after the first
* non-blank char in a line are compressed.
* Records to be compressed are assumed to be 80 chars or less
* (including an 8 char sequence number).
*
* The checksum is simply the addition of all the 120 chars in the
* compressed record except for the 6 control bytes.
*
* The smallest compressed line consists of 14 chars:
* A blank count byte (of 71), a data count byte (of 8),
* data (an 8 char sequence number, a blank count byte (of zero),
* a data count byte (of 1), data (one char), and an ff terminator
* compressed record format can be processed only by the following:    
*                                                                    
* Assembler, P4, Source Update, Editor and some functions of Media
* and of course some utility programs like flip.
*
* Note that a text line can be spread across several compressed
* records.
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define BLKSIZE 768             /* MPX block file sector size */
extern int rbl();
extern int getloi();            /* right from my mind */
extern int putloi();            /* write line */

/* read file and output to stdout */
int main(argc, argv)
int argc;
char *argv[];
{
    FILE    *fp, *fopen();
    unsigned char s[BUFSIZ];

    if(argc == 1) {             /* no args; copy std in */
        while (1) {
            if (rbl(stdin, s, BUFSIZ) <= 0) /* read til EOF */
                exit(0);
            putloi(s);
        }
    } else {
        while (--argc > 0)
            if ((fp = fopen(*++argv, "r")) == NULL) {
            fprintf(stderr, "list: can't open %s\n", *argv);
                exit(1);
            } else {
                while (1) {
                    if (rbl(fp, s, BUFSIZ) <= 0)    /* read til EOF */
                        exit(0);
                    putloi(s);
                }
                fclose(fp);
            }
    }
    exit(0);
}

/*
 * This function computes and checks the checksum of a compressed file
 */
int checksum(buf)
unsigned char *buf;
{
    int i = 0;
    short int ccs = 0;          /*zero checksum */
    /* get checksum */
    unsigned int rcs = (((buf[2] << 8) & 0xff00) | (buf[3] & 0xff));
    int cnt = buf[1] & 0xff;    /* record count */
//fprintf(stderr, "checksum cnt %x data %x %x %x %x %x %x %x %x\n",
//      cnt, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);

    while (cnt > 0) {
        unsigned short v = buf[i+6] & 0xff;
        ccs += v;               /* add the byte */ 
//fprintf(stderr, "checksum cnt %x val %x sum %x\n", i, v, ccs);
        i++;                    /* bump address */
        cnt--;                  /* reduce count */
    }
//fprintf(stderr, "checksum size %x read %x calc %x\n", buf[1], rcs, ccs);
    if (ccs == rcs)
        return 0;               /* return OK */
    return 1;                   /* return error */
}

int bin = 0;
unsigned char si[BLKSIZE];
unsigned char bi[BLKSIZE];
int ubdp = 0;                   /* unblocked data pointer */
int ubdc = 0;                   /* unblocked data count */
int bdp = 0;                    /* blocked data pointer */
int bdc = 0;                    /* blocked data count */
short filetype = 0;

#define unknown 0x00
#define blocked 0x01
#define compress 0x02
#define ascii 0x04

/*
 * This function reads MPX blocked files
 */
int readbb(fp, ip, cnt)
FILE *fp;
char *ip;
int cnt;
{
    int c;
    int i = 0;

    if (bin == 0) {
//fprintf(stderr, "read sector a\n");
        if (fread(si, 1, BLKSIZE, fp) <= 0)
            return (0);         /* this means eof */
        bin = 6;
    }
    /* check for EOF */
    if (si[bin] & 0x80) {
        bin = 0;
        return(0);              /* we have EOF */
    }
    /* check for EOB in last record */
    if (si[bin - 2] & 0x20) {
//fprintf(stderr, "read sector b\n");
        if (fread(si, 1, BLKSIZE, fp) <= 0)
            return (0);         /* this means eof */
        bin = 6;
    }
//fprintf(stderr, "copy block from sector @ bin %x\n", bin);
    /* copy the block into users buffer */
    if ((c = si[bin+1]) > 0) {
        for (i = 0; i < c; i++) {
            ip[i] = si[bin + 2 + i];
            if (i >= cnt)
                break;
        }
        bin += (c + 4);
        ip[i] = '\0';
        return (i);
    }
    bin = 0;
    return (i);
}

/* function to read a byte from an unblocked file */
int getb(fp)
FILE *fp;
{
    int c;
    static int goteof = 0;

    /* file is unblocked, get next record */
    if (goteof) {
        goteof = 0;
//fprintf(stderr, "getb - returning EOF ubdc=%x\n", ubdc);
        return (-1);            /* this means eof */
    }
    if (ubdp < ubdc)            /* is count exhausted */
        c = si[ubdp++] & 0xff;  /* copy char */
    if (ubdp >= ubdc) {         /* is count exhausted */
        /* need to read next block, if not first time */
        /* we need a new buffer */
        /* read in 768 byte block of the file */
        if ((ubdc = fread(si, 1, BLKSIZE, fp)) <= 0)
            goteof = 1;         /* this means eof */
//fprintf(stderr, "getb - read unblocked file goteof %x ubdc=%x\n", goteof, ubdc);
        ubdp = 0;
    }
    return (c);
}

/* get a line of input */
int getloi(fp, s, lim)          /* right from my mind */
FILE *fp;
unsigned char s[];
int lim;
{
    int c, i, cc, bc = 0, rc = 0;

    /* see how we are to process data */
    if (filetype & blocked) {
        /* file is blocked, get next record */
        if (bdp == 0) {
            /* we need a new buffer */
/*newbuf:*/
            if ((bdc = readbb(fp, bi, lim)) <= 0)
                return (0);     /* this means eof */
//fprintf(stderr, "getloi read blocked file %x\n", bdc);
            bdp = 0;
        }
        /* check for compressed data */
        if (filetype & compress) {
            if ((bi[bdp] & 0x9f) != 0x9f) {
                fprintf(stderr, "blocked compressed file read error %x\n", bi[bdp]);
                return (0);     /* this means error */
            }
            /* checksum the record */
            if (checksum(&bi[bdp])) {
                fprintf(stderr, "blocked compressed file checksum error\n");
                return (0);     /* this means error */
            }
//fprintf(stderr, "getloi blocked compressed file checksum OK @ %x cnt %x\n", bdp, bdc);
            /* copy in the next record */
            /* get chars until EOF or limit reached */
            cc = bi[bdp+1]+6;   /* get count */
            for (i = 0; (--lim > 0) && (i < cc); i++) {
                s[rc++] = bi[bdp++];    /* copy char */
            }
            if ((bdp >= bdc) || (i == cc)) {
                bdp = 0;        /* read new buffer next time */
//fprintf(stderr, "getloi blocked compressed read return %x bdc %x bdp %x\n", rc, bdc, bdp);
            }
            return (rc);        /* return data count */
        }
        /* file is uncompressed, so copy MPX records */
//fprintf(stderr, "getloi blocked data rc=%x bdc=%x\n", rc, bdc);
        for (i=0; i<bdc; i++)
            s[rc++] = bi[bdp++];    /* copy chars */
        s[rc++] = 0;            /* null terminate */
        bdp = 0;                /* read next buffer */
        return (i);             /* return data */
    }
    else {
        /* check for compressed data */
        if (filetype & compress) {
            cc = 120;
            rc = 0;
            bc = 0;
            while ((c = getb(fp)) != -1) {
                /* make sure this is a compressed record */
                if ((rc == 0) && ((c & 0x9f) != 0x9f)) {
                    fprintf(stderr,
            "getloi - unblocked compressed file read error %x rc %x\n", c, rc);
                    return (0); /* this means error */
                }
                if (rc == 1)
                    cc = c + 6; /* get 'real' record count */
                s[rc++] = c;    /* save the char */

                if (rc == cc)   /* compressed record is always <= 120 char buffers */
                    break;      /* done */
            }
            if (c == -1)
                return (0);     /* this means EOF */

            /* skip any extra chars from short records */
            bc = rc;
            while ((s[0] != 0x9f) && (bc < 120)) {
                if ((c = getb(fp)) == -1)
                    return (0); /* this means EOF */
                s[bc++] = c;    /* fill extra chars */
//fprintf(stderr, "getloi - filling extra chars with char %x bc %x\n", c, bc);
            }
            /* next char should be bf/9f */
            if ((si[ubdp] & 0x9f) != 0x9f) {    /* copy char */
                if ((c = getb(fp)) == -1) {
//fprintf(stderr, "getloi skipping exit char %0x at end of line, next %x s[0] %0x\n", c, si[ubdp], s[0]);
                    if (s[0] != 0x9f)
                        return (0); /* this means EOF */
                }
//fprintf(stderr, "getloi skipping char %0x at end of line, next %x s[0] %0x\n", c, si[ubdp], s[0]);
            }

//fprintf(stderr, "getloi unblked comp read done rc %x cc %x s[0] %x s[rc-1] %x next %x\n",
//rc, cc, s[0], s[rc-1], si[ubdp]);

            /* checksum the record */
            if (checksum(s)) {
                fprintf(stderr, "getloi - unblocked compressed file checksum error\n");
//fprintf(stderr, "getloi A unblocked compressed read return rc=%x cc=%x %x %x\n", rc, cc, s[0], s[rc-1]);
//fprintf(stderr, "getloi C unblocked compressed read return %x ubdc %x ubdp %x\n", rc, ubdc, ubdp);
                return (0);     /* this means error */
            }
//fprintf(stderr, "getloi B unblocked compressed read return rc=%x cc=%x %x\n", rc, cc, s[1]);
            return (rc);        /* return data count */
        }
        /* file is uncompressed, so copy UNIX records */
        while ((c = getb(fp)) != -1) {
            s[rc++] = c;        /* save the char */
            if (c == 0x0a) {
//fprintf(stderr, "getloi C unblocked compressed read return %x ubdc %x ubdp %x\n", rc, ubdc, ubdp);
                s[rc++] = 0;    /* terminate the line */
                return (rc);    /* return data */
            }
        }
        return (0);             /* EOF */
    }
    return (0);
}

/*
 **  output line of text from the source
 */
int putloi(s)
unsigned char *s;
{
    printf("%s", s);
    return(0);
}

unsigned char line[BUFSIZ];
unsigned char line2[BUFSIZ];
int cmpop = 0;
int cmpflg = 0;
int bcnt = 0;
unsigned char *bptr = 0;
int binary = 1;
int recl = 0;
int ubin = 0;

/* read lines of data from the source file */
/* files can be blocked w/wo compression */
/* files can be blocked ascii */
/* files can be byte strings, newline terminated */
int rbl(fp, buf, n)
FILE *fp;
unsigned char *buf;
int n;
{
    register int count = 0;
/*  register unsigned char *cp; */
    int i;
    unsigned char *linadrs = line;

    if (filetype == unknown) {  /* see if we know type of file to read */
        bin = 0;
        ubin = 0;
        bdp = 0;
        bdc = 0;
        ubdp = 0;
        ubdc = 0;
        /* read in 1st 768 Byte block of the file */
        if ((ubdc = fread(si, 1, BLKSIZE, fp)) <= 0)
            return (0);         /* this means eof */
        /* test 1st byte for 0x06 and bytes 2, 3, and 4 zero */
        if ((si[0] == 0x06) && (si[1] == 0) && (si[2] == 0) && (si[3] == 0)) {
            /* we have a library file, giver error and abort */
            fprintf(stderr, "deblk - Cannot list library file, aborting\n");
            return (0);         /* this means error exit */
        }
        /* test for a directory file, 8 ascii char then 4 zeros */
        if ((si[8] == 0) && (si[9] == 0) && (si[10] == 0) && (si[11] == 0)) {
            for (i=0; i<8; i++) {
                if (!isprint(si[i])) {
                    /* unknown file type, abort */
                    fprintf(stderr, "deblk - Unknown binary file type, aborting\n");
                    return (0); /* this means error exit */
                }
            }
            /* must be directory, abort */
            fprintf(stderr, "deblk - Cannot list directory file, aborting\n");
            return (0);         /* this means error exit */
        }
        i = ((si[2] << 8) & 0xff00) | (si[3] & 0xff);   /* get file offset pointer, bytes 2 & 3 */
        /* test 1st 2 byte of file for zero */
        if ((si[0] == 0) && (si[1] == 0) && (i < BLKSIZE)) {
            /* most likely blocked file if 1st 2 bytes 0 and next 2 bytes are less than 768 */
            filetype |= blocked;    /* we have blocked file */
            bin = 6;            /* where we start for data block */

            /* see if we have compressed data */
            if ((si[bin + 2] == 0xbf) || (si[bin + 2] == 0x9f)) {
                filetype |= compress;   /* data is compressed */
                bcnt = 0;       /* no data in buffer */
            }
            else {
                /* data is not compressed, just ascii without newlines */
                filetype |= ascii;  /* blocked ascii data */
            }
        }
        else
        /* test for a diag blocked ascii file */
        /* test 1st 2 byte of file for zero */
        if ((si[0] == 0) && (si[1] == 3) && (i == 0xf3b8)) {
            filetype |= blocked;    /* we have blocked file */
            bin = 6;            /* where we start for data block */

            /* see if we have compressed data */
            if ((si[bin + 2] == 0xbf) || (si[bin + 2] == 0x9f)) {
                filetype |= compress;   /* data is compressed */
                bcnt = 0;       /* no data in buffer */
            }
            else {
                /* data is not compressed, just ascii without newlines */
                filetype |= ascii;  /* blocked ascii data */
            }
        }
        else {
            /* data is unblocked, see if compressed or not */
            if ((si[ubin] == 0xbf) || (si[ubin] == 0x9f)) {
                filetype |= compress;   /* data is compressed */
                bcnt = 0;       /* no data in buffer */
            }
            else
            if ((si[ubin] == 0xef) || (si[ubin] == 0xcf))
            {
                /* file is an macro library, so abort */
                fprintf(stderr, "deblk - Cannot list macro library file, aborting\n");
                return (0);     /* this means error exit */
            }
            else {
                /* data is not compressed or blocked, just ascii with newlines */
                filetype |= ascii;  /* blocked ascii data */
            }
        }
    }
    if ((filetype & compress) && !cmpop) {  /* see if we tested for compressed */
        cmpop = 1;              /* set compresse tested flag */
        /* read in the first record */
        if ((recl = getloi(fp, line, BUFSIZ)) == 0)
            return (0);         /* this means eof */
        linadrs = line;
        if (*linadrs == 0xbf) { /* is this file compressed */
            cmpflg = 1;         /* set comp data flag */
            bcnt = linadrs[1];  /* set record count */
            bptr = &linadrs[6]; /* set data address */
//fprintf(stderr, "rbl - read 1st compressed record cnt %x %x\n", bcnt, linadrs[0]);
        }
        else
            goto re00;
    }

    if (cmpflg) {               /* reading compressed data? */
        if (bcnt == 0) {        /* any data left in buffer */
          re18:
            /* read in a data record */
            if ((recl = getloi(fp, line, BUFSIZ)) == 0)
                return (0);     /* this means eof */
            linadrs = line;
//fprintf(stderr, "rbl re18 - read compressed record recl %x cnt %x 1st %x\n", recl, bcnt, linadrs[0]);
            if ((*linadrs & 0x9f) != 0x9f)  /* is this valid rec */
                return (EOF);   /* error if not */
            bcnt = linadrs[1];  /* set record count */
            bptr = &linadrs[6]; /* set data address */
//fprintf(stderr, "rbl - read nth compressed record cnt %x %x\n", bcnt, linadrs[0]);
        }
      re20:
        /* see if any blanks */
        if (i = *bptr++) {      /* next buffer pointer */
            if (i == 0xff)
                goto re60;      /* if eol, get out */
            /* insert the required number of blanks */
            while (i--) {
                if (count < n) {
                    *buf++ = ' ';   /* put blank in buffer */
                    count++;
                }
            }
        }
        if (--bcnt <= 0)
            goto re18;          /* read next record */

        /* get character count */
        if (i = *bptr++) {      /* next buffer pointer */
/*try*/     if (i == 0xff)
/*0216*/        goto re60;      /* if eol, get out */
            while (i--) {
                if (count < n)
                    *buf++ = *bptr; /* put char in buffer */
                bcnt--;         /* decr count */
                bptr++;         /* next buffer pointer */
                count++;
            }
        }
        if (--bcnt <= 0)
            goto re18;          /* read next record */
        goto re20;

      re60:
        bcnt--;                 /* decr count */
        if ((*--buf == ' ') && (count == 1)) {
            *buf = '\n';        /* put new line at eol */
        }
        else {
            *++buf = '\n';      /* put new line at eol */
            count++;
        }
    }
    else {
        /* non compressed read here */
        /* read the next record */
        if ((recl = getloi(fp, line, BUFSIZ)) == 0)
            return (0);         /* this means eof */
        linadrs = line;         /* reset line pointer */

      re00:
//fprintf(stderr, "rbl - read nth uncompressed record cnt %x %s\n", recl, line);
#if 0
        /* here we need to strip off blank put in during write */
        /* this is because mpx does not support zero length blocks */
        if ((recl == 1) && (*linadrs == ' ')) {
            recl = 0;
            /* now append new line to end of buffer */
            if (!binary)
                line[recl] = '\n';
        }
#endif
        count = 0;
        /* copy this layer buffer to upper caller's buffer */
        while ((count < n) && (count < recl)) {
            buf[count] = line[count];
            count++;
        }
        /* if no newline, add one and null terminate */
        if (line[count-1] != '\n')
            buf[count++] = '\n';
        buf[count] = '\0';
    }
//fprintf(stderr, "rbl - read return cnt %x\n", count);
    return (count);
}
