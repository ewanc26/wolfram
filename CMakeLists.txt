cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0077 NEW)
project(wolfram
    VERSION 0.1.0
    DESCRIPTION "A C SDK for the AT Protocol"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
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

include(FetchContent)
FetchContent_Declare(
    cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG v1.7.18
)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
FetchContent_MakeAvailable(cjson)

FetchContent_Declare(
    libcbor
    GIT_REPOSITORY https://github.com/PJK/libcbor.git
    GIT_TAG v0.13.0
)
set(WITH_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CBOR_PRETTY_PRINTER OFF CACHE BOOL "" FORCE)
set(SANITIZE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libcbor)
set_target_properties(cbor PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(wolfram
    # Transport
    src/transport/xrpc.c
    src/transport/websocket.c
    # Identity
    src/identity/identity.c
    # Crypto
    src/crypto/crypto.c
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
    # Session
    src/session/session.c
    src/session/auth_client.c
    # OAuth (metadata, PKCE, DPoP, PAR, state, callback, flow)
    src/session/oauth/util.c
    src/session/oauth/meta.c
    src/session/oauth/pkce.c
    src/session/oauth/dpop.c
    src/session/oauth/par.c
    src/session/oauth/state.c
    src/session/oauth/callback.c
    src/session/oauth/flow.c
    # Jetstream
    src/jetstream/jetstream.c
    # Syntax validation
    src/syntax/syntax.c
    # Rich text / facet detection
    src/richtext/richtext.c
    src/richtext/detection.c
    # Firehose subscribeRepos subscription
    src/sync/sync_subscribe.c
    # Firehose commit verification
    src/sync/sync_verify.c
    # Generated lexicon endpoint wrappers
    src/atproto_lex.c
)

target_include_directories(wolfram
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${cjson_SOURCE_DIR}
        ${cjson_BINARY_DIR}
        ${SECP256K1_INCLUDE_DIRS}
)

target_link_libraries(wolfram PUBLIC CURL::libcurl cjson OpenSSL::Crypto)
target_link_libraries(wolfram PRIVATE libcbor::libcbor)

if(CARES_FOUND)
    target_include_directories(wolfram PRIVATE ${CARES_INCLUDE_DIRS})
    target_link_directories(wolfram PRIVATE ${CARES_LIBRARY_DIRS})
    target_link_libraries(wolfram PRIVATE ${CARES_LIBRARIES})
    target_compile_definitions(wolfram PRIVATE HAVE_CARES)
    message(STATUS "c-ares found: ${CARES_VERSION}")
endif()

if(ZSTD_FOUND)
    target_include_directories(wolfram PRIVATE ${ZSTD_INCLUDE_DIRS})
    target_link_directories(wolfram PRIVATE ${ZSTD_LIBRARY_DIRS})
    target_link_libraries(wolfram PRIVATE ${ZSTD_LIBRARIES})
    target_compile_definitions(wolfram PRIVATE HAVE_LIBZSTD)
    message(STATUS "libzstd found: ${ZSTD_VERSION}")
else()
    message(STATUS "libzstd not found — Jetstream compression remains unavailable")
endif()

find_library(RESOLV_LIB NAMES resolv)
if(NOT CARES_FOUND AND RESOLV_LIB)
    target_link_libraries(wolfram PUBLIC ${RESOLV_LIB})
    target_compile_definitions(wolfram PRIVATE HAVE_RESOLV)
    message(STATUS "resolv found: ${RESOLV_LIB}")
elseif(NOT CARES_FOUND)
    message(STATUS "resolv not found — DNS TXT lookup will fall back to HTTPS well-known")
endif()

if(SECP256K1_FOUND)
    find_library(SECP256K1_LIB
        NAMES secp256k1
        HINTS ${SECP256K1_LIBRARY_DIRS}
        REQUIRED
    )
    target_include_directories(wolfram PRIVATE ${SECP256K1_INCLUDE_DIRS})
    target_link_libraries(wolfram PUBLIC ${SECP256K1_LIB})
    target_compile_definitions(wolfram PRIVATE HAVE_LIBSECP256K1)
    message(STATUS "libsecp256k1 found: ${SECP256K1_LIB}")
else()
    message(STATUS "libsecp256k1 not found — crypto stubs will return WF_ERR_INVALID_ARG")
endif()

option(WOLFRAM_BUILD_EXAMPLES "Build example programs" ON)
option(WOLFRAM_BUILD_TESTS "Build unit tests" ON)

if(WOLFRAM_BUILD_EXAMPLES)
    add_executable(describe_repo examples/describe_repo.c)
    target_link_libraries(describe_repo PRIVATE wolfram)
    add_executable(oauth_session examples/oauth_session.c)
    target_link_libraries(oauth_session PRIVATE wolfram)
endif()

if(WOLFRAM_BUILD_TESTS)
    enable_testing()

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME lexgen
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_CURRENT_SOURCE_DIR}/test
            -p test_lexgen.py
    )

    add_executable(test_xrpc test/test_xrpc.c)
    target_link_libraries(test_xrpc PRIVATE wolfram)
    target_include_directories(test_xrpc PRIVATE test)
    add_test(NAME xrpc COMMAND test_xrpc)

    add_executable(test_identity test/test_identity.c)
    target_link_libraries(test_identity PRIVATE wolfram)
    target_include_directories(test_identity PRIVATE test)
    add_test(NAME identity COMMAND test_identity)

    add_executable(test_repo test/test_repo.c)
    target_link_libraries(test_repo PRIVATE wolfram)
    target_include_directories(test_repo PRIVATE test)
    add_test(NAME repo COMMAND test_repo)

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
    target_include_directories(test_jetstream PRIVATE test)
    if(ZSTD_FOUND)
        target_include_directories(test_jetstream PRIVATE ${ZSTD_INCLUDE_DIRS})
        target_link_directories(test_jetstream PRIVATE ${ZSTD_LIBRARY_DIRS})
        target_link_libraries(test_jetstream PRIVATE ${ZSTD_LIBRARIES})
        target_compile_definitions(test_jetstream PRIVATE HAVE_LIBZSTD)
    endif()
    add_test(NAME jetstream COMMAND test_jetstream)

    add_executable(test_oauth test/test_oauth.c)
    target_link_libraries(test_oauth PRIVATE wolfram)
    target_include_directories(test_oauth PRIVATE test ${cjson_SOURCE_DIR})
    add_test(NAME oauth COMMAND test_oauth)

    add_executable(test_syntax test/test_syntax.c)
    target_link_libraries(test_syntax PRIVATE wolfram)
    target_include_directories(test_syntax PRIVATE test)
    add_test(NAME syntax COMMAND test_syntax)

    add_executable(test_richtext test/test_richtext.c)
    target_link_libraries(test_richtext PRIVATE wolfram)
    target_include_directories(test_richtext PRIVATE test)
    add_test(NAME richtext COMMAND test_richtext)

    add_executable(test_sync_verify test/test_sync_verify.c)
    target_link_libraries(test_sync_verify PRIVATE wolfram)
    target_include_directories(test_sync_verify PRIVATE test)
    add_test(NAME sync_verify COMMAND test_sync_verify)

    add_executable(test_sync_subscribe test/test_sync_subscribe.c)
    target_link_libraries(test_sync_subscribe PRIVATE wolfram)
    target_include_directories(test_sync_subscribe PRIVATE test)
    add_test(NAME sync_subscribe COMMAND test_sync_subscribe)
    set_tests_properties(sync_subscribe PROPERTIES TIMEOUT 30)
endif()
