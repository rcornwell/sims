/*
 * tape2disk.c
 *
 * MPX uses 2 EOF in a row to separate sections of MPX3.x master SDT tapes.
 * It uses 3 EOF in a row to indicate the EOT on MPX 3.X tapes.  So we
 * cannot assume EOT is at the 1st or 2nd EOF in a row.  We keep looking
 * for a third one.  For user SDT tapes or MPX 1.X master SDT tapes use
 * option -f for 2 EOFs.  Use option -v for 3 eof's on VOLM tapes.  For
 * non MPX tapes, the 2nd EOF means EOT. Some tapes (Unix) have only one
 * EOF and will terminate on EOT detected. Leave off the output file name
 * to just scan the tape and output record sizes and counts.
 */

#include <stdio.h>
#include <sys/types.h>
//#include <sys/mtio.h>
#include <stdlib.h>                 /* for exit() */
#ifdef _WIN32
#include <fcntl.h>                  /* for O_RDONLY, O_WRONLY, O_CREAT */
#include <io.h>                     /* for _read, _open, _write, _close */
#define open _open
#define read _read
#define write _write
#define close _close
#else
#include <sys/fcntl.h>              /* for O_RDONLY, O_WRONLY, O_CREAT */
#include <unistd.h>                 /* for open, read, write */
#include <sys/signal.h>
#endif

#if defined(_MSC_VER) && (_MSC_VER < 1600)
typedef __int8           int8;
typedef __int16          int16;
typedef __int32          int32;
typedef unsigned __int8  uint8;
typedef unsigned __int16 uint16;
typedef unsigned __int32 uint32;
typedef signed __int64   t_int64;
typedef unsigned __int64 t_uint64;
//typedef t_int64          off_t;
#else                                                   
/* All modern/standard compiler environments */
/* any other environment needa a special case above */
#include <stdint.h>
typedef int8_t          int8;
typedef int16_t         int16;
typedef int32_t         int32;
typedef uint8_t         uint8;
typedef uint16_t        uint16;
typedef uint32_t        uint32;
#endif                              /* end standard integers */
//#define FMGRTAPE  /* defined for filemgr tapes, undefined for volmgr tape */

int  usefmgr = 1;                   /* use fmgr format with 2 EOF's, else 3 EOF's */
char *buff;                         /* buffer for read/write */
int filen = 1;                      /* file number being processed */
long count=0, lcount=0;             /* number of blocks for file */
#ifndef _WIN32
extern void RUBOUT();               /* handle user DELETE key signal */
#endif
off_t size=0, tsize=0;              /* number of bytes in file, total */
int ln;
char *inf, *outf;
int copy;
int32 size_256K = 256 * 1024;

