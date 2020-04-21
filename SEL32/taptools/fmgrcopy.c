/*
 * fmgrcopy.c
 *
 * This program scans a metatape file and prints file count and sizes.
 * In addition, it creats a directory for each username specified in
 * the filemanager tape and then creates a file containing the file
 * contents.  The file must be a filemgr save tape and not an SDT
 * tape.
 * input - stdin or specified filename  
 * output - stdout
 */

#include <stdio.h>
#include <sys/file.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

int lfilen, filen = 1;
int EOFcnt = 0;
int count=0, lcount=0;
int size=0, tsize=0;
int size_512K = 512 * 1024;
int ln;
#ifdef USE_READ
FILE *infp;
#else
int inp, outp;
#endif
#define PRINTABLE(x) ((x < 32) || (x > 126)) ? '.' : x

/* get a line of input. */
int getloi(char *s, int lim)
{
    int c, i;
    int32_t n1, n2, hc, tc, n;

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
            lfilen = filen;
            filen++;    /* set next file number */
        }
        count = 0;      /* file record count back to zero */
        lcount = 0;     /* last record count back to zero */
        tsize += size;  /* add to total tape size */
        size = 0;       /* file size back to zero */
        ln = -1;        /* set ln to -1 showing we are at EOF */

        /* we have EOF */
        return 0;       /* return EOF on tape data */
    }
    if (hc == -1)
        return -1;      /* at EOM on disk file */

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
        ln = n;
        lcount = count;
    }
    /* return bytes in buffer */
    return n;
}

