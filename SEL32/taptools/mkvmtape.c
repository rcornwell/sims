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
 *         - -m = master sdt tape image
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
#endif                                                  /* end standard integers */

#define BLKSIZE 768                     /* MPX file sector size */
uint32 dir[32];                         /* directory name */
uint32 vol[32];                         /* volume name */
unsigned char data[6144];               /* room for 8*768=(6144) 768 byte sectors per 4608 byte block */
unsigned char bigdata[19200];           /* room for 6*768=(4608) 768 byte sectors per 4608 byte block */
uint32 M[768];                          /* fake memory */
unsigned char bootcode[2048];           /* room for bootcode */
uint32 resdes[384];                     /* room for the 1536 char (two blks) resource descriptor */
uint32 dirlist[1536];                   /* 6144 byte directory listing */
int16  savecnt = 0;                     /* entries in save list */
char sysname[16] = "SYSTEM          ";
char mstrall[] = "mstrall";
char mstrext[] = "mstrext";
char mstrout[] = "mstrout";

/* write 1 file to tape in 768 byte records */
/* mblks is the maximum blockes to write from a file, 0=all */
/* mblks is rounded to next sector */
/* chunks is the number of sectors to wrote at a time 1-8 */
/* rem is byte excess in last sector based on program size */
/* sysinit reads exact bytes from file.  If rounded we get RM82 error */
int writefile(FILE *tp, char *fnp, uint32 mblks, int32 chunks, int32 rem) {
    uint32 word, blks=mblks;            /* just a temp word variable */         
    uint32 size, bsize, csize;          /* size in 768 byte sectors */
    FILE *fp;
    int32 n1, n2, hc, nw, cs;

    /* setting rem to 0 will always write full sectors */
    /* doing partial sectors did not fix anything */
    rem = 0;                            /* do not use remainders */
    memset((char *)data, 0, sizeof(data));  /* zero data storage */
    /* write file to tape */
    if ((fp = fopen(fnp, "r")) == NULL) {
        fprintf(stderr, "error: can't open user file %s\n", fnp);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);                 /* seek to end */
    word = ftell(fp);                       /* get filesize in bytes */
printf("MPX file %s is %x (%d) bytes rem %x (%d) mblks %x (%d)\n", fnp, word, word, rem, rem, mblks, mblks);
    fseek(fp, 0, SEEK_SET);                 /* rewind file */
    size = (word/768);                      /* filesize in sectors */
    if (word%768 != 0)                      /* see if bytes left over */
        size += 1;                          /* partial sector, add 1 */
    /* size has #768 blocks in file */
    if (mblks == 0) {
//      mblks = (word/768);                 /* blocks */
//      if ((word%768) != 0)                /* round up blks if remainder */
//          mblks++;                        /* total block to write */
        mblks = size;                       /* use file size */
        rem = 0;                            /* no rem if no mblks */
    }
    if (rem != 0) {
        mblks--;                            /* last block will be rem bytes */
    }
printf("MPX0 file %s is %x (%d) bytes mblks %d chunk %d rem %d\n",
    fnp, word, word, mblks, chunks, rem);
//  blks = mblks/chunks;                    /* chunks */
//  if (mblks%768 != 0)                     /* see if blks left over */
//      blks += 1;                          /* partial blks, add 1 */
/// bsize = mblks/chunks;                   /* chunks */
/// if (mblks%chunks != 0)                  /* see if blks left over */
///     bsize += 1;                         /* partial blks, add 1 */

/// blks = bsize;                           /* save # blks */
    bsize = mblks;                          /* save # blks */

printf("MPX1 file %s is %x (%d) bytes mblks %d bsize %d rem %d\n",
    fnp, word, word, mblks, bsize, rem);
    csize = 0;
    /* read in the image file */
    while (bsize > 0) { 
        if ((int32)bsize > chunks)          /* see if there is a chunk left to read */
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
        memset((char *)data, 0, csize*768); /* zero data storage */
        if ((bsize <= 0) && (rem != 0)) {
            fprintf(stderr, "writing last block to %s rem = %x (%d)\n", fnp, rem, rem);
            cs = fread((char *)data, rem, 1, fp);   /* read last "real" bytes */
            /* we have data to write */
            hc = (rem + 1) & ~1;            /* make byte count even */
            /* write actual byte count to 32 bit word as header */
            n1 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), tp);
            /* write the data mod 2 */
            nw = fwrite((unsigned char *)data, 1, (size_t)hc, tp);
            /* write the byte count in 32 bit word as footer */
            n2 = fwrite((char *)(&hc), 1, (size_t)sizeof(hc), tp);
            fprintf(stderr, "done with last block to %s rem %x n1 %x nw %x n2 %x\n", fnp, rem, n1, nw, n2);
            if (n1 != sizeof(hc) || nw != hc || n2 != sizeof(hc))
            {
                fprintf(stderr, "write to %s failure\n", fnp);
                fprintf(stderr, "Operation aborted\n");
                exit(1);
            }
        }
    }
