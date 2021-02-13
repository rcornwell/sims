/*
 * MPX uses 2 EOF in a row to separate sections of MPX3.x master SDT tapes.
 * It uses 3 EOF in a row to indicate the EOT on MPX 3.X tapes.  So we
 * cannot assume EOT is at the 1st or 2nd EOF in a row.  Keep looking
 * for a third one.  Comment out the #define FMGRTAPE below to read an
 * MPX 3.x master SDT.  For user SDT tapes or MPX 1.X master SDT tapes
 * leave the #define FMGRTAPE uncommented so it will be defined.  The
 * program will stop on two EOFs.  For non MPX tapes, the 2nd EOF means
 * EOT. Some tapes have only one EOT and will terminate on EOT detected.
 * Leave off the output file name to just scan the tape and output record
 * sizes and counts.
 */

#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/mtio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

//#define FMGRTAPE              /* defined for filemgr tapes, undefined for volmgr tape */

char *buff;                     /* buffer for read/write */
int filen = 1;                  /* file number being processed */
long count=0, lcount=0;         /* number of blocks for file */
extern void RUBOUT();           /* handle user DELETE key signal */
off_t size=0, tsize=0;          /* number of bytes in file, total */
int ln;
char *inf, *outf;
int copy;
int32_t size_1K = 1024;
#if 0
int32_t size_128K = 128 * 1024;
int32_t size_256K = 256 * 1024;
#endif

