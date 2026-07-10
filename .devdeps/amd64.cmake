# amd64.cmake — CMake toolchain file for Linux x86_64 cross-compilation.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=.devdeps/amd64.cmake -B build-x86_64
#
# Provides x86_64 target for native Linux builds (same as host) or
# cross-compilation from macOS to x86_64 Linux. Use a plain CMake command
# without toolchain file for native Linux builds.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSTEM_VERSION 1)

# x86_64 is little-endian
set(CMAKE_C_BYTE_ORDER LITTLE_ENDIAN)
set(CMAKE_CXX_BYTE_ORDER LITTLE_ENDIAN)

# Standard x86_64 Linux flags
set(CMAKE_C_FLAGS_INIT "-O2 -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections")

# Use standard GNU/Linux sysroot; development packages (libcurl, OpenSSL, etc.)
# must be available in the native tool chain.
set(CMAKE_FIND_ROOT_PATH /)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