printf("write file %s (size %d bytes) (%d sect) (%d blocks) (%d chunks) (%d rem)\n",
    fnp, word, size, mblks, blks, rem);
    fclose(fp);
    return(0);
}

uint32 readboot(char *name, char *buf, uint32 start, uint32 end) {
    int32 word = end-start+4;               /* just a temp word variable */         
    FILE *fp;
    int32 n1, n2;

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
uint32 flip(uint32 val) {
    /* byte swap the buffers for dmap and umap */
    return (((val & 0xff) << 24) | ((val & 0xff00) << 8) |
            ((val & 0xff0000) >> 8) | ((val >> 24) & 0xff));
}

/* get number of 768 byte blocks in file */
/* returned number of sectors is rounded up */
/* rem is bytes in last sect */
uint32 getblks(char *imgp, uint32 *rem)
{
    unsigned int word;                      /* just a temp word variable */         
    FILE *fp;
    int32 n1, n2, w2, blks;

    memset((char *)M, 0, 768);              /* zero data storage */
    if ((fp = fopen(imgp, "r")) == NULL) {
        fprintf(stderr, "error: can't open image file %s\n", imgp);
        exit(1);                            /* we are done here */
    }
    fseek(fp, 0, SEEK_END);                 /* seek to end */
    word = ftell(fp);                       /* get filesize in bytes */
printf("image file %s is %x (%d) bytes\n", imgp, word, word);
    fseek(fp, 0, SEEK_SET);                 /* rewind file */
    w2 = fread((char *)M, sizeof(uint32), 192, fp);  /* read the image file */
    n1 = flip(M[0x68/4]);                   /* get PR.BYTDR bytes in dsect rel matrix */
    n2 = flip(M[0x64/4]);                   /* get PR.SFADR sec addr of rel matrix */
    if (n2 == 0) {                          /* if zero use PR.SFAD rel addr of dsect */
        n1 = flip(M[0x5C/4]);               /* get PR.BYTED bytes in dsect */
        n2 = flip(M[0x58/4]);               /* get PR.SFAD sec addr of dsect */
        n2 += 1;                            /* add 1 blk for sys debug blk */
    }
    blks = n1/768;                          /* get #block rounded mod 768 */
    *rem = n1%768;                          /* get excess bytes */
    if ((n1%768) != 0)                      /* round up blks if remainder */
        blks++; 
    blks += n2;                             /* get total blks to read */
printf("image file %s n1 %x (%d) n2 %x (%d) blks %x (%d) rem %x (%d)\n",
    imgp, n1, n1, n2, n2, blks, blks, *rem, *rem);
    fseek(fp, 0, SEEK_SET);                 /* rewind file */
    fclose(fp);

    return blks;
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
//  unsigned char volname[32];
//  unsigned char dirname[32];
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
#define DOMSTR 2048
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
//  int ofd;                                /* output file number */
    int32 filen;                            /* file number */
//  uint32 *dirp = dir;                     /* directory entry pointer */
//  uint32 *volp = vol;                     /* volume entry pointer */
//  char *dirp = dirname;                   /* directory entry pointer */
//  char *volp = volname;                   /* volume entry pointer */
    char *dirp = sysname;                   /* directory entry pointer */
    char *volp = sysname;                   /* volume entry pointer */
    int32 totent;                           /* total smd entries */
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
                    case 'm':
                    case 'M':
                        option |= DOMSTR;   /* output master SDT */
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
            "Usage: %s [-ptloam] [-bboot] [-iimage] [-jj.vfmt] [-uusername] vmgrtape file1 file2 ...\n",
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
//printf("1 opened output in w mode, write at start\n");
                }
//else
//printf("2 opened output in r+ mode write at end\n");

                fseek(dp, 0, SEEK_END);             /* seek to end */
                bytes = ftell(dp);                  /* get filesize in bytes */
printf("1 file length %ld %lx bytes\n", bytes, bytes);
//67 printf("1 start writing at %ld %lx bytes offset\n", bytes-8, bytes-8);
printf("1 start writing at %ld %lx bytes offset\n", bytes-4, bytes-4);
                fseek(dp, 0, SEEK_SET);             /* rewind file to beginning */
                /* at this point, we are at the end of the tape */
                /* we should see 3 EOF's w/ or w/o an EOM */
                if (bytes > 8) {                    /* see if file written to already */
                    /* we need to find the EOT */
                    int32 n1, n2, hc, tc, n;
                    int EOFcnt = 0;
readmore:
                    fseek(dp, bytes-4, SEEK_SET);   /* seek back to EOF/EOM code */
                    n1 = fread((char *)(&hc), 1, (size_t)4, dp);    /* read 4 byte record size */
                    if (n1 <=0)                     /* check for read error */
                        goto doabort;               /* bad tape format */

printf("2 file length %ld %lx bytes\n", bytes, bytes);
//67printf("2 start writing at %ld %lx bytes offset\n", bytes-8, bytes-8);
printf("2 start writing at %ld %lx bytes offset\n", bytes-4, bytes-4);
                    if (hc & 0xffff0000)            /* check for garbage */
                        hc = 0;                     /* assume EOF on disk */

                    if (hc == 0) {                  /* check for EOF on file */
                        /* EOF found */
                        if (++EOFcnt == 2) {
                            /* we have second EOF, we need to backup 4 bytes */
//backup4:
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
        int32 w2, n1, n2, nw, hc, blks, rem;
        fnp = bootp;                                /* get file name pointer */
        memset((char *)data, 0, 0x800);              /* zero data storage */
#define USE_FILENAME
#ifdef USE_FILENAME
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
        fclose(fp);
#else
        /* go cut the boot code from the volmgr load module */
//      w2 = readboot("volmgr", (char *)data, 0x1c9a0, 0x1d144);    /* 7a4 volmgr 3.4+ */
//      w2 = readboot("volmgr", (char *)data, 0x1c9a0, 0x1d140);    /* 7a0 volmgr ?? */
        w2 = readboot("volmgr", (char *)data, 0x24860, 0x24f48);    /* 6ec volmgr 3.5u02 */
        word = w2;                                  /* get filesize in bytes */
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
        memset((char *)data, 0, 0x800);             /* zero data storage */

        if (option & DOMSTR) {
            /* get blocks in image file */
            blks = getblks(mstrall, &rem);
            /* write 1st mpx image file */
            writefile(dp, mstrall, blks, 1, rem);   /* write max of "blks" blocks to file */

            /* write EOF (zero) to file */
            filen = 0;                              /* zero count */
            fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);

            blks = getblks(mstrext, &rem);
            /* write 2nd mpx image file */
            writefile(dp, mstrext, blks, 1, rem);   /* write max of "blks" blocks to file */

            /* write EOF (zero) to file */
            filen = 0;                              /* zero count */
            fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);

            blks = getblks(mstrout, &rem);
            /* write 3rd mpx image file */
            writefile(dp, mstrout, blks, 1, rem);   /* write max of "blks" blocks to file */

            /* write EOF (zero) to file */
            filen = 0;                              /* zero count */
            fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
        } else {
            /* get blocks in image file */
            blks = getblks(imgp, &rem);

            /* write mpx image file */
            writefile(dp, imgp, blks, 1, rem);      /* write max of "blks" blocks to file */
        }
        /* get blocks in j.vfmt file */
        blks = getblks(vfmtp, &rem);
        /* write j.vfmt file */
        writefile(dp, vfmtp, blks, 1, rem);         /* write 1 blk at a time */
//      writefile(dp, vfmtp, 0, 1);                 /* write 1 blk at a time */

        /* write EOF (zero) to file */
        filen = 0;                                  /* zero count */
        fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);

        /* write j.mount file */
        blks = getblks("j.mount", &rem);
        writefile(dp, "j.mount", blks, 1, rem);     /* one blk at a time */
//      writefile(dp, "j.mount", 0, 1);             /* one blk at a time */

        /* write j.swapr file */
        blks = getblks("j.swapr", &rem);
        writefile(dp, "j.swapr", blks, 1, rem);     /* one blk at a time */
//      writefile(dp, "j.swapr", 0, 1);             /* one blk at a time */

        /* write volmgr file */
        blks = getblks("volmgr", &rem);
        writefile(dp, "volmgr", blks, 1, rem);      /* one blk at a time */
//      writefile(dp, "volmgr", 0, 1);              /* all of file 1 blk at a time */

        /* write EOF (zero) to file */
        filen = 0;                                  /* zero count */
        fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);

        /* do second EOF */
        fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);

        /* do third EOF */
        fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
        filen = -1;                                 /* make in -1 for EOM */
        /* do EOM */
        fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
