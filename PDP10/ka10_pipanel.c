/* kx10_pipanel.c: PDP-10 PiDP10 front panel.

   Copyright (c) 2022, Richard Cornwell
           Based on code by Oscar Vermeulen

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell

*/

#if PIDP10

/* The following includes are ok since this code can only be run on a
 * Rasberry PI under Linux.
 *
 * To Build this:
 *    make PIDP10=1 pdp10-ka
 */
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h> // extra
#include <time.h>                               //Needed for nanosleep
#include <pthread.h>                    //Needed for pthread
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>                              //Needed for I2C port
#include <sys/ioctl.h>                  //Needed for I2C port
#include <linux/i2c-dev.h>              //Needed for I2C port
#include <string.h>
#include <inttypes.h>
#include <editline/readline.h>
#include "kx10_defs.h"

extern uint64 SW;         /* Switch register */
extern t_addr AS;         /* Address switches */
extern uint64 MB;         /* Memory Bus register */
extern uint64 MI;         /* Memory indicator register */
extern uint8  MI_flag;    /* Memory indicator mode */
extern uint32 FLAGS;      /* Flags register */
extern uint8  RUN;        /* Run flag */
extern uint32 IR;         /* Instruction register */
extern uint32 AC;         /* Accumulator */
extern uint8  IX;         /* Index register */
extern uint8  IND;        /* Indirect flag */
extern t_addr AB;         /* Memory address register */
extern t_addr PC;         /* Program counter register */
#if KA | KI
extern int    nxm_stop;   /* Stop if non-existent memory access */
extern int    adr_cond;   /* Address stop condition */
#endif
extern uint8  IOB_PI;     /* Pending Interrupt requests */
extern uint8  PIR;        /* Current Interrupt requests */
extern uint8  PIH;        /* Currently held interrupts */
extern uint8  PIE;        /* Currently enabled interrupts */
extern int    pi_enable;  /* Interrupt system enabled */
extern uint8  prog_stop;  /* Program stop */
extern uint8  examine_sw; /* Examine memory */
extern uint8  deposit_sw; /* Deposit memory */
extern uint8  sing_inst_sw;/* Execute single instruction */
extern uint8  xct_sw;     /* Execute an instruction */
extern uint8  stop_sw;    /* Stop the simulator */
extern uint8  MI_disable; /* Disable MI display */
extern int    watch_stop; /* Memory Stop */
extern uint32 rdrin_dev;  /* Read in device. */
extern uint8  MI_disable; /* Disable MI */
int           repeat_sw;  /* Repeat switch state */
int           par_stop;   /* Parity stop */
int           pwr_off;    /* Power off system */
int           rep_rate;   /* Rate of repeat function */
int           rep_count;  /* Count down to repeat trigger */


/* PION_LAMP = pi_enable */
/* RUN_LAMP = RUN */
/* PWR_LAMP = 1 */

/* PI_LAMP = MI_flag, MI_LAMP = !MI_flag */
/* PI_ACT =  PIE */
/* PI_REQ = PIR */
/* PI_PRO = PIH */
/* USER_LAMP = FLAGS & USER */
/* PROG_STOP_LAMP = prog_stop */
/* MEM_STOP_LAMP = watch_stop */
/* MA = AB */
/* MB = MI_flag ? MI : MB */
/* IX_MASK = IX */
/* AC_MASK = AC */
/* IND_LAMP = IND */

/* led row 0 */
#define MB_MASK0             0xffff00000  /* 0-15 */
#define MB_V_0               20           /* Right */

/* led row 1 */
#define MB_MASK1             0x0000ffff0  /* 16-31 */
#define MB_V_1               4           /* Right */

/* led row 2 */
#define RUN_LAMP             0x0080
#define PION_LAMP            0x0100
#define PWR_LAMP             0x0200
#define PI_ENB_MASK          0x007f
#define MB_MASK2             0x0000000f  /* 32-35 */
#define MB_V_2               12          /* Left */
#define PI_LAMP              0x0400
#define MI_LAMP              0x0800

/* led row 3 */
#define IR_MASK3             0x1ff        /* 0-9 */
#define IR_V_3               7            /* Left */
#define AC_MASK3             0xf
#define AC_V_3               3            /* Left */
#define IND_LAMP             0x4
#define IX_MASK3             0xc
#define IX_V_3               2           /* Right */

/* led row 4 */
#define IX_MASK4             3
#define IX_V_4               14           /* left */
#define MA_MASK4             0x3fff0
#define MA_V_4               4            /* Right */

