/*
 * mkvmtape.c
 *
 * This program reads MPX files and stores them into a simulated
 * volmgr save tape. The tape may then become an sdt boot tape
 * to install MPX from or a volmgr file restore tape on a running
 * MPX system.
 *
 * intput - [options] volmtape filename, filename, etc.
 * output - volmtape file, file list to stdout
 *
 * options - -p = file type 0xca for programs
 *         - -t = ascii text file 0xee
 *         - -l = library/directory file 0xff
 *         - -o = other 0x00
 *         - -a = append entries to current file
 *         - -u = username (username)
 *         - -d = dirname (directory)
 *         - -v = volname (volume)
 *         - -b = bootfile name
 *         - -i = system image file
 *         - -j = j.vfmt filename
 * 04/11/2020 update for mpx3.x
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#define MASK24 0x00ffffff

/* memory access macros */
/* The RMW and WMW macros are used to read/write memory words */
/* RMW(addr) or WMW(addr, data) where addr is a byte alligned word address */
#define RMB(a) ((M[(a)>>2]>>(8*(7-(a&3))))&0xff)    /* read memory addressed byte */
#define RMH(a) ((a)&2?(M[(a)>>2]&RMASK):(M[(a)>>2]>>16)&RMASK)  /* read memory addressed halfword */
#define RMW(a) (M[((a)&MASK24)>>2])     /* read memory addressed word */
#define WMW(a,d) (M[((a)&MASK24)>>2]=d) /* write memory addressed word */
/* write halfword to memory address */
#define WMH(a,d) ((a)&2?(M[(a)>>2]=(M[(a)>>2]&LMASK)|((d)&RMASK)):(M[(a)>>2]=(M[(a)>>2]&RMASK)|((d)<<16)))
/* write byte to memory */
#define WMB(a,d) (M[(a)>>2]=(((M[(a)>>2])&(~(0xff<<(8*(7-(a&3))))))|((d&0xff)<<(8*(7-(a&3))))))

#define BLKSIZE 768                     /* MPX file sector size */
u_int32_t dir[32];                      /* directory name */
u_int32_t vol[32];                      /* volume name */
unsigned char data[4608];               /* room for 6*768=(4608) 768 byte sectors per 4608 byte block */
unsigned char bigdata[19200];           /* room for 6*768=(4608) 768 byte sectors per 4608 byte block */
u_int32_t M[768];                       /* fake memory */
unsigned char bootcode[2048];           /* room for bootcode */
unsigned char savelist[6144];           /* room for 8 byte flags, 127 (48 char) file names */
int16_t     savecnt = 0;                /* entries in save list */
char sysname[16] = "SYSTEM          ";

/* write 1 file to tape in 768 byte records */
int writefile(FILE *tp, char *fnp, u_int32_t mblks) {
    u_int32_t word, blks;                   /* just a temp word variable */         
    u_int32_t size, bsize;                  /* size in 768 byte sectors */
    FILE *fp;
    int32_t n1, n2, hc, nw;

    memset((char *)data, 0, sizeof(data));  /* zero data storage */
    /* write file to tape */
    if ((fp = fopen(fnp, "r")) == NULL) {
        fprintf(stderr, "error: can't open user file %s\n", fnp);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);                 /* seek to end */
    word = ftell(fp);                       /* get filesize in bytes */
printf("MPX file %s is %x (%d) bytes\n", fnp, word, word);
    fseek(fp, 0, SEEK_SET);                 /* rewind file */
    size = (word/768);                      /* filesize in sectors */
    if (word%768 != 0)                      /* see if byte left over */
        size += 1;                          /* partial sector, add 1 */
    if (mblks == 0) {
        blks = (word/768);                  /* blocks */
        if ((word%768) != 0)                /* round up blks if remainder */
            blks++; 
    } else {
        blks = mblks;                       /* write user specified blocks */
    }
    bsize = blks;                           /* save # blks */

    /* read in the image file */
    while ((bsize-- > 0) && fread((char *)data, 1, 768, fp) > 0) {
        /* we have data to write */
        hc = (768 + 1) & ~1;                /* make byte count even */
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
        memset((char *)data, 0, 768);       /* zero data storage */
    }
    printf("write file %s (size %d bytes) (%d sect) (%d blocks)\n",
        fnp, word, size, blks);
    fclose(fp);
}

