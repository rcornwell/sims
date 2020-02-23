
[![Coverity Scan Build Status](https://scan.coverity.com/projects/12020/badge.svg)](https://scan.coverity.com/projects/rcornwell-sims)  

This is a working copy of my Burroughs B5500, Dec PDP6, KA10, KI10, KL10, SEL32, IBM 360
and IBM7000 series simulators for SimH.

# Stable released simulators.

# IBM 7000 simulators.
Latest status for I7000 Cpus: 

## i701

   * Largely untested.  

## i704
   * SAP works.  
   * Fortran II works in some cases.

## i7010
   * PR155 works.
   * PR108 works.
   * Most Diags appear to pass without serious error.
   * Protection mode has some errors left.  
   * Protection mode does not handle setting H or L to 0.  

## i7070
   * Will load Diags.

## i7080
   * Sort of working.   
   * RWW, ECB untested.  
   * TLx instructions implimented, untested, see 8SE  
   * Will boot from card.  
   * Tape system appear to be working.  

   * 8CU10B errors:  
	410, 412, 413, 414, 418, 419, 420-427 error becuase
		storage is not parity checked.   
	440 divide not producing correct sign on error.  

## i7090
   * Working with exceptions.  

   * Known bugs:  

      * DFDP/DFMP     Sometimes off by +/-1 or 2 in least signifigant part of result.  
      * EAD           +n + -n should be -0 is +0
      * Not all channel skips working for 9P01C.
      * HTx	Not sure what problems are, does not quite work.  
      * DKx	Sometimes fails diagnostics with missing inhibit of interrupt.   

   * CTSS    works.  
  
   * IBSYS   works.  
  
   * Stand alone assembler works.  

   * Lisp 1.5 works.  

   * Signifigance mode Not tested, Test Code Needed.  

# Burroughs B5500

 Emulates a dual CPU B5500 with up to 32K of memory. I can support either DFX disks or
 dual disks up to 20 units. Up to 16 magnetic tapes drives is support. 2 Card readers
 a card punch and a pair of line printers. Will run MCP XIII and XV.

  * Up to either 10 disks with DFX support or 20 disks without.  
  * Up to 16 magnetic tape drives.  
  * Dual card reader.  
  * Dual Line printer.  
  * Single card punch.
  * Up to 32 terminal lines.  

# Dec PDP6

The PDP6 simulator is part of the KA/KI simulator. The PDP6 devices can be built into the KA10
simulator if desired.

The PDP6 runs TOPS 10 4.5 off Dectape.

  Disk  
  * 270 Disk  

  Tape  
  * 551 Tape  
  * 555 Dectape.  

  Paper Tape  
  * Punch  
  * Reader  

  Type 630 Terminal mux.  

  Type 340 graphics display.  

# Dec KA10 & KI10 & KL10

The KA10 sim has successfully run Tops 10 4.5, 5.03 and 6.03, ITS and WAITS.  
The KI10 sim has successfully run Tops 10 6.03 with VMSER.  
The KL10 sim has successfully run Tops 10 6.03-7.03, ITS and Tops 20 V2-V7.  

   Disk   
   * RC10 RD10/RM10  
   * RP10 RP01/RP02/RP03  
   * RH10/RH20 RP04/RP06/RP07 (RP07 not supported under 6.03).   
   * RH10/RH20 RS04  
   * PMP (P. PETIT'S IBM CHANNEL)  
   * System Concepts DC-10 IBM 2314  

   Tape  
   * TM10A or B  
   * RH10/RH20 TM03/TU16  
   * TD10 Dectape  

   Paper Tape  
   * Punch  
   * Reader  
 
   * DC10E terminal mux.  
   * Morton tty Mux for ITS.  
   * Knight Kludge mux for ITS.  
   * Terminals on KL10 Front End.  

   * LP10 Line printer.  
   * LP20 Line printer on KL10 Front End.  

   * IMP networking support for ITS and TENEX.  
   * NIA20 networking support for KL10.  
   * CH10 networking support for ITS.  

   * 340 Display Station support.  
   * III Display support for WAITS.  

   * Optional support for PDP6 devices (standard if WAITS included).  

   The RP10 supports boot (actual hardware did not), by reading in Blocks 4-7
and transfering to the loaded value. RC10, RH10, TM10 support readin mode. 

# ICL 1900 simulator.

This is a new simulator. Will pass 1904E/1905E CPU diagnostics (FLIT). Will boot paper
tape executive E4BM and tape executive E6RM. Still need to add more devices and test
with Goerge 2 and 3.

  The CPU can be configured for model A, B or C type system. Or models 1901 throgh 1909. 
Paging is currently not supported. Both standard interface and older non-standard interface
are supported on most device.


# IBM 360 simulator.

Currently will run IPL TOS/360, DOS/360, OS/360. TSO support still in progress.
IBM 360/67 support added for MTS early versions. 
Support for the following units:

    DASD (Disk) Up to 4 devices of 8.
    * 2301, 2302, 2303, 2305, 2311, 2314, 3330.
    
    Tape, 8 devices.
    * 2400 (7 track and 9 track).

    Unit record
    * 2540 Card reader
    * 2540 Card punch
    * 1403 Line Priner
    * 1050 Console

    Communications
    * 2703 with 16 lines of 2741 or 1050.

# SEL32 Development Simulator

This is a working copy of a simulator for the SEL Concept/32 computer.
The current test version is for the SEL 32/27, 32/67, 32/87, 32/97 computers.
Support for 32/55, 32/75, V6, and V9 computers may be added in the future.
This simulator is co-authors with James C. Bevier. I did the initial parts
of the simulator, James took it to a working simulator, I am assisting him
in maintaining and enhancing the simulator. This simulator is running a test
version of MPX-32 1.5F.  It is capable of creating a disk image of the O/S
from a SDT tape.  The disk image can be booted, initialized, and run many of
the MPX utilities; including OPCOM & TSM.  Eight terminals can be used to
access TSM via Telnet port 4747.  Initial support has been added for excess
64 floating point arithmetic.  More testing is still required.  The sim32disk.gz
can be uncompressed and booted with the sel32.27.sim32.disk.ini initialization file.
The sim32sdt.tap.gz file can also be uncompressed and started with the
sel32.27.sim32.tape.ini initialization file to install from tape.


