/*
 * filelist.c
 *
 * This program scans a metatape file and prints file count and sizes.
 * input - stdin or specified filename  
 * output - stdout
 */
#define NOTDUMP

#include <stdio.h>
#include <sys/file.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
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
#define PRINTABLE(x) ((x < 32) || (x > 126)) ? '.' : x

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
#ifdef NOTDUMP
            if (ln > 0) {
                if (count - lcount > 1)
                    fprintf(stderr, "file %d: records %d to %d: size %d\n", filen, lcount, count - 1, ln);
                else
                    fprintf(stderr, "file %d: record %d: size %d\n", filen, lcount, ln);
            }
            fprintf(stderr, "file %d: eof after %d records: %d bytes\n", filen, count, size);
#endif
            filen++;    /* set next file number */
        } else {
#ifdef NOTDUMP
            fprintf(stderr, "second eof after %d files: %d bytes\n", filen, size);
#endif
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
#ifdef NOTDUMP
        /* we have EOM */
        fprintf(stderr, "mpx eot\n");
        /* print out total tape size in bytes */
        fprintf(stderr, "total length: %ld bytes\n", tsize);
#endif
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
#ifdef NOTDUMP
        if (ln > 0) {
            if (count - lcount > 1)
                fprintf(stderr, "file %d: records %d to %d: size %d\n", filen, lcount, count - 1, ln);
            else
                fprintf(stderr, "file %d: record %d: size %d\n", filen, lcount, ln);
        }
#endif
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
    int lfilen = filen;
    unsigned int fileaddr, file_byte_count=0, curchar, buffptr, bufflen;
    int skipfile = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s infile\n", argv[0]);
        exit(1);
    } /* end of if */

    if ((inp = open(argv[1], O_RDONLY, 0666)) < 0) {
        fprintf(stderr,"%s: fopen: unable to open input file %s\n", argv[0], argv[1]);
        return (1);
    }

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
    filen = 1;
    lfilen = filen;
    buffptr = 0;
    bufflen = 16;
    fileaddr = 0;
    file_byte_count = 0;
    printf("\nfile %d:\n", filen);

    /* get lines until eof */
    while ((ll=getloi(buf, buf_size)) != EOF) {
        if (ll == 0) {
            /* eof found, process new file */
            skipfile = 0;
            file_byte_count = 0;
            fileaddr = 0;
            printf("\nfile %d:\n", filen);
        } else {
            int cc = 0;
            buffptr = 0;
            char buff[257];
            int ans;
            int w1, w2, i, j;
            char path[64], command[128];
        {
            /* dump first 2 words */
            w1 = (buf[0] & 0xff) << 24 | buf[1] << 16 | buf[2] << 8 | (buf[3] & 0xff);
            w2 = (buf[4] & 0xff) << 24 | buf[5] << 16 | buf[6] << 8 | (buf[7] & 0xff);
            if (filen > 480)
            printf("w1 = %x, w2 = %d count = %d\n", w1, w2, count);
            if (count == 1 && w1 == 1) {
                char file[20], dir[20], vol[20];
                int off = 8;
                int l = 0;
                /* we have directory entries */
                for (j=0; j<w2; j++) {
                    int k = l++ * 48;
                    if (k > (6144-48-off)) {
                        ll=getloi(buf, buf_size);
                        off = 0;
                        l = 0;
                        k = 0;
                        printf("reread: got ll= %d\n", ll);
                        for (i=0; i<16; i++) {
                            file[i] = tolower(buf[k+off+i]);
                            if (file[i] == ' ')
                                file[i] = '\0';
                        }
                        file[16] = '\0';
                        printf("file %s\n", file);
                    }
                    for (i=0; i<16; i++) {
                        file[i] = tolower(buf[k+off+i]);
                        if (file[i] == ' ')
                            file[i] = '\0';
                    }
                    file[16] = '\0';
                    for (i=0; i<16; i++) {
                        dir[i] = tolower(buf[k+off+16+i]);
                        if (dir[i] == ' ')
                            dir[i] = '\0';
                    }
                    dir[16] = '\0';
                    for (i=0; i<16; i++) {
                        vol[i] = tolower(buf[k+off+32+i]);
                        if (vol[i] == ' ')
                            vol[i] = '\0';
                    }
                    vol[16] = '\0';
                    sprintf(path, "./%s/%s", vol, dir);
                    /* create the directory/file */
//                  sprintf(command, "mkdir -p %s", path);
//                  system(command);
                    sprintf(path, "./%s/%s/%s", vol, dir, file);
                    printf("path %s\n", path);
                    sprintf(command, "touch %s", path);
//                  system(command);
                }
            } else
            if (count == 1 && w1 == 2 && w2 == 0) {
                char file[20], dir[20], vol[20];
                /* process file definition */
                /* we have a file definition entry */
                for (i=0; i<16; i++) {
                    file[i] = tolower(buf[8+i]);
                    if (file[i] == ' ')
                        file[i] = '\0';
                }
                file[16] = '\0';
                for (i=0; i<16; i++) {
                    dir[i] = tolower(buf[24+i]);
                    if (dir[i] == ' ')
                        dir[i] = '\0';
                }
                dir[16] = '\0';
                for (i=0; i<16; i++) {
                    vol[i] = tolower(buf[40+i]);
                    if (vol[i] == ' ')
                        vol[i] = '\0';
                }
                vol[16] = '\0';
                sprintf(path, "./%s/%s/%s", vol, dir, file);
                printf("path2 = %s\n", path);
            }
        }
            /* see if skipping to next file */
            if (skipfile == 1) {
                continue;
            }

#ifdef NODUMP
            /* process the returned buffer */
            while (cc < ll) {
                curchar = (unsigned int)buf[cc++] & 0xff;
                file_byte_count++;
                if (!buffptr)
                    printf(" %06x : ",fileaddr);
                printf("%02x", curchar & 0xff);
                buff[buffptr++] = PRINTABLE(curchar);
                if (!(buffptr % 4))
                    printf(" ");
                if (buffptr >= bufflen) {
                    buff[buffptr] = 0;
                    printf(" |%s|\n",buff);
                    buffptr = 0;
                    fileaddr += bufflen;
                    if (!(file_byte_count % 256)) {
                        printf("\n<cr> - continue, q = quit, s = skip > ");
                        ans = getchar();
                        if (ans == 'q') {
                            close(inp);
                            free(buf);
                            exit(1);
                        }
                        if (ans == 's')
                            skipfile = 1;
                        if (ans != '\n')
                            while ((ans=getchar()) != '\n' )
                                ;
                    } /* end of if */
                    if (skipfile == 1)
                        break;
                } /* end of if */
            } /* end of while */
#endif

#ifdef NODUMP
            if (buffptr && !skipfile) {
                buff[buffptr] = 0;
                while (buffptr++ < bufflen) {
                    printf("  ");
                    if (!(buffptr % 4))
                        printf(" ");
                } /* end of while */
                printf(" |%s|\n",buff);

                /* see what user wants to do */
                printf("\n<cr> - continue, q = quit > ");
                ans = getchar();
                if (ans == 'q') {
                    close(inp);
                    free(buf);
                    exit(1);
                }
                if (ans != '\n')
                    while ((ans=getchar()) != '\n' )
                        ;
            } /* end of if */
#endif
        }
    }
    close(inp);
    free(buf);
    exit(0);
}