printf("setting at %ld bytes in file after EOM\n", ftell(dp));
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
    savecnt = 0;                                /* no files "yet */
    /* make first pass over files and get filenames and sizes */
    /* got tapefile and options, handle files now */
    targc = argc;                               /* save argc to reread list */
    targv = argv;                               /* save argv to reread list */
printf("AT 3 argc %d argv %s\n", argc, *argv);
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
//  dirp = (uint32 *)dir;                       /* get word pointer for directory */
//  volp = (uint32 *)vol;                       /* get word pointer for volume */
//  dirp = dirname;                             /* get word pointer for directory */
//  volp = volname;                             /* get word pointer for volume */
//  dirp = sysname;                             /* get word pointer for directory */
    volp = sysname;                             /* get word pointer for volume */
    n = 2;
    dirlist[0] = n << 24;                       /* set record type to 1 */
    while (--argc > 0) {
        uint32 smd[32];                         /* dir entry data */
//      int blks, eof;

        for (i=0; i<16; i++)                    /* zero smd entry */
            smd[i] = 0x20;                      /* make blank */
        p = *argv++;
        i = strlen(p);                          /* filename size */
        if (i == 0 || i > 16) {
            fprintf(stderr, "error: Filename too long (%d>16) %s, Aborting\n", i, p);
            exit(1);
        }
printf("argc %d argv3 %s\n", argc, p);
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

printf("AT write file list with %d entries\n", filen);
    /* dirlist now has 1-127 filename entries to save */
    /* write out the directory entries for the files to save */
    if (filen != 0) {
        /* we need to write out the directory entries */
        int32 n1, n2, nw;
        int32 hc = (6144 + 1) & ~1;             /* make byte count even */
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
//      int32 n1, n2, nw, k;
        int32 n1, n2, nw;
        int32 hc = (1536 + 1) & ~1;             /* make byte count even */
        int blks, rem;

        p = *argv++;
printf("at 4 argc %d argv %s\n", argc, p);
        blks = getblks(p, &rem);

        if ((fp = fopen(p, "r")) == NULL) {
            fprintf(stderr, "error: can't open user file %s\n", p);
            exit(1);
        }
        fnp = p;                                /* get file name pointer */

        if (typ != 0xca) {
            /* we need to write the resource descriptor in 2 blks to file */
            /* followed by file content is 8 blk chunks */
            fseek(fp, 0, SEEK_END);             /* seek to end */
            word = ftell(fp);                   /* get filesize in bytes */
            fseek(fp, 0, SEEK_SET);             /* rewind file */
            size = (word/768);                  /* filesize in sectors */
            if (word%768 != 0)                  /* see if byte left over */
                size += 1;                      /* partial sector, add 1 */
        } else {
            size = blks;
        }
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
        resdes[0+2] = dirlist[n+0];             /* file */
        resdes[1+2] = dirlist[n+1];
        resdes[2+2] = dirlist[n+2];
        resdes[3+2] = dirlist[n+3];
        resdes[4+2] = dirlist[n+4];             /* directory, system*/
        resdes[5+2] = dirlist[n+5];
        resdes[6+2] = dirlist[n+6];
        resdes[7+2] = dirlist[n+7];
        resdes[8+2] = dirlist[n+8];             /* volume, system */
        resdes[9+2] = dirlist[n+9];
        resdes[10+2]= dirlist[n+10];
        resdes[11+2]= dirlist[n+11];
        /* 16 word resource create block */
        resdes[0+12+2] = dirlist[n+8];          /* RCB.OWNR owner, system */
        resdes[1+12+2] = dirlist[n+9];          /* only 8 bytes */
        resdes[2+12+2] = dirlist[n+8];          /* RCB.USER group, system */
        resdes[3+12+2] = dirlist[n+9];          /* only 8 bytes */
//SS    n += 12;
        resdes[4+12+2]=flip(0x80f00000);        /* RCB.OWRI owner rights */
        resdes[5+12+2]=flip(0x80b00000);        /* RCB.UGRI group rights */
        resdes[6+12+2]=flip(0x80800000);        /* RCB.OTRI other rights */
        if (typ == 0xca)
            resdes[7+12+2]=flip(0x00040110);    /* res mgmt flags */
        else
            resdes[7+12+2]=flip(0x00040110);    /* res mgmt flags */
//          resdes[21]=flip(0x0004011c);        /* res mgmt flags */
        resdes[11+12+2]=flip(285);              /* RCB.OSIZ org dir (0x120) size */
//      resdes[11+12+2]=flip(size);             /* org file size */
        resdes[13+12+2]=flip(0x53480);          /* RCB.FAST Res ID buffer address n/u */
//      resdes[27]=flip(1000);                  /* file starting address n/u */
        resdes[15+12+2]=flip(0x00fbfeef);       /* RCB.OPTS option flags */
//      resdes[29]=flip(0x00fbfeef);            /* RCB.OPTS option flags */
        /* rest of block is zero */

        /* second block is resosurce descriptor from disk M.RDCOM */
        resdes[0+192] = dirlist[n+8];           /* RD.IDNAM volume, system */
        resdes[1+192] = dirlist[n+9];
        resdes[2+192]= dirlist[n+10];
        resdes[3+192]= dirlist[n+11];

        resdes[4+192]=flip(0x00003190);         /* RD.DATE creation date */
        resdes[5+192]=flip(0x0e8c8000);         /* RD.TIME creation time */
        resdes[6+192]=flip(0x000003c0);         /* RD.DOFF abs blk of res des */
        resdes[7+192]=flip(0x0000000a);         /* RD.RDFLG / RD.RTYPE resource type, perm file */

        resdes[8+192]=flip(0x000029cf);         /* RD.CRDAT creation date */
        resdes[9+192]=flip(0x1dd8e074);         /* RD.CRTIM creation time */
        //202 10+192 RD.XPDAT
        //203 11+192 RD.XPTIM

        //204 12+192 RD.RDDAT
        //205 13+192 RD.RDTIM
        resdes[14+192]=flip(0x000029cf);        /* RD.CHDAT last chge date */
        resdes[15+192]=flip(0x1dd8e074);        /* RD.CHTIM last chge time */

        resdes[16+192]=flip(0x00003190);        /* RD.SVDAT last save date */
        resdes[17+192]=flip(0x0e8c8000);        /* RD.SVTIM last save time */
        //210 18+192 RD.RSDAT
        //211 19+192 RD.RSTIM

        resdes[20+192] = dirlist[n+8];          /* RD.CHOWN ownername last changer, system */
        resdes[21+192] = dirlist[n+9];
        resdes[22+192] = dirlist[n+8];          /* RD.CROWN ownername creator, system */
        resdes[23+192] = dirlist[n+9];

        //216 24+192 RD.RDCNT
        //217 25+192 RD.AFLGS
        resdes[26+192] = dirlist[n+8];          /* RD.OWNR ownername of resource, system */
        resdes[27+192] = dirlist[n+9];

        resdes[28+192] = dirlist[n+8];          /* RD.UGRP group of resource, system */
        resdes[29+192] = dirlist[n+9];
        resdes[30+192]=flip(0xf8400000);        /* RD.AOWNR owner access */
        resdes[31+192]=flip(0xf8400000);        /* RD.AUGRP group access */
//      resdes[223]=flip(0xf8e00000);           /* RD.AUGRP group access */
        resdes[32+192]=flip(0x80000000);        /* RD.AOTHR other access */
        //225 33+192 reserved
        resdes[34+192]=flip(0x00000001);        /* RD.LNKCT resource link count */
        //227 35+192 Port numbers

        //228-244  
        resdes[52+192] = dirlist[n+8];          /* RD.RDOWN ownername at last access, system */
        resdes[53+192] = dirlist[n+9];
        //247-255  

        resdes[64+192]=flip(0xca1000f0);        /* RD.SFLGS space definition flags */
        if (typ == 0xca)
            resdes[64+192]=flip(0xca1000f0);    /* RD.SFLGS space definition flags */
        else
        if (typ == 0xee)
            resdes[64+192]=flip(0xee1000f1);    /* RD.SFLGS space definition flags */
        else
        if (typ == 0x00)
            resdes[64+192]=flip(0x001000f1);    /* RD.SFLGS space definition flags */
        resdes[65+192]=flip(0x00000040);        /* RD.MXEXT max extends */
        resdes[66+192]=flip(0x00000010);        /* RD.MNEXT min incr */
        //259 67+192 RD.MXSIZ

        resdes[68+192]=flip(size-1);            /* RD.EOFBL eof block */
        resdes[69+192]=flip(size);              /* RD.EOMBL eom block */
        resdes[70+192]=flip(0x00000001);        /* RD.NUMSG # of segment */
        //263 71+192 RD.XSABA

        resdes[72+192]=resdes[6];               /* RD.DNAME directory, system*/
        resdes[73+192]=resdes[7];
        resdes[74+192]=resdes[8];
        resdes[75+192]=resdes[9];

        resdes[76+192]=flip(0x00000cd6);        /* RD.PAREN parent blk number */
        resdes[77+192]=flip(0x00000001);        /* RD.NUMCR segments at creation  */
        //270 78+192 reserved
        //271 79+192 reserved

        resdes[80+192]=resdes[2];               /* RD.DIRP filename*/
        resdes[81+192]=resdes[3];
        resdes[82+192]=resdes[4];
        resdes[83+192]=resdes[5];

        resdes[84+192]=flip(0x00000100);        /* RD.DADD parent blk number */
        resdes[85+192]=flip(0x000005c0);        /* RD.DIDX parent dir index number */
        //278 86+192 res
        // to
        //287 95+192 res

        /* Segment definitions (RD.SEGDF) Wds 96/97 */ 
        resdes[96+192]=flip(0x0000fda8);        /* file abs blk addr */
        resdes[97+192]=flip(size);              /* eom block */

        /* all others are zero */
        n += 12;                                /* next directory entry */

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
        if (typ != 0xca) {
            writefile(dp, fnp, 0, 8, 0);        /* all of file 8 blk at a time */
        } else {
            writefile(dp, fnp, size, 8, 0);     /* all of file 8 blk at a time */
        }

printf("File written at 4 argc %d argv %s\n", argc, fnp);

        /* write EOF (zero) to file */
        eof = 0;                                /* zero count */
        fwrite((char *)(&eof), 1, (size_t)sizeof(eof), dp);
    }
    /* we have saved the files in the image, write 2 eof's & 1 EOM */
    /* write EOF (zero) to file */
    filen = 0;                                  /* zero count */
    fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
    /* do second EOF */
    fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
    filen = -1;                                 /* make in -1 for EOM */
    /* do EOM */
    fwrite((char *)(&filen), 1, (size_t)sizeof(filen), dp);
printf("setting at %lx (%ld) bytes in file after EOM\n", ftell(dp), ftell(dp));
    fclose(dp);
    exit(0);
}
