# 3ds.cmake — CMake toolchain file for Nintendo 3DS cross-compilation.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=.devdeps/3ds.cmake \
#         -DWOLFRAM_BUILD_3DS=ON \
#         -B build-3ds
#
# Requires devkitPro with devkitARM and libctru installed:
#   dkp-pacman -S 3ds-dev
#
# Set DEVKITPRO=/opt/devkitpro before invoking cmake.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_SYSTEM_VERSION 1)

# ARM is little-endian
set(CMAKE_C_BYTE_ORDER LITTLE_ENDIAN)
set(CMAKE_CXX_BYTE_ORDER LITTLE_ENDIAN)

# devkitPro paths
if(DEFINED ENV{DEVKITPRO})
    set(DEVKITPRO $ENV{DEVKITPRO})
else()
    set(DEVKITPRO /opt/devkitpro)
endif()

set(DEVKITARM ${DEVKITPRO}/devkitARM)
set(LIBCTRU ${DEVKITPRO}/libctru)
set(PORTLIBS ${DEVKITPRO}/portlibs/3ds)

# Compilers (ARM)
set(CMAKE_C_COMPILER ${DEVKITARM}/bin/arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER ${DEVKITARM}/bin/arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER ${DEVKITARM}/bin/arm-none-eabi-gcc)
set(CMAKE_AR ${DEVKITARM}/bin/arm-none-eabi-ar)
set(CMAKE_RANLIB ${DEVKITARM}/bin/arm-none-eabi-ranlib)
set(CMAKE_OBJCOPY ${DEVKITARM}/bin/arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP ${DEVKITARM}/bin/arm-none-eabi-objdump)

# 3DS flags: ARM11 (ARMv6), hard-float, VFP
set(CMAKE_C_FLAGS_INIT "-march=armv6k -mtune=arm1176jzf-s -mfloat-abi=hard -mfpu=vfp -ffunction-sections -fdata-sections -Wno-error=incompatible-pointer-types")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-march=armv6k -L${LIBCTRU}/lib -L${PORTLIBS}/lib")

# Don't search host paths for libraries/headers
set(CMAKE_FIND_ROOT_PATH ${DEVKITARM} ${LIBCTRU} ${PORTLIBS})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
