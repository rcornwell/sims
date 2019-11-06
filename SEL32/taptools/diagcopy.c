/*
 * diagcopy.c
 *
 * This program scans a diag metatape file and prints file count and
 * sizes.  In addition, it creates a boot directory for the boot code,
 * a cmd directory for the cmd file, and a diag directory with a file
 * for each defined diagnostic.  The file must be a bootable diagnostic
 * tape in .tap format.  The output files are non .tap format, i.e. just
 * binary output of each file.
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
int diag = 0;
int EOFcnt = 0;
int count=0, lcount=0;
int size=0, tsize=0;
int size_512K = 512 * 1024;
int ln;
int inp, outp;
int smd = 0;
#define PRINTABLE(x) ((x < 32) || (x > 126)) ? '.' : x

/* get a line of input. */
int getloi(char *s, int lim)
{
    int c, i;
    int32_t n1, n2, hc, tc, n;

    errno = 0;
    /* read the byte count in 32 bit word as header */
    n1 = read(inp, (char *)(&hc), (size_t)sizeof(hc));
    if (n1 <= 0)
        hc = -1;        /* at EOM on disk file */

    /* check for EOF & EOM on tape data */
    if (hc & 0xffff0000)    /* check for garbage, assume EOM */
        hc = -1;        /* at EOM on disk file */

    /* check for EOF & EOM on tape data */
    if (hc == 0) {
        /* we are at tape EOF */
        if (++EOFcnt < 2) {     /* if 1st EOF, print file info */
//          printf("EOF found after file %d\n", filen);
            lfilen = filen;
            filen++;    /* set next file number */
            smd = 1;    /* look for smd again */
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
        printf("EOM found after file %d\n", filen);
        return -1;      /* at EOM on disk file */
    }

    /* read the data */
    n = read(inp, s, (size_t)hc);
    /* read the byte count in 32 bit word as trailer */
    n2 = read(inp, (char *)(&tc), (size_t)sizeof(tc));
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
    int boot = 0;
    int dol = 0;
    int sdt = 0;

    if (argc != 2) {
        printf("usage: %s infile\n", argv[0]);
        exit(1);
    } /* end of if */

    if ((inp = open(argv[1], O_RDONLY, 0666)) < 0) {
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
        printf("got ll = %d filen %d\n", ll, filen);
        if (ll == 204) {
            diag = 1;           /* process diag boot record */
            printf("process diag ll = %d\n", ll);
        }
        if ((dol == 1) || ((ll == 7680) && (diag))) {
            dol = 1;            /* process DOL record */
            printf("process dol ll = %d\n", ll);
        }

        if (ll == 0) {
            /* eof found, process new file */
            printf("\nfile complete %d:\n", filen);
            break;
            //continue;        /* go look for another SDT bootfile or SMD entry */
        } else
        if (ll == 1920) {
            printf("File is a MPX SDT tape and can not be processed!\n");
            break;
        } else
        if (ll == 4608) {
            printf("File is a MPX filemgr save tape and can not be processed!\n");
            break;
        } else
        if ((diag == 0) && (ll != 204)) {
            printf("File type is unknown and can not be processed!\n");
            break;
        } else {
            int cc = 0;
            unsigned int curchar;
            char filename[16];
            int i, j, m;

            /* A diag tape has a 204 byte boot loader in the first record. */
            /* It is followed by multiple 7680 byte records of the diagnostic */
            /* overlay loader (DOL) */

            if (diag == 1) {
                int no, ct = 0;;
                /* get more diag data */
                /* open output file, create it if necessary */
                if (outp == -1) {
                    sprintf(filename, "bootfile\0");
                    if ((outp = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0) {
                        printf("Can't open bootfile\n");
                        close(inp);
                        free(buf);
                        return (3);
                    }
                }
        printf("got2 ll = %d writing to bootfile -> %s\n", ll, filename);
                no = write(outp, buf, ll);
                if (no != ll)
                    printf("write (%d) != read (%d) on file bootfile\n", no, ll);
                close(outp);
                outp = -1;

                /* now put the rest of the code in another file */
                sprintf(filename, "dolfile");
                if ((outp = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0) {
                    printf("Can't open dolfile\n");
                    close(inp);
                    free(buf);
                    return (3);
                }
                /* get lines until eof */
                while ((ll=getloi(buf, buf_size)) > 0) {
                    ct++;
                    no = write(outp, buf, ll);
                    if (no != ll)
                        printf("write (%d) != read (%d) on file dolfile\n", no, ll);
                }
                diag = 2;
                close(outp);
                outp = -1;
        printf("wrote %d records to dolfile %s\n", ct, filename);
                continue;        /* go look for diag command file */
            } else
            if (diag == 2) {
                /* read the command file in and write to cmdfile */
                int no, ct = 0;;
                /* get more data */
                /* open output file, create it if necessary */
                if (outp == -1) {
                    sprintf(filename, "cmdfile\0");
                    if ((outp = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0) {
                        printf("Can't open cmdfile\n");
                        close(inp);
                        free(buf);
                        return (3);
                    }
                }
        printf("got2 ll = %d writing to cmdfile -> %s\n", ll, filename);
                no = write(outp, buf, ll);
                if (no != ll)
                    printf("write (%d) != read (%d) on file cmdfile\n", no, ll);

                /* now put the rest of the code in the file */
                /* get lines until eof */
                while ((ll=getloi(buf, buf_size)) > 0) {
                    ct++;
                    no = write(outp, buf, ll);
                    if (no != ll)
                        printf("write (%d) != read (%d) on file cmdfile\n", no, ll);
                }
                diag = 3;
                close(outp);
                outp = -1;
        printf("wrote %d records to cmdfile %s\n", ct, filename);
                continue;        /* go look for diag command file */
            } else
            if (diag == 3) {
                /* read the diag file in and write to diagfile */
                int no, ct = 0;;
                /* get more data */
                /* open output file, create it if necessary */
                if (outp == -1) {
                    sprintf(filename, "diagfile%02d\0", filen);
                    if ((outp = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0) {
                        printf("Can't open %s\n", filename);
                        close(inp);
                        free(buf);
                        return (3);
                    }
                }
        printf("got2 ll = %d writing to %s\n", ll, filename);
                no = write(outp, buf, ll);
                if (no != ll)
                    printf("write (%d) != read (%d) on file %s\n", no, ll, filename);

                /* now put the rest of the code in the file */
                /* get lines until eof */
                while ((ll=getloi(buf, buf_size)) > 0) {
                    ct++;
                    no = write(outp, buf, ll);
                    if (no != ll)
                        printf("write (%d) != read (%d) on file %s\n", no, ll, filename);
                }
                close(outp);
                outp = -1;
        printf("wrote %d records to cmdfile %s\n", ct, filename);
                continue;        /* go look for diag command file */
            } /* read cmd file records */
        } /* process read of tape */
    } /* end of getloi read */
dostop:
    close(inp);
    free(buf);
    if (outp >= 0)
        close(outp);
    exit(0);
}
