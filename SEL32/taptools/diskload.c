/*
 * diskload.c
 *
 * This program reads an MPX load module and stores it into the
 * sumulated diskfile.  The SMD entry is changed to reflect the
 * new file entry.
 * input - filename  
 *       - simulated diskfile
 * output - modified diskfile with SMD entry modified
 * 08/11/2018
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define BLKSIZE 768                 /* MPX file sector size */
unsigned char os[BLKSIZE * 100];    /* O/S code */

/* read program file and output to simulated mpx1.x diskfile using filemgr format */
/* dload file diskfile */
int main(argc, argv)
int argc;
char *argv[];
{
    FILE    *fp, *dp, *fopen();
    unsigned char si[BUFSIZ];
    char *p;
    int i, j;
#define DOLIST 1
#define DOADD  2
    unsigned int option = DOLIST;   /* what to do */
    unsigned char *fnp;             /* file name pointer */
    unsigned int size;              /* size in 768 byte sectors */
    unsigned int word,tmp;          /* just a temp word variable */         
    unsigned char name[9];          /* LM name */
    unsigned int lmname[2], passwd[2];
    unsigned int smds;              /* size of smd */
    unsigned int smdd[2];           /* SMD space definition */
    unsigned char *smd;
    unsigned int hash1;             /* SMD hash value for name */
    unsigned int udtp;              /* UDT pointer */
    unsigned int *mptr;             /* word ptr for SMD entries */
    unsigned int boff;
    unsigned int rem;
    unsigned int blk;
    unsigned int lmblk;             /* sector addess of load module on disk */
    unsigned int smdudt;            /* smd udt index */
    unsigned int spau;              /* sectors per allocation unit */
    unsigned int fileau;            /* file size in allocation units */
//  unsigned int auptr;             /* pointer to allocation unit on disk */
    unsigned int lsblk;             /* lowest block address of SMD entries */

    if (argc <= 1) {                    /* see if correct # args */
//      fprintf(stderr, "Error: incorrect number of parameters\n");
        fprintf(stderr, "Usage: %s -la program diskfile\n", *argv);
        exit(1);
    }
    if (--argc > 0) {
        p = *++argv;
//      printf("argv %s\n", p);
        if (*p++ == '-') {
            while (*p != '\0') {
                switch (*p++) {
                    case 'l':
                    case 'L':
                        option |= DOLIST;
                        break;
                    case 'A':
                    case 'a':
                        option |= DOADD;
                        break;
                    default:
//                      fprintf(stderr, "Error: incorrect option %c specified\n", *--p);
                        fprintf(stderr, "Usage: %s -la program diskfile\n", *--argv);
                        exit(1);
                        break;
                }
            }
            if (*--p == '-') {
                fprintf(stderr, "Error: no option specified\n");
                fprintf(stderr, "Usage: %s -la program diskfile\n", *--argv);
                exit(1);
            }
        } else {
            argc++;
            argv--;
        }
        if (--argc > 0) {
            p = *++argv;
//          printf("argv2 %s\n", p);
            if ((fp = fopen(*argv, "r")) == NULL) {
                fprintf(stderr, "error: can't open load module %s\n", *argv);
                exit(1);
            }
            fnp = *argv;                /* get file name pointer */
            fseek(fp, 0, SEEK_END);     /* seek to end */
            word = ftell(fp);           /* get filesize in bytes */
            fseek(fp, 0, SEEK_SET);     /* rewind file */
        }
    }
    if (--argc <= 0) {
        fprintf(stderr, "Error: incorrect number of parameters\n");
        fprintf(stderr, "Usage: %s -la program diskfile\n", *argv);
        exit(1);
    }
    /* open diskfile */
    if ((dp = fopen(*++argv, "r+")) == NULL) {
        fprintf(stderr, "error: can't open disk file %s\n", *argv);
        exit(1);
    }

    /* determine size of file */
    size = word/BLKSIZE;        /* get sector count */
    if ((word % BLKSIZE) != 0)
        size++;                 /* add in 1 for partial sector */
    /* size now has load module size in sectors */
    /* we need the file name and password for entry */
    for (i=0; i<8; i++)
        name[i] = 0x20;         /* pre blank name */
    name[8] = '\0';             /* NULL terminate */
    for (i=0; (i<8) && (fnp[i] != 0); i++)
        name[i] = toupper(fnp[i]);  /* upper case name */
//  fprintf(stderr, "file %s is size %d sectors\n", name, size);
    lmname[0] = (name[0] << 24) | (name[1] << 16) | (name[2] << 8) | (name[3]);
    lmname[1] = (name[4] << 24) | (name[5] << 16) | (name[6] << 8) | (name[7]);
    passwd[0] = passwd[1] = 0;          /* no password */
    /* the disk data has the O/S starting at 0x780 in the file */
    /* add 0x780 to any O/S location you want to load */
    /* C.SMDS (#smd entries) is at 0xb00 + 0x780 = 0x1280 */
    /* read the O/S diskfile into buffer, 7680 bytes */
//  fprintf(stderr, "reading 100*BLKSIZE bytes from disk\n", 100*BLKSIZE);
    if (fread(os, 1, BLKSIZE*100, dp) <= 0)
        return (0);                 /* this means bad read */
    smds = (os[0x780+0xb00] << 24) | (os[0x780+0xb01] << 16) | (os[0x780+0xb02] << 8) | (os[0x780+0xb03]);

    /* get udt index for SMD location */
    smdudt = (os[0x780+0xc2c] << 8) | (os[0x780+0xc2d]);
    /* udt size id 0x40, C.UDTA is at 0xb40 */
    word = (os[0x780+0xb40] << 24) | (os[0x780+0xb41] << 16) | (os[0x780+0xb42] << 8) | (os[0x780+0xb43]);
    udtp = (smdudt * 0x40 + word);  /* get udt address */
    /* spau is in byte 0x0f of udt */
    spau = os[0x780+udtp+0x0f] & 0xff;      /* get word with spau */

    /* get file size in au's */
    fileau = size / spau;           /* get number of au's required for file */
    if ((size % spau) != 0)
        fileau++;                   /* round up to next au of overflow */
    fprintf(stderr, "file %s is size %d sectors (%d au) requiring %d sectors on disk\n",
                    name, size, fileau, fileau*spau);
//  fprintf(stderr, "UDTP 0x%x SPAU 0x%x\n", udtp, spau);   /* display udtp & spau */
    fprintf(stderr, "C.SMDS(0xB00) - SMD size is 0x%x %d entries C.SMDUDT is %x\n", smds, smds, smdudt);

    /* get SMD space definition */
    smdd[0] = (os[0x780+0x840] << 24) | (os[0x780+0x841] << 16) | (os[0x780+0x842] << 8) | (os[0x780+0x843]);
    smdd[1] = (os[0x780+0x844] << 24) | (os[0x780+0x845] << 16) | (os[0x780+0x846] << 8) | (os[0x780+0x847]);
    fprintf(stderr, "C.SMDD(0x840) - SMD space definition wd0 0x%x wd1 0x%x wd0 %d wd1 %d entries %d\n",
                    smdd[0], smdd[1], smdd[0] ,smdd[1], smdd[1] * 24);
    fprintf(stderr, "SMD space def: blk 0x%x seek 0x%x size 0x%x (%d)\n",
                    smdd[0], smdd[0]*BLKSIZE, smdd[1], smdd[1]);

    /* rewind the disk */
    fseek(dp, 0, 0);                /* seek home */
    /* seek to the smd */
    fseek(dp, smdd[0]*BLKSIZE, 0);  /* seek smd */
    if ((smd = (unsigned char *)malloc(smdd[1] * BLKSIZE)) < 0)
        return (0);                 /* this means bad read */
    /* read the SMD into memory */
    if (fread(smd, 1, smdd[1]*BLKSIZE, dp) <= 0)
        return (0);                 /* this means bad read */
    /* use lowest space definition from SMD entries */
    lsblk = smdd[0];                /* set lowest to start of SMD */
    /* loop through the SMD looking for a match to the load module name */
    for (i=0; i<((int)smds*32); i+=32) {
        unsigned int sblk, len, udt, pw;
        int yep = 0;
        for (j=0; j<8; j++) {
            if (smd[i+j] != 0) {
                fprintf(stderr, "%c", smd[i+j]);
                yep = 1;
            }
        }
        if (yep) {
            int bit;
            unsigned int lmn[2];        /* load module name */

            sblk = (smd[i+8] << 24) | (smd[i+9] << 16) | (smd[i+10] << 8) | (smd[i+11]);
            if ((sblk & 0xffffff) < lsblk)
                lsblk = sblk & 0xffffff;/* save the new lowest address */
            len = (smd[i+12] << 24) | (smd[i+13] << 16) | (smd[i+14] << 8) | (smd[i+15]);
            pw = (smd[i+24] << 8) | (smd[i+25]);
            udt = (smd[i+26] << 8) | (smd[i+27]);

            /* hash the name to see if pointer is to current SMD entry */
            mptr = (unsigned int *)smd; /* get word pointer for SMD data */
            j = i/32;                   /* offset in SMD file (entry #) */
            lmn[0] = mptr[j*8];         /* get filename first 4 chars to R5 and shift right circular by 1 */
            word = ((lmn[0] & 0xff) << 24) | ((lmn[0] & 0xff00) << 8) |
                    ((lmn[0] & 0xff0000) >> 8) | ((lmn[0] & 0xff000000) >> 24);
            bit = word & 1;             /* save bit 31 */
            word = word >> 1;           /* shift right 1 */
            if (bit)
                word |= 0x80000000;     /* copy old bit 31 to bit 0 */
            lmn[1] = mptr[j*8+1];       /* get 2nd 4 chars of filename */
            rem = ((lmn[1] & 0xff) << 24) | ((lmn[1] & 0xff00) << 8) |
                    ((lmn[1] & 0xff0000) >> 8) | ((lmn[1] & 0xff000000) >> 24);
            word = word ^ rem;          /* EOR with 2nd half of load module name */
            rem = word % smds;          /* get remainder as relative entry number */
            fprintf(stderr, " entry %d (calc %d) off 0x%x typ 0x%x blk 0x%x len 0x%x, pw 0x%x udt 0x%x\n",
                j, rem, (sblk & 0xffffff)*BLKSIZE, sblk>>24, sblk, len, pw, udt);
//          fprintf(stderr, "offset index %d calc %d\n", i/32, rem);    /* display entry # */
        }
    }
  
    /* see if user want list only */
    if ((option & DOLIST) && !(option & DOADD))
        goto doexit;        // getout

#if 0
    /* get file size in au's */
    fileau = size / spau;           /* get number of au's required for file */
    if ((size % spau) != 0)
        fileau++;                   /* round up to next au of overflow */
#endif

    /* lsblk has the lowest space definition from SMD entries */
    lmblk = lsblk - fileau * spau;  /* get sector address of where to store lm */
    fprintf(stderr, "lmblk 0x%x lsblk 0x%x fileau 0x%x spau 0x%x\n", lmblk, lsblk, fileau, spau);

    /* hash filename into SMD entry number */
    fprintf(stderr, "1 hashing lmname[0] %x lmname[1] %x\n", lmname[0], lmname[1]);
    word = lmname[0];           /* get filename first 4 chars to R5 and shift right circular by 1 */
    i = word & 1;               /* save bit 31 */
    word = word >> 1;           /* shift right 1 */
    if (i)
        word |= 0x80000000;     /* copy old bit 31 to bit 0 */
    word = word ^ lmname[1];    /* EOR with 2nd half of load module name */
    hash1 = word / smds;        /* calc SMD hash number and save */
    rem = word % smds;          /* get remainder as relative entry number */

    /* get relative 192W block in SMD */
    blk = rem/24;               /* divide entry # by # entries/block */
    boff = (rem%24) * 32;       /* get byte offset in block of entry */
    tmp = (blk * 24 + (boff/32));   /* make word offset in smd array */ 
    fprintf(stderr, "hash1 %x rem %x blk 0x%x %d boff %x index %d\n", hash1, rem, blk, blk, boff, tmp);
    /* populate the 32 byte SMD entry */
    /* smd has entire SMD in memory */
    tmp = (blk * BLKSIZE + boff) >> 2;  /* make word offset in smd array */ 
    mptr = (unsigned int *)smd; /* get word pointer for smd data */
    /* word 1 and 2 has filename */
    blk = lmname[0];            /* 1st 4 char of lmname */
    word = ((blk & 0xff) << 24) | ((blk & 0xff00) << 8) | ((blk & 0xff0000) >> 8) | ((blk & 0xff000000) >> 24);
    mptr[tmp] = word;           /* set 1st 4 char of name */

    blk = lmname[1];            /* set 2nd 4 char of name */
    word = ((blk & 0xff) << 24) | ((blk & 0xff00) << 8) | ((blk & 0xff0000) >> 8) | ((blk & 0xff000000) >> 24);
    mptr[tmp+1] = word;         /* set 2nd 4 char of name */

//  blk = 0xca000000 | lmblk;   /* type ca and blk address */
    blk = 0xee000000 | lmblk;   /* type ca and blk address */
    word = ((blk & 0xff) << 24) | ((blk & 0xff00) << 8) | ((blk & 0xff0000) >> 8) | ((blk & 0xff000000) >> 24);
    mptr[tmp+2] = word;         /* set type and blk addr */
    /* size now has load module size in sectors */
    blk = 0x80000000 | size;    /* file flags and size */
    word = ((blk & 0xff) << 24) | ((blk & 0xff00) << 8) | ((blk & 0xff0000) >> 8) | ((blk & 0xff000000) >> 24);
    mptr[tmp+3] = word;         /* set flags and size */
    mptr[tmp+4] = 0;            /* no username */
    mptr[tmp+5] = 0;            /* no username */
    blk = 0x00000000 | smdudt;  /* no password and smd udt */
    word = ((blk & 0xff) << 24) | ((blk & 0xff00) << 8) | ((blk & 0xff0000) >> 8) | ((blk & 0xff000000) >> 24);
    mptr[tmp+6] = word;         /* set password and udt index */
    mptr[tmp+7] = 0;            /* not used */

    /* loop through the SMD looking for a match to the load module name */
    for (i=0; i<((int)smds*32); i+=32) {
        unsigned int sblk, len, udt, pw;
        int yep = 0;
        for (j=0; j<8; j++) {
            if (smd[i+j] != 0) {
                fprintf(stderr, "%c", smd[i+j]);
                yep = 1;
            }
        }
        if (yep) {
            int bit;
            unsigned int lmn[2];    /* load module name */
            sblk = (smd[i+8] << 24) | (smd[i+9] << 16) | (smd[i+10] << 8) | (smd[i+11]);
            len = (smd[i+12] << 24) | (smd[i+13] << 16) | (smd[i+14] << 8) | (smd[i+15]);
            pw = (smd[i+24] << 8) | (smd[i+25]);
            udt = (smd[i+26] << 8) | (smd[i+27]);
#if 0
            fprintf(stderr, " entry %d blk # 0x%x off 0x%x len 0x%x, pw 0x%x udt 0x%x\n",
                            i/32, sblk, (sblk & 0xffffff) * BLKSIZE, len, pw, udt);
            /* udt size id 0x40, C.UDTA is at 0xb40 */
            word = (os[0x780+0xb40] << 24) | (os[0x780+0xb41] << 16) | (os[0x780+0xb42] << 8) | (os[0x780+0xb43]);
            udtp = (udt * 0x40 + word); /* get udt address */
            fprintf(stderr, "UDTP 0x%x\n", udtp);   /* display word */
            /* spau is in byte 0x0f */
            spau = os[0x780+udtp+0x0f] & 0xff;          /* get word with spau */
            fprintf(stderr, "SPAU 0x%x\n", spau);   /* display spau */
#endif

            /* hash the name to see if pointer is to current SMD entry */
            mptr = (unsigned int *)smd; /* get word pointer for SMD data */
            j = i/32;                   /* offset in SMD file (entry #) */
            lmn[0] = mptr[j*8];         /* get filename first 4 chars to R5 and shift right circular by 1 */
            word = ((lmn[0] & 0xff) << 24) | ((lmn[0] & 0xff00) << 8) |
                    ((lmn[0] & 0xff0000) >> 8) | ((lmn[0] & 0xff000000) >> 24);
            bit = word & 1;             /* save bit 31 */
            word = word >> 1;           /* shift right 1 */
            if (bit)
                word |= 0x80000000;     /* copy old bit 31 to bit 0 */
            lmn[1] = mptr[j*8+1];       /* get 2nd 4 chars of filename */
            rem = ((lmn[1] & 0xff) << 24) | ((lmn[1] & 0xff00) << 8) |
                    ((lmn[1] & 0xff0000) >> 8) | ((lmn[1] & 0xff000000) >> 24);
            word = word ^ rem;          /* EOR with 2nd half of load module name */
            rem = word % smds;          /* get remainder as relative entry number */
            fprintf(stderr, " entry %d (calc %d) off 0x%x typ 0x%x blk 0x%x len 0x%x, pw 0x%x udt 0x%x\n",
                j, rem, (sblk & 0xffffff)*BLKSIZE, sblk>>24, sblk, len, pw, udt);
//          fprintf(stderr, "offset index %d calc %d\n", i/32, rem);    /* display entry # */
        }
    }

    fprintf(stderr, "file space def: blk 0x%x seek 0x%x size 0x%x %d\n", lmblk, lmblk*BLKSIZE, size, size);
    if (option & DOADD) {       /* see if user wants file written to disk */
        /* write the load module to disk */
        /* rewind the disk */
        fseek(dp, 0, 0);                /* seek home */
        /* seek to where the load module is to be stored */
        fseek(dp, lmblk*BLKSIZE, 0);    /* seek sector for saving load module */
        /* rewind the load module */
        fseek(fp, 0, 0);                /* seek home */
        /* copy all of the sectors */
        for (i=0; i<(int)size; i++) {
            /* read a block of the load module */
            if  (fread(si, 1, BLKSIZE, fp) <= 0)
                return (0);         /* this means eof */
            /* write it to the disk file */
            if (fwrite(si, 1, BLKSIZE, dp) <= 0)
                return (0);         /* this means eof */
        }

        /* write the updated smd to disk */
        /* rewind the disk */
        fseek(dp, 0, 0);                /* seek home */
        /* seek to the smd */
        fseek(dp, smdd[0]*BLKSIZE, 0);  /* seek smd */
        /* write the SMD from memory to disk */
        if (fwrite(smd, 1, smdd[1]*BLKSIZE, dp) <= 0)
            return (0);                 /* this means bad write */
    }
doexit:
    fclose(fp);
    fclose(dp);
    exit(0);
}
