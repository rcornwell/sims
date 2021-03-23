This directory contains several utilites to work with .tap metadata
files used by the simh simulators.  Some of these would be usable
for any simulator, not just the SEL32 simulator.  General use MPX
utilities are also attached that help with file format conversion
between MPX and UNIX files.

Users should create the directories /system and /system/bin to hold
the utilities.  The directories should be world readable/writable.
chmod 777 /system /system/bin
The following two lines should be added to your ~/.bashrc so you
will be able to execute the utilities from anywhere:
PATH=$PATH:/system/bin
export PATH
You can test to see if correct by typing 'echo $PATH' and you
should get a response with /system/bin in the directory list.

Compile and install the utilities by typing 'make install' to
install utilities into the /system/bin directory.


cutostap - This program scans a metadata .tap file and copies files
           until an EOF is found.  This program is used to remove
           the sdt image from a .tap file to be used in another
           .tap sdt image.  The mkfmcopy can be used to create a
           user sdt tape with files following the sdt image.

           command: cutostap <file.tap >stdout
           input -  stdin  <file to remove sdt image from
           output - stdout >file to be written with sdt image

diskload - This program reads an MPX load module and stores it into
           simulated diskfile.  The MPX 1.X SMD entry for the file
           is entered into the SMD for the file.  Will not work for
           MPX 3.X file systems.

           command: diskload -la program diskfile
           option -a - add filename to diskfile
           option -l - list files in diskfile SMD, ignore filename
           input     -  filename - file to copy to system disk
                     -  diskfile - simulated system disk
           output    - modified system disk

eomtap -   This program reads a tape assigned as input device.  It
           copies files until two EOFs are read and then makes sure
           two EOFs are written followed by an EOM. This program
           makes sure a .tap tape is readable without errors.

           command: eomtap mt00 filename
           input -  mag tape device or file being read
           output - disk file containing tape file contents

filelist - This program scans a metadata .tap file and prints the
           file count and sizes.  Used to determine the file
           format contained in the metadata .tap file.

           command: filelist <file.tap >stdout
           input  - stdin  <file to dump
           output - stdout

fmgrcopy - This program reads a MPX 1.X filemgr save tape.  The tape
           must contain a filemgr save image with 4608 byte records.
           SMD entries and file data are modulo 1152 32 bit words or
           4608 bytes.  The program will create a directory in the
           current directory for each different username.  Null user
           name will use the system directory.  Within each directory
           each file contained on the tape is extracted and written
           as binary data to the named file.  The .tap file MUST be
           a filemgr save tape and not a MPX 1.X SDT tape.

           command: fmgrcopy file.tap >stdout
           input -  file.tap file to dump
           output - stdout filelist and sizes
           output - directory/files extracted to current directory

mkfmtape - This program creates an MPX 1.X filemgr save tape.  The
           tape can then be used to restore files to the MPX 1.x
           system.  The output will be in SIMH simulated .tap format.

           command: mkfmtape opts output.tap file1 file2 ...
           input - list of filename to save to tape.
           output - output.tap a tap formatted file.
           options - -p = file type 0xca for programs
                   - -t = ASCII text file type 0xee
                   - -l = library/directory file type 0xff
                   - -o = other type 0x00
                   - -a = append entries to current file
                   - -u = username (directory)

mkvmtape - This program reads MPX files and stores them into a
           simulated volmgr save tape. The tape may then become
           a MPX 3.X sdt boot tape to install MPX 3.X from or a
           volmgr file restore tape for a running MPX system.
           The output will be in SIMH simulated .tap format.

           command: mkfmtape [-ptloa] [-bboot] [-iimage] [-jj.vfmt]
                            [-uusername] tapname file1 file2 ...
           intput - [options] volmtape filename, filename, etc.
           output - volmtape file, file list to stdout

           options - -p = file type 0xca for programs
                   - -t = ASCII text file type 0xee
                   - -l = library/directory file type 0xff
                   - -o = other type 0x00
                   - -a = append entries to current file
                   - -u = username (username)
                   - -d = dirname (directory)
                   - -v = volname (volume)
                   - -b = bootfile name
                   - -i = system image file
                   - -j = j.vfmt filename

mkdiagtape.c - This program extracts the diag command file (file 2)
           from a diagnostic tape in .tap format and replaces it
           with a new command file.  The rest of the tape is copied
           unchanged.  The original command file from the diag tape
           must be extracted from the diag tape using the diagcopy
           utility.  This file can then be deblocked using the deblk
           utility. The file can then be editied using VI or your
           favorite editor.  Lines must be blank filled to 80 chars
           exactly after editing.  The mpxblk utility must then be
           used to restore the file to blocked MPX format before
           installing it on the new diag tape.

           1. mkdir temp; Create temp directory.
           2. cp diag.tap temp; Copy in current diag tape.
           3. cd temp; Move to temp directory.
           4. diagcopy diag.tap; Extract diag tape contents.
           5. deblk cmdfile >cmd.txt;  Creat unblocked text file.
           6. vi cmd.txt; Edit text file with new commands and save.
           7. mpxblk <cmd.txt >cmd.blk; Restore blocked file format.
           8. mkdiagtape -c cmd.txt diag.tap newdiag.tap.
           9. copy newdiag.tap to execution directory and run sel32.

           command: mkdiagtape -c cmdfile olddiag newdiag
           intput - simulated in_diagtape
           output - simulated newtape
           option - -c cmdfile

