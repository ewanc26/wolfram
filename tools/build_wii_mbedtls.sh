#!/usr/bin/env bash
#
# build_wii_mbedtls.sh — Cross-compile mbedTLS 2.28.8 for the Nintendo Wii
# (devkitPPC / libogc, powerpc-eabi-newlib) and install it into the devkitPro
# repository-local portlibs directory so wolfram's Wii build can link it.
#
# Usage:
#   tools/build_wii_mbedtls.sh
#
# Requires: devkitPPC on PATH (or DEVKITPRO set), network access to github.
# The resulting headers/libs land in build-wii-mbedtls so the wolfram CMake
# Wii build (WOLFRAM_BUILD_WII=ON) finds them automatically without root.
#
set -euo pipefail

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"
MBEDTLS_VER="mbedtls-2.28.8"
SRC_DIR="$(mktemp -d)/${MBEDTLS_VER}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PORTLIBS="${WII_PORTLIBS:-$PROJECT_DIR/build-wii-mbedtls}"

export PATH="$DEVKITPPC/bin:$PATH"

echo ">> Cloning ${MBEDTLS_VER} into ${SRC_DIR}"
git clone --depth 1 --branch "${MBEDTLS_VER}" \
    "https://github.com/Mbed-TLS/mbedtls.git" "${SRC_DIR}"

echo ">> Installing wolfram Wii mbedTLS config as mbedtls/wii_config.h"
cp "${SCRIPT_DIR}/wii_mbedtls_config.h" "${SRC_DIR}/include/mbedtls/wii_config.h"

CFLAGS="-mogc -mcpu=750 -meabi -ffunction-sections -fdata-sections -Os \
-include limits.h \
-I${PORTLIBS}/include \
-DMBEDTLS_CONFIG_FILE='\"mbedtls/wii_config.h\"'"

echo ">> Building mbedTLS libraries"
make -C "${SRC_DIR}" clean >/dev/null 2>&1 || true
make -C "${SRC_DIR}" lib \
    CC=powerpc-eabi-gcc \
    AR=powerpc-eabi-ar \
    RANLIB=powerpc-eabi-ranlib \
    LD=powerpc-eabi-ld \
    OBJCOPY=powerpc-eabi-objcopy \
    CFLAGS="${CFLAGS}" \
    WARNING_CFLAGS="-Wall -Wextra"

echo ">> Installing to ${PORTLIBS}"
mkdir -p "${PORTLIBS}/include" "${PORTLIBS}/lib"
cp -R "${SRC_DIR}/include/"* "${PORTLIBS}/include/"
cp "${SRC_DIR}"/library/libmbedcrypto.a \
   "${SRC_DIR}"/library/libmbedx509.a \
   "${SRC_DIR}"/library/libmbedtls.a \
   "${PORTLIBS}/lib/"

echo ">> Done. Installed mbedTLS ${MBEDTLS_VER} for Wii into ${PORTLIBS}"