/* led row 5 */
#define PROG_STOP_LAMP       0x0080
#define USER_LAMP            0x0100
#define MEM_STOP_LAMP        0x0200
#define PI_REQ_MASK          0x007f
#define MA_MASK5             0xf
#define MA_V_5               12           /* Left */

/* led row 6 */
#define PC_MASK6             0x3fffc
#define PC_V_6               2            /* right */

/* led row 7 */
#define PI_PRO_MASK7         0x007f
#define PI_IOB_MASK7         0x0f80
#define PI_IOB_V_7           7            /* left */
#define PC_MASK7             0x0003
#define PC_V_7               14           /* Left */

/* switch row 0 */
#define SR_MASK_0            0xffff00000
#define SR_V0                20           /* Left */

/* switch row 1 */
#define SR_MASK_1            0x0000ffff0
#define SR_V1                4            /* Left */

/* Switch row 2 */
#define SR_MASK_2            0x00000000f
#define SR_V2                12           /* Right */
#define DEP_THIS             0x0800       /* SW = 11 */
#define DEP_NEXT             0x0400       /* SW = 10 */
#define SING_INST            0x0200       /* set sing_inst */
#define SING_CYCL            0x0100       /* Nop */
#define PAR_STOP             0x0080       /* Nop */
#define NXM_STOP             0x0040       /* set nxm_stop */
#define REP_SW               0x0020
#define INST_FETCH           0x0010       /* adr_cond */
#define DATA_FETCH           0x0008       /* adr_cond */
#define WRITE_SW             0x0004       /* adr_cond */
#define ADR_STOP_SW          0x0002       /* adr_cond */
#define ADR_BRK_SW           0x0001       /* adr_cond */

/* Switch row 3 */
#define MA_SW_MASK_3         0x3fffc
#define MA_SW_V3             2             /* Left */

/* Switch row 4 */
#define MA_SW_MASK_4         0x00003
#define MA_SW_V4             14            /* Right */
#define EXAM_NEXT            0x0001        /* SW=0 */
#define EXAM_THIS            0x0002        /* SW=1 */
#define XCT_SW               0x0004        /* SW=2 Set xct_inst */
#define RESET_SW             0x0008        /* SW=3 Call reset */
#define STOP_SW              0x0010        /* SW=4 Set RUN = 0 */
#define CONT_SW              0x0020        /* SW=5 call sim_instr */
#define START_SW             0x0040        /* SW=6 Call reset then sim_instr */
#define READ_SW              0x0080        /* SW=7 Boot function */

struct {
      int   last_state;      /* last state */
      int   state;           /* Stable state */
      int   debounce;        /* Debounce timer */
      int   changed;         /* Switch changed state */
} switch_state[16];


int file_i2c;                             /* i2c channel */
unsigned char buffer[60] = {0};           /* data exchange buffer for i2c use */


#define BLOCK_SIZE              (4*1024)

struct bcm2835_peripheral {
    unsigned long addr_p;
    int mem_fd;
    void *map;
    volatile unsigned int *addr;
} gpio;

static unsigned get_dt_ranges(const char *filename, unsigned offset)
{
    unsigned address = ~0;
    FILE *fp = fopen(filename, "rb");
    if (fp) {
        unsigned char buf[4];
        fseek(fp, offset, SEEK_SET);
        if (fread(buf, 1, sizeof buf, fp) == sizeof buf)
            address = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3] << 0;
        fclose(fp);
    }
    return address;
}


static unsigned bcm_host_get_peripheral_address(void) // find Pi's gpio base address
{
// Pi 4 fix: https://github.com/raspberrypi/userland/blob/master/host_applications/linux/libs/bcm_host/bcm_host.c
    unsigned address = get_dt_ranges("/proc/device-tree/soc/ranges", 4);
    if (address == 0)
        address = get_dt_ranges("/proc/device-tree/soc/ranges", 8);
    return address == ~0 ? 0x20000000 : address;
}

void short_wait(void) // creates pause required in between clocked GPIO settings changes
{
    fflush(stdout); //
    usleep(1); // suggested as alternative for asm which c99 does not accept
}

