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
set(B5500D     "${CMAKE_SOURCE_DIR}/B5500")
set(H316D      "${CMAKE_SOURCE_DIR}/H316")
set(I7000D     "${CMAKE_SOURCE_DIR}/I7000")
set(I7010D     "${CMAKE_SOURCE_DIR}/I7000")
set(IBM360D    "${CMAKE_SOURCE_DIR}/IBM360")
set(ICL1900D   "${CMAKE_SOURCE_DIR}/ICL1900")
set(KA10D      "${CMAKE_SOURCE_DIR}/PDP10")
set(KI10D      "${CMAKE_SOURCE_DIR}/PDP10")
set(KL10D      "${CMAKE_SOURCE_DIR}/PDP10")
set(KS10D      "${CMAKE_SOURCE_DIR}/PDP10")
set(PDP10D     "${CMAKE_SOURCE_DIR}/PDP10")
set(PDP6D      "${CMAKE_SOURCE_DIR}/PDP10")
set(SEL32D     "${CMAKE_SOURCE_DIR}/SEL32")

set(DISPLAYD   "${CMAKE_SOURCE_DIR}/display")
set(DISPLAY340 "${DISPLAYD}/type340.c")
set(DISPLAYIII "${DISPLAYD}/iii.c")
set(DISPLAYNG  "${DISPLAYD}/ng.c")
set(DISPLAYVT  "${DISPLAYD}/vt11.c")

## ----------------------------------------

if (NOT WITH_VIDEO)
    ### Hack: Unset these variables so that they don't expand if
    ### not building with video:
    set(DISPLAY340 "")
    set(DISPLAYIII "")
    set(DISPLAYNG  "")
    set(DISPLAYVT  "")
endif ()

## ----------------------------------------

add_subdirectory(B5500)
add_subdirectory(I7000)
add_subdirectory(IBM360)
add_subdirectory(ICL1900)
add_subdirectory(PDP10)
add_subdirectory(SEL32)