u_int32_t readboot(char *name, char *buf, u_int32_t start, u_int32_t end) {
    u_int32_t word = end-start+4;           /* just a temp word variable */         
    FILE *fp;
    int32_t n1, n2;

    memset((char *)bootcode, 0, sizeof(bootcode));  /* zero data storage */
    if ((fp = fopen(name, "r")) == NULL) {
        fprintf(stderr, "error: can't open user file %s\n", name);
        exit(1);
    }
    fseek(fp, start, 0);                    /* seek to boot code */
    n1 = fread(bootcode, 1, word, fp);      /* read bootcode */
    if (n1 <=0)                             /* check for read error */
        exit(1);                            /* bad tape format */
printf("MPX bootfile %s is %x (%d) bytes\n", name, word, word);
    fclose(fp);
    fopen("volmboot", "w");
    fwrite(bootcode, 1, word, fp);
    fclose(fp);
    for (n2=0; n2<word; n2++)
        buf[n2] = bootcode[n2];
    return word;
}

/* byte swap the 32 bit word */
u_int32_t flip(u_int32_t val) {
    /* byte swap the buffers for dmap and umap */
    return (((val & 0xff) << 24) | ((val & 0xff00) << 8) |
            ((val & 0xff0000) >> 8) | ((val >> 24) & 0xff));
}

