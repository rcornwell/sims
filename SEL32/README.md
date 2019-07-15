
# SEL32 Development Simulator

This is a working copy of a simulator for the SEL Concept/32 computer.
The current test version is for the SEL 32/27, 32/67, 32/87, 32/97 computers.
Support for 32/55, 32/75, V6, and V9 computers may be added in the future.

# SEL Concept/32 

This simulator is running a test version of MPX-32 1.5F.  It is capable of
creating a disk image of the O/S from a SDT tape.  The disk image can be
booted, initialized, and run many of the MPX utilities; including OPCOM & TSM.
Eight terminals can be used to access TSM via Telnet port 4747.  Initial
support has been added for excess 64 floating point arithmetic.  More testing
is still required.  The sim32disk.gz can be uncompressed and booted with the
sel32.27.sim32.disk.ini initialization file.  The sim32sdt.tap.gz file can
also be uncompressed and started with the sel32.27.sim32.tape.ini initialization
file to install from tape.

Available tap tools:
taptools.tgz - set of tools to work with .tap formatted tapes.  Also tools
               to convert between MPX and UNIX file formats.  See README
               file and source for descriptions.

Available disk images:
sim32disk.gz - bootable 300mb disk with MPX1.5F installed.  Unzip before
               any attempt to use it.  Use sel32.27.sim32.disk.ini command
               file to start MPX 1.5.  ./sel32 sel32.27.sim32.disk.ini

Available configuration SDT tapes:
sim32sdt.tap - MPX 1.5f user SDT install tape.  Uses 300mb disk, IOP 8-line
               serial terminals via telnet port 4747.  Line printer, 4 mag
               tapes and console terminal.  Terminal wakeup chan is ^G and
               console wakeup chars are @@A.  Install using TSM from console
               after exiting opcom.  Use filemgr to restore files from tape.

               TSM> A3 IN=M91000,TAP
               TSM> A4 SLO=UT
               TSM> FILEMGR
               FIL> RESTORE
               FIL> X

Available Level One DIagnostic boot tape:
diag.ini       command file to start diags. ./sel32 diag.ini
diag.tap       bootable level one diagnostic tape w/auto testing.  Set cpu type
               to 32/27, 32/67, 32/97, or V9.  V6 will not work at this time.
               VM.CP1 runs 127 instruction tests w and w/o arithmetic exceotion
               enable.  100 passes work fine.
               VM.CP2 runs 94 different tests in various modes.  Currently fails
               at some point to be figured out.  Testing is extremely difficult
               without any source for the diagnostics.  Updates to follow as
               tests are corrected.

Other MPX verion support:
               I am still looking for an MPX 3.X user or master SDT tape.  I have
               much of the source, but no loadable code to creat a bootable system.
               Please keep looking for anyone who can provide these tapes.

James C. Bevier
07/06/2019
 
