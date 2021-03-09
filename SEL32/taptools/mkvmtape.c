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
#define RMB(a) ((M[(a)>>2]>>(8*(3-(a&3))))&0xff)    /* read memory addressed byte */
#define RMH(a) ((a)&2?(M[(a)>>2]&RMASK):(M[(a)>>2]>>16)&RMASK)  /* read memory addressed halfword */
#define RMW(a) (M[((a)&MASK24)>>2])     /* read memory addressed word */
#define WMW(a,d) (M[((a)&MASK24)>>2]=d) /* write memory addressed word */
/* write halfword to memory address */
#define WMH(a,d) ((a)&2?(M[(a)>>2]=(M[(a)>>2]&LMASK)|((d)&RMASK)):(M[(a)>>2]=(M[(a)>>2]&RMASK)|((d)<<16)))
/* write byte to memory */
#define WMB(a,d) (M[(a)>>2]=(((M[(a)>>2])&(~(0xff<<(8*(3-(a&3))))))|((d&0xff)<<(8*(3-(a&3))))))

#define BLKSIZE 768                     /* MPX file sector size */
u_int32_t dir[32];                      /* directory name */
u_int32_t vol[32];                      /* volume name */
unsigned char data[6144];               /* room for 8*768=(6144) 768 byte sectors per 4608 byte block */
unsigned char bigdata[19200];           /* room for 6*768=(4608) 768 byte sectors per 4608 byte block */
u_int32_t M[768];                       /* fake memory */
unsigned char bootcode[2048];           /* room for bootcode */
u_int32_t resdes[384];                  /* room for the 1536 char (two blks) resource descriptor */
u_int32_t dirlist[1536];                /* 6144 byte directory listing */
int16_t     savecnt = 0;                /* entries in save list */
char sysname[16] = "SYSTEM          ";