// GPIO setup macros.
// In early versions INP_GPIO(x) was used always before OUT_GPIO(x),
// this is disabled now by INO_GPIO(g)
#define INO_GPIO(g) //INP_GPIO(g) // Use this before OUT_GPIO
#define INP_GPIO(g)   *(gpio.addr + ((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g)   *(gpio.addr + ((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio.addr + (((g)/10))) |= (((a)<=3?(a) + 4:(a)==4?3:2)<<(((g)%10)*3))
#define GPIO_SET  *(gpio.addr + 7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR  *(gpio.addr + 10) // clears bits which are 1 ignores bits which are 0
#define GPIO_READ(g)  *(gpio.addr + 13) &= (1<<(g))
#define GPIO_PULL *(gpio.addr + 37) // pull up/pull down
#define GPIO_PULLCLK0 *(gpio.addr + 38) // pull up/pull down clock
// Pi 4 update,
/* https://github.com/RPi-Distro/raspi-gpio/blob/master/raspi-gpio.c */
/* 2711 has a different mechanism for pin pull-up/down/enable  */
#define GPPUPPDN0                57        /* Pin pull-up/down for pins 15:0  */
#define GPPUPPDN1                58        /* Pin pull-up/down for pins 31:16 */
#define GPPUPPDN2                59        /* Pin pull-up/down for pins 47:32 */
#define GPPUPPDN3                60        /* Pin pull-up/down for pins 57:48 */


// GPIO pin definitions
//      8 ledrows each provide power to a block of 16 LEDs (when set to output high)
u_int8_t ledrows[8] = { 16,17,18,19, 20,21,22,23};
//      5 rows each provide a current sink to a block of 16 switches (when set to output low)
u_int8_t rows[5] = { 4,5,6,7,8 };
//      16 column pins in the MCP23017 provide a current sink to light up one of 16 LEDs (when set to output low)


