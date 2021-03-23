# Makefile for taptools
SHELL = /bin/sh

# Adapt the flags in the following paragraph to your system
#  for Linux
ROOT = .
OPTC = -O #-m32

#B = $(ROOT)
#B = $(ROOT)/bin
B = /system/bin
#I = $(ROOT)/include
I =
#L = $(ROOT)/lib
#D = $L/mylib.a 
D =

##CFLAGS= $(OPTC) -I$I
CFLAGS= $(OPTC) -I$I -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE

# For Linux
#LFLAGS= -L$L
LFLAGS= 

PROGS =			\
	$(ROOT)/diskload \
	$(ROOT)/filelist \
	$(ROOT)/fmgrcopy \
	$(ROOT)/mkfmtape \
	$(ROOT)/mkvmtape \
	$(ROOT)/sdtfmgrcopy \
	$(ROOT)/mkdiagtape \
	$(ROOT)/tapdump	\
	$(ROOT)/tape2disk \
	$(ROOT)/disk2tap \
	$(ROOT)/tapscan \
	$(ROOT)/eomtap \
	$(ROOT)/volmcopy \
	$(ROOT)/diagcopy \
	$(ROOT)/ddump \
	$(ROOT)/deblk \
	$(ROOT)/mpxblk \
	$(ROOT)/renum \
	$(ROOT)/tap2disk \
	$(ROOT)/cutostap \
	$(ROOT)/small

all:	$(PROGS)

#install	:$(PROGS)
#	@cp $(@F) $B
#	@echo $(@F) installed in $B
#	@echo
##	@chmod 755 $(@F)

##$(PROGS): $D $$(@F).c
##	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
##$(PROGS): $D $$(@F).c
##	@chmod 755 $@
##	@cp $(@F) $B
##	@echo $(@F) installed in $B
##	@echo

$B/diskload:	$D diskload.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/filelist:	$D filelist.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/fmgrcopy:	$D fmgrcopy.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/mkfmtape:	$D mkfmtape.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/mkvmtape:	$D mkvmtape.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/sdtfmgrcopy:	$D sdtfmgrcopy.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/mkdiagtape:	$D mkdiagtape.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/tapdump:	$D tapdump.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/tape2disk:	$D tape2disk.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/disk2tap:	$D disk2tap.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/tapsca:	$D tapscan.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/eomtap:	$D eomtap.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/volmcopy:	$D volmcopy.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/diagcopy:	$D diagcopy.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/ddump:	$D ddump.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/deblk:	$D deblk.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/mpxblk:	$D mpxblk.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/renum:	$D renum.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/cutostap:	$D cutostap.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/tap2disk:	$D tap2disk.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

$B/small:	$D small.c
	@-$(CC) $(CFLAGS) $(@F).c $(LFLAGS) -o $@
	@chmod 755 $@
	@cp $(@F) $B
	@echo $(@F) installed in $B

# Some makes don't understand the $$ notation above.  In this case
# you have to type out the compile paragraph for each PROG.  Sigh.
# Here's a start, good luck.
#
#$B/abshw:	abshw.c
#	$(CC) $(CFLAGS) $? $(LFLAGS) -o $@
#	@chmod 751 $@
#	@echo $(@F) installed in $B

remake	:	clobber
	@make ROOT=$(ROOT) OPTC=$(OPTC)
	
clean	:
	@-rm -f a.out junk* JUNK* core
	@-rm -f *.o

clobber	:	clean
	@-rm -f $(PROGS)

install	: $(PROGS)
	@cp $(PROGS) $B
	@echo $(PROGS) installed in $B
	@echo
#	@chmod 755 $(@F)