diagcopy - This program reads a SEL .tap diagnostic boot tape and splits
           the contents into multiple files.  The first tape record
           is 204 bytes of boot code and is put into the file bootcode.
           The following records in the file contains the diagnostic
           overlay program (DOL) and are in 7680 byte blocks.  The last
           record can be <= 7680 bytes.  The data is put into the file
           dolfile.  The 2nd file on the tape contains the diagnostic
           auto execute file.  It is a MPX blocked file where the first
           word of each 768 char block contains 0x0003f3b8.  It can be
           read using the deblk utility in the taptools directory.
           The following diagnostic files on the tape are written to
           files named diagfileNN where NN is relative file number on
           the tape.  These records are all multiple of 768 bytes each
           and contain binary programs.

           command: diagcopy diag.tap
           input  - diag.tap file to dump
           output - stdout filelist and sizes
           output - if tape contains a valid diag image, it will be
                    output to files bootfile, cmdfile, dolfile and
                    to diagfileNN, where NN is 3 or larger.

tapdump -  This program reads a metadata .tap file and prints a side
           by side hexdump of the file contents.  The records are 
           displayed as 256 byte chuncks.  After each record of 256
           bytes are displayed, hitting C/R will continue dumping, 
           hitting 'q' will terminate the display, and hitting 's'
           will skip to the next file on the simulated tape.

           command: tapdump <file.tap >stdout
           input  - stdin <file to dump
           output - stdout

tap2disk - This program reads a tape assigned as input device.  It
           creates the specified output file and reads the tape
           until an EOF is encountered.  Only one file is read from
           the tape. Good to transfer tar files from SEL32 to Linux.

           command: tap2disk mt00 filename
           input -  mag tape device being read
           output - disk file containing tape file contents

tape2disk - This program reads a tape assigned as input device.  It
           generates a .tap metadata file.  Stdout will contain a
           listing of the files and sizes written to disk.  The define
           #define FILEMGR must be compiled in for tapes ending with
           two EOFs.  Unix and MPX 1.X filemgr tapes use that format.
           MPX 3.X volmgr save tapes contain three EOFs so comment out
           the define for that case. 

           command: tape2disk mt00 [file.tap]
           input -  mag tape device being read
           output - list of files and sizes read from input tape
           output - metadata .tap file optionally specified

disk2tap - This program reads a file assigned as input device.  It
           generates a .tap formatted metadata file.  Stdout will
           display the file name and size written to disk.

           command: disk2tap file [dest.tap]
           input -  input filename being read
           output - filename and size read from input
           output - metadata .tap file specified

tapscan -  This program scans a metadata .tap file and prints the
           file count and sizes.  Used to determine the file
           format contained in the metadata .tap file.

           command: tapscan file.tap >stdout
           input -  file.tap file to scan
           output - stdout filelist and sizes

volmcopy - This program reads a MPX 3.X volmgr save tape.  The tape
           must contain a volmgr save image with 6144 byte records
           containing a list of saved files.  Followed by directory
           entries of 1536 bytes and finally file data of 1 to 8 768
           byte records. Files larger than 6144 bytes will be output
           as modulo 6144 bytes.  There is an EOF after each file
           definition.  The program will create a directory in the
           current directory for each different directory. Within
           each directory each file contained on the tape is extracted
           and written as binary data to the named file.  The .tap
           file MUST be a volmgr save tape and not a MPX 1.X SDT/save
           tape.

           command: fmgrcopy file.tap >stdout
           input -  file.tap file to dump
           output - stdout filelist and sizes
           output - directory/files extracted to current directory

General utilities for MPX

ddump -    Create a side by side ASCII dump of a file.  Same operation
           as the DDUMP utility in MPX.  256 bytes are displayed at
           a time.  Hitting C/R will continue to next 256 bytes.  A
           hex address can be input to display data at a given offset
           in the file.  Optionally, the file data can be modified.

           command:  ddump -r filename
           option -  -r means open file read only 
           input -   filename file to read
           output -  side by size ASCII dump of file

deblk -    read and convert a MPX blocked file to unblocked UNIX file
           format.  Compressed and uncompressed files records can be
           read.  Output is an ASCII string with '\n' termination.

           command:  deblk [filename]
           input -   filename or if non specified, stdin
           output -  ASCII sting to stdout

mpxblk -   Create an MPX blocked file from a '\n' terminated ASCII
           character string file.  Trailing blanks are not deleted
           from the source file.  Max line size is 254 bytes.

           command:  mpxblk <filein >fileout
           input   - read a '\n' terminated ASCII file from stdin
           output  - write MPX blocked file to stdout

renum -    Create a numbered file from a '\n' terminated ASCII file.
           The input file is assumed to be a standard UNIX file.  The
           input lines are truncated or expaned to lines of 72 chars.
           A line number in the form of XXXX.000 is appended to
           create 80 char '\n' terminated lines.

           command:  renum <filein >fileout
           input -   read ASCII file from stdin
           output -  write numbered ASCII file to stdout

small -   Remove line numbers and trailing blanks from an ASCII '\n'
          terminated file.  Lines are terminated at 72 chars and then
          stripped of trailing blanks.  Output is '\n' terminated
          ASCII files.

          command:   small <filein >fileout
          input -    read ASCII file from stdin
          output -   write stripped ASCII file to stdout

James C. Bevier
02/15/2021