// helper functions: 1 -- gpio
//
// map GPIO into virtual memory space ------------------------
int map_peripheral(struct bcm2835_peripheral *p)
{
    if ((p->mem_fd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
        printf("Failed to open /dev/mem, try checking permissions.\n");
        return -1;
    }
    p->map = mmap(NULL, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
        p->mem_fd, // File descriptor to physical memory virtual file '/dev/mem'
        p->addr_p); // Address in physical map that we want this memory block to expose
    if (p->map == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    p->addr = (volatile unsigned int *) p->map;
    return 0;
}

void unmap_peripheral(struct bcm2835_peripheral *p)
{
    munmap(p->map, BLOCK_SIZE);
    close(p->mem_fd);
}

// helper functions: 2 -- parallel threads for GPIO mux and LED brightness averaging
//
void *blink(void *ptr); // the real-time GPIO multiplexing process to start up
void *gpiopattern_update_leds(void *ptr); // the averaging thread
void gpio_mux_thread_start(void);

pthread_t blink_thread;
int blink_thread_terminate = 0;

void gpio_mux_thread_start()
{
    int res;
    res = pthread_create(&blink_thread, NULL, blink, &blink_thread_terminate);
    if (res) {
        fprintf(stderr, "Error creating gpio_mux thread, return code %d\n", res);
        exit(EXIT_FAILURE);
    }
    printf("Created blink_thread\n");
    sleep(2); // allow 2 sec for multiplex to start
}


// 3 - set MCP23017 'column' pins to input with pullup
static void mcp23017_to_input(void)
{
    // ----- set to 16 bits of input -----
    buffer[0] = 0x00;
    buffer[1] = 0xff;
    buffer[2] = 0xff;
    if (write(file_i2c, buffer, 3) != 3)
        printf("Failed to write to the i2c bus.\n");

    // ----- enable pullups -----
    buffer[0] = 0x0c;
    buffer[1] = 0xff;
    buffer[2] = 0xff;
    if (write(file_i2c, buffer, 3) != 3)
        printf("Failed to write to the i2c bus.\n");
}

static void mcp23017_to_output(void)
{
    // ---- set to 16 bits of output -----
    buffer[0] = 0x00;
    buffer[1] = 0x00;
    buffer[2] = 0x00;
    if (write(file_i2c, buffer, 3) != 3)
        printf("Failed to write to the i2c bus.\n");
}

/*
 * Debounce a momentary switch.
 */
static void debounce_sw(int state, int sw)
{
    if (switch_state[sw].state == state) {
        if (switch_state[sw].debounce != 0) {
            switch_state[sw].debounce--;
        } else {
            if (switch_state[sw].last_state != switch_state[sw].state) {
                 switch_state[sw].changed = 1;
            }
            switch_state[sw].last_state = switch_state[sw].state;
        }
    } else {
        switch_state[sw].debounce = 8;
        switch_state[sw].changed = 0;
        switch_state[sw].state = state;
    }
}

void *blink(void *ptr)
{
    int     *terminate = (int *)ptr;
    int     ledrow, row, i, col;
    uint16_t leds;
    uint16_t sw;
    uint64   new_sw;
    t_addr   new_as;

    // set thread to real time priority -----------------
    struct sched_param sp;
    sp.sched_priority = 98; // maybe 99, 32, 31?
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp))
        fprintf(stderr, "warning: failed to set RT priority\n");

    //----- OPEN THE I2C BUS -----
    char *filename = (char*)"/dev/i2c-1";
    if ((file_i2c = open(filename, O_RDWR)) < 0) {
        printf("Failed to open the i2c bus");
        exit(1);
    }
    if (ioctl(file_i2c, I2C_SLAVE, 0x20) < 0) {
        printf("Failed bus access and/or talk to slave.\n");
        exit(1);
    }
    printf("OK: access to MCP\r\n");


    // Find gpio address (different for Pi 1,2 and 4) ----------
    gpio.addr_p = bcm_host_get_peripheral_address() + +0x200000;
    if (gpio.addr_p == 0x20200000)          printf("*** RPi Plus detected\n");
    else if (gpio.addr_p == 0x3f000000) printf("*** RPi 2/3/Z detected\n");
    else if (gpio.addr_p == 0xfe200000) printf("*** RPi 4 detected\n");

    // map GPIO into virtual memory space ------------------------
    if (map_peripheral(&gpio) == -1) {
        printf("Failed to map the physical GPIO registers into the virtual memory space.\n");
        return (void *) -1;
    }

    // initialise GPIO (all pins used as inputs, with pull-ups enabled on cols)
    for (ledrow = 0; ledrow < 8; ledrow++) { // Define ledrows as input
        INP_GPIO(ledrows[ledrow]);
        GPIO_CLR = 1 << ledrows[ledrow]; // so go to Low when switched to output
    }
    mcp23017_to_input(); // Define cols as input with pullups

    // initialise GPIO pullups. Different for Pi 4 and older Pi's
    if (gpio.addr_p==0xfe200000)
    {
        //printf("Configuring pullups for Pi 4\r\n");
        /* https://github.com/RPi-Distro/raspi-gpio/blob/master/raspi-gpio.c */
        /* 2711 has a different mechanism for pin pull-up/down/enable  */
        int gpiox;
        int pullreg;
        int pullshift;
        unsigned int pullbits;
        unsigned int pull;

        // GPIO column pins

        // GPIO column pins
        // -- already done during setup

        // GPIO row pins
        for (i=0;i<5;i++) {
            gpiox = rows[i];
            pullreg = GPPUPPDN0 + (gpiox>>4);
            pullshift = (gpiox & 0xf) << 1;
            pull = 0;       // pullup

            pullbits = *(gpio.addr + pullreg);
            pullbits &= ~(3 << pullshift);
            pullbits |= (pull << pullshift);
            *(gpio.addr + pullreg) = pullbits;
        }
        // GPIO ledrow pins
        for (i=0;i<8;i++) {
            gpiox = ledrows[i];
            pullreg = GPPUPPDN0 + (gpiox>>4);
            pullshift = (gpiox & 0xf) << 1;
            pull = 0;       // pullup

            pullbits = *(gpio.addr + pullreg);
            pullbits &= ~(3 << pullshift);
            pullbits |= (pull << pullshift);
            *(gpio.addr + pullreg) = pullbits;
        }
    }
    else    // configure pullups for older Pis
    {
/*      // BCM2835 ARM Peripherals PDF p 101 & elinux.org/RPi_Low-level_peripherals#Internal_Pull-Ups_.26_Pull-Downs
        GPIO_PULL = 2; // pull-up
        short_wait(); // must wait 150 cycles
        GPIO_PULLCLK0 = 0x0c003ff0; // selects GPIO pins 4..13 and 26,27

        short_wait();
        GPIO_PULL = 0; // reset GPPUD register
        short_wait();
        GPIO_PULLCLK0 = 0; // remove clock
        short_wait(); // probably unnecessary
*/
        // BCM2835 ARM Peripherals PDF p 101 & elinux.org/RPi_Low-level_peripherals#Internal_Pull-Ups_.26_Pull-Downs
        GPIO_PULL = 0; // no pull-up no pull-down just float
        short_wait(); // must wait 150 cycles
        //GPIO_PULLCLK0 = 0x03f00000; // selects GPIO pins 20..25
        GPIO_PULLCLK0 = 0x0ff0000; // selects GPIO pins 16..23 (ledrow)
        short_wait();
        GPIO_PULL = 0; // reset GPPUD register
        short_wait();
        GPIO_PULLCLK0 = 0; // remove clock
        short_wait(); // probably unnecessary

        // BCM2835 ARM Peripherals PDF p 101 & elinux.org/RPi_Low-level_peripherals#Internal_Pull-Ups_.26_Pull-Downs
        GPIO_PULL = 0; // no pull-up no pull down just float
        short_wait(); // must wait 150 cycles
        //GPIO_PULLCLK0 = 0x070000; // selects GPIO pins 16..18
        GPIO_PULLCLK0 = 0x01f0; // selects GPIO pins 4..8 (row)
        short_wait();
        GPIO_PULL = 0; // reset GPPUD register
        short_wait();
        GPIO_PULLCLK0 = 0; // remove clock
        short_wait(); // probably unnecessary
    }
    /* -------------------------------------------------- */

    /* Read in initial switch status */
    mcp23017_to_input();

    new_sw = 0;
    new_as = 0;
    for (row=0; row<5; row++) {    /* there are 5 rows of 16 switches each */
         /* ----- for this row, output 0V to overrule built-in pull-up */
         /*       from column input pin ----- */
         INO_GPIO(rows[row]);
         OUT_GPIO(rows[row]);
         GPIO_CLR = 1 << rows[row];

         /* ----- read switches (request MCP23017 gpio values) ----- */
         buffer[0] = 0x12;
         if (write(file_i2c, buffer, 1) != 1)
             printf("Failed i2c write (3)\n");
         if (read(file_i2c, buffer, 2) != 2)
             printf("Failed i2c read (3)\n");
         else {
             sw = (buffer[1] << 8) + buffer[0];
         switch (row) {
         default:
         case 0:
                new_sw |= (((uint64)sw) << SR_V0) | SR_MASK_0;
                break;
         case 1:
                new_sw |= (((uint64)sw) << SR_V1) | SR_MASK_1;
                break;
         case 2:
                new_sw |= (((uint64)sw) << SR_V2) | SR_MASK_2;
#if KA | KI
                adr_cond = sw & (INST_FETCH|DATA_FETCH|WRITE_SW|ADR_STOP_SW|ADR_BRK_SW);
                nxm_stop = (sw & NXM_STOP) != 0;
#endif
                sing_inst_sw = (sw & SING_INST) != 0;
                /* PAR_STOP handle special features */
                /* SING_CYCL no function yet */
                for (col = 10; col < 12; col++) {
                    int state = (sw & (1 << col)) != 0;
                     switch_state[col].last_state = state;
                     switch_state[col].state = state;
                     switch_state[col].debounce = switch_state[col].changed = 0;
                }
                repeat_sw = (sw & REP_SW) != 0;
                break;
         case 3:
                new_as |= (((t_addr)sw) << MA_SW_V3) | MA_SW_MASK_3;
                break;
         case 4:
                new_as |= (((t_addr)sw) << MA_SW_V4) | MA_SW_MASK_4;
                for (col = 0; col < 8; col++) {
                    int state = (sw & (1 << col)) != 0;
                     switch_state[col].last_state = state;
                     switch_state[col].state = state;
                     switch_state[col].debounce = switch_state[col].changed = 0;
                }
                break;
          }
          SW = new_sw;
         AS = new_as;
          // stop sinking current from this row of switches
          INP_GPIO(rows[row]);
         }
         // stop sinking current from this row of switches
         INP_GPIO(rows[row]);
    }
    SW = new_sw;
    AS = new_as;

    printf("\nPiDP-10 FP on\n");


    // start the actual multiplexing

    while (*terminate == 0)
    {
        // ---- set to 16 bits of output -----
        buffer[0] = 0x00;
        buffer[1] = 0x00;
        buffer[2] = 0x00;
        if (write(file_i2c, buffer, 3) != 3)
            printf("Failed i2c write (1).\n");

        for (ledrow=0; ledrow<8; ledrow++) { /* 8 rows of LEDS get lit */
            switch (ledrow) {
            default:
            case 0:
                    leds = (((MI_flag)? MI : MB) & MB_MASK0) >> MB_V_0;
                    break;
            case 1:
                    leds = (((MI_flag)? MI : MB) & MB_MASK1) >> MB_V_1;
                    break;
            case 2:
                    leds = PWR_LAMP;
                    leds |= (RUN) ? RUN_LAMP : 0;
                    leds |= (pi_enable) ? PION_LAMP : 0;
                    leds |= (PIE & PI_ENB_MASK);
                    leds |= (((MI_flag)? MI : MB) & MB_MASK2) << MB_V_2;
                    leds |= (MI_flag) ? PI_LAMP : MI_LAMP;
                    break;

            case 3:
                    leds = (IR & IR_MASK3) << IR_V_3;
                    leds |= (AC & AC_MASK3) << AC_V_3;
                    leds |= (IND) ? IND_LAMP : 0;
                    leds |= (IX & IX_MASK3) >> IX_V_3;
                    break;

            case 4:
                    leds = (IX & IX_MASK4) << IX_V_4;
                    if (par_stop) {
                        leds |= (rdrin_dev & MA_MASK4) >> MA_V_4;
                        leds |= rep_rate << 12;
                        leds |= MI_disable << 10;
                    } else {
                        leds |= (AB & MA_MASK4) >> MA_V_4;
                    }
                    break;

            case 5:
                    if (par_stop) {
                        leds = (rdrin_dev & MA_MASK5) << MA_V_5;
                    } else {
                        leds = (AB & MA_MASK5) << MA_V_5;
                    }
                    leds |= (FLAGS & USER) ? USER_LAMP : 0;
                    leds |= (PIR & PI_REQ_MASK);
                    leds |= (prog_stop) ? PROG_STOP_LAMP: 0;
                    leds |= (watch_stop) ? MEM_STOP_LAMP: 0;
                    break;

            case 6:
                    leds = (PC & PC_MASK6) >> PC_V_6;
                    break;

            case 7:
                    leds = (PC & PC_MASK7) << PC_V_7;
                    leds |= (PIH & PI_PRO_MASK7);
                    leds |= (IOB_PI & PI_IOB_MASK7) << PI_IOB_V_7;
                    break;
            }

            leds = ~leds;

            /* ----- set MCP23017 IO pin values */
            /*       (determines which of the 16 LEDs will light up) ----- */
            buffer[0] = 0x14;
            buffer[2] = leds >> 8;
            buffer[1] = leds & 0x00ff;
            if (write(file_i2c, buffer, 3) != 3)
                printf("Failed i2c write (2)\n");

            /* ----- set ledrow pin to high (switch on the power) ----- */
            INO_GPIO(ledrows[ledrow]);
            GPIO_SET = 1 << ledrows[ledrow]; /* could be done once... TODO */
            OUT_GPIO(ledrows[ledrow]);

            /* ----- now wait a while with LEDs burning ----- */
            usleep(250);

            /* ----- Toggle ledrow off (cut the power) ----- */
            GPIO_CLR = 1 << ledrows[ledrow];  /* superfluous given next line */
            INP_GPIO(ledrows[ledrow]);
        }

        /* ----- prepare for reading the switches */
        /*       note: INP_GPIO has been done already for the ledrows, */
        /*       so they are inputs already, nothing to be done */

        /* ----- set MCP23017 IO pins to input with pullups enabled ----- */
        mcp23017_to_input();

        new_sw = 0;
        new_as = 0;
        for (row=0; row<5; row++) {   /* there are 5 rows of 16 switches each */
            /* ----- for this row, output 0V to overrule built-in pull-up */
            /*       from column input pin ----- */
            INO_GPIO(rows[row]);
            OUT_GPIO(rows[row]);
            GPIO_CLR = 1 << rows[row];

            /* ----- read switches (request MCP23017 gpio values) ----- */
            buffer[0] = 0x12;
            if (write(file_i2c, buffer, 1) != 1)
                printf("Failed i2c write (3)\n");
            if (read(file_i2c, buffer, 2) != 2)
                printf("Failed i2c read (3)\n");
            else {
                sw = (buffer[1] << 8) + buffer[0];
                switch (row) {
                default:
                case 0:
                        new_sw |= (((uint64)sw) << SR_V0) & SR_MASK_0;
                        break;
                case 1:
                        new_sw |= (((uint64)sw) << SR_V1) & SR_MASK_1;
                        break;
                case 2:
                        new_sw |= (((uint64)sw) << SR_V2) & SR_MASK_2;
#if KA | KI
                        adr_cond = sw & (INST_FETCH|DATA_FETCH|WRITE_SW|
                                                ADR_STOP_SW|ADR_BRK_SW);
                        nxm_stop = (sw & NXM_STOP) != 0;
#endif
                        sing_inst_sw = (sw & SING_INST) != 0;
                        /* PAR_STOP handle special features */
                        par_stop = (sw & PAR_STOP) != 0;
                        /* SING_CYCL no function yet */
                        for (col = 10; col < 12; col++) {
                            int state = (sw & (1 << col)) != 0;
                            debounce_sw(state, col);
                        }
                        repeat_sw = (sw & REP_SW) != 0;
                        break;
                case 3:
                        new_as |= (((t_addr)sw) << MA_SW_V3) & MA_SW_MASK_3;
                        break;
                case 4:
                        new_as |= (((t_addr)sw) << MA_SW_V4) & MA_SW_MASK_4;
                        for (col = 0; col < 8; col++) {
                            int state = (sw & (1 << col)) != 0;
                            debounce_sw(state, col);
                        }
                        break;
                }
            }

            /* stop sinking current from this row of switches */
            INP_GPIO(rows[row]);
        }
        SW = new_sw;

        /* If running, check for switch changes */
        if (par_stop) {
            for (col = 0; col < 12; col++) {
                if (switch_state[col].changed && switch_state[col].state) {
                   switch_state[col].changed = 0;
                   switch (col) {
#if KA | KI
                   case 7:      /* ReadIN */
                           rdrin_dev = 0774 & new_as;
                           break;
#endif

                   case 5:      /* Continue */
                           MI_disable = !MI_disable;
                           if (MI_disable)
                               MI_flag = 0;
                           break;

                   case 4:      /* Stop function */
                           stop_sw = 1;
                           pwr_off = 1;
                           break;

                   case 1:      /* Examine this */
                           rep_rate = (new_as >> 14) & 0xf;
                           break;

                   case 0:      /* Examine next */
                   case 2:      /* Execute function */
                   case 3:      /* Reset function */
                   case 6:      /* Start */
                   case 10:     /* Deposit next */
                   case 11:     /* Deposit this */
                   default:
                           break;
                   }
                }
            }
       } else {
          AS = new_as;
       }
       /* Check repeat count */
       if (rep_count > 0 && --rep_count == 0) {
           for (col = 0; col < 12; col++) {
               switch_state[col].changed = switch_state[col].state;
           }
       }
       if (RUN) {
            for (col = 0; col < 12; col++) {
                if (switch_state[col].changed && switch_state[col].state) {
                   /* If repeat switch set, trigger timer */
                   if (repeat_sw) {
                       rep_count = (rep_rate + 1) * 16;
                   }
                   switch (col) {
                   case 1:      /* Examine this */
                              examine_sw = 1;
                              MI_flag = 0;
                              switch_state[col].changed = 0;
                              break;
                   case 0:      /* Examine next */
                   case 5:      /* Continue */
                   case 6:      /* Start */
                   case 7:      /* ReadIN */
                   case 10:     /* Deposit next */
                   default:
                              switch_state[col].changed = 0;
                              break;
                   case 2:      /* Execute function */
                              xct_sw = 1;
                              switch_state[col].changed = 0;
                              break;
                   case 3:      /* Reset function */
                              stop_sw = 1;
                              break;
                   case 4:      /* Stop function */
                              stop_sw = 1;
                              switch_state[col].changed = 0;
                              break;
                   case 11:     /* Deposit this */
                              deposit_sw = 1;
                              MI_flag = 0;
                              switch_state[col].changed = 0;
                              break;
                   }
                }
            }
       }
        // done with reading the switches, so start the next cycle of lighting up LEDs
    }

    /* received terminate signal, close down */
}

