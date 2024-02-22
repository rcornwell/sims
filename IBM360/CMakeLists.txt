## IBM360 simulator
##
## This is an automagically generated file. Do NOT EDIT.
## Any changes you make will be overwritten!!
##
## Make changes to the SIMH top-level makefile and then run the
## "cmake/generate.py" script to regenerate these files.
##
##     cd cmake; python -m generate --help
##
## ------------------------------------------------------------

if (HAVE_UNITY_FRAMEWORK AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unit-tests/CMakeLists.txt")
  add_subdirectory(unit-tests)
endif ()

add_simulator(ibm360
    SOURCES
        ibm360_cpu.c
        ibm360_sys.c
        ibm360_con.c
        ibm360_chan.c
        ibm360_cdr.c
        ibm360_cdp.c
        ibm360_mt.c
        ibm360_lpr.c
        ibm360_dasd.c
        ibm360_com.c
        ibm360_scom.c
        ibm360_scon.c
        ibm360_vma.c
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
    DEFINES
        IBM360
        DONT_USE_AIO_INTRINSICS
    FEATURE_INT64
    LABEL IBM360
    PKG_FAMILY ibm_family
    TEST ibm360)
