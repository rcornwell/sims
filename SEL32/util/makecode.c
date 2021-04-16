/*
 * makecode.c
 *
 * This program uncompresses an MPX compressed object file and
 * dumps the object records into a file named "testcode.mem" 
 * as absolute data.  This code can then be loaded into the
 * sel32 simulator using the "load testcode.mem" command.  The
 * assembler code must be assembled as asbolute code with
 * origin at 0.
 *
 * input - stdin or specified file name
 * output - file testcode.mem in local directory.
 *
 */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

//#undef DOTRACE
#define DOTRACE

//#define JUNK
#ifdef JUNK
//#define QUIET
//#define QUIET1
//#define QUIET8
#endif

#define uint32 unsigned int
#define int32 int
void codedump(int sect);
int rbl(unsigned char *buf, int n);
void putloi(unsigned char *s, int cnt);
int getloi(unsigned char *s, int lim);
int rmopen(int cfcb);
int rmclose(int cfcb);
int rmread(int cfcb, unsigned char *buffer);

int binary = 1;
int dounix;                     /* set if doing unix object */
int first;                      /* set after 1st time */
int ifd;                        /* input file number */

int main(int argc, char *argv[])
{
    unsigned char s[BUFSIZ];
    int i;

#ifndef JUNK
    setbuf(stdout, 0);
#endif

    if (argc == 1)
    {                           /* no file given, use stdin */
        ifd = fileno(stdin);
    }
    else
    {
        if ((ifd = open(*++argv, O_RDWR)) < 0)
        {
            fprintf(stderr, "file %s not found\n", *argv);
            exit(1);
        }
    }

    while (1)
    {
        memset(s, '\0', BUFSIZ);
        if ((i = rbl(s, (int)BUFSIZ)) <= 0)
            exit(0);
        s[i] = '\0';
        putloi(s, i);
    }
}

/* get a line of input. */

int getloi(s, lim)              /*  right from the book.  */
unsigned char s[];
int lim;
{
    int c, i, j;

    j = rmread(ifd, s);
    if (j <= 0)
        return (0);
    if (!binary)
    {
        s[j] = '\n';
        for (i = 0; --lim > 0 && i < j;)
        {
            c = s[i];

            s[i++] = c;
            if ((*s != 0xbf) && (*s != 0x9f))
            {
                if (c == '\n')
                {
                    if ((s[i - 1] == '\n') && (i > 1))
                    {
                        while ((s[i - 2] == ' ') && (i > 1))
                            --i;
                        s[i - 1] = '\n';
                    }
                    s[i] = '\0';
                    return (i);
                }
            }
        }
        return (i);
    }
    return (j);
}

int com_org[256];               /* common origin */
int com_size[256];              /* common size (mod 32 bytes) */
int com_base[256];              /* common base address in dsect */
int last_ds;                    /* endi of last common defined */
/* com_addr is always dsect sect_addr */
char comname[8];                /* name of common, blank filled */
int sect_org[256];              /* sect origin */
int sect_size[256];             /* sect size */
int sect_base[256];             /* sect base */
unsigned char *sect_addr[256];  /* sect address */
int curr_sect;                  /* current section */
int doem;                       /* 1st time flag */
#define MEM_SIZE 0x40000        /* 256k memory */
                                    /*#define MEM_START 0x20000*//* where to start */
#define MEM_START 0x00000       /* where to start */
unsigned char *sa;

struct dpr {                    /* datapool reference table */
    char name[8];               /* datapool item name */
    unsigned int refa;          /* address of reference */
};

#define DP_SIZE 100             /* datapool reference size */
int ndpr;
int dp_size;                    /* # allocated */
struct dpr *datapool;           /* enough for 100 references */

struct extr {                   /* external reference table */
    char name[8];               /* item name */
    unsigned int refa;          /* address of reference */
    unsigned int sba;           /* string back to next reference */
};

#define EXT_SIZE 100            /* external reference size */
int nextr;
int ext_size;                   /* # allocated */
struct extr *external;          /* enough for 100 references */

struct defr {                   /* definition reference table */
    char name[8];               /* item name */
    unsigned int refa;          /* address of reference */
};

#define DEF_SIZE 100            /* external definition size */
int ndefr;
int def_size;                   /* # allocated */
struct defr *defined;           /* enough for 100 references */

unsigned int endaddr;           /* end transfer address, if given */

/*
 **  output line of text from the source
 */
