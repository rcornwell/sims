
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <strings.h>
#include <ctype.h>

#define TAPE_BUFFER_SIZE 100000L


#define TAPE_IRG 0200
#define BCD_TM 017

unsigned char	buffer[TAPE_BUFFER_SIZE];
char		*xlat;		/* Pointer to translate table */
int		eor = 0;	/* Report eor */
int		bin = 0;	/* Doing binary */
int		p7b = 0;	/* Doing BCD tape */
int		cc = 0;		/* Process print control chars */
typedef unsigned long int	uint32;	/* Header unit */


char parity_table[64] = {
        /* 0    1    2    3    4    5    6    7 */
        0000,0100,0100,0000,0100,0000,0000,0100,
        0100,0000,0000,0100,0000,0100,0100,0000,
        0100,0000,0000,0100,0000,0100,0100,0000,
        0000,0100,0100,0000,0100,0000,0000,0100,
        0100,0000,0000,0100,0000,0100,0100,0000,
        0000,0100,0100,0000,0100,0000,0000,0100,
        0000,0100,0100,0000,0100,0000,0000,0100,
        0100,0000,0000,0100,0000,0100,0100,0000
};

char bcd_ascii[64] = {
	'_',	/* 0           - space */
	'1',	/* 1        1  - 1 */
	'2',	/* 2       2   - 2 */
	'3',	/* 3       21  - 3 */
	'4',	/* 4      4    - 4 */
	'5',	/* 5      4 1  - 5 */
	'6',    /* 6      42   - 6 */
	'7',	/* 7	  421  - 7 */
	'8',	/* 8     8     - 8 */
	'9',	/* 9     8  1  - 9 */
	'0',	/* 10    8 2   - 0 */
	'=',    /* 11    8 21  - equal */
	'\'',	/* 12    84    - apostrophe */
	':',    /* 13    84 1  - colon */
	'>',	/* 14    842   - greater than */
	'"',	/* 15    8421  - radical 017 {? */
	' ',    /* 16   A      - substitute blank */
	'/',	/* 17   A   1  - slash */
	'S',	/* 18   A  2   - S */
	'T',	/* 19   A  21  - T */
	'U',	/* 20   A 4    - U */
	'V',	/* 21   A 4 1  - V */
	'W',	/* 22   A 42   - W */
	'X',	/* 23   A 421  - X */
	'Y',	/* 24   A8     - Y */
	'Z',	/* 25   A8  1  - Z */
	'#',	/* 26   A8 2   - record mark */
	',',	/* 27   A8 21  - comma */
	'(',	/* 28   A84    - paren */
	'`',	/* 29   A84 1  - word separator */
	'\\',	/* 30   A842   - left oblique */
	'{',    /* 31   A8421  - segment mark */
	'-',	/* 32  B       - hyphen */
	'J',	/* 33  B    1  - J */
	'K',	/* 34  B   2   - K */
	'L',	/* 35  B   21  - L */
	'M',	/* 36  B  4    - M */
	'N',	/* 37  B  4 1  - N */
	'O',	/* 38  B  42   - O */
	'P',	/* 39  B  421  - P */
	'Q',	/* 40  B 8     - Q */
	'R',	/* 41  B 8  1  - R */
	'!',	/* 42  B 8 2   - exclamation */
	'$',	/* 43  B 8 21  - dollar sign */
	'*',	/* 44  B 84    - asterisk */
	']',	/* 45  B 84 1  - right bracket */
	';',    /* 46  B 842   - semicolon */
	'_',    /* 47  B 8421  - delta */
	'+',    /* 48  BA      - ampersand or plus */
	'A',	/* 49  BA   1  - A */
	'B',    /* 50  BA  2   - B */
	'C',	/* 51  BA  21  - C */
	'D',	/* 52  BA 4    - D */
	'E',	/* 53  BA 4 1  - E */
	'F',	/* 54  BA 42   - F */
	'G',	/* 55  BA 421  - G */
	'H',	/* 56  BA8     - H */
	'I',	/* 57  BA8  1  - I */
	'?',	/* 58  BA8 2   - question mark 032 */
	'.',	/* 59  BA8 21  - period */
	')',	/* 60  BA84    - paren */
	'[',	/* 61  BA84 1  - left bracket 035 */
	'<',	/* 62  BA842   - less than 036 */
	'}'	/* 63  BA8421  - group mark 037 */
};

void usage() {
   fprintf(stderr,"Usage: listtape [-b] [-e] [-p] [-r#] <tapefile>\n");
   fprintf(stderr,"     -r#: Characters per record #\n");
   fprintf(stderr,"     -a:  Auto Binary/BCD translation\n");
   fprintf(stderr,"     -b:  Use IBSYS binary translation\n");
   fprintf(stderr,"     -m:  Show record marks |\n");
   fprintf(stderr,"     -e:  Show end of records as {\n");
   fprintf(stderr,"     -p:  Read BCD tape instead of TAP format\n");
   fprintf(stderr,"     -l:  Process listing control chars\n");
   exit(1);
}

