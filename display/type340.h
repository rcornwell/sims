/*
 * external interface for type340.c
 */

typedef unsigned int ty340word;

/*
 * Type340 status bits
 * MUST BE EXACT SAME VALUES AS USED IN PDP-10 CONI!!!
 */
#define ST340_VEDGE	04000
#define ST340_LPHIT	02000
#define ST340_HEDGE	01000
#define ST340_STOP_INT	00400

/* NOT same as PDP-10 CONI */
#define ST340_STOPPED	0400000

/*
 * calls from host into type340.c
 */
ty340word ty340_reset(void);
ty340word ty340_status(void);
ty340word ty340_instruction(ty340word inst);
void ty340_set_dac(ty340word addr);

/*
 * calls from type340.c into host simulator
 */
extern ty340word ty340_fetch(ty340word);
extern void ty340_store(ty340word, ty340word);
extern void ty340_lp_int(ty340word x, ty340word y);
extern void ty340_rfd(void);
