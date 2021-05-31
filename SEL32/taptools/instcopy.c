/*
 * instcopy.c
 *
 * This program scans an ISC install metatape file and prints file count and
 * sizes.  It is only for special ISC install save images.  The first set of
 * files on the tape are in volmgr format and the second set of files on the
 * tape are in filemgr format.  The files in filemgr format are prefixed with
 * ./fm/ before being saved.  Volmgr save images have the following format:
 * One or more 6144 byte records containing a list of files saved 16 char file
 * name followed my 16 char directory name followed by 16 char volume name.
 * A 1536 byte file definition entry will be followed by 1 to 8 768 byte
 * file data records followed by an EOF for each file.  If the file size is
 * greater than 6144 bytes, 1 or more 6144 byte records are output followed
 * by last record modulo 768 bytes.  Filemgr SMD entries are 8 words, and are
 * in 1152 words (4608 bytes) (6 sector) blocks.  Saved data files are modulo
 * 1152 words also; 8 wds per SMD entry, 24 entries per sector, 144 entries
 * per 6 sector block.  Three EOFs in a row define the EOT.
 * input - stdin or specified filename  
 * output - stdout
 */

#include <stdio.h>
#include <stdlib.h>
//#include <sys/file.h>
//#include <unistd.h>
//#include <string.h>
#include <ctype.h>
//#include <errno.h>
#include <time.h>

int filen = 1;
int EOFcnt = 0;
int count=0, lcount=0;
int size=0, tsize=0;
int size_512K = 512 * 1024;
int ln;
//int inp, outp;
FILE *infp, *outfp;
#define PRINTABLE(x) ((x < 32) || (x > 126)) ? '.' : x

