# linux-aarch64.cmake — CMake toolchain file for Linux AArch64 cross-compilation.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=.devdeps/linux-aarch64.cmake \
#         -B build-linux-arm64
#
# Requires the AArch64 cross-compiler:
#   macOS:  brew install aarch64-elf-gcc  (or use a Docker-based sysroot)
#   Linux:  apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#
# Produces a statically-linked Linux ELF binary for AArch64 (e.g. Raspberry Pi,
# AWS Graviton, etc.). For native x86_64 Linux, no toolchain file is needed.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSTEM_VERSION 1)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# AArch64 is little-endian
set(CMAKE_C_BYTE_ORDER LITTLE_ENDIAN)
set(CMAKE_CXX_BYTE_ORDER LITTLE_ENDIAN)

set(CMAKE_C_FLAGS_INIT "-O2 -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections")

# Don't search host paths for libraries/headers
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
