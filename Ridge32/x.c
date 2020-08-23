#include <stdio.h>

int main(int argc, char *argv[]) {

    int ch;

    while ((ch = getchar()) != EOF) {
        ch = ((ch >> 4) & 0xf) | ((ch & 0xf) << 4);
//        int xch, i;
 //       for (xch = i = 0; i < 7; i++) {
  //          xch |= (ch & 1);
   //         xch <<= 1;
    //        ch >>= 1;
     //   }
        putchar(ch);
   }
}
