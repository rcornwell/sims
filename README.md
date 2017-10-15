
[![Coverity Scan Build Status](https://scan.coverity.com/projects/12020/badge.svg)](https://scan.coverity.com/projects/rcornwell-sims)  

# Development Simulators.

This is a working copy of my Burroughs B5500, Dec KA10, KI10, IBM 360 and IBM7000 series
simulators for SimH.

# Burroughs B5500

This is the same version as in the current simH source tree. It is working
correctly as far as I am able to tell. It will currently run MCP XIII and XV.

# Dec KA10 & KI10

These are still in the testing stages. Please report any issues that you find in them.

The KA10 sim has successfully run Tops 10 5.03 and Tops 10 6.03.  
The KI10 sim has successfully run Tops 10 6.03  
Support for ITS (untested).  
Support for BBN pager (untested).  

   Disk   
   * RC10 RD10/RM10  
   * RP10 RP01/RP02/RP03  
   * RH10 RP04/RP06/RP07 (RP07 not supported under 6.03).   
   * RH10 RS04  

   Tape  
   * TM10A or B  
   * RH10 TM03/TU16  
   * TD10 Dectape  

   Paper Tape  
   * Punch  
   * Reader  
 
   DC10E terminal mux.  

   The RP10 supports boot (actual hardware did not), by reading in Blocks 4-7
and transfering to the loaded value. RC10, RH10, TM10 support readin mode. 

   Possibly add in support for KL10A. (No extended addressing). 

To do:  
   * Add support for IBM 3330 drive to run Waits.  
   * Add support for DC76 for ITS.  

# IBM 360 simulator.

This is a new simulator. There is still much work and testing that needs to be done.
The goal is to have it run TOS, DOS and OS/360. If there is interest I might try early 
versions of MTS, and possibly add in 370 paging support.

It supports the following units:

    DASD (Disk)
    * 2301, 2302, 2303, 2305, 2311, 2314, 3330.
    
    Tape
    * 2400 (7 track and 9 track).

    Unit record
    * 2540 Card reader
    * 2540 Card punch
    * 1403 Line Priner
    * 1050 Console

    Communications
    * 2703 with 16 lines of 2741 or 1050. (Todo).

# IBM 7000 simulators.
Latest status for I7000 Cpus: 

## i701

   * Largely untested.  

## i704
   * SAP works.  
   * Fort2 unfinished.  

## i7010
   * PR155 works.
   * PR108 works.
   * Most Diags appear to pass without serious error.
   * Protection mode has some errors left.  
   * Protection mode does not handle setting H or L to 0.  

## i7070
   * Will load Diags, need to remember how to run them to run
   * tests on machine.   

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

   * Signifigence mode Not tested, Test Code Needed.  

