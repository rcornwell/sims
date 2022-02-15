/*
 * mkdiagtape.c
 *
 * This program extracts the diag command file (file 2) from a
 * diagnostic tape in .tap format and replaces it with a new
 * command file.  The rest of the tape is copied unchanged.
 * intput - simulated indiagtape outdiagtape
 * output - simulated newtape
 * option - -c = cmdfile
 * 08/22/2020
 * 02/14/2022
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#ifdef _WIN32
#include <stddef.h>
#endif
//#include <unistd.h>

#if defined(_MSC_VER) && (_MSC_VER < 1600)
typedef __int8           int8;
typedef __int16          int16;
typedef __int32          int32;
typedef unsigned __int8  uint8;
typedef unsigned __int16 uint16;
typedef unsigned __int32 uint32;
typedef signed __int64   t_int64;
typedef unsigned __int64 t_uint64;
typedef t_int64          off_t;
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
#endif                                      /* end standard integers */

#define BLKSIZE 768                         /* MPX file sector size */
unsigned char data[7680];                   /* room for 10*768=(7680) 768 byte sectors per 7680 byte block */

/* write 1 file to tape in 768 byte records */
/* mblks is the maximum blockes to write from a file, 0=all */
/* chunks is the number of sectors to wrote at a time 1-8 */
int writefile(FILE *tp, char *fnp, uint32 mblks, int32 chunks) {
    int32 word, blks=mblks;                 /* just a temp word variable */         
    int32 size, bsize, csize;               /* size in 768 byte sectors */
    FILE *fp;
    int32_t n1, n2, hc, nw, cs;

    memset((char *)data, 0, sizeof(data));  /* zero data storage */
    /* write file to tape */
    if ((fp = fopen(fnp, "r")) == NULL) {
        fprintf(stderr, "error: can't open user file %s\n", fnp);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);                 /* seek to end */
    word = ftell(fp);                       /* get filesize in bytes */
//printf("MPX file %s is %x (%d) bytes\n", fnp, word, word);
    fseek(fp, 0, SEEK_SET);                 /* rewind file */
    size = (word/768);                      /* filesize in sectors */
    if (word%768 != 0)                      /* see if byte left over */
        size += 1;                          /* partial sector, add 1 */
    if (mblks == 0) {
        mblks = (word/768);                 /* blocks */
        if ((word%768) != 0)                /* round up blks if remainder */
            mblks++;                        /* total block to write */
    }
    blks = mblks/chunks;                    /* chunks */
    if (mblks%768 != 0)                     /* see if blks left over */
        blks += 1;                          /* partial blks, add 1 */

    bsize = mblks;                          /* save # blks */

//printf("MPX file %s is %x (%d) bytes blks %d chk %d\n",
//      fnp, word, word, bsize, (bsize+1)/chunks);
    csize = 0;
    /* read in the image file */
    while (bsize > 0) { 
        if (bsize > chunks)                 /* see if there is a chunk left to read */
            csize = chunks;                 /* yes, do max chunk size */
        else
            csize = bsize;                  /* no, use what is left */
        cs = fread((char *)data, 1, csize*768, fp);
        /* we have data to write */
        hc = (csize*768 + 1) & ~1;          /* make byte count even */
        /* write actual byte count to 32 bit word as header */
        n1 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), tp);
        /* write the data mod 2 */
        nw = fwrite((unsigned char *)data, 1, (size_t)hc, tp);
        /* write the byte count in 32 bit word as footer */
        n2 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), tp);
        if (n1 != sizeof(hc) || nw != hc || n2 != sizeof(hc))
        {
            fprintf(stderr, "write to %s failure\n", fnp);
            fprintf(stderr, "Operation aborted\n");
            exit(1);
        }
        bsize -= csize;                     /* do next chunk */
        memset((char *)data, 0, csize);     /* zero data storage */
    }
//printf("write file %s (size %d bytes) (%d sect) (%d blocks) (%d chunks)\n",
//    fnp, word, size, mblks, blks);
//HUH    fclose(fp);
//HUH    exit(0);
//printf("return from write file\n");
    return (1);
}

