/*
   ddump - file dump utility for listing files in side-by-side hex
   and ascii, plus having the option to modify the file giving
   a hexadecimal byte offset into the file and the hex data
   which will be written to the supplied address.
   Usage is:

       ddump file

   where:

       file1    is the file to dump and/or modify.

   written by Rick Beery
   modified many times by Jim Bevier

   The program fd.c written by M. Kraieski has been incorporated
   into this program.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dodump();
int modify();
void modify_file();

int main(argc,args)
int argc;
char *args[];
{
  FILE *fp, *fopen();
  int i;
  char *tempstring;

  if ((argc < 2) || (argc > 3)) {
     printf("usage: %s [-r] file\n", args[0]);
     printf("        -r uses read-only mode\n");
     exit(1);
  } /* end of if */

  if (argc == 3) 
#ifdef mpx
     /* open read and unblocked */
     if (strcmp("-r",args[1]) == 0)  fp = fopen(args[2],"ru");
#else
     if (strcmp("-r",args[1]) == 0)  fp = fopen(args[2],"r");
#endif
     else  {
    printf("Invalid option %s ignored\n", args[1]);
#ifdef mpx
        /* open read/write unblocked */
        fp = fopen(args[2],"r+u");
#else
        fp = fopen(args[2],"r+");
#endif
     }
#ifdef mpx
  /* open read/write unblocked */
  else  fp = fopen(args[1],"r+u");
#else
  else  fp = fopen(args[1],"r+");
#endif

  if (fp == NULL) {
     fprintf(stderr,"%s: fopen: unable to open file %s\n", args[0], args[1]);
     exit(1);
  } /* end of if */
  dodump(fp,0);
  fclose(fp);
} /* end of main */

#define PRINTABLE(x) ((x < 32) || (x > 126)) ? '.' : x

void dodump(fp, fileadr)
FILE *fp;
int  fileadr;
{
   char buff[257];
   int  file_byte_count=0, curchar, buffptr, bufflen;

   buffptr = 0;
   bufflen = 16;

   fseek(fp,fileadr,0);
   while ((curchar = getc(fp)) != EOF) {
     file_byte_count++;
     if (!buffptr) printf(" %06x : ",fileadr);
     printf("%02x",curchar);
     buff[buffptr++] = PRINTABLE(curchar);
     if (!(buffptr % 4)) printf(" ");
     if (buffptr >= bufflen) {
        buff[buffptr] = 0;
        printf(" |%s|\n",buff);
        buffptr = 0;
        fileadr += bufflen;
    if (!(file_byte_count % 256)) {
       modify_file(fp);
        } /* end of if */
     } /* end of if */
   } /* end of while */

   if (buffptr) {
      buff[buffptr] = 0;
      while (buffptr++ < bufflen) {
         printf("  ");
         if (!(buffptr % 4)) printf(" ");
      } /* end of while */
      printf(" |%s|\n",buff);
   } /* end of if */
   modify_file(fp);
   return;
} /* end of dodump */

int modify(fp1)
   FILE   *fp1;
{
   char hexstring[33];
   int  num_chars, digit, i, byte=0, stat, file_offset=0, indx=0;
   int  power();
   void gethex_string();
   int  asciihex_digit_to_decimal();

   gethex_string(hexstring);
   num_chars = strlen(hexstring); 
   for (i=num_chars-1; i>=0; i--) {
      digit = asciihex_digit_to_decimal(hexstring[indx++]);
      file_offset += digit*power(16,i);
   } /* end of for */
   if ((fseek(fp1,file_offset,0)) != -1) {
      /* read characters from the terminal */
      printf("\nenter new hex data > ");
      gethex_string(hexstring);
      num_chars = strlen(hexstring);

      /* convert character to numeric field and 
         new set of data at offset in file specified */
      for (i=0; i<num_chars; i++) {
     digit = asciihex_digit_to_decimal(hexstring[i]);
     if ((i%2)) {
       byte = byte*16 + digit;
       fputc(byte, fp1);
       byte = 0;
         }
     else {
       byte = digit;
         }
      } /* end of for */
   } /* end of "if" part of if - else */
   else {
      printf("Unable to seek to that address\n");
      file_offset = 0;
   } /* end of if - else */
   return(file_offset);
} /* end of modify */

void gethex_string(hex)
   char *hex;
{
   char c;
   int  indx=0;

   while ((c=getchar()) != '\n' ) {
      if ((c>='0' && c<='9') || (c>='a' && c<='f')) 
         hex[indx++] = c;
      else {
         while ((c=getchar()) != '\n' );
     indx = 0;
     printf("Invalid hex number; please re-enter > "); 
      }
   }
   hex[indx] = '\0';
} /* end of gethex_string */

int asciihex_digit_to_decimal(c)
   char c;
{
   int digit;

     if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
         else if (c>= '0' && c <= '9') digit  = c -'0'; 
          else digit = -1; /* error */
     return(digit);
} /* end of asciihex_digit_to_decimal */ 

int power(x,n)
   int x, n;
{
    int  p;

    for (p=1; n>0; --n)
       p = p*x;
    return (p);
} /* end of routine power */

void modify_file(fp)
   FILE *fp;
{
   int ans, modify(),modified_addr;

   printf("\n hex address,     <cr> - continue,       q = quit > ");
   ans = getchar();
   if ((ans>='0' && ans<='9') || (ans>='a' && ans<='f')) {
      ungetc(ans, stdin);
      modified_addr = modify(fp);
      printf("\n");
      dodump(fp, (modified_addr/256)*256);
   } /* end of "if" part of if - then */
   else {
      if (ans == 'q') {
         fclose(fp);
     exit(1);
      }
      else {
     if (ans != '\n') while ((ans=getchar()) != '\n' );
      }
   } /* end of if -then */
} /* end of modify_file */