/* write 1 file to tape in 768 byte records */
/* mblks is the maximum blockes to write from a file, 0=all */
/* chunks is the number of sectors to wrote at a time 1-8 */
int writefile(FILE *tp, char *fnp, u_int32_t mblks, int32_t chunks) {
    u_int32_t word, blks=mblks;             /* just a temp word variable */         
    u_int32_t size, bsize, csize;           /* size in 768 byte sectors */
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
//        fnp, word, word, bsize, (bsize+1)/chunks);
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
//      fnp, word, size, mblks, blks);
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
//printf("MPX bootfile %s is %x (%d) bytes\n", name, word, word);
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
    int i, n, eof;
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
#define DOMASK (DOBOOT|DOIMG|DOVFMT)
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
/// char *dirp = dirname;                   /* directory entry pointer */
/// char *volp = volname;                   /* volume entry pointer */
    char *dirp = sysname;                   /* directory entry pointer */
    char *volp = sysname;                   /* volume entry pointer */
    int32_t totent;                         /* total smd entries */
    char    j_vfmt[] = {"!VOLSYST!DIRSYST!FIL    J.VFMT          "};
    char    j_mount[] = {"!VOLSYST!DIRSYST!FIL    J.MOUNT         "};
    char    j_swapr[] = {"!VOLSYST!DIRSYST!FIL    J.SWAPR         "};
    char    volmgr[] = {"!VOLSYST!DIRSYST!FIL    VOLMGR          "};

//  memset((char *)dir, 0, 4608);           /* zero smd storage */
    memset((char *)data, 0, 4608);          /* zero data storage */
    for (i=0; i<32; i++)
        username[i] = 0;                    /* use zero for system username */
    typ = 0xee;                             /* set type */
    if (argc <= 1) {                        /* see if correct # args */
        fprintf(stderr,
            "Usage: %s [-ptloa] [-bboot] [-iimage] [-jj.vfmt] [-uusername] vmgrtape file1 file2 ...\n",
            *argv);
        exit(1);
    }
    while(--argc > 0) {
//printf("argc %d argv %s\n", argc, *argv);
        p = *++argv;
        if (*p++ == '-') {
            if (*p == '\0') {
                fprintf(stderr, "Error: no option specified\n");
                fprintf(stderr,
            "Usage: %s [-ptloa] [-bboot] [-iimage] [-jj.vfmt] [-uusername] vmgrtape file1 file2 ...\n",
                    *argv);
                exit(1);
            }
//printf("doing options %s\n", p);
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
                        if (option & DOBOOT) { /* error if boot specified with append command */
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
//                      typ = 0xff;         /* set type */
                        typ = 0x00;         /* set type */
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
//printf("option set to %x\n", option);
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
//printf("1 file length %ld %lx bytes\n", bytes, bytes);
//printf("1 start writing at %ld %lx bytes offset\n", bytes-8, bytes-8);
                fseek(dp, 0, SEEK_SET);             /* rewind file to beginning */
                /* at this point, we are at the end of the tape */
                /* we should see 3 EOF's w/ or w/o an EOM */
                if (bytes > 8) {                    /* see if file written to already */
                    /* we need to find the EOT */
                    int32_t n1, n2, hc, tc, n;
                    int EOFcnt = 0;
readmore:
                    fseek(dp, bytes-4, SEEK_SET);   /* seek back to EOF/EOM code */
                    n1 = fread((char *)(&hc), 1, (size_t)4, dp);    /* read 4 byte record size */
                    if (n1 <=0)                     /* check for read error */
                        goto doabort;               /* bad tape format */

//printf("2 file length %ld %lx bytes\n", bytes, bytes);
//printf("2 start writing at %ld %lx bytes offset\n", bytes-8, bytes-8);
                    if (hc & 0xffff0000)            /* check for garbage */
                        hc = 0;                     /* assume EOF on disk */

                    if (hc == 0) {                  /* check for EOF on file */
                        /* EOF found */
                        if (++EOFcnt == 2) {
                            /* we have second EOF, we need to backup 4 bytes */
backup4:
                            /* we are setting after 2nd EOF, start writing there */
//                          bytes -= 4;             /* backup 4 bytes */
                            fseek(dp, bytes-4, SEEK_SET);   /* backspace over 2nd EOF */
//printf("3 file length %ld %lx bytes\n", bytes, bytes);
//printf("3 start writing at %ld %lx bytes offset\n", bytes-8, bytes-8);
                            goto getout;            /* start our processing */
                        }
                        /* we have first EOF, keep reading */
                        bytes -= 4;                 /* backup 4 bytes */
                        goto readmore;              /* read more records */
                    } else
                    if (hc == -1) {                 /* check for EOM */
                        if (EOFcnt != 0)
                            /* we have an EOM before an EOF, so bad tape */
                            goto doabort;           /* bad tape format */
                        bytes -= 4;                 /* backup 4 bytes */
                        goto readmore;              /* read more records */
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
//printf("3 opened output in w mode, write at start\n");
            }
getout:
//printf("opening %s file for tape\n", *argv);
            *++argv;
            break;      /* go handle files now */
        }
        continue;
    }
    /* end while --argc */

    if (!DOADD) {
    if ((option & DOMASK) && ((option & DOMASK) != DOMASK)) {
        fprintf(stderr, "Error: incorrect number of sdt files, must be three\n");
        fprintf(stderr, "Usage: %s [-ptloa] [-uusername] vmgrtape, file1 file2 ...\n", *argv);
        exit(1);
    }
    }
    /* process the bootfile first */
    if (option & DOBOOT) {
        int32_t w2, n1, n2, nw, hc, blks;
        fnp = bootp;                                /* get file name pointer */
#ifndef USE_FILENAME
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
//      w2 = readboot("volmgr", (char *)data, 0x1c9a0, 0x1d144);
        w2 = readboot("volmgr", (char *)data, 0x1c9a0, 0x1d140);
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
printf("write boot file %s (size %d bytes)\n", bootp, word, word);
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
        writefile(dp, imgp, blks, 1);               /* write max of "blks" blocks to file */

        /* write j.vfmt file */
        writefile(dp, vfmtp, 0, 1);                 /* write 1 blk at a time */

        /* write EOF (zero) to file */
        filen = 0;                                  /* zero count */
        fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);

        /* write j.mount file */
        writefile(dp, "j.mount", 0, 1);             /* one blk at a time */

        /* write j.swapr file */
        writefile(dp, "j.swapr", 0, 1);             /* one blk at a time */

        /* write volmgr file */
        writefile(dp, "volmgr", 0, 1);              /* all of file 1 blk at a time */

        /* write EOF (zero) to file */
        filen = 0;                                  /* zero count */
        fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);

        /* do second EOF */
        fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
        filen = -1;                                 /* make in -1 for EOM */
        /* do EOM */
        fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
//printf("setting at %ld bytes in file after EOM\n", ftell(dp));
        fclose(dp);
        exit(0);
    }

    /* process save files */
    if ((argc-1) <= 0) {
        fprintf(stderr, "Error: incorrect number of parameters\n");
        goto error1;                            /* we are done here */
    }
    /* save up to 127 file names in 6144 byte record */
    if (argc > 127) {
        fprintf(stderr, "Error: only 127 files max at a time\n");
        goto error1;                            /* we are done here */
    }
/*------------------------------------------------------------------------*/
    savecnt = 0;                                /* no files yet */
    /* make first pass over files and get filenames and sizes */
    /* got tapefile and options, handle files now */
    targc = argc;                               /* save argc to reread list */
    targv = argv;                               /* save argv to reread list */
//printf("AT 3 argc %d argv %s\n", argc, *argv);
    filen = 0;                                  /* no files yet */
    totent = 0;                                 /* no files yet */

    /* save up to 127 file names in 6144 byte record */
    if (argc > 127) {
        fprintf(stderr, "Error: only 127 files max at a time\n");
        goto error1;                            /* we are done here */
    }
    /* filename/directory/volume */
    /* wd 1 is 1 for type 1 record */
    /* wd 2 will be # of 48 byte pathnames */ 
    memset((char *)dirlist, 0, sizeof(dirlist));    /* zero data storage */

    /* populate the 48 byte entry starting at byte 2 */
//  dirp = (u_int32_t *)dir;                    /* get word pointer for directory */
//  volp = (u_int32_t *)vol;                    /* get word pointer for volume */
//  dirp = dirname;                             /* get word pointer for directory */
//  volp = volname;                             /* get word pointer for volume */
/// dirp = sysname;                             /* get word pointer for directory */
    volp = sysname;                             /* get word pointer for volume */
    n = 2;
    dirlist[0] = n << 24;                       /* set record type to 1 */
    while (--argc > 0) {
        u_int32_t smd[32];                      /* dir entry data */
        int blks, eof;

        for (i=0; i<16; i++)                    /* zero smd entry */
            smd[i] = 0x20;                      /* make blank */
        p = *argv++;
        i = strlen(p);                          /* filename size */
        if (i == 0 || i > 16) {
            fprintf(stderr, "error: Filename too long (%d>16) %s, Aborting\n", i, p);
            exit(1);
        }
//printf("argc %d argv3 %s\n", argc, p);
        if ((fp = fopen(p, "r")) == NULL) {
            fprintf(stderr, "error: can't open user file %s\n", p);
            exit(1);
        }
        fnp = p;                                /* get file name pointer */

        /* create dir entry for this file */
        /* first is 16 char filename */
        memset(name, ' ', 16);                  /* blank directory */
        for (i=0; i<16; i++) {
            if (p[i] == '\0')                   /* check for null termination char */
                break;
            name[i] = toupper(p[i]);            /* uppercase filename */
        }
        /* populate the 16 byte file name entry */
        dirlist[n+0] = name[3] << 24 | name[2] << 16 | name[1] << 8 | name[0];
        dirlist[n+1] = name[7] << 24 | name[6] << 16 | name[5] << 8 | name[4];
        dirlist[n+2] = name[11] << 24 | name[10] << 16 | name[9] << 8 | name[8];
        dirlist[n+3] = name[15] << 24 | name[14] << 16 | name[13] << 8 | name[12];
        n += 4;

        /* populate the 16 byte directory/user name entry */
        memset(name, ' ', 16);                  /* blank directory */
        for (i=0; i<16; i++) {
            if (dirp[i] == '\0')                /* check for null termination char */
                break;
            name[i] = toupper(dirp[i]);         /* uppercase directory name */
        }
        /* set directory name */
        dirlist[n+0] = name[3] << 24 | name[2] << 16 | name[1] << 8 | name[0];
        dirlist[n+1] = name[7] << 24 | name[6] << 16 | name[5] << 8 | name[4];
        dirlist[n+2] = name[11] << 24 | name[10] << 16 | name[9] << 8 | name[8];
        dirlist[n+3] = name[15] << 24 | name[14] << 16 | name[13] << 8 | name[12];
        n += 4;

        /* populate the 16 byte volume name entry */
        memset(name, ' ', 16);                  /* blank directory */
        for (i=0; i<16; i++) {
            if (dirp[i] == '\0')                /* check for null termination char */
                break;
            name[i] = toupper(dirp[i]);         /* uppercase directory name */
        }
        /* set volume name */
        dirlist[n+0] = sysname[3] << 24 | sysname[2] << 16 | sysname[1] << 8 | sysname[0];
        dirlist[n+1] = sysname[7] << 24 | sysname[6] << 16 | sysname[5] << 8 | sysname[4];
        dirlist[n+2] = sysname[11] << 24 | sysname[10] << 16 | sysname[9] << 8 | sysname[8];
        dirlist[n+3] = sysname[15] << 24 | sysname[14] << 16 | sysname[13] << 8 | sysname[12];
        n += 4;

        filen++;                                /* bump the file count */
        totent++;                               /* bump total count */
    }

//printf("AT write file list with %d entries\n", filen);
    /* dirlist now has 1-127 filename entries to save */
    /* write out the directory entries for the files to save */
    if (filen != 0) {
        /* we need to write out the directory entries */
        int32_t n1, n2, nw;
        int32_t hc = (6144 + 1) & ~1;           /* make byte count even */
        /* put record type 1 in dirlist[0] byte swapped */
        dirlist[0] = 0x01000000;                /* record type 1 */
        /* put the number of entries into dirlist[1] */
        dirlist[1] = (filen&0xff) << 24 | (filen&0xff00) << 16 |
            (filen&0xff0000) >> 8 | ((filen&0xff000000) >> 16);

        /* now output the record */
        /* write actual byte count to 32 bit word as header */
        n1 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), dp);
        /* write the data mod 2 */
        nw = fwrite((unsigned char *)dirlist, 1, (size_t)hc, dp);
        /* write the byte count in 32 bit word as footer */
        n2 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), dp);
        if (n1 != sizeof(hc) || nw != hc || n2 != sizeof(hc))
        {
            fprintf(stderr, "write (%d) failure\n", nw);
            fprintf(stderr, "Operation aborted\n");
            exit(1);
        }
