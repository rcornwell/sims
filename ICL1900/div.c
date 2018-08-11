#include <stdio.h>
#include <stdlib.h>

#define N 24
#define B0 (1<<N)
#define B1 (1<<(N-1))
#define M0 ((1<<(N))-1)
main(int argc, char *argv[]) {
   int n = N;
   int a = 0;
   int m = atoi(argv[2]);
   int q = atoi(argv[1]);

printf("0: n=%d B0=%08o B1=%08o M0=%08o\n", n, B0, B1, M0);
do {
      a <<= 1;
      q <<= 1;
      if (q & B0)
         a |= 1;
   if (a & B0) {
       a = a+m;
   } else {
       a = a + (m^ M0) + 1;
   }
   a &= M0;
   q &= M0;
   if ((a& B1)  == 0)
      q|= 1;
printf("1: n=%d m=%08o a=%08o q=%08o\n", n, m, a, q);
  n--;
 } while (n != 0);
if (a & B1)
  a = a+m;
printf("2: n=%d m=%08o a=%08o q=%08o\n", n, m, a, q);
}

