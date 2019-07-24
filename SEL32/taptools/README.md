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
           command filelist <file.tap >stdout
           input - stdin  <file to remove sdt from
           output - stdout >file to be written with sdt image

diskload - This program reads an MPX load module and stores it into
           simulated diskfile.  The MPX-1.x SMD entry for the file
           is entered into the SMD for the file.  Will not work for
           MPX 3.x file systems.
           command: diskload -la program diskfile
           option -a - add filename to diskfile
           option -l - list files in diskfile SMD, ignore filename
           input - filename - file to copy to system disk
                 - diskfile - simulated system disk
           output - modified system disk

filelist - This program scans a metadata .tap file and prints the
           file count and sizes.  Used to determine the file
           format contained in the metadata .tap file.
           command filelist <file.tap >stdout
           input - stdin  <file to dump
           output - stdout

fmgrcopy - This program reads a MPX 1.x filemgr save tape.  The tape
           must contain a filemgr save image with 4608 byte records.
           SMD entries and file data are modulo 1152 32 bit words or
           4608 bytes.  The program will create a directory in the
           current directory for each different username.  Null user
           name will be the system directory.  Within each directory
           each file contained on the tape is extracted and written
           as binary data to the named file.  The .tap file MUST be
           a filemgr save tape and not a MPX-1.x SDT tape.
           command fmgrcopy file.tap >stdout
           input - file.tap file to dump
           output - stdout filelist and sizes
           output - directory/files extracted to current directory

sdtfmgrcopy - This program reads a MPX 1.x filemgr save tape or a
           user SDT followed by filemgr saves.  The filemgr tape 
           must contain a filemgr save image with 4608 byte records.
           A sdt tape starts with a 1920 byte record followed by
           multiple 768 byte records to finish the sdt system image.
           A sdt tape can be followed by one or more filemgr save
           images of 4608 bytes. The boot image is put into bootfile1
           in the current directory.  The program will create a
           directory in the current directory for each different
           username.  A null username will be the system directory.
           Within each directory each file contained on the tape is
           extracted and written as binary data to the named file.
           The .tap file MUST be a filemgr save tape and not a
           MPX 3.x volmgr save tape.
           command sdtfmgrcopy file.tap >stdout
           input - file.tap file to dump
           output - stdout filelist and sizes
           output - if tape contains a sdt image, it will be output
                    to the file bootfil1
           output - directory/files extracted to current directory

tapdump -  This program reads a metadata .tap file and prints a side
           by side hexdump of the file contents.  The records are 
           displayed as 256 byte chuncks.  After each record if 256
           bytes are displayed, hitting <cr> will continue dump, 
           hitting <q> will terminate the display, and hitting <s>
           will skip to the next file on the simulated tape.
           command tapdump <file.tap >stdout
           input - stdin  <file to dump
           output - stdout

tape2disk - This program reads a tape assigned as input device.  It
           generated a .tap metadata file.  Stdout will contain a
           listing of the files and sizes written to disk.  The define
           #define FILEMGR must be compiled in for tapes ending with
           two EOFs.  Unix and MPX 1.x filemgr tapes use that format.
           MPX 3.x volmgr save tapes contain three EOFs so comment out
           the define for that case. 
           command - tape2disk mt00 [file.tap]
           input - mag tape device being read
           output - list of files and sizes read from input tape
           output - metadata .tap file optionally specified

tapscan -  This program scans a metadata .tap file and prints the
           file count and sizes.  Used to determine the file
           format contained in the metadata .tap file.
           command - tapscan file.tap >stdout
           input - file.tap file to scan
           output - stdout filelist and sizes

volmcopy - This program reads a MPX 3.x volmgrr save tape.  The tape
           must contain a volmgr save image with 6144 byte records
           containing a list of saved files.  Followed by directdory
           entries of 1536 bytes and finally file data of 1 to 8 768
           byte records. Files larger than 6144 bytes will be output
           as modulo 6144 bytes.  There is an EOF after each file
           definition.  The program will create a directory in the
           current directory for each different directory. Within
           each directory each file contained on the tape is extracted
           and written as binary data to the named file.  The .tap
           file MUST be a volmgr save tape and not a MPX-1.x SDT/save
           tape.
           command fmgrcopy file.tap >stdout
           input - file.tap file to dump
           output - stdout filelist and sizes
           output - directory/files extracted to current directory

General utilities for MPX

ddump -    Create a sys by side ascii dump of a file.  Same operation
           as the DDUMP utility in MPX.  256 bytes are displayed at
           a time.  Hitting <cr> will continue to next 256 bytes.  A
           hex address can be input to display data at a given offset
           in the file.  Optionall, the file data can be modified.
           command - ddump -r filename
           option -  -r means open file read only 
           input -   filename file to read
           output -  side by size ascii dump of file

deblk -    read and convert mpx blocked ifile to unblocked unix file
           format.  Compressed and uncompressed files records can be
           read.  Output is an ascii string with '\n' termination.
           command - deblk [filename]
           input -   filename or if non specified, stdin
           output -  ascii sting to stdout

mpxblk -   Create an MPX blocked file from a '\n' terminated ascii
           character string file.  Trailing blanks are not deleted
           from the source file.  Max line size is 254 bytes.
           command - mpxblk <filein >fileout
           input   - read ascii file from stdin
           output  - write mpx blocked file to stdout

renum -    Create a numbered file from a '\n' terminated ascii file.
           The input file is assumed to be a standard unix file.  The
           input lines are truncated or expaned to lines of 72 chars.
           A line number in the form of XXXX.000 are appended to
           create 80 char '\n' terminated lines.
           command - renum <filein >fileout
           input -   read ascii file from stdin
           output -  write numbered ascii file to stdout

small -   Remove line numbers and trailing blanks from an ascii '\n'
          terminated file.  Lines are terminated at 72 chars and then
          stripped of trailing blanks.  Output is '\n' terminated
          ascii files.
          command -  small <filein >fileout
          input -    read ascii file from stdin
          output -   write stripped ascii file to stdout

