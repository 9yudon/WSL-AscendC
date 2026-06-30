cmake_minimum_required(VERSION 3.16)

project(AscendCWorkspace VERSION 1.0.0 LANGUAGES CXX)

# ---- Detect CANN environment -----------------------------------------------
if(NOT DEFINED ENV{ASCEND_TOOLKIT_HOME})
    message(FATAL_ERROR
        "ASCEND_TOOLKIT_HOME not set.\n"
        "Run: source /usr/local/Ascend/ascend-toolkit/set_env.sh\n"
        "Then re-run cmake."
    )
endif()

set(ASCEND_TOOLKIT_HOME $ENV{ASCEND_TOOLKIT_HOME})

# Required by ascendc.cmake
set(ASCEND_CANN_PACKAGE_PATH "${ASCEND_TOOLKIT_HOME}")
set(ASCENDC_KERNEL_CMAKE_DIR "${ASCEND_TOOLKIT_HOME}/x86_64-linux/tikcpp/ascendc_kernel_cmake")

# Target SoC (ascend310p3 = AI Core m200, works with CPU simulator)
if(NOT DEFINED ENV{SOC_VERSION})
    set(SOC_VERSION "ascend310p3" CACHE STRING "Target NPU architecture")
endif()

message(STATUS "ASCEND_CANN_PACKAGE_PATH = ${ASCEND_CANN_PACKAGE_PATH}")
message(STATUS "SOC_VERSION = ${SOC_VERSION}")

# ---- Build type (required by ascendc merge step) ---------------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING
        "Build type: Debug, Release, RelWithDebInfo, MinSizeRel" FORCE)
endif()
message(STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")

# ---- Load AscendC CMake modules -------------------------------------------
include(${ASCENDC_KERNEL_CMAKE_DIR}/ascendc.cmake)

# ---- Subdirectories --------------------------------------------------------
add_subdirectory(src)

# Host-side test (optional - needs AscendCL runtime)
option(BUILD_TESTS "Build host-side tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
