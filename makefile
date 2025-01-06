#
# This GNU make makefile has been tested on:
#   Linux (x86 & Sparc & PPC)
#   Android (Termux)
#   OS X
#   Solaris (x86 & Sparc) (gcc and Sun C)
#   OpenBSD
#   NetBSD
#   FreeBSD
#   HP-UX
#   AIX
#   Windows (MinGW & cygwin)
#   Linux x86 targeting Android (using agcc script)
#   Haiku x86 (with gcc4)
#
# Android targeted builds should invoke GNU make with GCC=agcc on
# the command line.
#
# In general, the logic below will detect and build with the available
# features which the host build environment provides.
#
# Dynamic loading of libpcap is the preferred default behavior if pcap.h 
# is available at build time.  Support to statically linking against libpcap
# is deprecated and may be removed in the future.  Static linking against 
# libpcap can be enabled if GNU make is invoked with USE_NETWORK=1 on the 
# command line.
#
# Some platforms may not have vendor supplied libpcap available.  HP-UX is 
# one such example.  The packages which are available for this platform
# install include files and libraries in user specified directories.  In 
# order for this makefile to locate where these components may have been 
# installed, gmake should be invoked with LPATH=/usr/lib:/usr/local/lib 
# defined (adjusted as needed depending on where they may be installed).
#
# In the unlikely event that someone wants to build network capable 
# simulators without networking support, invoking GNU make with 
# NONETWORK=1 on the command line will do the trick.
#
# By default, video support is enabled if the SDL2 development
# headers and libraries are available.  To force a build without video
# support, invoke GNU make with NOVIDEO=1 on the command line.
#
# The default build will build compiler optimized binaries.
# If debugging is desired, then GNU make can be invoked with
# DEBUG=1 on the command line.
#
# When building compiler optimized binaries with the gcc or clang 
# compilers, invoking GNU make with LTO=1 on the command line will 
# cause the build to use Link Time Optimization to maximally optimize 
# the results.  Link Time Optimization can report errors which aren't 
# otherwise detected and will also take significantly longer to 
# complete.  Additionally, non debug builds default to build with an
# optimization level of -O2.  This optimization level can be changed 
# by invoking GNU OPTIMIZE=-O3 (or whatever optimize value you want) 
# on the command line if desired.
#
# The default setup will fail simulator build(s) if the compile 
# produces any warnings.  These should be cleaned up before new 
# or changd code is accepted into the code base.  This option 
# can be overridden if GNU make is invoked with WARNINGS=ALLOWED
# on the command line.
#
# The default build will run per simulator tests if they are 
# available.  If building without running tests is desired, 
# then GNU make should be invoked with TESTS=0 on the command 
# line.
#
# Default test execution will produce summary output.  Detailed
# test output can be produced if GNU make is invoked with 
# TEST_ARG=-v on the command line.
#
# simh project support is provided for simulators that are built with 
# dependent packages provided with the or by the operating system 
# distribution OR for platforms where that isn't directly available 
# (OS X/macOS) by packages from specific package management systems (MacPorts 
# or Homebrew).  Users wanting to build simulators with locally built 
# dependent packages or packages provided by an unsupported package 
# management system may be able to override where this procedure looks 
# for include files and/or libraries.  Overrides can be specified by define 
# exported environment variables or GNU make command line arguments which 
# specify INCLUDES and/or LIBRARIES.  
# Each of these, if specified, must be the complete list include directories
# or library directories that should be used with each element separated by 
# colons. (i.e. INCLUDES=/usr/include/:/usr/local/include/:...)
# If this doesn't work for you and/or you're interested in using a different 
# ToolChain, you're free to solve this problem on your own.  Good Luck.
#
# Some environments may have the LLVM (clang) compiler installed as
# an alternate to gcc.  If you want to build with the clang compiler, 
# invoke make with GCC=clang.
#
# Internal ROM support can be disabled if GNU make is invoked with
# DONT_USE_ROMS=1 on the command line.
#
# For linting (or other code analyzers) make may be invoked similar to:
#
#   make GCC=cppcheck CC_OUTSPEC= LDFLAGS= CFLAGS_G="--enable=all --template=gcc" CC_STD=--std=c99
#
# CC Command (and platform available options).  (Poor man's autoconf)
#

OS_CCDEFS=
AIO_CCDEFS=

ifneq (,${GREP_OPTIONS})
  $(info GREP_OPTIONS is defined in your environment.)
  $(info )
  $(info This variable interferes with the proper operation of this script.)
  $(info )
  $(info The GREP_OPTIONS environment variable feature of grep is deprecated)
  $(info for exactly this reason and will be removed from future versions of)
  $(info grep.  The grep man page suggests that you use an alias or a script)
  $(info to invoke grep with your preferred options.)
  $(info )
  $(info unset the GREP_OPTIONS environment variable to use this makefile)
  $(error 1)
endif
ifneq ($(findstring Windows,${OS}),)
  # Cygwin can return SHELL := C:/cygwin/bin/sh.exe  cygwin is OK & NOT WIN32
  ifeq ($(findstring /cygwin/,$(SHELL)),)
    ifeq ($(findstring .exe,${SHELL}),.exe)
      # MinGW
      WIN32 := 1
      # Tests don't run under MinGW
      TESTS := 0
    else # Msys or cygwin
      ifeq (MINGW,$(findstring MINGW,$(shell uname)))
        $(info *** This makefile can not be used with the Msys bash shell)
        $(error Use build_mingw.bat ${MAKECMDGOALS} from a Windows command prompt)
      endif
    endif
  endif
endif
ifeq ($(WIN32),)
  SIM_MAJOR=$(shell grep SIM_MAJOR sim_rev.h | awk '{ print $$3 }')
  GMAKE_VERSION = $(shell $(MAKE) --version /dev/null 2>&1 | grep 'GNU Make' | awk '{ print $$3 }')
  OLD = $(shell echo $(GMAKE_VERSION) | awk '{ if ($$1 < "3.81") {print "old"} }')
else
  SIM_MAJOR=$(shell for /F "tokens=3" %%i in ('findstr /c:"SIM_MAJOR" sim_rev.h') do echo %%i)
  GMAKE_VERSION = $(shell for /F "tokens=3" %%i in ('$(MAKE) --version ^| findstr /c:"GNU Make"') do echo %%i)
  OLD = $(shell cmd /e:on /c "if $(GMAKE_VERSION) LSS 3.81 echo old")
endif
ifeq ($(OLD),old)
  $(warning *** Warning *** GNU Make Version $(GMAKE_VERSION) is too old to)
  $(warning *** Warning *** fully process this makefile)
endif
BUILD_SINGLE := ${MAKECMDGOALS} $(BLANK_SUFFIX)
BUILD_MULTIPLE_VERB = is
# building the pdp1, pdp11, tx-0, or any microvax simulator could use video support
ifneq (3,${SIM_MAJOR})
  ifneq (,$(or $(findstring XXpdp1XX,$(addsuffix XX,$(addprefix XX,${MAKECMDGOALS}))),$(findstring pdp11,${MAKECMDGOALS}),$(findstring tx-0,${MAKECMDGOALS}),$(findstring microvax1,${MAKECMDGOALS}),$(findstring microvax2,${MAKECMDGOALS}),$(findstring microvax3900,${MAKECMDGOALS}),$(findstring microvax2000,${MAKECMDGOALS}),$(findstring vaxstation3100,${MAKECMDGOALS}),$(findstring XXvaxXX,$(addsuffix XX,$(addprefix XX,${MAKECMDGOALS})))))
    VIDEO_USEFUL = true
  endif
  # building the besm6 needs both video support and fontfile support
  ifneq (,$(findstring besm6,${MAKECMDGOALS}))
    VIDEO_USEFUL = true
    BESM6_BUILD = true
  endif
  # building the Imlac needs video support
  ifneq (,$(findstring imlac,${MAKECMDGOALS}))
    VIDEO_USEFUL = true
  endif
  # building the TT2500 needs video support
  ifneq (,$(findstring tt2500,${MAKECMDGOALS}))
    VIDEO_USEFUL = true
  endif
  # building the PDP6, KA10 or KI10 needs video support
  ifneq (,$(or $(findstring pdp6,${MAKECMDGOALS}),$(findstring pdp10-ka,${MAKECMDGOALS}),$(findstring pdp10-ki,${MAKECMDGOALS})))
    VIDEO_USEFUL = true
  endif
  # building the AltairZ80 could use video support
  ifneq (,$(findstring altairz80,${MAKECMDGOALS}))
    VIDEO_USEFUL = true
  endif
endif
# building the SEL32 networking can be used
ifneq (,$(findstring sel32,${MAKECMDGOALS}))
  NETWORK_USEFUL = true
endif
# building the PDP-7 needs video support
ifneq (,$(findstring pdp7,${MAKECMDGOALS}))
  VIDEO_USEFUL = true
endif
# building the pdp11, any pdp10, any 3b2, or any vax simulator could use networking support
ifneq (,$(findstring pdp11,${MAKECMDGOALS})$(findstring pdp10,${MAKECMDGOALS})$(findstring vax,${MAKECMDGOALS})$(findstring infoserver,${MAKECMDGOALS})$(findstring 3b2,${MAKECMDGOALS})$(findstring all,${MAKECMDGOALS}))
  NETWORK_USEFUL = true
  ifneq (,$(findstring all,${MAKECMDGOALS}))
    BUILD_MULTIPLE = s
    BUILD_MULTIPLE_VERB = are
    VIDEO_USEFUL = true
    BESM6_BUILD = true
  endif
  ifneq (,$(word 2,${MAKECMDGOALS}))
    BUILD_MULTIPLE = s
    BUILD_MULTIPLE_VERB = are
  endif
else
  ifeq (${MAKECMDGOALS},)
    # default target is all
    NETWORK_USEFUL = true
    VIDEO_USEFUL = true
    BUILD_MULTIPLE = s
    BUILD_MULTIPLE_VERB = are
    BUILD_SINGLE := all $(BUILD_SINGLE)
    BESM6_BUILD = true
  endif
endif
# someone may want to explicitly build simulators without network support
ifneq ($(NONETWORK),)
  NETWORK_USEFUL =
endif
# ... or without video support
ifneq ($(NOVIDEO),)
  VIDEO_USEFUL =
endif
ifneq ($(findstring Windows,${OS}),)
  ifeq ($(findstring /cygwin/,$(SHELL)),)
    ifeq ($(findstring .exe,${SHELL}),.exe)
      # MinGW
      WIN32 := 1
      # Tests don't run under MinGW
      TESTS := 0
    else # Msys or cygwin
      ifeq (MINGW,$(findstring MINGW,$(shell uname)))
        $(info *** This makefile can not be used with the Msys bash shell)
        $(error Use build_mingw.bat ${MAKECMDGOALS} from a Windows command prompt)
      endif
    endif
  endif
endif
ifeq (3,${SIM_MAJOR})
  # simh v3 DOES not have any video support
  VIDEO_USEFUL =
endif

find_exe = $(abspath $(strip $(firstword $(foreach dir,$(strip $(subst :, ,${PATH})),$(wildcard $(dir)/$(1))))))
find_lib = $(firstword $(abspath $(strip $(firstword $(foreach dir,$(strip ${LIBPATH}),$(foreach ext,$(strip ${LIBEXT}),$(wildcard $(dir)/lib$(1).$(ext))))))))
find_include = $(abspath $(strip $(firstword $(foreach dir,$(strip ${INCPATH}),$(wildcard $(dir)/$(1).h)))))
ifneq (3,${SIM_MAJOR})
  ifneq (0,$(TESTS))
    find_test = RegisterSanityCheck $(abspath $(wildcard $(1)/tests/$(2)_test.ini)) </dev/null
    ifneq (,${TEST_ARG})
      TESTING_FEATURES = - Per simulator tests will be run with argument: ${TEST_ARG}
    else
      TESTING_FEATURES = - Per simulator tests will be run
    endif
  else
    TESTING_FEATURES = - Per simulator tests will be skipped
  endif