void putloi(unsigned char *s, int cnt)
{
    int i, j, k, l;
    int seq;
    int bc = s[1];
    int type, objcnt, repeat, bound;
    int exttype, offset;
    int temp1, temp2;
    struct dpr *pdpp;
    struct extr *pextp;
    struct defr *pdefp;

    if (!doem)
    {
        temp1 = MEM_SIZE;
        curr_sect = 0;          /* dsect */
        sect_base[curr_sect] = MEM_START;   /* assume dsect start */
        sect_org[curr_sect] = 0;    /* origin */
#ifdef NOT_NOW
        sect_size[curr_sect] = MEM_SIZE;    /* size */
#endif
        /* get area momory */
        sect_addr[curr_sect] = (unsigned char *)calloc(MEM_SIZE, 1);

        /* get datapool ref storage */
        datapool = (struct dpr *)calloc(DP_SIZE, sizeof(struct dpr));
        dp_size = DP_SIZE;      /* # allocated */
        ndpr = 0;               /* no ref yet */

        /* get external ref storage */
        external = (struct extr *)calloc(EXT_SIZE, sizeof(struct extr));
        ext_size = EXT_SIZE;    /* # allocated */
        nextr = 0;              /* no ref yet */

        /* get external def storage */
        defined = (struct defr *)calloc(DEF_SIZE, sizeof(struct defr));
        def_size = DEF_SIZE;    /* # allocated */
        ndefr = 0;              /* no ref yet */

        doem = 1;
    }
/*    if(*s == 0xdf)printf("**last record\n"); */
    seq = s[4] << 8 | s[5];
#ifdef QUIET
    fprintf(stderr, "**record %x cnt %d\n", seq, bc);
#endif
    offset = 6;
  doit:
    if (offset >= bc + 6)
        return;
    type = (s[offset] >> 4) & 0x0f; /* get object record type */
    objcnt = s[offset] & 0x0f;  /* get object record count */
    if (objcnt == 0)
        objcnt = 16;            /* zero cnt means 16 */

    switch (type)
    {
    case 0:                 /* absolute data */
        sa = sect_addr[curr_sect];  /* address of section */
#ifdef QUIET
        fprintf(stderr, "addr %x abs data cnt = %d data = ",
                sect_org[curr_sect] + sect_base[curr_sect], objcnt);
#endif
        for (k = 0; k < objcnt; k++)
        {
#ifdef QUIET
            if (k > 0 && (k % 4 == 0))
                printf(" ");
            fprintf(stderr, "%.2x", s[k + offset + 1]);
#endif
            /* put data in memory */
            sa[sect_org[curr_sect]++] = s[k + offset + 1];
        }
        if (!first)
        {
            first = 1;          /* we been here */
            type = 0;
            for (k = 0; k < 4; k++)
            {
                type |= (s[k + offset + 1] << ((3 - k) * 8));
            }
            if (type == 0x2243223a)
                dounix = 1;     /* doing c object */
        }
#ifdef QUIET
        fprintf(stderr, "\n");
#endif
        offset += (objcnt + 1);
        break;

    case 1:                 /* program origin */
#ifdef QUIET8
        fprintf(stderr, "%s program origin addr = ", (s[offset + 1] & 0x80) ? "rel" : "abs");
#endif
        s[offset + 1] &= 0x7f;  /* turn off rel bit */
        temp1 = 0;              /* clear for new org */
        for (k = 0; k < objcnt; k++)
        {
#ifdef QUIET8
            fprintf(stderr, "%.2x", s[k + offset + 1]);
#endif
            temp1 = (temp1 << 8) | s[offset + 1 + k];
        }
#ifdef QUIET8
        fprintf(stderr, "\n");
#endif
        sect_org[curr_sect] = temp1;    /* set new origin */
        offset += (objcnt + 1);
        break;

    case 2:                 /* absolute data repeat */
#ifdef OLD_JUNK
        fprintf(stderr, "DUMP abs data repeat cnt = %d\n", objcnt);
        for (k = 0; k < objcnt + 1; k++)
        {
            if (k > 0 && (k % 20 == 0))
                fprintf(stderr, "\n");
            fprintf(stderr, "%.2x ", s[offset + k]);
        }
        fprintf(stderr, "\n");
#endif
        sa = sect_addr[curr_sect];  /* address of section */
        repeat = s[offset + 1];
        if (repeat == 0)
            repeat = 1;
#ifdef QUIET
        fprintf(stderr, "abs data repeat cnt = %d\n", repeat);
#endif
        for (i = 0; i < repeat; i++)
        {
            for (k = 0; k < objcnt - 1; k++)
            {
#ifdef QUIET
                fprintf(stderr, "%.2x ", s[k + offset + 2]);
#endif
                /* put data in memory */
                sa[sect_org[curr_sect]++] = s[k + offset + 2];
            }
        }
#ifdef QUIET
        fprintf(stderr, "\n");
#endif
        offset += (objcnt + 1);
        break;

    case 3:                 /* transfer address */
#ifdef OLD_JUNK
        fprintf(stderr, "DUMP transfer address cnt = %d\n", objcnt);
        for (k = 0; k < objcnt + 1; k++)
        {
            if (k > 0 && (k % 20 == 0))
                fprintf(stderr, "\n");
            fprintf(stderr, "%.2x ", s[offset + k]);
        }
        fprintf(stderr, "\n");
#endif
#ifdef QUIET
        fprintf(stderr, "transfer address cnt = %d addr = ", objcnt);
#endif
        temp1 = 0;
        s[offset + 1] &= 0x7f;  /* turn off rel bit */
        for (k = 0; k < objcnt; k++)
        {
            temp1 = (temp1 << 8) | s[k + offset + 1];
#ifdef QUIET
            fprintf(stderr, "%.2x", s[k + offset + 1]);
#endif
        }
        temp1 += sect_base[curr_sect];
        endaddr = temp1;
#ifdef QUIET
        fprintf(stderr, "\n");
#endif
        offset += (objcnt + 1);
        break;

    case 4:                 /* relocatable data */
        sa = sect_addr[curr_sect];  /* address of section */
#ifdef QUIET
        fprintf(stderr, "addr %x rel data cnt = %d data = ",
                sect_org[curr_sect] + sect_base[curr_sect], objcnt);
#endif
        for (k = 0; k < objcnt; k++)
        {
            if (k == 0)
                temp1 = 0;
            temp1 = (temp1 << 8) | s[offset + 1 + k];
            if (k > 0 && (k % 4 == 3))
            {
                temp1 += sect_base[curr_sect];
#ifdef QUIET
                fprintf(stderr, "%.8x", temp1);
                fprintf(stderr, " ");
#endif
                /* put data in memory */
                for (i = 0; i < 4; i++)
                    sa[sect_org[curr_sect]++] = (temp1 >> ((3 - i) * 8)) & 0xff;
                temp1 = 0;
            }
        }
#ifdef QUIET
        fprintf(stderr, "\n");
#endif
        offset += (objcnt + 1);
        break;

    case 5:                 /* program name */
        bound = s[offset + 1 + objcnt - 1];
        repeat = s[offset + 1 + objcnt - 2];
        /* see if 'C' object */
        if (repeat == 0x04)
            dounix = 1;         /* doing c object */
#ifdef JUNK
        fprintf(stderr, "program name = ");
#endif
#ifdef QUIET9
        fprintf(stderr, "          PROGRAM  ");
        for (k = 0; k < objcnt - 3; k++)
        {
            fprintf(stderr, "%c", s[k + offset + 1]);
        }
        fprintf(stderr, "\n");
#endif
#ifdef QUIET1
        fprintf(stderr, " bound = %x obj type = %x", bound, repeat);
        fprintf(stderr, "\n");
#endif
        offset += (objcnt + 1);
        break;

    case 6:                 /* relocatable data repeat */
        sa = sect_addr[curr_sect];  /* address of section */
        repeat = s[offset + 1];
        if (repeat == 0)
            repeat = 1;
#ifdef QUIET
        fprintf(stderr, "rel data repeat cnt = %d\n", repeat);
#endif
        for (j = 0; j < repeat; j++)
        {
            for (k = 0; k < objcnt - 1; k++)
            {
                if (k == 0)
                    temp1 = 0;
                temp1 = (temp1 << 8) | s[offset + 2 + k];
                if (k > 0 && (k % 4 == 3))
                {
                    temp1 += sect_base[curr_sect];
#ifdef QUIET
                    fprintf(stderr, "%.8x", temp1);
                    fprintf(stderr, " ");
#endif
                    /* put data in memory */
                    for (i = 0; i < 4; i++)
                        sa[sect_org[curr_sect]++] = (temp1 >> ((3 - i) * 8)) & 0xff;
                    temp1 = 0;
                }
            }
        }
#ifdef QUIET
        fprintf(stderr, "\n");
#endif
        offset += (objcnt + 1);
        break;

    case 7:                 /* external definition */
#ifdef JUNK
        fprintf(stderr, "%s external def name = ", (s[offset + 1 + objcnt - 3] & 0x80) ? "rel" : "abs");
#endif
        s[offset + 1 + objcnt - 3] &= 0x7f; /* turn off rel bit */
        bound = s[offset + 1 + objcnt - 3] << 16 | s[offset + 1 + objcnt - 2] << 8 | s[offset + 1 + objcnt - 1];
        if (ndefr >= def_size)
        {                       /* we need to resize */
            defined = (struct defr *)realloc((void *)defined, (def_size + 10) * sizeof(struct defr));
            def_size += 10;     /* 10 more */
        }
        pdefp = &defined[ndefr];    /* ref data address */
#ifdef QUIET9
        fprintf(stderr, "          DEF      ");
        memset(pdefp->name, ' ', 8);    /* blank name buffer */
        for (k = 0; k < objcnt - 3; k++)
        {
            fprintf(stderr, "%c", s[k + offset + 1]);
            pdefp->name[k] = s[k + offset + 1];
        }
#endif
        pdefp->refa = bound + sect_base[curr_sect];
        ndefr++;                /* next reference */
#ifdef JUNK
        fprintf(stderr, " address = %x", bound);
#endif
#ifdef QUIET9
        fprintf(stderr, "\n");
#endif
        offset += (objcnt + 1);
        break;

    case 8:                 /* forward reference */
#ifdef QUIET9
        fprintf(stderr, "%s forward reference addr = ", (s[offset + 1] & 0x80) ? "rel" : "abs");
#endif
        s[offset + 1] &= 0x7f;  /* turn off rel bit */
#ifdef QUIET9
        for (k = 0; k < objcnt - 3; k++)
        {
            fprintf(stderr, "%.2x", s[k + offset + 1]);
        }
        fprintf(stderr, " sb addr = ");
        for (k = 3; k < objcnt; k++)
        {
            fprintf(stderr, "%.2x", s[k + offset + 1]);
        }
        fprintf(stderr, "\n");
#endif
        offset += (objcnt + 1);
        break;

    case 9:                 /* external reference */
#ifdef JUNK
        fprintf(stderr, "%s external ref name = ", (s[offset + 1 + objcnt - 3] & 0x80) ? "rel" : "abs");
#endif
        s[offset + 1 + objcnt - 3] &= 0x7f; /* turn off rel bit */
        bound = s[offset + 1 + objcnt - 3] << 16 | s[offset + 1 + objcnt - 2] << 8 | s[offset + 1 + objcnt - 1];
        if (nextr >= ext_size)
        {                       /* we need to resize */
            external = (struct extr *)realloc((void *)external, (ext_size + 10) * sizeof(struct extr));
            ext_size += 10;     /* 10 more */
        }
        pextp = &external[nextr];   /* ref data address */
#ifndef QUIET9
        fprintf(stderr, "          EXT      ");
        memset(pextp->name, ' ', 8);    /* blank name buffer */
        for (k = 0; k < objcnt - 3; k++)
        {
            fprintf(stderr, "%c", s[k + offset + 1]);
            pextp->name[k] = s[k + offset + 1];
        }
#endif
        pextp->refa = sect_org[curr_sect] + sect_base[curr_sect];
        pextp->sba = bound + sect_base[curr_sect];
        nextr++;                /* next reference */
#ifdef JUNK
        fprintf(stderr, " sb addr = %x", bound);
#endif
#ifdef QUIET9
        fprintf(stderr, "\n");
#endif
        offset += (objcnt + 1);
        break;

    case 10:                    /* common definition */
#ifdef QUIET9
        fprintf(stderr, "common def name = ");
#endif
        /* get common block number */
        repeat = s[offset + 1 + objcnt - 3];
        /* get common size */
        bound = s[offset + 1 + objcnt - 2] << 8 | s[offset + 1 + objcnt - 1];
#ifdef QUIET9
        for (k = 0; k < objcnt - 3; k++)
        {
            fprintf(stderr, "%c", s[k + offset + 1]);
        }
        fprintf(stderr, " block = %d size = %x\n", repeat, bound);
#endif
        offset += (objcnt + 1);
        break;

    case 11:                    /* common reference */
        /* get common block number */
        i = s[offset + 1];      /* block number */
        sa = sect_addr[0];      /* address in dsect */
#ifdef QUIET8
        fprintf(stderr, "common ref block = %d cnt %d data = ", i, objcnt - 1);
#endif
        for (k = 0; k < objcnt - 1; k++)
        {
            if (k == 0)
                temp1 = 0;
            temp1 = (temp1 << 8) | s[offset + 2 + k];
            if (k > 0 && (k % 4 == 3))
            {
#ifdef QUIET8
                fprintf(stderr, "%.8x", temp1);
                fprintf(stderr, " ");
#endif
                temp1 += com_base[i] + sect_base[0];
#ifdef QUIET8
                fprintf(stderr, "(%.8x)", temp1);
                fprintf(stderr, " ");
#endif
                /* put data in memory */
                for (j = 0; j < 4; j++)
                    sa[sect_org[curr_sect]++] = (temp1 >> ((3 - j) * 8)) & 0xff;
                temp1 = 0;
#ifdef QUIET8
                if (k > 0 && (k % 20 == 0))
                    fprintf(stderr, "\n");
#endif
            }
        }
#ifdef QUIET8
        if (k % 20 != 0)
            fprintf(stderr, "\n");
#endif
        offset += (objcnt + 1);
        break;

    case 12:                    /* datapool reference */
        sa = sect_addr[curr_sect];  /* address of section */
#ifdef JUNK
        fprintf(stderr, "datapool ref number %d size %d\n", ndpr, dp_size);
#endif
        fprintf(stderr, "DATAPOOL  COMMON   ");
        if (ndpr >= dp_size)
        {                       /* we need to resize */
            datapool = (struct dpr *)realloc((void *)datapool, (dp_size + 10) * sizeof(struct dpr));
            dp_size += 10;      /* 10 more */
        }
        pdpp = &datapool[ndpr]; /* ref data address */
        memset(pdpp->name, ' ', 8); /* blank name buffer */
        for (k = 0; k < objcnt - 4; k++)
        {
            fprintf(stderr, "%c", s[k + offset + 1]);
            pdpp->name[k] = s[k + offset + 1];
        }
        pdpp->refa = sect_org[curr_sect] + sect_base[curr_sect];
        ndpr++;                 /* next reference */
#ifdef JUNK
        fprintf(stderr, " at %x addr = ", sect_org[curr_sect] + sect_base[curr_sect]);
#endif
        for (k = 0; k < 4; k++)
        {
            if (k == 0)
                temp1 = 0;
            temp1 = (temp1 << 8) | s[k + offset + objcnt + 1 - 4];
            if (k > 0 && (k % 4 == 3))
            {
/*          temp1 += sect_base[curr_sect]; */
                /* put data in memory */
                for (i = 0; i < 4; i++)
                    sa[sect_org[curr_sect]++] = (temp1 >> ((3 - i) * 8)) & 0xff;
                temp1 = 0;
            }
#ifdef JUNK
            fprintf(stderr, "%.2x", s[k + offset + objcnt + 1 - 4]);
#endif
        }
        fprintf(stderr, "\n");
        offset += (objcnt + 1);
        break;

    case 13:                    /* escape to extended functions */
        objcnt = s[offset] & 0x0f;  /* get sub type */
        exttype = s[offset + 1];    /* get ext type */

        switch (exttype)
        {
        case 0:             /* undefined */
            for (k = 0; k < s[offset + 2]; k++)
            {
                if (k > 0 && (k % 20 == 0))
                    fprintf(stderr, "\n");
                fprintf(stderr, "%.2x ", s[offset + k]);
            }
            fprintf(stderr, "\n");
            offset += s[offset + 2];
            break;

        case 1:             /* section definition */
#ifdef QUIET8
            fprintf(stderr, "section def bound = %x num = %d size = ", s[offset + 3], s[offset + 4]);
            temp1 = 0;
            for (k = 0; k < 3; k++)
            {
                fprintf(stderr, "%.2x", s[offset + 5 + k]);
                temp1 = (temp1 << 8) | s[offset + 5 + k];
            }
            fprintf(stderr, " name = ");
            for (k = 0; k < 8; k++)
            {
                fprintf(stderr, "%c", s[offset + 8 + k]);
            }
            fprintf(stderr, "\n");
#endif
            curr_sect = s[offset + 4];  /* sect # */
            if (curr_sect == 0)
                sect_base[curr_sect] = 0x20000; /* assume dsect start */
            if (curr_sect == 1)
            {
                /* assume csect start */
                temp2 = (temp1 / 0x2000) + 1;   /* mod page size */
                sect_base[curr_sect] = 0x80000 - (temp2 * 0x2000);
            }
            sect_org[curr_sect] = 0;    /* origin */

            temp2 = sect_size[curr_sect];   /* get old size */
            if (temp2 == 0 && curr_sect == 0)
                temp2 = MEM_SIZE;   /* initial allocation */
            sect_size[curr_sect] = temp1;   /* new size */
            /* get area momory */
            if (sect_addr[curr_sect])
            {                   /* if we got one */
                /* resize */
                sect_addr[curr_sect] = (unsigned char *)realloc((void *)sect_addr[curr_sect], temp1);
                /* clear new memory */
                if ((temp1 - temp2) > 0)
                    memset(sect_addr[curr_sect] + temp2, 0, temp1 - temp2);
            }
            else                /* get a new one */
                sect_addr[curr_sect] = (unsigned char *)calloc(temp1, 1);

            offset += s[offset + 2];
            break;

        case 2:             /* section origin */
#ifdef QUIET8
            fprintf(stderr, "section origin bound = %x sect %s origin = ",
                    s[offset + 3], s[offset + 4] == 0 ? "DSECT" : s[offset + 4] == 1 ? "CSECT" : "UNKNOWN");
            temp1 = 0;
            for (k = 0; k < 3; k++)
            {
                fprintf(stderr, "%.2x", s[offset + 5 + k]);
                temp1 = (temp1 << 8) | s[offset + 5 + k];
            }
            fprintf(stderr, "\n");
#endif
            curr_sect = s[offset + 4];  /* sect # */
            sect_org[curr_sect] = temp1;    /* origin */
            offset += s[offset + 2];
            break;

        case 3:             /* section relocatable ref */
            repeat = s[offset + 5];
            sa = sect_addr[curr_sect];  /* address of section */
            if (repeat == 0)
                repeat = 1;     /* 0 is 1 */
#ifdef QUIET
            fprintf(stderr, "addr %x sect rel ref %s rep cnt = %d data = ",
                    sect_org[curr_sect] + sect_base[curr_sect],
                    s[offset + 4] == 0 ? "DSECT" : s[offset + 4] == 1 ? "CSECT" : "UNKNOWN", repeat);
#endif
            for (k = 0; k < (s[offset + 2] - 6); k++)
            {
                if (k == 0)
                    temp1 = 0;
                temp1 = (temp1 << 8) | s[offset + 6 + k];
                if (k > 0 && (k % 4 == 3))
                {
                    temp1 += sect_base[s[offset + 4]];
#ifdef QUIET
                    fprintf(stderr, "%.8x", temp1);
                    fprintf(stderr, " ");
#endif
                    /* put data in memory */
                    for (i = 0; i < 4; i++)
                        sa[sect_org[curr_sect]++] = (temp1 >> ((3 - i) * 8)) & 0xff;
                    temp1 = 0;
                }
#ifdef QUIET
                if (k > 0 && (k % 20 == 0))
                    fprintf(stderr, "\n");
#endif
            }
#ifdef QUIET
            if (k % 20 != 0)
                fprintf(stderr, "\n");
#endif
            offset += s[offset + 2];
            break;

        case 4:             /* section transfer address */
#ifdef QUIET
            fprintf(stderr, "section transfer address num = %d address = ", s[offset + 4]);
            for (k = 0; k < 3; k++)
            {
                fprintf(stderr, "%.2x", s[offset + 5 + k]);
            }
            fprintf(stderr, "\n");
#endif
            offset += s[offset + 2];
            break;

        case 5:             /* section external def */
#ifdef QUIET
            fprintf(stderr, "sect ext def num = %d address = ", s[offset + 4]);
            for (k = 0; k < 3; k++)
            {
                fprintf(stderr, "%.2x", s[offset + 5 + k]);
            }
            fprintf(stderr, " name = ");
            for (k = 0; k < s[offset + 2] - 8; k++)
            {
                fprintf(stderr, "%c", s[offset + 8 + k]);
            }
            fprintf(stderr, "\n");
#endif
            offset += s[offset + 2];
            break;

        case 6:             /* section external ref */
#ifdef QUIET
            fprintf(stderr, "section ext ref num = %d sb address = ", s[offset + 4]);
            for (k = 0; k < 3; k++)
            {
                fprintf(stderr, "%.2x", s[offset + 5 + k]);
            }
            fprintf(stderr, " name = ");
            for (k = 0; k < s[offset + 2] - 8; k++)
            {
                fprintf(stderr, "%c", s[offset + 8 + k]);
            }
            fprintf(stderr, "\n");
#endif
            offset += s[offset + 2];
            break;

        case 7:             /* section forward ref */
#ifdef QUIET
            fprintf(stderr, "sect forward ref num = %d address = ", s[offset + 4]);
            for (k = 0; k < 3; k++)
            {
                fprintf(stderr, "%.2x", s[offset + 5 + k]);
            }
            fprintf(stderr, " num = %d sb addr = ", s[offset + 8]);
            for (k = 0; k < 3; k++)
            {
                fprintf(stderr, "%.2x", s[offset + 9 + k]);
            }
            fprintf(stderr, "\n");
#endif
            offset += s[offset + 2];
            break;

        case 8:             /* large common def */
#ifdef QUIET8
            fprintf(stderr, "large com def num = %d sb size = ", s[offset + 4]);
#endif
            i = s[offset + 4];  /* common number */
            temp1 = 0;          /* size of common */
            for (k = 0; k < 3; k++)
            {
#ifdef QUIET8
                fprintf(stderr, "%.2x", s[offset + 5 + k]);
#endif
                temp1 = (temp1 << 8) | s[offset + 5 + k];
            }
            /* common is on 32 byte boundry and mod 32 bytes */
            /* set dsect origin mod 32 bytes */
            sect_org[0] = (sect_org[0] + 31) & ~31;
            com_base[i] = sect_org[0];  /* addr of common in dsect */
            com_org[i] = 0;     /* curr origin in common */
            com_size[i] = temp1;    /* common size */
#ifdef QUIET8
            fprintf(stderr, " org %x base %x", com_org[i], com_base[i]);
            fprintf(stderr, " name = ");
#endif
            memset(comname, ' ', 8);    /* blank name buffer */
            if (ndefr >= def_size)
            {                   /* we need to resize */
                defined = (struct defr *)realloc((void *)defined, (def_size + 10) * sizeof(struct defr));
                def_size += 10; /* 10 more */
            }
            pdefp = &defined[ndefr];    /* ref data address */
            /* copy in common name */
            memset(pdefp->name, ' ', 8);    /* blank name buffer */
            for (k = 0; k < s[offset + 2] - 8; k++)
            {
                comname[k] = s[offset + 8 + k];
                pdefp->name[k] = s[k + offset + 8];
#ifdef QUIET8
                fprintf(stderr, "%c", s[offset + 8 + k]);
#endif
            }
#ifdef QUIET8
            fprintf(stderr, "\n");
#endif
#ifdef QUIET8
            fprintf(stderr, "%.8s  COMMON   c_%d(%d)\n", comname, i, temp1);
#endif
            /* set next dsect origin */
            sect_org[0] += ((temp1 + 31) & ~31);    /* new origin */
            last_ds = sect_org[0];  /* save new ds start */
            pdefp->refa = com_base[i] + sect_base[0];
            ndefr++;            /* next reference */
            offset += s[offset + 2];
            break;

        case 9:             /* large common origin */
            i = s[offset + 4];  /* block number */
#ifdef QUIET8
            fprintf(stderr, "large com origin num = %d sb origin = ", s[offset + 4]);
#endif
            temp1 = 0;          /* size of common */
            for (k = 0; k < 3; k++)
            {
                temp1 = (temp1 << 8) | s[offset + 5 + k];
#ifdef QUIET8
                fprintf(stderr, "%.2x", s[offset + 5 + k]);
#endif
            }
#ifdef QUIET8
            fprintf(stderr, "\n");
#endif
            /* reset origin in common block */
            com_org[i] = temp1; /* new origin in common */
            offset += s[offset + 2];
            break;

        case 10:                /* large common ref */
            i = s[offset + 4];  /* block number */
            sa = sect_addr[0];  /* address in dsect */
            repeat = s[offset + 5];
            if (repeat == 0)
                repeat = 1;     /* 0 is 1 */
#ifdef QUIET8
            fprintf(stderr, "large com ref num = %d rep cnt = %d data = ", s[offset + 4], repeat);
#endif

            for (j = 0; j < repeat; j++)
            {
                for (k = 0; k < (s[offset + 2] - 6); k++)
                {
                    if (k == 0)
                        temp1 = 0;
                    temp1 = (temp1 << 8) | s[offset + 6 + k];
                    if (k > 0 && (k % 4 == 3))
                    {
#ifdef QUIET8
                        fprintf(stderr, "%.8x", temp1);
                        fprintf(stderr, " ");
#endif
                        temp1 += com_base[i] + sect_base[0];
                        /* put data in memory */
                        for (l = 0; l < 4; l++)
                            sa[sect_org[curr_sect]++] = (temp1 >> ((3 - l) * 8)) & 0xff;
                        temp1 = 0;
                    }
#ifdef QUIET8
                    if (k > 0 && (k % 20 == 0))
                        fprintf(stderr, "\n");
#endif
                }
#ifdef QUIET8
                if (k % 20 != 0)
                    fprintf(stderr, "\n");
#endif
            }
            offset += s[offset + 2];
            break;

        case 11:                /* debugger information */
#ifdef NOTNOW
            fprintf(stderr, "debugger info: flg: %d len: %d %s %s type: ",
                    objcnt, s[offset + 2], s[offset + 3] & 2 ? "abs" : "rel",
                    s[offset + 3] & 1 ? "CSECT" : "DSECT");
            switch (s[offset + 4])
            {
            case 0:
                fprintf(stderr, "int * 1");
                break;
            case 1:
                fprintf(stderr, "int * 2");
                break;
            case 2:
                fprintf(stderr, "int * 4");
                break;
            case 3:
                fprintf(stderr, "int * 8");
                break;
            case 4:
                fprintf(stderr, "real * 4");
                break;
            case 5:
                fprintf(stderr, "real * 8");
                break;
            case 6:
                fprintf(stderr, "complex * 8");
                break;
            case 7:
                fprintf(stderr, "complex * 16");
                break;
            case 8:
                fprintf(stderr, "bit logical");
                break;
            case 9:
                fprintf(stderr, "logical * 1");
                break;
            case 10:
                fprintf(stderr, "logical * 4");
                break;
            case 11:
                fprintf(stderr, "char * n");
                break;
            case 12:
                fprintf(stderr, "type 12");
                break;
            case 13:
                fprintf(stderr, "type 13");
                break;
            case 14:
                fprintf(stderr, "statement label");
                break;
            case 15:
                fprintf(stderr, "proceedure");
                break;
            default:
                fprintf(stderr, "unknown type %d", s[offset + 4]);
                break;
            }
#endif
            bound = 0;
            for (k = 0; k < 3; k++)
            {
                bound = (bound << 8) | (s[offset + 5 + k]);
            }
#ifdef QUIET8
            fprintf(stderr, "\n* %s address: %x bitnum: %x",
                    s[offset + 3] & 1 ? "csect" : "dsect", bound >> 3, bound & 7);
            fprintf(stderr, " size: %d symbol: ", s[offset + 8] << 8 | s[offset + 9]);
            /* now print the 8 chars of the symbol */
            for (k = 0; k < s[offset + 2] - 10; k++)
            {
                fprintf(stderr, "%c", s[offset + 10 + k]);
            }
            /* see if common */
            if (s[offset + 2] == 26)
            {
                fprintf(stderr, " common: ");
                for (k = 0; k < 8; k++)
                {
                    fprintf(stderr, "%c", s[offset + 18 + k]);
                }
            }
            fprintf(stderr, "\n");
#endif
            offset += s[offset + 2];
            break;

        case 12:                /* object creation information */
            /* object creation date/time */
            if (objcnt == 0)
            {
#ifdef QUIET8
                fprintf(stderr, "* OBJECT CREATED   ");
#ifdef JUNK
                fprintf(stderr, "object creation date/time: ");
#endif
                for (k = 0; k < 8; k++)
                {
                    fprintf(stderr, "%c", s[offset + 4 + k]);
                }
                fprintf(stderr, " ");
                for (k = 0; k < 8; k++)
                {
                    fprintf(stderr, "%c", s[offset + 12 + k]);
                }
                fprintf(stderr, "\n");
#endif
                offset += s[offset + 2];
                break;
            }
            /* product id leader */
            if (objcnt == 1)
            {
#ifdef QUIET8
                fprintf(stderr, "* product id: ");
                for (k = 0; k < s[offset + 2] - 4; k++)
                {
                    fprintf(stderr, "%c", s[offset + 4 + k]);
                }
                fprintf(stderr, "\n");
#endif
                offset += s[offset + 2];
                break;
            }
            /* generating processor info */
            if (objcnt == 2)
            {
#ifdef QUIET8
                fprintf(stderr, "* generating processor info: opt wd1 = ");
                for (k = 0; k < 4; k++)
                {
                    fprintf(stderr, "%.2x", s[offset + 4 + k]);
                }
                fprintf(stderr, " opt wd2 = ");
                for (k = 0; k < 4; k++)
                {
                    fprintf(stderr, "%.2x", s[offset + 8 + k]);
                }
#ifdef FUTURE
                fprintf(stderr, " rev lev = ");
                for (k = 0; k < 4; k++)
                {
                    fprintf(stderr, "%.2x", s[offset + 12 + k]);
                }
#endif
                fprintf(stderr, "\n");
                fprintf(stderr, "* rrs cnt = ");
                for (k = 0; k < 2; k++)
                {
                    fprintf(stderr, "%.2x", s[offset + 16 + k]);
                }
                fprintf(stderr, " incl cnt = ");
                for (k = 0; k < 2; k++)
                {
                    fprintf(stderr, "%.2x", s[offset + 18 + k]);
                }
/*              fprintf(stderr, "\n"); */
                fprintf(stderr, " pathname: ");
                for (k = 0; k < s[offset + 2] - 20; k++)
                {
                    fprintf(stderr, "%c", s[offset + 20 + k]);
                }
                fprintf(stderr, "\n");
#endif
                offset += s[offset + 2];
                break;
            }

            /* required resources info */
            if (objcnt == 3)
            {
#ifdef QUIET8
                repeat = s[offset + 3];
                fprintf(stderr, "* required resource info: %s lfc = ",
                        repeat & 1 ? "FORT incl file" : "req obj file");
                for (k = 0; k < 3; k++)
                {
                    fprintf(stderr, "%c", s[offset + 5 + k]);
                }
                fprintf(stderr, "\n");
                repeat = s[offset + 10];
                fprintf(stderr, "* acc mode = ");
                for (k = 0; k < 4; k++)
                {
                    fprintf(stderr, "%.2x", s[offset + 12 + k]);
                }
                fprintf(stderr, " opts = ");
                for (k = 0; k < 4; k++)
                {
                    fprintf(stderr, "%.2x", s[offset + 16 + k]);
                }
                fprintf(stderr, " pathname: ");
                for (k = 0; k < s[offset + 2] - 20; k++)
                {
                    fprintf(stderr, "%c", s[offset + 20 + k]);
                }
                fprintf(stderr, "\n");
#ifdef OLD_JUNK
                fprintf(stderr, "PNAM LEN %d PATH LEN %d\n", repeat, s[offset + 2] - 20);
#endif
#endif
                offset += s[offset + 2];
                break;
            }
#ifdef QUIET8
            for (k = 0; k < s[offset + 2]; k++)
            {
                if (k > 0 && (k % 20 == 0))
                    fprintf(stderr, "\n");
                fprintf(stderr, "%.2x ", s[offset + k]);
            }
            fprintf(stderr, "\n");
#endif
            offset += s[offset + 2];
            break;

        case 13:                /* multiple datapool ref */
#ifdef QUIET8
            fprintf(stderr, "multiple datapool ref sym =");
            for (k = 0; k < (s[offset + 2] - 12); k++)
            {
                fprintf(stderr, "%c ", s[offset + 4 + k]);
            }
            fprintf(stderr, "\naddr = ");
            for (k = 0; k < 4; k++)
            {
                fprintf(stderr, "%.2x ", s[offset + 2] - 8 + k);
            }
            fprintf(stderr, " pool num = ");
            for (k = 0; k < 4; k++)
            {
                fprintf(stderr, "%.2x ", s[offset + 2] - 4 + k);
            }
            fprintf(stderr, "\n");
#endif
            offset += s[offset + 2];
            break;

        case 14:                /* new debugger information */
#ifdef QUIET9
#ifdef NOT_NOW
            fprintf(stderr, "new debugger info (hex dump)\n");
            for (k = 0; k < s[offset + 2]; k++)
            {
                if (k > 0 && (k % 20 == 0))
                    fprintf(stderr, "\n");
                fprintf(stderr, "%.2x ", s[offset + k]);
            }
            fprintf(stderr, "\n");
#endif
            fprintf(stderr, "new debugger info: flags: %x type: ", s[offset + 3]);
#endif
#ifdef QUIET9
            if (dounix)
            {
                /* see if we have an enum */
                if (s[offset + 4] & 0x20)
                {
                    fprintf(stderr, "enum ");
                }
                /* see if stack variable */
                if (s[offset + 3] & 0x10)
                {
                    fprintf(stderr, "stack ");
                }
                /* see if register variable */
                if (s[offset + 3] & 0x04)
                {
                    fprintf(stderr, "register ");
                }
                /* see if pointer */
                if (s[offset + 3] & 0x20)
                {
                    int indcnt = s[offset + 5];
                    /* print number of indirections */
                    while (indcnt--)
                        fprintf(stderr, "*");
                }
                switch (s[offset + 4] & ~0x20)
                {
                case 0:
                    fprintf(stderr, "char");
                    break;
                case 1:
                    fprintf(stderr, "short");
                    break;
                case 2:
                    fprintf(stderr, "int");
                    break;
                case 3:
                    fprintf(stderr, "long");
                    break;
                case 4:
                    fprintf(stderr, "float");
                    break;
                case 5:
                    fprintf(stderr, "double");
                    break;
                case 6:
                    fprintf(stderr, "struct ");
                    for (k = 0; k < 8; k++)
                    {
                        fprintf(stderr, "%c", s[offset + 20 + k]);
                    }
                    break;
                case 7:
                    fprintf(stderr, "union ");
                    for (k = 0; k < 8; k++)
                    {
                        fprintf(stderr, "%c", s[offset + 20 + k]);
                    }
                    break;
                case 8:
                    fprintf(stderr, "uchar");
                    break;
                case 9:
                    fprintf(stderr, "ushort");
                    break;
                case 10:
                    fprintf(stderr, "uint");
                    break;
                case 11:
                    fprintf(stderr, "ulong");
                    break;
                case 12:
                    fprintf(stderr, "enum");
                    break;
                case 13:
                    fprintf(stderr, "struct def");
                    break;
                case 14:
                    fprintf(stderr, "statement label");
                    break;
                case 15:
                    fprintf(stderr, "entry point");
                    break;
                case 16:
                    fprintf(stderr, "illegal");
                    break;
                case 17:
                    fprintf(stderr, "CSECT load origin");
                    break;
                case 18:
                    fprintf(stderr, "DSECT load origin");
                    break;
                default:
                    fprintf(stderr, "unknown type %d", s[offset + 4]);
                    break;
                }
            }
            else
            {
                /* see if we have a parameter */
                if ((s[offset + 4] & 0x60) == 0x20)
                {
                    fprintf("parameter ");
                }
                switch (s[offset + 4] & ~0x20)
                {
                case 0:
                    fprintf(stderr, "int * 1");
                    break;
                case 1:
                    fprintf(stderr, "int * 2");
                    break;
                case 2:
                    fprintf(stderr, "int * 4");
                    break;
                case 3:
                    fprintf(stderr, "int * 8");
                    break;
                case 4:
                    fprintf(stderr, "real * 4");
                    break;
                case 5:
                    fprintf(stderr, "real * 8");
                    break;
                case 6:
                    fprintf(stderr, "complex * 8");
                    break;
                case 7:
                    fprintf(stderr, "complex * 16");
                    break;
                case 8:
                    fprintf(stderr, "bit logical");
                    break;
                case 9:
                    fprintf(stderr, "logical * 1");
                    break;
                case 10:
                    fprintf(stderr, "logical * 4");
                    break;
                case 11:
                    fprintf(stderr, "char * n");
                    break;
                case 12:
                    fprintf(stderr, "type 12");
                    break;
                case 13:
                    fprintf(stderr, "type 13");
                    break;
                case 14:
                    fprintf(stderr, "statement label");
                    break;
                case 15:
                    fprintf(stderr, "proceedure");
                    break;
                default:
                    fprintf(stderr, "unknown type %d", s[offset + 4]);
                    break;
                }
            }
            fprintf(stderr, " rsvd: %x size: %d\n", s[offset + 5], s[offset + 6] * 256 + s[offset + 7]);
#endif
            repeat = 0;
            for (k = 0; k < 4; k++)
            {
                repeat = (repeat << 8) | s[offset + 8 + k];
            }
#ifdef QUIET8
            fprintf(stderr, "* %s address: %x, bitnum: %d symbol: ",
                    s[offset + 3] & 1 ? "csect" : "dsect", repeat >> 3, repeat & 7);
            for (k = 0; k < 8; k++)
            {
                fprintf(stderr, "%c", s[offset + 12 + k]);
            }
            fprintf(stderr, "\n");
#endif
            /* only look at ISC type if len is > 20 */
            if (s[offset + 2] <= 20)
                goto wedone;
            /* only look at ISC type if flags is zero */
            /* and not a parameter */
            if (s[offset + 3] <= 1 && s[offset + 4] < 0x20)
            {
#ifdef QUIET8
                /* do ISC record */
                fprintf(stderr, "* ISC type: ");
                switch (s[offset + 20] & 0x7f)
                {
                case 0:
                    fprintf(stderr, "no src");
                    break;
                case 1:
                    fprintf(stderr, "declaration");
                    break;
                case 2:
                    fprintf(stderr, "xeq statement");
                    break;
                case 3:
                    fprintf(stderr, "cont line");
                    break;
                case 4:
                    fprintf(stderr, "comment");
                    break;
                case 5:
                    fprintf(stderr, "FORTRAN err");
                    break;
                default:
                    fprintf(stderr, "undefined %x", s[offset + 20] & 0x7f);
                    break;
                }
                fprintf(stderr, " caret %d\n", s[offset + 21]);
#endif
                /* only process source if caret is zero */
                if (s[offset + 21] != 0)
                    goto wedone;

                if (s[offset + 20] & 0x80)
                {
                    /* uncompressed source */
                    int srccnt = s[offset + 22];
                    int blkcnt = s[offset + 23];

#ifdef QUIET8
                    for (k = 0; k < blkcnt; k++)
                    {
                        fprintf(stderr, " ");   /* blanks */
                    }
                    for (k = 0; k < srccnt; k++)
                    {
                        fprintf(stderr, "%c", s[offset + 24 + k]);  /* source */
                    }
#endif
                }
                else
                {
                    /* compressed source */
                    unsigned char chr;
                    int srccnt = s[offset + 22];
                    int blkcnt = s[offset + 23];
                    int j = 0;
                    int l = offset + 24;

#ifdef QUIET8
                    for (k = 0; k < blkcnt; k++)
                    {
                        fprintf(stderr, " ");   /* blanks */
                    }
                    for (k = 0; k < srccnt; k++)
                    {
                        switch (j)
                        {
                        case 0:
                            chr = ((s[offset + l]) >> 2) & 0x3f + 32;
                            j++;
                            break;
                        case 1:
                            chr = (((s[offset + l++] & 3) << 4) | ((s[offset + l] & 0xf0) >> 4) + 32);
                            j++;
                            break;
                        case 2:
                            chr = (((s[offset + l++] & 0xf) << 2) | ((s[offset + l] & 0xc0) >> 6) + 32);
                            j++;
                            break;
                        case 3:
                            chr = ((s[offset + l] & 0x3f) + 32);
                            j = 0;
                            break;
                        }
                        fprintf(stderr, "%c", chr); /* source */
                    }
#endif
                }
#ifdef QUIET8
                fprintf(stderr, "\n");
#endif
            }
            else
            {                   /* else handle Encore debug record */
                int srccnt;
                unsigned int srcval;
                unsigned int srcval2;
                int sv;         /* switch value */
                int xx = 20;    /* array info offset */

                /* First look for a parameter.  Flags will be zero. */
                /* Type will be >= 32 (0x20) */
                if (dounix)
                {
                    sv = s[offset + 3] & 0x48;
                }
                else
                {
                    sv = s[offset + 3];
                }
                switch (s[offset + 3])
                {               /* switch on flags */
                case 0x00:      /* parameter */
                    switch (s[offset + 4] & 0x1f)
                    {
                    case 0: /* int * 1 */
                    case 9: /* logical * 1 */
#ifdef QUIET8
                        /* wd 5 byte 0 has value */
                        fprintf(stderr, "* param value: 0x");
                        fprintf(stderr, "%.2x", s[offset + 20]);
                        fprintf(stderr, "\n");
#endif
                        break;
                    case 1: /* int * 2 */
                        /* wd 5 bytes 0-1 has value */
                        srcval = s[offset + 20] << 8 | s[offset + 21];
#ifdef QUIET8
                        fprintf(stderr, "* param value: 0x");
                        fprintf(stderr, "%.4x", srcval);
                        fprintf(stderr, "\n");
#endif
                        break;
                    case 2: /* int * 4 */
                    case 4: /* real * 4 */
                    case 10:    /* logical * 4 */
                        /* wd 5 bytes 0-3 has value */
                        srcval = s[offset + 20] << 24
                            | s[offset + 21] << 16 | s[offset + 22] << 8 | s[offset + 23];
#ifdef QUIET8
                        fprintf(stderr, "* param value: 0x");
                        fprintf(stderr, "%.8x", srcval);
                        fprintf(stderr, "\n");
#endif
                        break;
                    case 3: /* int * 8 */
                    case 5: /* real * 8 */
                    case 6: /* complex * 8 */
                        /* wd 5-6 bytes 0-3 has value */
                        srcval = s[offset + 20] << 24
                            | s[offset + 21] << 16 | s[offset + 22] << 8 | s[offset + 23];
                        srcval2 = s[offset + 24] << 24
                            | s[offset + 25] << 16 | s[offset + 26] << 8 | s[offset + 27];
#ifdef QUIET8
                        fprintf(stderr, "* param value: 0x");
                        fprintf(stderr, "%.8x %.8x", srcval, srcval2);
                        fprintf(stderr, "\n");
#endif
                        break;
                    case 7: /* complex * 16 */
                        /* wd 5-6 bytes 0-3 has value */
                        srcval = s[offset + 20] << 24
                            | s[offset + 21] << 16 | s[offset + 22] << 8 | s[offset + 23];
                        srcval2 = s[offset + 24] << 24
                            | s[offset + 25] << 16 | s[offset + 26] << 8 | s[offset + 27];
#ifdef QUIET8
                        fprintf(stderr, "* param value: 0x");
                        fprintf(stderr, "%.8x %.8x ", srcval, srcval2);
#endif
                        /* wd 7-8 bytes 0-3 has value */
                        srcval = s[offset + 28] << 24
                            | s[offset + 29] << 16 | s[offset + 30] << 8 | s[offset + 31];
                        srcval2 = s[offset + 32] << 24
                            | s[offset + 33] << 16 | s[offset + 34] << 8 | s[offset + 35];
#ifdef QUIET8
                        fprintf(stderr, "%.8x %.8x", srcval, srcval2);
                        fprintf(stderr, "\n");
#endif
                        break;
                    case 8: /* bit logical */
                        /* wd 5 bit 0 has value */
#ifdef QUIET8
                        fprintf(stderr, "* param value: ");
                        fprintf(stderr, "%s\n", s[offset + 20] & 0x80 ? ".TRUE. (1)" : ".FALSE. (0)");
#endif
                        break;
                    case 11:    /* char * n */
                        /* size has array length */
                        /* wds 5 - 8 has char values */
                        srccnt = s[offset + 6] << 8 | s[offset + 7];
#ifdef QUIET8
                        fprintf(stderr, "* param value (%d): ", srccnt);
                        if (srccnt > 16)
                            srccnt = 16;
                        for (k = 0; k < srccnt; k++)
                        {
                            fprintf(stderr, "%c", s[offset + 20 + k]);
                        }
                        fprintf(stderr, "\n");
#endif
                        break;
                    default:
                        goto jumpit;
                    }
                    break;
                case 0x04:      /* datapool */
                    if (dounix)
                    {
                        /* register variable, ignore */
                        break;
                    }
                    else
                    {
#ifdef QUIET8
                        /* print datapool name */
                        fprintf(stderr, "* len = %d Dpool: ", s[offset + 2]);
#endif
                    }
#ifdef QUIET8
                    for (k = 0; k < 8; k++)
                    {
                        fprintf(stderr, "%c", s[offset + 20 + k]);
                    }
                    fprintf(stderr, "\n");
#endif
                    break;
                case 0x08:      /* common symbol */
#ifdef QUIET8
                    if (dounix)
                    {
                        /* print struct/union ref name */
                        fprintf(stderr, "* len = %d S/U ref: ", s[offset + 2]);
                    }
                    else
                    {
                        /* print common name */
                        fprintf(stderr, "* len = %d Common: ", s[offset + 2]);
                    }
                    for (k = 0; k < 8; k++)
                    {
                        fprintf(stderr, "%c", s[offset + 20 + k]);
                    }
                    fprintf(stderr, "\n");
#endif
                    break;
                case 0x48:      /* array of structures */
                    xx = 28;    /* account for struct name */
                case 0x40:      /* array info present */
#ifdef QUIET8
                    fprintf(stderr, "* array dims: %d bnds: %d len: %d lbflags %x sbflags %x\n",
                            s[offset + xx + 0], s[offset + xx + 1], s[offset + xx + 4],
                            s[offset + xx + 2] << 8 | s[offset + xx + 3],
                            s[offset + xx + 6] << 8 | s[offset + xx + 7]);
                    fprintf(stderr, "* wds: ");
                    for (k = 0; k < s[offset + xx + 4] - 8; k++)
                    {
                        if (k > 0 && (k % 4 == 0))
                        {
                            if ((k % 20 == 0))
                                fprintf(stderr, "\n");
                            else
                                fprintf(stderr, " ");
                        }
                        fprintf(stderr, "%.2x", s[offset + xx + 8 + k]);
                    }
                    fprintf(stderr, "\n");
#endif
                    break;
                case 0x50:      /* array parameters */
#ifdef QUIET8
                    fprintf(stderr, "* array param dims: %d bnds: %d len: %d lbflags %x sbflags %x\n",
                            s[offset + 20], s[offset + 21], s[offset + 24],
                            s[offset + 22] << 8 | s[offset + 23], s[offset + 26] << 8 | s[offset + 27]);
                    fprintf(stderr, "* wds: ");
                    for (k = 0; k < s[offset + 24] - 8; k++)
                    {
                        if (k > 0 && (k % 20 == 0))
                            fprintf(stderr, "\n");
                        fprintf(stderr, "%.2x ", s[offset + 28 + k]);
                    }
                    fprintf(stderr, "\n");
#endif
                    break;
                case 0x01:      /* CSECT */
                case 0x02:      /* absolute address */
                case 0x10:      /* parameter/stack variable */
                case 0x20:      /* ext memory address/pointer */
#ifdef QUIET8
                    fprintf(stderr, "* unprocessed flags %x\n", s[offset + 3]);
#endif
                default:
                  jumpit:
#ifdef QUIET8
                    fprintf(stderr, "* Encore record flags = %x\n", s[offset + 3]);
                    fprintf(stderr, "Encore debugger info (hex dump)\n");
                    for (k = 0; k < s[offset + 2]; k++)
                    {
                        if (k > 0 && (k % 20 == 0))
                            fprintf(stderr, "\n");
                        fprintf(stderr, "%.2x ", s[offset + k]);
                    }
                    fprintf(stderr, "\n");
#endif
                    break;
                }
            }
          wedone:
            offset += s[offset + 2];
            break;
        case 15:                /* undefined */
#ifdef QUIET8
            for (k = 0; k < s[offset + 2]; k++)
            {
                if (k > 0 && (k % 20 == 0))
                    fprintf(stderr, "\n");
                fprintf(stderr, "%.2x ", s[offset + k]);
            }
            fprintf(stderr, "\n");
#endif
            offset += s[offset + 2];
            break;
        }
        break;

    case 14:                    /* common origin */
        /* get common block number */
        repeat = s[offset + 1];
        /* get common size */
        bound = s[offset + 2] << 8 | s[offset + 3];
#ifdef QUIET8
        fprintf(stderr, "common origin block = %x origin = %x\n", repeat, bound);
#endif
        offset += (objcnt + 1);
        break;

    case 15:                    /* object termination */
#ifdef QUIET
        fprintf(stderr, "program object termination\n");
#endif
        offset += (objcnt + 1);

        /* define dsect start */
        if ((ndefr + 1) >= def_size)
        {                       /* we need to resize */
            defined = (struct defr *)realloc((void *)defined, (def_size + 10) * sizeof(struct defr));
            def_size += 10;     /* 10 more */
        }
        pdefp = &defined[ndefr];    /* ref data address */
        strcpy(pdefp->name, "DS");  /* dsect start */
        ndefr++;                /* next reference */
        pdefp->refa = sect_base[0] + last_ds;   /* ds start */
        pdefp++;

        if (sect_size[1])
        {
            /* define csect start */
            strcpy(pdefp->name, "CS");  /* dsect start */
            ndefr++;            /* next reference */
            pdefp->refa = sect_base[0]; /* section start */
            pdefp++;
        }

        /* dump csect/dsect */
        curr_sect = 0;          /* dsect */
        /* if no size given, use last origin address */
        if (sect_size[curr_sect] == 0)
            sect_size[0] = sect_org[0];
#ifdef NODUMP
        fprintf(stderr, "DSECT contents %x bytes\n", sect_size[0]);
        sa = sect_addr[0];      /* address of section */
        temp2 = 0;
        for (j = 0; j < sect_size[0]; j += 4)
        {
            fprintf(stderr, "%.4x ", j);
            for (i = 0; i < 4; i++)
            {
                fprintf(stderr, "%0.2x", sa[j + i]);
            }
            fprintf(stderr, " ");
            for (i = 0; i < 4; i++)
            {
                if (sa[j + i] >= 0x20 && sa[j + i] < 0x7f)
                    fprintf(stderr, "%c", sa[j + i]);
                else
                    fprintf(stderr, ".", sa[j + i]);
            }
            fprintf(stderr, "\n");
        }
#endif
#ifdef DOTRACE
        if (sect_size[0])
            codedump(0);        /* go dump the code */
#endif

        curr_sect = 1;          /* csect */
#ifdef NODUMP
        fprintf(stderr, "CSECT contents %x bytes\n", sect_size[1]);
        sa = sect_addr[1];      /* address of section */
        temp2 = 0;
        for (j = 0; j < sect_size[1]; j += 4)
        {
            fprintf(stderr, "%.4x ", j);
            for (i = 0; i < 4; i++)
            {
                fprintf(stderr, "%02x", sa[j + i]);
            }
            fprintf(stderr, " ");
            for (i = 0; i < 4; i++)
            {
                if (sa[j + i] >= 0x20 && sa[j + i] < 0x7f)
                    fprintf(stderr, "%c", sa[j + i]);
                else
                    fprintf(stderr, ".", sa[j + i]);
            }
            fprintf(stderr, "\n");
        }
#endif
#ifdef DONOTDOCSECT
        if (sect_size[1])
            codedump(1);        /* dump the instructions */
#endif

        /* we may have several objects, start over */
        free((void *)datapool); /* free storage */
        free((void *)external); /* free storage */
        free((void *)defined);  /* free storage */
        for (i = 0; i < 256; i++)   /* check all sections */
            if (sect_addr[i])   /* if we got one */
                free((void *)sect_addr[i]); /* free memory */
        doem = 0;               /* reinitialize if more object */
        break;
    }
    goto doit;
}

