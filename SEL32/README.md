
# SEL32 Concept/32 Simulator

This is a working simulator for the SEL Concept/32 computer. The 
current version is for the SEL 32/27, 32/67, 32/77, 32/87, 32/97, V6,
and V9 computers.  All of the processors except for the 32/77 can run
the Gould diags.  Operational support for the 32/77 computers may be
added in the future.

# SEL Concept/32 

This simulator is capable of running UTX2.1A, UTX2.1B, MPX 1.5F, and a
test version of MPX 3.4.  It is capable of creating a disk image for the
O/S from a UTX or MPX SDT tape. The disk image can be booted, initialized,
and can run many of the UTX and MPX utilities and programs. Ethernet is
supported on UTX and may be added to MPX in the future.  Eight terminals
can be used to access MPX or UTX via Telnet port 4747. The sumulator has
support for excess 64 floating point arithmetic and passes the 32/27 and
32/67 FP diags.

The sim32disk.gz file is a prebuilt MPX 1.5F system disk.  It can be
uncompressed and booted with the sel32.27.sim32.disk.ini initialization
file.  The sim32sdt.tap.gz file can also be uncompressed and started with
the sel32.27.sim32.tape.ini initialization file to do a sdt install to
disk from tape.  There are three test versions of UTX.  Utxtest1a.ini,
utxtest1b.ini, utxtest1c.ini are three initialization files to build a
UTX 2.1A system.  Utxtest1a.ini builds and loads the root filesystem on
/dev/dk0a.  Utxtest1b.ini boots the new root file system from disk and
builds the usr file system on /dev/dk0d.  Utxtest1c.ini boots the UTX
system from disk and enters multi-user mode.  Utxtest2[a-c].ini files
do the same thing for UTX 2.1B.  Utxscsi[a-c].ini builds a MFP SCSI disk
UTX 2.1B system.  These files are only available from my sims project at
https://github.com/AZBevier/sims.  A MPX 3.X test version will be added
in the future when testing is complete.

Available tap tools in taptools directory:
./taptools   - set of tools to work with .tap formatted tapes.  Also tools
               to convert between MPX and UNIX file formats.  See README
               file and source for descriptions.

Available disk images:
sim32disk.gz - bootable 300mb disk with MPX1.5F installed.  Unzip before
               any attempt to use it.  Use sel32.27.sim32.disk.ini command
               file to start MPX 1.5. Type "./sel32 sel32.27.sim32.disk.ini"

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

Available Level One Diagnostic boot tape in tests directory:
diag.ini     - command file to start diags. Type "./sel32 tests/diag.ini"
diag.tap     - bootable level one diagnostic tape w/auto testing.
               Set cpu type to 32/27, 32/67, 32/87, 32/97, V6 or V9.  All
               cpu models now run all diagnostics provided on the
               diagnostic tape.  Running DEXP stand alone causes input
               to stop after a few characters are entered.  More testing
               is still required.

               CV.CSL - Firmware control diag.  Disabled in auto testing.
               CV.CP1 - CPU diag part 1 runs OK.
               CV.CP2 - CPU diag part 2 runs OK.
               CV.CP3 - CPU diag part 3 runs OK.
               CV.EAD - Effective address diag runs OK.
               CV.BRD - Base register instruction diag runs OK, except 32/27.
               CV.INT - Interrupt diag runs OK.
               CV.TRP - Traps diag runs OK.
               CV.CMD - Cache/Shadow diag.  Disabled in auto testing.
               CN.MMM - Non virtual memory diag runs OK.
               VM.MMM - Virtual memory diag for V6 & V9 runs OK.
               CV.IPT - IPU trap diag.  Disabled in auto testing.
               CV.CSD - WCS read/write trap diag.  Disabled in auto testing.
               CV.CON - Operators Console runs all tests for all CPUs.
               CV.DXP - Diagnostic executive for level 2 diags. OK in batch.
               67.FPT - Level two floating point diag runs under DXP OK.
               CV.ITD - Level two interval timer diag runs under DXP OK.

               Set GPR[0] = 0xffffffff before booting from tape to disable the
               auto test and go to the Diagnostic Overlay Loader (DOL>) prompt.
               Testing is extremely difficult without any source for the
               diagnostics.  Updates to follow as tests are corrected.

Available UTX-21a install tape for testing:
utxtest1a.ini  command file to start UTX install tape.  "./sel32 utxtest1a.ini"
utx21a1.tap    bootable UTX install tape for testing basemode.  The current
               V6 & V9 will boot UTX into single/multi user mode.  You can run
               the full set of the commands that are on the installation tapes.
               Prep, the disk preparation UTX program, can format a disk
               drive.  Other file systems can be created and saves restored.
               All basemode instructions have been tested with the CV.BRD diag.
               The virtual memory has been fully tested with the VM.MMM diag.

Other MPX verions support:
               I am still looking for an MPX 3.X user or master SDT tape.  I have
               much of the source, but no loadable code to create a bootable system.
               Please keep looking for anyone who can provide these tapes or a
               disk image of a bootable MPX3.X system.

James C. Bevier
04/14/2021 