int main(argc, argv)
int argc;
char **argv;
{
    int n, nw, inp, outp;
//  struct mtop op;
    int32 buf_size = size_256K;
    int EOFcnt = 0;                 /* count the number of EOFs in a row. */
    char *name = *argv;
    char *p = *++argv;

    if (argc <= 1 || argc > 4) {
        fprintf(stderr, "Usage: tape2disk -vf src [dest]\n");
        exit(1);
    }
    if (*p == '-') {
        char ch = *++p;
        argc--;                     /* one less arg */
        if (ch == 'v')
            usefmgr = 0;            /* use volmgr format */
        else
        if (ch == 'f')
            usefmgr = 1;            /* use fmgr format */
        else {
            fprintf(stderr, "Invalid option %c\n", ch);
            fprintf(stderr, "Usage: tape2disk -vf src [dest]\n");
            exit(1);
        }
    }
    inf = argv[1];
    if (argc == 3) {
        outf = argv[2];
        copy = 1;
    }
    if ((inp = open(inf, O_RDONLY, 0666)) < 0) {
        fprintf(stderr, "Can't open %s\n", inf);
        exit(1);
    }
    if (copy) {
        /* open output file, create it if necessary */
        if ((outp = open(outf, O_WRONLY|O_CREAT, 0666)) < 0) {
            fprintf(stderr, "Can't open %s\n", outf);
            exit(3);
        }
    }
    /* get a 256k buffer */
    if ((buff = malloc(buf_size)) == NULL) {
        fprintf(stderr, "Can't allocate memory for tapecopy\n");
        exit(4);
    }
#ifndef _WIN32
    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
        (void)signal(SIGINT, RUBOUT);
#endif

    ln = -2;
    for (;;) {
        count++;
        /* read record */
        while ((n = read(inp, buff, buf_size)) < 0) {
            perror("Unknown read error");
//          errno = 0;
            exit(6);                /* abort on error, comment out to ignore tape errors */
        }
        if (n > 0) {
            /* we read some data, see if scanning or writing */
            EOFcnt = 0;             /* not at EOF anymore */
            if (copy) {
                int32 n1, n2;
                /* we have data to write */
                int32 hc = (n + 1) & ~1;  /* make byte count even */
                int32 wc = n;       /* get actual byte count */
                /* write actual byte count to 32 bit word as header */
                n1 = write(outp, (char *)(&wc), (int32)4);
                /* write the data mod 2 */
                nw = write(outp, buff, (int32)hc);
                /* write the byte count in 32 bit word as footer */
                n2 = write(outp, (char *)(&wc), (int32)4);
                if (n1 != 4 || nw != hc || n2 != 4) {
                    fprintf(stderr, "write (%d) !=" " read (%d)\n", nw, n);
                    fprintf(stderr, "COPY " "Aborted\n");
                    exit(5);
                }
            }
            size += n;              /* update bytes read */
            if (n != ln) {          /* must be last record of file if different */
                if (ln > 0) {
                    /* we read something */
                    if ((count - lcount) > 1)
                        printf("file %d: records %ld to %ld: size %d\n",
                            filen, lcount, count - 1, ln);
                    else
                        printf("file %d: record %ld: size %d\n", filen, lcount, ln);
                }
                ln = n;             /* save last record size */
                lcount = count;     /* also record count */
            }
        } else {
            /* we did not read data, it must be an EOF */
            /* if ln is -1, last operation was EOF, now we have a second */
            /* see if fmgr or volm */
            if (usefmgr) {
                /* filemgr has 2 EOF's at end of tape */
                if (++EOFcnt > 1) {
                    /* two EOFs mean we are at EOT */
                    printf("fmgr eot\n");
                break;
                }
            } else {
                /* volmgr has 3 EOF's at end of tape */
                if (++EOFcnt > 2) {
                    /* three EOFs mean we are at EOT on MPX */
                    printf("volm eot\n");
                    break;
                }
            }
            if (ln > 0) {
                if ((count - lcount) > 1)
                    printf("file %d: records %ld to %ld: size %d\n",
                        filen, lcount, count - 1, ln);
                else
                    printf("file %d: record %ld: size %d\n", filen, lcount, ln);
            }
            if (usefmgr) {
                printf("file %d: eof after %ld records: %ld bytes\n",
                    filen, count - 1, size);
            } else {
                if (EOFcnt == 2)    /* if 2nd EOF, print file info */
                    printf("second eof after %d files: %ld bytes\n", filen, size);
            }

            if (copy) {
                /* write a sudo EOF to disk file as a zero 4 byte record */
                int n1, hc = 0;
                /* write the EOF */
                /* write a zero as the byte count in 32 bit word as EOF */
                n1 = write(outp, (char *)(&hc), (int32)4);
                if (n1 != 4) {
                    perror("Write EOF");
                    exit(6);
                }
            }
            if (usefmgr)
                filen++;            /* advance number of files */
            else 
                if (EOFcnt < 2)     /* not really a file if 2nd EOF */
                    filen++;        /* advance number of files */
            count = 0;              /* file record count back to zero */
            lcount = 0;             /* last record count back to zero */
            tsize += size;          /* add to total tape size */
            size = 0;               /* file size back to zero */
            ln = n;                 /* set ln to -1 showing we are at EOF */
        }
    }
    if (copy) {
        /* write a sudo EOM to disk file as a -1 4 byte record */
        int32 n1, hc = 0xffffffff;
        /* write the EOM to disk */
        /* write a -1 as the byte count in 32 bit word as EOM */
        n1 = write(outp, (char *)(&hc), (size_t)sizeof(hc));
        if (n1 != 4) {
            perror("Write EOM");
            return(6);
        }
        (void)close(outp);
    }
    /* print out total tape size in bytes */
    (void)printf("total length: %ld bytes\n", tsize);
    exit(0);
}

#ifndef _WIN32
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
#endif