unsigned char line[BUFSIZ];
int cmpop = 0;
int cmpflg = 0;
int bcnt = 0;
unsigned char *bptr = 0;
int recl = 0;

int rbl(unsigned char *buf, int n)
{
    int count = 0;
    unsigned char *cp;
    int i;

    unsigned char *linadrs = line;

    if (!cmpop)
    {                           /* see if we tested for compressed */
        cmpop = 1;              /* set comp tested flag */
        /* read in the first record */
        if ((recl = getloi(line, BUFSIZ)) == 0)
            return (0);         /* this means eof */
        linadrs = line;
        if (*linadrs == 0xbf || *linadrs == 0x9f)
        {                       /* is this file compressed */
            cmpflg = 1;         /* set comp data flag */
            bcnt = linadrs[1];  /* set record count */
            bptr = &linadrs[6]; /* set data address */
        }
        else
            goto re00;
    }

    if (cmpflg)
    {                           /* reading compressed data? */
        if (bcnt == 0)
        {                       /* any data left in buffer */
          re18:
            /* read in a data record */
            if ((recl = getloi(line, BUFSIZ)) == 0)
                return (0);     /* this means eof */
            linadrs = line;
            if ((*linadrs & 0xdf) != 0x9f)  /* is this valid rec */
                return (EOF);   /* error if not */
            bcnt = linadrs[1];  /* set record count */
            bptr = &linadrs[6]; /* set data address */
        }
      re20:
        /* see if any blanks */
        if (i = *bptr++)
        {                       /* next buffer pointer */
            if (i == 0xff)
                goto re60;      /* if eol, get out */
            while (i--)
            {
                if (count < n)
                {
                    *buf++ = ' ';   /* put blank in buffer */
                    count++;
                }
            }
        }
        if (--bcnt <= 0)
            goto re18;          /* read next record */

        if (i = *bptr++)
        {                       /* next buffer pointer */
            while (i--)
            {
                if (count < n)
                    *buf++ = *bptr; /* put char in buffer */
                bcnt--;         /* decr count */
                bptr++;         /* next buffer pointer */
                count++;
            }
        }
        if (--bcnt <= 0)
            goto re18;          /* read next record */
        goto re20;

      re60:
        bcnt--;                 /* decr count */
        if ((*--buf == ' ') && (count == 1))
        {
            *buf = '\n';        /* put new line at eol */
            *++buf = '\0';      /* put new line at eol */
        }
        else
        {
            count++;
            *++buf = '\n';      /* put new line at eol */
            *++buf = '\0';      /* put new line at eol */
        }
/* fprintf(stderr, "ret cnt = %ld\n", count); */
        return (count);
    }
    else
    {
        /* non compressed read here */
        /* read the next record */
        if ((recl = getloi(line, BUFSIZ)) == 0)
            return (0);         /* this means eof */

      re00:
        /* here we need to strip off blank put in during write */
        /* this is because mpx does not support zero length blocks */
        if (!binary)
        {
            if ((recl == 1) && (*linadrs == ' '))
                recl = 0;
            /* now append new line to end of buffer */
            *(cp = linadrs + recl) = '\n';  /* point to last char */
        }
        else
        {
            cp = linadrs + recl - 1;    /* point to last char */
        }

        /* copy this layer buffer to upper caller's buffer */

/* fprintf(stderr, "recl = %x, cp = %x, linadrs = %x\n", recl, cp, linadrs); */
        while (count < n)
        {
            if (linadrs > cp)
                break;
            *buf++ = *linadrs++;
            count++;
        }
    }

/* fprintf(stderr, "ret1 cnt = %ld\n", count); */
    return (count);
}

