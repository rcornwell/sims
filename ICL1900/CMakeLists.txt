## ICL1900 simulator
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

add_simulator(icl1900
    SOURCES
        icl1900_cpu.c
        icl1900_sys.c
        icl1900_stdio.c
        icl1900_cty.c
        icl1900_tr.c
        icl1900_tp.c
        icl1900_cr.c
        icl1900_cp.c
        icl1900_lp.c
        icl1900_mta.c
        icl1900_mt.c
        icl1900_eds8.c
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
    DEFINES
        ICL1900
    LABEL ICL1900
    PKG_FAMILY icl_family
    TEST icl1900)