int main(argc, argv)
int argc;
char **argv;
{
    int n, nw, inp, outp;
    struct mtop op;
//  int32_t buf_size = size_256K;
    int32_t buf_size = size_1K;
    int EOFcnt = 0;     /* count the number of EOFs in a row. */

    if (argc <= 1 || argc > 3) {
        (void)fprintf(stderr, "Usage: disk2tap src [dest]\n");
        return (1);
    }
    inf = argv[1];
    if (argc == 3) {
        outf = argv[2];
        copy = 1;
    }
    if ((inp = open(inf, O_RDONLY, 0666)) < 0) {
        (void)fprintf(stderr, "Can't open %s\n", inf);
        return (1);
    }
    if (copy) {
        /* open output file, create it if necessary */
        if ((outp = open(outf, O_WRONLY|O_CREAT, 0666)) < 0) {
            (void)fprintf(stderr, "Can't open %s\n", outf);
            return (3);
        }
    }
    /* get a 1k buffer */
    if ((buff = malloc(buf_size)) == NULL) {
        (void)fprintf(stderr, "Can't allocate memory for disk2tap\n");
        return (4);
    }
    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        (void)signal(SIGINT, RUBOUT);

    ln = -2;
    for (;;) {
        count++;
        errno = 0;
        /* read first record to get size for buffer, doubling each time unsuccessful */
        while ((n = read(inp, buff, buf_size)) < 0) {
            if (errno == ENOMEM) {
#if 0
                if (buf_size < size_256K)
                    buf_size = size_256K;
                else
                    buf_size *= 2;
#endif
                free(buff);
                if ((buff = malloc(buf_size)) == NULL) {
                    (void)fprintf(stderr, "Can't allocate memory for tapecopy\n");
                    return (4);
                }
                op.mt_op = MTFSF;   /* Rewind to start of file */
                op.mt_count = (daddr_t) 0;
                if (ioctl(inp, MTIOCTOP, (char *)&op) < 0) {
                    perror("Read buffer size error");
                    return (6);
                }
                errno = 0;
                continue;
            }
            perror("Unknown read error");
            errno = 0;
//          return (6);     /* abort on error, comment out to ignore taoe errors */
        }
        if (n > 0) {
            /* we read some data, see if scanning or writing */
            EOFcnt = 0;     /* not at EOF anymore */
            if (copy) {
                int32_t n1, n2;
                /* we have data to write */
                int32_t hc = (n + 1) & ~1;      /* make byte count even */
                int32_t wc = n;                 /* get actual byte count */
                /* write actual byte count to 32 bit word as header */
                n1 = write(outp, (char *)(&wc), (int32_t)4);
                /* write the data mod 2 */
                nw = write(outp, buff, (int32_t)hc);
                /* write the byte count in 32 bit word as footer */
                n2 = write(outp, (char *)(&wc), (int32_t)4);
                if (n1 != 4 || nw != hc || n2 != 4) {
                    fprintf(stderr, "write (%d) !=" " read (%d)\n", nw, n);
                    fprintf(stderr, "COPY " "Aborted\n");
                    return (5);
                }
            }
            size += n;      /* update bytes read */
            if (n != ln) {  /* must be last record of file if different */
                if (ln > 0) {
                    /* we read something */
                    if ((count - lcount) > 1)
                        (void)printf("file %d: records %ld to %ld: size %d\n", filen, lcount, count - 1, ln);
                    else
                        (void)printf("file %d: record %ld: size %d\n", filen, lcount, ln);
                }
                ln = n;             /* save last record size */
                lcount = count;     /* also record count */
            }
        } else {
            /* we did not read data, it must be an EOF */
            /* if ln is -1, last operation was EOF, now we have a second */
#ifdef FMGRTAPE
            /* filemgr has 2 EOF's at end of tape */
            if (++EOFcnt > 1) {
                /* two EOFs mean we are at EOT */
                (void)printf("fmgr eot\n");
                break;
            }
#else
            /* volmgr has 3 EOF's at end of tape */
            if (++EOFcnt > 2) {
                /* three EOFs mean we are at EOT on MPX */
                (void)printf("volm eot\n");
                break;
            }
#endif
            if (ln > 0) {
                if (count - lcount > 1)
                    (void)printf("file %d: records %ld to %ld: size %d\n", filen, lcount, count - 1, ln);
                else
                    (void)printf("file %d: record %ld: size %d\n", filen, lcount, ln);
            }
#ifdef FMGRTAPE
            (void)printf("file %d: eof after %ld records: %ld bytes\n", filen, count - 1, size);
#else
            if (EOFcnt == 2)        /* if 2nd EOF, print file info */
                (void)printf("second eof after %d files: %ld bytes\n", filen, size);
#endif
            if (copy) {
                /* write a sudo EOF to disk file as a zero 4 byte record */
                int n1, hc = 0;
                /* write the EOF */
                /* write a zero as the byte count in 32 bit word as EOF */
                n1 = write(outp, (char *)(&hc), (int32_t)4);
                if (n1 != 4) {
                    perror("Write EOF");
                    return (6);
                }
            }
#ifdef FMGRTAPE
            filen++;            /* advance number of files */
#else
            if (EOFcnt < 2)     /* not really a file if 2nd EOF */
                filen++;        /* advance number of files */
#endif
            count = 0;      /* file record count back to zero */
            lcount = 0;     /* last record count back to zero */
            tsize += size;  /* add to total tape size */
            size = 0;       /* file size back to zero */
            ln = n;         /* set ln to -1 showing we are at EOF */
        }
    }
    if (copy) {
        /* write a sudo EOM to disk file as a -1 4 byte record */
        int32_t n1, hc = 0xffffffff;
        /* write the EOM to disk */
        /* write a -1 as the byte count in 32 bit word as EOM */
        n1 = write(outp, (char *)(&hc), (int32_t)4);
        if (n1 != 4) {
            perror("Write EOM");
            return (6);
        }
        (void)close(outp);
    }
    /* print out total tape size in bytes */
    (void)printf("total length: %ld bytes\n", tsize);
    return (0);
}

/* entered when user hit the DELETE key */
void RUBOUT()
{
    if (count > lcount)
        --count;
    if (count)
        if (count > lcount)
            (void)printf("file %d: records %ld to %ld: size" " %d\n", filen, lcount, count, ln);
        else
            (void)printf("file %d: record %ld: size %d\n", filen, lcount, ln);
    (void)printf("interrupted at file %d: record %ld\n", filen, count);
    (void)printf("total length: %ld bytes\n", tsize + size);
    exit(1);
}
