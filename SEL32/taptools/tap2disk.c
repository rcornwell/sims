/*
 * tap2disk.c
 *
 * This program scans a metatape file and copies a file until EOF is found.
 * input - specified filename  
 * output - specified filename
 */

#include <stdio.h>
#include <sys/file.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int     filen = 1;
int     EOFcnt = 0;
int     count=0, lcount=0;
int     size=0, tsize=0;
int     oldsize, newsize;
FILE    *outfp;
int     inp;
int     ln;

/* get a line of input. */
int getloi(char *s, int lim)
{
    int     c, i;
    int     n1, n2, hc, tc, n;

    /* read the byte count in 32 bit word as header */
    n1 = read(inp, (char *)(&hc), (size_t)4);
    if (n1 <= 0) {
        hc = -1;                /* at EOM on disk file */
        return hc;              /* return EOM */
    }

    /* check for EOF & EOM on tape data */
    if (hc & 0xffff0000) {      /* check for garbage, assume EOF */
        hc = -1;                /* at EOM on disk file */
        return hc;              /* return EOM */
    }

    /* check for EOF & EOM on tape data */
    if (hc == 0) {
        /* we are at tape EOF */
        if (++EOFcnt < 2) {     /* if 1st EOF, print32_t file info */
            if (ln > 0) {
                if (count - lcount > 1)
                    fprintf(stderr, "file %d: records %d to %d: size %d\n", filen, lcount, count - 1, ln);
                else
                    fprintf(stderr, "file %d: record %d: size %d\n", filen, lcount, ln);
            }
            fprintf(stderr, "file %d: EOF after %d records: %d bytes\n\n", filen, count, size);
            filen++;    /* set next file number */
        } else {
            fprintf(stderr, "second EOF after %d files: %d bytes\n", filen-1, tsize+size);
        }
        count = 0;      /* file record count back to zero */
        lcount = 0;     /* last record count back to zero */
        tsize += size;  /* add to total tape size */
        size = 0;       /* file size back to zero */
        ln = -1;        /* set ln to -1 showing we are at EOF */
        return 0;               /* return EOF on tape data */
    }

    /* check for EOM */
    if (hc == -1) {
        return -1;              /* at EOM on disk file */
    }

    /* read the data */
    n = read(inp, s, (size_t)hc);
    if (n <= 0)
        return -1;              /* at EOM on disk file */

    /* if odd byte record, read extra byte and throw it away */
    if (n & 0x1) {
        n2 = read(inp, (char *)(&tc), (size_t)1);
        if (n2 <= 0)
            return -1;          /* at EOM on disk file */
    }

    /* read the byte count in 32 bit word as trailer */
    n2 = read(inp, (char *)(&tc), (size_t)4);
    if (n2 <= 0)
        return -1;              /* at EOM on disk file */

    count++;                    /* bump record count */
    size += n;                  /* update bytes read */
    EOFcnt = 0;                 /* not an EOF */
    if (n != ln) {
        if (ln > 0) {
            if (count - lcount > 1)
                fprintf(stderr, "file %d: records %d to %d: size %d\n", filen, lcount, count - 1, ln);
            else
                fprintf(stderr, "file %d: record %d: size %d\n", filen, lcount, ln);
        }
        ln = n;
        lcount = count;
    }
    /* return bytes in buffer */
    return n;
}

void putrec(int cnt, char *buf)
{
    int  n1, n2, nw;
    int  hc = (cnt + 1) & ~1;        /* make byte count even */
    int  ac = cnt;                   /* get actual byte count */

//printf("writing %d chars\n", cnt);
#if 0
    /* write actual byte count to 32 bit word as header */
    n1 = fwrite((char *)(&hc), (size_t)1, (size_t)4, outfp);
#endif
    /* write the data mod 2 */
    nw = fwrite((char *)buf, (size_t)1, (size_t)hc, outfp);
#if 0
    /* write the byte count in 32 bit word as footer */
    n2 = fwrite((char *)(&hc), (size_t)1, (size_t)4, outfp);
#endif
#if 0
    if (n1 != 4 || nw != hc || n2 != 4)
#else
    if (nw != hc)
#endif
    {
        fprintf(stderr, "write (%d) failure\n", nw);
        fprintf(stderr, "Operation aborted\n");
        fclose(outfp);
        close(inp);
        free(buf);
        exit(1);
    }
}

int main (int argc, char *argv[])
{
    char *buf;
    size_t buf_size = 256 * 1024;
    size_t ll, gotboth = 0;
    int zero = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: %s infile outfile\n", argv[0]);
        exit(1);
    }

    /* open input file */
    if ((inp = open(argv[1], O_RDONLY, 0666)) < 0) {
        fprintf(stderr,"%s: fopen: unable to open input file %s\n", argv[0], argv[1]);
        return (1);
    }

    close(inp);                             /* close input */
    outfp = fopen(argv[1],"r");             /* reopen */
    fseek(outfp, 0, SEEK_END);              /* seek to end */
    oldsize = ftell(outfp);                 /* get filesize in bytes */
    fclose(outfp);                          /* now close it */

    /* open input file */
    if ((inp = open(argv[1], O_RDONLY, 0666)) < 0) {
        fprintf(stderr,"%s: fopen: unable to open input file %s\n", argv[0], argv[1]);
        return (1);
    }

    /* open output file */
    outfp = fopen(argv[2],"w");
    if (outfp == NULL) {
        fprintf(stderr,"%s: fopen: unable to open output file %s\n", argv[0], argv[2]);
        exit(1);
    }

    /* init counts */
    ln = -2;    /* look for 2 eof */
    count = 0;
    size = 0;
    tsize = 0;
    lcount = 0;

    /* get a 256k buffer */
    if ((buf = malloc(buf_size)) == NULL) {
        fprintf(stderr, "Can't allocate memory for %s\n", argv[0]);
        return (4);
    }

    /* get buffers until eof */
domore:
    while ((ll=getloi(buf, buf_size)) > 0) {
//printf("got %d char\n", ll);
        /* we have data to write */
        putrec(ll, buf);            /* write the buffer */
        gotboth = 0;
    }
//printf("we have EOF %d\n", gotboth+1);
    /* we have an EOF or an EOM */
    if (ll == 0) {
        /* we have EOF, write a zero */
        fwrite((char *)(&zero), (size_t)1, (size_t)4, outfp);
        gotboth++;                  /* one more EOF */
        if (gotboth == 2) {
            /* we have written both EOF's, now write EOM */
            ll = -1;
        } else {
            goto domore;            /* get more data */
        }
    }

//printf("we have EOM, DONE\n");
    /* We have EOM, see if EOF's needed */
    switch (gotboth) {
    case 0:                 /* we have written no EOFs, so write two */
        fwrite((char *)(&zero), (size_t)1, (size_t)4, outfp);
        /* drop through */
    case 1:                 /* we have written 1 EOF, so write one more */
        fwrite((char *)(&zero), (size_t)1, (size_t)4, outfp);
        /* drop through */
    default:
    case 2:                 /* we have written 2 EOFs, now do EOM */
        zero = -1;
        fwrite((char *)(&zero), (size_t)1, (size_t)4, outfp);
    }
    fprintf(stderr, "EOM after 2 EOFs %d files: %d bytes\n", filen-1, tsize);

    newsize = ftell(outfp);                 /* get filesize in bytes */
    fprintf(stderr, "Size of file changed from %d to %d\n", oldsize, newsize);

    /* we done */
    fclose(outfp);
    close(inp);
    free(buf);
    exit(0);
}
