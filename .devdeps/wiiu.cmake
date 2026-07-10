# wiiu.cmake — CMake toolchain file for Nintendo Wii U cross-compilation.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=.devdeps/wiiu.cmake \
#         -DWOLFRAM_BUILD_WIIU=ON \
#         -B build-wiiu
#
# Requires devkitPro with devkitPPC and the wut SDK installed:
#   dkp-pacman -S wiiu-dev
#
# Set DEVKITPRO=/opt/devkitpro before invoking cmake.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc)
set(CMAKE_SYSTEM_VERSION 1)

# PowerPC is big-endian
set(CMAKE_C_BYTE_ORDER BIG_ENDIAN)
set(CMAKE_CXX_BYTE_ORDER BIG_ENDIAN)

# devkitPro paths
if(DEFINED ENV{DEVKITPRO})
    set(DEVKITPRO $ENV{DEVKITPRO})
else()
    set(DEVKITPRO /opt/devkitpro)
endif()

set(DEVKITPPC ${DEVKITPRO}/devkitPPC)
set(WUT ${DEVKITPRO}/wut)
set(PORTLIBS ${DEVKITPRO}/portlibs/wiiu)

# Compilers (same devkitPPC as Wii, different flags)
set(CMAKE_C_COMPILER ${DEVKITPPC}/bin/powerpc-eabi-gcc)
set(CMAKE_CXX_COMPILER ${DEVKITPPC}/bin/powerpc-eabi-g++)
set(CMAKE_ASM_COMPILER ${DEVKITPPC}/bin/powerpc-eabi-gcc)
set(CMAKE_AR ${DEVKITPPC}/bin/powerpc-eabi-ar)
set(CMAKE_RANLIB ${DEVKITPPC}/bin/powerpc-eabi-ranlib)
set(CMAKE_OBJCOPY ${DEVKITPPC}/bin/powerpc-eabi-objcopy)
set(CMAKE_OBJDUMP ${DEVKITPPC}/bin/powerpc-eabi-objdump)

# Wii U flags: Espresso CPU (tri-core PowerPC 750CL), big-endian
# -D__WIIU__ is defined by the wut headers, not here.
set(CMAKE_C_FLAGS_INIT "-mwiiu -mcpu=750 -meabi -mhard-float -ffunction-sections -fdata-sections -Wno-error=incompatible-pointer-types")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-mwiiu -L${WUT}/lib -L${PORTLIBS}/lib")

# Don't search host paths for libraries/headers
set(CMAKE_FIND_ROOT_PATH ${DEVKITPPC} ${WUT} ${PORTLIBS})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
