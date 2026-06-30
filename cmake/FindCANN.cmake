#[=======================================================================[
FindCANN - Locate Huawei Ascend CANN toolkit

This module looks for:
  ASCEND_TOOLKIT_HOME  - root of the CANN installation
  ASCENDC_COMPILER     - the ascendc kernel compiler
  CANN_INCLUDE_DIRS    - header search paths
  CANN_LIBRARIES       - runtime libraries

Set ASCEND_TOOLKIT_HOME explicitly before calling find_package to skip the env
fallback.
#]=======================================================================]

# --- locate toolkit root ----------------------------------------------------
if(NOT DEFINED ASCEND_TOOLKIT_HOME)
    if(DEFINED ENV{ASCEND_TOOLKIT_HOME})
        set(ASCEND_TOOLKIT_HOME "$ENV{ASCEND_TOOLKIT_HOME}" CACHE PATH "CANN toolkit root")
    elseif(DEFINED ENV{ASCEND_HOME})
        set(ASCEND_TOOLKIT_HOME "$ENV{ASCEND_HOME}" CACHE PATH "CANN toolkit root")
    endif()
endif()

# common install paths
set(_cann_search_hints
    ${ASCEND_TOOLKIT_HOME}
    /usr/local/Ascend
    /opt/Ascend
    "C:/Program Files/Ascend"
    "D:/Ascend"
)

find_path(ASCEND_TOOLKIT_HOME
    NAMES cmake/AscendCCmakeModule.cmake
    HINTS ${_cann_search_hints}
    DOC   "CANN toolkit installation root"
)

if(NOT ASCEND_TOOLKIT_HOME)
    message(FATAL_ERROR
        "CANN toolkit not found. "
        "Download and install CANN from Huawei's support site, then set "
        "ASCEND_TOOLKIT_HOME to its installation root (e.g. /usr/local/Ascend/ascend-toolkit/<version>)."
    )
endif()

# --- locate ascendc compiler ------------------------------------------------
find_program(ASCENDC_COMPILER
    NAMES ascendc
    HINTS ${ASCEND_TOOLKIT_HOME}
    PATH_SUFFIXES tools/ascendc/bin compilers/ascendc/bin bin)
mark_as_advanced(ASCENDC_COMPILER)

# --- include dirs -----------------------------------------------------------
set(CANN_INCLUDE_DIRS
    "${ASCEND_TOOLKIT_HOME}/include"
    "${ASCEND_TOOLKIT_HOME}/ascendc/include"
    "${ASCEND_TOOLKIT_HOME}/opp/built-in/op_proto/inc"
    CACHE PATH "CANN header directories")

# --- libraries --------------------------------------------------------------
find_library(CANN_RUNTIME_LIB
    NAMES ascendcl ascendruntime
    HINTS ${ASCEND_TOOLKIT_HOME}
    PATH_SUFFIXES lib64 lib)

set(CANN_LIBRARIES ${CANN_RUNTIME_LIB} CACHE STRING "CANN runtime libraries")

# --- report -----------------------------------------------------------------
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CANN
    REQUIRED_VARS ASCEND_TOOLKIT_HOME
    VERSION_VAR   CANN_VERSION
)

mark_as_advanced(CANN_RUNTIME_LIB)