volatile int    input_wait;
static char  *input_buffer;

/*
 * Handler for EditLine package when line is complete.
 */
static void read_line_handler(char *line)
{
    if (line != NULL) {
       input_buffer = line;
       printf("Got: %s\n", line);
       input_wait = 0;
    }
}

/*
 * Process input from stdin or switches.
 */
static char *
vm_read(char *cptr, int32 sz, FILE *file)
{
    struct timeval tv = {0,10000};  /* Wait for 10ms */
    fd_set         read_set;
    int            fd = fileno(file);  /* What to wait on */
    int            col;

    input_wait = 1;
    input_buffer = NULL;
    while (input_wait) {
       FD_ZERO(&read_set);
       FD_SET(fd, &read_set);
       tv.tv_sec = 0;
       tv.tv_usec = 10000;
       (void)select(fd+1, &read_set, NULL, NULL, &tv);
       if (FD_ISSET(fd, &read_set)) {
           rl_callback_read_char();
       } else {
           if (pwr_off) {
               if ((input_buffer = (char *)malloc(20)) != 0) {
                   strcpy(input_buffer, "quit");
                   stop_sw = 1;
                   pwr_off = 0;
                   input_wait = 0;
               }
               break;
           }

           /* Process switches */
           for (col = 0; col < 12; col++) {
                if (switch_state[col].changed && switch_state[col].state) {
                    /* If repeat switch set, trigger timer */
                    if (repeat_sw) {
                        rep_count = (rep_rate + 1) * 16;
                    }
                    switch (col) {
                    case 0:      /* Examine next */
                            AB++;
                            MB = (AB < 020) ? FM[AB] : M[AB];
                            MI_flag = 0;
                            break;

                    case 1:      /* Examine this */
                            AB = AS;
                            MB = (AB < 020) ? FM[AB] : M[AB];
                            MB = M[AB];
                            MI_flag = 0;
                            break;

                    case 2:      /* Execute function */
                            if ((input_buffer = (char *)malloc(20)) != 0) {
                                strcpy(input_buffer, "step");
                                xct_sw = 1;
                                input_wait = 0;
                            }
                            break;

                    case 3:      /* Reset function */
                            if ((input_buffer = (char *)malloc(10)) != 0) {
                                strcpy(input_buffer, "reset all");
                                input_wait = 0;
                            }
                            break;

                    case 4:      /* Stop function */
                            break;

                    case 5:      /* Continue */
                            if ((input_buffer = (char *)malloc(10)) != 0) {
                               strcpy(input_buffer,
                                        (sing_inst_sw) ? "step" : "cont");
                               input_wait = 0;
                            }
                            break;

                    case 6:      /* Start */
                            if ((input_buffer = (char *)malloc(20)) != 0) {
                                sprintf(input_buffer, "run %06o", AS);
                                input_wait = 0;
                            }
                            break;

#if KA | KI
                    case 7:      /* ReadIN */
                            if ((input_buffer = (char *)malloc(20)) != 0) {
                                DEVICE         *dptr;
                                int            i;

                                /* Scan all devices to find match */
                                for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
                                    DIB *dibp = (DIB *) dptr->ctxt;
                                    if (dibp && !(dptr->flags & DEV_DIS) &&
                                        (dibp->dev_num == (rdrin_dev & 0774))) {
                                        if (dptr->numunits > 1)
                                            sprintf(input_buffer, "boot %s0",
                                                      dptr->name);
                                        else
                                            sprintf(input_buffer, "boot %s",
                                                      dptr->name);
                                        input_wait = 0;
                                        break;
                                    }
                                }
                            }
                            /* If we did not find a boot device, free command */
                            if (input_wait) {
                                free(input_buffer);
                                input_buffer = NULL;
                                sim_messagef(SCPE_OK, "Device %03o not found\n",
                                                rdrin_dev);
                            }
                            break;
#endif

                    case 10:     /* Deposit next */
                           AB++;
                           if (AB < 020) {
                               FM[AB] = SW;
                               MB = FM[AB];
                           } else {
                               M[AB] = SW;
                               MB = M[AB];
                           }
                           MI_flag = 0;
                           break;

                    case 11:     /* Deposit this */
                           AB = AS;
                           if (AB < 020) {
                               FM[AB] = SW;
                               MB = FM[AB];
                           } else {
                               M[AB] = SW;
                               MB = M[AB];
                           }
                           MI_flag = 0;
                           break;
                    }
                    switch_state[col].changed = 0;
                }
            }
       }
    }
    return input_buffer;
}

static void
vm_post(t_bool from_scp)
{
}

/*
 * Start panel thread, and install console read functions.
 */
void pi_panel_start(void)
{
    int terminate = 1;
    int i,j;

    /* start up multiplexing thread */
    gpio_mux_thread_start();
    sim_vm_read = &vm_read;
    sim_vm_post = &vm_post;
    rl_callback_handler_install("", (rl_vcpfunc_t*) &read_line_handler);
}

/*
 * Stop display thread.
 */
void pi_panel_stop(void)
{
    if (blink_thread_terminate == 0) {
        blink_thread_terminate=1;
        rl_callback_handler_remove();
        sim_vm_read = NULL;

        sleep (2);      /* allow threads to close down */
    }
}

#endif
