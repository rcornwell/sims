/*
 * small.c
 *
 * This program deletes trailing blanks from a source file
 * input - stdin   
 * output - stdout
 */

#include <stdio.h>
#include <stdlib.h>

int getloi();
int putloi();

int main(argc, argv)
int argc;
char *argv[];
{
    char s[BUFSIZ];

    while(1) {
        if (getloi(s, BUFSIZ) == 0)
            exit(0);
        putloi(s);
    }
}
        
/* get a line of input. */
int getloi(s, lim)                    /*  right from the book.  */
char s[];
int lim;
{
    int c, i;

    for (i = 0; --lim > 0 && (c = getchar()) != EOF && (s[i++] = c) != '\n'; )
    {
        ;
    }
#define STRIP_LINE
#ifdef STRIP_LINE
    if (i > 73)
        i = 73;
    s[72] = '\n';
    s[73] = '\0';
#endif
    if ((s[i-1] == '\n') && (i > 1))
    {
        while (((s[i-2] == ' ') || (s[i-2] == '\r')) && (i > 1))
            --i;
        s[i-1] = '\n';
    }
    s[i] = '\0';
    return (i);
}

/*
 **  output line of text from the source
 */
int putloi(s)
char *s;
{
    printf("%s", s);
}