//      filen = 0;                              /* restart count */
    }

    /* write EOF (zero) to file */
    eof = 0;                                    /* zero count */
    fwrite((char *)(&eof), 1, (size_t)sizeof(eof), dp);

    /* totcnt has total number of files to output */
    memset((char *)data, 0, 4608);              /* zero data storage */
    argc = targc;                               /* restore argc for reread */
    argv = targv;                               /* restore argv for reread */
    n = 2;
    /* read each file and output to save tape file in 6 sector blocks */
    while (--argc > 0) {
        int32_t n1, n2, nw, k;
        int32_t hc = (1536 + 1) & ~1;           /* make byte count even */
        int blks;

        p = *argv++;
//printf("at 4 argc %d argv %s\n", argc, p);

        if ((fp = fopen(p, "r")) == NULL) {
            fprintf(stderr, "error: can't open user file %s\n", p);
            exit(1);
        }
        fnp = p;                                /* get file name pointer */

        /* we need to write the resource descriptor in 2 blks to file */
        /* followed by file content is 8 blk chunks */
        fseek(fp, 0, SEEK_END);                 /* seek to end */
        word = ftell(fp);                       /* get filesize in bytes */
        fseek(fp, 0, SEEK_SET);                 /* rewind file */
        size = (word/768);                      /* filesize in sectors */
        if (word%768 != 0)                      /* see if byte left over */
            size += 1;                          /* partial sector, add 1 */
        /* round up mod 4 */
        size += 3;
        size &= ~3;
        fclose(fp);
        /*******************************/
        memset((char *)resdes, 0, sizeof(resdes));  /* zero resource descriptor storage */
        /* wd 0 record type 2, wd 1 is zero of phase error flag n/u */
        resdes[0] = 0x02000000;                 /* 2 byte swapped */
        /* 48 char file/directory/volume name (122 wds) */
        /* copy name from dirlist written earlier */
        resdes[2] = dirlist[n+0];               /* file */
        resdes[3] = dirlist[n+1];
        resdes[4] = dirlist[n+2];
        resdes[5] = dirlist[n+3];
        resdes[6] = dirlist[n+4];               /* directory, system*/
        resdes[7] = dirlist[n+5];
        resdes[8] = dirlist[n+6];
        resdes[9] = dirlist[n+7];
        resdes[10] = dirlist[n+8];               /* volume, system */
        resdes[11] = dirlist[n+9];
        resdes[12]= dirlist[n+10];
        resdes[13]= dirlist[n+11];
        /* 16 word resource create block */
        resdes[14] = dirlist[n+8];              /* owner, system */
        resdes[15] = dirlist[n+9];              /* only 8 bytes */
//      resdes[16]= dirlist[n+10];
//      resdes[17]= dirlist[n+11];
///     resdes[18] = dirlist[n+8];              /* group, system */
///     resdes[19] = dirlist[n+9];              /* only 8 bytes */
        resdes[16] = dirlist[n+8];              /* group, system */
        resdes[17] = dirlist[n+9];              /* only 8 bytes */
//      resdes[20]= dirlist[n+10];
//      resdes[21]= dirlist[n+11];
        n += 12;
#ifdef OLDOLD
        resdes[22]=flip(0x80f00000);            /* owner rights */
        resdes[23]=flip(0x80b00000);            /* group rights */
        resdes[24]=flip(0x80800000);            /* other rights */
        if (typ == 0xca)
            resdes[25]=flip(0x00040110);        /* res mgmt flags */
        else
            resdes[25]=flip(0x00040110);        /* res mgmt flags */
//          resdes[25]=flip(0x0004011c);        /* res mgmt flags */
        resdes[29]=flip(size);                  /* org file size */
        resdes[31]=flip(1000);                  /* file starting address n/u */
        resdes[33]=flip(0x00fbfeef);            /* option flags */
#else
        resdes[18]=flip(0x80f00000);            /* owner rights */
        resdes[19]=flip(0x80b00000);            /* group rights */
        resdes[20]=flip(0x80800000);            /* other rights */
        if (typ == 0xca)
            resdes[21]=flip(0x00040110);        /* res mgmt flags */
        else
            resdes[21]=flip(0x00040110);        /* res mgmt flags */
//          resdes[21]=flip(0x0004011c);        /* res mgmt flags */
        resdes[25]=flip(size);                  /* org file size */
        resdes[27]=flip(1000);                  /* file starting address n/u */
        resdes[29]=flip(0x00fbfeef);            /* option flags */
#endif
        /* reset of block is zero */

        /* second block is resosurce descriptor from disk */
        resdes[192] = dirlist[n+8];             /* volume, system */
        resdes[193] = dirlist[n+9];
        resdes[194]= dirlist[n+10];
        resdes[195]= dirlist[n+11];

        resdes[196]=flip(0x00003190);           /* creation date */
        resdes[197]=flip(0x0e8c8000);           /* creation time */
        resdes[198]=flip(0x000003c0);           /* abs blk if res des */
        resdes[199]=flip(0x0000000a);           /* resource type, perm file */

        resdes[200]=flip(0x000029cf);           /* creation date */
        resdes[201]=flip(0x1dd8e074);           /* creation time */
        //202
        //203

        //204
        //205
        resdes[206]=flip(0x000029cf);           /* last chge date */
        resdes[207]=flip(0x1dd8e074);           /* last chge time */

        resdes[208]=flip(0x00003190);           /* creation date */
        resdes[209]=flip(0x0e8c8000);           /* creation time */
        //210
        //211

///     resdes[212] = dirlist[n+8];             /* ownername last changer, system */
///     resdes[213] = dirlist[n+9];
        resdes[214] = dirlist[n+8];             /* ownername creator, system */
        resdes[215] = dirlist[n+9];

        //216
        //217
        resdes[218] = dirlist[n+8];             /* ownername of resource, system */
        resdes[219] = dirlist[n+9];

        resdes[220] = dirlist[n+8];             /* group of resource, system */
        resdes[221] = dirlist[n+9];
        resdes[222]=flip(0xf8400000);           /* owner access */
//      resdes[223]=flip(0xf8e00000);           /* group access */
        resdes[223]=flip(0xf8400000);           /* group access */

        resdes[224]=flip(0x80000000);           /* other access */
        //225
        resdes[226]=flip(0x00000001);           /* resource link count */
        //227

        //228-255

        resdes[256]=flip(0xca100010);           /* space definition flags */
        if (typ == 0xca)
            resdes[256]=flip(0xca100010);       /* space definition flags */
        else
        if (typ == 0xee)
            resdes[256]=flip(0xee1000f1);       /* space definition flags */
        else
        if (typ == 0x00)
            resdes[256]=flip(0x001000f1);       /* space definition flags */
//      resdes[257]=flip(0x00000018);           /* max extends */
        resdes[257]=flip(0x00000040);           /* max extends */
//      resdes[258]=flip(0x00000008);           /* min incr */
        resdes[258]=flip(0x00000010);           /* min incr */
        //259

//      resdes[260]=flip(size-1);               /* eof */
//      resdes[261]=flip(size);                 /* eom */
///     resdes[260]=flip(size);                 /* eof */
///     resdes[261]=flip(size+1);               /* eom */
        resdes[260]=0;                          /* eof */
        resdes[261]=flip(size);                 /* eom */
        resdes[262]=flip(0x00000001);           /* segment */
        //263

        resdes[264]=resdes[6];                  /* directory, system*/
        resdes[265]=resdes[7];
        resdes[266]=resdes[8];
        resdes[267]=resdes[9];

        resdes[268]=flip(0x00000100);           /* parent blk number */
        resdes[269]=flip(0x00000001);           /* segments at creation  */
        //270
        //271

        resdes[272]=resdes[2];                  /* filename*/
        resdes[273]=resdes[3];
        resdes[274]=resdes[4];
        resdes[275]=resdes[5];

        resdes[276]=flip(0x00000100);           /* parent blk number */
        resdes[277]=flip(0x000005c0);           /* parent didr index number */
        //277
        //278

        //279-286

        resdes[288]=flip(0x0000fda8);           /* file blk number */
        resdes[289]=flip(size);                 /* eom */

        /* all others are zero */

        /* we have data to write */
        /* write actual byte count to 32 bit word as header */
        n1 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), dp);
        /* write the data mod 2 */
        nw = fwrite((unsigned char *)resdes, 1, sizeof(resdes), dp);
        /* write the byte count in 32 bit word as footer */
        n2 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), dp);
        if (n1 != sizeof(hc) || nw != hc || n2 != sizeof(hc))
        {
            fprintf(stderr, "rd write (%d) failure\n", nw);
            fprintf(stderr, "Operation aborted\n");
            exit(1);
        }
        /*******************************/
        /* write file up to 8 blks at a time */
        writefile(dp, fnp, 0, 8);               /* all of file 1 blk at a time */

//printf("File written at 4 argc %d argv %s\n", argc, fnp);

        /* write EOF (zero) to file */
        eof = 0;                                /* zero count */
        fwrite((char *)(&eof), 1, (size_t)sizeof(eof), dp);
    }
    /* we have saved the files in the image, write 2 eof's & 1 EOM */
//  word = ftell(dp);                           /* get position in bytes */
//  printf("setting after file EOF write pos %x %d\n", word, word);
    /* write EOF (zero) to file */
    filen = 0;                                  /* zero count */
    fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
//  word = ftell(dp);                           /* get position in bytes */
//  printf("setting after 1st EOF file pos %x %d\n", word, word);
    /* do second EOF */
    fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
    filen = -1;                                 /* make in -1 for EOM */
//  word = ftell(dp);                           /* get position in bytes */
//  printf("setting after 2st EOF file pos %x %d\n", word, word);
    /* do EOM */
    fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
//  word = ftell(dp);                           /* get position in bytes */
//  printf("setting after EOM file pos %x %d\n", word, word);
printf("setting at %lx (%ld) bytes in file after EOM\n", ftell(dp), ftell(dp));
    fclose(dp);
    exit(0);
}
