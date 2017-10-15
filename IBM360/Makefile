# CC Command
#
# Note: -O2 is sometimes broken in GCC when setjump/longjump is being
# used. Try -O2 only with released simulators.
#
#ifeq ($(WIN32),)
#Unix Environments
#ifeq ($(OSTYPE),solaris)
#OS_CCDEFS = -lsocket -lnsl -lpthread -D_GNU_SOURCE
#else
#OS_CCDEFS = -D_GNU_SOURCE -I .
#endif
#CC = gcc -std=c99 -O2 -U__STRICT_ANSI__ -g -lm $(OS_CCDEFS) -I .
#ifeq ($(USE_NETWORK),)
#else
#NETWORK_OPT = -DUSE_NETWORK -isystem /usr/local/include /usr/local/lib/libpcap.a
#endif
#else
##Win32 Environments
LDFLAGS = -lm -lwsock32
#CC = gcc -std=c99 -U__STRICT_ANSI__ -O0 -I.
#EXE = .exe
#ifeq ($(USE_NETWORK),)
#else
#NETWORK_OPT = -DUSE_NETWORK -lwpcap -lpacket
#endif
#endif
CC=gcc -g  -std=c99 -U__STRICT_ANSI__  -D_GNU_SOURCE -I. -DUSE_SIM_CARD 
LDFLAGS = -lm -lrt


#
# Common Libraries
#
BIN = ./
SIM = ../scp.c ../sim_console.c ../sim_fio.c ../sim_timer.c ../sim_tape.c \
       ../sim_sock.c ../sim_tmxr.c ../sim_ether.c ../sim_video.c ../sim_serial.c \
	../sim_disk.c ../sim_card.c


#
# Emulator source files and compile time options
#
IBM360D = ./
IBM360 = ${IBM360D}ibm360_cpu.c ${IBM360D}ibm360_sys.c ${IBM360D}ibm360_con.c \
	${IBM360D}ibm360_chan.c ${IBM360D}ibm360_cdr.c ${IBM360D}ibm360_cdp.c \
	${IBM360D}ibm360_mt.c ${IBM360D}ibm360_lpr.c  ${IBM360D}ibm360_dasd.c \
        ${IBM360D}ibm360_com.c
IBM360_OPT = -I.. -DIBM360

#
# Build everything
#
all : ${BIN}ibm360${EXE}

clean : 
ifeq ($(WIN32),)
	${RM} ${ALL}
else
	if exist BIN\*.exe del /q BIN\*.exe
endif
#
# Individual builds
#
${BIN}ibm360${EXE} : ${IBM360} ${SIM} 
	${CC} ${IBM360} ${SIM} ${IBM360_OPT} -o $@ ${LDFLAGS}


