# rpi1.cmake — CMake toolchain file for Raspberry Pi 1 / Zero (ARMv6,
# ARM1176JZF-S) cross-compilation.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=.devdeps/rpi1.cmake \
#         -DWOLFRAM_BUILD_SERVER=ON \
#         -B build-rpi1
#
# This is a plain Linux target (unlike wii.cmake/wiiu.cmake/3ds.cmake, which
# build stripped-down console clients) — OAuth, the XRPC server, and SQLite
# storage all build normally here, since this target exists to run MetalBear.
#
# Requires an ARMv6 (armv6zk/vfp/hard-float) cross-compiler. The Pi 1B/Zero's
# ARM1176JZF-S predates the ARMv7 baseline that Debian/Ubuntu's "armhf"
# multiarch and crossbuild-essential-armhf packages target — a toolchain or
# rootfs built for generic armhf will produce a binary that hits SIGILL on
# real Pi 1B/Zero hardware. Use a toolchain built specifically for this CPU,
# e.g.:
#   - The Raspberry Pi Foundation's own cross-tools
#     (github.com/raspberrypi/tools, arm-bcm2708/arm-linux-gnueabihf), or
#   - A crosstool-NG / Buildroot toolchain configured for
#     arm-linux-gnueabihf with the flags below, or
#   - Raspbian's actual armhf (this is Raspbian-the-distro's own armhf,
#     which — confusingly — targets ARMv6, not Debian's armhf) gcc, if
#     building inside a Pi 1B-compatible chroot/QEMU environment.
#
# Environment variables:
#   RPI1_CC / RPI1_CXX — compiler, if not the arm-linux-gnueabihf- default.
#   RPI1_ROOTFS         — root filesystem (include/lib) for the target. Must
#                          be a real Pi 1B/Zero-compatible userland (e.g.
#                          copied from /lib, /usr/lib, /usr/include of a
#                          Raspberry Pi OS image that still supports ARMv6 —
#                          not a generic armhf/Debian rootfs), containing
#                          SQLite3, OpenSSL, and libcurl dev packages for
#                          MetalBear's dependencies.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_SYSTEM_VERSION 1)

# ARM is little-endian
set(CMAKE_C_BYTE_ORDER LITTLE_ENDIAN)
set(CMAKE_CXX_BYTE_ORDER LITTLE_ENDIAN)

if(DEFINED ENV{RPI1_CC})
    set(CMAKE_C_COMPILER $ENV{RPI1_CC})
    set(CMAKE_CXX_COMPILER $ENV{RPI1_CXX})
else()
    set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
    set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
endif()

# ARM1176JZF-S: ARMv6Z with the VFPv2 FPU, hard-float ABI. Using -march=armv7
# or higher (as generic "armhf" toolchains default to) emits instructions
# this CPU does not have and will SIGILL at runtime — these flags are not
# optional. -mtune schedules for the specific core without raising the
# architecture floor.
#
# The "zk" in armv6zk specifically matters here, not just "armv6": it adds
# the LDREXD/STREXD exclusive-access instructions, which is what lets GCC
# lower MetalBear's `_Atomic uint64_t` request counters (ops/metrics.c) and
# wolfram's atomic DID-cache refcounts (identity.c) to inline instructions.
# Plain armv6 lacks them, so 64-bit atomics would instead need libatomic
# calls the link step may not pull in by default.
set(RPI1_CPU_FLAGS "-march=armv6zk -mfpu=vfp -mfloat-abi=hard -mtune=arm1176jzf-s")
set(CMAKE_C_FLAGS_INIT "${RPI1_CPU_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections")

# Sysroot for cross-compilation — see RPI1_ROOTFS note above.
if(DEFINED ENV{RPI1_ROOTFS})
    set(CMAKE_SYSROOT $ENV{RPI1_ROOTFS})
    set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} --sysroot=${CMAKE_SYSROOT}")
    set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} --sysroot=${CMAKE_SYSROOT}")
else()
    message(WARNING
        "RPI1_ROOTFS is not set — SQLite3/OpenSSL/libcurl (required by "
        "WOLFRAM_BUILD_SERVER) will not be found unless this host's "
        "installed cross-compiler already targets a Pi 1B-compatible "
        "sysroot by default.")
endif()

# Don't search host paths for libraries/headers
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 256MB (Pi 1B rev1) / 512MB (rev2+) total RAM, no swap by default on most
# minimal images — LTO's memory use during the final link is a host-side
# cross-compile cost, not a target-side one, so it stays enabled (see the
# top-level CMakeLists.txt) rather than being disabled here.
