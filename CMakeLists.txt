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
)

target_include_directories(wolfram
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${cjson_SOURCE_DIR}
        ${cjson_BINARY_DIR}
)

target_link_libraries(wolfram PUBLIC CURL::libcurl cjson OpenSSL::Crypto)

option(WOLFRAM_BUILD_EXAMPLES "Build example programs" ON)
option(WOLFRAM_BUILD_TESTS "Build unit tests" ON)

if(WOLFRAM_BUILD_EXAMPLES)
    add_executable(describe_repo examples/describe_repo.c)
    target_link_libraries(describe_repo PRIVATE wolfram)
endif()

if(WOLFRAM_BUILD_TESTS)
    enable_testing()

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
endif()
