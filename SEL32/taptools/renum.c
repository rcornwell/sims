/*
 * renum.c
 *
 * This program truncates a line or expands a line with blanks to
 * have a line length of 72 chars.  A 7 digit line number is then
 * append to the line.  Numbers are in the form XXXX.000.
 * input - stdin   
 * output - stdout
 */

#include <stdio.h>
#include <stdlib.h>

/* get a line of input. */
int ln = 1;
int getloi(s, lim)                    /*  right from the book.  */
char s[];
int lim;
{
    int c, i, j;
    char line[12];

    for (i=0; --lim > 0;)
    {
        if ((c = getchar()) == EOF)
            return 0;
        if (c == '\r')
            continue;
        if (c == '\n')
            break;
        s[i++] = c;
    }
    if (i > 72)             /* truncate at char 72 */
        i = 72;
    for (; i<72; i++)
        s[i] = ' ';
//  sprintf(line, "%04d.000", ln++);
    sprintf(line, "%04d.%01d00", ln/10, ln%10);
    ln++;
    for (j=0; j<8; j++)
        s[72+j] = line[j];
    s[80] = '\n';
    s[81] = '\0';
    return (i);
}

int main (argc, argv)
int argc;
char *argv[];
{
    char s[BUFSIZ];

    while (1) {
        if (getloi(s, BUFSIZ) == 0)
            exit(0);
        /* output line of text from the source */
        printf("%s", s);
    }
}

