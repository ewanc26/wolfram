# windows.cmake — CMake toolchain file for Windows cross-compilation (MinGW-w64).
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=.devdeps/windows.cmake \
#         -DWOLFRAM_BUILD_WINDOWS=ON \
#         -B build-windows
#
# Requires MinGW-w64:
#   macOS:  brew install mingw-w64
#   Linux:  apt install mingw-w64
#
# Cross-compiles from Linux/macOS to produce a Windows .exe/.dll.
# For native Windows builds, use the standard MSVC or MinGW toolchain
# directly — no toolchain file needed.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSTEM_VERSION 10)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_AR x86_64-w64-mingw32-ar)
set(CMAKE_RANLIB x86_64-w64-mingw32-ranlib)

# Windows is little-endian
set(CMAKE_C_BYTE_ORDER LITTLE_ENDIAN)
set(CMAKE_CXX_BYTE_ORDER LITTLE_ENDIAN)

# MinGW flags
set(CMAKE_C_FLAGS_INIT "-O2 -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -Wl,--gc-sections")

# Don't search host paths for libraries/headers
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