/* read program file and output to a simulated diagnostic tape */
/* mkdiagtape -c cmdfile diagtape diagtapeout */
int main(argc, argv)
int argc;
char *argv[];
{
    FILE    *dp, *fp, *fopen();
    char    **targv;
    char    *p, *cmdp;
    int     i;
    unsigned int word;                      /* just a temp word variable */         
    int goteof;                             /* end flags */
    int writing;                            /* writing output */

    memset((char *)data, 0, 4608);          /* zero data storage */

    targv = argv;                           /* save filename */
    if (argc <= 1) {                        /* see if correct # args */
        fprintf(stderr, "Usage: %s [-ptloa] [-uusername] fmgrtape file1 file2 ...\n", *argv);
        exit(1);
    }
    while(--argc > 0) {
      printf("argc %d argv %s\n", argc, *argv);
        p = *++argv;
        if (*p++ == '-') {
            if (*p == '\0') {
                fprintf(stderr, "Error: no option specified\n");
                fprintf(stderr, "Usage: %s -c cmdfile infile outfile\n", *targv);
                exit(1);
            }
            printf("doing options %s\n", p);
            while (*p != '\0') {
                switch (*p++) {
                    case 'c':
                    case 'C':
                        if (*p == '\0') {
                            p = *++argv;    /* next parameter */
                            --argc;         /* one less arg */
                        };
                        cmdp = p;           /* save ptr to file name */
                        while (*p != '\0')
                            p++;
                        break;
                    default:
                        fprintf(stderr, "Error: no cmd file specified\n");
                        fprintf(stderr, "Usage: %s -c cmdfile infile outfile\n", *targv);
                        exit(1);
                        break;
                }   /* end switch */
                continue;
            }   /* end while */
        }
        else {
            if ((dp = fopen(*argv, "r")) == NULL) {
                fprintf(stderr, "error: can't open input diag tape file %s\n", *argv);
                exit(1);
            }
            printf("opening %s file for tape\n", *argv);
            *++argv;
            break;                          /* go handle files now */
        }
        continue;
    }
    /* end while --argc */
    if ((argc-1) <= 0) {
        fprintf(stderr, "Error: incorrect number of parameters\n");
        fprintf(stderr, "Usage: %s -c cmdfile infile outfile\n", *targv);
        exit(1);
    }
    /* got input tapefile and options, handle output file now */
//printf("AT 3 argc %d argv %s\n", argc, *argv);
    if (--argc > 0) {
        p = *argv++;
        printf("argc %d argv3 %s\n", argc, p);
        if ((fp = fopen(p, "w")) == NULL) {
            fprintf(stderr, "error: can't open tape output file %s\n", p);
            fprintf(stderr, "Usage: %s -c cmdfile infile outfile\n", *targv);
            exit(1);
        }
        printf("opened output file %s\n", p);
    }

    /* now copy input tape until 1st EOF */
    goteof = 0;                             /* no eof yet */
    writing = 1;                            /* we are copying in to out */
    while (goteof == 0) {
        int n1, n2, hc, tc, n, nw;
        /* read the byte count in 32 bit word as header */
        n1 = fread((char *)(&hc), 1, (size_t)4, dp);

        /* check for EOM on tape */
        if ((n1 <= 0) || (hc & 0xffff0000)) {   /* check for garbage, assume EOM */
            fprintf(stderr, "Premature EOM on input file bad tape\n");
            exit(1);
        }
        /* see if at EOF */
        if (hc == 0) {
            /* we are at tape EOF */
            if (writing == 0)
                break;                      /* done with copy */
            writing = 0;                    /* stop writing */
            /* copy the EOF to the output tape */
            n1 = fwrite((char *)(&hc), 1, (size_t)4, fp);
            if (n1 != 4) {
                fprintf(stderr, "Error write EOF to output file\n");
                exit(1);
            }
            continue;                       /* continue processing */
        }

        /* read the data to copy to output */
        n = fread(data, 1, (size_t)hc, dp);
        if (n <= 0) {
            fprintf(stderr, "Read error on input file bad tape\n");
            exit(1);
        }

        /* if odd byte record, read extra byte and throw it away */
        if (n & 0x1) {
            n2 = fread((char *)(&tc), 1, (size_t)1, dp);
            if (n2 <= 0) {
                fprintf(stderr, "Read error on input file bad tape\n");
                exit(1);
            }
        }
        /* if writing, write out the record */
        if (writing) {
            int wc = n;                     /* get actual byte count */
            /* write actual byte count to 32 bit word as header */
            n1 = fwrite((char *)(&wc), 1, (size_t)4, fp);
            /* write the data mod 2 */
            nw = fwrite(data, 1, (size_t)hc, fp);
            if (n1 != 4 || nw != hc) {
                fprintf(stderr, "write error to tape\n");
                exit(1);
            }
        }

        /* read the byte count in 32 bit word as trailer */
        n2 = fread((char *)(&tc), 1, (size_t)4, dp);
        if (n2 <= 0) {
            fprintf(stderr, "Read error on input file bad tape\n");
            exit(1);
        }
        if (writing) {
            /* write the byte count in 32 bit word as footer */
            n2 = fwrite((char *)(&tc), 1, (size_t)4, fp);
            if (n2 != 4) {
                fprintf(stderr, "write error to tape\n");
                exit(1);
            }
        }
    }

    /* now handle new command file */
    /* we have copied the first file and bypassed the second */
    /* copy in the new com file and write out 768 byte records */
    writefile(fp, cmdp, 0, 1);                  /* write command file out in 768 byte blks */

    word = 0;
    /* copy the EOF to the output tape */
    i = fwrite((char *)(&word), 1, (size_t)4, fp);
    if (i != 4) {
        fprintf(stderr, "Error writing EOF to output file\n");
        exit(1);
    }

    while (1) {
        /* read the data to copy to output */
        i = fread(data, 1, (size_t)7680, dp);
//printf("got i = %x after fread call\n", i);
        if (i <= 0) {
            fprintf(stderr, "EOM input file, done\n");
            break;
        }
        /* write the data to output */
        i = fwrite(data, 1, (size_t)i, fp);
        if (i <= 0) {
            fprintf(stderr, "Error writing data to output file\n");
            break;
        }
    }
    fclose(dp);
    fclose(fp);
    exit(0);
}
