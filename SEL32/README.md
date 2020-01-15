
# SEL32 Development Simulator

This is a working copy of a simulator for the SEL Concept/32 computer.
The current test version is for the SEL 32/27, 32/67, 32/77, 32/87 and
32/97 computers.  Initial support is provided for the V6 and V9 cpus.
Support for 32/55 computers may be added in the future.

# SEL Concept/32 

This simulator is running a test version of MPX-32 1.5F.  It is capable of
creating a disk image of the O/S from a SDT tape.  The disk image can be
booted, initialized, and run many of the MPX utilities; including OPCOM & TSM.
Eight terminals can be used to access TSM via Telnet port 4747.  Initial
support has been added for excess 64 floating point arithmetic.  More testing
is still required.  The sim32disk.gz can be uncompressed and booted with the
sel32.27.sim32.disk.ini initialization file.  The sim32sdt.tap.gz file can
also be uncompressed and started with the sel32.27.sim32.tape.ini initialization
file to install to disk from tape.

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

Available Level One Diagnostic boot tape:
diag.ini       command file to start diags. ./sel32 diag.ini
diag.tap       bootable level one diagnostic tape w/auto testing.  Set cpu type
               to 32/27, 32/67, 32/87, 32/97, V6 or V9.  32/97 and V9 diags 
               halt at test 46,subtest 2 in the CN.MMM & VM.MMM diags.  The
               rest of the machines stop in the CN.COM console diagnostic.

               CV.CSL - Firmware control diag.  Disabled in auto testing.
               CV.CP1 - CPU diag part 1 runs OK.
               CV.CP2 - CPU diag part 2 runs OK.
               CV.CP3 - CPU diag part 3 runs OK.
               CV.EAD - Effective address diag runs OK.
               CV.BRD - Base register instruction diag runs OK, except 32/27.
               CV.INT - Interrupt diag runs OK.
               CV.TRP - Traps diag runs OK.
               CV.CMD - Cache/Shadow diag.  Disabled in auto testing.
               CN.MMM - Non virtual memory diag runs OK, except for 32/97.
               VM.MMM - virtual memory diag for V6 & V9 runs OK, except for V9.
               CV.IPT - IPU trap diag.  Disabled in auto testing.
               CV.CSD - WCS read/write trap diag.  Disabled in auto testing.
               CV.CON - Operators Console diag halts at 0x852e for all CPUs.

               Set GPR[0] = 0xffffffff before booting from tape to disable the
               auto test and go to the Diagnostic Overlay Loader (DOL>) prompt.
               Testing is extremely difficult without any source for the
               diagnostics.  Updates to follow as tests are corrected.

Available UTX-21a install tape for testing:
utxtape1.ini   command file to start UTX install tape.  ./sel32 utxtape1.ini
utx21a1.tap    bootable UTX install tape for testing basemode.  The current
               V6 & V9 will boot UTX into single user mode.  You can run a
               small subset of the commands that on the installation tape.
               Prep, the disk preparation UTX program, cannot format a disk
               drive at this time due to unsupport disk commands that are
               required. All basemode instructions have been tested with
               the CV.BRD diag.  The virtual memory has been fully tested
               with the VM.MMM diag.  UTX needs better support for the disk
               controller before we can move any farther with the installation.

Other MPX verion support:
               I am still looking for an MPX 3.X user or master SDT tape.  I have
               much of the source, but no loadable code to creat a bootable system.
               Please keep looking for anyone who can provide these tapes or a
               disk image of a bootable system..

James C. Bevier
01/14/2020 

