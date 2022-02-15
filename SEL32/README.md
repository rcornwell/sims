
# SEL32 Concept/32 Simulator

This is a working simulator for the SEL Concept/32 computer. The 
current version is for the SEL 32/27, 32/67, 32/77, 32/87, 32/97, V6,
and V9 computers.  All of the processors except for the 32/77 can run
the Gould diags.  Operational support for the 32/77 computers may be
added in the future.

# SEL Concept/32 

This simulator is capable of running UTX2.1A, UTX2.1B, MPX 1.5F, MPX 3.4,
MPX 3.5, MPX 3.6, and SEL diagnostics. Actually any software using the SEL
instruction set should work.  It is capable of creating a disk image for the
O/S from a UTX or MPX SDT tape. The disk image can be booted, initialized,
and can run many of the UTX and MPX utilities and programs. Ethernet is
supported on UTX and will be added to MPX in the future.  Eight terminals
can be used to access MPX or UTX via Telnet port 4747. The sumulator has
support for excess 64 floating point arithmetic and passes the 32/27 and
32/67 FP diags.  UTX is the SEL version of System V Unix and BSD Unix
ported to the V6 and V9 processors.  UTX utilizes the basemode instruction
set and a virtual memory system supported by the V6 & V9 CPUs.  The system
needs further testing to solidify the SEL32 simulator code in all of the
supported environmenets and hardware configurations.

Note: Not all of the command files described below may be in the tests
directory for the github.com/AZBevier/SEL32-rcsim repository.  All will
be in the github.com/AZBevier/sims repository along with any new versions
that are being tested.  Feel free to copy them to your local tests
directory.  They may be added to the SEL32-rcsim repositary at some
later date.

In the tests directory there are multiple auto installation command files
that do MPX/UTX installs from user distribution tapes.  This replaces the
prebuilt disks from previous releases.  This allows more versions to be
supplied in the minimum amount of storage space.  Minimal or no user input
is required to create the bootable system.  The command files are in pairs,
one to do the install and one to execute the installed system.  Some MPX
systems also contain some of the NBC software environment.  See the NBC
project at github.com/AZBevier/nbc for all of the NBC software.

# SEL32 installation configuration files in the tests directory:

#sel32_test.ini - diag.tap; type "../sel32_test.ini" or run from makefile.
Auto run by sel32 makefile to validate build or can be run at any time
to validate system functionality.

#diag.ini - diag.tap; type "../sel32 diag.ini" to run.
This is the SEL32 memory diagnostic that validates the SEL32 instruction
set and memory management sumulated hardware.  Any errors are reported
to the terminal. Different cpu models can be configured to test various
cpu model features.

--------------------

The rest of the command files are zipped to reduce project storage space.
The software distribution tape files are also zipped in the tapes
directory.  All zipped files must be unzipped to be useable.  See the
MPX manuals at bitsavers.org/pdf/sel/sel32_mpx for using MPX 1X or 3X.

--------------------

#sel32load1x.ini - sel32sdt.tap; type "../sel32 sel32load1x.ini
This is a minimal MPX 1.5F installation to a UDP/DPII 300 MB disk.  It
will initialize the disk and install MPX bootable software.  The disk
image is in the dsk directory named sel32disk.

#sel32run1x.ini - dsk/sel32disk; type "../sel32 sel32run1x.ini" to run.
The disk is booted up to the TSM> prompt logged in as "SYSTEM".  Use
@@A to log in to the system console.

--------------------

#loaddma1x67.ini - mpx1xsdt.tap; type "../sel32 loaddma1x67.ini
This is an MPX 1.5F installation to a UDP/DPII 300 MB disk.  It will
initialize the disk and install MPX bootable software.  The disk
image is in the dsk directory named mpx1xdma0.  Once the MPX software
is loaded an MPX command file is executed that runs the SYSGEN program
to create a new MPX O/S image.  That system is then restarted to
install the new image to the disk as the new bootable image.  The
system reboots to the new image, logs in as SYSTEM and exits TSM.
Use @@A to relogin to the console.

#rundma1x67.ini - dsk/mpx1xdma0; type "../sel32 rundma1x67.ini" to run.
The disk is booted up to the TSM> prompt and logged in as "SYSTEM".
MPX can be accessed from a second Linux screen by using the command
"telnet locallhost 4747".  This will bring up the "ENTER OWNERNAME
AND KEY:".  Any name is valid, but SYSTEM should be used.  At the
"TSM>" prompt, type "EXIT" to exit TSM.  Use ^G to get the login
prompt when the "RING IN FOR SERVICE" message is displayed.

--------------------

#loaddma21b.ini - utx21b1.tap, utx21b2.tap, utx21b3.tap;
type "../sel32 loaddma21b.ini" to install UTX 21b to UDP/DPII disk.
This is an automated installation of UTX 21b to disk.  Two disks,
21bdisk0.dma and 21bdisk1.dma are initialized and then the file
systems are created and loaded.  Tape 1 loads "/" and tape 2 and 3
loads "/usr.POWERNODE" filesystems. The system boots from tape and
installs the root filesystem.  The system restarts and boots from
the new root filesystem where the 2nd & 3rd tapes are then loaded to
/usr.POWERNODE.  A third empty file system is created and mounted
as /usr/POWERNODE/src.  The second disk is one large filesystem and is
mounted under /home.  Several files are modified during installation
to allow the system to be booted into multiuser mode.  Only the user
"root" is created and is the only allowable user login.

