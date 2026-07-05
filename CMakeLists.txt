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

add_library(wolfram
    src/xrpc.c
    src/identity.c
    src/crypto.c
    src/repo.c
    src/record.c
    src/session.c
    src/websocket.c
    src/jetstream.c
    src/sync.c
)

target_include_directories(wolfram
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${cjson_SOURCE_DIR}
        ${cjson_BINARY_DIR}
        ${SECP256K1_INCLUDE_DIRS}
)

target_link_libraries(wolfram PUBLIC CURL::libcurl cjson OpenSSL::Crypto)

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
endif()
