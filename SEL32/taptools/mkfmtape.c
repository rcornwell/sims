/*
 * mkfmtape.c
 *
 * This program reads MPX files and stores it into a simulated
 * filemgr save tape.  The tape may then be restored to a MPX
 * running system using the filemgr.
 * new file entry.
 * intput - simulated fmgrtape filename, filename, etc.
 * output - simulated fmgrtape, file list to stdout
 * options - -p = file type 0xca for programs
 *         - -t = ascii text file 0xee
 *         - -l = library/directory file 0xff
 *         - -o = other 0x00
 *         - -a = append entries to current file
 *         - -u = username (directory)
 * 11/26/2018
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#define BLKSIZE 768                 /* MPX file sector size */
u_int32_t dir[1152];                /* room for 144 8w smd entries */
unsigned char data[4608];           /* room for 6*768=(4608) 768 byte sectors per 4608 byte block */
unsigned char bigdata[19200];       /* room for 6*768=(4608) 768 byte sectors per 4608 byte block */

/* read program file and output to a simulated mpx1.x filemgr savetape */
/* mkfmtape -ltoa fmgrtape, filename, filename, ... */
int main(argc, argv)
int argc;
char *argv[];
{
    FILE    *fp, *dp, *fopen();
    int     targc;
    char    **targv;
    unsigned char username[8];
    char *p;
    int i;
#define DOPROG  1
#define DOADD  2
#define DOOTHER  4
#define DOTEXT  8
#define DOLIB  16
#define DOUSER  32
    unsigned int option = DOTEXT;           /* what to do */
    unsigned char *fnp;                     /* file name pointer */
    unsigned int size;                      /* size in 768 byte sectors */
    unsigned int word;                      /* just a temp word variable */         
    unsigned char name[9];                  /* LM name */
    unsigned int typ;                       /* file type requested by user */
    char *userp = username;                 /* pointer to username */
    int ofd;                                /* output file number */
    int32_t filen;                          /* file number */
    u_int32_t *dirp;                        /* directory entry pointer */
    int32_t totent;                         /* total smd entries */

    memset((char *)dir, 0, 4608);           /* zero smd storage */
    memset((char *)data, 0, 4608);          /* zero data storage */
    for (i=0; i<8; i++)
        username[i] = 0;                    /* use zero for system username */
    typ = 0xee000000;                       /* set type */
    if (argc <= 1) {                        /* see if correct # args */
        fprintf(stderr, "Usage: %s [-ptloa] [-uusername] fmgrtape file1 file2 ...\n", *argv);
        exit(1);
    }
    while(--argc > 0) {
//      printf("argc %d argv %s\n", argc, *argv);
        p = *++argv;
        if (*p++ == '-') {
            if (*p == '\0') {
                fprintf(stderr, "Error: no option specified\n");
                fprintf(stderr, "Usage: %s [-ptloa] [-uusername] fmgrtape file1 file2 ...\n", *argv);
                exit(1);
            }
//          printf("doing options %s\n", p);
            while (*p != '\0') {
                switch (*p++) {
                    case 'p':
                    case 'P':
                        option |= DOPROG;   /* save program modules */
                        typ = 0xca;         /* set type */
                        break;
                    case 'A':
                    case 'a':
                        option |= DOADD;    /* append to save tape */
                        break;
                    case 'O':
                    case 'o':
                        option |= DOOTHER;  /* other type files */
                        typ = 0x00;         /* set type */
                        break;
                    case 'T':
                    case 't':
                        option |= DOTEXT;   /* save text files */
                        typ = 0xee;         /* set type */
                        break;
                    case 'L':
                    case 'l':
                        option |= DOLIB;    /* save library files */
                        typ = 0xff;         /* set type */
                        break;
                    case 'U':
                    case 'u':
                        option |= DOUSER;   /* save username for files */
                        if (*p == '\0')
                        {
                            p = *++argv;    /* next parameter */
                            --argc;         /* one less arg */
                        };
                        userp = p;
                        while (*p != '\0')
                            p++;
                        break;
                    default:
                        fprintf(stderr, "Error: no option specified\n");
                        fprintf(stderr, "Usage: %s [-ptloa] [-uusername] fmgrtape file1 file2 ...\n", *argv);
                        exit(1);
                        break;
                }   /* end switch */
                continue;
            }   /* end while */
        }
        else {
//          printf("option set to %x\n", option);
            if (option & DOADD) {
                long bytes;
                /* open read/write */
                if ((dp = fopen(*argv, "r+")) == NULL) {
                    /* file not there, create one by opening w+ */
                    if ((dp = fopen(*argv, "w")) == NULL) {
                        fprintf(stderr, "error: can't create/open simulated tape disk file %s\n", *argv);
                        exit(1);
                    }
//                  printf("1 opened output in w mode, write at start\n");
                }
//              else
//                  printf("2 opened output in r+ mode write at end\n");

                fseek(dp, 0, SEEK_END);             /* seek to end */
                bytes = ftell(dp);                  /* get filesize in bytes */
                printf("file length %ld bytes\n", bytes);
                printf("start writing at %ld bytes offset\n", bytes-8);
                fseek(dp, 0, SEEK_SET);             /* rewind file to beginning */
                if (bytes > 8) {                    /* see if file written to already */
                    /* we need to find the EOT */
                    int32_t n1, n2, hc, tc, n;
                    int EOFcnt = 0;
readmore:
                    n1 = fread((char *)(&hc), 1, (size_t)4, dp);    /* read 4 byte record size */
                    if (n1 <=0)                     /* check for read error */
                        goto doabort;               /* bad tape format */

                    if (hc & 0xffff0000)            /* check for garbage */
                        hc = 0;                     /* assume EOF on disk */

                    if (hc == 0) {                  /* check for EOF on file */
                        /* EOF found */
                        if (++EOFcnt == 2) {
                            /* we have second EOF, we need to backup 4 bytes */
backup4:
                            bytes = ftell(dp);      /* get file position in bytes */
                            fseek(dp, bytes-4, SEEK_SET);   /* backspace over 2nd EOF */
                            goto getout;            /* start our processing */
                        }
                        /* we have first EOF, keep reading */
                        goto readmore;              /* read more records */
                    } else
                    if (hc == -1) {                 /* check for EOM */
                        if (EOFcnt == 1)
                            /* see if one EOF followed by EOM (-1) */
                            goto backup4;           /* start write over the EOM */
                        /* we have an EOM without any EOF, so bad tape */
                        goto doabort;               /* bad tape format */
                    }

                    /* we have data, so no EOF */
                    EOFcnt = 0;                     /* reset EOF count */

                    /* read the data */
                    tc = hc;                        /* save record size */
                    n = fread(bigdata, 1, (size_t)hc, dp);  /* read in record size */
                    if (n <= 0) {                   /* check for read error */
                        goto doabort;               /* bad tape format */
                    }
                    n2 = fread((char *)(&hc), 1, (size_t)4, dp);    /* read 4 byte record size */
                    if (n2 <= 0) {                  /* check for read error */
doabort:
                        /* error, abort the operation */
                        fprintf(stderr, "error: formatting error on simulated tape disk file %s\n", *argv);
                        exit(1);
                    }
                    /* verify counts & sizes */
                    if ((tc != hc) || (hc != n)) 
                        goto doabort;               /* bad tape format */
                    goto readmore;                  /* read more records */
                }
            }
            else {
                if ((dp = fopen(*argv, "w")) == NULL) {
                    fprintf(stderr, "error: can't create/open simulated tape disk file %s\n", *argv);
                    exit(1);
                }
//              printf("3 opened output in w mode, write at start\n");
            }
getout:
//          printf("opening %s file for tape\n", *argv);
            *++argv;
            break;      /* go handle files now */
        }
        continue;
    }
    /* end while --argc */
    if ((argc-1) <= 0) {
        fprintf(stderr, "Error: incorrect number of parameters\n");
        fprintf(stderr, "Usage: %s [-ptloa] [-uusername] fmgrtape, file1 file2 ...\n", *argv);
        exit(1);
    }
    /* got tapefile and options, handle files now */
    targc = argc;                               /* save argc to reread list */
    targv = argv;                               /* save argv to reread list */
//  printf("AT 3 argc %d argv %s\n", argc, *argv);
    filen = 0;                                  /* no files yet */
    totent = 0;                                 /* no files yet */
    /* populate the 32 byte SMD entry */
    dirp = (u_int32_t *)dir;                    /* get word pointer for smd data */
    while (--argc > 0) {
        u_int32_t smd[8];                       /* smd entry data */
        int blks;

        for (i=0; i<8; i++)                     /* zero smd entry */
            smd[i] = 0;
        p = *argv++;
        i = strlen(p);                          /* filename size */
        if (i == 0 || i > 8) {
            fprintf(stderr, "error: Filename too long (%d>8) %s, Aborting\n", i, p);
            exit(1);
        }
//      printf("argc %d argv3 %s\n", argc, p);
        if ((fp = fopen(p, "r")) == NULL) {
            fprintf(stderr, "error: can't open user file %s\n", p);
            exit(1);
        }
        fnp = p;                                /* get file name pointer */
        fseek(fp, 0, SEEK_END);                 /* seek to end */
        word = ftell(fp);                       /* get filesize in bytes */
        fseek(fp, 0, SEEK_SET);                 /* rewind file */
        size = (word/768);                      /* filesize in sectors */
        if (word%768 != 0)                      /* see if byte left over */
            size += 1;                          /* partial sector, add 1 */
        blks = (word/4608);                     /* blocks */
        if ((word%4608) != 0)
            blks++; 
        printf("write SMD %s user %s size %d bytes %d sect %d blocks\n",
            fnp, userp, word, size, blks);
        fclose(fp);
        /* create smd entry for this file */
        memset(name, ' ', 8);                   /* blank filename */
        for (i=0; i<8; i++) {
            if (p[i] == '\0')                   /* check for null termination char */
                break;
            name[i] = toupper(p[i]);            /* uppercase filename */
        }
        /* populate the 32 byte SMD entry */
        /* word 1 and 2 has filename */
        smd[0] = name[3] << 24 | name[2] << 16 | name[1] << 8 | name[0];
        smd[1] = name[7] << 24 | name[6] << 16 | name[5] << 8 | name[4];
        /* type and smd loc in wd 2  */
        smd[2] = typ;                           /* save the type of file */
        /* size now has load module size in sectors */
        size = 0x80000000 | (blks * 6);         /* file flags and size */
        smd[3] = (size & 0xff) << 24 | (size & 0xff00) << 8 |
            (size & 0xff0000) >> 8 | (size & 0xff000000) >> 24;
        memset(name, ' ', 8);                   /* blank username */
        for (i=0; i<8; i++) {
            if (userp[i] == '\0')               /* check for null termination char */
                break;
            name[i] = toupper(userp[i]);        /* uppercase username */
        }
        /* set username */
        smd[4] = name[3] << 24 | name[2] << 16 | name[1] << 8 | name[0];
        smd[5] = name[7] << 24 | name[6] << 16 | name[5] << 8 | name[4];
        if ((smd[4] == 0x20202020 && smd[5] == 0x20202020) ||
            (smd[4] == 0 & smd[5] == 0)) {
            smd[4] = smd[5] = 0;                /* use null for system */
        }
        smd[6] = 0x00080000;                    /* no password or udt index */
        smd[7] = 0x00000080;                    /* fmgr has 0x80000000 in it so I will too */
        for (i=0; i<8; i++)
            *dirp++ = smd[i];                   /* save smd entry */
        filen++;                                /* bump the file count */
        totent++;                               /* bump total count */
        if (filen == 144) {                     /* see if entry is full */
            /* we need to write out the directory entries */
            int32_t n1, n2, nw;
            /* we have data to write */
            int32_t hc = (4608 + 1) & ~1;       /* make byte count even */
            /* write actual byte count to 32 bit word as header */
            n1 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), dp);
            /* write the data mod 2 */
            nw = fwrite((unsigned char *)dir, 1, (size_t)hc, dp);
            /* write the byte count in 32 bit word as footer */
            n2 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), dp);
            if (n1 != sizeof(hc) || nw != hc || n2 != sizeof(hc))
            {
                fprintf(stderr, "write (%d) failure\n", nw);
                fprintf(stderr, "Operation aborted\n");
                exit(1);
            }
            memset((char *)dir, 0, 4608);       /* zero smd storage */
            filen = 0;                          /* restart count */
            dirp = (u_int32_t *)dir;            /* get word pointer for smd data */
        }
    }
    /* write out the directory entries for the files to save */
    if (filen != 0) {
        /* we need to write out the directory entries */
        int32_t n1, n2, nw;
        /* we have data to write */
        int32_t hc = (4608 + 1) & ~1;           /* make byte count even */
        /* write actual byte count to 32 bit word as header */
        n1 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), dp);
        /* write the data mod 2 */
        nw = fwrite((unsigned char *)dir, 1, (size_t)hc, dp);
        /* write the byte count in 32 bit word as footer */
        n2 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), dp);
        if (n1 != sizeof(hc) || nw != hc || n2 != sizeof(hc))
        {
            fprintf(stderr, "write (%d) failure\n", nw);
            fprintf(stderr, "Operation aborted\n");
            exit(1);
        }
        memset(dir, 0, 4608);                   /* zero smd storage */
        filen = 0;                              /* restart count */
    }
    /* totcnt has total number of files to output */
    memset((char *)data, 0, 4608);              /* zero data storage */
    argc = targc;                               /* restore argc for reread */
    argv = targv;                               /* restore argv for reread */
    /* read each file and output to save tape file in 6 sector blocks */
    while (--argc > 0) {
        int blks;

        p = *argv++;
//      printf("argc %d argv3 %s\n", argc, p);
        if ((fp = fopen(p, "r")) == NULL) {
            fprintf(stderr, "error: can't open user file %s\n", p);
            exit(1);
        }
        fnp = p;                                /* get file name pointer */
        fseek(fp, 0, SEEK_END);                 /* seek to end */
        word = ftell(fp);                       /* get filesize in bytes */
        fseek(fp, 0, SEEK_SET);                 /* rewind file */
        size = (word/768);                      /* filesize in sectors */
        if (word%768 != 0)                      /* see if byte left over */
            size += 1;                          /* partial sector, add 1 */
        blks = (word/4608);                     /* blocks */
        if ((word%4608) != 0)
            blks++; 
//      rewind(fp);                             /* back to beginning */
        while (fread((char *)data, 1, 4608, fp) > 0) {
            int32_t n1, n2, nw;
            /* we have data to write */
            int32_t hc = (4608 + 1) & ~1;       /* make byte count even */
            /* write actual byte count to 32 bit word as header */
            n1 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), dp);
            /* write the data mod 2 */
            nw = fwrite((unsigned char *)data, 1, (size_t)hc, dp);
            /* write the byte count in 32 bit word as footer */
            n2 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), dp);
            if (n1 != sizeof(hc) || nw != hc || n2 != sizeof(hc))
            {
                fprintf(stderr, "write (%d) failure\n", nw);
                fprintf(stderr, "Operation aborted\n");
                exit(1);
            }
            memset((char *)data, 0, 4608);      /* zero data storage */
        }
        printf("write file %s user %s (size %d bytes) (%d sect) (%d blocks)\n",
            fnp, userp, word, size, blks);
        fclose(fp);
    }
    /* write EOF (zero) to file */
    filen = 0;                                  /* zero count */
    fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
    /* do second EOF */
    fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
    filen = -1;                                 /* make in -1 for EOM */
    /* do EOM */
    fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
//  printf("setting at %ld bytes in file after EOM\n", ftell(dp));
    fclose(dp);
    exit(0);
}