#rundma21b.ini - dsk/21bdisk0.dma & dsk/21bdisk1.dma;
type "../sel32 rundma21b.ini" to run the installed UTX system.
The disk is booted up to the "login:" prompt for the user to login
as "root" in multi-user mode.

--------------------

#loadscsi21b.ini - utx21b1.tap, utx21b2.tap, utx21b3.tap;
type "../sel32 loadscsi21b.ini" to install UTX 21b to MFP SCSI disks.
This is an automated installation of UTX 21b to disk.  Two disks,
scsidiska0 and scsidiska1 are initialized and then the file systems
are created and loaded.  Tape 1 loads root "/" and tapes 2 and 3
loads "/usr.POWERNODE" filesystem. The system boots from tape and
installs the root filesystem.  The system restarts and boots from
the new root filesystem where the 2nd & 3rd tapes are then loaded to
/usr.POWERNODE.  A third empty file system is created and mounted
as /usr/POWERNODE/src.  The second disk is one large filesystem and is
mounted under /home.  Several files are modified during installation
to allow the system to be booted into multiuser mode.  Only the user
"root" is created and is the only allowable user login.

#rundscsi21b.ini - dsk/scsidiska0 & dsk/scsidiska1;
type "../sel32 rundscsi21b.ini" to run the installed UTX system.
The disk is booted up to the "login:" prompt for the user to login
as "root" in multi-user mode.

--------------------

#loadscsi3x.ini - mpxsdt69.tap;
type "../sel32 loadscsi3x.ini" to install MPX 3.4 to MFP SCSI disks.
This is an automated installation of MPX 3.4 to disk.  Two 300MB disks,
mpx3xsba0.dsk and mpx3xsbb0.dsk are initialized and then the file
systems are created and loaded.  The user sdt tape contains system
and user files that are loaded to multiple directories.  The second
disk is initialized and formatted and only a system directory defined.
The install is exited and @@A is used to login into MPX.  The username
SYSTEM is used to login into TSM without a password.

#runscsi3x.ini - dsk/mpx3xsba0.dsk & dsk/mpx3csbb0.dsk;
               - dsk/scsi35m1disk0 & dsk/scsi35m2disk0;
type "../sel32 rundscsi3x.ini" to run the installed MPX system.
The disk is booted up to the MPX message "Press Attention for TSM".
Use @@A to get login prompt.  Login as SYSTEM.  The WORK volume will
be mounted along with the SYSTEM volume and the system is ready for
use.  MPX can be accessed from a second Linux screen by using the
command "telnet locallhost 4747".  This will bring up the "Connected
to the SEL-32 simulator COMC device, line 0".  Use ^G as the wakeup
character to get the "ENTER YOUR OWNERNAME:" login prompt.  Any name
is valid, but SYSTEM should be used.  At the "TSM>" prompt, type
"EXIT" to exit TSM.  Use ^G to get the login prompt when the "RING
IN FOR SERVICE" message is displayed.

--------------------

#user36esdtp2.ini - user36esdtp2.tap;
type "../sel32 user36esdtp2.ini" to install MPX 3.6 to HSDP disks.
This is an automated installation of MPX 3.6 to disk.  A 300MB system
disk volume (user36p2udp0) and a 600MB work disk volume (user36s1udp1)
are initialized and then the file systems are created and loaded using
the volmgr.  The user sdt tape contains system and user files that
are loaded to multiple directories.  The second disk is initialized
and formatted and only a system directory defined.  The disk is mounted
as the volume "work" as the 2nd disk drive.  The installed MPX system
also has 2 scsi disks configured into the system.  Two 700MB SCSI disks
are created, but they are not initialized and no directories are
created.  The usage of these disks is left as an exercise for the user.
A third HSDP 600MD disk is also configured in MPX, but not used.  The
user can provide other data volumes that can be mounted for use on the
system.  The install is exited and @@A can be used to login into MPX.
The username SYSTEM is used to login into TSM without a password.  Any
username is valid until an m.key file is created for valid user login.

#user36erunp2.ini - dsk/user36p2udp0 & dsk/user36s1udp1;
type "../sel32 user36erunp2.ini" to run the installed MPX 3.6 system.
The disk is booted up to the MPX message "Press Attention for TSM".
@@A is used to get the login prompt and the user is logged in as SYSTEM.
The WORK volume will be mounted along with the SYSTEM volume and the
system is ready for use at the TSM> prompt.  The install tape also has
some of the NBC development system.  A complete installation tape is
available at github.com/azbevier/nbc.

--------------------

Available tap tools in taptools directory:
./taptools   - set of tools to work with .tap formatted tapes.  Also tools
               to convert between MPX and UNIX file formats.  See README
               file in the taptools directory and source for descriptions.

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

Other MPX versions support:
               I have recently received some old MPX 3.X save tapes.  Using these
               I have been able to hand build a MPX3.6 SDT tape that can be used
               to install MPX3.6.  Once installed, the system can be used to build
               a new user SDT tape and install it elsewhere.  Both based and non-
               based O/S images can be created.  More images for installation will
               be made available in the future as I work my way through the save
               tapes. I still do not have a master SDT tape for any of the MPX 1.X
               or MPX 3.X systems.  I have a 1600/6250 BPI tape drive that can read
               9 track tapes and convert them to .tap files.  If you have a master
               SDT, I would be very thankfull.  Please keep looking.

James C. Bevier
12/31/2021 

