/*
 * mpxblk.c
 *
 * This program converts a unix/dos ascii file to an mpx blocked file
 * trailing blanks are not deleted from the source file
 * input - stdin   
 * output - stdout
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#define MPXMAX 254
int mpxbb();
int getloi();
#define BLKSIZE 768

int main (argc, argv)
int argc;
char *argv[];
{
    char s[MPXMAX];
    int i;
    int fn = fileno(stdout);

    while (1) {
        if ((i = getloi(s, MPXMAX)) == 0) {
            mpxbb(fn, s, 0, 1); /* last write to output */
            exit(0);
        }
        if (i == 1 && s[0] == '\n')
            s[0] = ' ';     /* replace single n/l with blank */
        if (s[i-1] == '\n')
            i--;            /* remove trailing n/l */
        if (mpxbb(fn, s, i, 0) < 0) {   /* last data record out */
            fprintf(stderr, "output file error\n");
            exit(1);
        }
    }
}
        
/* get a line of input. */
/* terminate line on \n or \r\n */
/* allow max of 254 chars in a line */

int getloi(s, lim)
char s[];
int lim;
{
    int c, i;

    for(i = 0; --lim > 0; ) {
        c = getchar();
        switch (c) {
        case '\n':
            /* terminate on \n */
            s[i++] = c;
            goto linedone;
        case '\r':
            c = getchar();
            /* terminate on \r\n */
            if (c == '\n') {
                s[i++] = c;
                goto linedone;
            }
            /* put back the character */
            ungetc(c, stdin);
            /* output the \r to the stream */
            c = '\r';
            /* drop through */
        default:
            s[i++] = c;
            break;
        case EOF:
            return (0);
            break;
        }
    }
linedone:
    return (i);
}

/*
 **  output line of text from the source
 */

static  unsigned  char  bb[BLKSIZE];    /* blocking buffer */
static  char    first = 0;      /* 1st time thru flag */

/*
 * mpxbb - make up mpx block file output
 * input - buffer address
 *   - byte count
 *       - last write flag
 */

int
mpxbb(fd, buf, cnt, last)
int fd;
unsigned char   *buf;
int cnt;
int last;
{
    int boff;               /* next write address offset */

    if (!first) {           /* is this 1st time thru */
        first = 1;          /* set the flag */
        memset (bb, '\0', BLKSIZE); /* zero the buffer */
        bb[3] = 4;          /* next write byte offset */
        bb[4] = 0x60;           /* set beg/end of block */
        bb[5] = 0;          /* 1st block count is 0 bytes */
    }
    boff = (bb[2] << 8) | (bb[3]);  /* get next write address offset */
    if (last)
        goto alldone;           /* close out the file */

    /* see if enough room in buffer for this record */
    /* add current offset + 2 (for last record info) plus new */
    /* record size plus 4 (2 for this rec, 2 for last) */
    if ((boff + 2 + cnt + 4) >= BLKSIZE) {
        /* not enough space, write out this record */
        if (write(fd, bb, BLKSIZE) < 0)
            return(-1);
        memset (bb, '\0', BLKSIZE); /* zero the buffer */
        bb[4] = 0x60;           /* set beg/end of block */
        bb[5] = 0;          /* 1st block count is 0 bytes */
                    /* after 1st write */
        boff = 4;           /* init count at 4 bytes */
    }
    /* we have enough room, move in the record */
    /* clear last record end of block flag, set up this record */
    /* info and last rec info at end of data, and update cnt */
    bb[boff] &= ~0x20;          /* clear end of block flag */
    bb[boff+2] = 0x00;          /* clear this blocks flags */
    bb[boff+3] = cnt;           /* set this record count */
    memcpy(&bb[boff+4], buf, cnt);  /* copy in the data */
    boff += (cnt+4);            /* update count */
    bb[boff] = 0x20;            /* set eob status flag */
    bb[boff+1] = cnt;           /* set last rec byte count */
    bb[2] = (boff & 0xff00) >> 8;   /* set hi byte of count */
    bb[3] = (boff & 0xff);      /* set lo byte of count */
    return(cnt);            /* done */

alldone:
    /* that was the last record, set eof flag in bb, write it and exit */
    /* see if enough room in buffer for EOM record */
    /* add current offset + 2 (for last record info) plus new */
    /* EOF record size of 4 (2 for this rec, 2 for last) */
    if ((boff + 2 + 4) >= BLKSIZE) {
        /* not enough space, write out this record */
        if (write(fd, bb, BLKSIZE) < 0)
            return(-1);
        memset (bb, '\0', BLKSIZE); /* zero the buffer */
        bb[4] = 0x60;           /* set beg/end of block */
        bb[5] = 0;          /* 1st block count is 0 bytes */
                    /* after 1st write */
        boff = 4;           /* init count at 4 bytes */
    }
    bb[boff] &= ~0x20;          /* clear end of block flag */
/*  bb[boff+2] = 0xa0;          /* clear this blocks flags */
    bb[boff+2] = 0x80;          /* clear this blocks flags */
    bb[boff+3] = 0;         /* set record count of 0 */
    bb[boff+4] = 0xa0;          /* set EOF/EOB flags */
    boff += 4;              /* 4 more bytes */
    bb[2] = (boff & 0xff00) >> 8;   /* set hi byte of count */
    bb[3] = (boff & 0xff);      /* set lo byte of count */
    /* write out EOF record */
    if (write(fd, bb, BLKSIZE) < 0)
        return(-1);
    first = 0;              /* reset 1st time flag */
    return(cnt);            /* get out, done */
}

