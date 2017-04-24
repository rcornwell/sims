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
CC=gcc -g  -std=c99 -U__STRICT_ANSI__  -D_GNU_SOURCE -I.
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
KA10D = ./
KA10 = ${KA10D}ka10_cpu.c ${KA10D}ka10_sys.c ${KA10D}ka10_df.c \
        ${KA10D}ka10_dp.c ${KA10D}ka10_mt.c ${KA10D}ka10_cty.c \
        ${KA10D}ka10_lp.c ${KA10D}ka10_pt.c ${KA10D}ka10_dc.c \
        ${KA10D}ka10_rp.c ${KA10D}ka10_rc.c ${KA10D}ka10_dt.c \
        ${KA10D}ka10_dk.c ${KA10D}ka10_cr.c ${KA10D}ka10_cp.c \
        ${KA10D}ka10_rs.c ${KA10D}ka10_tu.c
KA10_OPT = -I.. -DUSE_INT64 -DKA=1 -DUSE_SIM_CARD

KI10D = ./
KI10 = ${KA10D}ka10_cpu.c ${KA10D}ka10_sys.c ${KA10D}ka10_df.c \
        ${KA10D}ka10_dp.c ${KA10D}ka10_mt.c ${KA10D}ka10_cty.c \
        ${KA10D}ka10_lp.c ${KA10D}ka10_pt.c ${KA10D}ka10_dc.c  \
        ${KA10D}ka10_rp.c ${KA10D}ka10_rc.c ${KA10D}ka10_dt.c \
        ${KA10D}ka10_dk.c ${KA10D}ka10_cr.c ${KA10D}ka10_cp.c \
        ${KA10D}ka10_rs.c ${KA10D}ka10_tu.c
KI10_OPT = -I.. -DUSE_INT64 -DKI=1 -DUSE_SIM_CARD

#
# Build everything
#
all : ${BIN}ka10${EXE} ${BIN}ki10${EXE}

clean : 
ifeq ($(WIN32),)
	${RM} ${ALL}
else
	if exist BIN\*.exe del /q BIN\*.exe
endif
#
# Individual builds
#
${BIN}ka10${EXE} : ${KA10} ${SIM} 
	${CC} ${KA10} ${SIM} ${KA10_OPT} -o $@ ${LDFLAGS}

${BIN}ki10${EXE} : ${KI10} ${SIM} 
	${CC} ${KI10} ${SIM} ${KI10_OPT} -o $@ ${LDFLAGS}