/*~!bbio.c*/
/* Name:  bbio.c Part No.: _______-____r
 *
 * Copyright 1991 - J B Systems, Morrison, CO
 *
 * The recipient of this product specifically agrees not to distribute,
 * disclose, or disseminate in any way, to any one, nor use for its own
 * benefit, or the benefit of others, any information contained  herein
 * without the expressed written consent of J B Systems.
 *
 *                     RESTRICTED RIGHTS LEGEND
 *
 * Use, duplication, or disclosure by the Government is  subject  to
 * restriction  as  set forth in paragraph (b) (3) (B) of the Rights
 * in Technical Data and Computer Software  Clause  in  DAR  7-104.9
 * (a).
 */

#ident  "@(#)nbrtl:bbio.c   1.0"

#include <stdio.h>

/********************************************************************/
/* These routines handle blocked and optionally compressed MPX files */

#define IOCMAX 10               /* start with 10 for now */

struct ioc {
    int iofcb;                  /* fcb for i/o */
    int afcb;                   /* callers fcb address */
    unsigned char *bufaddr;     /* callers buffer address */
    int bufcnt;                 /* callers buffer size */
    unsigned char *cba;         /* current buffer address */
    unsigned char *rcba;        /* record control block pointer */
    int cpp;                    /* current pool position */
    int cfp;                    /* current file position */
    char cbn;                   /* current buffer number */
    char nab;                   /* number of active buffers */
    struct {                    /* ioc bit flags */
        unsigned openop:1;      /* open flag */
        unsigned writop:1;      /* last op was write */
        unsigned outaop:1;      /* output active flag */
        unsigned compop:1;      /* if set, tested for comp'ed file */
        unsigned cmpflg:1;      /* if set, reading comp'ed file */
        unsigned iocaloc:1;     /* ioc allocated */
        unsigned free:3;        /* free flags, available */
    } flag;
    int bcnt;                   /* compressed rec cur count */
    unsigned char *bptr;        /* compressed rec pointer */
    unsigned char *bufa;        /* start of contiguous buffers */
};

