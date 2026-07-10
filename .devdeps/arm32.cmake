# arm32.cmake — CMake toolchain file for Linux ARMhf (32-bit) cross-compilation.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=.devdeps/arm32.cmake -B build-armhf
#
# Requires the ARMhf cross-compiler:
#   apt install gcc-aarch32-linux-gnu g++-aarch32-linux-gnu
#   OR via Docker with an ARMhf rootfs as sysroot.

# Environment variables:
#   ARM32_ROOTFS=<path>   — root filesystem (include/lib) for ARMhf target.
#
# If ARM32_ROOTFS is set, it is used as sysroot for finding headers/libraries.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_SYSTEM_VERSION 1)

# ARM is little-endian
set(CMAKE_C_BYTE_ORDER LITTLE_ENDIAN)
set(CMAKE_CXX_BYTE_ORDER LITTLE_ENDIAN)

# Compiler selection: try env var, then fallback
set(ARM32_CC "aarch32-linux-gnu-gcc")
set(ARM32_CXX "aarch32-linux-gnu-g++")

if(DEFINED ENV{ARM32_CC})
    set(CMAKE_C_COMPILER $ENV{ARM32_CC})
    set(CMAKE_CXX_COMPILER $ENV{ARM32_CXX})
elseif(CMAKE_C_COMPILER MATCHES "gcc")
    # Already set by CMake; assume host compiler matches target
else()
    message(FATAL_ERROR "ARM32 cross-compiler not found. Install gcc-aarch32-linux-gnu or set ARM32_CC environment variable.")
endif()

set(CMAKE_C_FLAGS_INIT "-mhard-float -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections")

# Sysroot for cross-compilation
if(DEFINED ENV{ARM32_ROOTFS})
    set(CMAKE_SYSROOT $ENV{ARM32_ROOTFS})
    set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} --sysroot=${CMAKE_SYSROOT}")
    set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} --sysroot=${CMAKE_SYSROOT}")
endif()

# Don't search host paths for libraries/headers
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