/* get a line of input. */
int getloi(char *s, int lim)
{
//  int c, i;
    int n1, n2, hc, tc, n;

//  errno = 0;
    /* read the byte count in 32 bit word as header */
//  n1 = read(inp, (char *)(&hc), (size_t)4);
    n1 = fread((char *)(&hc), (size_t)1, (size_t)4, infp);
    if (n1 <= 0)
        hc = -1;        /* at EOM on disk file */

    /* check for EOF & EOM on tape data */
    if (hc & 0xffff0000)    /* check for garbage, assume EOM */
        hc = -1;        /* at EOM on disk file */

    /* check for EOF & EOM on tape data */
    if (hc == 0) {
        /* we are at tape EOF */
        if (++EOFcnt < 2) {     /* if 1st EOF, print file info */
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
//  n = read(inp, s, (size_t)hc);
    n = fread(s, (size_t)1, (size_t)hc, infp);

    /* if odd byte record, read extra byte and throw it away */
    if (n & 0x1) {
//      n2 = read(inp, (char *)(&tc), (size_t)1);
        n2 = fread((char *)(&tc), (size_t)1, (size_t)1, infp);
        if (n2 <= 0)
            return -1;          /* at EOM on disk file */
    }

    /* read the byte count in 32 bit word as trailer */
//  n2 = read(inp, (char *)(&tc), (size_t)4);
    n2 = fread((char *)(&tc), (size_t)1, (size_t)4, infp);
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
    unsigned char *buf;
    size_t size_512K = 512 * 1024;
    size_t buf_size = 512 * 1024;
//  char *cp, *np;
    int ll;
    int lfilen = filen;
    unsigned int mpxtime[2];
    char path[64], command[128], dtime[64];
    time_t utime=0;
    struct tm *dt;

    if (argc != 2) {
        fprintf(stderr, "usage: %s infile\n", argv[0]);
        exit(1);
    } /* end of if */

//  if ((inp = open(argv[1], O_RDONLY, 0666)) < 0) {
    if ((infp = fopen(argv[1], "r")) == NULL) {
        fprintf(stderr,"%s: fopen: unable to open input file %s\n", argv[0], argv[1]);
        return (1);
    }
//  outp = -1;
    outfp = NULL;

    /* get a 512k buffer */
    if ((buf = malloc(buf_size)) == NULL) {
        fprintf(stderr, "Can't allocate memory for tscan\n");
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
    printf("\nfile %d:\n", filen);

    /* get lines until eof */
    while ((ll=getloi(buf, buf_size)) != EOF) {
//      printf("got ll = %d filen %d count %d size %d\n", ll, filen, count, size);
        if (ll == 0) {
            /* eof found, process new file */
            printf("\nfile %d:\n", filen);
        } else
        if ((ll == 4608) && (count == 1)) {
            int smd = 1;
            /* see if ISC install tape with filemgr records */
            /* ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
            /* filemgr smd entries are 8 words, and are in 1152 words (4608 bytes)
             * (6 sector) blocks.  Saved data files are modulo 1152 words also */
            /* 8 wds per SMD entry, 24 entries per sector, 144 entries per 6 sector
             * block. */
//      printf("gotx ll = %d filen %d count %d size %d\n", ll, filen, count, size);
            if (smd == 1) {
            /* check for SMD entry instead of SDT entry */
                /* read smd entry */
                char file[20], dir[20];
                int j, m, i, l = 0;
                int smddone = 0;
                int totent = 0;
                char *buf2 = buf;
//              printf("\nfile %d: %d\n", filen, ll);
                /* see how many entries here */
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
#if 0
                        // see if active file
                        if ((buf[k+12] & 0x80) == 0)
                            // not active goon
                            printf("inactive file: w1 = %d path = %s\n", w1, path);
                        else
                            printf("active file: w1 = %d path = %s\n", w1, path);
#else
                            printf("blks: %d file: path = %s\n", w1, path);
#endif
                    }
                    if (smddone)
                        break;
                    buf2 += 4608;       /* next buffer */
                    ll=getloi(buf2, 4608);
                }
//              printf("%d smd entries found\n", totent);
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
//                      sprintf(dir, "%s", "system");
                        sprintf(dir, "%s", "./fm/system");
//                  sprintf(path, "./%s", dir);
                    sprintf(path, "./fm/%s", dir);

#if 0
                    // see if active file
                    if ((buf[k+12] & 0x80) == 0) {
                        // not active goon
                        printf("inactive file: w1 = %d\n", w1);
                    }
                    printf("active file: w1 = %d\n", w1);
#endif
                    if (w1 <= 0)
                        break;
                    /* create the directory/file */
//                  printf("path = %s\n", path);
                    sprintf(command, "mkdir -p %s", path);
//                  printf("command = %s\n", command);
                    system(command);
//                  sprintf(path, "./%s/%s", dir, file);
                    sprintf(path, "./fm/%s/%s", dir, file);
                    printf("entry %d blks %d = %s\n", j+1, w1, path);
                    sprintf(command, "touch %s", path);
//                  printf("command = %s\n", command);
                    system(command);

//                  printf("blks: %d file: path = %s\n", w1, path);

//                  if (outp >= 0)
                    if (outfp != NULL)
//                      close(outp);
                        fclose(outfp);
//                  outp = -1;
                    outfp = NULL;

                    /* open output file, create it if necessary */
//                  if ((outp = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0) {
                    if ((outfp = fopen(path, "w")) == NULL) {
                        //fprintf(stderr, "Can't open %s\n", path);
                        printf("Can't open %s\n", path);
//                      close(inp);
                        fclose(infp);
                        free(buf);
                        return (3);
                    }

                    /* get the file data */
                    for (m=0; m<((w1+5)/6); m++) {
                        char data[5000];        /* data buffer */
                        ll=getloi(data, 4608);

                        /* process file data for file */
                        if (ll == 4608) {
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
//                              no = write(outp, data, bcnt);
                                no = fwrite(data, (size_t)1, (size_t)bcnt, outfp);
                                if (no != bcnt)
                                    //fprintf(stderr, "write (%d) != read (%d) on file %s\n", no, bcnt, path);
                                    printf("write (%d) != read (%d) on file %s\n", no, bcnt, path);
                            }
                        } else {
                            printf("Bad file size read! %d instead of 4608\n", ll);
///                         if (ll == -1) break;
                            if (ll == -1)
                                goto dostop;
                        }
                    } /* end writing file */
                } /* end of smd scan */
            } /* read smd entries 4608 byte records */
//      } /* process read of smd or sdt */
            /* ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
        } else {
            int cc = 0;
//          unsigned int curchar;

            /* dump first 2 words */
            int w1, w2, i, j;
            w1 = (buf[0] & 0xff) << 24 | buf[1] << 16 | buf[2] << 8 | (buf[3] & 0xff);
            w2 = (buf[4] & 0xff) << 24 | buf[5] << 16 | buf[6] << 8 | (buf[7] & 0xff);
//   printf("w1 = %x, w2 = %d count = %d\n", w1, w2, count);
            if (count == 1 && w1 == 1) {
                char file[20], dir[20], vol[20];
                int off = 8;
                int l = 0;
printf("Directory with %d entries\n", w2);
                /* we have directory entries */
                for (j=0; j<w2; j++) {
                    int k = l++ * 48;
                    if (k > (6144-48-off)) {
                        ll=getloi(buf, buf_size);
                        off = 0;
                        l = 0;
                        k = 0;
//   printf("reread: got ll= %d\n", ll);
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
#if  0
                    /* create the directory/file */
                    sprintf(command, "mkdir -p %s", path);
                    system(command);
#endif
                    sprintf(path, "./%s/%s/%s", vol, dir, file);
#if 0
                    sprintf(command, "touch %s", path);
                    system(command);
#endif
//   printf("path0 = %s\n", path);
                }
            } else
            if (count == 1 && w1 == 2 && w2 == 0) {
                char file[20], dir[20], vol[20];
//              if (outp >= 0)
                if (outfp != NULL) {
//                  close(outp);
                    fclose(outfp);
                    sprintf(command, "touch -a -m -t %s %s", dtime, path);
//                  sprintf(command, "touch -t %s %s", dtime, path);
// printf("1cmd [%s] path[%s]\n", command, path);
// fflush(stdout);
                    system(command);
                }
//              outp = -1;
                outfp = NULL;
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

                i = 0x300 + 0x38;       /* last modified date */
//              i = 0x300 + 0x20;       /* creation date */
                /* get last modification time for file */
                /* day number since 1/1/1960 */
// printf("D %02x%02x%02x%02x M %02x%02x%02x%02x\n",
//     buf[i+0],buf[i+1],buf[i+2],buf[i+3],buf[i+4],buf[i+5],buf[i+6],buf[i+7]);
                mpxtime[0] = buf[i+0] << 24 | buf[i+1] << 16 | buf[i+2] << 8 | (buf[i+3] & 0xff);
                /* .1ms count in day 0-864,000,000 */
                mpxtime[1] = (buf[i+4] << 24) | (buf[i+5] << 16) | (buf[i+6] << 8) | (buf[i+7] & 0xff);
                /* get UNIX time, secs sense 1970, not 1960 */
                utime =(time_t)((mpxtime[0]-3653)*86400)+(time_t)(mpxtime[1]/10000);
                dt = gmtime(&utime);
                j = dt->tm_year+1900;
                if (j > 2037)
                    j -= 100;
                sprintf(dtime, "%04d%02d%02d%02d%02d", j, dt->tm_mon+1,
                    dt->tm_mday, dt->tm_hour, dt->tm_min);
// printf("mpx day %x ms %x utime %ld dtime %s\n", mpxtime[0], mpxtime[1], utime, dtime);
// fflush(stdout);

                sprintf(path, "./%s/%s", vol, dir);
                /* create the directory/file */
//              printf("dir path = %s\n", path);
                sprintf(command, "mkdir -p %s", path);
                system(command);
                sprintf(path, "./%s/%s/%s", vol, dir, file);
                printf("path = %s\n", path);

                /* open output file, create it if necessary */
//              if ((outp = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0) {
                if ((outfp = fopen(path, "w")) == NULL) {
                    (void)fprintf(stderr, "Can't open %s\n", path);
//                  close(inp);
                    fclose(infp);
                    free(buf);
                    return (3);
                }
                /* process file data for file */
                if (ll > 1536) {
//                  int no = write(outp, buf+1536, ll-1536);
                    int no = fwrite(buf+1536, (size_t)1, (size_t)(ll-1536), outfp);
                    if (no != ll)
                        fprintf(stderr, "write (%d) != read (%d) on file %s\n", no, ll, path);
                }
            } else
            if (count > 1) {
                /* process file data for file */
//              int no = write(outp, buf, ll);
                int no = fwrite(buf, (size_t)1, (size_t)ll, outfp);
                if (no != ll)
                    fprintf(stderr, "write (%d) != read (%d) on file %s\n", no, ll, path);
            }
        } /* end if */
    } /* end while */
dostop:
//  close(inp);
    fclose(infp);
    free(buf);
//  if (outp >= 0)
    if (outfp != NULL) {
//      close(outp);
        fclose(outfp);
        sprintf(command, "touch -a -m -t %s %s", dtime, path);
//      sprintf(command, "touch -t %s %s", dtime, path);
// printf("2cmd [%s]\n", command);
// fflush(stdout);
        system(command);
    }
    exit(0);
}
