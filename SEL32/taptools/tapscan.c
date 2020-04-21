/*
 * tapscan.c
 *
 * This program scans a metatape file and prints file count and sizes.
 * input - stdin or specified filename  
 * output - stdout
 */

#include <stdio.h>
#include <sys/file.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int filen = 1;
int EOFcnt = 0;
int count=0, lcount=0;
int size=0, tsize=0;
int size_512K = 512 * 1024;
int ln;
#ifdef USE_READ
FILE *infp;
#else
int inp;
#endif

/* get a line of input. */
int getloi(char *s, int lim)
{
    int c, i;
    int n1, n2, hc, tc, n;

    errno = 0;
    /* read the byte count in 32 bit word as header */
    n1 = read(inp, (char *)(&hc), (size_t)4);
    if (n1 <= 0)
        hc = -1;        /* at EOM on disk file */

    /* check for EOF & EOM on tape data */
    if (hc & 0xffff0000)    /* check for garbage, assume EOM */
        hc = -1;        /* at EOM on disk file */

    /* check for EOF & EOM on tape data */
    if (hc == 0) {
        /* we are at tape EOF */
        if (++EOFcnt < 2) {     /* if 1st EOF, print file info */
            if (ln > 0) {
                if (count - lcount > 1)
                    fprintf(stderr, "file %d: records %d (0x%x) to %d (0x%x): size %d (0x%x)\n",
                        filen, lcount, lcount, count-1, count-1, ln, ln);
                else
                    fprintf(stderr, "file %d: record %d (0x%x): size %d (0x%x)\n",
                        filen, lcount, lcount, ln, ln);
            }
            fprintf(stderr, "file %d: eof after %d (0x%x) records: %d (0x%x) bytes\n",
                filen, count, count, size, size);
            filen++;    /* set next file number */
        } else {
            fprintf(stderr, "second eof after %d (0x%x) files: %d (0x%x) bytes\n",
                filen-1, filen-1, size, size);
        }
        count = 0;      /* file record count back to zero */
        lcount = 0;     /* last record count back to zero */
        tsize += size;  /* add to total tape size */
        size = 0;       /* file size back to zero */
        ln = -1;        /* set ln to -1 showing we are at EOF */

        /* we have EOF */
        return 0;       /* return EOF on tape data */
    }
    if (hc == -1) {
        /* we have EOM */
        fprintf(stderr, "mpx eot\n");
        /* print out total tape size in bytes */
        fprintf(stderr, "total length: %ld (0x%lx) bytes\n", tsize, tsize);
        return -1;      /* at EOM on disk file */
    }
    /* read the data */
    n = read(inp, s, (size_t)hc);

    /* if odd byte record, read extra byte and throw it away */
    if (n & 0x1) {
        n2 = read(inp, (char *)(&tc), (size_t)1);
        if (n2 <= 0)
            return -1;          /* at EOM on disk file */
    }

    /* read the byte count in 32 bit word as trailer */
    n2 = read(inp, (char *)(&tc), (size_t)4);
    count++;        /* bump record count */
    size += n;      /* update bytes read */
    EOFcnt = 0;     /* not an EOF */
    if (n != ln) {
        if (ln > 0) {
            if (count - lcount > 1)
                fprintf(stderr, "file %d: records %d (0x%x) to %d (0x%x): size %d (0x%x)\n",
                    filen, lcount, lcount, count-1, count-1, ln, ln);
            else
                fprintf(stderr, "file %d: record %d (0x%x): size %d (0x%x)\n",
                    filen, lcount, lcount, ln, ln);
        }
        ln = n;
        lcount = count;
    }
    /* return bytes in buffer */
    return n;
}

int main (int argc, char *argv[])
{
//  char in[BUFSIZ], out[BUFSIZ];
    char *buf;
    size_t size_512K = 512 * 1024;
    size_t buf_size = 512 * 1024;
    char *cp, *np;
    int ll, gotboth = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s infile\n", argv[0]);
        exit(1);
    } /* end of if */

#ifdef USE_READ
    /* open input file */
    infp = fopen(argv[1],"r");
    if (infp == NULL) {
        fprintf(stderr,"%s: fopen: unable to open input file %s\n", argv[0], argv[1]);
        exit(1);
    } /* end of if */
#else
    if ((inp = open(argv[1], O_RDONLY, 0666)) < 0) {
        fprintf(stderr,"%s: fopen: unable to open input file %s\n", argv[0], argv[1]);
        return (1);
    }
#endif

    /* get a 512k buffer */
    if ((buf = malloc(buf_size)) == NULL) {
        fprintf(stderr, "Can't allocate memory for %s\n", argv[0]);
        return (4);
    }

    /* init counts */
    ln = -2;    /* look for 2 eof */
    count = 0;
    size = 0;
    tsize = 0;
    lcount = 0;

    /* get lines until eof */
    while (((ll=getloi(buf, buf_size)) != EOF)) {
        ;
    }
    close(inp);
    free(buf);
    exit(0);
}
