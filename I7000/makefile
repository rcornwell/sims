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
CC=gcc -g -D_GNU_SOURCE -I. -DUSE_SIM_CARD
LDFLAGS = -lm -lrt


#
# Common Libraries
#
BIN = ./
SIM = ../scp.c ../sim_console.c ../sim_fio.c ../sim_timer.c ../sim_tape.c \
      ../sim_sock.c ../sim_tmxr.c ../sim_ether.c ../sim_card.c \
      ../sim_video.c ../sim_serial.c ../sim_disk.c


#
# Emulator source files and compile time options
#
I7000D = ./
I7090 = ${I7000D}i7090_cpu.c ${I7000D}i7090_sys.c ${I7000D}i7090_chan.c \
        ${I7000D}i7090_cdr.c ${I7000D}i7090_cdp.c ${I7000D}i7090_lpr.c \
        ${I7000D}i7000_chan.c ${I7000D}i7000_mt.c ${I7000D}i7090_drum.c \
        ${I7000D}i7090_hdrum.c ${I7000D}i7000_chron.c ${I7000D}i7000_dsk.c \
        ${I7000D}i7000_com.c ${I7000D}i7000_ht.c 
I7090_OPT = -I.. -DUSE_INT64 -DI7090 

I7080D = ./
I7080 = ${I7000D}i7080_cpu.c ${I7000D}i7080_sys.c ${I7000D}i7080_chan.c \
        ${I7000D}i7080_drum.c ${I7000D}i7000_cdp.c ${I7000D}i7000_cdr.c \
        ${I7000D}i7000_con.c ${I7000D}i7000_chan.c ${I7000D}i7000_lpr.c \
        ${I7000D}i7000_mt.c ${I7000D}i7000_chron.c ${I7000D}i7000_dsk.c \
        ${I7000D}i7000_com.c ${I7000D}i7000_ht.c 
I7080_OPT = -I.. -DI7080 

I7070D = ./
I7070 = ${I7000D}i7070_cpu.c ${I7000D}i7070_sys.c ${I7000D}i7070_chan.c \
        ${I7000D}i7000_cdp.c ${I7000D}i7000_cdr.c ${I7000D}i7000_con.c \
        ${I7000D}i7000_chan.c ${I7000D}i7000_lpr.c ${I7000D}i7000_mt.c \
        ${I7000D}i7000_chron.c ${I7000D}i7000_dsk.c ${I7000D}i7000_com.c \
        ${I7000D}i7000_ht.c 
I7070_OPT = -I.. -DUSE_INT64 -DI7070 

I7010D = ./
I7010 = ${I7000D}i7010_cpu.c ${I7000D}i7010_sys.c ${I7000D}i7010_chan.c \
        ${I7000D}i7000_cdp.c ${I7000D}i7000_cdr.c ${I7000D}i7000_con.c \
        ${I7000D}i7000_chan.c ${I7000D}i7000_lpr.c ${I7000D}i7000_mt.c \
        ${I7000D}i7000_chron.c ${I7000D}i7000_dsk.c ${I7000D}i7000_com.c \
        ${I7000D}i7000_ht.c 
I7010_OPT = -I.. -DI7010 

I704D  = ./
I704   = ${I7000D}i7090_cpu.c ${I7000D}i7090_sys.c ${I7000D}i7090_chan.c \
         ${I7000D}i7090_cdr.c ${I7000D}i7090_cdp.c ${I7000D}i7090_lpr.c \
         ${I7000D}i7000_mt.c ${I7000D}i7090_drum.c ${I7000D}i7000_chan.c 
I704_OPT = -I.. -DUSE_INT64 -DI704


I701D  = ./
I701   = ${I7000D}i701_cpu.c ${I7000D}i701_sys.c ${I7000D}i701_chan.c \
         ${I7000D}i7090_cdr.c ${I7000D}i7090_cdp.c ${I7000D}i7090_lpr.c \
         ${I7000D}i7000_mt.c ${I7000D}i7090_drum.c ${I7000D}i7000_chan.c 
I701_OPT = -I.. -DUSE_INT64 -DI701

#
# Build everything
#
all : ${BIN}i7090${EXE} ${BIN}i7080${EXE} ${BIN}i7070${EXE} ${BIN}i7010${EXE} \
        ${BIN}i704${EXE} ${BIN}i701${EXE} 

clean : 
ifeq ($(WIN32),)
	${RM} ${ALL}
else
        if exist BIN\*.exe del /q BIN\*.exe
endif
#
# Individual builds
#
${BIN}i7090${EXE} : ${I7090} ${SIM} 
	${CC} ${I7090} ${SIM} ${I7090_OPT} -o $@ ${LDFLAGS}

${BIN}i7080${EXE} : ${I7080} ${SIM}
	${CC} ${I7080} ${SIM} ${I7080_OPT} -o $@ ${LDFLAGS}

${BIN}i7070${EXE} : ${I7070} ${SIM}
	${CC} ${I7070} ${SIM} ${I7070_OPT} -o $@ ${LDFLAGS}

${BIN}i7010${EXE} : ${I7010} ${SIM}
	${CC} ${I7010} ${SIM} ${I7010_OPT} -o $@ ${LDFLAGS}

${BIN}i704${EXE} : ${I704} ${SIM}
	${CC} ${I704} ${SIM} ${I704_OPT} -o $@ ${LDFLAGS}

${BIN}i701${EXE} : ${I701} ${SIM}
	${CC} ${I701} ${SIM} ${I701_OPT} -o $@ ${LDFLAGS}

