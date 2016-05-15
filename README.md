This is a working copy of my KA10 and KI10 and IBM7000 series simulators for SimH.

These are still in the testing stages. Please report any issues that you find in them.

The KA10 sim has successfully run Tops 10 5.03 and Tops 10 6.03, however this was before
modifications where made to include support for the RP04/5/6/7 with an RH10 controller. This
controller has not been tested. Also the start of support for the KI10 has been added, 
none of this has been tested.

Currently the KA10 CPU is implimented in such a way that is simulates the hardware directly,
this makes is slow.

Todo:

   Test RP04/5/6/7 under Tops 6.03. 
   Write TM03 controller.
   Finish KI10 support.
   Possibly add in support for KL10A. (No extended addressing).
   Clean up KA10/KI10 instruction implimentation to be less step oriented. 

Latest status for I7000 Cpus:

i701:		Largely untested.

i704:		SAP works.
		Fort2 unfinished.

i7090:		Working with exceptions.
		
		Known bugs:

                When starting IBSYS there is an I/O Check generated.

		DFDP/DFMP     Sometimes off by +/-1 or 2 in least signifigant
				 part of result.

		HTx	Not sure what problems are, does not quite work.

		DKx	Sometimes fails diagnostics with missing inhibit of
			interupt. 

                CTSS    works.

                Share Lisp 1.5 does not boot.

		Signifigence mode Not tested Need.

                    IOMC diag codes:
                    first keys 403030 and 001717 does channel E
                    first keys 203030  test channel A
                    016000200000  for disk test chan D

i7070:		Will load Diags, need to remember how to run them to run
		tests on machine. 
		Need to update instruction timing for new I/O

i7010:		Protection mode has some errors left.
		Protection mode does not handle setting H or L to 0.
		T020 tape diags fail.
		Disks may work.
		PR108 not tested.

i7080:		Sort of working. 
		RWW, ECB untested.
		TLx instructions implimented, untested, see 8SE
		Will boot from card.
		Tape system appear to be working.
		    Tape system will not pass diags.

		8CU10B errors:
			410, 412, 413, 414, 418, 419, 420-427 error becuase
				storage is not parity checked. 
			440 divide not producing correct sign on error.
