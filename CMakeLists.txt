cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0077 NEW)
if(POLICY CMP0156)
  cmake_policy(SET CMP0156 NEW)
endif()
project(
  wolfram
  VERSION 0.12.1
  DESCRIPTION "A C/C++ SDK for the AT Protocol"
  LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Wno-gcc-compat
  -Wno-unused-function)

# Console/embedded cross-compilation targets. When any is ON, desktop-specific
# dependencies (libcurl, OpenSSL, pthreads) are replaced with platform stubs.
option(WOLFRAM_BUILD_WII "Build for Nintendo Wii (devkitPPC/libogc)" OFF)
option(WOLFRAM_BUILD_WIIU "Build for Nintendo Wii U (devkitPPC/wut)" OFF)
option(WOLFRAM_MHD_SHIM
       "Use the bundled libmicrohttpd shim instead of the real library" OFF)
option(WOLFRAM_BUILD_3DS "Build for Nintendo 3DS (devkitARM/libctru)" OFF)

# Windows cross-compilation (MinGW-w64). Uses Win32 APIs instead of POSIX.
option(WOLFRAM_BUILD_WINDOWS "Build for Windows (MinGW-w64 cross-compilation)"
       OFF)

# Derived: true for any embedded/console target
if(WOLFRAM_BUILD_WII OR WOLFRAM_BUILD_WIIU OR WOLFRAM_BUILD_3DS)
  set(WOLFRAM_BUILD_EMBEDDED ON)
else()
  set(WOLFRAM_BUILD_EMBEDDED OFF)
endif()

# Derived: true for any non-POSIX target (embedded or Windows)
if(WOLFRAM_BUILD_EMBEDDED OR WOLFRAM_BUILD_WINDOWS)
  set(WOLFRAM_BUILD_NON_POSIX ON)
else()
  set(WOLFRAM_BUILD_NON_POSIX OFF)
endif()

# Transport selection is a separate axis from platform, because "embedded" is
# not one thing. The Wii and 3DS have no libcurl, so they use the hand-rolled
# socket+mbedTLS transport (xrpc_wii.c / websocket_wii.c). That transport pulls
# in src/transport/wii_tls.c, which is written against libogc's net_* API and so
# only builds for the Wii.
#
# The Wii U is a different case: devkitPro packages libcurl for it (dkp-pacman
# -S wiiu-curl), so it uses the same curl-based transport as desktop. Routing it
# through the socket transport instead left libwolfram.a with undefined
# wii_tls_* symbols — invisible inside a static archive, but a hard link failure
# for any executable that actually used XRPC.
#
# Crypto stays keyed off the embedded axis regardless: no console ships OpenSSL,
# so they all use the mbedTLS-backed crypto_wii.c.
if(WOLFRAM_BUILD_WII OR WOLFRAM_BUILD_3DS)
  set(WOLFRAM_USE_SOCKET_TRANSPORT ON)
else()
  set(WOLFRAM_USE_SOCKET_TRANSPORT OFF)
endif()

if(NOT WOLFRAM_BUILD_NON_POSIX)
  find_package(CURL REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(Threads REQUIRED)
endif()
if(NOT WOLFRAM_BUILD_EMBEDDED)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SECP256K1 libsecp256k1)
    pkg_check_modules(ZSTD libzstd)
    pkg_check_modules(CARES libcares>=1.28)
  endif()
  if(NOT ZSTD_FOUND)
    find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)
    find_library(ZSTD_LIBRARY NAMES zstd)
    if(ZSTD_INCLUDE_DIR AND ZSTD_LIBRARY)
      set(ZSTD_FOUND TRUE)
      set(ZSTD_INCLUDE_DIRS ${ZSTD_INCLUDE_DIR})
      set(ZSTD_LIBRARIES ${ZSTD_LIBRARY})
      set(ZSTD_VERSION "system")
    endif()
  endif()
  if(NOT SECP256K1_FOUND)
    find_path(SECP256K1_INCLUDE_DIR NAMES secp256k1.h)
    find_library(SECP256K1_LIBRARY NAMES secp256k1)
    if(SECP256K1_INCLUDE_DIR AND SECP256K1_LIBRARY)
      set(SECP256K1_FOUND TRUE)
      set(SECP256K1_INCLUDE_DIRS ${SECP256K1_INCLUDE_DIR})
      set(SECP256K1_LIBRARIES ${SECP256K1_LIBRARY})
    endif()
  endif()
endif()

# cJSON v1.7.18 and libcbor v0.13.0 declare old cmake_minimum_required versions
# which CMake 4.x rejects. Configure them with 3.10 policies.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10 CACHE STRING "" FORCE)

include(FetchContent)
FetchContent_Declare(
  cjson GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git GIT_TAG v1.7.18)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
FetchContent_MakeAvailable(cjson)
target_compile_options(cjson PRIVATE -Wno-keyword-macro)

FetchContent_Declare(libcbor GIT_REPOSITORY https://github.com/PJK/libcbor.git
                     GIT_TAG v0.13.0)
set(WITH_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CBOR_PRETTY_PRINTER OFF CACHE BOOL "" FORCE)
set(SANITIZE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libcbor)
set_target_properties(cbor PROPERTIES POSITION_INDEPENDENT_CODE ON)