endif
ifeq (${WIN32},)  #*nix Environments (&& cygwin)
  ifeq (${GCC},)
    ifeq (,$(shell which gcc 2>/dev/null))
      $(info *** Warning *** Using local cc since gcc isn't available locally.)
      $(info *** Warning *** You may need to install gcc to build working simulators.)
      GCC = cc
    else
      GCC = gcc
    endif
  endif
  OSTYPE = $(shell uname)
  # OSNAME is used in messages to indicate the source of libpcap components
  OSNAME = $(OSTYPE)
  ifeq (SunOS,$(OSTYPE))
    TEST = /bin/test
  else
    TEST = test
  endif
  ifeq (CYGWIN,$(findstring CYGWIN,$(OSTYPE))) # uname returns CYGWIN_NT-n.n-ver
    OSTYPE = cygwin
    OSNAME = windows-build
  endif
  ifeq (Darwin,$(OSTYPE))
    ifeq (,$(shell which port)$(shell which brew))
      $(info *** Info *** simh dependent packages on macOS must be provided by either the)
      $(info *** Info *** MacPorts package system or by the HomeBrew package system.)
      $(info *** Info *** Neither of these seem to be installed on the local system.)
      $(info *** Info ***)
      ifeq (,$(INCLUDES)$(LIBRARIES))
        $(info *** Info *** Users wanting to build simulators with locally built dependent)
        $(info *** Info *** packages or packages provided by an unsupported package)
        $(info *** Info *** management system may be able to override where this procedure)
        $(info *** Info *** looks for include files and/or libraries.  Overrides can be)
        $(info *** Info *** specified by defining exported environment variables or GNU make)
        $(info *** Info *** command line arguments which specify INCLUDES and/or LIBRARIES.)
        $(info *** Info *** If this works, that's great, if it doesn't you are on your own!)
      else
        $(info *** Warning *** Attempting to build on macOS with:)
        $(info *** Warning *** INCLUDES defined as $(INCLUDES))
        $(info *** Warning *** and)
        $(info *** Warning *** LIBRARIES defined as $(LIBRARIES))
      endif
    endif
  endif
  ifeq (,$(shell ${GCC} -v /dev/null 2>&1 | grep 'clang'))
    GCC_VERSION = $(shell ${GCC} -v /dev/null 2>&1 | grep 'gcc version' | awk '{ print $$3 }')
    COMPILER_NAME = GCC Version: $(GCC_VERSION)
    ifeq (,$(GCC_VERSION))
      ifeq (SunOS,$(OSTYPE))
        ifneq (,$(shell ${GCC} -V 2>&1 | grep 'Sun C'))
          SUNC_VERSION = $(shell ${GCC} -V 2>&1 | grep 'Sun C')
          COMPILER_NAME = $(wordlist 2,10,$(SUNC_VERSION))
          CC_STD = -std=c99
        endif
      endif
      ifeq (HP-UX,$(OSTYPE))
        ifneq (,$(shell what `which $(firstword ${GCC}) 2>&1`| grep -i compiler))
          COMPILER_NAME = $(strip $(shell what `which $(firstword ${GCC}) 2>&1` | grep -i compiler))
          CC_STD = -std=gnu99
        endif
      endif
    else
      OS_CCDEFS += $(if $(findstring ALLOWED,$(WARNINGS)),,-Werror)
      ifeq (,$(findstring ++,${GCC}))
        CC_STD = -std=gnu99
      else
        CPP_BUILD = 1
      endif
    endif
  else
    OS_CCDEFS += $(if $(findstring ALLOWED,$(WARNINGS)),,-Werror)
    ifeq (Apple,$(shell ${GCC} -v /dev/null 2>&1 | grep 'Apple' | awk '{ print $$1 }'))
      COMPILER_NAME = $(shell ${GCC} -v /dev/null 2>&1 | grep 'Apple' | awk '{ print $$1 " " $$2 " " $$3 " " $$4 }')
      CLANG_VERSION = $(word 4,$(COMPILER_NAME))
    else
      COMPILER_NAME = $(shell ${GCC} -v /dev/null 2>&1 | grep 'clang version' | awk '{ print $$1 " " $$2 " " $$3 }')
      CLANG_VERSION = $(word 3,$(COMPILER_NAME))
      ifeq (,$(findstring .,$(CLANG_VERSION)))
        COMPILER_NAME = $(shell ${GCC} -v /dev/null 2>&1 | grep 'clang version' | awk '{ print $$1 " " $$2 " " $$3 " " $$4 }')
        CLANG_VERSION = $(word 4,$(COMPILER_NAME))
      endif
    endif
    ifeq (,$(findstring ++,${GCC}))
      CC_STD = -std=c99
    else
      CPP_BUILD = 1
      OS_CCDEFS += -Wno-deprecated
    endif
  endif
  ifeq (git-repo,$(shell if ${TEST} -e ./.git; then echo git-repo; fi))
    GIT_PATH=$(strip $(shell which git))
    ifeq (,$(GIT_PATH))
      $(error building using a git repository, but git is not available)
    endif
    ifeq (commit-id-exists,$(shell if ${TEST} -e .git-commit-id; then echo commit-id-exists; fi))
      CURRENT_GIT_COMMIT_ID=$(strip $(shell grep 'SIM_GIT_COMMIT_ID' .git-commit-id | awk '{ print $$2 }'))
      ACTUAL_GIT_COMMIT_ID=$(strip $(shell git log -1 --pretty="%H"))
      ifneq ($(CURRENT_GIT_COMMIT_ID),$(ACTUAL_GIT_COMMIT_ID))
        NEED_COMMIT_ID = need-commit-id
        # make sure that the invalidly formatted .git-commit-id file wasn't generated
        # by legacy git hooks which need to be removed.
        $(shell rm -f .git/hooks/post-checkout .git/hooks/post-commit .git/hooks/post-merge)
      endif
    else
      NEED_COMMIT_ID = need-commit-id
    endif
    ifneq (,$(shell git update-index --refresh --))
      GIT_EXTRA_FILES=+uncommitted-changes
    endif
    ifneq (,$(or $(NEED_COMMIT_ID),$(GIT_EXTRA_FILES)))
      isodate=$(shell git log -1 --pretty="%ai"|sed -e 's/ /T/'|sed -e 's/ //')
      $(shell git log -1 --pretty="SIM_GIT_COMMIT_ID %H$(GIT_EXTRA_FILES)%nSIM_GIT_COMMIT_TIME $(isodate)" >.git-commit-id)
    endif
  endif
  LTO_EXCLUDE_VERSIONS = 
  PCAPLIB = pcap
  ifeq (agcc,$(findstring agcc,${GCC})) # Android target build?
    OS_CCDEFS += -D_GNU_SOURCE
    AIO_CCDEFS += -DSIM_ASYNCH_IO
    OS_LDFLAGS = -lm
  else # Non-Android (or Native Android) Builds
    ifeq (,$(INCLUDES)$(LIBRARIES))
      INCPATH:=$(shell LANG=C; ${GCC} -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | tr -d '\n')
      ifeq (,${INCPATH})
        INCPATH:=/usr/include
      endif
      LIBPATH:=/usr/lib
    else
      $(info *** Warning ***)
      ifeq (,$(INCLUDES))
        INCPATH:=$(shell LANG=C; ${GCC} -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | tr -d '\n')
      else
        $(info *** Warning *** Unsupported build with INCLUDES defined as: $(INCLUDES))
        INCPATH:=$(strip $(subst :, ,$(INCLUDES)))
        UNSUPPORTED_BUILD := include
      endif
      ifeq (,$(LIBRARIES))
        LIBPATH:=/usr/lib
      else
        $(info *** Warning *** Unsupported build with LIBRARIES defined as: $(LIBRARIES))
        LIBPATH:=$(strip $(subst :, ,$(LIBRARIES)))
        ifeq (include,$(UNSUPPORTED_BUILD))
          UNSUPPORTED_BUILD := include+lib
        else
          UNSUPPORTED_BUILD := lib
        endif
      endif
      $(info *** Warning ***)
    endif
    OS_CCDEFS += -D_GNU_SOURCE
    GCC_OPTIMIZERS_CMD = ${GCC} -v --help 2>&1
    GCC_WARNINGS_CMD = ${GCC} -v --help 2>&1
    LD_ELF = $(shell echo | ${GCC} -E -dM - | grep __ELF__)
    ifeq (Darwin,$(OSTYPE))
      OSNAME = OSX
      LIBEXT = dylib
      ifneq (include,$(findstring include,$(UNSUPPORTED_BUILD)))
        INCPATH:=$(shell LANG=C; ${GCC} -x c -v -E /dev/null 2>&1 | grep -A 10 '> search starts here' | grep '^ ' | grep -v 'framework directory' | tr -d '\n')
      endif
      ifeq (incopt,$(shell if ${TEST} -d /opt/local/include; then echo incopt; fi))
        INCPATH += /opt/local/include
        OS_CCDEFS += -I/opt/local/include
      endif
      ifeq (libopt,$(shell if ${TEST} -d /opt/local/lib; then echo libopt; fi))
        LIBPATH += /opt/local/lib
        OS_LDFLAGS += -L/opt/local/lib
      endif
      ifeq (HomeBrew,$(or $(shell if ${TEST} -d /usr/local/Cellar; then echo HomeBrew; fi),$(shell if ${TEST} -d /opt/homebrew/Cellar; then echo HomeBrew; fi)))
        ifeq (local,$(shell if $(TEST) -d /usr/local/Cellar; then echo local; fi))
          HBPATH = /usr/local
        else
          HBPATH = /opt/homebrew
        endif
        INCPATH += $(foreach dir,$(wildcard $(HBPATH)/Cellar/*/*),$(realpath $(dir)/include))
        LIBPATH += $(foreach dir,$(wildcard $(HBPATH)/Cellar/*/*),$(realpath $(dir)/lib))
      endif
    else
      ifeq (Linux,$(OSTYPE))
        ifeq (Android,$(shell uname -o))
          OS_CCDEFS += -D__ANDROID_API__=$(shell getprop ro.build.version.sdk) -DSIM_BUILD_OS=" On Android Version $(shell getprop ro.build.version.release)"
        endif
        ifneq (lib,$(findstring lib,$(UNSUPPORTED_BUILD)))
          ifeq (Android,$(shell uname -o))
            ifneq (,$(shell if ${TEST} -d ${PREFIX}/lib; then echo prefixlib; fi))
              LIBPATH += ${PREFIX}/lib
            endif
            ifneq (,$(shell if ${TEST} -d /system/lib; then echo systemlib; fi))
              LIBPATH += /system/lib
            endif
            LIBPATH += $(LD_LIBRARY_PATH)
          endif
          ifeq (ldconfig,$(shell if ${TEST} -e /sbin/ldconfig; then echo ldconfig; fi))
            LIBPATH := $(sort $(foreach lib,$(shell /sbin/ldconfig -p | grep ' => /' | sed 's/^.* => //'),$(dir $(lib))))
          endif
        endif
        LIBSOEXT = so
        LIBEXT = $(LIBSOEXT) a
      else
        ifeq (SunOS,$(OSTYPE))
          OSNAME = Solaris
          ifneq (lib,$(findstring lib,$(UNSUPPORTED_BUILD)))
            LIBPATH := $(shell LANG=C; crle | grep 'Default Library Path' | awk '{ print $$5 }' | sed 's/:/ /g')
          endif
          LIBEXT = so
          OS_LDFLAGS += -lsocket -lnsl
          ifeq (incsfw,$(shell if ${TEST} -d /opt/sfw/include; then echo incsfw; fi))
            INCPATH += /opt/sfw/include
            OS_CCDEFS += -I/opt/sfw/include
          endif
          ifeq (libsfw,$(shell if ${TEST} -d /opt/sfw/lib; then echo libsfw; fi))
            LIBPATH += /opt/sfw/lib
            OS_LDFLAGS += -L/opt/sfw/lib -R/opt/sfw/lib
          endif
          OS_CCDEFS += -D_LARGEFILE_SOURCE
        else
          ifeq (cygwin,$(OSTYPE))
            # use 0readme_ethernet.txt documented Windows pcap build components
            INCPATH += ../windows-build/winpcap/WpdPack/Include
            LIBPATH += ../windows-build/winpcap/WpdPack/Lib
            PCAPLIB = wpcap
            LIBEXT = a
          else
            ifneq (,$(findstring AIX,$(OSTYPE)))
              OS_LDFLAGS += -lm -lrt
              ifeq (incopt,$(shell if ${TEST} -d /opt/freeware/include; then echo incopt; fi))
                INCPATH += /opt/freeware/include
                OS_CCDEFS += -I/opt/freeware/include
              endif
              ifeq (libopt,$(shell if ${TEST} -d /opt/freeware/lib; then echo libopt; fi))
                LIBPATH += /opt/freeware/lib
                OS_LDFLAGS += -L/opt/freeware/lib
              endif
            else
              ifneq (,$(findstring Haiku,$(OSTYPE)))
                HAIKU_ARCH=$(shell getarch)
                ifeq ($(HAIKU_ARCH),)
                  $(error Missing getarch command, your Haiku release is probably too old)
                endif
                ifeq ($(HAIKU_ARCH),x86_gcc2)
                  $(error Unsupported arch x86_gcc2. Run setarch x86 and retry)
                endif
                INCPATH := $(shell findpaths -e -a $(HAIKU_ARCH) B_FIND_PATH_HEADERS_DIRECTORY)
                INCPATH += $(shell findpaths -e B_FIND_PATH_HEADERS_DIRECTORY posix)
                LIBPATH := $(shell findpaths -e -a $(HAIKU_ARCH) B_FIND_PATH_DEVELOP_LIB_DIRECTORY)
                OS_LDFLAGS += -lnetwork
              else
                ifeq (,$(findstring NetBSD,$(OSTYPE)))
                  ifneq (no ldconfig,$(findstring no ldconfig,$(shell which ldconfig 2>&1)))
                    LDSEARCH :=$(shell LANG=C; ldconfig -r | grep 'search directories' | awk '{print $$3}' | sed 's/:/ /g')
                  endif
                  ifneq (,$(LDSEARCH))
                    LIBPATH := $(LDSEARCH)
                  else
                    ifeq (,$(strip $(LPATH)))
                      $(info *** Warning ***)
                      $(info *** Warning *** The library search path on your $(OSTYPE) platform can not be)
                      $(info *** Warning *** determined.  This should be resolved before you can expect)
                      $(info *** Warning *** to have fully working simulators.)
                      $(info *** Warning ***)
                      $(info *** Warning *** You can specify your library paths via the LPATH environment)
                      $(info *** Warning *** variable.)
                      $(info *** Warning ***)
                    else
                      LIBPATH = $(subst :, ,$(LPATH))
                    endif
                  endif
                  OS_LDFLAGS += $(patsubst %,-L%,${LIBPATH})
                endif
              endif
            endif
            ifeq (usrpkglib,$(shell if ${TEST} -d /usr/pkg/lib; then echo usrpkglib; fi))
              LIBPATH += /usr/pkg/lib
              INCPATH += /usr/pkg/include
              OS_LDFLAGS += -L/usr/pkg/lib -R/usr/pkg/lib
              OS_CCDEFS += -I/usr/pkg/include
            endif
            ifeq (/usr/local/lib,$(findstring /usr/local/lib,${LIBPATH}))
              INCPATH += /usr/local/include
              OS_CCDEFS += -I/usr/local/include
            endif
            ifneq (,$(findstring NetBSD,$(OSTYPE))$(findstring FreeBSD,$(OSTYPE))$(findstring AIX,$(OSTYPE)))
              LIBEXT = so
            else
              ifeq (HP-UX,$(OSTYPE))
                ifeq (ia64,$(shell uname -m))
                  LIBEXT = so
                else
                  LIBEXT = sl
                endif
                OS_CCDEFS += -D_HPUX_SOURCE -D_LARGEFILE64_SOURCE
                OS_LDFLAGS += -Wl,+b:
                override LTO =
              else
                LIBEXT = a
              endif
            endif
          endif
        endif
      endif
    endif
    ifeq (,$(LIBSOEXT))
      LIBSOEXT = $(LIBEXT)
    endif
    ifeq (,$(filter /lib/,$(LIBPATH)))
      ifeq (existlib,$(shell if $(TEST) -d /lib/; then echo existlib; fi))
        LIBPATH += /lib/
      endif
    endif
    ifeq (,$(filter /usr/lib/,$(LIBPATH)))
      ifeq (existusrlib,$(shell if $(TEST) -d /usr/lib/; then echo existusrlib; fi))
        LIBPATH += /usr/lib/
      endif
    endif
    export CPATH = $(subst $() $(),:,$(INCPATH))
    export LIBRARY_PATH = $(subst $() $(),:,$(LIBPATH))
  endif
  $(info lib paths are: ${LIBPATH})
  $(info include paths are: ${INCPATH})
  need_search = $(strip $(shell ld -l$(1) /dev/null 2>&1 | grep $(1) | sed s/$(1)//))
  LD_SEARCH_NEEDED := $(call need_search,ZzzzzzzZ)
  ifneq (,$(call find_lib,m))
    OS_LDFLAGS += -lm
    $(info using libm: $(call find_lib,m))
  endif
  ifneq (,$(call find_lib,rt))
    OS_LDFLAGS += -lrt
    $(info using librt: $(call find_lib,rt))
  endif
  ifneq (,$(call find_include,pthread))
    ifneq (,$(call find_lib,pthread))
      AIO_CCDEFS += -DUSE_READER_THREAD -DSIM_ASYNCH_IO
      OS_LDFLAGS += -lpthread
      $(info using libpthread: $(call find_lib,pthread) $(call find_include,pthread))
    else
      LIBEXTSAVE := ${LIBEXT}
      LIBEXT = a
      ifneq (,$(call find_lib,pthread))
        AIO_CCDEFS += -DUSE_READER_THREAD -DSIM_ASYNCH_IO 
        OS_LDFLAGS += -lpthread
        $(info using libpthread: $(call find_lib,pthread) $(call find_include,pthread))
      else
        ifneq (,$(findstring Haiku,$(OSTYPE)))
          AIO_CCDEFS += -DUSE_READER_THREAD -DSIM_ASYNCH_IO 
          $(info using libpthread: $(call find_include,pthread))
        else
          ifeq (Darwin,$(OSTYPE))
            AIO_CCDEFS += -DUSE_READER_THREAD -DSIM_ASYNCH_IO 
            OS_LDFLAGS += -lpthread
            $(info using macOS libpthread: $(call find_include,pthread))
          endif
        endif
      endif
      LIBEXT = $(LIBEXTSAVE)
    endif
  endif
  # Find PCRE RegEx library.
  ifneq (,$(call find_include,pcre))
    ifneq (,$(call find_lib,pcre))
      OS_CCDEFS += -DHAVE_PCRE_H
      OS_LDFLAGS += -lpcre
      $(info using libpcre: $(call find_lib,pcre) $(call find_include,pcre))
      ifeq ($(LD_SEARCH_NEEDED),$(call need_search,pcre))
        OS_LDFLAGS += -L$(dir $(call find_lib,pcre))
      endif
    endif
  endif
  # Find available ncurses library.
  ifneq (,$(call find_include,ncurses))
    ifneq (,$(call find_lib,ncurses))
      OS_CURSES_DEFS += -DHAVE_NCURSES -lncurses
    endif
  endif
  ifneq (,$(call find_include,semaphore))
    ifneq (, $(shell grep sem_timedwait $(call find_include,semaphore)))
      OS_CCDEFS += -DHAVE_SEMAPHORE
      $(info using semaphore: $(call find_include,semaphore))
    endif
  endif
  ifneq (,$(call find_include,sys/ioctl))
    OS_CCDEFS += -DHAVE_SYS_IOCTL
  endif
  ifneq (,$(call find_include,linux/cdrom))
    OS_CCDEFS += -DHAVE_LINUX_CDROM
  endif
  ifneq (,$(call find_include,dlfcn))
    ifneq (,$(call find_lib,dl))
      OS_CCDEFS += -DSIM_HAVE_DLOPEN=$(LIBSOEXT)
      OS_LDFLAGS += -ldl
      $(info using libdl: $(call find_lib,dl) $(call find_include,dlfcn))
    else
      ifneq (,$(findstring BSD,$(OSTYPE))$(findstring AIX,$(OSTYPE))$(findstring Haiku,$(OSTYPE)))
        OS_CCDEFS += -DSIM_HAVE_DLOPEN=so
        $(info using libdl: $(call find_include,dlfcn))
      else
        ifneq (,$(call find_lib,dld))
          OS_CCDEFS += -DSIM_HAVE_DLOPEN=$(LIBSOEXT)
          OS_LDFLAGS += -ldld
          $(info using libdld: $(call find_lib,dld) $(call find_include,dlfcn))
        else
          ifeq (Darwin,$(OSTYPE))
            OS_CCDEFS += -DSIM_HAVE_DLOPEN=dylib
            $(info using macOS dlopen with .dylib)
          endif
        endif
      endif
    endif
  endif
  ifneq (,$(call find_include,editline/readline))
    OS_CCDEFS += -DHAVE_EDITLINE
    OS_LDFLAGS += -ledit
    ifneq (Darwin,$(OSTYPE))
      # The doc says termcap is needed, though reality suggests
      # otherwise.  Put it in anyway, it can't hurt.
      ifneq (,$(call find_lib,termcap))
        OS_LDFLAGS += -ltermcap
      endif
    endif
    $(info using libedit: $(call find_include,editline/readline))
  endif
  ifneq (,$(call find_include,utime))
    OS_CCDEFS += -DHAVE_UTIME
  endif
  ifneq (,$(call find_include,png))
    ifneq (,$(call find_lib,png))
      OS_CCDEFS += -DHAVE_LIBPNG
      OS_LDFLAGS += -lpng
      $(info using libpng: $(call find_lib,png) $(call find_include,png))
      ifneq (,$(call find_include,zlib))
        ifneq (,$(call find_lib,z))
          OS_CCDEFS += -DHAVE_ZLIB
          OS_LDFLAGS += -lz
          $(info using zlib: $(call find_lib,z) $(call find_include,zlib))
        endif
      endif
    endif
  endif
  ifneq (,$(call find_include,glob))
    OS_CCDEFS += -DHAVE_GLOB
  else
    ifneq (,$(call find_include,fnmatch))
      OS_CCDEFS += -DHAVE_FNMATCH    
    endif
  endif
  ifneq (,$(call find_include,sys/mman))
    ifneq (,$(shell grep shm_open $(call find_include,sys/mman)))
      # some Linux installs have been known to have the include, but are
      # missing librt (where the shm_ APIs are implemented on Linux)
      # other OSes seem have these APIs implemented elsewhere
      ifneq (,$(if $(findstring Linux,$(OSTYPE)),$(call find_lib,rt),OK))
        OS_CCDEFS += -DHAVE_SHM_OPEN
        $(info using mman: $(call find_include,sys/mman))
      endif
    endif
  endif
  ifeq (cygwin,$(OSTYPE))
    LIBEXTSAVE := ${LIBEXT}
    LIBEXT = dll.a
  endif
  ifneq (,$(VIDEO_USEFUL))
    ifneq (,$(call find_include,SDL2/SDL))
      ifneq (,$(call find_lib,SDL2))
        ifneq (,$(shell which sdl2-config))
          SDLX_CONFIG = sdl2-config
        endif
        ifneq (,$(SDLX_CONFIG))
          VIDEO_CCDEFS += -DHAVE_LIBSDL -DUSE_SIM_VIDEO `$(SDLX_CONFIG) --cflags`
          VIDEO_LDFLAGS += `$(SDLX_CONFIG) --libs`
          VIDEO_FEATURES = - video capabilities provided by libSDL2 (Simple Directmedia Layer)
          DISPLAYL = ${DISPLAYD}/display.c $(DISPLAYD)/sim_ws.c
          DISPLAYVT = ${DISPLAYD}/vt11.c
          DISPLAY340 = ${DISPLAYD}/type340.c
          DISPLAYNG = ${DISPLAYD}/ng.c
          DISPLAYIII = ${DISPLAYD}/iii.c
          DISPLAY_OPT += -DUSE_DISPLAY $(VIDEO_CCDEFS) $(VIDEO_LDFLAGS)
          $(info using libSDL2: $(call find_include,SDL2/SDL))
          ifeq (Darwin,$(OSTYPE))
            VIDEO_CCDEFS += -DSDL_MAIN_AVAILABLE
          endif
          ifneq (,$(and $(BESM6_BUILD), $(or $(and $(call find_include,SDL2/SDL_ttf),$(call find_lib,SDL2_ttf)), $(and $(call find_include,SDL/SDL_ttf),$(call find_lib,SDL_ttf)))))
            FONTPATH += /usr/share/fonts /Library/Fonts /usr/lib/jvm /System/Library/Frameworks/JavaVM.framework/Versions /System/Library/Fonts C:/Windows/Fonts
            FONTPATH := $(dir $(foreach dir,$(strip $(FONTPATH)),$(wildcard $(dir)/.)))
            FONTNAME += DejaVuSans.ttf LucidaSansRegular.ttf FreeSans.ttf AppleGothic.ttf tahoma.ttf
            $(info font paths are: $(FONTPATH))
            $(info font names are: $(FONTNAME))
            find_fontfile = $(strip $(firstword $(foreach dir,$(strip $(FONTPATH)),$(wildcard $(dir)/$(1))$(wildcard $(dir)/*/$(1))$(wildcard $(dir)/*/*/$(1))$(wildcard $(dir)/*/*/*/$(1)))))
            find_font = $(abspath $(strip $(firstword $(foreach font,$(strip $(FONTNAME)),$(call find_fontfile,$(font))))))
            ifneq (,$(call find_font))
              FONTFILE=$(call find_font)
            else
              $(info ***)
              $(info *** No font file available, BESM-6 video panel disabled.)
              $(info ***)
              $(info *** To enable the panel display please specify one of:)
              $(info ***          a font path with FONTPATH=path)
              $(info ***          a font name with FONTNAME=fontname.ttf)
              $(info ***          a font file with FONTFILE=path/fontname.ttf)
              $(info ***)
            endif
          endif
          ifeq (,$(and ${VIDEO_LDFLAGS}, ${FONTFILE}, $(BESM6_BUILD)))
            $(info *** No SDL ttf support available.  BESM-6 video panel disabled.)
            $(info ***)
            ifeq (Darwin,$(OSTYPE))
              ifeq (/opt/local/bin/port,$(shell which port))
                $(info *** Info *** Install the MacPorts libSDL2-ttf development package to provide this)
                $(info *** Info *** functionality for your OS X system:)
                $(info *** Info ***       # port install libsdl2-ttf-dev)
              endif
              ifeq (/usr/local/bin/brew,$(shell which brew))
                ifeq (/opt/local/bin/port,$(shell which port))
                  $(info *** Info ***)
                  $(info *** Info *** OR)
                  $(info *** Info ***)
                endif
                $(info *** Info *** Install the HomeBrew sdl2_ttf package to provide this)
                $(info *** Info *** functionality for your OS X system:)
                $(info *** Info ***       $$ brew install sdl2_ttf)
              endif
            else
              ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,apt-get)))
                $(info *** Info *** Install the development components of libSDL2-ttf)
                $(info *** Info *** packaged for your Linux operating system distribution:)
                $(info *** Info ***        $$ sudo apt-get install libsdl2-ttf-dev)
              else
                $(info *** Info *** Install the development components of libSDL2-ttf packaged by your)
                $(info *** Info *** operating system distribution and rebuild your simulator to)
                $(info *** Info *** enable this extra functionality.)
              endif
            endif
          else
            ifneq (,$(call find_include,SDL2/SDL_ttf),$(call find_lib,SDL2_ttf))
              $(info using libSDL2_ttf: $(call find_lib,SDL2_ttf) $(call find_include,SDL2/SDL_ttf))
              $(info ***)
              BESM6_PANEL_OPT = -DFONTFILE=${FONTFILE} $(filter-out -DSDL_MAIN_AVAILABLE,${VIDEO_CCDEFS}) ${VIDEO_LDFLAGS} -lSDL2_ttf
            endif
          endif
        endif
      endif
    endif
    ifeq (,$(findstring HAVE_LIBSDL,$(VIDEO_CCDEFS)))
      $(info *** Info ***)
      $(info *** Info *** The simulator$(BUILD_MULTIPLE) you are building could provide more functionality)
      $(info *** Info *** if video support was available on your system.)
      $(info *** Info *** To gain this functionality:)
      ifeq (Darwin,$(OSTYPE))
        ifeq (/opt/local/bin/port,$(shell which port))
          $(info *** Info *** Install the MacPorts libSDL2 package to provide this)
          $(info *** Info *** functionality for your OS X system:)
          $(info *** Info ***       # port install libsdl2 libpng zlib)
        endif
        ifeq (/usr/local/bin/brew,$(shell which brew))
          ifeq (/opt/local/bin/port,$(shell which port))
            $(info *** Info ***)
            $(info *** Info *** OR)
            $(info *** Info ***)
          endif
          $(info *** Info *** Install the HomeBrew libSDL2 package to provide this)
          $(info *** Info *** functionality for your OS X system:)
          $(info *** Info ***       $$ brew install sdl2 libpng zlib)
        else
          ifeq (,$(shell which port))
            $(info *** Info *** Install MacPorts or HomeBrew and rerun this make for)
            $(info *** Info *** specific advice)
          endif
        endif
      else
        ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,apt-get)))
          $(info *** Info *** Install the development components of libSDL2 packaged for)
          $(info *** Info *** your operating system distribution for your Linux)
          $(info *** Info *** system:)
          $(info *** Info ***        $$ sudo apt-get install libsdl2-dev libpng-dev)
        else
          $(info *** Info *** Install the development components of libSDL2 packaged by your)
          $(info *** Info *** operating system distribution and rebuild your simulator to)
          $(info *** Info *** enable this extra functionality.)
        endif
      endif
      $(info *** Info ***)
    endif
  endif
  ifeq (cygwin,$(OSTYPE))
    LIBEXT = $(LIBEXTSAVE)
    LIBPATH += /usr/lib/w32api
    ifneq (,$(call find_lib,winmm))
      OS_CCDEFS += -DHAVE_WINMM
      OS_LDFLAGS += -lwinmm
    endif
  endif
  ifneq (,$(NETWORK_USEFUL))
    ifneq (,$(call find_include,pcap))
      ifneq (,$(shell grep 'pcap/pcap.h' $(call find_include,pcap) | grep include))
        PCAP_H_PATH = $(dir $(call find_include,pcap))pcap/pcap.h
      else
        PCAP_H_PATH = $(call find_include,pcap)
      endif
      ifneq (,$(shell grep pcap_compile $(PCAP_H_PATH) | grep const))
        BPF_CONST_STRING = -DBPF_CONST_STRING
      endif
      NETWORK_CCDEFS += -DHAVE_PCAP_NETWORK -I$(dir $(call find_include,pcap)) $(BPF_CONST_STRING)
      NETWORK_LAN_FEATURES += PCAP
      ifneq (,$(call find_lib,$(PCAPLIB)))
        ifneq ($(USE_NETWORK),) # Network support specified on the GNU make command line
          NETWORK_CCDEFS += -DUSE_NETWORK
          ifeq (,$(findstring Linux,$(OSTYPE))$(findstring Darwin,$(OSTYPE)))
            $(info *** Warning ***)
            $(info *** Warning *** Statically linking against libpcap is provides no measurable)
            $(info *** Warning *** benefits over dynamically linking libpcap.)
            $(info *** Warning ***)
            $(info *** Warning *** Support for linking this way is currently deprecated and may be removed)
            $(info *** Warning *** in the future.)
            $(info *** Warning ***)
          else
            $(info *** Error ***)
            $(info *** Error *** Statically linking against libpcap is provides no measurable)
            $(info *** Error *** benefits over dynamically linking libpcap.)
            $(info *** Error ***)
            $(info *** Error *** Support for linking statically has been removed on the $(OSTYPE))
            $(info *** Error *** platform.)
            $(info *** Error ***)
            $(error Retry your build without specifying USE_NETWORK=1)
          endif
          ifeq (cygwin,$(OSTYPE))
            # cygwin has no ldconfig so explicitly specify pcap object library
            NETWORK_LDFLAGS = -L$(dir $(call find_lib,$(PCAPLIB))) -Wl,-R,$(dir $(call find_lib,$(PCAPLIB))) -l$(PCAPLIB)
          else
            NETWORK_LDFLAGS = -l$(PCAPLIB)
          endif
          $(info using libpcap: $(call find_lib,$(PCAPLIB)) $(call find_include,pcap))
          NETWORK_FEATURES = - static networking support using $(OSNAME) provided libpcap components
        else # default build uses dynamic libpcap
          NETWORK_CCDEFS += -DUSE_SHARED
          $(info using libpcap: $(call find_include,pcap))
          NETWORK_FEATURES = - dynamic networking support using $(OSNAME) provided libpcap components
        endif
      else
        LIBEXTSAVE := ${LIBEXT}
        LIBEXT = a
        ifneq (,$(call find_lib,$(PCAPLIB)))
          NETWORK_CCDEFS += -DUSE_NETWORK
          NETWORK_LDFLAGS := -L$(dir $(call find_lib,$(PCAPLIB))) -l$(PCAPLIB)
          NETWORK_FEATURES = - static networking support using $(OSNAME) provided libpcap components
          $(info using libpcap: $(call find_lib,$(PCAPLIB)) $(call find_include,pcap))
        endif
        LIBEXT = $(LIBEXTSAVE)
        ifeq (Darwin,$(OSTYPE)$(findstring USE_,$(NETWORK_CCDEFS)))
          NETWORK_CCDEFS += -DUSE_SHARED
          NETWORK_FEATURES = - dynamic networking support using $(OSNAME) provided libpcap components
          $(info using macOS dynamic libpcap: $(call find_include,pcap))
        endif
      endif
    else
      # On non-Linux platforms, we'll still try to provide deprecated support for libpcap in /usr/local
      INCPATHSAVE := ${INCPATH}
      ifeq (,$(findstring Linux,$(OSTYPE)))
        # Look for package built from tcpdump.org sources with default install target (or cygwin winpcap)
        INCPATH += /usr/local/include
        PCAP_H_FOUND = $(call find_include,pcap)
      endif
      ifneq (,$(strip $(PCAP_H_FOUND)))
        ifneq (,$(shell grep 'pcap/pcap.h' $(call find_include,pcap) | grep include))
          PCAP_H_PATH = $(dir $(call find_include,pcap))pcap/pcap.h
        else
          PCAP_H_PATH = $(call find_include,pcap)
        endif
        ifneq (,$(shell grep pcap_compile $(PCAP_H_PATH) | grep const))
          BPF_CONST_STRING = -DBPF_CONST_STRING
        endif
        LIBEXTSAVE := ${LIBEXT}
        # first check if binary - shared objects are available/installed in the linker known search paths
        ifneq (,$(call find_lib,$(PCAPLIB)))
          NETWORK_CCDEFS = -DUSE_SHARED -I$(dir $(call find_include,pcap)) $(BPF_CONST_STRING)
          NETWORK_FEATURES = - dynamic networking support using libpcap components from www.tcpdump.org and locally installed libpcap.${LIBEXT}
          $(info using libpcap: $(call find_include,pcap))
        else
          LIBPATH += /usr/local/lib
          LIBEXT = a
          ifneq (,$(call find_lib,$(PCAPLIB)))
            $(info using libpcap: $(call find_lib,$(PCAPLIB)) $(call find_include,pcap))
            ifeq (cygwin,$(OSTYPE))
              NETWORK_CCDEFS = -DUSE_NETWORK -DHAVE_PCAP_NETWORK -I$(dir $(call find_include,pcap)) $(BPF_CONST_STRING)
              NETWORK_LDFLAGS = -L$(dir $(call find_lib,$(PCAPLIB))) -Wl,-R,$(dir $(call find_lib,$(PCAPLIB))) -l$(PCAPLIB)
              NETWORK_FEATURES = - static networking support using libpcap components located in the cygwin directories
            else
              NETWORK_CCDEFS := -DUSE_NETWORK -DHAVE_PCAP_NETWORK -isystem -I$(dir $(call find_include,pcap)) $(BPF_CONST_STRING) $(call find_lib,$(PCAPLIB))
              NETWORK_FEATURES = - networking support using libpcap components from www.tcpdump.org
              $(info *** Warning ***)
              $(info *** Warning *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) being built with networking support using)
              $(info *** Warning *** libpcap components from www.tcpdump.org.)
              $(info *** Warning *** Some users have had problems using the www.tcpdump.org libpcap)
              $(info *** Warning *** components for simh networking.  For best results, with)
              $(info *** Warning *** simh networking, it is recommended that you install the)
              $(info *** Warning *** libpcap-dev (or libpcap-devel) package from your $(OSNAME) distribution)
              $(info *** Warning ***)
              $(info *** Warning *** Building with the components manually installed from www.tcpdump.org)
              $(info *** Warning *** is officially deprecated.  Attempting to do so is unsupported.)
              $(info *** Warning ***)
            endif
          else
            $(error using libpcap: $(call find_include,pcap) missing $(PCAPLIB).${LIBEXT})
          endif
          NETWORK_LAN_FEATURES += PCAP
        endif
        LIBEXT = $(LIBEXTSAVE)
      else
        INCPATH = $(INCPATHSAVE)
        $(info *** Warning ***)
        $(info *** Warning *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) $(BUILD_MULTIPLE_VERB) being built WITHOUT)
        $(info *** Warning *** libpcap networking support)
        $(info *** Warning ***)
        $(info *** Warning *** To build simulator(s) with libpcap networking support you)
        ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,apt-get)))
          $(info *** Warning *** should install the libpcap development components for)
          $(info *** Warning *** for your Linux system:)
          $(info *** Warning ***        $$ sudo apt-get install libpcap-dev)
        else
          $(info *** Warning *** should read 0readme_ethernet.txt and follow the instructions)
          $(info *** Warning *** regarding the needed libpcap development components for your)
          $(info *** Warning *** $(OSTYPE) platform)
        endif
        $(info *** Warning ***)
      endif
    endif
    # Consider other network connections
    ifneq (,$(call find_lib,vdeplug))
      # libvdeplug requires the use of the OS provided libpcap
      ifeq (,$(findstring usr/local,$(NETWORK_CCDEFS)))
        ifneq (,$(call find_include,libvdeplug))
          # Provide support for vde networking
          NETWORK_CCDEFS += -DHAVE_VDE_NETWORK
          NETWORK_LAN_FEATURES += VDE
          ifeq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS)))
            NETWORK_CCDEFS += -DUSE_NETWORK
          endif
          ifeq (Darwin,$(OSTYPE))
            NETWORK_LDFLAGS += -lvdeplug -L$(dir $(call find_lib,vdeplug))
          else
            NETWORK_LDFLAGS += -lvdeplug -Wl,-R,$(dir $(call find_lib,vdeplug)) -L$(dir $(call find_lib,vdeplug))
          endif
          $(info using libvdeplug: $(call find_lib,vdeplug) $(call find_include,libvdeplug))
        endif
      endif
    endif
    ifeq (,$(findstring HAVE_VDE_NETWORK,$(NETWORK_CCDEFS)))
      # Support is available on Linux for libvdeplug.  Advise on its usage
      ifneq (,$(findstring Linux,$(OSTYPE))$(findstring Darwin,$(OSTYPE)))
        ifneq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS)))
          $(info *** Info ***)
          $(info *** Info *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) $(BUILD_MULTIPLE_VERB) being built with)
          $(info *** Info *** minimal libpcap networking support)
          $(info *** Info ***)
        endif
        $(info *** Info ***)
        $(info *** Info *** Simulators on your $(OSNAME) platform can also be built with)
        $(info *** Info *** extended LAN Ethernet networking support by using VDE Ethernet.)
        $(info *** Info ***)
        $(info *** Info *** To build simulator(s) with extended networking support you)
        ifeq (Darwin,$(OSTYPE))
          ifeq (/opt/local/bin/port,$(shell which port))
            $(info *** Info *** should install the MacPorts vde2 package to provide this)
            $(info *** Info *** functionality for your OS X system:)
            $(info *** Info ***       # port install vde2)
          endif
          ifeq (/usr/local/bin/brew,$(shell which brew))
            ifeq (/opt/local/bin/port,$(shell which port))
              $(info *** Info ***)
              $(info *** Info *** OR)
              $(info *** Info ***)
            endif
            $(info *** Info *** should install the HomeBrew vde package to provide this)
            $(info *** Info *** functionality for your OS X system:)
            $(info *** Info ***       $$ brew install vde)
          else
            ifeq (,$(shell which port))
              $(info *** Info *** should install MacPorts or HomeBrew and rerun this make for)
              $(info *** Info *** specific advice)
            endif
          endif
        else
          ifneq (,$(and $(findstring Linux,$(OSTYPE)),$(call find_exe,apt-get)))
            $(info *** Info *** should install the vde2 package to provide this)
            $(info *** Info *** functionality for your $(OSNAME) system:)
            ifneq (,$(shell apt list 2>/dev/null| grep libvdeplug-dev))
              $(info *** Info ***        $$ sudo apt-get install libvdeplug-dev)
            else
              $(info *** Info ***        $$ sudo apt-get install vde2)
            endif
          else
            $(info *** Info *** should read 0readme_ethernet.txt and follow the instructions)
            $(info *** Info *** regarding the needed libvdeplug components for your $(OSNAME))
            $(info *** Info *** platform)
          endif
        endif
        $(info *** Info ***)
      endif
    endif
    ifneq (,$(call find_include,linux/if_tun))
      # Provide support for Tap networking on Linux
      NETWORK_CCDEFS += -DHAVE_TAP_NETWORK
      NETWORK_LAN_FEATURES += TAP
      ifeq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS)))
        NETWORK_CCDEFS += -DUSE_NETWORK
      endif
    endif
    ifeq (bsdtuntap,$(shell if ${TEST} -e /usr/include/net/if_tun.h -o -e /Library/Extensions/tap.kext -o -e /Applications/Tunnelblick.app/Contents/Resources/tap-notarized.kext; then echo bsdtuntap; fi))
      # Provide support for Tap networking on BSD platforms (including OS X)
      NETWORK_CCDEFS += -DHAVE_TAP_NETWORK -DHAVE_BSDTUNTAP
      NETWORK_LAN_FEATURES += TAP
      ifeq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS)))
        NETWORK_CCDEFS += -DUSE_NETWORK
      endif
    endif
    ifeq (slirp,$(shell if ${TEST} -e slirp_glue/sim_slirp.c; then echo slirp; fi))
      NETWORK_CCDEFS += -Islirp -Islirp_glue -Islirp_glue/qemu -DHAVE_SLIRP_NETWORK -DUSE_SIMH_SLIRP_DEBUG slirp/*.c slirp_glue/*.c
      NETWORK_LAN_FEATURES += NAT(SLiRP)
    endif
    ifeq (,$(findstring USE_NETWORK,$(NETWORK_CCDEFS))$(findstring USE_SHARED,$(NETWORK_CCDEFS))$(findstring HAVE_VDE_NETWORK,$(NETWORK_CCDEFS)))
      NETWORK_CCDEFS += -DUSE_NETWORK
      NETWORK_FEATURES = - WITHOUT Local LAN networking support
      $(info *** Warning ***)
      $(info *** Warning *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) $(BUILD_MULTIPLE_VERB) being built WITHOUT LAN networking support)
      $(info *** Warning ***)
      $(info *** Warning *** To build simulator(s) with networking support you should read)
      $(info *** Warning *** 0readme_ethernet.txt and follow the instructions regarding the)
      $(info *** Warning *** needed libpcap components for your $(OSTYPE) platform)
      $(info *** Warning ***)
    endif
    NETWORK_OPT = $(NETWORK_CCDEFS)
  endif
  ifneq (binexists,$(shell if ${TEST} -e BIN/buildtools; then echo binexists; fi))
    MKDIRBIN = @mkdir -p BIN/buildtools
  endif
  ifeq (commit-id-exists,$(shell if ${TEST} -e .git-commit-id; then echo commit-id-exists; fi))
    GIT_COMMIT_ID=$(shell grep 'SIM_GIT_COMMIT_ID' .git-commit-id | awk '{ print $$2 }')
    GIT_COMMIT_TIME=$(shell grep 'SIM_GIT_COMMIT_TIME' .git-commit-id | awk '{ print $$2 }')
  else
    ifeq (,$(shell grep 'define SIM_GIT_COMMIT_ID' sim_rev.h | grep 'Format:'))
      GIT_COMMIT_ID=$(shell grep 'define SIM_GIT_COMMIT_ID' sim_rev.h | awk '{ print $$3 }')
      GIT_COMMIT_TIME=$(shell grep 'define SIM_GIT_COMMIT_TIME' sim_rev.h | awk '{ print $$3 }')
    else
      ifeq (git-submodule,$(if $(shell cd .. ; git rev-parse --git-dir 2>/dev/null),git-submodule))
        GIT_COMMIT_ID=$(shell cd .. ; git submodule status | grep " $(notdir $(realpath .)) " | awk '{ print $$1 }')
        GIT_COMMIT_TIME=$(shell git --git-dir=$(realpath .)/.git log $(GIT_COMMIT_ID) -1 --pretty="%aI")
      else
        $(info *** Error ***)
        $(info *** Error *** The simh git commit id can not be determined.)
        $(info *** Error ***)
        $(info *** Error *** There are ONLY two supported ways to acquire and build)
        $(info *** Error *** the simh source code:)
        $(info *** Error ***   1: directly with git via:)
        $(info *** Error ***      $$ git clone https://github.com/rcornwell/sims)
        $(info *** Error ***      $$ cd simh)
        $(info *** Error ***      $$ make {simulator-name})
        $(info *** Error *** OR)
        $(info *** Error ***   2: download the source code zip archive from:)
        $(info *** Error ***      $$ wget(or via browser) https://github.com/rcornwell/sims/archive/master.zip)
        $(info *** Error ***      $$ unzip master.zip)
        $(info *** Error ***      $$ cd simh-master)
        $(info *** Error ***      $$ make {simulator-name})
        $(info *** Error ***)
        $(error get simh source either with zip download or git clone)
      endif
    endif
  endif
else
  #Win32 Environments (via MinGW32)
  GCC := gcc
  GCC_Path := $(abspath $(dir $(word 1,$(wildcard $(addsuffix /${GCC}.exe,$(subst ;, ,${PATH}))))))
  ifeq (rename-build-support,$(shell if exist ..\windows-build-windows-build echo rename-build-support))
    REMOVE_OLD_BUILD := $(shell if exist ..\windows-build rmdir/s/q ..\windows-build)
    FIXED_BUILD := $(shell move ..\windows-build-windows-build ..\windows-build >NUL)
  endif
  GCC_VERSION = $(word 3,$(shell ${GCC} --version))
  COMPILER_NAME = GCC Version: $(GCC_VERSION)
  ifeq (,$(findstring ++,${GCC}))
    CC_STD = -std=gnu99
  else
    CPP_BUILD = 1
  endif
  LTO_EXCLUDE_VERSIONS = 4.5.2
  ifeq (,$(PATH_SEPARATOR))
    PATH_SEPARATOR := ;
  endif
  INCPATH = $(abspath $(wildcard $(GCC_Path)\..\include $(subst $(PATH_SEPARATOR), ,$(CPATH))  $(subst $(PATH_SEPARATOR), ,$(C_INCLUDE_PATH))))
  LIBPATH = $(abspath $(wildcard $(GCC_Path)\..\lib $(subst :, ,$(LIBRARY_PATH))))
  $(info lib paths are: ${LIBPATH})
  $(info include paths are: ${INCPATH})
  # Give preference to any MinGW provided threading (if available)
  ifneq (,$(call find_include,pthread))
    PTHREADS_CCDEFS =
    AIO_CCDEFS = -DUSE_READER_THREAD -DSIM_ASYNCH_IO
    PTHREADS_LDFLAGS = -lpthread
  else
    ifeq (pthreads,$(shell if exist ..\windows-build\pthreads\Pre-built.2\include\pthread.h echo pthreads))
      PTHREADS_CCDEFS = -DPTW32_STATIC_LIB -D_POSIX_C_SOURCE -I../windows-build/pthreads/Pre-built.2/include
      AIO_CCDEFS = -DUSE_READER_THREAD -DSIM_ASYNCH_IO
      PTHREADS_LDFLAGS = -lpthreadGC2 -L..\windows-build\pthreads\Pre-built.2\lib
    endif
  endif
  ifeq (pcap,$(shell if exist ..\windows-build\winpcap\Wpdpack\include\pcap.h echo pcap))
    NETWORK_LDFLAGS =
    NETWORK_OPT = -DUSE_SHARED -I../windows-build/winpcap/Wpdpack/include
    NETWORK_FEATURES = - dynamic networking support using windows-build provided libpcap components
    NETWORK_LAN_FEATURES += PCAP
  else
    ifneq (,$(call find_include,pcap))
      NETWORK_LDFLAGS =
      NETWORK_OPT = -DUSE_SHARED
      NETWORK_FEATURES = - dynamic networking support using libpcap components found in the MinGW directories
      NETWORK_LAN_FEATURES += PCAP
    endif
  endif
  ifneq (,$(VIDEO_USEFUL))
    SDL_INCLUDE = $(word 1,$(shell dir /b /s ..\windows-build\libSDL\SDL.h))
    ifeq (SDL.h,$(findstring SDL.h,$(SDL_INCLUDE)))
      VIDEO_CCDEFS += -DHAVE_LIBSDL -DUSE_SIM_VIDEO -I$(abspath $(dir $(SDL_INCLUDE)))
      ifneq ($(DEBUG),)
        VIDEO_LDFLAGS  += $(abspath $(dir $(SDL_INCLUDE))\..\..\..\lib\lib-VC2008\Debug)/SDL2.lib
      else
        VIDEO_LDFLAGS  += $(abspath $(dir $(SDL_INCLUDE))\..\..\..\lib\lib-VC2008\Release)/SDL2.lib
      endif
      VIDEO_FEATURES = - video capabilities provided by libSDL2 (Simple Directmedia Layer)
      DISPLAYL = ${DISPLAYD}/display.c $(DISPLAYD)/sim_ws.c
      DISPLAYVT = ${DISPLAYD}/vt11.c
      DISPLAY340 = ${DISPLAYD}/type340.c
      DISPLAYNG = ${DISPLAYD}/ng.c
      DISPLAY_OPT += -DUSE_DISPLAY $(VIDEO_CCDEFS) $(VIDEO_LDFLAGS)
    else
      $(info ***********************************************************************)
      $(info ***********************************************************************)
      $(info **  This build could produce simulators with video capabilities.     **)
      $(info **  However, the required files to achieve this can not be found on  **)
      $(info **  on this system.  Download the file:                              **)
      $(info **  https://github.com/simh/windows-build/archive/windows-build.zip  **)
      $(info **  Extract the windows-build-windows-build folder it contains to    **)
      $(info **  $(abspath ..\)                                                   **)
      $(info ***********************************************************************)
      $(info ***********************************************************************)
      $(info .)
    endif
  endif
  OS_CCDEFS += -fms-extensions $(PTHREADS_CCDEFS)
  OS_LDFLAGS += -lm -lwsock32 -lwinmm $(PTHREADS_LDFLAGS)
  EXE = .exe
  ifneq (clean,${MAKECMDGOALS})
    ifneq (buildtoolsexists,$(shell if exist BIN\buildtools (echo buildtoolsexists) else (mkdir BIN\buildtools)))
      MKDIRBIN=
    endif
  endif
  ifneq ($(USE_NETWORK),)
    NETWORK_OPT += -DUSE_SHARED
  endif
  ifeq (git-repo,$(shell if exist .git echo git-repo))
    GIT_PATH := $(shell where git)
    ifeq (,$(GIT_PATH))
      $(error building using a git repository, but git is not available)
    endif
    ifeq (commit-id-exists,$(shell if exist .git-commit-id echo commit-id-exists))
      CURRENT_GIT_COMMIT_ID=$(shell for /F "tokens=2" %%i in ("$(shell findstr /C:"SIM_GIT_COMMIT_ID" .git-commit-id)") do echo %%i)
      ifneq (, $(shell git update-index --refresh --))
        ACTUAL_GIT_COMMIT_EXTRAS=+uncommitted-changes
      endif
      ACTUAL_GIT_COMMIT_ID=$(strip $(shell git log -1 --pretty=%H))$(ACTUAL_GIT_COMMIT_EXTRAS)
      ifneq ($(CURRENT_GIT_COMMIT_ID),$(ACTUAL_GIT_COMMIT_ID))
        NEED_COMMIT_ID = need-commit-id
        # make sure that the invalidly formatted .git-commit-id file wasn't generated
        # by legacy git hooks which need to be removed.
        $(shell if exist .git\hooks\post-checkout del .git\hooks\post-checkout)
        $(shell if exist .git\hooks\post-commit   del .git\hooks\post-commit)
        $(shell if exist .git\hooks\post-merge    del .git\hooks\post-merge)
      endif
    else
      NEED_COMMIT_ID = need-commit-id
    endif
    ifeq (need-commit-id,$(NEED_COMMIT_ID))
      ifneq (, $(shell git update-index --refresh --))
        ACTUAL_GIT_COMMIT_EXTRAS=+uncommitted-changes
      endif
      ACTUAL_GIT_COMMIT_ID=$(strip $(shell git log -1 --pretty=%H))$(ACTUAL_GIT_COMMIT_EXTRAS)
      isodate=$(shell git log -1 --pretty=%ai)
      commit_time=$(word 1,$(isodate))T$(word 2,$(isodate))$(word 3,$(isodate))
      $(shell echo SIM_GIT_COMMIT_ID $(ACTUAL_GIT_COMMIT_ID)>.git-commit-id)
      $(shell echo SIM_GIT_COMMIT_TIME $(commit_time)>>.git-commit-id)
    endif
  endif
  ifneq (,$(shell if exist .git-commit-id echo git-commit-id))
    GIT_COMMIT_ID=$(shell for /F "tokens=2" %%i in ("$(shell findstr /C:"SIM_GIT_COMMIT_ID" .git-commit-id)") do echo %%i)
    GIT_COMMIT_TIME=$(shell for /F "tokens=2" %%i in ("$(shell findstr /C:"SIM_GIT_COMMIT_TIME" .git-commit-id)") do echo %%i)
  else
    ifeq (,$(shell findstr /C:"define SIM_GIT_COMMIT_ID" sim_rev.h | findstr Format))
      GIT_COMMIT_ID=$(shell for /F "tokens=3" %%i in ("$(shell findstr /C:"define SIM_GIT_COMMIT_ID" sim_rev.h)") do echo %%i)
      GIT_COMMIT_TIME=$(shell for /F "tokens=3" %%i in ("$(shell findstr /C:"define SIM_GIT_COMMIT_TIME" sim_rev.h)") do echo %%i)
    endif
  endif
  ifneq (windows-build,$(shell if exist ..\windows-build\README.md echo windows-build))
    ifneq (,$(GIT_PATH))
      $(info Cloning the windows-build dependencies into $(abspath ..)/windows-build)
      $(shell git clone https://github.com/simh/windows-build ../windows-build)
    else
      ifneq (3,${SIM_MAJOR})
        $(info ***********************************************************************)
        $(info ***********************************************************************)
        $(info **  This build is operating without the required windows-build       **)
        $(info **  components and therefore will produce less than optimal          **)
        $(info **  simulator operation and features.                                **)
        $(info **  Download the file:                                               **)
        $(info **  https://github.com/simh/windows-build/archive/windows-build.zip  **)
        $(info **  Extract the windows-build-windows-build folder it contains to    **)
        $(info **  $(abspath ..\)                                                   **)
        $(info ***********************************************************************)
        $(info ***********************************************************************)
        $(info .)
      endif
    endif
  else
    # Version check on windows-build
    WINDOWS_BUILD = $(word 2,$(shell findstr WINDOWS-BUILD ..\windows-build\Windows-Build_Versions.txt))
    ifeq (,$(WINDOWS_BUILD))
      WINDOWS_BUILD = 00000000
    endif
    ifneq (,$(or $(shell if 20190124 GTR $(WINDOWS_BUILD) echo old-windows-build),$(and $(shell if 20171112 GTR $(WINDOWS_BUILD) echo old-windows-build),$(findstring pthreadGC2,$(PTHREADS_LDFLAGS)))))
      $(info .)
      $(info windows-build components at: $(abspath ..\windows-build))
      $(info .)
      $(info ***********************************************************************)
      $(info ***********************************************************************)
      $(info **  This currently available windows-build components are out of     **)
      ifneq (,$(GIT_PATH))
        $(info **  date.  You need to update to the latest windows-build            **)
        $(info **  dependencies by executing these commands:                        **)
        $(info **                                                                   **)
        $(info **    > cd ..\windows-build                                          **)
        $(info **    > git pull                                                     **)
        $(info **                                                                   **)
        $(info ***********************************************************************)
        $(info ***********************************************************************)
        $(error .)
      else
        $(info **  date.  For the most functional and stable features you shoud     **)
        $(info **  Download the file:                                               **)
        $(info **  https://github.com/simh/windows-build/archive/windows-build.zip  **)
        $(info **  Extract the windows-build-windows-build folder it contains to    **)
        $(info **  $(abspath ..\)                                                   **)
        $(info ***********************************************************************)
        $(info ***********************************************************************)
        $(info .)
        $(error Update windows-build)
      endif
    endif
    ifeq (pcre,$(shell if exist ..\windows-build\PCRE\include\pcre.h echo pcre))
      OS_CCDEFS += -DHAVE_PCRE_H -DPCRE_STATIC -I$(abspath ../windows-build/PCRE/include)
      OS_LDFLAGS += -lpcre -L../windows-build/PCRE/lib/
      $(info using libpcre: $(abspath ../windows-build/PCRE/lib/pcre.a) $(abspath ../windows-build/PCRE/include/pcre.h))
    endif
    ifeq (slirp,slirp)
      NETWORK_OPT += -Islirp -Islirp_glue -Islirp_glue/qemu -DHAVE_SLIRP_NETWORK -DUSE_SIMH_SLIRP_DEBUG slirp/*.c slirp_glue/*.c -lIphlpapi
      NETWORK_LAN_FEATURES += NAT(SLiRP)
    endif
  endif
  ifneq (,$(call find_include,ddk/ntdddisk))
    CFLAGS_I = -DHAVE_NTDDDISK_H
  endif
endif # Win32 (via MinGW)
ifneq (,$(GIT_COMMIT_ID))
  CFLAGS_GIT = -DSIM_GIT_COMMIT_ID=$(GIT_COMMIT_ID)
endif
ifneq (,$(GIT_COMMIT_TIME))
  CFLAGS_GIT += -DSIM_GIT_COMMIT_TIME=$(GIT_COMMIT_TIME)
endif
ifneq (,$(UNSUPPORTED_BUILD))
  CFLAGS_GIT += -DSIM_BUILD=Unsupported=$(UNSUPPORTED_BUILD)
endif
OPTIMIZE ?= -O2 -DNDEBUG=1
ifneq ($(DEBUG),)
  CFLAGS_G = -g -ggdb -g3 -D_DEBUG=1
  CFLAGS_O = -O0
  BUILD_FEATURES = - debugging support
  LTO =
else
  ifneq (,$(findstring clang,$(COMPILER_NAME))$(findstring LLVM,$(COMPILER_NAME)))
    CFLAGS_O = $(OPTIMIZE) -fno-strict-overflow
    GCC_OPTIMIZERS_CMD = ${GCC} --help 2>&1
  else
    CFLAGS_O := $(OPTIMIZE)
  endif
  LDFLAGS_O = 
  GCC_MAJOR_VERSION = $(firstword $(subst  ., ,$(GCC_VERSION)))
  ifneq (3,$(GCC_MAJOR_VERSION))
    ifeq (,$(GCC_OPTIMIZERS_CMD))
      GCC_OPTIMIZERS_CMD = ${GCC} --help=optimizers
      GCC_COMMON_CMD = ${GCC} --help=common
    endif
  endif
  ifneq (,$(GCC_OPTIMIZERS_CMD))
    GCC_OPTIMIZERS = $(shell $(GCC_OPTIMIZERS_CMD))
  endif
  ifneq (,$(GCC_COMMON_CMD))
    GCC_OPTIMIZERS += $(shell $(GCC_COMMON_CMD))
  endif
  ifneq (,$(findstring -finline-functions,$(GCC_OPTIMIZERS)))
    CFLAGS_O += -finline-functions
  endif
  ifneq (,$(findstring -fgcse-after-reload,$(GCC_OPTIMIZERS)))
    CFLAGS_O += -fgcse-after-reload
  endif
  ifneq (,$(findstring -fpredictive-commoning,$(GCC_OPTIMIZERS)))
    CFLAGS_O += -fpredictive-commoning
  endif
  ifneq (,$(findstring -fipa-cp-clone,$(GCC_OPTIMIZERS)))
    CFLAGS_O += -fipa-cp-clone
  endif
  ifneq (,$(findstring -funsafe-loop-optimizations,$(GCC_OPTIMIZERS)))
    CFLAGS_O += -fno-unsafe-loop-optimizations
  endif
  ifneq (,$(findstring -fstrict-overflow,$(GCC_OPTIMIZERS)))
    CFLAGS_O += -fno-strict-overflow
  endif
  ifneq (,$(findstring $(GCC_VERSION),$(LTO_EXCLUDE_VERSIONS)))
    override LTO =
  endif
  ifneq (,$(LTO))
    ifneq (,$(findstring -flto,$(GCC_OPTIMIZERS)))
      CFLAGS_O += -flto
      LTO_FEATURE = , with Link Time Optimization,
    endif
  endif
  BUILD_FEATURES = - compiler optimizations$(LTO_FEATURE) and no debugging support
endif
ifneq (3,$(GCC_MAJOR_VERSION))
  ifeq (,$(GCC_WARNINGS_CMD))
    GCC_WARNINGS_CMD = ${GCC} --help=warnings
  endif
endif
ifneq (clean,${MAKECMDGOALS})
  BUILD_FEATURES := $(BUILD_FEATURES). $(COMPILER_NAME)
  $(info ***)
  $(info *** $(BUILD_SINGLE)Simulator$(BUILD_MULTIPLE) being built with:)
  $(info *** $(BUILD_FEATURES).)
  ifneq (,$(NETWORK_FEATURES))
    $(info *** $(NETWORK_FEATURES).)
  endif
  ifneq (,$(NETWORK_LAN_FEATURES))
    $(info *** - Local LAN packet transports: $(NETWORK_LAN_FEATURES))
  endif
  ifneq (,$(VIDEO_FEATURES))
    $(info *** $(VIDEO_FEATURES).)
  endif
  ifneq (,$(TESTING_FEATURES))
    $(info *** $(TESTING_FEATURES).)
  endif
  ifneq (,$(GIT_COMMIT_ID))
    $(info ***)
    $(info *** git commit id is $(GIT_COMMIT_ID).)
    $(info *** git commit time is $(GIT_COMMIT_TIME).)
  endif
  $(info ***)
endif
ifneq ($(DONT_USE_ROMS),)
  ROMS_OPT = -DDONT_USE_INTERNAL_ROM
else
  BUILD_ROMS = ${BIN}buildtools/BuildROMs${EXE}
endif
ifneq ($(DONT_USE_READER_THREAD),)
  NETWORK_OPT += -DDONT_USE_READER_THREAD
endif

CC_OUTSPEC = -o $@
CC := ${GCC} ${CC_STD} -U__STRICT_ANSI__ ${CFLAGS_G} ${CFLAGS_O} ${CFLAGS_GIT} ${CFLAGS_I} -DSIM_COMPILER="${COMPILER_NAME}" -DSIM_BUILD_TOOL=simh-makefile -I . ${OS_CCDEFS} ${ROMS_OPT}
ifneq (,${SIM_VERSION_MODE})
  CC += -DSIM_VERSION_MODE="${SIM_VERSION_MODE}"
endif
LDFLAGS := ${OS_LDFLAGS} ${NETWORK_LDFLAGS} ${LDFLAGS_O}


#
# Common Libraries
#
BIN = BIN/
SIMHD = .
SIM = ${SIMHD}/scp.c ${SIMHD}/sim_console.c ${SIMHD}/sim_fio.c \
	${SIMHD}/sim_timer.c ${SIMHD}/sim_sock.c ${SIMHD}/sim_tmxr.c \
	${SIMHD}/sim_ether.c ${SIMHD}/sim_tape.c ${SIMHD}/sim_disk.c \
	${SIMHD}/sim_serial.c ${SIMHD}/sim_video.c ${SIMHD}/sim_imd.c \
	${SIMHD}/sim_card.c

DISPLAYD = ${SIMHD}/display


I7000D = ${SIMHD}/I7000
I7090 = ${I7000D}/i7090_cpu.c ${I7000D}/i7090_sys.c ${I7000D}/i7090_chan.c \
	${I7000D}/i7090_cdr.c ${I7000D}/i7090_cdp.c ${I7000D}/i7090_lpr.c \
	${I7000D}/i7000_chan.c ${I7000D}/i7000_mt.c ${I7000D}/i7090_drum.c \
	${I7000D}/i7090_hdrum.c ${I7000D}/i7000_chron.c ${I7000D}/i7000_dsk.c \
	${I7000D}/i7000_com.c ${I7000D}/i7000_ht.c 
I7090_OPT = -I $(I7000D) -DUSE_INT64 -DI7090 -DUSE_SIM_CARD

I7080D = ${SIMHD}/I7000
I7080 = ${I7000D}/i7080_cpu.c ${I7000D}/i7080_sys.c ${I7000D}/i7080_chan.c \
	${I7000D}/i7080_drum.c ${I7000D}/i7000_cdp.c ${I7000D}/i7000_cdr.c \
	${I7000D}/i7000_con.c ${I7000D}/i7000_chan.c ${I7000D}/i7000_lpr.c \
	${I7000D}/i7000_mt.c ${I7000D}/i7000_chron.c ${I7000D}/i7000_dsk.c \
	${I7000D}/i7000_com.c ${I7000D}/i7000_ht.c 
I7080_OPT = -I $(I7000D) -DI7080 -DUSE_SIM_CARD

I7070D = ${SIMHD}/I7000
I7070 = ${I7000D}/i7070_cpu.c ${I7000D}/i7070_sys.c ${I7000D}/i7070_chan.c \
	${I7000D}/i7000_cdp.c ${I7000D}/i7000_cdr.c ${I7000D}/i7000_con.c \
	${I7000D}/i7000_chan.c ${I7000D}/i7000_lpr.c ${I7000D}/i7000_mt.c \
	${I7000D}/i7000_chron.c ${I7000D}/i7000_dsk.c ${I7000D}/i7000_com.c \
	${I7000D}/i7000_ht.c 
I7070_OPT = -I $(I7000D) -DUSE_INT64 -DI7070 -DUSE_SIM_CARD

I7010D = ${SIMHD}/I7000
I7010 = ${I7000D}/i7010_cpu.c ${I7000D}/i7010_sys.c ${I7000D}/i7010_chan.c \
	${I7000D}/i7000_cdp.c ${I7000D}/i7000_cdr.c ${I7000D}/i7000_con.c \
	${I7000D}/i7000_chan.c ${I7000D}/i7000_lpr.c ${I7000D}/i7000_mt.c \
	${I7000D}/i7000_chron.c ${I7000D}/i7000_dsk.c ${I7000D}/i7000_com.c \
	${I7000D}/i7000_ht.c 
I7010_OPT = -I $(I7010D) -DI7010 -DUSE_SIM_CARD

I704D  = ${SIMHD}/I7000
I704   = ${I7000D}/i7090_cpu.c ${I7000D}/i7090_sys.c ${I7000D}/i7090_chan.c \
	 ${I7000D}/i7090_cdr.c ${I7000D}/i7090_cdp.c ${I7000D}/i7090_lpr.c \
	 ${I7000D}/i7000_mt.c ${I7000D}/i7090_drum.c ${I7000D}/i7000_chan.c 
I704_OPT = -I $(I7000D) -DUSE_INT64 -DI704 -DUSE_SIM_CARD


I701D  = ${SIMHD}/I7000
I701   = ${I7000D}/i701_cpu.c ${I7000D}/i701_sys.c ${I7000D}/i701_chan.c \
	 ${I7000D}/i7090_cdr.c ${I7000D}/i7090_cdp.c ${I7000D}/i7090_lpr.c \
	 ${I7000D}/i7000_mt.c ${I7000D}/i7090_drum.c ${I7000D}/i7000_chan.c 
I701_OPT = -I $(I7000D) -DUSE_INT64 -DI701 -DUSE_SIM_CARD


I7094D = ${SIMHD}/I7094
I7094 = ${I7094D}/i7094_cpu.c ${I7094D}/i7094_cpu1.c ${I7094D}/i7094_io.c \
	${I7094D}/i7094_cd.c ${I7094D}/i7094_clk.c ${I7094D}/i7094_com.c \
	${I7094D}/i7094_drm.c ${I7094D}/i7094_dsk.c ${I7094D}/i7094_sys.c \
	${I7094D}/i7094_lp.c ${I7094D}/i7094_mt.c ${I7094D}/i7094_binloader.c
I7094_OPT = -DUSE_INT64 -I ${I7094D}


B5500D = ${SIMHD}/B5500
B5500 = ${B5500D}/b5500_cpu.c ${B5500D}/b5500_io.c ${B5500D}/b5500_sys.c \
	${B5500D}/b5500_dk.c ${B5500D}/b5500_mt.c ${B5500D}/b5500_urec.c \
	${B5500D}/b5500_dr.c ${B5500D}/b5500_dtc.c
B5500_OPT = -I.. -DUSE_INT64 -DB5500 -DUSE_SIM_CARD

SEL32D = ${SIMHD}/SEL32
SEL32 = ${SEL32D}/sel32_cpu.c ${SEL32D}/sel32_sys.c ${SEL32D}/sel32_chan.c \
	${SEL32D}/sel32_iop.c ${SEL32D}/sel32_com.c ${SEL32D}/sel32_con.c \
	${SEL32D}/sel32_clk.c ${SEL32D}/sel32_mt.c ${SEL32D}/sel32_lpr.c \
	${SEL32D}/sel32_scfi.c ${SEL32D}/sel32_fltpt.c ${SEL32D}/sel32_disk.c \
	${SEL32D}/sel32_hsdp.c ${SEL32D}/sel32_mfp.c ${SEL32D}/sel32_scsi.c \
	${SEL32D}/sel32_ec.c ${SEL32D}/sel32_ipu.c
SEL32_OPT = -I ${SEL32D} -DSEL32  ${NETWORK_OPT}

ICL1900D = ${SIMHD}/ICL1900
ICL1900 = ${ICL1900D}/icl1900_cpu.c ${ICL1900D}/icl1900_sys.c \
	${ICL1900D}/icl1900_stdio.c ${ICL1900D}/icl1900_cty.c \
	${ICL1900D}/icl1900_tr.c ${ICL1900D}/icl1900_tp.c \
	${ICL1900D}/icl1900_cr.c ${ICL1900D}/icl1900_cp.c \
	${ICL1900D}/icl1900_lp.c ${ICL1900D}/icl1900_mta.c \
	${ICL1900D}/icl1900_mt.c ${ICL1900D}/icl1900_eds8.c
ICL1900_OPT = -I ${ICL1900D} -DICL1900 -DUSE_SIM_CARD

IBM360D = ${SIMHD}/IBM360
IBM360 = ${IBM360D}/ibm360_cpu.c ${IBM360D}/ibm360_sys.c ${IBM360D}/ibm360_con.c \
	${IBM360D}/ibm360_chan.c ${IBM360D}/ibm360_cdr.c ${IBM360D}/ibm360_cdp.c \
	${IBM360D}/ibm360_mt.c ${IBM360D}/ibm360_lpr.c ${IBM360D}/ibm360_dasd.c \
	${IBM360D}/ibm360_com.c ${IBM360D}/ibm360_scom.c ${IBM360D}/ibm360_scon.c \
    ${IBM360D}/ibm360_vma.c
IBM360_OPT = -I ${IBM360D} -DIBM360 -DUSE_INT64 -DUSE_SIM_CARD 

PDP6D = ${SIMHD}/PDP10
ifneq (,${DISPLAY_OPT})
  PDP6_DISPLAY_OPT = 
endif
PDP6 = ${PDP6D}/kx10_cpu.c ${PDP6D}/kx10_sys.c ${PDP6D}/kx10_cty.c \
	${PDP6D}/kx10_lp.c ${PDP6D}/kx10_pt.c ${PDP6D}/kx10_cr.c \
	${PDP6D}/kx10_cp.c ${PDP6D}/pdp6_dct.c ${PDP6D}/pdp6_dtc.c \
	${PDP6D}/pdp6_mtc.c ${PDP6D}/pdp6_dsk.c ${PDP6D}/pdp6_dcs.c \
	${PDP6D}/kx10_dpy.c ${PDP6D}/pdp6_slave.c ${PDP6D}/pdp6_ge.c \
	${DISPLAYL} ${DISPLAY340}
PDP6_OPT = -DPDP6=1 -DUSE_INT64 -I ${PDP6D} -DUSE_SIM_CARD ${DISPLAY_OPT} ${PDP6_DISPLAY_OPT} ${AIO_CCDEFS}
ifneq (${PANDA_LIGHTS},)
# ONLY for Panda display.
PDP6_OPT += -DPANDA_LIGHTS
PDP6 += ${PDP6D}/kx10_lights.c
PDP6_LDFLAGS += -lusb-1.0
endif
ifneq (${PIDP10},)
PDP6_OPT += -DPIDP10=1
PDP6 += ${PDP6D}/ka10_pipanel.c
endif

PDP10D = ${SIMHD}/PDP10
KA10D = ${SIMHD}/PDP10
ifneq (,${DISPLAY_OPT})
  KA10_DISPLAY_OPT = 
endif
KA10 = ${KA10D}/kx10_cpu.c ${KA10D}/kx10_sys.c ${KA10D}/kx10_df.c \
	${KA10D}/kx10_dp.c ${KA10D}/kx10_mt.c ${KA10D}/kx10_cty.c \
	${KA10D}/kx10_lp.c ${KA10D}/kx10_pt.c ${KA10D}/kx10_dc.c \
	${KA10D}/kx10_rp.c ${KA10D}/kx10_rc.c ${KA10D}/kx10_dt.c \
	${KA10D}/kx10_dk.c ${KA10D}/kx10_cr.c ${KA10D}/kx10_cp.c \
	${KA10D}/kx10_tu.c ${KA10D}/kx10_rs.c ${KA10D}/ka10_pd.c \
	${KA10D}/kx10_rh.c ${KA10D}/kx10_imp.c ${KA10D}/ka10_tk10.c \
	${KA10D}/ka10_mty.c ${KA10D}/ka10_imx.c ${KA10D}/ka10_ch10.c \
	${KA10D}/ka10_stk.c ${KA10D}/ka10_ten11.c ${KA10D}/ka10_auxcpu.c \
	${KA10D}/ka10_pmp.c ${KA10D}/ka10_dkb.c ${KA10D}/pdp6_dct.c \
	${KA10D}/pdp6_dtc.c ${KA10D}/pdp6_mtc.c ${KA10D}/pdp6_dsk.c \
	${KA10D}/pdp6_dcs.c ${KA10D}/ka10_dpk.c ${KA10D}/kx10_dpy.c \
	${PDP10D}/ka10_ai.c ${KA10D}/ka10_iii.c ${KA10D}/kx10_disk.c \
	${PDP10D}/ka10_pclk.c ${PDP10D}/ka10_tv.c ${KA10D}/kx10_ddc.c \
	${PDP10D}/ka10_dd.c ${PDP10D}/ka10_cart.c \
	${DISPLAYL} ${DISPLAY340}
KA10_OPT = -DKA=1 -DUSE_INT64 -I ${KA10D} -DUSE_SIM_CARD ${NETWORK_OPT} ${DISPLAY_OPT} ${KA10_DISPLAY_OPT}
ifneq (${PANDA_LIGHTS},)
# ONLY for Panda display.
KA10_OPT += -DPANDA_LIGHTS
KA10 += ${KA10D}/kx10_lights.c
KA10_LDFLAGS += -lusb-1.0
endif
ifneq (${PIDP10},)
KA10_OPT += -DPIDP10=1
KA10 += ${KA10D}/ka10_pipanel.c
endif

KI10D = ${SIMHD}/PDP10
ifneq (,${DISPLAY_OPT})
KI10_DISPLAY_OPT = 
endif
KI10 = ${KI10D}/kx10_cpu.c ${KI10D}/kx10_sys.c ${KI10D}/kx10_df.c \
	${KI10D}/kx10_dp.c ${KI10D}/kx10_mt.c ${KI10D}/kx10_cty.c \
	${KI10D}/kx10_lp.c ${KI10D}/kx10_pt.c ${KI10D}/kx10_dc.c  \
	${KI10D}/kx10_rh.c ${KI10D}/kx10_rp.c ${KI10D}/kx10_rc.c \
	${KI10D}/kx10_dt.c ${KI10D}/kx10_dk.c ${KI10D}/kx10_cr.c \
	${KI10D}/kx10_cp.c ${KI10D}/kx10_tu.c ${KI10D}/kx10_rs.c \
	${KI10D}/kx10_imp.c ${KI10D}/kx10_dpy.c ${KI10D}/kx10_disk.c \
	${KI10D}/kx10_ddc.c ${KI10D}/kx10_tym.c ${DISPLAYL} ${DISPLAY340}
KI10_OPT = -DKI=1 -DUSE_INT64 -I ${KI10D} -DUSE_SIM_CARD ${NETWORK_OPT} ${DISPLAY_OPT} ${KI10_DISPLAY_OPT}
ifneq (${PANDA_LIGHTS},)
# ONLY for Panda display.
KI10_OPT += -DPANDA_LIGHTS
KI10 += ${KA10D}/kx10_lights.c
KI10_LDFLAGS = -lusb-1.0
endif
ifneq (${PIDP10},)
KI10_OPT += -DPIDP10=1
KI10 += ${KI10D}/ka10_pipanel.c
endif

KL10D = ${SIMHD}/PDP10
KL10 = ${KL10D}/kx10_cpu.c ${KL10D}/kx10_sys.c ${KL10D}/kx10_df.c \
	${KA10D}/kx10_dp.c ${KA10D}/kx10_mt.c ${KA10D}/kx10_lp.c \
    ${KA10D}/kx10_pt.c ${KA10D}/kx10_dc.c ${KL10D}/kx10_rh.c \
    ${KA10D}/kx10_dt.c ${KA10D}/kx10_cr.c ${KA10D}/kx10_cp.c \
	${KL10D}/kx10_rp.c ${KL10D}/kx10_tu.c ${KL10D}/kx10_rs.c \
	${KL10D}/kx10_imp.c ${KL10D}/kl10_fe.c ${KL10D}/ka10_pd.c \
	${KL10D}/ka10_ch10.c ${KL10D}/kl10_nia.c ${KL10D}/kx10_disk.c \
    ${KL10D}/kl10_dn.c
KL10_OPT = -DKL=1 -DUSE_INT64 -I ${KL10D} -DUSE_SIM_CARD ${NETWORK_OPT} 
ifneq (${PIDP10},)
KS10_OPT += -DPIDP10=1
KS10 += ${KS10D}/ka10_pipanel.c
endif

KS10D = ${SIMHD}/PDP10
KS10 = ${KS10D}/kx10_cpu.c ${KS10D}/kx10_sys.c ${KS10D}/kx10_disk.c \
	${KS10D}/ks10_cty.c ${KS10D}/ks10_uba.c ${KS10D}/kx10_rh.c \
	${KS10D}/kx10_rp.c ${KS10D}/kx10_tu.c ${KS10D}/ks10_dz.c \
    ${KS10D}/ks10_tcu.c ${KS10D}/ks10_lp.c ${KS10D}/ks10_ch11.c \
    ${KS10D}/ks10_kmc.c ${KS10D}/ks10_dup.c ${KS10D}/kx10_imp.c
KS10_OPT = -DKS=1 -DUSE_INT64 -I ${KS10D} ${NETWORK_OPT} 
ifneq (${PIDP10},)
KS10_OPT += -DPIDP10=1
KS10 += ${KS10D}/ka10_pipanel.c
endif


#
# Build everything (not the unsupported/incomplete or experimental simulators)
#

ALL = b5500 i701 i704 i7010 i7070 i7080 i7090 pdp10-ka pdp10-ki pdp10-kl pdp10-ks pdp6 ibm360 icl1900 sel32

all : ${ALL}


clean :
ifeq (${WIN32},)
	${RM} -rf ${BIN}
else
	if exist BIN rmdir /s /q BIN
endif

ibm360: ${BIN}ibm360${EXE}

${BIN}ibm360${EXE}: ${IBM360} ${SIM}
	${MKDIRBIN}
	${CC} ${IBM360} ${SIM} ${IBM360_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${IBM360D},ibm360))
	$@ $(call find_test,${IBM360D},ibm360) ${TEST_ARG}
endif

icl1900: ${BIN}icl1900${EXE}

${BIN}icl1900${EXE}: ${ICL1900} ${SIM}
	${MKDIRBIN}
	${CC} ${ICL1900} ${SIM} ${ICL1900_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${ICL1900D},icl1900))
	$@ $(call find_test,${ICL1900D},icl1900) ${TEST_ARG}
endif

sel32: ${BIN}sel32${EXE}

${BIN}sel32${EXE}: ${SEL32} ${SIM}
	${MKDIRBIN}
	${CC} ${SEL32} ${SIM} ${SEL32_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${SEL32D},sel32))
	$@ $(call find_test,${SEL32D},sel32) ${TEST_ARG}
endif

b5500 : ${BIN}b5500${EXE}

${BIN}b5500${EXE} : ${B5500} ${SIM} 
	${MKDIRBIN}
	${CC} ${B5500} ${SIM} ${B5500_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${B5500D},b5500))
	$@ $(call find_test,${B5500D},b5500) ${TEST_ARG}
endif

i7090 : ${BIN}i7090${EXE}

${BIN}i7090${EXE} : ${I7090} ${SIM} 
	${MKDIRBIN}
	${CC} ${I7090} ${SIM} ${I7090_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I7000D},i7090))
	$@ $(call find_test,${I7000D},i7090) ${TEST_ARG}
endif

i7080 : ${BIN}i7080${EXE}

${BIN}i7080${EXE} : ${I7080} ${SIM} 
	${MKDIRBIN}
	${CC} ${I7080} ${SIM} ${I7080_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I7080D},i7080))
	$@ $(call find_test,${I7080D},i7080) ${TEST_ARG}
endif

i7070 : ${BIN}i7070${EXE}

${BIN}i7070${EXE} : ${I7070} ${SIM} 
	${MKDIRBIN}
	${CC} ${I7070} ${SIM} ${I7070_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I7070D},i7070))
	$@ $(call find_test,${I7070D},i7070) ${TEST_ARG}
endif

i7010 : ${BIN}i7010${EXE}

${BIN}i7010${EXE} : ${I7010} ${SIM} 
	${MKDIRBIN}
	${CC} ${I7010} ${SIM} ${I7010_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I7010D},i7010))
	$@ $(call find_test,${I7010D},i7010) ${TEST_ARG}
endif

i704 : ${BIN}i704${EXE}

${BIN}i704${EXE} : ${I704} ${SIM} 
	${MKDIRBIN}
	${CC} ${I704} ${SIM} ${I704_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I704D},i704))
	$@ $(call find_test,${I704D},i704) ${TEST_ARG}
endif

i701 : ${BIN}i701${EXE}

${BIN}i701${EXE} : ${I701} ${SIM} 
	${MKDIRBIN}
	${CC} ${I701} ${SIM} ${I701_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${I701D},i701))
	$@ $(call find_test,${I701D},i701) ${TEST_ARG}
endif

pdp6 : ${BIN}pdp6${EXE}

${BIN}pdp6${EXE} : ${PDP6} ${SIM}
	${MKDIRBIN}
	${CC} ${PDP6} ${PDP6_DPY} ${SIM} ${PDP6_OPT} ${CC_OUTSPEC} ${LDFLAGS} ${PDP6_LDFLAGS}
ifneq (,$(call find_test,${PDP10D},pdp6))
	$@ $(call find_test,${PDP10D},pdp6) ${TEST_ARG}
endif

pdp10-ka : ${BIN}pdp10-ka${EXE}

${BIN}pdp10-ka${EXE} : ${KA10} ${SIM}
	${MKDIRBIN}
	${CC} ${KA10} ${KA10_DPY} ${SIM} ${KA10_OPT} ${CC_OUTSPEC} ${LDFLAGS} ${KA10_LDFLAGS}
ifneq (,$(call find_test,${PDP10D},ka10))
	$@ $(call find_test,${PDP10D},ka10) ${TEST_ARG}
endif

pdp10-ki : ${BIN}pdp10-ki${EXE}

${BIN}pdp10-ki${EXE} : ${KI10} ${SIM}
	${MKDIRBIN}
	${CC} ${KI10} ${KI10_DPY} ${SIM} ${KI10_OPT} ${CC_OUTSPEC} ${LDFLAGS} ${KI10_LDFLAGS}
ifneq (,$(call find_test,${PDP10D},ki10))
	$@ $(call find_test,${PDP10D},ki10) ${TEST_ARG}
endif

pdp10-kl : ${BIN}pdp10-kl${EXE}

${BIN}pdp10-kl${EXE} : ${KL10} ${SIM}
	${MKDIRBIN}
	${CC} ${KL10} ${SIM} ${KL10_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDP10D},kl10))
	$@ $(call find_test,${PDP10D},kl10) ${TEST_ARG}
endif

pdp10-ks : ${BIN}pdp10-ks${EXE}

${BIN}pdp10-ks${EXE} : ${KS10} ${SIM}
	${MKDIRBIN}
	${CC} ${KS10} ${SIM} ${KS10_OPT} ${CC_OUTSPEC} ${LDFLAGS}
ifneq (,$(call find_test,${PDP10D},ks10))
	$@ $(call find_test,${PDP10D},ks10) ${TEST_ARG}
endif