struct ioc iocx[IOCMAX];

#define ERRFLAG 0x04000000
#define EOFFLAG 0x02000000
#define EOMFLAG 0x01000000

#define RCBEOF 0x80             /* bit 0 */
#define RCBBOB 0x40             /* bit 1 */
#define RCBEOB 0x20             /* bit 2 */
#define RCBNULL 0x10            /* bit 3 */
#define RCBCONT 0x08            /* bit 4 */

#define SBLR 0
#define BCLR 1
#define SBTR 2
#define BCTR 3

#define BLKSIZE 768
#define BLKS (10*BLKSIZE)

/* forword definition */
int bfredf(struct ioc *wioc);
int plredf(struct ioc *wioc);

/* ircont - establish ioc address for this fcb */
/* input - file number */
/* output - ioc address */
struct ioc *ircont(int cfcb)
{
    int i;
    struct ioc *wioc;           /* current ioc */

    for (i = 0, wioc = &iocx[0]; i < IOCMAX; wioc = &iocx[++i])
    {
        if (wioc->flag.iocaloc)
            if (wioc->afcb == cfcb)
            {
                return (wioc);
            }
    }
    /* not yet allocated, get a new one */
    for (i = 0, wioc = &iocx[0]; i < IOCMAX; wioc = &iocx[++i])
    {
        if (!wioc->flag.iocaloc)
        {
            wioc->flag.iocaloc = 1; /* ioc allocated */
            if ((wioc->bufa = (unsigned char *)malloc(BLKS)) == 0)
            {
                fprintf(stderr, "ioc malloc error\n");
                return (0);
            }
            /* initialize new ioc */
            wioc->afcb = cfcb;  /* save callers fcb address */
            wioc->iofcb = cfcb; /* use callers fcb for I/O */
            wioc->cba = wioc->bufa; /* set buffer addr in fcb */
            wioc->cbn = 0;      /* no curr buf number */
            wioc->nab = 0;      /* no active bufs */
            /* clear flags */
            wioc->flag.openop = 0;  /* open ioc */
            wioc->flag.writop = 0;
            wioc->flag.outaop = 0;
            wioc->flag.compop = 0;
            wioc->flag.cmpflg = 0;
            wioc->cpp = 0;      /* pool empty */
            wioc->bcnt = 0;     /* no compress cnt */
            wioc->bptr = 0;     /* no pointer either */
            wioc->cfp = 1;      /* curr position is 1st blk */
            return (wioc);
        }
    }
    fprintf(stderr, "no ioc space left\n");
    return (0);
}

