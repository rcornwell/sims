/*
 * small.c
 *
 * This program deletes trailing blanks from a source file
 * option - -a to remove all trailing spaces
 *          -h help
 * input - file or stdin
 * output - file or stdout
 */

#include <stdio.h>
#include <stdlib.h>

int getloi();
int putloi();
int all = 0;                        /* do all, set non zero by -a */

int main(argc, argv)
int argc;
char *argv[];
{
    FILE    *ifp=NULL, *ofp=NULL, *fopen();
    int     iu=0, ou=0;
    char    *p, *name = *argv;
    char    s[BUFSIZ];

    all = 0;
    if (argc <= 1) {
doerr:
        fprintf(stderr, "Usage: %s [-ah] [infile] [outfile]\n", name);
        exit(1);
    }
    /* check for any options */
    while(--argc > 0) {
        p = *++argv;
        if (*p == '-') {
            if (*++p == '\0') {
                fprintf(stderr, "Error: no option specified\n");
                goto doerr;
            }
            while (*p != '\0') {
                switch (*p++) {
                    case 'a':   /* user wants all spaces removed */
                    case 'A':
                        all = 1;    /* remove all trailing blanks */
                        break;
                    case 'h':   /* user wants help */
                    case 'H':
                    default:
                        goto doerr;
                }
                continue;
            }
            continue;
        }
        /* process in, then out file */
        if (ifp == NULL) {
        fprintf(stderr, "4 argc %x argv %s\n", argc, p);
            if ((ifp = fopen(*argv, "r")) == NULL) {
                fprintf(stderr,
                    "Error: can't open input file %s\n", *argv);
                exit(1);
            }
            continue;
        }
        /* process out file */
        if (ofp == NULL) {
            if ((ofp = fopen(*argv, "w")) == NULL) {
                fprintf(stderr,
                    "Error: can't open output file %s\n", *argv);
                exit(1);
            }
            continue;
        }
        fprintf(stderr,
            "Error: too many parameters %s\n", *argv);
        goto doerr;
    }
    if (ifp == NULL) {
        ifp = stdin;            /* use stdin */
        iu = 1;
    }
    if (ofp == NULL) {
        ofp = stdout;           /* use stdout */
        ou = 1;
    }
    fflush(stderr);

    /* copy in to out */
    while (1) {
        if (getloi(ifp, s, BUFSIZ) <= 0) {  /* read til EOF */
            if (iu == 0)
                fclose(ifp);
            if (ou == 0)
                fclose(ofp);
            exit(0);
        }
        putloi(ofp, s);
    }
    exit(0);
}

/* get a line of input. */
int getloi(ifp, s, lim)         /*  right from the book.  */
FILE *ifp;
char s[];
int lim;
{
    int c, i, cr=0;

#ifdef _WIN32
    cr = 1;                     /* if on windows, add c/r */
#endif
    for (i=0; --lim > 0;) {
        if ((c = fgetc(ifp)) == EOF)
            return 0;
        if (c == '\r') {
            cr = 1;
            continue;
        }
        if (c == '\n')
            break;
        s[i++] = c;
    }
    if (i > 72)                 /* truncate at char 72 */
        i = 72;
    for (; i<72; )
        s[i++] = ' ';
    if (cr)                     /* only put in c/r if one wanted */
        s[i++] = '\r';
    s[i++] = '\n';              /* add a newline */
    s[i] = '\0';

    if (all == 0)               /* see if all spaces to be removed */
        return (i);             /* no, just return */

    /* the user wants to keep removing spaces */
    if ((i > 1) && ((s[i-1] == '\n') || s[i-1] == '\r')) {
        while (((s[i-2] == ' ') || (s[i-2] == '\r') ||
            (s[i-2] == '\n')) && (i > 1))
            --i;
        if (cr) {               /* put in c/r if required */
            s[i-1] = '\r';
            i++;
        }
        s[i-1] = '\n';          /* add the newline */
    }
    s[i] = '\0';
    return (i);
}

/*
 **  output line of text from the source
 */
int putloi(ofp, s)
FILE *ofp;
char *s;
{
    fprintf(ofp, "%s", s);      /* printf the line */
    return(0);
}
