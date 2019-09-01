# Makefile for utils
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
	$(ROOT)/makecode

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

$B/makecode:	$D makecode.c
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
