   00001                                * PRE FILE FOR NON-EXTENDED MODE OF MPX-32 REV 3  RELEASE 3.6   MPX_NON
   00072                                C.3227   SETT
   00073                                C.TRACF  SETF
   00074                                C.MEMO   SETF
   00075                                BOPT_MPX SETF
   00076                                C.FWMID  SETF                                                   EMMM-35A
   00077                                C.DPMODE SETF                                                   EMMM-35B
   00078                                C.DPDEV  SETF                                                   EMMM-35B
   00079                                MPX_SVC  SET       6                                            ECAL-35
   00080                                C.MPXOUT SETF                                                   EOUT-35
   00081                                *.EXTD35 SETT                                           E2CCM35 EIPU360
   00082                                *.SEAR   SETF                                           E2CCM35 EIPU360
   00083                                *.RISCFL SETT                                          E2CCM35A EIPU360
   00084                                *.INITF  SETF                                          E2CCM35B EIPU360
   00085                                *.SEGRFL SETF                                          E2CCM35A E2SEG35
   00086                                *.SEGRFL SETT                                           E2SEG35 EIPU360
   00087                                *.BRSTK  SETT                                          E2CCM35C EIPU360
   00088                                *.BUFIO  SETT                                           S920301 EIPU360
   00089                                         REIS                                                   EOUT-35
   00090                                         LIST      NOMAC
   00091   P00000                                M.SETREV  3.6,1                                        EIPU360
   00092                                         LIST      MAC
   00093                                *TAPE & FLOPPY DISC BOOTSTRAP LOADER(S)                 HEADER  FUPBOO  0001.000
   00094   P00000                                M.PGM     FUPBOOT,MPX-32,3.6.1,00                              0002.000
                                                 PROGRAM   FUPBOOT MPX-32_3.6.1.00
                                                 ENDM
  
   00096                                *                                                                       0004.000
   00097                                ************************************************************************0005.000
   00098                                *                                                                      *0006.000
   00099                                *                    PROPRIETARY INFORMATION                           *0007.000
   00100                                *                    -----------------------                           *0008.000
   00101                                *   THE INFORMATION CONTAINED HEREIN IS PROPRIETARY TO ENCORE          *0009.000
   00102                                *   COMPUTER CORPORATION AND/OR ITS VENDORS, AND ITS USE, DISCLOSURE,  *0010.000
   00103                                *   OR DUPLICATION IS SUBJECT TO THE RESTRICTIONS STATED IN THE        *0011.000
   00104                                *   STANDARD ENCORE COMPUTER CORPORATION LICENSE TERMS AND CONDITIONS  *0012.000
   00105                                *   OR THE APPROPRIATE THIRD-PARTY SUBLICENSE AGREEMENT.               *0013.000
   00106                                *                                                                      *0014.000
   00107                                *                       RESTRICTED RIGHTS                              *0015.000
   00108                                *                       -----------------                              *0016.000
   00109                                *   USE, DUPLICATION, OR DISCLOSURE BY THE GOVERNMENT IS SUBJECT       *0017.000
   00110                                *   TO RESTRICTIONS AS SET FOURTH IN SUBDIVISION (c) (1) (ii) OF       *0018.000
   00111                                *   THE RIGHTS IN TECHNICAL DATA AND COMPUTER SOFTWARE CLAUSE AT       *0019.000
   00112                                *   252.227.7013                                                       *0020.000
   00113                                ************************************************************************0021.000
   00114                                * (C) COPYRIGHT 1989 ENCORE COMPUTER CORPORATION                       *0022.000
   00115                                *     ALL RIGHTS RESERVED                                              *0023.000
   00116                                ************************************************************************0024.000
   00117                                *                                                                       0025.000
  
   00119                                ************************************************************************0027.000
   00120                                *                                                                      *0028.000
   00121                                **                                                                    **0029.000
   00122                                ***                                                                  ***0030.000
   00123                                ****             * * *  W A R N I N G  * * *                        ****0031.000
   00124                                *****                                                              *****0032.000
   00125                                ****             SOME PARAMETERS (AMONG THE ONES THAT ARE DEF'ED)   ****0033.000
   00126                                ***              ARE INITIALIZED BY THE SDT MODULE/ROUTINE(S)        ***0034.000
   00127                                **                                                                    **0035.000
   00128                                *                                                                      *0036.000
   00129                                ************************************************************************0037.000
   00130                                *                                                                       0038.000
   00131                                *        EXTERNAL DEFINITIONS                                           0039.000
   00132                                *                                                                       0040.000
  
   00134                                * (C) COPYRIGHT 1989 ENCORE COMPUTER CORPORATION                        0042.000
   00135                                *     ALL RIGHTS RESERVED                                               0043.000
  
   00137                                         DEF       BOOTL           # BYTES IN BOOTSTRAP                 0045.000
   00138                                         DEF       MTB             STARTING ADDRESS OF BOOTSTRAP LOADER 0046.000
   00139                                         DEF       SDTFLG          SDT LOAD FLAGS                       0047.000
   00140                                         DEF       MAXSECT         MAXIMUM STARTING SECTOR FOR FLOPPY   0048.000
   00141                                         DEF       IOCD.ZR         REZERO COMMAND CODE                  0049.000
   00142                                         DEF       IOCD1           IOCD #1 FOR IPL                      0050.000
   00143                                         DEF       IOCD.BKR        BACKSPACE RECORD COMMAND CODE        0051.000
  
   00145                                ************************************************************************0053.000
   00146                                *                                                                      *0054.000
   00147                                *               SYSTEM DISTRIBUTION BOOTSTRAP LOADER                   *0055.000
   00148                                *                     (MAG TAPE OR FLOPPY)                             *0056.000
   00149                                *                                                                      *0057.000
   00150                                ************************************************************************0058.000
   00151                                *                                                                      *0059.000
   00152                                *        THE FOLLOWING LOADER IS COPIED TO THE BEGINNING OF THE        *0060.000
   00153                                *        SYSTEM DISTRIBUTION MEDIUM.  IT IS EXECUTED BY ENTERING       *0061.000
   00154                                *        THE ADDRESS OF THE DEVICE UPON WHICH THE MEDIUM (TAPE         *0062.000
   00155                                *        OR FLOPPY) IS MOUNTED VIA THE CPU FRONT PANEL AND             *0063.000
   00156                                *        DEPRESSING THE IPL SWITCH.                                    *0064.000
   00157                                *                                                                      *0065.000
   00158                                *        THIS SYSTEM DISTRIBUTION BOOTSTRAP LOADS CODE IN              *0066.000
   00159                                *        MPX-32 LOAD MODULE FORMAT AND WILL DEFAULT TO A STARTING      *0067.000
   00160                                *        LOCATION OF X'800' (32/7X) OR X'780' (32/27) UNLESS A         *0068.000
   00161                                *        DIFFERENT BIAS VALUE IS ENTERED IN R3 PRIOR TO DEPRESSING     *0069.000
   00162                                *        IPL ON THE CPU FRONT PANEL.                                    0070.000
   00163                                *        THIS VERSION OF THE BOOTSTRAP IS THE MPX 2.0 VERSION.          0071.000
   00164                                *        IT HAS BEEN MODIFIED TO READ ONLY THE OPERATING SYSTEM         0072.000
   00165                                *        IMAGE, AND THEN TRANSFER CONTROL TO SYSINIT, WHICH WILL        0073.000
   00166                                *        INITIALIZE THE SYSTEM AND ACTIVATE THE KEY LOAD MODULES        0074.000
   00167                                *        THAT FOLLOW THE SYSTEM ON THE SDT TAPE. THIS BOOT SHOULD       0075.000
   00168                                *        NOT BE USED WITH ANY EARLIER VERSION OF MPX.                   0076.000
   00169                                *                                                                      *0077.000
   00170                                ************************************************************************0078.000
   00171                                         LIST      NOMAC                                                0079.000
   00172   P00000                                M.REQS                                                         0080.000
   00173   P00000                                M.PR.                                                          0081.000
   00174   P00000                                M.COMM.                                                        0082.000
   00175   P00000                                M.BREGS                                                        0083.000
   00176   P00000                                M.TSA.                                                         0084.000
  
   00178                                *                                                                       0086.000
   00179                                *        BOOTSTRAP EQUATES AND CONSTANTS                                0087.000
   00180                                *                                                                       0088.000
  
   00182    00010                       MT       EQU       X'10'           MT CHANNEL ADDRESS                   0090.000
   00183    00001                       DELAY    EQU       1                                                    0091.000
   00184    00002                       ACTIVE   EQU       2                                                    0092.000
   00185    00000                       MTBUF    EQU       0               START 0F MT BUFFER IS L0C 0          0093.000
   00186    0B000                       MTREAD   EQU       X'B000'         800BPI,PACKED,ODD PARITY - READ TD   0094.000
   00187    00800                       BKSPCREC EQU       X'0800'         BACKSPACE RECORD TD                  0095.000
   00188    00200                       MTSKF    EQU       X'0200'         SKIPFILE CT COMMAND CODE             0096.000
   00189    00007                       EORFFLG  EQU       7               SET = CLASS F                        0097.000
   00190    00008                       INTRLVL  EQU       8               BOOT INTRPT PRIORITY LEVEL           0098.000
   00191    FFFFB                       RCNT     EQU       -5              XIO ERROR RETRY COUNT                0099.000
   00192    00724                       IOCB27   EQU       X'724'          IOCB LOC FOR 32/27 TAPE I/O          0100.000
  
   00194   P00000                                BOUND     1W                                                   0102.000
  
   00196   P00000                                BOUND     8W                      START ON AN 8W BOUNDARY      0104.000
   00197   P00000                       MTB      EQU       $                       SETUP CONFIGURATION RAM      0105.000
   00198   P00000  EC000029    00028             BU        BOOT.ST-MTB                                          0106.000
   00199   P00004  00004000                      DATAW     X'00004000'             2ND WD OF NEW PSD BLOCKED    0107.000
   00200   P00008  53000000             IOCD1    GEN       8/X'53',24/0            BKSR OR RZR TO RE-READ       0108.000
   00201   P0000C  60000001                      GEN       8/X'60',24/1            CMD CHAIN; SUPRESS INCOR LGTH0109.000
   00202   P00010  02000000             IOCD2    GEN       8/X'02',24/0            READ/ADDRESS 0               0110.000
   00203   P00014  000006EC    006EC             GEN       8/0,24/BOOTLEN          # BYTES IN BOOTSTRAP (*SDT)  0111.000
   00204   P00018                                BOUND     1D                                                   0112.000
   00205   P00018  8000002C    0002C    FORCEPSD GEN       8/X'80',24/W(REALST-MTB)    PRIV ONLY                0113.000
   00206   P0001C  00004000                      DATAW     X'00004000'             2ND WD OF PSD -- BLOCKED     0114.000
   00207   P00020  0000F000             CLAS.MSK DATAW     X'F000'                 MSK, BITS 16-19=1 "F", 0 "E" 0115.000
   00208   P00024  0000000F             CPUMASK  DATAW     X'0000000F'     GET MACHINE TYPE                     0116.000
   00209   P00028                       BOOT.ST  EQU       $                                                    0117.000
   00210   P00028  F9800018    00018             LPSD      FORCEPSD-MTB            FORCE PRIV ONLY              0118.000
   00211   P0002C                       REALST   EQU       $                                                    0119.000
   00212   P0002C  D40001B8    001B8             STW       R0,GPR0-MTB     SAVE USER IMAGE CHOICE       EMST-35 0120.000
   00213   P00030  D5000380    00380             STW       R2,NUMSKIP-MTB       SAVE SYSBUILD FILE SKIP NO.     0121.000
  
   00215                                *                                                                       0123.000
   00216                                *  NOTE:  THE CPU MODEL INDICATOR IS CONTAINED IN BITS 28-31 OF THE     0124.000
   00217                                *  CPU STATUS WORD.  THIS FIELD SHOULD BE INTERPRETED AS AN INTEGER,    0125.000
   00218                                *  DEFINED AS FOLLOWS:                                                  0126.000
   00219                                *        0  -  CONCEPT-32/2000                                  EMST-35 0127.000
   00220                                *        1  -  RESERVED                                         EMST-35 0128.000
   00221                                *        2  -  CONCEPT-32/27                                    EMST-35 0129.000
   00222                                *        3  -  CONCEPT-32/67                                    EMST-35 0130.000
   00223                                *        4  -  CONCEPT-32/87                                    EMST-35 0131.000
   00224                                *        5  -  CONCEPT-32/97                                    EMST-35 0132.000
   00225                                *        6  -  RESERVED                                         EMST-35 0133.000
   00226                                *        7  -  RESERVED                                         EMST-35 0134.000
   00227                                *                                                                       0135.000
  
   00229   P00034  0109                          RDSTS     R2                   READ CPU STATUS WORD            0137.000
   00230   P00038  85000024    00024             ANMW      R2,CPUMASK-MTB  GET MACHINE TYPE                     0138.000
   00231   P0003C  D508023D    0023D             STB       R2,MACH-MTB     SAVE IT                              0139.000
   00232   P00040  C9050001                      CI        R2,1            TEST FOR 32/75                       0140.000
   00233   P00044  F200004D    0004C             BNE       NOT1-MTB        IF NOT, GO ON                EMST-35 0141.000
   00234   P00048  F8800191    00190             BL        ERR.B-MTB       IF MACH TYPE = 1, ERROR      EMST-35 0142.000
   00235   P0004C  C9050005             NOT1     CI        R2,5            IS MACH TYPE > 5?            EMST-35 0143.000
   00236   P00050  EF000059    00058             BLE       NOTBIG-MTB      IF NOT, GO ON                EMST-35 0144.000
   00237   P00054  F8800191    00190             BL        ERR.B-MTB       IF SO, ERROR                 EMST-35 0145.000
   00238   P00058  C9050000             NOTBIG   CI        R2,0            IS MACH TYPE < 0?            EMST-35 0146.000
   00239   P0005C  EE800065    00064             BGE       SET27-MTB       MACHINE TYPE OK - GO ON      EMST-35 0147.000
   00240   P00060  F8800191    00190             BL        ERR.B-MTB       IF SO, ERROR                 EMST-35 0148.000
   00241                                *        BGT       SET27-MTB       BRANCH FOR 32/27 OR 32/87    EMST-35 0149.000
   00242                                *                                                                       0150.000
   00243                                *        32/7X SCRATCHPAD SET-UP                                        0151.000
   00244                                *                                                                       0152.000
   00245                                *        LI        R5,X'8F'             DRT INDEX IN SPAD       EMST-35 0153.000
   00246                                *        SLL       R5,16                FORM SPAD SETTING       EMST-35 0154.000
   00247                                *        TSCR      R5,R4                R4 = DEVICE PROTOCOL    EMST-35 0155.000
   00248                                *        ANMW      R4,CLAS.MSK-MTB      MASK OUT CLASS          EMST-35 0156.000
   00249                                *        BCT       ZR,ECLAS7X-MTB       IF 'E' CLASS ... BRANCH EMST-35 0157.000
   00250                                *        SBM       EORFFLG,CLASS-MTB    SHOW 'F' CLASS          EMST-35 0158.000
   00251                                *        SBM       EORFFLG,DEVTBL-MTB   SET CLASS IN DEVICE TBL EMST-35 0159.000
   00252                                *        LI        R4,SI.TRAP-MTB       R4 = SPAD TRAP ADDRESS  EMST-35 0160.000
   00253                                *        STW       R4,INTRTBL-MTB       STORE IN INTRPT TABLE   EMST-35 0161.000
   00254                                *        ZR        R2                   STATUS IN WORDS 0,1     EMST-35 0162.000
   00255                                *        BL        STATF.I-MTB          CHECK STATUS            EMST-35 0163.000
   00256                                *        BL        BTERR1-MTB           ERROR...HALT, R0=ABT AD EMST-35 0164.000
   00257                                *ECLAS7X  EQU       $                    'E' CLASS TAPE DRIVE   EMST-35 0165.000
   00258                                *        LW        R5,IPL7X-MTB         R5 = IPLDEV SPAD LOC.   EMST-35 0166.000
   00259                                *        TSCR      R5,R4                R4 = IPL DEVICE ADDR.   EMST-35 0167.000
   00260                                *        STB       R4,CH.SA+1B-MTB      SAVE SUBADDR FOR XIO    EMST-35 0168.000
   00261                                *        ARMW      R4,DEVTBL-MTB        ADD CH/SUBA TO DEV WORD EMST-35 0169.000
   00262                                *        SRLD      R4,8                 SUBADDRESS TO R5        EMST-35 0170.000
   00263                                *        LI        R4,X'10' USE BAS AD 10 FOR INT ENT 11AUG81BS EMST-35 0171.000
   00264                                *        TBM       EORFFLG,CLASS-MTB    'F' CLASS DEVICE?       EMST-35 0172.000
   00265                                *        BS        CHONLY-MTB           IF YES ... USE CH ONLY  EMST-35 0173.000
   00266                                *        SRL       R5,24                RIGHT JUSTIFY SUBADDR   EMST-35 0174.000
   00267                                *        ADR       R5,R4                ADD CHAN AND SUBADDR    EMST-35 0175.000
   00268                                *        SLL       R5,16 ADD SUBA TO DEV ENT AD/E-CLAS 11AUG81  EMST-35 0176.000
   00269                                *        ARMW      R5,DEVTBL+2W-MTB UPDAT DEV ENT AD  11AUG81BS EMST-35 0177.000
   00270                                *CHONLY   SLL       R4,16                FORM SPAD SETTING      EMST-35 0178.000
   00271                                *        ARMW      R4,INTRTBL-MTB       ADD TO INTRPT WORD      EMST-35 0179.000
   00272                                *        LW        R1,DEVTBL-MTB        R1 = SPAD DEVICE ENTRY  EMST-35 0180.000
   00273                                *        LW        R2,DEVTBL+2W-MTB     R2 = SPAD LOCATION      EMST-35 0181.000
   00274                                *        TRSC      R1,R2                DEVICE WORD TO SPAD     EMST-35 0182.000
   00275                                *        ZR        R2  NEED AT LOC 10 IF SUBA 14AUG81 BS        EMST-35 0183.000
   00276                                *        SBR       R2,11  SINCE I/O IS TO CHAN X'10'114AUG81 BS EMST-35 0184.000
   00277                                *        TRSC      R1,R2           TRANSFER TO SPAD 14AUG81 BS  EMST-35 0185.000
   00278                                *        LW        R1,INTRTBL-MTB       R1 = SPAD INTRPT ENTRY  EMST-35 0186.000
   00279                                *        LW        R2,INTRTBL+1W-MTB    R2 = SPAD LOCATION      EMST-35 0187.000
   00280                                *        TRSC      R1,R2                INTRPT WORD TO SPAD     EMST-35 0188.000
   00281                                *        BU        BOOTTD-MTB           SPAD SET ... CONTINUE   EMST-35 0189.000
   00282                                *                                                                       0190.000
   00283                                *        32/27 SCRATCHPAD SET-UP                                        0191.000
   00284                                *                                                                       0192.000
   00285   P00064  CA8000F8             SET27    LI        R5,X'F8'             R5 = SPAD DRT INDEX             0193.000
   00286   P00068  72D0                          SLL       R5,16                FORM SPAD SETTING               0194.000
   00287   P0006A      2E5F                      TSCR      R5,R4                R4 = DEVICE PROTOCOL            0195.000
   00288   P0006C  86000020    00020             ANMW      R4,CLAS.MSK-MTB      MASK OUT CLASS                  0196.000
   00289   P00070  EE000099    00098             BCT       ZR,ECLASPX-MTB       IF 'E' CLASS ... BRANCH         0197.000
   00290   P00074  9B88023C    0023C             SBM       EORFFLG,CLASS-MTB    SHOW 'F' CLASS                  0198.000
   00291   P00078  9B880220    00220             SBM       EORFFLG,DEVTBL+1W-MTB   SET CLASS IN DEVICE TBL      0199.000
   00292   P0007C  CA0000F1                      LI        R4,X'F1'             LOAD SPAD ITBA INDEX            0200.000
   00293   P00080  7250                          SLL       R4,16                FORM SPAD SETTING               0201.000
   00294   P00084  3680035C    0035C             LA        R5,ITBA-MTB          R5 = ITBA FOR BOOT              0202.000
   00295   P00088  2E5E                          TRSC      R5,R4                BOOT ITBA TO SPAD               0203.000
   00296   P0008A      0D20                      ZR        R2                   STATUS IN WORDS 0,1             0204.000
   00297   P0008C  F88003F5    003F4             BL        STATF.I-MTB          CHECK STATUS                    0205.000
   00298                                *        BL        BTERR1-MTB      ERROR...HALT, R0=ABORT ADDR  EMST-35 0206.000
   00299   P00090  F88003CD    003CC             BL        ERR.3-MTB       ERROR...HALT, R0=ABORT ADDR  EMST-35 0207.000
   00300   P00094  EC0000A5    000A4             BU        GETIPL-MTB           FETCH IPL ADDRESS               0208.000
   00301   P00098                       ECLASPX  EQU       $                                                    0209.000
   00302   P00098  CA8000F2                      LI        R5,X'F2'             IOCDBA SPAD INDEX               0210.000
   00303   P0009C  CA000700                      LI        R4,X'700'            IOCD BASE ADDRESS               0211.000
   00304   P000A0  72D0                          SLL       R5,16                SHIFT INDEX INTO POS.           0212.000
   00305   P000A2      2ECE                      TRSC      R4,R5                INIT IOCDBA IN SPAD             0213.000
   00306   P000A4  AE800238    00238    GETIPL   LW        R5,IPL27-MTB         R5 = IPLDEV SPAD LOC.           0214.000
   00307   P000A8  2E5F                          TSCR      R5,R4                R4 = IPL DEVICE ADDR.           0215.000
   00308   P000AC  D60806D1    006D1             STB       R4,CH.SA+1B-MTB      SAVE SUBADDR FOR XIO            0216.000
   00309   P000B0  86000214    00214             ANMW      R4,SPAD.MSK-MTB      REMOVE ALL BUT CHANNEL 7-29-81BS0217.000
   00310                                *        TRSC      R4,R5                THIS INST. NOT NEEDED 7-30-81BS 0218.000
   00311   P000B4  8A000220    00220             ORMW      R4,DEVTBL+1W-MTB     R4 = SPAD DEVICE WORD           0219.000
   00312   P000B8  AD000224    00224             LW        R2,DEVTBL+2W-MTB     R2 = SPAD LOCATION              0220.000
   00313   P000BC  2D4E                          TRSC      R4,R2                DEVICE WORD TO SPAD             0221.000
   00314   P000C0  AD000230    00230             LW        R2,INTRTBL+2W-MTB    R2 = INTRPT SPAD LOC.           0222.000
   00315   P000C4  2D4E                          TRSC      R4,R2                INTRPT WORD TO SPAD             0223.000
   00316   P000C8  36000694    00694             LA        R4,MTTCW-MTB         R4 = MAG TAPE TCW LOCATION      0224.000
   00317   P000CC  D6000724    00724             STW       R4,IOCB27            TCW ADDR TO IOCD                0225.000
   00318   P000D0  CA000780                      LI        R4,X'780'            32/27 LOAD POINT                0226.000
   00319   P000D4  D6000218    00218             STW       R4,DEFAULT-MTB       SET 32/27 DEFAULT LOAD PT.      0227.000
   00320                                *                                                                       0228.000
   00321                                *        INITIALIZE CHANNEL FOR 'F' CLASS I/O                           0229.000
   00322                                *                                                                       0230.000
   00323                                *BOOTTD   TBM       EORFFLG,CLASS-MTB    CK IF "F" CLASS        EMST-35 0231.000
   00324                                *        BCF       SET,NOT.F-MTB        BR IF "E"               EMST-35 0232.000
   00325   P000D8  F88000E1    000E0             BL        DO.INCH-MTB          DO THE INCH                     0233.000
   00326   P000DC  EC0001FD    001FC             BU        MTB1-MTB             CONTINUE                        0234.000
   00327                                *NOT.F    TD        MT,X'8000'  TEST STATUS FOR 'E' CLASS       EMST-35 0235.000
   00328                                *        BANY      E.ERR-MTB       BRANCH IF ANY ERRORS         EMST-35 0236.000
   00329                                *        TBM       MASTER,SDTFLG-MTB    TEST FOR MASTER SDT BOOTEMST-35 0237.000
   00330                                *        BNS       MTB1-MTB        NO, GO ON                    EMST-35 0238.000
   00331                                *                                                                       0239.000
   00332                                *        SKIP THE TAPE AHEAD 1 FILE TO FIND THE 32/75 MASTER IMAGE      0240.000
   00333                                *        THIS IS DONE WITH INTERRUPTS BECAUSE THE CONTROLLER DOES       0241.000
   00334                                *        NOT RETURN A BUSY STATUS ON A TD INSTRUCTION. AND IT IS        0242.000
   00335                                *        NICE TO KNOW WHEN THE SKIPFILE IS DONE!!!                      0243.000
   00336                                *                                                                       0244.000
   00337                                *        UEI                       UNBLOCK EXTERNAL INTERRUPTS  EMST-35 0245.000
   00338                                *        DI        X'18'           DISABLE THE INTERRUPT        EMST-35 0246.000
   00339                                *        EI        X'18'           RE--ENABLE IT                EMST-35 0247.000
   00340                                *                                                                       0248.000
   00341                                *        THE CD BELOW HAS ITS ICB @ X'7BC' AND WILL RETURN TO           0249.000
   00342                                *        LABEL MTB1 WITH INTERRUPTS BLOCKED                             0250.000
   00343                                *                                                                       0251.000
   00344                                *        CD        MT,MTSKF        DO THE SKIPFILE              EMST-35 0252.000
   00345                                *        WAIT                      WAIT FOR I/O COMPLETE        EMST-35 0253.000
   00346                                *E.ERR    BL        BTERR1.MTB      ERROR...HALT, R0=ABORT ADDR EMST-35 0254.000
  
   00348                                *                                                                       0256.000
   00349                                * NOTE: THERE ARE THREE IMAGES ON THE MASTER SDT:               EMST-35 0257.000
   00350                                *       MSTRALL (DEFAULT IMAGE FOR CONCEPT-32/XX MACHINES,      EMST-35 0258.000
   00351                                *                VALID OPTION FOR CONCEPT-32/2000)              EMST-35 0259.000
   00352                                *       MSTREXT (EXTENDED MPX - VALID OPTION FOR ALL CONCEPT    EMST-35 0260.000
   00353                                *                MACHINES EXCEPT 32/27)                         EMST-35 0261.000
   00354                                *       MSTROUT (DEFAULT IMAGE FOR CONCEPT-32/2000 - INVALID    EMST-35 0262.000
   00355                                *                OPTION FOR ALL OTHER MACHINES)                 EMST-35 0263.000
   00356                                *                                                               EMST-35 0264.000
   00357                                * THE USER CAN CHOOSE AN IMAGE OTHER THAN THE DEFAULT BY        EMST-35 0265.000
   00358                                * SETTING GPR0 TO ONE OF THE FOLLOWING VALUES BEFORE IPL:       EMST-35 0266.000
   00359                                * (NOTE ABOVE CONSTRAINTS)                                      EMST-35 0267.000
   00360                                *       GPR0 = 1 -- MSTRALL                                     EMST-35 0268.000
   00361                                *       GPR0 = 2 -- MSTREXT                                     EMST-35 0269.000
   00362                                *                                                               EMST-35 0270.000
   00363   P000E0                       DO.INCH  EQU       $                    'F' CLASS INCH                  0271.000
   00364   P000E0  D40001C0    001C0             STW       R0,INCH.RT-MTB       SAVE RETURN ADDRESS             0272.000
   00365   P000E4  A508066E    0066E             TBM       MASTER,SDTFLG-MTB    IS THIS A MASTER SDT            0273.000
   00366   P000E8  F080013D    0013C             BNS       SKF.L1-MTB           NO, SKIP THE SKIPFILES          0274.000
   00367   P000EC  AE88023D    0023D             LB        R5,MACH-MTB     GET THE MACHINE TYPE BYTE            0275.000
   00368                                *        CI        R5,2            IS THIS A 32/27 BOOT?        EMST-35 0276.000
   00369                                *        BEQ       SKF.L1-MTB      YES, NO FILE SKIPS           EMST-35 0277.000
   00370                                *        SBM       1,IOCD.SM+1W-MTB   SET CMD CHAIN BIT TO      EMST-35 0278.000
   00371                                *                               DO ONE SKIP FILE ON TAPE BOOT   EMST-35 0279.000
   00372                                *        SBM       1,INCH.FP+1W-MTB SET CMD CH BIT FOR FLP      EMST-35 0280.000
   00373                                *                                     TO DO ONE SKIP FILE       EMST-35 0281.000
   00374                                *        CI        R5,1            IS THIS A 32/75 BOOT?        EMST-35 0282.000
   00375                                *        BEQ       SKF.L1-MTB      YES, ONLY 1 SKIPFILE FOR 75  EMST-35 0283.000
   00376                                *        SBM       1,IOCD.SK1+1W-MTB  DO TWO SKIPFILES          EMST-35 0284.000
   00377   P000F0  AC0001B8    001B8             LW        R0,GPR0-MTB     GET USER'S IMAGE CHOICE      EMST-35 0285.000
   00378   P000F4  F18000FD    000FC             BNN       GPR0CHK2-MTB    IF NOT < 0, CHECK IF > 2     EMST-35 0286.000
   00379   P000F8  F8800189    00188             BL        ERR.A-MTB       IF SO, INVALID - HALT        EMST-35 0287.000
   00380   P000FC  C8050002             GPR0CHK2 CI        R0,2            IS GPR0 > 2?                 EMST-35 0288.000
   00381   P00100  EF000109    00108             BLE       GPR0OK-MTB      IF NOT, VALUE IS OK          EMST-35 0289.000
   00382   P00104  F8800189    00188             BL        ERR.A-MTB       IF SO, INVALID - HALT        EMST-35 0290.000
   00383   P00108  CA850000             GPR0OK   CI        R5,0            IS THIS A 32/2000?           EMST-35 0291.000
   00384   P0010C  F2000121    00120             BNE       NOTOUT-MTB      NO, CAN'T USE MAPPED OUT     EMST-35 0292.000
   00385   P00110  C8050000                      CI        R0,0            DO THEY WANT MAPPED OUT?     EMST-35 0293.000
   00386   P00114  F2000121    00120             BNE       NOTOUT-MTB      NO, CHOOSE ANOTHER IMAGE     EMST-35 0294.000
   00387   P00118  988801EC    001EC             SBM       1,IOCD.SK1+1W-MTB  SKIP SECOND IMAGE ON TAPE EMST-35 0295.000
   00388   P0011C  EC000135    00134             BU        SKIPF1-MTB      GO SKIP THE FIRST IMAGE, TOO EMST-35 0296.000
   00389   P00120  C8050002             NOTOUT   CI        R0,2            DO THEY WANT EXTENDED?       EMST-35 0297.000
   00390   P00124  F200013D    0013C             BNE       SKF.L1-MTB      NO - GO GET FIRST IMAGE      EMST-35 0298.000
   00391   P00128  CA850002                      CI        R5,2            IS THIS A 32/27?             EMST-35 0299.000
   00392   P0012C  F2000135    00134             BNE       SKIPF1-MTB      NO - SKIP FIRST IMAGE        EMST-35 0300.000
   00393   P00130  F8800199    00198             BL        ERR.C-MTB       MSTREXT INVALID FOR 32/27    EMST-35 0301.000
   00394   P00134  988801D4    001D4    SKIPF1   SBM       1,IOCD.SM+1W-MTB  SET CMD CHAIN BIT TO       EMST-35 0302.000
   00395                                *                             DO ONE SKIP FILE ON TAPE BOOT     EMST-35 0303.000
   00396   P00138  988801E4    001E4             SBM       1,INCH.FP+1W-MTB  SET CMD CHAIN BIT FOR      EMST-35 0304.000
   00397                                *                             FLOPPY TO DO ONE FILE SKIP        EMST-35 0305.000
   00398   P0013C                       SKF.L1   EQU       $                                                    0306.000
   00399   P0013C  A488066E    0066E             TBM       FLP,SDTFLG-MTB       IPL FROM FLOPPY?                0307.000
   00400   P00140  F080014D    0014C             BNS       INIT.TAP-MTB         IF NOT ... INIT FOR TAPE        0308.000
   00401   P00144  368001E0    001E0             LA        R5,INCH.FP-MTB       FETCH FLOPPY IOCD ADDRESS       0309.000
   00402   P00148  EC000151    00150             BU        $+2W-MTB             JOIN COMMON CODE                0310.000
   00403   P0014C  368001C8    001C8    INIT.TAP LA        R5,INCH.TP-MTB       FETCH TAPE IOCL ADDRESS         0311.000
   00404   P00150  D6800354    00354             STW       R5,IOCLA             STORE IOCL PTR. IN CONTROL BLK  0312.000
   00405   P00154  AE8006D1    006D0             LH        R5,CH.SA-MTB         FETCH LOGICAL CHANNEL           0313.000
   00406   P00158  FEE70000                      ECI       R5,0                 ENABLE CHANNEL INTERRUPT        0314.000
   00407   P0015C  0007                          UEI                            UNBLOCK INTERRUPTS              0315.000
   00408   P00160  F8800405    00404             BL        DO.SIO-MTB           INITIALIZE CHANNEL              0316.000
   00409   P00164  F88003F1    003F0             BL        CK.STATF-MTB         CHECK STATUS                    0317.000
   00410                                *        BL        BTERR1-MTB      ERROR...HALT, R0=ABORT ADDR  EMST-35 0318.000
   00411   P00168  F88003CD    003CC             BL        ERR.3-MTB       ERROR...HALT, R0=ABORT ADDR  EMST-35 0319.000
   00412   P0016C  A488066E    0066E             TBM       FLP,SDTFLG-MTB       IPL FROM FLOPPY?                0320.000
   00413   P00170  F080017D    0017C             BNS       IOCL.TAP-MTB         IF NOT ... SET IOCL FOR TAPE    0321.000
   00414   P00174  340003A0    003A0             LA        R0,IOCD.SK-MTB       FETCH ADDR. OF SEEK IOCD        0322.000
   00415   P00178  EC000181    00180             BU        $+2W-MTB             JOIN COMMON CODE                0323.000
   00416   P0017C  34000390    00390    IOCL.TAP LA        R0,IOCD.RD-MTB       FETCH ADDR. OF READ IOCD        0324.000
   00417   P00180  D4000354    00354             STW       R0,IOCLA             STORE IOCL PTR. IN CONTROL BLK  0325.000
   00418   P00184  EC1001C1    001C0             BU        *INCH.RT-MTB         RETURN TO CALLER                0326.000
   00419                                *                                                               EMST-35 0327.000
   00420                                *     ERROR PROCESSING                                          EMST-35 0328.000
   00421                                *                                                               EMST-35 0329.000
   00422   P00188  AF0001A2    001A0    ERR.A    LD        R6,ERRA.MSG-MTB  INVALID VALUE IN GPR0       EMST-35 0330.000
   00423   P0018C  EC0003D5    003D4             BU        ERR.HALT-MTB     HALT MACHINE                EMST-35 0331.000
   00424   P00190  AF0001AA    001A8    ERR.B    LD        R6,ERRB.MSG-MTB  INVALID MACHINE TYPE        EMST-35 0332.000
   00425   P00194  EC0003D5    003D4             BU        ERR.HALT-MTB     HALT MACHINE                EMST-35 0333.000
   00426   P00198  AF0001B2    001B0    ERR.C    LD        R6,ERRC.MSG-MTB  INVALID IMAGE FOR MACHINE   EMST-35 0334.000
   00427   P0019C  EC0003D5    003D4             BU        ERR.HALT-MTB     HALT MACHINE                EMST-35 0335.000
   00428   P001A0  494E5620             ERRA.MSG DATAD     C'INV GPR0'                                  EMST-35 0336.000
           P001A4  47505230
   00429   P001A8  4D414348             ERRB.MSG DATAD     C'MACHTYPE'                                  EMST-35 0337.000
           P001AC  54595045
   00430   P001B0  494E5620             ERRC.MSG DATAD     C'INV IMAG'                                  EMST-35 0338.000
           P001B4  494D4147
   00431   P001B8  00000000             GPR0     DATAW     0               CONTAINS USER'S IMAGE CHOICE EMST-35 0339.000
   00432                                *                                                                       0340.000
   00433                                *        INCH DATA AREA                                                 0341.000
   00434                                *                                                                       0342.000
   00435   P001BC  80000000             MODE     DATAW     X'80000000'          MODE = PACKED, ODD PARITY (TAPE)0343.000
   00436   P001C0  00000000             INCH.RT  DATAW     0                    CALLER'S RETURN ADDRESS         0344.000
   00437   P001C8                                BOUND     1D                                                   0345.000
   00438   P001C8  00000320    00320    INCH.TP  GEN       8/0,24/W(INCH.BUF-MTB)      TAPE INCH BUFFER         0346.000
   00439   P001CC  40000001                      GEN       8/X'40',24/1                COMMAND CHAIN; 1 BYTE    0347.000
   00440   P001D0  830001BC    001BC    IOCD.SM  GEN       8/X'83',24/W(MODE-MTB)      SET MODE                 0348.000
   00441   P001D4  00000001                      GEN       8/0,24/1                    1 BYTE                   0349.000
   00442   P001D8  080001E8    001E8    IOCD.TIC GEN       8/X'08',24/W(IOCD.SK1-MTB)  TRANSFER TO SKIPFILE     0350.000
   00443   P001DC  00000000                      DATAW     0                            IOCD'S IF NEEDED        0351.000
   00444   P001E0  000003B0    003B0    INCH.FP  GEN       8/0,24/A(IOSTAT-MTB)        STATUS ADDRESS           0352.000
   00445   P001E4  00000001                      GEN       16/0,16/1                   1 BYTE                   0353.000
   00446   P001E8  63000000             IOCD.SK1 GEN       8/X'63',24/0    SKIP ONE FILE                        0354.000
   00447   P001EC  00000001                      DATAW     1                                                    0355.000
   00448   P001F0  63000000             IOCD.SK2 GEN       8/X'63',24/0                                         0356.000
   00449   P001F4  00000001                      DATAW     1                                                    0357.000
  
   00451   P001F8  FCC10000             INT.RET  DI        X'18'           DISABLE THE INTERRUPT ON RETURN      0359.000
   00452   P001FC                       MTB1     EQU       $               INITIALIZE LOAD POINTER              0360.000
   00453   P001FC  C9850000                      CI        R3,0            WAS A LOAD BIAS SUPPLIED?            0361.000
   00454   P00200  F2000209    00208             BNE       $+2W-MTB        ..YES, SKIP NEXT INSTRUCTION         0362.000
   00455   P00204  AD800218    00218             LW        R3,DEFAULT-MTB  ELSE FORCE DEFULT LOAD POINT         0363.000
   00456   P00208  85800240    00240             ANMW      R3,WORDMASK-MTB MASK TO WORD BOUND                   0364.000
   00457   P0020C  D58006BC    006BC             STW       R3,BIAS-MTB     SAVE BIAS VALUE                      0365.000
   00458   P00210  EC000429    00428             BU        READPR-MTB      ..THEN READ PREAMBLE BLOCK 1         0366.000
  
   00460   P00214  0000FF00             SPAD.MSK DATAW     X'0000FF00'     MASK ALL BUT CHANNEL OF SPAD WORD    0368.000
   00461   P00218  00000800             DEFAULT  DATAW     X'800'          DEFAULT START LOAD POINT             0369.000
   00462   P0021C  0E670000             DEVTBL   DATAW     X'0E670000'     32/7X SPAD DEVICE WORD               0370.000
   00463   P00220  0E770000                      DATAW     X'0E770000'     32/27 SPAD DEVICE WORD               0371.000
   00464   P00224  00100000                      DATAW     X'00100000'     USE SPAD=CH10+SA0                    0372.000
   00465   P00228  000006D4             INTRTBL  DATAW     MT.ICB-MTB      32/7X SPAD INTR ENTRY                0373.000
   00466                                *                                  SETS TCW ADDR TO 77C                 0374.000
   00467   P0022C  00980000                      DATAW     X'00980000'     32/7X SPAD INTR LOC                  0375.000
   00468   P00230  00880000                      DATAW     X'00880000'     32/27 SPAD INTR LOC                  0376.000
   00469   P00234  00840000             IPL7X    DATAW     X'00840000'     32/7X IPL DEVICE SPAD LOC.           0377.000
   00470   P00238  00F40000             IPL27    DATAW     X'00F40000'     32/27 IPL DEVICE SPAD LOCATION       0378.000
   00471   P0023C  0E                   CLASS    DATAB     X'0E'           DEVICE CLASS                 EMST-35 0379.000
   00472   P0023D    00                 MACH     DATAB     0               SAVE AREA FOR MACHINE TYPE   EMST-35 0380.000
   00473   P00240  0007FFFC             WORDMASK DATAW     X'7FFFC'        WORD MASK                    EMST-35 0381.000
   00474   P00244                       ICLEND   EQU       $                                                    0382.000
   00475   P00244  00000000                      REZ       X'300'+MTB-ICLEND    PAD SO THAT READPR WILL         0383.000
           P00248  00000000
           P0024C  00000000
           P00250  00000000
           P00254  00000000
           P00258  00000000
           P0025C  00000000
           P00260  00000000
           P00264  00000000
           P00268  00000000
           P0026C  00000000
           P00270  00000000
           P00274  00000000
           P00278  00000000
           P0027C  00000000
           P00280  00000000
           P00284  00000000
           P00288  00000000
           P0028C  00000000
           P00290  00000000
           P00294  00000000
           P00298  00000000
           P0029C  00000000
           P002A0  00000000
           P002A4  00000000
           P002A8  00000000
           P002AC  00000000
           P002B0  00000000
           P002B4  00000000
           P002B8  00000000
           P002BC  00000000
           P002C0  00000000
           P002C4  00000000
           P002C8  00000000
           P002CC  00000000
           P002D0  00000000
           P002D4  00000000
           P002D8  00000000
           P002DC  00000000
           P002E0  00000000
           P002E4  00000000
           P002E8  00000000
           P002EC  00000000
           P002F0  00000000
           P002F4  00000000
           P002F8  00000000
           P002FC  00000000
   00476                                *                                 BEGIN AT X'300' LEAVING 000 TO 2FF    0384.000
   00477                                *                                       AS THE 192W MT BUFFER           0385.000
   00478   P00300                                BOUND     8W                                                   0386.000
   00479   P00300  00000000             SAVEREG  REZ       8W              REGISTER SAVE AREA                   0387.000
           P00304  00000000
           P00308  00000000
           P0030C  00000000
           P00310  00000000
           P00314  00000000
           P00318  00000000
           P0031C  00000000
   00480   P00320  00000000             INCH.BUF REZ       8W              EXT. I/O INCH BUFFER                 0388.000
           P00324  00000000
           P00328  00000000
           P0032C  00000000
           P00330  00000000
           P00334  00000000
           P00338  00000000
           P0033C  00000000
   00481   P00340  00000344    00344    SI.TRAP  ACW       SI.BLKA-MTB     SPAD TRAPS HERE..TO SI BLK           0389.000
   00482   P00344  00000000             SI.BLKA  REZ       2W                        XIO CONTROL BLK (OLD PSD)  0390.000
           P00348  00000000
   00483   P0034C  80000418    00418    NEW.PSD  GEN       8/X'80',24/W(HNDLR-MTB)   NEW PSD                    0391.000
   00484   P00350  00008000                      DATAW     X'00008000'               RETAIN CURRENT BLOCK MODE  0392.000
   00485   P00354  00000000                      REZ       2W                        IOCLA AND STATUS ADDRESS   0393.000
           P00358  00000000
   00486    00344                       SI.BLK   EQU       SI.BLKA-MTB     SI POINTER-CLASS F                   0394.000
   00487    00354                       IOCLA    EQU       SI.BLK+4W       IOCLA...REL TO SI BLK                0395.000
   00488    00358                       STAT.ADR EQU       SI.BLK+5W       STATUS POINTER                       0396.000
   00489   P0035C  00000000             ITBA     REZ       4*INTRLVL       INTRPT TBL FOR 32/27 BOOT            0397.000
           P00360  00000000
           P00364  00000000
           P00368  00000000
           P0036C  00000000
           P00370  00000000
           P00374  00000000
           P00378  00000000
   00490   P0037C  00000344    00344             ACW       SI.BLKA-MTB     POINTER TO SI BLOCK                  0398.000
   00491   P00380  00000000             NUMSKIP  DATAW     0               NO. OF FILES/BLKS SYSBUILD TO SKIP   0399.000
   00492   P00384  18000001    00001    MTHTCW   GEN       12/384,20/H(MTBUF)   HANDLER TCW                     0400.000
   00493                                *CH.SA    DATAH     X'1000'         LOG CH & PHYS SUBAD (INSRT) EMST-35 0401.000
   00494                                *MAXSECT  DATAH     0               MAX START SECT FOR FLOPPY   EMST-35 0402.000
   00495                                *CLASS    DATAB     X'0E'           DEVICE CLASS                EMST-35 0403.000
   00496                                *SDTFLG   DATAB     0               SDT LOAD FLAGS              EMST-35 0404.000
   00497                                *MACH     DATAB     0               SAVE AREA FOR MACHINE TYPE  EMST-35 0405.000
   00498    00000                       PHX      EQU       0               32/27 CPU                            0406.000
   00499    00001                       FLP      EQU       1               IPL FROM FLOPPY                      0407.000
   00500    00002                       MASTER   EQU       2               IPL FROM MASTER SDT                  0408.000
  
   00502                                *                                                                       0410.000
   00503                                *        IOCD CHAIN FOR 'F' CLASS TAPE                                  0411.000
   00504                                *                                                                       0412.000
   00505   P00388                                BOUND     1D                                                   0413.000
   00506   P00388  53000000             IOCD.BKR GEN       8/X'53',24/0    BKSR                                 0414.000
   00507   P0038C  40000001                      GEN       8/X'40',24/1    COM.CHAIN/1 BYTE                     0415.000
   00508   P00390  02000000             IOCD.RD  GEN       8/X'02',24/0    READ/ADDR X'0'                       0416.000
   00509   P00394  00000300                      GEN       16/0,16/X'300'  BYTE CNT (192W)                      0417.000
   00510                                *                                                                       0418.000
   00511                                *        IOCD CHAIN FOR IOP FLOPPY                                      0419.000
   00512                                *                                                                       0420.000
   00513   P00398  37000000             IOCD.ZR  GEN       8/X'37',24/0                   REZERO                0421.000
   00514   P0039C  40000000                      GEN       8/X'40',24/0                   COMMAND CHAIN, 0 BYTE 0422.000
   00515   P003A0  170003B8    003B8    IOCD.SK  GEN       8/X'17',24/A(SEEKINFO-MTB)     BINARY SEEK           0423.000
   00516   P003A4  40000004                      GEN       8/X'40',24/4                   COMMAND CHAIN, 4 BYTES0424.000
   00517   P003A8  02000000             IOCD.RD2 GEN       8/X'02',24/0                   READ, ADDRESS X'0'    0425.000
   00518   P003AC  00000300                      GEN       8/0,24/X'300'                  768 BYTES (192W)      0426.000
   00519   P003B0  00000000             IOSTAT   REZ       1D                             IO STATUS DBLWORD     0427.000
           P003B4  00000000
   00520   P003B8  00000006             SEEKINFO DATAW     6               SEEK INITIALIZED TO 6                0428.000
  
   00522                                *                                                                       0430.000
   00523                                *        ALL UNRECOVERABLE ERRORS DURING THE LOAD OPERATION             0431.000
   00524                                *        BRANCH AND LINK TO THIS POINT.                         EMST-35 0432.000
   00525                                *        R0 CONTAINS THE ABORT ADDRESS.                         EMST-35 0433.000
   00526                                *        R4,R5 CONTAIN BOOTSTRAP FAIL MESSAGE                   EMST-35 0434.000
   00527                                *        R6,R7 CONTAIN TYPE OF FAILURE                          EMST-35 0435.000
   00528                                *                                                                       0436.000
   00529   P003BC  AF0003EA    003E8    ERR.1    LD        R6,ERR1.MSG-MTB BAD LOAD MODULE              EMST-35 0437.000
   00530   P003C0  EC0003D5    003D4             BU        ERR.HALT-MTB                                 EMST-35 0438.000
   00531   P003C4  AF0006C2    006C0    ERR.2    LD        R6,ERR2.MSG-MTB CHECKSUM ERROR               EMST-35 0439.000
   00532   P003C8  EC0003D5    003D4             BU        ERR.HALT-MTB                                 EMST-35 0440.000
   00533   P003CC  AF0006CA    006C8    ERR.3    LD        R6,ERR3.MSG-MTB I/O STATUS ERROR             EMST-35 0441.000
   00534   P003D0  EC0003D5    003D4             BU        ERR.HALT-MTB                                 EMST-35 0442.000
   00535                                *BTERR1   EQU       $                                           EMST-35 0443.000
   00536   P003D4  AE0003E2    003E0    ERR.HALT LD        R4,BOOT.MSG-MTB  BOOTFAIL MESSAGE            EMST-35 0444.000
   00537   P003D8  0000                          HALT                      ERROR WHILE LOADING BOOTSTRAP        0445.000
   00538                                *        BU        BTERR1-MTB      NO RECOVERY ALLOWED          EMST-35 0446.000
   00539   P003DC  EC0003D5    003D4             BU        ERR.HALT-MTB    NO RECOVERY ALLOWED          EMST-35 0447.000
   00540   P003E0  424F4F54             BOOT.MSG DATAD     C'BOOTFAIL'                                  EMST-35 0448.000
           P003E4  4641494C
   00541   P003E8  4C4F4144             ERR1.MSG DATAD     C'LOAD MOD'                                  EMST-35 0449.000
           P003EC  204D4F44
   00543                                *                                                                       0451.000
   00544                                *        STATUS CHECK FOR 'F' CLASS I/O                                 0452.000
   00545                                *                                                                       0453.000
   00546   P003F0                       CK.STATF EQU       $                                                    0454.000
   00547   P003F0  AD000358    00358             LW        R2,STAT.ADR     STATUS CELLS LOC.                    0455.000
   00548   P003F4                       STATF.I  EQU       $               SPECIAL BOOT ENTRY                   0456.000
   00549   P003F4  CA00FFD3                      LI        R4,X'FFD3'      ALLOW CONT.END/CH END/DEVEND         0457.000
   00550   P003F8  B3400005    00004             LMH       R6,2H,X2        CK STATUS                            0458.000
   00551   P003FC  F2000403    00402             BNZ       STAT.RET-MTB    IF ERROR ... NORMAL RETURN           0459.000
   00552   P00400  2283                          ABR       R0,29           IF NO ERROR ... RETURN + 1W          0460.000
   00553   P00402      2800             STAT.RET TRSW      R0              RETURN TO CALLER                     0461.000
  
   00555                                *                                                                       0463.000
   00556                                *        'F' CLASS I/O ROUTINE                                          0464.000
   00557                                *                                                                       0465.000
   00558   P00404  AF8006D1    006D0    DO.SIO   LH        R7,CH.SA-MTB    LOGICAL CHANNEL/SUBCHANNEL ADDR      0466.000
   00559   P00408  FF970000                      SIO       R7,0            ISSUE I/O REQUEST                    0467.000
   00560   P0040C  F3000415    00414             BCF       6,WAIT-MTB      IF CC3/CC4 = 0 ... WAIT              0468.000
   00561                                *        BU        BTERR1-MTB      OTHERWISE ... ERROR          EMST-35 0469.000
   00562   P00410  F88003CD    003CC             BL        ERR.3-MTB       OTHERWISE ... ERROR          EMST-35 0470.000
   00563   P00414  0001                 WAIT     WAIT                      WAIT FOR INTERRUPT                   0471.000
   00564   P00416      2800                      TRSW      R0              RETURN TO CALLER                     0472.000
  
   00566                                *                                                                       0474.000
   00567                                *        'F' CLASS INTERRUPT HANDLER                                    0475.000
   00568                                *                                                                       0476.000
   00569   P00418                                BOUND     1W                                                   0477.000
   00570                                *HNDLR    LW        R7,DACI-MTB     FETCH DACI INSTRUCTION SHELLEMST-35 0478.000
   00571   P00418  AF8006D1    006D0    HNDLR    LH        R7,CH.SA-MTB    FETCH LOGICAL CHANNEL        EMST-35 0479.000
   00572                                *        LH        R6,CH.SA-MTB    FETCH LOGICAL CHANNEL        EMST-35 0480.000
   00573                                *        ORR       R6,R7           FORM DACI INSTRUCTION        EMST-35 0481.000
   00574                                *        STW       R7,DACI.EXE-MTB STORE IN-LINE                EMST-35 0482.000
   00575   P0041C  A3080347    00347             ABM       30,SI.BLK       BUMP RETURN ADDRESS BY 1H            0483.000
   00576   P00420  FFFF0000                      DACI      R7,0            DEACTIVATE CHANNEL INTERRUPT EMST-35 0484.000
   00577                                *DACI.EXE DATAW     0               DEACTIVATE CHANNEL INTERRUPTEMST-35 0485.000
   00578   P00424  F9800344    00344             LPSD      SI.BLK          I/O COMPLETED                        0486.000
   00579                                *DACI     DACI      0,0             DACI INSTRUCTION SHELL      EMST-35 0487.000
  
   00581                                *                                                                       0489.000
   00582                                *        READ LOAD MODULE PREAMBLE AND SAVE PARAMETERS                  0490.000
   00583                                *                                                                       0491.000
   00584   P00428                       READPR   EQU       $               READ LOAD MODULE PREAMBLE            0492.000
   00585                                *        ZMW       CBLKNUM-MTB     INIT CURR BLK # IN BUFFER    EMST-35 0493.000
   00586   P00428  F88005D1    005D0             BL        READBL-MTB      READ 1ST BLOCK OF PREAMBLE           0494.000
   00587   P0042C  AE800014    00014             LW        R5,MTBUF+PR.TRAN     TRANSFER ADDRESS                0495.000
   00588   P00430  D6800670    00670             STW       R5,TRAN-MTB                                          0496.000
  
   00590   P00434  AE800028    00028             LW        R5,MTBUF+PR.ORGC     CSECT LOAD ORIGIN + COMMON DELTA0498.000
   00591   P00438  D6800674    00674             STW       R5,ORGC-MTB                                          0499.000
  
   00593   P0043C  C900FFF9                      LI        R2,-7                SET UP LOOP COUNT               0501.000
   00594   P00440  0C90                          ZR        R1                   SET UP INDEX                    0502.000
   00595   P00442      0DB0                      ZR        R3              SET UP ANOTHER FOR LATER     EMST-35 0503.000
   00596   P00444                       PR.CSECT EQU       $                                                    0504.000
   00597   P00444  AEA00034    00034             LW        R5,MTBUF+PR.SFAC,R1  SAVE CSECT DATA START BLOCK #   0505.000
   00598   P00448  D6A00678    00678             STW       R5,SFAC-MTB,R1       TO DSECT LOAD ORIGIN + C DELTA  0506.000
   00599   P0044C  C8810004                      ADI       R1,1W                INCR INDEX                      0507.000
   00600   P00450  F5000445    00444             BIB       R2,PR.CSECT-MTB      LOOP TO GET ALL 7 PARAMETERS    0508.000
   00601   P00454  C900FFFA                      LI        R2,-6                SETUP NEXT LOOP COUNT           0509.000
   00602                                *        ZR        R1                   SETUP INDEX             EMST-35 0510.000
   00603   P00458                       PR.DSECT EQU       $                                                    0511.000
   00604                                *        LW        R5,MTBUF+PR.SFAD,R1  SAVE DSECT DAT ST BLK # EMST-35 0512.000
   00605   P00458  AEE00058    00058             LW        R5,MTBUF+PR.SFAD,R3  SAVE DSECT DAT ST BLK # EMST-35 0513.000
   00606                                *        STW       R5,SFAD-MTB,R1       TO DSECT RELOC CHECKSUM EMST-35 0514.000
   00607   P0045C  D6E00698    00698             STW       R5,SFAD-MTB,R3       TO DSECT RELOC CHECKSUM EMST-35 0515.000
   00608                                *        ADI       R1,1W                INCR INDEX              EMST-35 0516.000
   00609   P00460  C9810004                      ADI       R3,1W                INCR INDEX              EMST-35 0517.000
   00610   P00464  F5000459    00458             BIB       R2,PR.DSECT-MTB      LOOP TO GET ALL 6 PARAMETERS    0518.000
   00611                                *                                                                       0519.000
   00612                                *        SKIP RRS BLOCK                                                 0520.000
   00613                                *                                                                       0521.000
   00614   P00468  F88005D1    005D0             BL        READBL-MTB           READ RRS BLOCK AND IGNORE IT    0522.000
   00615                                *                                                                       0523.000
   00616                                *        LOAD CSECT DATA                                                0524.000
   00617                                *                                                                       0525.000
   00618   P0046C                       LDCSEC   EQU       $                                                    0526.000
   00619   P0046C  AD80067C    0067C             LW        R3,DBYTEC-MTB   GET CSECT DATA BYTE COUNT            0527.000
   00620   P00470  EE0004C5    004C4             BZ        LDDSEC-MTB      ..IF ZERO, GO LOAD DSECT             0528.000
   00621                                *        ABR       R3,31           ROUND UP TO NEAREST HALFWORD EMST-35 0529.000
   00622                                *        SRL       R3,1            CONVERT TO HALFWORD COUNT    EMST-35 0530.000
   00623   P00474  AD000674    00674             LW        R2,ORGC-MTB     GET CSECT RELATIVE LOAD POINT        0531.000
   00624                                *        ADMW      R2,BIAS-MTB     ADD BIAS TO ABSOLUTIZE LD PT EMST-35 0532.000
   00625                                *        STW       R2,LOADPT-MTB   SAVE LOAD POINT              EMST-35 0533.000
   00626                                *        LW        R7,CBLKNUM-MTB  GET CURRENT BLOCK NUMBER     EMST-35 0534.000
   00627   P00478  F8800559    00558             BL        SETUP1-MTB      COMMON SETUP ROUTINE         EMST-35 0535.000
   00628   P0047C  93800678    00678             CAMW      R7,SFAC-MTB     AND VALIDATE                         0536.000
   00629   P00480  EE000489    00488             BEQ       LDCSEC1-MTB     ..OK - CONTINUE                      0537.000
   00630                                *        HALT                      ELSE HALT - BAD LOAD MODULE  EMST-35 0538.000
   00631                                *        BU        $-1W-MTB        NO RECOVERY                  EMST-35 0539.000
   00632   P00484  F88003BD    003BC             BL        ERR.1-MTB       BAD LOAD MODULE              EMST-35 0540.000
   00633   P00488                       LDCSEC1  EQU       $                                                    0541.000
   00634                                *        ZMW       COMPCKSM-MTB    ZERO CHECKSUM ACCUMULATOR    EMST-35 0542.000
   00635                                *        BL        DATARD-MTB      READ & STORE CSECT DATA     ENBH-35  0543.000
   00636   P00488  F8800579    00578             BL        COMRD-MTB       READ & STORE CSECT DATA     ENBH-35  0544.000
   00637                                *        LW        R6,COMPCKSM-MTB GET COMPUTED CHECKSUM        EMST-35 0545.000
   00638   P0048C  93000680    00680             CAMW      R6,DCHECKC-MTB  AND VALIDATE AGAINST PREAMBLE CKSM   0546.000
   00639   P00490  EE000499    00498             BEQ       RELCSEC-MTB     ..OK - GO PROCESS RELOC MATR         0547.000
   00640                                *        HALT                      ELSE HALT - CSECT DATA CHECKSEMST-35 0548.000
   00641                                *        BU        $-1W-MTB        NO RECOVERY                  EMST-35 0549.000
   00642   P00494  F88003C5    003C4             BL        ERR.2-MTB       CHECKSUM ERROR               EMST-35 0550.000
   00643                                *                                                                       0551.000
   00644                                *        PROCESS CSECT RELOCATION MATRIX                                0552.000
   00645                                *                                                                       0553.000
   00646   P00498                       RELCSEC  EQU       $                                                    0554.000
   00647   P00498  AD800688    00688             LW        R3,RBYTEC-MTB   GET RELOCATION MATRIX BYTE COUNT     0555.000
   00648   P0049C  EE0004C5    004C4             BZ        LDDSEC-MTB      ..IF ZERO, GO LOAD DSECT             0556.000
   00649                                *        ABR       R3,31           ROUND UP TO NEAREST HALFWORD EMST-35 0557.000
   00650                                *        SRL       R3,1            CONVERT TO HALFWORD COUNT    EMST-35 0558.000
   00651   P004A0  AD0006B0    006B0             LW        R2,LOADPT-MTB   INITIALIZE R2 = LOAD POINT           0559.000
   00652                                *        LW        R7,CBLKNUM-MTB  GET CURR BLOCK NUMBER        EMST-35 0560.000
   00653   P004A4  F8800563    00562             BL        SETUP2-MTB      COMMON SETUP ROUTINE         EMST-35 0561.000
   00654   P004A8  93800684    00684             CAMW      R7,SFACR-MTB    AND VALIDATE                         0562.000
   00655   P004AC  EE0004B5    004B4             BEQ       RELCSEC1-MTB    ..OK - CONTINUE                      0563.000
   00656                                *        HALT                      ELSE HALT - BAD LOAD MODULE  EMST-35 0564.000
   00657                                *        BU        $-1W-MTB        NO RECOVERY                  EMST-35 0565.000
   00658   P004B0  F88003BD    003BC             BL        ERR.1-MTB       BAD LOAD MODULE              EMST-35 0566.000
   00659   P004B4                       RELCSEC1 EQU       $                                                    0567.000
   00660                                *        ZMW       COMPCKSM-MTB    ZERO CHECKSUM ACCUMULATOR    EMST-35 0568.000
   00661                                *        BL        RELOCRD-MTB     PROCESS RELOCTION MATRIX    ENBH-35  0569.000
   00662   P004B4  F8800579    00578             BL        COMRD-MTB       PROCESS RELOCTION MATRIX    ENBH-35  0570.000
   00663                                *        LW        R6,COMPCKSM-MTB GET COMPUTED CHECKSUM        EMST-35 0571.000
   00664   P004B8  9300068C    0068C             CAMW      R6,RCHECKC-MTB  VALIDATE RELOCATION CHECKSUM         0572.000
   00665   P004BC  EE0004C5    004C4             BEQ       LDDSEC-MTB      ..OK - GO PROCESS DSECT DATA         0573.000
   00666                                *        HALT                      ELSE HALT - DSECT REL CKSM EREMST-35 0574.000
   00667                                *        BU        $-1W-MTB        NO RECOVERY                  EMST-35 0575.000
   00668   P004C0  F88003C5    003C4             BL        ERR.2-MTB       CHECKSUM ERROR               EMST-35 0576.000
   00669                                *                                                                       0577.000
   00670                                *        LOAD DSECT DATA                                                0578.000
   00671                                *                                                                       0579.000
   00672   P004C4                       LDDSEC   EQU       $                                                    0580.000
   00673   P004C4  AD80069C    0069C             LW        R3,DBYTED-MTB   GET DSECT DATA BYTE COUNT            0581.000
   00674   P004C8  C9810300                      ADI       R3,192W         ADD A BLOCK FOR DEBUG BLOCK          0582.000
   00675                                *                                  NOT CHECKSUMMED                      0583.000
   00676   P004CC  EE000521    00520             BZ        LDDONE-MTB      ..IF ZERO,LOAD DONE, GO EXECUTE      0584.000
   00677                                *        ABR       R3,31           ROUND UP TO NEAREST HALFWORD EMST-35 0585.000
   00678                                *        SRL       R3,1            CONVERT TO HALFWORD COUNT    EMST-35 0586.000
   00679   P004D0  AD000690    00690             LW        R2,ORGD-MTB     GET DSECT RELATIVE LOAD POINT        0587.000
   00680                                *        ADMW      R2,BIAS-MTB     ADD BIAS TO ABSOLUTIZE LD PT EMST-35 0588.000
   00681                                *        STW       R2,LOADPT-MTB   SAVE LOAD POINT              EMST-35 0589.000
   00682                                *        LW        R7,CBLKNUM-MTB  GET CURRENT BLOCK NUMBER     EMST-35 0590.000
   00683   P004D4  F8800559    00558             BL        SETUP1-MTB      COMMON SETUP ROUTINE         EMST-35 0591.000
   00684   P004D8  93800698    00698             CAMW      R7,SFAD-MTB     VALIDATE AGAINST START BLOCK #       0592.000
   00685   P004DC  EE0004E5    004E4             BEQ       LDDSEC1-MTB     ..OK - CONTINUE                      0593.000
   00686                                *        HALT                      ELSE HALT - BAD LOAD MODULE  EMST-35 0594.000
   00687                                *        BU        $-1W-MTB        NO RECOVERY                  EMST-35 0595.000
   00688   P004E0  F88003BD    003BC             BL        ERR.1-MTB       BAD LOAD MODULE              EMST-35 0596.000
   00689   P004E4                       LDDSEC1  EQU       $                                                    0597.000
   00690                                *        ZMW       COMPCKSM-MTB    ZERO CHECKSUM ACCUMULATOR    EMST-35 0598.000
   00691                                *        BL        DATARD-MTB      READ & STORE DSECT DATA     ENBH-35  0599.000
   00692   P004E4  F8800579    00578             BL        COMRD-MTB       READ & STORE DSECT DATA     ENBH-35  0600.000
   00693                                *        LW        R6,COMPCKSM-MTB GET COMPUTED CHECKSUM        EMST-35 0601.000
   00694   P004E8  930006A0    006A0             CAMW      R6,DCHECKD-MTB  AND VALIDATE AGAINST PREAMBLE CKSM   0602.000
   00695   P004EC  EE0004F5    004F4             BEQ       RELDSEC-MTB     ..OK - GO PROCESS RELOC MATR         0603.000
   00696                                *        HALT                      ELSE HALT - DSECT DATA CHECKSEMST-35 0604.000
   00697                                *        BU        $-1W-MTB        NO RECOVERY                  EMST-35 0605.000
   00698   P004F0  F88003C5    003C4             BL        ERR.2-MTB       CHECKSUM ERROR               EMST-35 0606.000
   00699                                *                                                                       0607.000
   00700                                *        PROCESS DSECT RELOCATION MATRIX                                0608.000
   00701                                *                                                                       0609.000
   00702   P004F4                       RELDSEC  EQU       $                                                    0610.000
   00703   P004F4  AD8006A8    006A8             LW        R3,RBYTED-MTB   GET RELOCATION MATRIX BYTE COUNT     0611.000
   00704   P004F8  EE000521    00520             BZ        LDDONE-MTB      ..IF ZERO,DONE, GO EXECUTE           0612.000
   00705                                *        ABR       R3,31           ROUND UP TO NEAREST HALFWORD EMST-35 0613.000
   00706                                *        SRL       R3,1            CONVERT TO HALFWORD COUNT    EMST-35 0614.000
   00707   P004FC  AD0006B0    006B0             LW        R2,LOADPT-MTB   INITIALIZE R2=LOAD POINT             0615.000
   00708                                *        LW        R7,CBLKNUM-MTB  GET START BLOCK NUMBER       EMST-35 0616.000
   00709   P00500  F8800563    00562             BL        SETUP2-MTB      COMMON SETUP ROUTINE         EMST-35 0617.000
   00710   P00504  938006A4    006A4             CAMW      R7,SFADR-MTB    AND VALIDATE                         0618.000
   00711   P00508  EE000511    00510             BEQ       RELDSEC1-MTB    ..OK - CONTINUE                      0619.000
   00712                                *        HALT                      ELSE HALT - BAD LOAD MODULE  EMST-35 0620.000
   00713                                *        BU        $-1W-MTB        NO RECOVERY                  EMST-35 0621.000
   00714   P0050C  F88003BD    003BC             BL        ERR.1-MTB       BAD LOAD MODULE              EMST-35 0622.000
   00715   P00510                       RELDSEC1 EQU       $                                                    0623.000
   00716                                *        ZMW       COMPCKSM-MTB    ZERO CHECKSUM ACCUMULATOR    EMST-35 0624.000
   00717                                *        BL        RELOCRD-MTB     PROCESS RELOCATION MATRIX   ENBH-35  0625.000
   00718   P00510  F8800579    00578             BL        COMRD-MTB       PROCESS RELOCATION MATRIX   ENBH-35  0626.000
   00719                                *        LW        R6,COMPCKSM-MTB GET COMPUTED CHECKSUM        EMST-35 0627.000
   00720   P00514  930006AC    006AC             CAMW      R6,RCHECKD-MTB  VALIDATE RELOCATION CHECKSUM         0628.000
   00721   P00518  EE000521    00520             BEQ       LDDONE-MTB      ..OK - DONE, GO EXECUTE              0629.000
   00722                                *        HALT                      ELSE HALT - DSECT REL CKSM EREMST-35 0630.000
   00723                                *        BU        $-1W-MTB        NO RECOVERY                  EMST-35 0631.000
   00724   P0051C  F88003C5    003C4             BL        ERR.2-MTB       CHECKSUM ERROR               EMST-35 0632.000
   00725                                *                                                                       0633.000
   00726                                *        MODULE LOAD COMPLETE                                           0634.000
   00727                                *                                                                       0635.000
   00728   P00520                       LDDONE   EQU       $                                                    0636.000
   00729   P00520  A488066E    0066E             TBM       FLP,SDTFLG-MTB  IPL FROM FLOPPY?                     0637.000
   00730   P00524  F080053D    0053C             BNS       TPGO-MTB        IF NOT ... BRANCH                    0638.000
   00731   P00528  AF8006B8    006B8             LW        R7,CBLKNUM-MTB  GET NO. OF BLOCKS READ               0639.000
   00732   P0052C  EB800380    00380             ARMW      R7,NUMSKIP-MTB  ADD TO BLOCK COUNT FOR 'SYSINIT'     0640.000
   00733                                *        LW        R2,NUMSKIP-MTB  GET SKIP COUNT FOR SYSINIT  ENBH-35  0641.000
   00734   P00530  AC000380    00380             LW        R0,NUMSKIP-MTB  GET SKIP COUNT FOR SYSINIT  ENBH-35  0642.000
   00735   P00534  AF800570    00570             LW        R7,FL.CONST-MTB GET FLOPPY CONSTANT                  0643.000
   00736                                *        STW       R7,X'0'         PUT AT ZERO                  EMST-35 0644.000
   00737                                *        BU        *C.REGS         GO TO SYSINIT                EMST-35 0645.000
   00738   P00538  EC000541    00540             BU        LDGO-MTB        COMMON EXIT                  EMST-35 0646.000
   00739   P0053C  AF800574    00574    TPGO     LW        R7,TP.CONST-MTB GET TAPE STARTUP FLAG                0647.000
   00740                                *        STW       R7,X'0'         PUT AT ZERO                  EMST-35 0648.000
   00741   P00540  D7800000    00000    LDGO     STW       R7,X'0'         PUT AT ZERO                  EMST-35 0649.000
   00742   P00544  AC800D94    00D94             LW        R1,C.CTSAD      GET ADDR OF SYSINIT TSA     ENBH-35  0650.000
   00743   P00548  CC380864    00864             LFBR      B0,*T.MPXBR+3W,X1 LOAD THE BASE REGS        ENBH-35  0651.000
   00744   P0054C  ACA005B8    005B8             LW        R1,T.REGP,X1    GET THE GPR STACK POINTER   ENBH-35  0652.000
   00745   P00550  CD200008    00008             LF        R2,T.R2,X1      LOAD GPR'S 2 TO 7           ENBH-35  0653.000
   00746   P00554  F9A00020    00020             LPSD      T.PSD1,X1       HANDOFF TO SYSINIT          ENBH-35  0654.000
   00747                                *IGO     BU        *C.REGS         GO TO SYSINIT FOR INITIALIZATION     0655.000
   00748                                *                                                               EMST-35 0656.000
   00749                                *        COMMON SETUP ROUTINES                                  EMST-35 0657.000
   00750                                *                                                               EMST-35 0658.000
   00751   P00558  B90006BC    006BC    SETUP1   ADMW      R2,BIAS-MTB     ADD BIAS TO ABSOLUTIZE LD PT EMST-35 0659.000
   00752   P0055C  D50006B0    006B0             STW       R2,LOADPT-MTB   SAVE LOAD POINT              EMST-35 0660.000
   00753   P00560  18A0                          SBR       R2,1            FLAG FOR DATA READ          ENBH-35  0661.000
   00754   P00562      23B3             SETUP2   ABR       R3,31           ROUND UP TO NEAREST HALFWORD EMST-35 0662.000
   00755                                *        SRL       R3,1            CONVERT TO HW COUNT EMST-35 ENBH-35  0663.000
   00756   P00564  AF8006B8    006B8             LW        R7,CBLKNUM-MTB  GET CURRENT BLOCK NUMBER     EMST-35 0664.000
   00757   P00568  F80006B4    006B4             ZMW       COMPCKSM-MTB    ZERO CHECKSUM ACCUMULATOR    EMST-35 0665.000
   00758   P0056C  7181                          SRL       R3,1            CONVERT TO HW COUNT EMST-35 ENBH-35  0666.000
   00759   P0056E      2800                      TRSW      R0              GO BACK TO CALLER            EMST-35 0667.000
  
   00761   P00570  464C4F50             FL.CONST DATAW     C'FLOP'                                              0669.000
   00762   P00574  54415045             TP.CONST DATAW     C'TAPE'                                              0670.000
   00764                                *                                                              ENBH-35  0672.000
   00765                                *                  DATARD - READS BLOCK OF DATA & LOADS IN MEMOENBH-35  0673.000
   00766                                *                                                              ENBH-35  0674.000
   00767                                *                    ENTRY:                                    ENBH-35  0675.000
   00768                                *                      R2 - LOAD POINTER                       ENBH-35  0676.000
   00769                                *                      R3 - HALFWORD COUNT                     ENBH-35  0677.000
   00770                                *                    EXIT:                                     ENBH-35  0678.000
   00771                                *                      COMPUTED CHECKSUM IN COMPCKSM           ENBH-35  0679.000
   00772                                *                          AND IN R6                   EMST-35 ENBH-35  0680.000
   00773                                *        SPACE                                                 ENBH-35  0681.000
   00774                                *ATARD   EQU       $                                           ENBH-35  0682.000
   00775                                *        TRR       R0,R5            SAVE RETURN REGISTER       ENBH-35  0683.000
   00776                                *        TRN       R3,R3            NEGATE # HALFWORDS OF DATA ENBH-35  0684.000
   00777                                *R1      EQU       $                READ 192W AT A TIME        ENBH-35  0685.000
   00778                                *        BL        READBL-MTB FETCH NEXT BLOCK OF DATA TO MTBUFENBH-35  0686.000
   00779                                *        LI        R1,-384H         SETUP LOOP COUNTER         ENBH-35  0687.000
   00780                                *R2      EQU       $                                           ENBH-35  0688.000
   00781                                *        LH        R6,MTBUF+384H,R1 GET NEXT HWORD OF DATA     ENBH-35  0689.000
   00782                                *        ARMW      R6,COMPCKSM-MTB  ADD TO CHECKSUM ACCUMULATORENBH-35  0690.000
   00783                                *        STH       R6,0,R2          STORE HALFWORD IN MEMORY   ENBH-35  0691.000
   00784                                *        ADI       R2,1H            INCREMENT LOAD POINTER     ENBH-35  0692.000
   00785                                *        BIB       R3,$+2W-MTB INCR HW CNT-IF NOT DONE,SKIP    ENBH-35  0693.000
   00786                                *        BU        DRXIT-MTB        ..ALL DONE - EXIT  EMST-35 ENBH-35  0694.000
   00787                                *        BU        COMXIT-MTB       ..ALL DONE - EXIT  EMST-35 ENBH-35  0695.000
   00788                                *        BIH       R1,DR2-MTB       IF NOT DONE, GET NEXT HWORDENBH-35  0696.000
   00789                                *        BU        DR1-MTB          ELSE SETUP TO READ NEXT BLKENBH-35  0697.000
   00790                                *DRXIT    EQU       $                DATA ALL LOADED   EMST-35 ENBH-35  0698.000
   00791                                *        TRR       R5,R0            RESTORE RETURN REG EMST-35 ENBH-35  0699.000
   00792                                *        TRSW      R0               RETURN             EMST-35 ENBH-35  0700.000
   00793                                *        PAGE                                                  ENBH-35  0701.000
   00794                                *                                                              ENBH-35  0702.000
   00795                                *                  RELOCRD - READ & PROCESS RELOCATION MATRIX  ENBH-35  0703.000
   00796                                *                                                              ENBH-35  0704.000
   00797                                *                    ENTRY:                                    ENBH-35  0705.000
   00798                                *                      R2 - LOAD POINTER                       ENBH-35  0706.000
   00799                                *                      R3 - HALFWORD COUNT OF RELOCATION DATA  ENBH-35  0707.000
   00800                                *                    EXIT:                                     ENBH-35  0708.000
   00801                                *                      COMPUTED CHECKSUM IN COMPCKSM           ENBH-35  0709.000
   00802                                *                                                              ENBH-35  0710.000
   00803                                *        SPACE                                                 ENBH-35  0711.000
   00804                                *ELOCRD  EQU       $                                           ENBH-35  0712.000
   00805                                *        TRR       R0,R5            SAVE RETURN REGISTER       ENBH-35  0713.000
   00806                                *        TRN       R3,R3            NEGATE # HWORDS OF RELOC   ENBH-35  0714.000
   00807                                *        LW        R4,LOADPT-MTB    SET R4 = RELOCATION BIAS   ENBH-35  0715.000
   00808                                *EL1     EQU       $                                           ENBH-35  0716.000
   00809                                *        BL        READBL-MTB       RD NXT REL BLK INTO MTBUF  ENBH-35  0717.000
   00810                                *        LI        R1,-384H         SETUP LOOP COUNTER         ENBH-35  0718.000
   00811                                *EL2     EQU       $                                           ENBH-35  0719.000
   00812                                *        LI        R7,-16           SETUP FOR 16 BITS          ENBH-35  0720.000
   00813                                *        LH        R6,MTBUF+384H,R1 GET NEXT HW OF RELOC MATRIXENBH-35  0721.000
   00814                                *        ARMW      R6,COMPCKSM-MTB  ADD TO CHECKSUM ACCUMULATORENBH-35  0722.000
   00815                                *EL3     EQU       $                                           ENBH-35  0723.000
   00816                                *        TBR       R6,16            BIT SET?                   ENBH-35  0724.000
   00817                                *        BNS       $+2W-MTB        NO - SKIP NEXT INSTRUCTION  ENBH-35  0725.000
   00818                                *        ARMW      R4,0,R2         ADD BIAS TO MEMORY LOCATION ENBH-35  0726.000
   00819                                *        ADI       R2,1W           INCREMENT LOAD POINTER      ENBH-35  0727.000
   00820                                *        SLL       R6,1            SHIFT RELOC WORD 1 BIT LEFT ENBH-35  0728.000
   00821                                *        BIB       R7,REL3-MTB     IF NOT DONE, TEST NEXT BIT  ENBH-35  0729.000
   00822                                *        BIB       R3,$+2W-MTB     INC HW CNT-IF NOT DONE,SKIP ENBH-35  0730.000
   00823                                *        BU        RELXIT-MTB      ..ALL DONE - EXIT   EMST-35 ENBH-35  0731.000
   00824                                *        BU        COMXIT-MTB      ..ALL DONE - EXIT   EMST-35 ENBH-35  0732.000
   00825                                *        BIH       R1,REL2-MTB     IF NOT DONE, GET NEXT HWORD ENBH-35  0733.000
   00826                                *        BU        REL1-MTB        ELSE READ NXT BLK OF RELOC  ENBH-35  0734.000
   00827                                *RELXIT   EQU       $                                  EMST-35 ENBH-35  0735.000
   00828                                *        TRR       R5,R0           RESTORE RETURN REG  EMST-35 ENBH-35  0736.000
   00829                                *        TRSW      R0              RETURN              EMST-35 ENBH-35  0737.000
   00830                                *        SPACE                                                 ENBH-35  0738.000
   00831                                *                                                              ENBH-35  0739.000
   00832                                *        COMMON EXIT FROM READ SUBROUTINES             EMST-35 ENBH-35  0740.000
   00833                                *                                                              ENBH-35  0741.000
   00834                                *OMXIT   LW        R6,COMPCKSM-MTB GET COMPUTED CHECKSUEMST-35 ENBH-35  0742.000
   00835                                *        TRR       R5,R0           RESTORE RETURN REGISEMST-35 ENBH-35  0743.000
   00836                                *        TRSW      R0              RETURN              EMST-35 ENBH-35  0744.000
   00838                                *                                                              ENBH-35  0746.000
   00839                                *        COMMON READ SUBROUTINE FOR DATA AND RELOCATION MATRIX ENBH-35  0747.000
   00840                                *                                                              ENBH-35  0748.000
   00841                                *        INPUTS:   R2 = LOAD POINTER                           ENBH-35  0749.000
   00842                                *                  R3 = HALFWORD COUNT                         ENBH-35  0750.000
   00843                                *        OUTPUT:   COMPUTED CHECKSUM IN COMPCKSM               ENBH-35  0751.000
   00844                                *                  AND IN R6                                   ENBH-35  0752.000
   00845                                *                                                              ENBH-35  0753.000
   00846   P00578                       COMRD    CEQU      $                                           ENBH-35  0754.000
   00847   P00578  2E80                          TRR       R0,R5           SAVE RETURN ADDR            ENBH-35  0755.000
   00848   P0057A      2DB4                      TRN       R3,R3           NEGATE # HW EXPECTED        ENBH-35  0756.000
   00849   P0057C  AE0006B0    006B0             LW        R4,LOADPT-MTB   SET R4=RELOC BIAS           ENBH-35  0757.000
   00850   P00580                       RD1      CEQU      $                                           ENBH-35  0758.000
   00851   P00580  F88005D1    005D0             BL        READBL-MTB      READ BLOCK INTO MTBUF       ENBH-35  0759.000
   00852   P00584  C880FD00                      LI        R1,-384H        LOOP COUNTER (192 W)        ENBH-35  0760.000
   00853   P00588                       RD2      CEQU      $                                           ENBH-35  0761.000
   00854   P00588  AF200301    00300             LH        R6,MTBUF+384H,R1 GET HW FROM BUFFER         ENBH-35  0762.000
   00855   P0058C  EB0006B4    006B4             ARMW      R6,COMPCKSM-MTB  ADD TO CHECKSUM            ENBH-35  0763.000
   00856   P00590  24A0                          TBR       R2,1            IS THIS A DATA READ?        ENBH-35  0764.000
   00857   P00594  F08005A5    005A4             BNS       RELRD-MTB       RD & PROC RELOC MATRIX      ENBH-35  0765.000
   00858   P00598  D7400001    00000    DATRD    STH       R6,0,R2         STORE HW IN MEMORY          ENBH-35  0766.000
   00859   P0059C  C9010002                      ADI       R2,1H           INCREMENT LOAD PTR          ENBH-35  0767.000
   00860   P005A0  EC0005BD    005BC             BU        COMRD2-MTB      JOIN COMMON CODE            ENBH-35  0768.000
   00861   P005A4                       RELRD    CEQU      $                                           ENBH-35  0769.000
   00862   P005A4  CB80FFF0                      LI        R7,-16          SETUP FOR 16 BITS           ENBH-35  0770.000
   00863   P005A8                       RELRD2   CEQU      $                                           ENBH-35  0771.000
   00864   P005A8  2462                          TBR       R6,16           IS BIT 16 SET?              ENBH-35  0772.000
   00865   P005AA      7341                      SLL       R6,1            SHIFT FOR NEXT LOOP         ENBH-35  0773.000
   00866   P005AC  F08005B5    005B4             BNS       SKIPADD-MTB     NO, DON'T ADD BIAS          ENBH-35  0774.000
   00867   P005B0  EA400000    00000             ARMW      R4,0,R2         ADD BIAS TO MEMORY LOC      ENBH-35  0775.000
   00868   P005B4  C9010004             SKIPADD  ADI       R2,1W           INCREMENT WORD COUNT        ENBH-35  0776.000
   00869   P005B8  F78005A9    005A8             BIB       R7,RELRD2-MTB   TEST NEXT BIT IN HW         ENBH-35  0777.000
   00870   P005BC                       COMRD2   CEQU      $                                           ENBH-35  0778.000
   00871   P005BC  F58005C9    005C8             BIB       R3,NEXTHW-MTB   INCR HW COUNT               ENBH-35  0779.000
   00872   P005C0  AF0006B4    006B4             LW        R6,COMPCKSM-MTB EXITING - GET CKSUM         ENBH-35  0780.000
   00873   P005C4  2C50                          TRR       R5,R0           EXITING - GET RET ADDR      ENBH-35  0781.000
   00874   P005C6      2800                      TRSW      R0              GO BACK TO CALLER           ENBH-35  0782.000
   00875   P005C8  F4A00589    00588    NEXTHW   BIH       R1,RD2-MTB      GET ANOTHER HW              ENBH-35  0783.000
   00876   P005CC  EC000581    00580             BU        RD1-MTB         GET ANOTHER BLOCK           ENBH-35  0784.000
   00877                                *                                                                       0785.000
   00878                                *        READBL - READ NEXT BLOCK INTO MT BUFFER                        0786.000
   00879                                *                                                                       0787.000
  
   00881   P005D0                       READBL   EQU       $                                                    0789.000
   00882   P005D0  A38806BB    006BB             ABM       31,CBLKNUM-MTB       INCREMENT CURRENT BLOCK NUMBER  0790.000
   00883                                *         TBM       EORFFLG,CLASS-MTB    F CLASS?               EMST-35 0791.000
   00884                                *         BCT       SET,READBLF-MTB      IF SO ... READ VIA XIO EMST-35 0792.000
   00885                                *MTH      EQU       $                    MAG TAPE HANDLER       EMST-35 0793.000
   00886                                *         LW        R1,MTHTCW-MTB        GET HANDLER TCW        EMST-35 0794.000
   00887                                *         STW       R1,MTTCW-MTB         STORE IN TCW LOCATION  EMST-35 0795.000
   00888                                *CDREAD   CD        MT,MTREAD            READ A BLOCK           EMST-35 0796.000
   00889                                *TDREAD   TD        MT,X'8000'           DONE?                  EMST-35 0797.000
   00890                                *         BCT       DELAY,TDREAD-MTB     NO - WAIT              EMST-35 0798.000
   00891                                *         BCT       ACTIVE,TDREAD-MTB    NO - WAIT              EMST-35 0799.000
   00892                                *         BCT       ANY,RETRY-MTB        ANY ERRORS?  YES - RETREMST-35 0800.000
   00893                                *         TRSW      R0                   ELSE RETURN            EMST-35 0801.000
  
   00895   P005D4                       READBLF  EQU       $                    'F' CLASS READ                  0803.000
   00896   P005D4  DC000300    00300             STF       R0,SAVEREG-MTB       SAVE REGS                       0804.000
   00897   P005D8  A488066E    0066E             TBM       FLP,SDTFLG-MTB       IPL FROM FLOPPY?                0805.000
   00898   P005DC  F080063D    0063C             BNS       RD.TAPE-MTB          IF NOT ... READ FROM TAPE       0806.000
   00899   P005E0  AE0003BB    003BA             LH        R4,SEEKINFO+1H-MTB   FETCH LAST SECTOR ADDR.         0807.000
   00900   P005E4  CA010003                      ADI       R4,3                 3 SECTOR INCREMENTS (192W)      0808.000
   00901   P005E8  920006D3    006D2             CAMH      R4,MAXSECT-MTB       IS SECTOR NO. TOO LARGE?        0809.000
   00902   P005EC  EF0005F5    005F4             BLE       $+2W-MTB             IF NOT ... CONTINUE             0810.000
   00903                                *        BL        BTERR1-MTB      ERROR...HALT, R0=ABORT ADDR  EMST-35 0811.000
   00904   P005F0  F88003CD    003CC             BL        ERR.3-MTB       ERROR...HALT                 EMST-35 0812.000
   00905   P005F4  D60003BB    003BA             STH       R4,SEEKINFO+1H-MTB   UPDATE SEEK DATA                0813.000
   00906   P005F8  C980FFFE                      LI        R3,-2                TWO RETRY CYCLES                0814.000
   00907   P005FC  CA80FFFB             RTRY1    LI        R5,RCNT              RETRY 'RCNT' TIMES              0815.000
   00908   P00600  F8800405    00404    FLP.LOOP BL        DO.SIO-MTB           READ BLOCK                      0816.000
   00909   P00604  F88003F1    003F0             BL        CK.STATF-MTB         CHECK STATUS                    0817.000
   00910   P00608  EC000611    00610             BU        $+2W-MTB             IF ERROR ... ATTEMPT TO RETRY   0818.000
   00911   P0060C  EC000669    00668             BU        OK.TOGO-MTB          READ SUCCESSFUL ... RETURN      0819.000
   00912   P00610  F6800601    00600             BIB       R5,FLP.LOOP-MTB      RETRY 'RCNT' TIMES              0820.000
   00913   P00614  F580061D    0061C             BIB       R3,$+2W-MTB          HAVE WE REZEROED?               0821.000
   00914                                *        BL        BTERR1-MTB      ERROR...HALT, R0=ABORT ADDR  EMST-35 0822.000
   00915   P00618  F88003CD    003CC             BL        ERR.3-MTB       ERROR...HALT, R0=ABORT ADDR  EMST-35 0823.000
   00916   P0061C  36800398    00398             LA        R5,IOCD.ZR-MTB       FETCH ADDR. OF REZERO IOCD      0824.000
   00917   P00620  D6800354    00354             STW       R5,IOCLA             STORE IT IN CONTROL BLOCK       0825.000
   00918   P00624  F8800405    00404             BL        DO.SIO-MTB           REZERO HEADS                    0826.000
   00919   P00628  F88003F1    003F0             BL        CK.STATF-MTB         CHECK STATUS                    0827.000
   00920                                *        BL        BTERR1-MTB      ERROR...HALT, R0=ABORT ADDR  EMST-35 0828.000
   00921   P0062C  F88003CD    003CC             BL        ERR.3-MTB       ERROR...HALT, R0=ABORT ADDR  EMST-35 0829.000
   00922   P00630  368003A0    003A0             LA        R5,IOCD.SK-MTB       FETCH ADDR OF SEEK CD           0830.000
   00923   P00634  D6800354    00354             STW       R5,IOCLA             RESTORE IT IN CONTROL BLOCK     0831.000
   00924   P00638  EC0005FD    005FC             BU        RTRY1-MTB            RETRY ONE MORE CYCLE            0832.000
   00925                                *                                                                       0833.000
   00926   P0063C  CA80FFFB             RD.TAPE  LI        R5,RCNT              LOAD RETRY COUNT                0834.000
   00927   P00640  F8800405    00404             BL        DO.SIO-MTB           READ BLOCK                      0835.000
   00928   P00644  F88003F1    003F0    TAP.LOOP BL        CK.STATF-MTB         CHECK STATUS                    0836.000
   00929   P00648  EC000651    00650             BU        $+2W-MTB             ERROR ... ATTEMPT TO RETRY      0837.000
   00930   P0064C  EC000669    00668             BU        OK.TOGO-MTB          SUCCESSFUL READ ... RETURN      0838.000
   00931   P00650  37800388    00388             LA        R7,IOCD.BKR-MTB      FETCH BACKRECORD IOCD ADDR      0839.000
   00932   P00654  D7800354    00354             STW       R7,IOCLA             STORE IT IN CONTROL BLOCK       0840.000
   00933   P00658  F8800405    00404             BL        DO.SIO-MTB           BACKSPACE AND READ              0841.000
   00934   P0065C  37800390    00390             LA        R7,IOCD.RD-MTB       FETCH ADDR OF READ IOCD         0842.000
   00935   P00660  D7800354    00354             STW       R7,IOCLA             RESTORE IT IN CONTROL BLOCK     0843.000
   00936   P00664  F6800645    00644             BIB       R5,TAP.LOOP-MTB      RETRY 'RCNT' TIMES              0844.000
   00937   P00668                       OK.TOGO  EQU       $                    NORMAL RETURN SEQUENCE          0845.000
   00938   P00668  CC000300    00300             LF        R0,SAVEREG-MTB       RESTORE REGS                    0846.000
   00939   P0066C  2800                          TRSW      R0                   RETURN TO CALLER                0847.000
   00940                                *                                                                       0848.000
   00941                                *        RETRY FOR 'E' CLASS I/O                                        0849.000
   00942                                *                                                                       0850.000
   00943                                *         BOUND     1W                                          EMST-35 0851.000
   00944                                *RETRY    HALT                                                  EMST-35 0852.000
   00945                                *CDBKSPC  CD        MT,BKSPCREC     BACKSPACE RECORD            EMST-35 0853.000
   00946                                *TDBKSPC  TD        MT,X'8000'      DONE?                       EMST-35 0854.000
   00947                                *         BCT       DELAY,TDBKSPC-MTB    NO - WAIT              EMST-35 0855.000
   00948                                *RETRY1   TD        MT,X'2000'                                  EMST-35 0856.000
   00949                                *         TBM       13,0            BACKSPACE IN PROGRESS?      EMST-35 0857.000
   00950                                *         BS        RETRY1-MTB      YES - TEST AGAIN            EMST-35 0858.000
   00951                                *         TD        MT,X'8000'      DONE?                       EMST-35 0859.000
   00952                                *         BCF       ANY,MTH-MTB     RE-READ BLOCK IF NO ERROR ONEMST-35 0860.000
   00953                                *RETRY2   HALT                      ELSE HALT                   EMST-35 0861.000
   00954                                *         BU        RETRY2-MTB      NO RECOVERY                 EMST-35 0862.000
   00955                                *SDTEND   EQU       $                    END OF SDT LOADER      EMST-35 0863.000
   00956                                *         REZ       X'728'+MTB-SDTEND    ZERO FILL TO X'728' FOREMST-35 0864.000
   00957                                *                                        IOCD                   EMST-35 0865.000
   00959                                *                                                                       0867.000
   00960                                *        LOCAL BOOTSTRAP STORAGE                                        0868.000
   00961                                *                                                                       0869.000
  
   00963   P0066E      00               SDTFLG   DATAB     0               SDT LOAD FLAGS               EMST-35 0871.000
   00964   P00670                                BOUND     1D                                                   0872.000
   00965   P00670  00000000             TRAN     DATAW     0               TRANSFER ADDRESS                     0873.000
   00966   P00674  00000000             ORGC     DATAW     0               CSECT LOAD ORIGIN + COMMON DELTA     0874.000
   00967   P00678  00000000             SFAC     DATAW     0               START CSECT DATA BLOCK #             0875.000
   00968   P0067C  00000000             DBYTEC   DATAW     0               CSECT DATA BYTE COUNT                0876.000
   00969   P00680  00000000             DCHECKC  DATAW     0               CSECT DATA CHECKSUM                  0877.000
   00970   P00684  00000000             SFACR    DATAW     0               START CSECT REL MATRIX BLOCK #       0878.000
   00971   P00688  00000000             RBYTEC   DATAW     0               CSECT REL BYTE COUNT                 0879.000
   00972   P0068C  00000000             RCHECKC  DATAW     0               CSECT REL CHECKSUM                   0880.000
   00973   P00690  00000000             ORGD     DATAW     0               DSECT LOAD ORIGIN + COMMON DELTA     0881.000
   00974   P00694  00000000             MTTCW    DATAW     0               MAG TAPE TCW LOCATION                0882.000
   00975   P00698  00000000             SFAD     DATAW     0               START DSECT DATA BLOCK #             0883.000
   00976   P0069C  00000000             DBYTED   DATAW     0               DSECT DATA BYTE COUNT                0884.000
   00977   P006A0  00000000             DCHECKD  DATAW     0               DSECT DATA CHECKSUM                  0885.000
   00978   P006A4  00000000             SFADR    DATAW     0               START DSECT REL MATRIX BLOCK #       0886.000
   00979   P006A8  00000000             RBYTED   DATAW     0               DSECT REL BYTE COUNT                 0887.000
   00980   P006AC  00000000             RCHECKD  DATAW     0               DSECT REL CHECKSUM                   0888.000
   00981   P006B0  00000000             LOADPT   DATAW     0               MEMORY LOAD POINTER                  0889.000
   00982   P006B4  00000000             COMPCKSM DATAW     0               CHECKSUM ACCUMULATOR                 0890.000
   00983   P006B8  00000000             CBLKNUM  DATAW     0               CURRENT BLOCK NUMBER                 0891.000
   00984                                *WORDMASK DATAW     X'7FFFC'        WORD MASK                   EMST-35 0892.000
   00985   P006BC  00000000             BIAS     DATAW     0               BIAS SUPPLIED IN R3 AT IPL           0893.000
   00986   P006C0  43484543             ERR2.MSG DATAD     C'CHECKSUM'                                  EMST-35 0894.000
           P006C4  4B53554D
   00987   P006C8  494F2045             ERR3.MSG DATAD     C'IO ERROR'                                  EMST-35 0895.000
           P006CC  52524F52
   00988   P006D0  1000                 CH.SA    DATAH     X'1000'         LOG CH & PHYS SUBAD (INSRT)  EMST-35 0896.000
   00989   P006D2      0000             MAXSECT  DATAH     0               MAX START SECT FOR FLOPPY    EMST-35 0897.000
   00990   P006D4                                ORG       MTTCW+X'40'     SI LOC IS 40B FROM TCW               0898.000
   00991   P006D4  000006D4             MT.ICB   DATAW     $-MTB           ICB POINTS TO ITSELF                 0899.000
   00992   P006D8  00000000                      DATAW     0               OLD PSD2                             0900.000
   00993   P006DC  800001F8    001F8             GEN       1/1,31/A(INT.RET-MTB)   NEW PSD                      0901.000
   00994   P006E0  00004000                      DATAW     X'4000'         INTERRUPTS BLOCKED                   0902.000
   00995   P006E4  00000000                      DATAW     0,0             REST OF ICB                          0903.000
           P006E8  00000000
   00996   P006EC                       BOOTEND  EQU       $                                                    0904.000
   00997    006EC                       BOOTLEN  EQU       BOOTEND-MTB     # OF BYTES IN BOOTSTRAP LOADER       0905.000
   00998                                *                                                                       0906.000
   00999                                *                  END OF BOOTSTRAP                                     0907.000
   01000                                *                                                                       0908.000
   01001   P006EC  000006EC             BOOTL    DATAW     BOOTLEN         # BYTES IN BOOTSTRAP                 0909.000
   01002   P006F0                                END                                                            0910.000
*  0000  ERRORS IN FUPBOOT
