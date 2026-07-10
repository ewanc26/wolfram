# wii.cmake — CMake toolchain file for Nintendo Wii cross-compilation.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=.devdeps/wii.cmake \
#         -DWOLFRAM_BUILD_WII=ON \
#         -B build-wii
#
# Requires devkitPro with devkitPPC and libogc installed.
# Set DEVKITPRO=/opt/devkitpro before invoking cmake.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc)
set(CMAKE_SYSTEM_VERSION 1)

# PowerPC is big-endian — skip test_big_endian (can't run cross-compiled binaries)
set(CMAKE_C_BYTE_ORDER BIG_ENDIAN)
set(CMAKE_CXX_BYTE_ORDER BIG_ENDIAN)

# devkitPro paths
if(DEFINED ENV{DEVKITPRO})
    set(DEVKITPRO $ENV{DEVKITPRO})
else()
    set(DEVKITPRO /opt/devkitpro)
endif()

set(DEVKITPPC ${DEVKITPRO}/devkitPPC)
set(LIBOGC ${DEVKITPRO}/libogc)
set(PORTLIBS ${DEVKITPRO}/portlibs/ppc)

# Compilers
set(CMAKE_C_COMPILER ${DEVKITPPC}/bin/powerpc-eabi-gcc)
set(CMAKE_CXX_COMPILER ${DEVKITPPC}/bin/powerpc-eabi-g++)
set(CMAKE_ASM_COMPILER ${DEVKITPPC}/bin/powerpc-eabi-gcc)
set(CMAKE_AR ${DEVKITPPC}/bin/powerpc-eabi-ar)
set(CMAKE_RANLIB ${DEVKITPPC}/bin/powerpc-eabi-ranlib)
set(CMAKE_OBJCOPY ${DEVKITPPC}/bin/powerpc-eabi-objcopy)
set(CMAKE_OBJDUMP ${DEVKITPPC}/bin/powerpc-eabi-objdump)

# Wii-specific flags: big-endian, single-precision FPU, no exceptions
# -Wno-error=incompatible-pointer-types: pre-existing type mismatches in the
# codebase that GCC 14+ treats as errors by default.
set(CMAKE_C_FLAGS_INIT "-mogc -mcpu=750 -meabi -mhard-float -ffunction-sections -fdata-sections -Wno-error=incompatible-pointer-types")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

# Wii memory layout: 24 MB MEM1 + ~48 MB MEM2
set(CMAKE_EXE_LINKER_FLAGS_INIT "-mogc -L${LIBOGC}/lib/wii -L${PORTLIBS}/lib")

# Don't search host paths for libraries/headers
set(CMAKE_FIND_ROOT_PATH ${DEVKITPPC} ${LIBOGC} ${PORTLIBS})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