/* read program file and output to a simulated mpx3.x volmgr savetape */
/* mkvmtape [opt] volmtape, filename, filename, ... */
int main(argc, argv)
int argc;
char *argv[];
{
    FILE    *fp, *dp, *fopen();
    int     targc;
    char    **targv;
    unsigned char username[32];
    unsigned char bootname[32];
    unsigned char imgname[32];
    unsigned char vfmtname[32];
    unsigned char volname[32];
    unsigned char dirname[32];
    char *p;
    int i;
#define DOPROG  1
#define DOADD  2
#define DOOTHER  4
#define DOTEXT  8
#define DOLIB  16
#define DOUSER  32
#define DOBOOT  64
#define DOIMG  128
#define DOVFMT 256
#define DOVOL 512
#define DODIR 1024
    unsigned int option = DOTEXT;           /* what to do */
    unsigned char *fnp;                     /* file name pointer */
    unsigned int size;                      /* size in 768 byte sectors */
    unsigned int word;                      /* just a temp word variable */         
    unsigned char name[128];                /* LM name */
    unsigned int typ;                       /* file type requested by user */
    char *userp = username;                 /* pointer to username */
    char *bootp = bootname;                 /* pointer to boot file name */
    char *imgp= imgname;                    /* pointer to image file name */
    char *vfmtp = vfmtname;                 /* pointer to j.vfmt file name */
    int ofd;                                /* output file number */
    int32_t filen;                          /* file number */
//  u_int32_t *dirp = dir;                  /* directory entry pointer */
//  u_int32_t *volp = vol;                  /* volume entry pointer */
    char *dirp = dirname;                   /* directory entry pointer */
    char *volp = volname;                   /* volume entry pointer */
    int32_t totent;                         /* total smd entries */
    char    j_vfmt[] = {"!VOLSYST!DIRSYST!FIL    J.VFMT          "};
    char    j_mount[] = {"!VOLSYST!DIRSYST!FIL    J.MOUNT         "};
    char    j_swapr[] = {"!VOLSYST!DIRSYST!FIL    J.SWAPR         "};
    char    volmgr[] = {"!VOLSYST!DIRSYST!FIL    VOLMGR          "};

//  memset((char *)dir, 0, 4608);           /* zero smd storage */
    memset((char *)data, 0, 4608);          /* zero data storage */
    for (i=0; i<32; i++)
        username[i] = 0;                    /* use zero for system username */
    typ = 0xee000000;                       /* set type */
    if (argc <= 1) {                        /* see if correct # args */
        fprintf(stderr,
            "Usage: %s [-ptloa] [-bboot] [-iimage] [-jj.vfmt] [-uusername] vmgrtape file1 file2 ...\n",
            *argv);
        exit(1);
    }
    while(--argc > 0) {
//      printf("argc %d argv %s\n", argc, *argv);
        p = *++argv;
        if (*p++ == '-') {
            if (*p == '\0') {
                fprintf(stderr, "Error: no option specified\n");
                fprintf(stderr,
            "Usage: %s [-ptloa] [-bboot] [-iimage] [-jj.vfmt] [-uusername] vmgrtape file1 file2 ...\n",
                    *argv);
                exit(1);
            }
//          printf("doing options %s\n", p);
            while (*p != '\0') {
                switch (*p++) {
                    case 'b':
                    case 'B':
                        if (option & DOADD) { /* error if append specified with boot command */
                            fprintf(stderr, "Error: -b cannot be specified with -a option\n");
                            goto error1;    /* we are done here */
                        }
                        option |= DOBOOT;   /* save boot code, mpx image, and j.vfmt */
                        if (*p == '\0') {
                            p = *++argv;    /* next parameter */
                            --argc;         /* one less arg */
                        };
                        bootp = p;
                        while (*p != '\0')
                            p++;
                        typ = 0xca;         /* set type */
                        break;
                    case 'i':
                    case 'I':
                        option |= DOIMG;    /* save mpx image filename */
                        if (*p == '\0') {
                            p = *++argv;    /* next parameter */
                            --argc;         /* one less arg */
                        };
                        imgp = p;
                        while (*p != '\0')
                            p++;
                        typ = 0xca;         /* set type */
                        break;
                    case 'j':
                    case 'J':
                        option |= DOVFMT;   /* save j.vfmt file name */
                        if (*p == '\0') {
                            p = *++argv;    /* next parameter */
                            --argc;         /* one less arg */
                        };
                        vfmtp = p;
                        while (*p != '\0')
                            p++;
                        typ = 0xca;         /* set type */
                        break;
                    case 'p':
                    case 'P':
                        option |= DOPROG;   /* save program modules */
                        typ = 0xca;         /* set type */
                        break;
                    case 'A':
                    case 'a':
                        if (option & DOBOOT) { /* error if boor specified with append command */
                            fprintf(stderr, "Error: -a cannot be specified with -b option\n");
                            goto error1;    /* we are done here */
                        }
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
                    case 'V':
                    case 'v':
                        option |= DOVOL;    /* save volume name files */
                        if (*p == '\0') {
                            p = *++argv;    /* next parameter */
                            --argc;         /* one less arg */
                        };
                        volp = p;           /* save volume pointer */
                        while (*p != '\0')
                            p++;
                        break;
                    case 'D':
                    case 'd':
                        option |= DODIR;    /* save directory name for files */
                        if (*p == '\0') {
                            p = *++argv;    /* next parameter */
                            --argc;         /* one less arg */
                        };
                        dirp = p;
                        while (*p != '\0')
                            p++;
                        break;
                    case 'U':
                    case 'u':
                        option |= DOUSER;   /* save username for files */
                        if (*p == '\0') {
                            p = *++argv;    /* next parameter */
                            --argc;         /* one less arg */
                        };
                        userp = p;
                        while (*p != '\0')
                            p++;
                        break;
                    default:
                        fprintf(stderr, "Error: no option specified\n");
error1:
                        fprintf(stderr,
            "Usage: %s [-ptloa] [-bboot] [-iimage] [-jj.vfmt] [-uusername] vmgrtape file1 file2 ...\n",
                            *argv);
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

    if (!((option & DOBOOT) && (option & DOIMG) && (option & DOVFMT))) {
        fprintf(stderr, "Error: incorrect number of sdt files, must be three\n");
        fprintf(stderr, "Usage: %s [-ptloa] [-uusername] fmgrtape, file1 file2 ...\n", *argv);
        exit(1);
    }
    /* process the bootfile first */
    if (option & DOBOOT) {
        int32_t w2, n1, n2, nw, hc, blks;
        fnp = bootp;                                /* get file name pointer */
#ifdef USE_FILENAME
        memset((char *)data, 0, 0x800);              /* zero data storage */
        if ((fp = fopen(bootp, "r")) == NULL) {
            fprintf(stderr, "error: can't open boot file %s\n", bootp);
            exit(1);
        }
        fseek(fp, 0, SEEK_END);                     /* seek to end */
        word = ftell(fp);                           /* get filesize in bytes */
printf("bootfile %s is %x (%d) bytes\n", bootp, word, word);
        fseek(fp, 0, SEEK_SET);                     /* rewind file */
        /* bootfile must be <= 0x780 chars */
//      if (word > 0x780) {
//          fprintf(stderr, "error: boot file %s greater than 0x780 bytes\n", bootp);
//          exit(1);
//      }
        w2 = fread((char *)data, 1, word, fp);      /* read the boot file */
#else
        /* go cut the boot code from the volmgr load module */
        w2 = readboot("volmgr", (char *)data, 0x1c9a0, 0x1d144);
#endif
        /* we have data to write */
        hc = (w2 + 1) & ~1;                         /* make byte count even */
//      hc = (0x780 + 1) & ~1;                      /* make byte count even */
        /* write actual byte count to 32 bit word as header */
        n1 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), dp);
        /* write the data mod 2 */
        nw = fwrite((unsigned char *)data, 1, (size_t)hc, dp);
        /* write the byte count in 32 bit word as footer */
        n2 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), dp);
        if (n1 != sizeof(hc) || nw != hc || n2 != sizeof(hc)) {
            fprintf(stderr, "write (%d) failure\n", nw);
           fprintf(stderr, "Operation aborted\n");
           exit(1);
        }
        printf("write boot file %s (size %d bytes)\n",
            bootp, word, word);
        /* setup for mpx image file */
        memset((char *)data, 0, 0x800);              /* zero data storage */
#ifdef USE_FILENAME
        fclose(fp);
#endif

        memset((char *)M, 0, 192);                  /* zero data storage */
        if ((fp = fopen(imgp, "r")) == NULL) {
            fprintf(stderr, "error: can't open boot file %s\n", imgp);
            goto error1;                            /* we are done here */
        }
        fnp = imgp;                                 /* get file name pointer */
        fseek(fp, 0, SEEK_END);                     /* seek to end */
        word = ftell(fp);                           /* get filesize in bytes */
printf("image file %s is %x (%d) bytes\n", imgp, word, word);
        fseek(fp, 0, SEEK_SET);                     /* rewind file */
        w2 = fread((char *)M, sizeof(u_int32_t), 192, fp);  /* read the image file */
//      n1 = RMW(0x68);                             /* get PR.BYTDR bytes in dsect rel matrix */
//      n2 = RMW(0x64);                             /* get PR.SFADR sec addr of rel matrix */
        n1 = flip(M[0x68/4]);                       /* get PR.BYTDR bytes in dsect rel matrix */
        n2 = flip(M[0x64/4]);                       /* get PR.SFADR sec addr of rel matrix */
        if (n2 == 0) {                              /* if zero use PR.SFAD rel addr of dsect */
//          n1 = RMW(0x5C);                         /* get PR.BYTED bytes in dsect */
//          n2 = RMW(0x58);                         /* get PR.SFADR sec addr of dsect */
            n1 = flip(M[0x5C/4]);                   /* get PR.BYTED bytes in dsect */
            n2 = flip(M[0x58/4]);                   /* get PR.SFADR sec addr of dsect */
            n2 += 1;                                /* add 1 blk for sys debug blk */
        }
        blks = n1/768;                              /* get #block rounded mod 768 */
        if ((n1%768) != 0)                          /* round up blks if remainder */
            blks++; 
        blks += n2;                                 /* get total blks to read */
printf("image file %s n1 %x (%d) n2 %x (%d) blks %x (%d)\n",
   imgp, n1, n1, n2, n2, blks, blks);
        fseek(fp, 0, SEEK_SET);                     /* rewind file */
        fclose(fp);

        /* write mpx image file */
        writefile(dp, imgp, blks);                  /* write max of "blks" blocks to file */

        /* write j.vfmt file */
        writefile(dp, vfmtp, 0);

        /* write EOF (zero) to file */
        filen = 0;                                  /* zero count */
        fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);

        /* write j.mount file */
        writefile(dp, "j.mount", 0);

        /* write j.swapr file */
        writefile(dp, "j.swapr", 0);

        /* write volmgr file */
        writefile(dp, "volmgr", 0);

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

    /* process save files */
    if ((argc-1) <= 0) {
        fprintf(stderr, "Error: incorrect number of parameters\n");
        goto error1;                            /* we are done here */
    }
/*------------------------------------------------------------------------*/
    savecnt = 0;                                /* files yet */
    /* make first pass over files and get filenames and sizes */
    /* got tapefile and options, handle files now */
    targc = argc;                               /* save argc to reread list */
    targv = argv;                               /* save argv to reread list */
//  printf("AT 3 argc %d argv %s\n", argc, *argv);
    filen = 0;                                  /* no files yet */
    totent = 0;                                 /* no files yet */

    /* save up to 127 file names in 6144 byte record */
    /* filename/directory/volume */
    /* wd 1 is 1 for type 1 record */
    /* wd 2 will be # of 48 byte pathnames */ 
    memset((char *)savelist, 0, sizeof(savelist));  /* zero data storage */
    /* populate the 48 byte entry starting at byte 2 */
//  dirp = (u_int32_t *)dir;                    /* get word pointer for directory */
//  volp = (u_int32_t *)vol;                    /* get word pointer for volume */
    dirp = dirname;                             /* get word pointer for directory */
    volp = volname;                             /* get word pointer for volume */
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
            (smd[4] == 0 && smd[5] == 0)) {
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
//          memset((char *)dir, 0, 4608);       /* zero smd storage */
            filen = 0;                          /* restart count */
//          dirp = (u_int32_t *)dir;            /* get word pointer for smd data */
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
//      memset(dir, 0, 4608);                   /* zero smd storage */
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