/* rmopen - open up a file stream */
int rmopen(int cfcb)
{
    struct ioc *wioc;

    wioc = ircont(cfcb);
    if (wioc->flag.openop)
        return (1);             /* return o.k. */
    /* not open yet, do open */
    wioc->flag.openop = 1;      /* mark open */
    return (1);                 /* return o.k. */
}

/* rmclose - close a file stream */
int rmclose(int cfcb)
{
    struct ioc *wioc;

    wioc = ircont(cfcb);
    if (!wioc->flag.openop)
        return (1);             /* if not open, return o.k. */
    /* open, do close */
    wioc->flag.openop = 0;      /* mark closed */
    wioc->flag.iocaloc = 0;     /* mark unallocated */
    free(wioc->bufa);           /* free buffer */
    return (1);
}

/* rmread - read from file stream */
int rmread(int cfcb, unsigned char *buffer)
{
    struct ioc *wioc;
    int fillcnt, i, bytecnt;
    unsigned char *buffp;

    rmopen(cfcb);               /* make sure open */
    wioc = ircont(cfcb);
    if (wioc->flag.writop)
        fprintf(stderr, "read after write not allowed");
    if (wioc->cpp == 0)
    {                           /* if no data in pool yet, read it in */
        bfredf(wioc);           /* read in a block */
        wioc->rcba = wioc->cba + 4;
        /* have we tested for comp data yet */
        if (!wioc->flag.compop)
        {
            wioc->flag.compop = 1;
            /* test for comp rec */
            if (wioc->rcba[4] == 0xbf || wioc->rcba[4] == 0x9f)
            {
                wioc->flag.cmpflg = 1;  /* this is comp data */
                wioc->bcnt = 0; /* init pointer */
            }
        }
    }
    bytecnt = 0;                /* no bytes to caller yet */
    buffp = buffer;             /* save caller buff addr */
    if (wioc->flag.cmpflg)      /* reading compressed */
        if (wioc->bcnt)
            goto re07;          /* any data left */
  re18:
    if (wioc->rcba[SBLR] & RCBEOB)
    {
      re06:
        if (bfredf(wioc) == -1)
            return (-1);
        wioc->rcba = wioc->cba + 4;
    }
#define JIM                     /* define for non-library reading */
#ifdef JIM
    if (wioc->rcba[SBTR] & RCBEOF)
    {
#else
    if (wioc->rcba[SBTR] & RCBEOF)
    {
        wioc->rcba[SBTR] &= ~RCBEOF;
        goto re06;
#endif
      re05:
        return (-1);            /* return EOF */
    }
    if (wioc->flag.cmpflg)
    {                           /* reading compressed */
        if ((wioc->rcba[4] & 0xdf) != 0x9f)
        {
            return (-1);        /* return EOF */
        }
        wioc->bptr = wioc->rcba + 10;   /* first data byte */
        wioc->bcnt = wioc->rcba[5]; /* get bytes this record */
        if (wioc->bcnt == 0)
        {
          re10:
            wioc->rcba += (wioc->rcba[BCTR] + 4);
            goto re18;
        }
      re07:
        if (*wioc->bptr)
        {                       /* got blanks */
            if (*wioc->bptr == 0xff)
            {
                wioc->bptr++;
                if (--wioc->bcnt == 0)
                {
                    wioc->rcba += (wioc->rcba[BCTR] + 4);
                    wioc->bptr = wioc->rcba + 10;
                }
                wioc->bufcnt = bytecnt;
                return (bytecnt);
            }
            for (i = *wioc->bptr; i; *buffp++ = ' ', i--, bytecnt++);
        }
        wioc->bptr++;
        if (--wioc->bcnt > 0)
        {
            if (i = *wioc->bptr)
            {
                wioc->bptr++;
                wioc->bcnt--;
                for (; i; i--)
                {
                    *buffp++ = *wioc->bptr++;
                    bytecnt++;
                    wioc->bcnt--;
                }
                if (wioc->bcnt)
                    goto re07;
                goto re10;
            }
          re24:
            wioc->bptr++;
            if (--wioc->bcnt > 0)
                goto re07;
        }
        goto re10;
    }

    /* process uncompressed record */
    wioc->bufcnt = wioc->rcba[BCTR];
    for (i = 0; i < wioc->bufcnt; i++)
        buffer[i] = wioc->rcba[i + 4];
    wioc->rcba += (wioc->rcba[BCTR] + 4);
    return (wioc->bufcnt);
}

/* bfredf - read next buffer from file stream */
int bfredf(struct ioc *wioc)
{
    unsigned char *cbaddr;

    /* see if next buffer in memory */
    if (wioc->cbn >= wioc->nab)
    {
        /* no, read in next pool */
        if (plredf(wioc) == -1) /* read next pool */
            return (-1);        /* show EOF */
        wioc->cbn = 1;          /* we start at 1st buffer */
        cbaddr = wioc->bufa;    /* reset address pointer */
    }
    else
    {
        wioc->cbn++;            /* bump buffer number */
        cbaddr = wioc->cba + BLKSIZE;   /* next buffer address */
    }
    wioc->cba = cbaddr;         /* set new address */
    /* check for cnt < 0x300 (768) */
    /* check for BOB on in control bits */
    return (1);                 /* return O.K. */
}

/* plredf - read next buffer pool from file stream */
int plredf(struct ioc *wioc)
{
    int retc;

    /* read the file */
    retc = read(wioc->iofcb, wioc->bufa, BLKS);
    if (retc <= 0)
    {
        return (-1);            /* show EOF for error */
    }
    /* compute # of block we read */
    wioc->nab = (retc + BLKSIZE - 1) / BLKSIZE;
    wioc->cpp = wioc->cfp;      /* update curr pool position */
    wioc->cfp += wioc->nab;     /* update current file position */
    return 0;
}

/*
 * Dump without executing it to file "testcode.mem
 */
void codedump(int sect)
{
    int i;
    uint32 *memory;             /* memory address */
    int tr_start;               /* first loc to trace */
    int tr_stop;                /* last loc to trace */
    FILE *fp;

    if ((fp = fopen("testcode.mem", "w")) == NULL)
    {
        fprintf(stderr, "unable to open file 'testcode.mem' for output, aborting.\n");
        return;
    }
    tr_start = sect_base[sect]; /* starting psw */
    if (tr_start != 0)
    {
        fprintf(stderr, "program does not start at zero, aborting.\n");
        return;
    }
    memory = (uint32 *)sect_addr[sect];    /* real memory pointer */
    tr_stop = sect_base[sect] + sect_size[sect];
#ifndef QUIET
    fprintf(stderr, "tr_start %x tr_stop %x memory %x\n", tr_start, tr_stop, (uint32 *)memory);
#endif
    /* write out the memory to disk */
    fwrite(memory, sizeof(char), sect_size[sect], fp);
    while (tr_start < tr_stop)
    {
        uint32 data = *memory;
        uint32 byte;

#ifdef QUIET
        fprintf(stderr, "addr %0x - data %0x\n", (int32)tr_start, *memory);
#else
        fprintf(stderr, "addr %0x - data %0.2x%0.2x%0.2x%0.2x\n", (int32)tr_start,
            (data >> 0) & 0xff,
            (data >> 8) & 0xff,
            (data >> 16) & 0xff,
            (data >> 24) & 0xff);
#endif
#ifdef NOBIGENDIAN
        byte = (data >> 24) & 0xff;
        fputc(byte, fp);        /* 1st char */
        byte = (data >> 16) & 0xff;
        fputc(byte, fp);        /* 2nd char */
        byte = (data >> 8) & 0xff;
        fputc(byte, fp);        /* 3rd char */
        byte = data & 0xff;
        fputc(byte, fp);        /* 4th char */
        fputc('\n', fp);        /* cr */
#endif
        memory++;
        tr_start += 4;
    }
    return;
}
