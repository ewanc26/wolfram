cmake_minimum_required(VERSION 3.20)
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
)

target_link_libraries(wolfram PUBLIC CURL::libcurl)

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
endif()