int main (int argc, char *argv[])
{
    char *buf;
    size_t size_512K = 512 * 1024;
    size_t buf_size = 512 * 1024;
    char *cp, *np;
    int ll;
    char path[64], command[128];

    if (argc != 2) {
        //fprintf(stderr, "usage: %s infile\n", argv[0]);
        printf("usage: %s infile\n", argv[0]);
        exit(1);
    } /* end of if */

    if ((inp = open(argv[1], O_RDONLY, 0666)) < 0) {
        //fprintf(stderr,"%s: fopen: unable to open input file %s\n", argv[0], argv[1]);
        printf("%s: fopen: unable to open input file %s\n", argv[0], argv[1]);
        return (1);
    }
    outp = -1;

    /* get a 512k buffer */
    if ((buf = malloc(buf_size)) == NULL) {
        //fprintf(stderr, "Can't allocate memory for tscan\n");
        printf("Can't allocate memory for tscan\n");
        return (4);
    }
    /* init counts */
    ln = -2;    /* look for 2 eof */
    count = 0;
    size = 0;
    tsize = 0;
    lcount = 0;
    filen = 1;
    lfilen = 0;
    printf("\nfile %d:\n", filen);

    /* get lines until eof */
    while ((ll=getloi(buf, buf_size)) != EOF) {
        if (ll == 0) {
            /* eof found, process new file */
            printf("\nfile %d:\n", filen);
        } else {
            int cc = 0;
            unsigned int curchar;

            /* filemgr smd entries are 8 words, and are in 1152 words (4608 bytes)
             * (6 sector) blocks.  Saved data files are modulo 1152 words also */
            /* 8 wds per SMD entry, 24 entries per sector, 144 entries per 6 sector
             * block. */
            int i, j, m;
//          if (filen == 1)
            {
                /* read smd entry */
                char file[20], dir[20];
                int l = 0;
                int smddone = 0;
                int totent = 0;
                char *buf2 = buf;
                printf("\nfile %d:\n", filen);
                /* see how man entries here */
                while(!smddone) {
                    /* process entries in this record */
                    for (j=0; j<144; j++) {
                        int k = l++ * 32;
                        int w1 = (buf[k+13] & 0xff) << 16 | (buf[k+14] & 0xff) << 8 | (buf[k+15] & 0xff);
                        /* stop processing on first zero smd entry */
                        if (w1 <= 0) {
                            smddone = 1;
                            break;
                        }
                        totent++;
                        /* get file/dir name */
                        for (i=0; i<8; i++) {
                            file[i] = tolower(buf[k+0+i]);
                            if (file[i] == ' ')
                                file[i] = '\0';
                        }
                        file[8] = '\0';
                        for (i=0; i<8; i++) {
                            dir[i] = tolower(buf[k+0+16+i]);
                            if (dir[i] == ' ')
                                dir[i] = '\0';
                        }
                        dir[8] = '\0';
                        if (dir[0] == '\0')
                            sprintf(dir, "%s", "system");
                        sprintf(path, "./%s/%s", dir, file);
                        // see if active file
                        if ((buf[k+12] & 0x80) == 0)
                            // not active goon
                            printf("inactive file: w1 = %d path = %s\n", w1, path);
                        else
                            printf("active file: w1 = %d path = %s\n", w1, path);
                    }
                    if (smddone)
                        break;
                    buf2 += 4608;       /* next buffer */
                    ll=getloi(buf2, 4608);
                }
                printf("%d smd entries found\n", totent);
                /* we have directory entries */
                for (j=0; j<totent; j++) {
                    int k = j * 32;
                    /* get file size in blocks */
                    int w1 = (buf[k+13] & 0xff) << 16 | (buf[k+14] & 0xff) << 8 | (buf[k+15] & 0xff);
                    int blks = w1;      /* save block count */

                    /* get file/dir name */
                    for (i=0; i<8; i++) {
                        file[i] = tolower(buf[k+0+i]);
                        if (file[i] == ' ')
                            file[i] = '\0';
                    }
                    file[8] = '\0';
                    for (i=0; i<8; i++) {
                        dir[i] = tolower(buf[k+0+16+i]);
                        if (dir[i] == ' ')
                            dir[i] = '\0';
                    }
                    dir[8] = '\0';
                    if (dir[0] == '\0')
                        sprintf(dir, "%s", "system");
                    sprintf(path, "./%s", dir);

                    // see if active file
                    if ((buf[k+12] & 0x80) == 0) {
                        // not active goon
                        printf("inactive file: w1 = %d\n", w1);
                    }
                    printf("active file: w1 = %d\n", w1);
                    if (w1 <= 0)
                        break;
                    /* create the directory/file */
                    printf("path = %s\n", path);
                    sprintf(command, "mkdir -p %s", path);
                    printf("command = %s\n", command);
                    system(command);
                    sprintf(path, "./%s/%s", dir, file);
                    printf("file %d = %s\n", j+1, path);
                    sprintf(command, "touch %s", path);
                    printf("command = %s\n", command);
                    system(command);

                    if (outp >= 0)
                        close(outp);
                    outp = -1;

#ifndef DO_LATER
                    /* open output file, create it if necessary */
                    if ((outp = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0) {
                        //fprintf(stderr, "Can't open %s\n", path);
                        printf("Can't open %s\n", path);
                        close(inp);
                        free(buf);
                        return (3);
                    }
#endif
                    /* get the file data */
                    for (m=0; m<((w1+5)/6); m++) {
                        char data[5000];        /* data buffer */
                        ll=getloi(data, 4608);

                        /* process file data for file */
                        if (ll == 4608) {
#ifndef DO_LATER
                            /* blks/w1 have number of blocks to write */
                            int bcnt, no;       /* block count */
                            if (blks >= 6) {
                                blks -= 6;      /* enough for 6 block, write them */
                                bcnt = 6*768;   /* write all 6 blocks */
                            } else {
                                bcnt = blks*768;/* just write what we need */
                                blks = 0;
                            }
                            /* only write number of sectors on save tape, not all 4608 */
                            /* if zero, just reading excess blocks */
                            if (bcnt != 0) {
                                no = write(outp, data, bcnt);
                                if (no != bcnt)
                                    // fprintf(stderr, "write (%d) != read (%d) on file %s\n", no, bcnt, path);
                                    printf("write (%d) != read (%d) on file %s\n", no, bcnt, path);
                            }
#else
                                printf("read (%d) on file %s\n", ll, path);
#endif
                        } else {
                            printf("Bad file size read! %d instead of 4608\n", ll);
///                         if (ll == -1) break;
                            if (ll == -1)
                                goto dostop;
                        }
                    } /* end writing file */
                } /* end of smd scan */
            } /* read smd entries 4608 byte records */
        } /* process read of smd or sdt */
    } /* end of getloi read */
dostop:
    close(inp);
    free(buf);
    if (outp >= 0)
        close(outp);
    exit(0);
}