FetchContent_Declare(
  base64url GIT_REPOSITORY https://github.com/jons/base64url.git GIT_TAG master)
FetchContent_GetProperties(base64url)
if(NOT base64url_POPULATED)
  FetchContent_Populate(base64url)
endif()

# Lexicon generator tool — C++ replacement for the removed tools/wf_lexgen.py. A
# development-time codegen tool, so it is always built (not gated on
# WOLFRAM_BUILD_TESTS) and ready to regenerate atproto_lex.{h,c}. It links the
# vendored cJSON and needs OpenSSL headers, which the desktop build already
# requires; non-POSIX cross-targets have no OpenSSL and skip the tool.
if(NOT WOLFRAM_BUILD_NON_POSIX)
  add_executable(wf_lexgen_tool tools/wf_lexgen.cpp)
  set_target_properties(wf_lexgen_tool PROPERTIES CXX_STANDARD 23)
  target_include_directories(
    wf_lexgen_tool PRIVATE ${cjson_SOURCE_DIR} ${cjson_BINARY_DIR}
                           ${OPENSSL_INCLUDE_DIR})
  target_link_libraries(wf_lexgen_tool PRIVATE cjson OpenSSL::Crypto)
endif()

add_library(
  wolfram
  # Transport
  $<IF:$<BOOL:${WOLFRAM_USE_SOCKET_TRANSPORT}>,src/transport/xrpc_wii.c,src/transport/xrpc.c>
  $<IF:$<BOOL:${WOLFRAM_USE_SOCKET_TRANSPORT}>,src/transport/websocket_wii.c,src/transport/websocket.c>
  # Identity
  src/identity/identity.c
  # Crypto
  $<IF:$<BOOL:${WOLFRAM_BUILD_EMBEDDED}>,src/crypto/crypto_wii.c,src/crypto/crypto.c>
  # Repository (DAG-CBOR, CID, CAR, MST, commit, record, diff, sync)
  src/repo/cbor.c
  src/repo/cid.c
  src/repo/car.c
  src/repo/commit.c
  src/repo/mst.c
  src/repo/diff.c
  src/repo/record.c
  src/repo/repo.c
  src/repo/sync.c
  src/repo/cbor_build.c
  src/repo/cbor_map_find.c
  ${base64url_SOURCE_DIR}/librb64u.c
  # Session
  src/session/session.c
  $<$<NOT:$<BOOL:${WOLFRAM_BUILD_EMBEDDED}>>:src/session/auth_client.c>
  # OAuth (metadata, PKCE, DPoP, PAR, state, callback, flow)
  $<$<NOT:$<BOOL:${WOLFRAM_BUILD_EMBEDDED}>>:
  src/session/oauth/util.c
  src/session/oauth/meta.c
  src/session/oauth/pkce.c
  src/session/oauth/dpop.c
  src/session/oauth/par.c
  src/session/oauth/state.c
  src/session/oauth/callback.c
  src/session/oauth/flow.c
  >
  # OAuth resource-server verification (Bearer + DPoP)
  $<$<NOT:$<BOOL:${WOLFRAM_BUILD_EMBEDDED}>>:src/oauth/verify.c>
  # Jetstream
  src/jetstream/jetstream.c
  # Label subscriptions
  src/label/label.c
  # Label query/get (request/response)
  src/label/label_query.c
  # Label subscribeLabels agent consumption wrapper
  src/agent/label_subscribe_typed.c
  # Syntax validation
  cpp/wolfram/syntax.cpp
  # TID generation clock
  $<IF:$<BOOL:${WOLFRAM_BUILD_EMBEDDED}>,src/platform/wii_tid.c,src/tid.c>
  # Lexicon validation (C++ with RAII for cJSON)
  $<$<NOT:$<BOOL:${WOLFRAM_BUILD_EMBEDDED}>>:cpp/wolfram/validate.cpp>
  # High-level agent API
  src/agent/agent.c
  src/agent/agent_repo_sync.c
  src/agent/post.c
  src/agent/embed_typed.c
  src/agent/feed.c
  src/agent/graph.c
  src/agent/graph_lists.c
  src/agent/notification.c
  src/agent/agent_ext.c
  src/agent/feed_typed.c
  src/agent/thread_typed.c
  src/agent/graph_typed.c
  src/agent/graph_social_typed.c
  src/agent/graph_write.c
  src/agent/actor_typed.c
  src/agent/sync_list_typed.c
  src/agent/actor_prefs_typed.c
  src/agent/actor_status_typed.c
  src/agent/moderation_report_typed.c
  src/agent/contact_typed.c
  src/agent/labeler_typed.c
  src/agent/video_typed.c
  src/agent/identity_typed.c
  src/agent/notification_v2_typed.c
  src/agent/bookmark_typed.c
  src/agent/list_typed.c
  src/agent/moderation_typed.c
  src/agent/notification_typed.c
  src/agent/repo_typed.c
  src/agent/ageassurance_typed.c
  src/agent/feedgen_typed.c
  src/agent/feed_gen_typed.c
  # getFeedSkeleton builder (validate/dedupe/cursor pagination)
  src/server/feedgen.c
  src/agent/unspecced_typed.c
  src/agent/draft_typed.c
  src/agent/temp_typed.c
  src/agent/notification_prefs_typed.c
  src/agent/chat_typed.c
  src/agent/chat_mod_events.c
  src/agent/admin_typed.c
  src/agent/sync_typed.c
  $<$<NOT:$<BOOL:${WOLFRAM_BUILD_EMBEDDED}>>:src/agent/server_typed.c>
  src/agent/lexicon_typed.c
  src/agent/threadgate_postgate.c
  src/agent/moderation_actions.c
  src/agent/lexcall.c
  src/agent/pagination.c
  src/agent/push_endpoint.c
  src/agent/create_record.c
  src/agent/moderation.c
  src/agent/moderation_apply.c
  src/agent/label_persist.c
  src/agent/identity_mgmt.c
  # Owned typed wrappers for tools.ozone.report/queue/signature moderation ops
  src/agent/ozone_moderation_ops_typed.c
  # High-level BskyAgent convenience wrapper
  src/agent/bsky_agent.c
  # Rich text / facet detection (C++ RAII, no allocation-leak paths)
  cpp/wolfram/richtext.cpp
  # Firehose subscribeRepos subscription
  src/sync/sync_subscribe.c
  # Firehose subscription event frame production (builder)
  src/sync/sync_publish.c
  # Firehose commit verification
  src/sync/sync_verify.c
  # End-to-end record commit signature verification
  src/repo/verify.c
  # Generated lexicon endpoint wrappers
  src/atproto_lex.c
  # Server account management (PDS server-side)
  $<$<NOT:$<BOOL:${WOLFRAM_BUILD_EMBEDDED}>>:src/server/server.c>
  # Service-auth (service JWT) token issuance + verification
  $<$<NOT:$<BOOL:${WOLFRAM_BUILD_EMBEDDED}>>:src/server/service_auth.c>
  # DID PLC operation build / sign / submit helpers
  src/plc/plc.c
  # New utilities and embed support
  src/util.c
  src/agent/blob.c
  # Blob persistence removed: migrated to MetalBear project.
  src/agent/embed.c
  # Ozone moderation / labeler helper
  src/ozone/ozone.c
  # Owned typed wrappers for trends / suggested users / thread v2
  src/agent/unspecced_trends_typed.c
  # Owned typed wrappers for the tools.ozone.* namespaces
  src/agent/ozone_typed.c
  # Owned typed wrappers for the remaining tools.ozone.moderation.* endpoints
  src/agent/ozone_admin_typed.c
  # Generic JSON round-trip / schema-subset validation
  $<IF:$<BOOL:${WOLFRAM_BUILD_NON_POSIX}>,src/json/json.c,cpp/wolfram/json.cpp>
  # Image dimension probing via vendored stb_image (header-only, third_party/)
  src/image.c)

# The version lives only in the `VERSION` above; derive the public
# WOLFRAM_VERSION_* macros from PROJECT_VERSION at compile time.
string(REPLACE "." ";" WOLFRAM_VERSION_PARTS "${PROJECT_VERSION}")
list(GET WOLFRAM_VERSION_PARTS 0 WOLFRAM_VERSION_MAJOR)
list(GET WOLFRAM_VERSION_PARTS 1 WOLFRAM_VERSION_MINOR)
list(GET WOLFRAM_VERSION_PARTS 2 WOLFRAM_VERSION_PATCH)
target_compile_definitions(
  wolfram
  PUBLIC WOLFRAM_VERSION_MAJOR=${WOLFRAM_VERSION_MAJOR}
         WOLFRAM_VERSION_MINOR=${WOLFRAM_VERSION_MINOR}
         WOLFRAM_VERSION_PATCH=${WOLFRAM_VERSION_PATCH}
         WOLFRAM_VERSION_STRING="${PROJECT_VERSION}")

# POSIX feature-test macros.
#
# glibc hides strdup, strndup, struct sigaction, clock_gettime, gmtime_r and
# friends behind a feature-test macro under strict ISO C, while macOS and the
# BSDs expose them by default. Without this, a Linux build produces implicit
# declarations of strdup — which returns int, so the pointer is truncated on
# 64-bit — and hard errors like "storage size of 'sa' isn't known". The
# Dockerfile set CFLAGS=-D_DEFAULT_SOURCE to paper over this, but CI and any
# other consumer did not, so Linux builds broke there instead.
#
# Set it on the target so every consumer inherits it and no source file has to
# remember; most of the affected files are generated and cannot carry a #define.
# Console toolchains (newlib/devkitPro) supply their own headers, so scope this
# to hosted POSIX platforms.
if(UNIX
   AND NOT WOLFRAM_BUILD_WII
   AND NOT WOLFRAM_BUILD_WIIU
   AND NOT WOLFRAM_BUILD_3DS)
  target_compile_definitions(wolfram PUBLIC _POSIX_C_SOURCE=200809L
                                            _DEFAULT_SOURCE)
  if(APPLE)
    # _POSIX_C_SOURCE alone hides BSD extensions in Apple's libc.
    target_compile_definitions(wolfram PUBLIC _DARWIN_C_SOURCE)
  endif()
endif()

# Embedded/console platform support
if(WOLFRAM_BUILD_WII)
  target_sources(wolfram PRIVATE src/platform/wii_platform.c
                                 src/platform/openssl_compat.c src/transport/wii_tls.c)
  target_compile_definitions(wolfram PUBLIC WOLFRAM_WII)
  target_include_directories(wolfram PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                                             ${LIBOGC}/include)
  # mbedTLS is cross-compiled as a Wii portlib (see tools/build_wii_mbedtls.sh).
  # Keep the custom portlib local by default so setup never requires root.
  # WII_PORTLIBS may override the location for packaged toolchains.
  if(DEFINED ENV{WII_PORTLIBS})
    set(WII_PORTLIBS "$ENV{WII_PORTLIBS}")
  else()
    set(WII_PORTLIBS "${CMAKE_CURRENT_SOURCE_DIR}/build-wii-mbedtls")
  endif()
  if(NOT EXISTS "${WII_PORTLIBS}/lib/libmbedtls.a")
    message(
      FATAL_ERROR
        "WOLFRAM_BUILD_WII=ON requires mbedTLS for the Wii. It was not found "
        "at ${WII_PORTLIBS}/lib/libmbedtls.a. Build it with:\n"
        "    tools/build_wii_mbedtls.sh\n"
        "(network + devkitPro toolchain required).")
  endif()
  target_include_directories(wolfram PRIVATE ${WII_PORTLIBS}/include)
  target_link_libraries(
    wolfram
    PUBLIC ogc ${WII_PORTLIBS}/lib/libmbedtls.a
           ${WII_PORTLIBS}/lib/libmbedx509.a
           ${WII_PORTLIBS}/lib/libmbedcrypto.a)
  message(
    STATUS "Building for Nintendo Wii (WOLFRAM_BUILD_WII=ON), mbedTLS linked")
elseif(WOLFRAM_BUILD_WIIU)
  # wiiu_random.c supplies wii_tls_random(), which crypto_wii.c needs for P-256.
  # On the Wii that symbol lives in wii_tls.c, which the Wii U does not build
  # (libogc-only, and the Wii U uses the curl transport instead).
  target_sources(wolfram PRIVATE src/platform/wiiu_platform.c
                                 src/crypto/wiiu_random.c src/platform/openssl_compat.c)
  target_compile_definitions(wolfram PUBLIC WOLFRAM_WIIU)
  target_include_directories(wolfram PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

  # Unlike the Wii, the Wii U does not need mbedTLS cross-compiled by hand:
  # devkitPro packages it (dkp-pacman -S wiiu-mbedtls), so it is found in
  # portlibs alongside wut. The embedded crypto and transport sources shared
  # with the Wii (crypto_wii.c, xrpc_wii.c) include mbedtls headers directly, so
  # this is required, not optional.
  if(DEFINED ENV{DEVKITPRO})
    set(WIIU_PORTLIBS "$ENV{DEVKITPRO}/portlibs/wiiu")
  else()
    set(WIIU_PORTLIBS "/opt/devkitpro/portlibs/wiiu")
  endif()

  if(NOT EXISTS "${WIIU_PORTLIBS}/lib/libmbedtls.a")
    message(
      FATAL_ERROR
        "WOLFRAM_BUILD_WIIU=ON requires mbedTLS for the Wii U. It was not "
        "found at ${WIIU_PORTLIBS}/lib/libmbedtls.a. Install it with:\n"
        "    dkp-pacman -S wiiu-mbedtls\n"
        "or set DEVKITPRO to your devkitPro root.")
  endif()

  # The Wii U uses the curl-based transport (see WOLFRAM_USE_SOCKET_TRANSPORT
  # above), so libcurl is required too. curl is built statically here and its
  # headers need CURL_STATICLIB or they declare the wrong linkage.
  if(NOT EXISTS "${WIIU_PORTLIBS}/lib/libcurl.a")
    message(
      FATAL_ERROR
        "WOLFRAM_BUILD_WIIU=ON requires libcurl for the Wii U. It was not "
        "found at ${WIIU_PORTLIBS}/lib/libcurl.a. Install it with:\n"
        "    dkp-pacman -S wiiu-curl")
  endif()

  target_compile_definitions(wolfram PUBLIC CURL_STATICLIB)
  target_include_directories(wolfram PRIVATE ${WIIU_PORTLIBS}/include)
  target_link_libraries(
    wolfram
    PUBLIC ${WIIU_PORTLIBS}/lib/libcurl.a ${WIIU_PORTLIBS}/lib/libmbedtls.a
           ${WIIU_PORTLIBS}/lib/libmbedx509.a
           ${WIIU_PORTLIBS}/lib/libmbedcrypto.a)
  message(
    STATUS
      "Building for Nintendo Wii U (WOLFRAM_BUILD_WIIU=ON), curl + mbedTLS linked"
  )
elseif(WOLFRAM_BUILD_3DS)
  target_sources(wolfram PRIVATE src/platform/3ds_platform.c
                                 src/platform/openssl_compat.c)
  target_compile_definitions(wolfram PUBLIC WOLFRAM_3DS)
  target_include_directories(wolfram PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  if(DEFINED ENV{DEVKITPRO})
    set(DEVKITPRO "$ENV{DEVKITPRO}")
  else()
    set(DEVKITPRO /opt/devkitpro)
  endif()
  set(LIBCTRU "${DEVKITPRO}/libctru")
  set(PORTLIBS "${DEVKITPRO}/portlibs/3ds")
  target_include_directories(wolfram PRIVATE ${LIBCTRU}/include
                                             ${PORTLIBS}/include)
  target_link_libraries(wolfram PUBLIC ${LIBCTRU}/lib/libctru.a
                                       ${PORTLIBS}/lib/libmbedtls.a
                                       ${PORTLIBS}/lib/libmbedx509.a
                                       ${PORTLIBS}/lib/libmbedcrypto.a)
  message(STATUS "Building for Nintendo 3DS (WOLFRAM_BUILD_3DS=ON), libctru + mbedtls linked")
elseif(WOLFRAM_BUILD_WINDOWS)
  target_sources(wolfram PRIVATE src/platform/windows_platform.c)
  target_compile_definitions(wolfram PUBLIC WOLFRAM_WINDOWS)
  target_link_libraries(wolfram PUBLIC ws2_32 bcrypt)
  message(STATUS "Building for Windows (WOLFRAM_BUILD_WINDOWS=ON)")
else()
  target_sources(wolfram PRIVATE src/platform/posix_platform.c)
endif()

if(WOLFRAM_BUILD_EMBEDDED)
  #
  # Embedded targets skip examples and tests: there is no host to run them on.
  # The server used to be skipped here too, for want of curl/OpenSSL/pthreads —
  # but that is no longer true of the Wii U, which has curl and mbedTLS in
  # devkitPro's portlibs, pthreads in newlib, and now the libmicrohttpd shim
  # below. It is still true of the Wii and the 3DS, so those keep the old
  # behaviour.
  #
  if(NOT WOLFRAM_BUILD_WIIU)
    set(WOLFRAM_BUILD_SERVER OFF CACHE BOOL "" FORCE)
  endif()
  set(WOLFRAM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(WOLFRAM_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
endif()

# XRPC server module (optional, requires libmicrohttpd — or the shim)
if(WOLFRAM_BUILD_SERVER)
  #
  # libmicrohttpd has no port to the Nintendo consoles, so WOLFRAM_MHD_SHIM
  # substitutes an implementation of the slice xrpc_server.c uses, built on BSD
  # sockets. It is forced on for the Wii U and available everywhere else,
  # because a shim that can only be built for a target nobody can run tests on
  # is a shim nobody can trust: enabling it natively runs the whole existing
  # server suite against it.
  #
  if(WOLFRAM_BUILD_WIIU OR WOLFRAM_BUILD_WII OR WOLFRAM_BUILD_3DS)
    set(WOLFRAM_MHD_SHIM ON CACHE BOOL "" FORCE)
  endif()
  find_package(Threads REQUIRED)
  if(WOLFRAM_MHD_SHIM)
    # Ahead of any system microhttpd.h on the include path, so `#include
    # <microhttpd.h>` resolves to the shim.
    set(MICROHTTPD_INCLUDE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/include/wolfram/compat")
    set(MICROHTTPD_LIBRARY "")
    target_sources(wolfram PRIVATE src/server/mhd_shim.c)
    message(STATUS "XRPC server using the libmicrohttpd shim")
  else()
    find_path(MICROHTTPD_INCLUDE_DIR NAMES microhttpd.h REQUIRED)
    find_library(MICROHTTPD_LIBRARY NAMES microhttpd REQUIRED)
  endif()
  target_sources(wolfram PRIVATE src/server/xrpc_server.c)
  # RFC 6455 WebSocket upgrade/framing, split out of xrpc_server.c.
  target_sources(wolfram PRIVATE src/server/xrpc_server_ws.c)
  # JSON configuration loader for the XRPC server.
  target_sources(wolfram PRIVATE src/server/xrpc_server_config.c)
  # Blob store server routes removed: migrated to MetalBear project.
  # Feed-generator skeleton server helper (builds on the XRPC server).
  target_sources(wolfram PRIVATE src/server/feedgen_server.c)
  # Generic upstream→downstream WebSocket subscription relay (builds on the XRPC
  # server and the always-built WebSocket client transport).
  target_sources(wolfram PRIVATE src/server/relay_server.c)
  # Service-token / Bearer auth middleware (built on the XRPC server + the
  # OAuth/service-auth verification primitives).
  target_sources(wolfram PRIVATE src/server/xrpc_server_auth.c)
  # Note: repo_store has been moved to MetalBear project
  target_include_directories(wolfram PRIVATE ${MICROHTTPD_INCLUDE_DIR})
  target_link_libraries(wolfram PUBLIC ${MICROHTTPD_LIBRARY} Threads::Threads)

  # SQLite3 for durable repo storage.
  find_package(SQLite3 QUIET)
  if(NOT SQLite3_FOUND)
    find_path(SQLITE3_INCLUDE_DIR NAMES sqlite3.h)
    find_library(SQLITE3_LIBRARY NAMES sqlite3)
    if(SQLITE3_INCLUDE_DIR AND SQLITE3_LIBRARY)
      set(SQLite3_FOUND TRUE)
      set(SQLite3_INCLUDE_DIRS ${SQLITE3_INCLUDE_DIR})
      set(SQLite3_LIBRARIES ${SQLITE3_LIBRARY})
    endif()
  endif()
  #
  # devkitPro packages no SQLite for the Wii U, and there is no system one to
  # find. Build it from the amalgamation instead, which is what SQLite ships for
  # exactly this case. The console is a single process with no other writer, so
  # it needs neither threading nor WAL nor the fcntl locking that newlib does
  # not provide — `unix-none` semantics are correct here rather than a
  # compromise.
  #
  if(NOT SQLite3_FOUND AND WOLFRAM_BUILD_WIIU)
    set(WOLFRAM_SQLITE_AMALGAMATION
        ""
        CACHE
          PATH
          "Directory holding sqlite3.c/sqlite3.h for targets with no packaged SQLite"
    )
    if(NOT WOLFRAM_SQLITE_AMALGAMATION)
      message(
        FATAL_ERROR
          "WOLFRAM_BUILD_WIIU=ON with the server needs SQLite, which "
          "devkitPro does not package. Download the amalgamation from "
          "https://sqlite.org/download.html and point "
          "-DWOLFRAM_SQLITE_AMALGAMATION= at the unpacked directory.")
    endif()
    target_sources(wolfram PRIVATE "${WOLFRAM_SQLITE_AMALGAMATION}/sqlite3.c")
    set_source_files_properties(
      "${WOLFRAM_SQLITE_AMALGAMATION}/sqlite3.c"
      PROPERTIES
        COMPILE_DEFINITIONS
        "SQLITE_THREADSAFE=0;SQLITE_OMIT_WAL=1;SQLITE_OMIT_LOAD_EXTENSION=1;SQLITE_DEFAULT_LOCKING_MODE=1;SQLITE_TEMP_STORE=3"
    )
    set(SQLite3_FOUND TRUE)
    set(SQLite3_INCLUDE_DIRS "${WOLFRAM_SQLITE_AMALGAMATION}")
    set(SQLite3_LIBRARIES "")
    message(STATUS "SQLite3 built from the amalgamation at "
                   "${WOLFRAM_SQLITE_AMALGAMATION}")
  endif()
  if(NOT SQLite3_FOUND)
    message(
      FATAL_ERROR
        "WOLFRAM_BUILD_SERVER=ON requires SQLite3 (needed by the PDS "
        "repo store) but it was not found on this system. Install it "
        "(e.g. `brew install sqlite3` / `apt install libsqlite3-dev`).")
  endif()
  target_include_directories(wolfram PRIVATE ${SQLite3_INCLUDE_DIRS})
  target_link_libraries(wolfram PRIVATE ${SQLite3_LIBRARIES})
  message(
    STATUS "SQLite3 found (${SQLite3_LIBRARIES}) — building PDS repo store")

  if(WOLFRAM_BUILD_TESTS)
    add_executable(test_xrpc_server test/test_xrpc_server.c)
    target_link_libraries(test_xrpc_server PRIVATE wolfram Threads::Threads)
    target_include_directories(
      test_xrpc_server PRIVATE test ${cjson_SOURCE_DIR}
                               ${MICROHTTPD_INCLUDE_DIR})
    add_test(NAME xrpc_server COMMAND test_xrpc_server)

    add_executable(test_xrpc_server_sse test/test_xrpc_server_sse.c)
    target_link_libraries(test_xrpc_server_sse PRIVATE wolfram Threads::Threads)
    target_include_directories(
      test_xrpc_server_sse PRIVATE test ${cjson_SOURCE_DIR}
                                   ${MICROHTTPD_INCLUDE_DIR})
    add_test(NAME xrpc_server_sse COMMAND test_xrpc_server_sse)

    add_executable(test_xrpc_server_parallel test/test_xrpc_server_parallel.c)
    target_link_libraries(test_xrpc_server_parallel PRIVATE wolfram Threads::Threads)
    target_include_directories(
      test_xrpc_server_parallel PRIVATE test ${cjson_SOURCE_DIR}
                                       ${MICROHTTPD_INCLUDE_DIR})
    add_test(NAME xrpc_server_parallel COMMAND test_xrpc_server_parallel)

    add_executable(test_xrpc_server_ws test/test_xrpc_server_ws.c)
    target_link_libraries(test_xrpc_server_ws PRIVATE wolfram Threads::Threads)
    target_include_directories(
      test_xrpc_server_ws
      PRIVATE test ${cjson_SOURCE_DIR} ${MICROHTTPD_INCLUDE_DIR}
              ${OPENSSL_INCLUDE_DIR})
    add_test(NAME xrpc_server_ws COMMAND test_xrpc_server_ws)

    add_executable(test_xrpc_server_ws_many test/test_xrpc_server_ws_many.c)
    target_link_libraries(test_xrpc_server_ws_many PRIVATE wolfram
                                                           Threads::Threads)
    target_include_directories(
      test_xrpc_server_ws_many
      PRIVATE test ${cjson_SOURCE_DIR} ${MICROHTTPD_INCLUDE_DIR}
              ${OPENSSL_INCLUDE_DIR})
    add_test(NAME xrpc_server_ws_many COMMAND test_xrpc_server_ws_many)
    # The teardown bug this covers is a use-after-free in threads that are never
    # joined, so the process can still exit 0 while corrupting memory. Make a
    # sanitizer build fail the test instead of merely printing.
    set_tests_properties(
      xrpc_server_ws_many
      PROPERTIES ENVIRONMENT "ASAN_OPTIONS=abort_on_error=1:detect_leaks=0")

    add_executable(test_xrpc_server_ws_slow_client
                   test/test_xrpc_server_ws_slow_client.c)
    target_link_libraries(test_xrpc_server_ws_slow_client
                          PRIVATE wolfram Threads::Threads)
    target_include_directories(
      test_xrpc_server_ws_slow_client
      PRIVATE test ${cjson_SOURCE_DIR} ${MICROHTTPD_INCLUDE_DIR}
              ${OPENSSL_INCLUDE_DIR})
    add_test(NAME xrpc_server_ws_slow_client
             COMMAND test_xrpc_server_ws_slow_client)

    add_executable(test_xrpc_server_ws_never_drains
                   test/test_xrpc_server_ws_never_drains.c)
    target_link_libraries(test_xrpc_server_ws_never_drains
                          PRIVATE wolfram Threads::Threads)
    target_include_directories(
      test_xrpc_server_ws_never_drains
      PRIVATE test ${cjson_SOURCE_DIR} ${MICROHTTPD_INCLUDE_DIR}
              ${OPENSSL_INCLUDE_DIR})
    add_test(NAME xrpc_server_ws_never_drains
             COMMAND test_xrpc_server_ws_never_drains)

    add_executable(test_chat_modevents_sub test/test_chat_modevents_sub.c)
    target_link_libraries(test_chat_modevents_sub PRIVATE wolfram
                                                          Threads::Threads)
    target_include_directories(
      test_chat_modevents_sub
      PRIVATE test ${cjson_SOURCE_DIR} ${MICROHTTPD_INCLUDE_DIR}
              ${OPENSSL_INCLUDE_DIR})
    # The file's Layer 2 (in-process XRPC server e2e round-trip) is gated on
    # `#if defined(WOLFRAM_BUILD_SERVER)`, but nothing previously defined that C
    # macro for any target — the CMake option only ever controlled which targets
    # got built, not a -D flag — so Layer 2 silently never compiled in even with
    # the option ON. Define it here so the e2e test the file already contains
    # actually runs.
    target_compile_definitions(test_chat_modevents_sub
                               PRIVATE WOLFRAM_BUILD_SERVER)
    add_test(NAME chat_modevents_sub COMMAND test_chat_modevents_sub)

    add_executable(test_relay_server test/test_relay_server.c)
    target_link_libraries(test_relay_server PRIVATE wolfram Threads::Threads)
    target_include_directories(
      test_relay_server
      PRIVATE test ${cjson_SOURCE_DIR} ${MICROHTTPD_INCLUDE_DIR}
              ${OPENSSL_INCLUDE_DIR})
    add_test(NAME relay_server COMMAND test_relay_server)

    add_executable(test_xrpc_server_observer test/test_xrpc_server_observer.c)
    target_link_libraries(test_xrpc_server_observer PRIVATE wolfram
                                                            Threads::Threads)
    target_include_directories(
      test_xrpc_server_observer
      PRIVATE test ${cjson_SOURCE_DIR} ${MICROHTTPD_INCLUDE_DIR}
              ${OPENSSL_INCLUDE_DIR})
    add_test(NAME xrpc_server_observer COMMAND test_xrpc_server_observer)

    add_executable(test_xrpc_server_auth test/test_xrpc_server_auth.c)
    target_link_libraries(test_xrpc_server_auth PRIVATE wolfram
                                                        Threads::Threads)
    target_include_directories(
      test_xrpc_server_auth
      PRIVATE test ${cjson_SOURCE_DIR} ${MICROHTTPD_INCLUDE_DIR}
              ${OPENSSL_INCLUDE_DIR})
    add_test(NAME xrpc_server_auth COMMAND test_xrpc_server_auth)

    add_executable(test_xrpc_server_config test/test_xrpc_server_config.c)
    target_link_libraries(test_xrpc_server_config PRIVATE wolfram
                                                          Threads::Threads)
    target_include_directories(
      test_xrpc_server_config PRIVATE test ${cjson_SOURCE_DIR}
                                      ${MICROHTTPD_INCLUDE_DIR})
    add_test(NAME xrpc_server_config COMMAND test_xrpc_server_config)

    # sync_publish → subscribeRepos server-side end-to-end round-trip.
    add_executable(test_sync_publish_e2e test/test_sync_publish_e2e.c)
    target_link_libraries(test_sync_publish_e2e PRIVATE wolfram
                                                        Threads::Threads)
    target_include_directories(
      test_sync_publish_e2e
      PRIVATE test ${cjson_SOURCE_DIR} ${MICROHTTPD_INCLUDE_DIR}
              ${OPENSSL_INCLUDE_DIR})
    add_test(NAME sync_publish_e2e COMMAND test_sync_publish_e2e)

    # sync_publish → subscribeRepos → sync_subscribe client end-to-end
    # round-trip, driven by the real subscription client.
    add_executable(test_sync_publish_server test/test_sync_publish_server.c)
    target_link_libraries(test_sync_publish_server PRIVATE wolfram
                                                           Threads::Threads)
    target_include_directories(
      test_sync_publish_server
      PRIVATE test ${cjson_SOURCE_DIR} ${MICROHTTPD_INCLUDE_DIR}
              ${OPENSSL_INCLUDE_DIR})
    add_test(NAME sync_publish_server COMMAND test_sync_publish_server)

    # Durable, writable PDS repo store + core com.atproto.repo serving.
    add_executable(test_pds_repo test/test_pds_repo.c)
    target_link_libraries(test_pds_repo PRIVATE wolfram Threads::Threads)
    target_include_directories(
      test_pds_repo PRIVATE test ${cjson_SOURCE_DIR} ${MICROHTTPD_INCLUDE_DIR}
                            ${OPENSSL_INCLUDE_DIR})
    add_test(NAME pds_repo COMMAND test_pds_repo)

    # Per-request repo/blob resolver for a multi-tenant PDS.
    add_executable(test_pds_repo_resolver test/test_pds_repo_resolver.c)
    target_link_libraries(test_pds_repo_resolver PRIVATE wolfram
                                                         Threads::Threads)
    target_include_directories(
      test_pds_repo_resolver
      PRIVATE test ${cjson_SOURCE_DIR} ${MICROHTTPD_INCLUDE_DIR}
              ${OPENSSL_INCLUDE_DIR})
    add_test(NAME pds_repo_resolver COMMAND test_pds_repo_resolver)

    add_executable(test_feedgen_skeleton test/test_feedgen_skeleton.c)
    target_link_libraries(test_feedgen_skeleton PRIVATE wolfram)
    target_include_directories(
      test_feedgen_skeleton PRIVATE test ${cjson_SOURCE_DIR}
                                    ${MICROHTTPD_INCLUDE_DIR})
    add_test(NAME feedgen_skeleton COMMAND test_feedgen_skeleton)
  endif()
endif()

target_include_directories(
  wolfram
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
          ${CMAKE_CURRENT_SOURCE_DIR}/third_party
          ${cjson_SOURCE_DIR}
          ${cjson_BINARY_DIR}
          ${base64url_SOURCE_DIR}
          ${SECP256K1_INCLUDE_DIRS})

if(NOT WOLFRAM_BUILD_EMBEDDED)
  target_link_libraries(wolfram PUBLIC CURL::libcurl cjson OpenSSL::Crypto
                                       Threads::Threads)
else()
  target_link_libraries(wolfram PUBLIC cjson)
endif()
target_link_libraries(wolfram PRIVATE libcbor::libcbor)

# stb_image.h (vendored, third_party/) emits warnings under -Wextra -Wpedantic.
# Silence the toolchain warnings for this single third-party TU only.
set_source_files_properties(src/image.c PROPERTIES COMPILE_FLAGS "-w")

#
# Resolve a pkg-config module's library names to absolute paths.
#
# pkg_check_modules reports bare names ("cares") plus a separate directory list.
# target_link_directories(PRIVATE) does not propagate to consumers, but the
# names do: a static libwolfram hands `-lcares` to whatever links it, without
# the `-L` that makes it findable. On Linux the libraries sit in a default
# search path so nothing noticed; on macOS with Homebrew they do not, and the
# consumer's link fails with "library 'cares' not found".
#
# Absolute paths propagate correctly and need no search path at all.
#
function(wolfram_absolute_libs out_var names dirs)
  set(resolved "")
  foreach(name ${names})
    if(IS_ABSOLUTE "${name}")
      list(APPEND resolved "${name}")
    else()
      find_library(WF_LIB_${name} NAMES ${name} HINTS ${dirs})
      if(WF_LIB_${name})
        list(APPEND resolved "${WF_LIB_${name}}")
      else()
        # Fall back to the bare name; a default-path library still links, and
        # this is no worse than before.
        list(APPEND resolved "${name}")
      endif()
    endif()
  endforeach()
  set(${out_var} "${resolved}" PARENT_SCOPE)
endfunction()

if(CARES_FOUND)
  wolfram_absolute_libs(CARES_LINK_LIBS "${CARES_LIBRARIES}"
                        "${CARES_LIBRARY_DIRS}")
  target_include_directories(wolfram PRIVATE ${CARES_INCLUDE_DIRS})
  target_link_libraries(wolfram PRIVATE ${CARES_LINK_LIBS})
  target_compile_definitions(wolfram PRIVATE HAVE_CARES)
  message(STATUS "c-ares found: ${CARES_VERSION} (${CARES_LINK_LIBS})")
endif()

if(ZSTD_FOUND)
  wolfram_absolute_libs(ZSTD_LINK_LIBS "${ZSTD_LIBRARIES}"
                        "${ZSTD_LIBRARY_DIRS}")
  target_include_directories(wolfram PRIVATE ${ZSTD_INCLUDE_DIRS})
  target_link_libraries(wolfram PRIVATE ${ZSTD_LINK_LIBS})
  target_compile_definitions(wolfram PRIVATE HAVE_LIBZSTD)
  message(STATUS "libzstd found: ${ZSTD_VERSION} (${ZSTD_LINK_LIBS})")
else()
  message(
    STATUS "libzstd not found — Jetstream compression remains unavailable")
endif()

if(NOT WOLFRAM_BUILD_WII)
  find_library(RESOLV_LIB NAMES resolv)
  if(NOT CARES_FOUND AND RESOLV_LIB)
    target_link_libraries(wolfram PUBLIC ${RESOLV_LIB})
    target_compile_definitions(wolfram PRIVATE HAVE_RESOLV)
    message(STATUS "resolv found: ${RESOLV_LIB}")
  elseif(NOT CARES_FOUND)
    message(
      STATUS
        "resolv not found — DNS TXT lookup will fall back to HTTPS well-known")
  endif()
endif()

# Optional libidn2 IDNA2008 / punycode handle resolution. Located here (after
# the wolfram target exists) so we can compile-define and privately link it.
if(WOLFRAM_BUILD_IDN)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(IDN2 libidn2)
  endif()
  if(NOT IDN2_FOUND)
    find_path(IDN2_INCLUDE_DIR NAMES idn2.h REQUIRED)
    find_library(IDN2_LIBRARY NAMES idn2 REQUIRED)
    find_library(UNISTRING_LIBRARY NAMES unistring)
    set(IDN2_INCLUDE_DIRS ${IDN2_INCLUDE_DIR})
    set(IDN2_LIBRARIES ${IDN2_LIBRARY})
    get_filename_component(IDN2_LIBRARY_DIRS "${IDN2_LIBRARY}" DIRECTORY)
    if(UNISTRING_LIBRARY)
      list(APPEND IDN2_LIBRARIES ${UNISTRING_LIBRARY})
    endif()
  endif()
  target_include_directories(wolfram PRIVATE ${IDN2_INCLUDE_DIRS})
  target_link_directories(wolfram PRIVATE ${IDN2_LIBRARY_DIRS})
  target_link_libraries(wolfram PRIVATE ${IDN2_LIBRARIES})
  target_compile_definitions(wolfram PRIVATE WOLFRAM_BUILD_IDN)
  message(
    STATUS
      "libidn2 found (${IDN2_LIBRARIES}) — building IDNA/punycode handle resolution"
  )
endif()

if(SECP256K1_FOUND AND NOT WOLFRAM_BUILD_WII)
  find_library(SECP256K1_LIB NAMES secp256k1 HINTS ${SECP256K1_LIBRARY_DIRS}
                                                   REQUIRED)
  target_include_directories(wolfram PRIVATE ${SECP256K1_INCLUDE_DIRS})
  target_link_libraries(wolfram PUBLIC ${SECP256K1_LIB})
  target_compile_definitions(wolfram PRIVATE HAVE_LIBSECP256K1)
  message(STATUS "libsecp256k1 found: ${SECP256K1_LIB}")
else()
  message(
    STATUS
      "libsecp256k1 not found — crypto stubs will return WF_ERR_INVALID_ARG")
endif()

option(WOLFRAM_BUILD_EXAMPLES "Build example programs" ON)
option(WOLFRAM_BUILD_TESTS "Build unit tests" ON)

# Optional SQLite3-backed persistence module (wf_store). OFF by default so the
# default build keeps its small dependency set. When ON, the system SQLite3
# library is required (no network FetchContent is used).
option(WOLFRAM_BUILD_STORE
       "Build the optional SQLite3-backed persistence module" OFF)

# Optional at-rest encryption of persisted sessions. Requires
# WOLFRAM_BUILD_STORE and the system libsodium library. OFF by default. When ON,
# the session blob is encrypted with libsodium (crypto_secretbox_easy) using a
# key derived from a caller-supplied passphrase via libsodium's crypto_pwhash
# (Argon2id). No network FetchContent is used — libsodium must be installed on
# the system.
option(WOLFRAM_BUILD_STORE_CRYPTO
       "Encrypt persisted sessions at rest with libsodium" OFF)

# Optional mock PDS for offline HTTP integration tests. When ON, the system
# libmicrohttpd (GNU MHD) library is required (no network FetchContent). It
# builds test/mock_pds.c and the offline integration test that points the SDK's
# XRPC client at a real local HTTP server. OFF by default so the default test
# suite has no extra dependency.
option(
  WOLFRAM_BUILD_TEST_HTTPD
  "Build the optional libmicrohttpd-backed mock PDS and offline HTTP integration test"
  OFF)

# Optional XRPC server module (libmicrohttpd-based). When ON, the system
# libmicrohttpd library is required. Adds wf_xrpc_server_start/stop/free and
# route registration APIs to the wolfram library.
option(WOLFRAM_BUILD_SERVER
       "Build the XRPC server module (requires libmicrohttpd)" OFF)

# Optional libidn2-backed IDNA2008 / punycode handle resolution. When ON, the
# system libidn2 library (GNU, MIT) is required (no network FetchContent). It
# converts Unicode handles to ASCII punycode (xn--…) before the DNS/HTTPS lookup
# and decodes punycode responses back to Unicode for display. OFF by default so
# the default build stays dependency-free and passes handles through unchanged.
# Install via `brew install libidn2` / `apt install libidn2-dev`.
option(WOLFRAM_BUILD_IDN "Build with libidn2 IDNA/punycode handle resolution"
       OFF)
option(WOLFRAM_BUILD_CPP "Build the C++ RAII wrapper (wolfram-cpp)" OFF)

if(WOLFRAM_BUILD_STORE_CRYPTO)
  set(WOLFRAM_BUILD_STORE ON CACHE BOOL "" FORCE)
endif()

if(WOLFRAM_BUILD_STORE)
  find_package(SQLite3 QUIET)
  if(NOT SQLite3_FOUND)
    find_path(SQLITE3_INCLUDE_DIR NAMES sqlite3.h)
    find_library(SQLITE3_LIBRARY NAMES sqlite3)
    if(SQLITE3_INCLUDE_DIR AND SQLITE3_LIBRARY)
      set(SQLite3_FOUND TRUE)
      set(SQLite3_INCLUDE_DIRS ${SQLITE3_INCLUDE_DIR})
      set(SQLite3_LIBRARIES ${SQLITE3_LIBRARY})
    endif()
  endif()
  if(NOT SQLite3_FOUND)
    message(
      FATAL_ERROR
        "WOLFRAM_BUILD_STORE=ON but SQLite3 was not found on this system. "
        "Install it (e.g. `brew install sqlite3` / `apt install libsqlite3-dev`) "
        "or build with WOLFRAM_BUILD_STORE=OFF.")
  endif()

  target_sources(wolfram PRIVATE src/store/sqlite_store.c)
  target_include_directories(wolfram PRIVATE ${SQLite3_INCLUDE_DIRS})
  target_link_libraries(wolfram PRIVATE ${SQLite3_LIBRARIES})
  target_compile_definitions(wolfram PRIVATE WOLFRAM_BUILD_STORE)
  message(
    STATUS
      "SQLite3 found (${SQLite3_LIBRARIES}) — building optional wf_store persistence module"
  )

  if(WOLFRAM_BUILD_STORE_CRYPTO)
    find_package(sodium QUIET)
    if(NOT sodium_FOUND)
      find_path(SODIUM_INCLUDE_DIR NAMES sodium.h)
      find_library(SODIUM_LIBRARY NAMES sodium)
      if(SODIUM_INCLUDE_DIR AND SODIUM_LIBRARY)
        set(sodium_FOUND TRUE)
        set(sodium_INCLUDE_DIRS ${SODIUM_INCLUDE_DIR})
        set(sodium_LIBRARIES ${SODIUM_LIBRARY})
      endif()
    endif()
    if(NOT sodium_FOUND)
      message(
        FATAL_ERROR
          "WOLFRAM_BUILD_STORE_CRYPTO=ON but libsodium was not found on this "
          "system. Install it (e.g. `brew install libsodium` / `apt install "
          "libsodium-dev`) or build with WOLFRAM_BUILD_STORE_CRYPTO=OFF.")
    endif()

    target_include_directories(wolfram PRIVATE ${sodium_INCLUDE_DIRS})
    target_link_libraries(wolfram PRIVATE ${sodium_LIBRARIES})
    target_compile_definitions(wolfram PRIVATE WOLFRAM_BUILD_STORE_CRYPTO)
    message(
      STATUS
        "libsodium found (${sodium_LIBRARIES}) — encrypting persisted sessions at rest"
    )
  endif()
endif()

# wolfram command-line client — a thin end-to-end demonstration over the SDK.
# Always built (not gated on WOLFRAM_BUILD_EXAMPLES) so the CLI is part of the
# default build. The target is named wolfram_cli because the wolfram library
# already owns the `wolfram` target name; OUTPUT_NAME keeps the binary
# `wolfram`.
if(NOT WOLFRAM_BUILD_EMBEDDED)
  add_executable(wolfram_cli src/cli/main.c src/cli/cli_oauth.c)
  target_link_libraries(wolfram_cli PRIVATE wolfram)
  target_include_directories(wolfram_cli PRIVATE ${cjson_SOURCE_DIR}
                                                 ${cjson_BINARY_DIR})
  set_target_properties(wolfram_cli PROPERTIES OUTPUT_NAME wolfram)
endif()

if(WOLFRAM_BUILD_EXAMPLES)
  add_executable(describe_repo examples/describe_repo.c)
  target_link_libraries(describe_repo PRIVATE wolfram)
  target_include_directories(describe_repo PRIVATE ${cjson_SOURCE_DIR})
  add_executable(oauth_session examples/oauth_session.c)
  target_include_directories(oauth_session PRIVATE ${cjson_SOURCE_DIR})
  target_link_libraries(oauth_session PRIVATE wolfram)
  add_executable(create_post examples/create_post.c)
  target_link_libraries(create_post PRIVATE wolfram)
  target_include_directories(create_post PRIVATE ${cjson_SOURCE_DIR})
  add_executable(post_image examples/post_image.c)
  target_link_libraries(post_image PRIVATE wolfram)
  target_include_directories(post_image PRIVATE ${cjson_SOURCE_DIR})
  target_link_libraries(create_post PRIVATE wolfram)
  target_include_directories(create_post PRIVATE ${cjson_SOURCE_DIR})
  add_executable(sync_head examples/sync_head.c)
  target_link_libraries(sync_head PRIVATE wolfram)
  target_include_directories(sync_head PRIVATE ${cjson_SOURCE_DIR})
  add_executable(upload_image examples/upload_image.c)
  target_link_libraries(upload_image PRIVATE wolfram)
  target_include_directories(upload_image PRIVATE ${cjson_SOURCE_DIR})
  add_executable(describe_server examples/describe_server.c)
  target_link_libraries(describe_server PRIVATE wolfram)
  target_include_directories(describe_server PRIVATE ${cjson_SOURCE_DIR})

  add_executable(reply_post examples/reply_post.c)
  target_link_libraries(reply_post PRIVATE wolfram)
  target_include_directories(reply_post PRIVATE ${cjson_SOURCE_DIR})
  target_include_directories(reply_post PRIVATE ${cjson_SOURCE_DIR})

  add_executable(agent_feed_search examples/agent_feed_search.c)
  target_link_libraries(agent_feed_search PRIVATE wolfram)
  target_include_directories(agent_feed_search PRIVATE ${cjson_SOURCE_DIR})
  target_include_directories(agent_feed_search PRIVATE ${cjson_SOURCE_DIR})

  add_executable(bsky_agent_demo examples/bsky_agent_demo.c)
  target_link_libraries(bsky_agent_demo PRIVATE wolfram)
  target_include_directories(bsky_agent_demo PRIVATE ${cjson_SOURCE_DIR})

  add_executable(agent_graph examples/agent_graph.c)
  target_link_libraries(agent_graph PRIVATE wolfram)
  target_include_directories(agent_graph PRIVATE ${cjson_SOURCE_DIR})
  target_include_directories(agent_graph PRIVATE ${cjson_SOURCE_DIR})

  add_executable(lex_client examples/lex_client.c)
  target_link_libraries(lex_client PRIVATE wolfram)
  target_include_directories(lex_client PRIVATE ${cjson_SOURCE_DIR})

  add_executable(lex_client_full examples/lex_client_full.c)
  target_link_libraries(lex_client_full PRIVATE wolfram)
  target_include_directories(lex_client_full PRIVATE ${cjson_SOURCE_DIR})
  add_executable(thread examples/thread.c)
  target_link_libraries(thread PRIVATE wolfram)
  target_include_directories(thread PRIVATE ${cjson_SOURCE_DIR})
  target_include_directories(thread PRIVATE ${cjson_SOURCE_DIR})

  add_executable(preferences_and_push examples/preferences_and_push.c)
  target_link_libraries(preferences_and_push PRIVATE wolfram)
  target_include_directories(preferences_and_push PRIVATE ${cjson_SOURCE_DIR})

  add_executable(labeler_service examples/labeler_service.c)
  target_link_libraries(labeler_service PRIVATE wolfram)
  target_include_directories(labeler_service PRIVATE ${cjson_SOURCE_DIR})

  add_executable(timeline_thread_typed examples/timeline_thread_typed.c)
  target_link_libraries(timeline_thread_typed PRIVATE wolfram)
  target_include_directories(timeline_thread_typed PRIVATE ${cjson_SOURCE_DIR})

  add_executable(moderation_demo examples/moderation_demo.c)
  target_link_libraries(moderation_demo PRIVATE wolfram)
  target_include_directories(moderation_demo PRIVATE ${cjson_SOURCE_DIR})

  add_executable(agent_moderate examples/agent_moderate.c)
  target_link_libraries(agent_moderate PRIVATE wolfram)
  target_include_directories(agent_moderate PRIVATE ${cjson_SOURCE_DIR})

  add_executable(timeline_paged examples/timeline_paged.c)
  target_link_libraries(timeline_paged PRIVATE wolfram)
  target_include_directories(timeline_paged PRIVATE ${cjson_SOURCE_DIR})

  add_executable(feed_generator examples/feed_generator.c)
  target_link_libraries(feed_generator PRIVATE wolfram)
  target_include_directories(feed_generator PRIVATE ${cjson_SOURCE_DIR})

  add_executable(post_image_embed examples/post_image_embed.c)
  target_link_libraries(post_image_embed PRIVATE wolfram)
  target_include_directories(post_image_embed PRIVATE ${cjson_SOURCE_DIR})

  add_executable(timeline_moderation examples/timeline_moderation.c)
  target_link_libraries(timeline_moderation PRIVATE wolfram)
  target_include_directories(timeline_moderation PRIVATE ${cjson_SOURCE_DIR})

  add_executable(ozone_moderation examples/ozone_moderation.c)
  target_link_libraries(ozone_moderation PRIVATE wolfram)
  target_include_directories(ozone_moderation PRIVATE ${cjson_SOURCE_DIR})
  target_link_libraries(timeline_moderation PRIVATE wolfram)
  target_include_directories(timeline_moderation PRIVATE ${cjson_SOURCE_DIR})

  add_executable(get_record examples/get_record.c)
  target_link_libraries(get_record PRIVATE wolfram)
  target_include_directories(get_record PRIVATE ${cjson_SOURCE_DIR})
  add_executable(subscribe_labels examples/subscribe_labels.c)
  target_link_libraries(subscribe_labels PRIVATE wolfram)
  target_include_directories(subscribe_labels PRIVATE ${cjson_SOURCE_DIR})
  add_executable(create_account examples/create_account.c)
  target_link_libraries(create_account PRIVATE wolfram)
  target_include_directories(create_account PRIVATE ${cjson_SOURCE_DIR})
  add_executable(create_app_password examples/create_app_password.c)
  target_link_libraries(create_app_password PRIVATE wolfram)
  target_include_directories(create_app_password PRIVATE ${cjson_SOURCE_DIR})

  add_executable(get_thread examples/get_thread.c)
  target_link_libraries(get_thread PRIVATE wolfram)
  target_include_directories(get_thread PRIVATE ${cjson_SOURCE_DIR})
  add_executable(list_notifications examples/list_notifications.c)
  target_link_libraries(list_notifications PRIVATE wolfram)
  target_include_directories(list_notifications PRIVATE ${cjson_SOURCE_DIR})
  add_executable(list_convos examples/list_convos.c)
  target_link_libraries(list_convos PRIVATE wolfram)
  target_include_directories(list_convos PRIVATE ${cjson_SOURCE_DIR})

  add_executable(validate_record examples/validate_record.c)
  target_link_libraries(validate_record PRIVATE wolfram)
  target_include_directories(validate_record PRIVATE ${cjson_SOURCE_DIR})

  add_executable(example_label_query examples/label_query.c)
  target_link_libraries(example_label_query PRIVATE wolfram)
  target_include_directories(example_label_query PRIVATE ${cjson_SOURCE_DIR})

  add_executable(example_identity_rotate examples/identity_rotate.c)
  target_link_libraries(example_identity_rotate PRIVATE wolfram)
  target_include_directories(example_identity_rotate
                             PRIVATE ${cjson_SOURCE_DIR})

  add_executable(example_notification_v2 examples/notification_v2.c)
  target_link_libraries(example_notification_v2 PRIVATE wolfram)
  target_include_directories(example_notification_v2
                             PRIVATE ${cjson_SOURCE_DIR})

  add_executable(example_admin_search examples/admin_search.c)
  target_link_libraries(example_admin_search PRIVATE wolfram)
  target_include_directories(example_admin_search PRIVATE ${cjson_SOURCE_DIR})

  add_executable(mint_did test/mint_did.c)
  target_link_libraries(mint_did PRIVATE wolfram)
  target_include_directories(mint_did PRIVATE test ${cjson_SOURCE_DIR})
endif()

if(WOLFRAM_BUILD_TESTS)
  enable_testing()

  add_executable(test_lexgen test/test_lexgen.cpp)
  set_target_properties(test_lexgen PROPERTIES CXX_STANDARD 23)
  target_link_libraries(test_lexgen PRIVATE cjson OpenSSL::Crypto)
  target_include_directories(
    test_lexgen PRIVATE ${cjson_SOURCE_DIR} ${cjson_BINARY_DIR}
                        ${OPENSSL_INCLUDE_DIR})
  target_compile_definitions(
    test_lexgen
    PRIVATE WF_TEST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
            WF_TEST_GENERATOR="$<TARGET_FILE:wf_lexgen_tool>"
            WF_TEST_CJSON_INCLUDE="${cjson_SOURCE_DIR}"
            WF_TEST_CJSON_LIB="${cjson_BINARY_DIR}"
            WF_TEST_OPENSSL_INCLUDE="${OPENSSL_INCLUDE_DIR}"
            WF_TEST_OPENSSL_CRYPTO="${OPENSSL_CRYPTO_LIBRARY}")
  add_test(NAME lexgen COMMAND test_lexgen)

  add_executable(test_xrpc test/test_xrpc.c)
  target_link_libraries(test_xrpc PRIVATE wolfram)
  target_include_directories(test_xrpc PRIVATE test)
  add_test(NAME xrpc COMMAND test_xrpc)

  if(NOT WOLFRAM_BUILD_EMBEDDED)
    add_executable(test_xrpc_wii_host test/test_xrpc_wii_host.c
                                      src/transport/xrpc_wii.c)
    target_include_directories(test_xrpc_wii_host PRIVATE include src
                                                          ${cjson_SOURCE_DIR})
    target_link_libraries(test_xrpc_wii_host PRIVATE cjson)
    target_compile_definitions(
      test_xrpc_wii_host
      PRIVATE WOLFRAM_VERSION_MAJOR=${WOLFRAM_VERSION_MAJOR}
              WOLFRAM_VERSION_MINOR=${WOLFRAM_VERSION_MINOR}
              WOLFRAM_VERSION_PATCH=${WOLFRAM_VERSION_PATCH}
              WOLFRAM_VERSION_STRING="${PROJECT_VERSION}")
    add_test(NAME xrpc_wii_host COMMAND test_xrpc_wii_host)
  endif()

  add_executable(test_identity test/test_identity.c)
  target_link_libraries(test_identity PRIVATE wolfram)
  target_include_directories(test_identity PRIVATE test)
  target_compile_definitions(
    test_identity
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME identity COMMAND test_identity)

  add_executable(test_repo test/test_repo.c)
  target_link_libraries(test_repo PRIVATE wolfram)
  target_include_directories(test_repo PRIVATE test)
  add_test(NAME repo COMMAND test_repo)

  add_executable(test_cbor_extra test/test_cbor_extra.c)
  target_link_libraries(test_cbor_extra PRIVATE wolfram)
  target_include_directories(test_cbor_extra PRIVATE test)
  add_test(NAME cbor_extra COMMAND test_cbor_extra)

  add_executable(test_drisl test/test_drisl.c)
  target_link_libraries(test_drisl PRIVATE wolfram)
  target_include_directories(test_drisl PRIVATE test)
  add_test(NAME drisl COMMAND test_drisl)

  add_executable(test_record test/test_record.c)
  target_link_libraries(test_record PRIVATE wolfram)
  target_include_directories(test_record PRIVATE test)
  add_test(NAME record COMMAND test_record)

  add_executable(test_crypto test/test_crypto.c)
  target_link_libraries(test_crypto PRIVATE wolfram)
  target_include_directories(test_crypto PRIVATE test)
  if(SECP256K1_FOUND)
    target_compile_definitions(test_crypto PRIVATE HAVE_LIBSECP256K1)
  endif()
  add_test(NAME crypto COMMAND test_crypto)

  add_executable(test_crypto_interop test/test_crypto_interop.c)
  target_link_libraries(test_crypto_interop PRIVATE wolfram)
  target_include_directories(test_crypto_interop PRIVATE test
                                                         ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_crypto_interop
    PRIVATE
      WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/crypto")
  if(SECP256K1_FOUND)
    target_compile_definitions(test_crypto_interop PRIVATE HAVE_LIBSECP256K1)
  endif()
  add_test(NAME crypto_interop COMMAND test_crypto_interop)

  add_executable(test_car_interop test/test_car_interop.c)
  target_link_libraries(test_car_interop PRIVATE wolfram)
  target_include_directories(test_car_interop PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_car_interop
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME car_interop COMMAND test_car_interop)

  add_executable(test_car_malformed test/test_car_malformed.c)
  target_link_libraries(test_car_malformed PRIVATE wolfram)
  target_include_directories(test_car_malformed PRIVATE test)
  add_test(NAME car_malformed COMMAND test_car_malformed)

  add_executable(test_dagcbor_interop test/test_dagcbor_interop.c)
  target_link_libraries(test_dagcbor_interop PRIVATE wolfram)
  target_include_directories(test_dagcbor_interop PRIVATE test
                                                          ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_dagcbor_interop
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME dagcbor_interop COMMAND test_dagcbor_interop)

  add_executable(test_mst_interop test/test_mst_interop.c)
  target_link_libraries(test_mst_interop PRIVATE wolfram)
  target_include_directories(test_mst_interop PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_mst_interop
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME mst_interop COMMAND test_mst_interop)

  add_executable(test_mst_depth test/test_mst_depth.c)
  target_link_libraries(test_mst_depth PRIVATE wolfram)
  target_include_directories(test_mst_depth PRIVATE test)
  add_test(NAME mst_depth COMMAND test_mst_depth)

  add_executable(test_session test/test_session.c)
  target_link_libraries(test_session PRIVATE wolfram)
  target_include_directories(test_session PRIVATE test)
  add_test(NAME session COMMAND test_session)

  add_executable(test_sync test/test_sync.c)
  target_link_libraries(test_sync PRIVATE wolfram)
  target_include_directories(test_sync PRIVATE test)
  add_test(NAME sync COMMAND test_sync)

  add_executable(test_jetstream test/test_jetstream.c)
  target_link_libraries(test_jetstream PRIVATE wolfram)
  target_include_directories(test_jetstream PRIVATE test ${cjson_SOURCE_DIR}
                                                    ${cjson_BINARY_DIR})
  if(ZSTD_FOUND)
    target_include_directories(test_jetstream PRIVATE ${ZSTD_INCLUDE_DIRS})
    target_link_directories(test_jetstream PRIVATE ${ZSTD_LIBRARY_DIRS})
    target_link_libraries(test_jetstream PRIVATE ${ZSTD_LIBRARIES})
    target_compile_definitions(test_jetstream PRIVATE HAVE_LIBZSTD)
  endif()
  add_test(NAME jetstream COMMAND test_jetstream)

  add_executable(test_jetstream_typed test/test_jetstream_typed.c)
  target_link_libraries(test_jetstream_typed PRIVATE wolfram)
  target_include_directories(
    test_jetstream_typed PRIVATE test ${cjson_SOURCE_DIR} ${cjson_BINARY_DIR})
  add_test(NAME jetstream_typed COMMAND test_jetstream_typed)

  add_executable(test_label_query test/test_label_query.c)
  target_link_libraries(test_label_query PRIVATE wolfram)
  target_include_directories(test_label_query PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME label_query COMMAND test_label_query)

  add_executable(test_label_subscribe_typed test/test_label_subscribe_typed.c)
  target_link_libraries(test_label_subscribe_typed PRIVATE wolfram)
  target_include_directories(test_label_subscribe_typed
                             PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME label_subscribe_typed COMMAND test_label_subscribe_typed)

  add_executable(test_label_verify test/test_label_verify.c)
  target_link_libraries(test_label_verify PRIVATE wolfram)
  target_include_directories(test_label_verify PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME label_verify COMMAND test_label_verify)

  add_executable(test_oauth test/test_oauth.c)
  target_link_libraries(test_oauth PRIVATE wolfram)
  target_include_directories(test_oauth PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME oauth COMMAND test_oauth)

  add_executable(test_oauth_refresh test/test_oauth_refresh.c)
  target_link_libraries(test_oauth_refresh PRIVATE wolfram)
  target_include_directories(test_oauth_refresh PRIVATE test
                                                        ${cjson_SOURCE_DIR})
  add_test(NAME oauth_refresh COMMAND test_oauth_refresh)

  add_executable(test_oauth_verify test/test_oauth_verify.c)
  target_link_libraries(test_oauth_verify PRIVATE wolfram)
  target_include_directories(test_oauth_verify PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME oauth_verify COMMAND test_oauth_verify)

  add_executable(test_syntax test/test_syntax.c)
  target_link_libraries(test_syntax PRIVATE wolfram)
  target_include_directories(test_syntax PRIVATE test)
  add_test(NAME syntax COMMAND test_syntax)

  add_executable(test_syntax_extra test/test_syntax_extra.c)
  target_link_libraries(test_syntax_extra PRIVATE wolfram)
  target_include_directories(test_syntax_extra PRIVATE test)
  add_test(NAME syntax_extra COMMAND test_syntax_extra)

  add_executable(test_tid test/test_tid.c)
  target_link_libraries(test_tid PRIVATE wolfram)
  target_include_directories(test_tid PRIVATE test)
  add_test(NAME tid COMMAND test_tid)

  add_executable(test_syntax_interop test/test_syntax_interop.c)
  target_link_libraries(test_syntax_interop PRIVATE wolfram)
  target_include_directories(test_syntax_interop PRIVATE test)
  target_compile_definitions(
    test_syntax_interop
    PRIVATE
      SYNTAX_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/syntax")
  add_test(NAME syntax_interop COMMAND test_syntax_interop)

  add_executable(test_richtext test/test_richtext.c)
  target_link_libraries(test_richtext PRIVATE wolfram)
  target_include_directories(test_richtext PRIVATE test)
  add_test(NAME richtext COMMAND test_richtext)

  add_executable(test_richtext_extra test/test_richtext_extra.c)
  target_link_libraries(test_richtext_extra PRIVATE wolfram)
  target_include_directories(test_richtext_extra PRIVATE test)
  add_test(NAME richtext_extra COMMAND test_richtext_extra)

  add_executable(test_sync_verify test/test_sync_verify.c)
  target_link_libraries(test_sync_verify PRIVATE wolfram)
  target_include_directories(test_sync_verify PRIVATE test)
  add_test(NAME sync_verify COMMAND test_sync_verify)

  add_executable(test_verify_record test/test_verify_record.c)
  target_link_libraries(test_verify_record PRIVATE wolfram)
  target_include_directories(test_verify_record PRIVATE test)
  target_include_directories(test_verify_record PRIVATE ${cjson_SOURCE_DIR})
  add_test(NAME verify_record COMMAND test_verify_record)

  add_executable(test_verify_resolver test/test_verify_resolver.c)
  target_link_libraries(test_verify_resolver PRIVATE wolfram)
  target_include_directories(test_verify_resolver PRIVATE test)
  target_include_directories(test_verify_resolver PRIVATE ${cjson_SOURCE_DIR})
  add_test(NAME verify_resolver COMMAND test_verify_resolver)

  add_executable(test_sync_subscribe test/test_sync_subscribe.c)
  target_link_libraries(test_sync_subscribe PRIVATE wolfram)
  target_include_directories(test_sync_subscribe PRIVATE test)
  add_test(NAME sync_subscribe COMMAND test_sync_subscribe)
  set_tests_properties(sync_subscribe PROPERTIES TIMEOUT 30)

  add_executable(test_sync_publish test/test_sync_publish.c)
  target_link_libraries(test_sync_publish PRIVATE wolfram libcbor::libcbor)
  target_include_directories(
    test_sync_publish
    PRIVATE test
            $<TARGET_PROPERTY:libcbor::libcbor,INTERFACE_INCLUDE_DIRECTORIES>)
  add_test(NAME sync_publish COMMAND test_sync_publish)

  add_executable(test_blob test/test_blob.c)
  target_link_libraries(test_blob PRIVATE wolfram)
  target_include_directories(test_blob PRIVATE test)
  add_test(NAME blob COMMAND test_blob)

  add_executable(test_agent_sync test/test_agent_sync.c)
  target_link_libraries(test_agent_sync PRIVATE wolfram)
  target_include_directories(test_agent_sync PRIVATE ${cjson_SOURCE_DIR})
  target_include_directories(test_agent_sync PRIVATE test)
  add_test(NAME agent_sync COMMAND test_agent_sync)

  add_executable(test_graph_lists test/test_graph_lists.c)
  target_link_libraries(test_graph_lists PRIVATE wolfram)
  target_include_directories(test_graph_lists PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME graph_lists COMMAND test_graph_lists)

  add_executable(test_agent_repo test/test_agent_repo.c)
  target_link_libraries(test_agent_repo PRIVATE wolfram)
  target_include_directories(test_agent_repo PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME agent_repo COMMAND test_agent_repo)

  add_executable(test_server test/test_server.c)
  target_link_libraries(test_server PRIVATE wolfram)
  target_include_directories(test_server PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME server COMMAND test_server)

  add_executable(test_plc test/test_plc.c)
  target_link_libraries(test_plc PRIVATE wolfram)
  target_include_directories(test_plc PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME plc COMMAND test_plc)

  add_executable(test_identity_mgmt test/test_identity_mgmt.c)
  target_link_libraries(test_identity_mgmt PRIVATE wolfram)
  target_include_directories(test_identity_mgmt PRIVATE test
                                                        ${cjson_SOURCE_DIR})
  add_test(NAME identity_mgmt COMMAND test_identity_mgmt)

  add_executable(test_diff test/test_diff.c)
  target_link_libraries(test_diff PRIVATE wolfram)
  target_include_directories(test_diff PRIVATE test)
  add_test(NAME diff COMMAND test_diff)

  add_executable(test_validate test/test_validate.c)
  target_link_libraries(test_validate PRIVATE wolfram)
  target_include_directories(test_validate PRIVATE test)
  add_test(NAME validate COMMAND test_validate)

  add_executable(test_validate_grapheme test/test_validate_grapheme.c)
  target_link_libraries(test_validate_grapheme PRIVATE wolfram)
  target_include_directories(test_validate_grapheme PRIVATE test)
  add_test(NAME validate_grapheme COMMAND test_validate_grapheme)

  if(WOLFRAM_BUILD_IDN)
    add_executable(test_idn test/test_idn.c)
    target_link_libraries(test_idn PRIVATE wolfram ${IDN2_LIBRARIES})
    target_link_directories(test_idn PRIVATE ${IDN2_LIBRARY_DIRS})
    target_include_directories(test_idn PRIVATE test ${IDN2_INCLUDE_DIRS})
    target_compile_definitions(test_idn PRIVATE WOLFRAM_BUILD_IDN)
    add_test(NAME idn COMMAND test_idn)
  endif()

  add_executable(test_validate_corpus test/test_validate_corpus.c)
  target_link_libraries(test_validate_corpus PRIVATE wolfram)
  target_include_directories(test_validate_corpus PRIVATE test)
  # Default to the lexicon snapshot checked in at lexicons/, so the corpus is
  # actually validated everywhere rather than only on a machine that happens to
  # have an upstream atproto checkout at one absolute path — which is what was
  # hardcoded here, so this test SKIPped on CI and for every contributor.
  # WF_ATPROTO_LEXICONS in the environment still overrides, for checking against
  # a newer upstream tree.
  target_compile_definitions(
    test_validate_corpus
    PRIVATE WF_ATPROTO_LEXICONS="${CMAKE_CURRENT_SOURCE_DIR}/lexicons")
  add_test(NAME validate_corpus COMMAND test_validate_corpus)

  add_executable(test_embed test/test_embed.c)
  target_link_libraries(test_embed PRIVATE wolfram)
  target_include_directories(test_embed PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME embed COMMAND test_embed)

  add_executable(test_embed_typed test/test_embed_typed.c)
  target_link_libraries(test_embed_typed PRIVATE wolfram)
  target_include_directories(test_embed_typed PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME embed_typed COMMAND test_embed_typed)

  add_executable(test_image_aspect test/test_image_aspect.c)
  target_link_libraries(test_image_aspect PRIVATE wolfram)
  target_include_directories(test_image_aspect PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME image_aspect COMMAND test_image_aspect)

  add_executable(test_agent_preferences test/test_agent_preferences.c)
  target_link_libraries(test_agent_preferences PRIVATE wolfram)
  target_include_directories(test_agent_preferences PRIVATE test
                                                            ${cjson_SOURCE_DIR})
  add_test(NAME agent_preferences COMMAND test_agent_preferences)

  add_executable(test_agent_notifications test/test_agent_notifications.c)
  target_link_libraries(test_agent_notifications PRIVATE wolfram)
  target_include_directories(test_agent_notifications
                             PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_agent_notifications
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME agent_notifications COMMAND test_agent_notifications)

  add_executable(test_feed_typed test/test_feed_typed.c)
  target_link_libraries(test_feed_typed PRIVATE wolfram)
  target_include_directories(test_feed_typed PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_feed_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME feed_typed COMMAND test_feed_typed)

  add_executable(test_bookmark_typed test/test_bookmark_typed.c)
  target_link_libraries(test_bookmark_typed PRIVATE wolfram)
  target_include_directories(test_bookmark_typed PRIVATE test
                                                         ${cjson_SOURCE_DIR})
  add_test(NAME bookmark_typed COMMAND test_bookmark_typed)

  add_executable(test_thread_typed test/test_thread_typed.c)
  target_link_libraries(test_thread_typed PRIVATE wolfram)
  target_include_directories(test_thread_typed PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_thread_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME thread_typed COMMAND test_thread_typed)

  add_executable(test_graph_typed test/test_graph_typed.c)
  target_link_libraries(test_graph_typed PRIVATE wolfram)
  target_include_directories(test_graph_typed PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_graph_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME graph_typed COMMAND test_graph_typed)

  add_executable(test_graph_social_typed test/test_graph_social_typed.c)
  target_link_libraries(test_graph_social_typed PRIVATE wolfram)
  target_include_directories(test_graph_social_typed
                             PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_graph_social_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME graph_social_typed COMMAND test_graph_social_typed)

  add_executable(test_actor_typed test/test_actor_typed.c)
  target_link_libraries(test_actor_typed PRIVATE wolfram)
  target_include_directories(test_actor_typed PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_actor_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME actor_typed COMMAND test_actor_typed)

  add_executable(test_actor_prefs_typed test/test_actor_prefs_typed.c)
  target_link_libraries(test_actor_prefs_typed PRIVATE wolfram)
  target_include_directories(test_actor_prefs_typed PRIVATE test
                                                            ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_actor_prefs_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME actor_prefs_typed COMMAND test_actor_prefs_typed)

  add_executable(test_actor_status_typed test/test_actor_status_typed.c)
  target_link_libraries(test_actor_status_typed PRIVATE wolfram)
  target_include_directories(test_actor_status_typed
                             PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_actor_status_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME actor_status_typed COMMAND test_actor_status_typed)

  add_executable(test_sync_typed test/test_sync_typed.c)
  target_link_libraries(test_sync_typed PRIVATE wolfram)
  target_include_directories(test_sync_typed PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_sync_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME sync_typed COMMAND test_sync_typed)

  add_executable(test_sync_list_typed test/test_sync_list_typed.c)
  target_link_libraries(test_sync_list_typed PRIVATE wolfram)
  target_include_directories(test_sync_list_typed PRIVATE test
                                                          ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_sync_list_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME sync_list_typed COMMAND test_sync_list_typed)

  add_executable(test_ageassurance_typed test/test_ageassurance_typed.c)
  target_link_libraries(test_ageassurance_typed PRIVATE wolfram)
  target_include_directories(test_ageassurance_typed
                             PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_ageassurance_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME ageassurance_typed COMMAND test_ageassurance_typed)

  add_executable(test_list_typed test/test_list_typed.c)
  target_link_libraries(test_list_typed PRIVATE wolfram)
  target_include_directories(test_list_typed PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_list_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME list_typed COMMAND test_list_typed)

  add_executable(test_draft_typed test/test_draft_typed.c)
  target_link_libraries(test_draft_typed PRIVATE wolfram)
  target_include_directories(test_draft_typed PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_draft_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME draft_typed COMMAND test_draft_typed)

  add_executable(test_temp_typed test/test_temp_typed.c)
  target_link_libraries(test_temp_typed PRIVATE wolfram)
  target_include_directories(test_temp_typed PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME temp_typed COMMAND test_temp_typed)

  add_executable(test_moderation_typed test/test_moderation_typed.c)
  target_link_libraries(test_moderation_typed PRIVATE wolfram)
  target_include_directories(test_moderation_typed PRIVATE test
                                                           ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_moderation_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME moderation_typed COMMAND test_moderation_typed)

  add_executable(test_moderation_report_typed
                 test/test_moderation_report_typed.c)
  target_link_libraries(test_moderation_report_typed PRIVATE wolfram)
  target_include_directories(test_moderation_report_typed
                             PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_moderation_report_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME moderation_report_typed COMMAND test_moderation_report_typed)

  add_executable(test_feedgen_typed test/test_feedgen_typed.c)
  target_link_libraries(test_feedgen_typed PRIVATE wolfram)
  target_include_directories(test_feedgen_typed PRIVATE test
                                                        ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_feedgen_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME feedgen_typed COMMAND test_feedgen_typed)

  add_executable(test_feed_gen_typed test/test_feed_gen_typed.c)
  target_link_libraries(test_feed_gen_typed PRIVATE wolfram)
  target_include_directories(test_feed_gen_typed PRIVATE test
                                                         ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_feed_gen_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME feed_gen_typed COMMAND test_feed_gen_typed)

  add_executable(test_feedgen test/test_feedgen.c)
  target_link_libraries(test_feedgen PRIVATE wolfram)
  target_include_directories(test_feedgen PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME feedgen COMMAND test_feedgen)

  add_executable(test_unspecced_typed test/test_unspecced_typed.c)
  target_link_libraries(test_unspecced_typed PRIVATE wolfram)
  target_include_directories(test_unspecced_typed PRIVATE test
                                                          ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_unspecced_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME unspecced_typed COMMAND test_unspecced_typed)

  add_executable(test_unspecced_trends_typed test/test_unspecced_trends_typed.c)
  target_link_libraries(test_unspecced_trends_typed PRIVATE wolfram)
  target_include_directories(test_unspecced_trends_typed
                             PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_unspecced_trends_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME unspecced_trends_typed COMMAND test_unspecced_trends_typed)

  add_executable(test_chat_typed test/test_chat_typed.c)
  target_link_libraries(test_chat_typed PRIVATE wolfram)
  target_include_directories(test_chat_typed PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_chat_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME chat_typed COMMAND test_chat_typed)

  add_executable(test_ozone_extra_typed test/test_ozone_extra_typed.c)
  target_link_libraries(test_ozone_extra_typed PRIVATE wolfram)
  target_include_directories(test_ozone_extra_typed PRIVATE test
                                                            ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_ozone_extra_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME ozone_extra_typed COMMAND test_ozone_extra_typed)

  add_executable(test_contact_typed test/test_contact_typed.c)
  target_link_libraries(test_contact_typed PRIVATE wolfram)
  target_include_directories(test_contact_typed PRIVATE test
                                                        ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_contact_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME contact_typed COMMAND test_contact_typed)

  add_executable(test_labeler_typed test/test_labeler_typed.c)
  target_link_libraries(test_labeler_typed PRIVATE wolfram)
  target_include_directories(test_labeler_typed PRIVATE test
                                                        ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_labeler_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME labeler_typed COMMAND test_labeler_typed)

  add_executable(test_video_typed test/test_video_typed.c)
  target_link_libraries(test_video_typed PRIVATE wolfram)
  target_include_directories(test_video_typed PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME video_typed COMMAND test_video_typed)

  add_executable(test_identity_typed test/test_identity_typed.c)
  target_link_libraries(test_identity_typed PRIVATE wolfram)
  target_include_directories(test_identity_typed PRIVATE test
                                                         ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_identity_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME identity_typed COMMAND test_identity_typed)

  add_executable(test_notification_v2_typed test/test_notification_v2_typed.c)
  target_link_libraries(test_notification_v2_typed PRIVATE wolfram)
  target_include_directories(test_notification_v2_typed
                             PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_notification_v2_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME notification_v2_typed COMMAND test_notification_v2_typed)

  add_executable(test_notification_v2_prefs_typed
                 test/test_notification_v2_prefs.c)
  target_link_libraries(test_notification_v2_prefs_typed PRIVATE wolfram)
  target_include_directories(test_notification_v2_prefs_typed
                             PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME notification_v2_prefs_typed
           COMMAND test_notification_v2_prefs_typed)

  add_executable(test_notification_typed test/test_notification_typed.c)
  target_link_libraries(test_notification_typed PRIVATE wolfram)
  target_include_directories(test_notification_typed
                             PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_notification_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME notification_typed COMMAND test_notification_typed)

  add_executable(test_repo_typed test/test_repo_typed.c)
  target_link_libraries(test_repo_typed PRIVATE wolfram)
  target_include_directories(test_repo_typed PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_repo_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME repo_typed COMMAND test_repo_typed)

  add_executable(test_chat_write test/test_chat_write.c)
  target_link_libraries(test_chat_write PRIVATE wolfram)
  target_include_directories(test_chat_write PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_chat_write
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME chat_write COMMAND test_chat_write)

  add_executable(examples_live test/test_examples_live.c)
  target_link_libraries(examples_live PRIVATE wolfram)
  target_include_directories(examples_live PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME examples_live COMMAND examples_live)

  add_executable(test_moderation_actions test/test_moderation_actions.c)
  target_link_libraries(test_moderation_actions PRIVATE wolfram)
  target_include_directories(test_moderation_actions
                             PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_moderation_actions
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME moderation_actions COMMAND test_moderation_actions)

  add_executable(test_admin_typed test/test_admin_typed.c)
  target_link_libraries(test_admin_typed PRIVATE wolfram)
  target_include_directories(test_admin_typed PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME admin_typed COMMAND test_admin_typed)

  add_executable(test_server_typed test/test_server_typed.c)
  target_link_libraries(test_server_typed PRIVATE wolfram)
  target_include_directories(test_server_typed PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_server_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME server_typed COMMAND test_server_typed)

  add_executable(test_service_auth test/test_service_auth.c)
  target_link_libraries(test_service_auth PRIVATE wolfram)
  target_include_directories(test_service_auth PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME service_auth COMMAND test_service_auth)

  add_executable(test_lexicon_typed test/test_lexicon_typed.c)
  target_link_libraries(test_lexicon_typed PRIVATE wolfram)
  target_include_directories(test_lexicon_typed PRIVATE test
                                                        ${cjson_SOURCE_DIR})
  add_test(NAME lexicon_typed COMMAND test_lexicon_typed)

  add_executable(test_lexcall test/test_lexcall.c)
  target_link_libraries(test_lexcall PRIVATE wolfram)
  target_include_directories(test_lexcall PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_lexcall
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME lexcall COMMAND test_lexcall)

  add_executable(test_moderation test/test_moderation.c)
  target_link_libraries(test_moderation PRIVATE wolfram)
  target_include_directories(test_moderation PRIVATE test)
  add_test(NAME moderation COMMAND test_moderation)

  add_executable(test_agent_moderation test/test_agent_moderation.c)
  target_link_libraries(test_agent_moderation PRIVATE wolfram)
  target_include_directories(test_agent_moderation PRIVATE test
                                                           ${cjson_SOURCE_DIR})
  add_test(NAME agent_moderation COMMAND test_agent_moderation)

  add_executable(test_agent_tls test/test_agent_tls.c)
  target_link_libraries(test_agent_tls PRIVATE wolfram)
  target_include_directories(test_agent_tls PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME agent_tls COMMAND test_agent_tls)

  add_executable(test_bsky_agent test/test_bsky_agent.c)
  target_link_libraries(test_bsky_agent PRIVATE wolfram)
  target_include_directories(test_bsky_agent PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME bsky_agent COMMAND test_bsky_agent)

  add_test(NAME moderation_demo COMMAND moderation_demo)

  add_executable(test_agent_pagination test/test_agent_pagination.c)
  target_link_libraries(test_agent_pagination PRIVATE wolfram)
  target_include_directories(test_agent_pagination PRIVATE test
                                                           ${cjson_SOURCE_DIR})
  add_test(NAME agent_pagination COMMAND test_agent_pagination)

  add_executable(test_json_roundtrip test/test_json_roundtrip.c)
  target_link_libraries(test_json_roundtrip PRIVATE wolfram)
  target_include_directories(test_json_roundtrip PRIVATE test
                                                         ${cjson_SOURCE_DIR})
  add_test(NAME json_roundtrip COMMAND test_json_roundtrip)

  add_executable(test_ozone test/test_ozone.c)
  target_link_libraries(test_ozone PRIVATE wolfram)
  target_include_directories(test_ozone PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME ozone COMMAND test_ozone)

  add_executable(test_ozone_typed test/test_ozone_typed.c)
  target_link_libraries(test_ozone_typed PRIVATE wolfram)
  target_include_directories(test_ozone_typed PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_ozone_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME ozone_typed COMMAND test_ozone_typed)

  add_executable(test_ozone_admin_typed test/test_ozone_admin_typed.c)
  target_link_libraries(test_ozone_admin_typed PRIVATE wolfram)
  target_include_directories(test_ozone_admin_typed PRIVATE test
                                                            ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_ozone_admin_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME ozone_admin_typed COMMAND test_ozone_admin_typed)

  add_executable(test_ozone_moderation_ops_typed
                 test/test_ozone_moderation_ops_typed.c)
  target_link_libraries(test_ozone_moderation_ops_typed PRIVATE wolfram)
  target_include_directories(test_ozone_moderation_ops_typed
                             PRIVATE test ${cjson_SOURCE_DIR})
  target_compile_definitions(
    test_ozone_moderation_ops_typed
    PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME ozone_moderation_ops_typed
           COMMAND test_ozone_moderation_ops_typed)

  add_executable(test_json_schema test/test_json_schema.c)
  target_link_libraries(test_json_schema PRIVATE wolfram)
  target_include_directories(test_json_schema PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME json_schema COMMAND test_json_schema)

  add_executable(test_json_extra test/test_json_extra.c)
  target_link_libraries(test_json_extra PRIVATE wolfram)
  target_include_directories(test_json_extra PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME json_extra COMMAND test_json_extra)

  add_executable(test_notification_prefs_typed
                 test/test_notification_prefs_typed.c)
  target_link_libraries(test_notification_prefs_typed PRIVATE wolfram)
  target_include_directories(test_notification_prefs_typed
                             PRIVATE test ${cjson_SOURCE_DIR})
  add_test(NAME notification_prefs COMMAND test_notification_prefs_typed)

  add_executable(test_agent_graph_feed test/test_agent_graph_feed.c)
  target_link_libraries(test_agent_graph_feed PRIVATE wolfram)
  target_include_directories(test_agent_graph_feed PRIVATE test
                                                           ${cjson_SOURCE_DIR})
  add_test(NAME agent_graph_feed COMMAND test_agent_graph_feed)

  if(WOLFRAM_BUILD_STORE)
    if(WOLFRAM_BUILD_STORE_CRYPTO)
      add_executable(test_store_crypto test/test_store_crypto.c)
      target_link_libraries(test_store_crypto PRIVATE wolfram)
      target_include_directories(test_store_crypto PRIVATE test)
      target_compile_definitions(
        test_store_crypto PRIVATE WOLFRAM_BUILD_STORE
                                  WOLFRAM_BUILD_STORE_CRYPTO)
      add_test(NAME store_crypto COMMAND test_store_crypto)
    else()
      add_executable(test_store test/test_store.c)
      target_link_libraries(test_store PRIVATE wolfram)
      target_include_directories(test_store PRIVATE test)
      target_compile_definitions(test_store PRIVATE WOLFRAM_BUILD_STORE)
      add_test(NAME store COMMAND test_store)
    endif()

    add_executable(test_mirror_persist test/test_mirror_persist.c)
    target_link_libraries(test_mirror_persist PRIVATE wolfram)
    target_include_directories(test_mirror_persist PRIVATE test
                                                           ${cjson_SOURCE_DIR})
    target_compile_definitions(test_mirror_persist PRIVATE WOLFRAM_BUILD_STORE)
    add_test(NAME mirror_persist COMMAND test_mirror_persist)

    add_executable(test_label_persist test/test_label_persist.c)
    target_link_libraries(test_label_persist PRIVATE wolfram)
    target_include_directories(test_label_persist PRIVATE test
                                                          ${cjson_SOURCE_DIR})
    target_compile_definitions(test_label_persist PRIVATE WOLFRAM_BUILD_STORE)
    add_test(NAME label_persist COMMAND test_label_persist)
  endif()

  if(WOLFRAM_BUILD_TEST_HTTPD)
    find_path(MICROHTTPD_INCLUDE_DIR NAMES microhttpd.h REQUIRED)
    find_library(MICROHTTPD_LIBRARY NAMES microhttpd REQUIRED)

    add_executable(test_mock_pds test/test_mock_pds.c test/mock_pds.c)
    target_link_libraries(test_mock_pds PRIVATE wolfram ${MICROHTTPD_LIBRARY})
    target_include_directories(
      test_mock_pds PRIVATE test ${MICROHTTPD_INCLUDE_DIR} ${cjson_SOURCE_DIR})
    add_test(NAME mock_pds COMMAND test_mock_pds)

    add_executable(test_graph_writes test/test_graph_writes.c test/mock_pds.c)
    target_link_libraries(test_graph_writes PRIVATE wolfram
                                                    ${MICROHTTPD_LIBRARY})
    target_include_directories(
      test_graph_writes PRIVATE test ${MICROHTTPD_INCLUDE_DIR}
                                ${cjson_SOURCE_DIR})
    add_test(NAME graph_writes COMMAND test_graph_writes)

    add_executable(test_video_typed_httpd test/test_video_typed_httpd.c
                                          test/mock_pds.c)
    target_link_libraries(test_video_typed_httpd PRIVATE wolfram
                                                         ${MICROHTTPD_LIBRARY})
    target_include_directories(
      test_video_typed_httpd PRIVATE test ${MICROHTTPD_INCLUDE_DIR}
                                     ${cjson_SOURCE_DIR})
    add_test(NAME video_typed_httpd COMMAND test_video_typed_httpd)

    add_executable(test_actor_status_typed_httpd
                   test/test_actor_status_typed_httpd.c test/mock_pds.c)
    target_link_libraries(test_actor_status_typed_httpd
                          PRIVATE wolfram ${MICROHTTPD_LIBRARY})
    target_include_directories(
      test_actor_status_typed_httpd PRIVATE test ${MICROHTTPD_INCLUDE_DIR}
                                     ${cjson_SOURCE_DIR})
    add_test(NAME actor_status_typed_httpd
             COMMAND test_actor_status_typed_httpd)
  endif()
endif()

if(WOLFRAM_BUILD_CPP)
  add_subdirectory(cpp)
endif()