/* Read one record from tape */
int read_tape(FILE *f, int *len) {
   unsigned long int sz;
   *len = 0;
   if (p7b) {
	static unsigned char lastchar = 0xff;
	unsigned char	    ch;
        sz = 0;
	/* Check if last char was Tape Mark */
	if (lastchar == (BCD_TM|TAPE_IRG)) {
	    lastchar = 0xff;
	    *len = -1;
	    return 1;
	}
	if (lastchar != 0xff)
	    buffer[sz++] = lastchar;
	lastchar = 0xff;
	while(fread(&ch, sizeof(unsigned char), 1, f) == 1) {
            if (sz == 0 && lastchar == 0xff)
               ch &= ~TAPE_IRG;
	    if (ch & TAPE_IRG) {
	        lastchar = ch;
		*len = sz;
	        return 1;
	    }
	    buffer[sz++] = ch;
	}
	if (sz != 0) {
	   *len = sz;
	   return 1;
	}
	return 0;
   } else {
	unsigned char	xlen[4];
	int		i;
        if (fread(&xlen, sizeof(unsigned char), 4, f) != 4)
	    return 0;
	/* Convert to number */
	sz = xlen[0];
	sz |= (xlen[1]) << 8;
	sz |= (xlen[2]) << 16;
	sz |= (xlen[3]) << 24;
	sz = (xlen[0] & 0xff) << 24;
	sz |= (xlen[1] & 0xff) << 16;
	sz |= (xlen[2] & 0xff) << 8;
	sz |= (xlen[3] & 0xff);
        sz &= 0xffffffff;

printf("Rec = %d %o\n", sz, sz/3);
        /* Check for EOF */
        if (sz == 0xffffffff)  {
	    *len = -1;
	    return 1;
	}
//	/* Check for EOF */
//        if (sz == 0) {
//	   *len = -1;
//	   return 1;
//        }
        if (sz > TAPE_BUFFER_SIZE) {
	   fprintf(stderr, "Block to big for buffer\n");
	   return 0;
        }
	*len = sz;
//	sz = 0x7fffffff & ((sz + 1) & ~1);
        if (fread(buffer, 1, sz, f) != sz)  {
           fprintf(stderr, "read error\n");
           return 0;
        }
        /* Read backward length */
//        fread(&sz, sizeof(unsigned long int), 1, f);
   }
   return 1;
}

/* Write EOM indicator */
void write_eom(FILE *f) {
   if (p7b) {
      fputc(BCD_TM|TAPE_IRG, f);
   } else {
   	static uint32 eom = 0xffffffff;
        fwrite(&eom, sizeof(uint32), 1, f);
   }
}

/* Put a tape mark on file */
void write_mark(FILE *f) {
   if (p7b) {
      fputc(BCD_TM|TAPE_IRG, f);
   } else {
      static unsigned char xlen[4] = {0, 0, 0, 0};
      fwrite(&xlen, sizeof(unsigned char), 4, f);
   }
}

/* Write out a TAP format block */
void write_block(FILE *f, uint32 len, unsigned char *buffer) {
     if (p7b) {
	/* Put IRG at end of record */
	 buffer[0] |= TAPE_IRG;
	 fwrite(buffer, sizeof(unsigned char), len, f);
     } else {
         uint32 wlen = 0x7fffffff & ((len + 1) & ~1);
	 unsigned char	xlen[4];
	 xlen[0] = len & 0xff;
	 xlen[1] = (len >> 8) & 0xff;
	 xlen[2] = (len >> 16) & 0xff;
	 xlen[3] = (len >> 24) & 0xff;
         fwrite(xlen, sizeof(unsigned char), 4, f);
         fwrite(buffer, sizeof(unsigned char), wlen, f);
         fwrite(xlen, sizeof(unsigned char), 4, f);
     }
}


int main(int argc, char *argv[]) {
   int		sz;
   int		i;
   int		col;
   char		*p;
   FILE		*tape;
   FILE		*otape;

   xlat = &bcd_ascii[0];
   while(--argc && **(++argv) == '-') {
   	switch(tolower((*argv)[1])) {
	case 'b':
		bin = 1;
		break;
	case 'p':
		p7b = 1;
		break;
 	case 'l':
		cc = 1;
		break;
      default:
      	fprintf(stderr,"Unknown option: %s\n",*argv);
      }
   }

   if(argc != 2) {
   	usage();
   }

   /* Open input */
   if((tape = fopen(*argv,"rb")) == NULL) {
	fprintf(stderr,"Can't open tape input file %s: ",*argv);
        perror("");
        exit(1);
   }

   if((otape = fopen(*++argv,"wb")) == NULL) {
	fprintf(stderr,"Can't open tape output file %s: ",*argv);
        perror("");
        exit(1);
   }
   /* Process records of the file */
   while(read_tape(tape, &sz)) {
	if (sz == -2)
	    break;
	if (sz == -1) {
	    puts("*EOF*");
	    write_mark(otape);
	} else {
            for (i = 0; i < sz; i+=3)
               printf("%08o ", (buffer[i] << 16) | (buffer[i+1] << 8) | buffer[i+2]);
            printf("\n");
	    write_block(otape, sz, buffer);
        }
    }
    fclose(tape);
    fclose(otape);
}


