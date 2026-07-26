# wiiu.cmake — CMake toolchain file for Nintendo Wii U cross-compilation.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=.devdeps/wiiu.cmake \
#         -DWOLFRAM_BUILD_WIIU=ON \
#         -B build-wiiu
#
# Requires devkitPro with devkitPPC and the wut SDK installed:
#   dkp-pacman -S wiiu-dev wiiu-pkg-config
#
# Set DEVKITPRO=/opt/devkitpro before invoking cmake.
#
# This file delegates to devkitPro's own WiiU.cmake rather than describing the
# toolchain itself. An earlier hand-written version specified compiler flags
# directly and did not work: it passed -mwiiu, which devkitPPC's gcc does not
# accept (the Wii U flags are -DESPRESSO -mcpu=750 -meabi -mhard-float), and it
# never added ${WUT}/include to the include path or the wut.specs linker spec
# file, so nothing using a wut header could have compiled.
#
# Delegating keeps this in step with devkitPro upgrades instead of re-deriving
# flags that move. The Wii, 3DS and Windows toolchain files in this directory
# are separate and unaffected.

if(DEFINED ENV{DEVKITPRO})
    set(DEVKITPRO $ENV{DEVKITPRO})
elseif(NOT DEFINED DEVKITPRO)
    set(DEVKITPRO /opt/devkitpro)
endif()

if(NOT EXISTS "${DEVKITPRO}/cmake/WiiU.cmake")
    message(FATAL_ERROR
        "devkitPro's Wii U CMake support was not found at "
        "${DEVKITPRO}/cmake/WiiU.cmake.\n"
        "Install it with:  dkp-pacman -S wiiu-dev wiiu-pkg-config\n"
        "and set DEVKITPRO to your devkitPro root.")
endif()

include(${DEVKITPRO}/cmake/WiiU.cmake)

# PowerPC is big-endian. Declared explicitly because test_big_endian cannot run
# a cross-compiled probe binary.
set(CMAKE_C_BYTE_ORDER BIG_ENDIAN)
set(CMAKE_CXX_BYTE_ORDER BIG_ENDIAN)
