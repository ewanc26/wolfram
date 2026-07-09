# wolfram-cpp: header-only C++ RAII wrapper around libwolfram.
# The C11 core stays authoritative; this layer only wraps it.
add_library(wolfram-cpp INTERFACE)
# wolfram's public headers include <cJSON.h>, but its cJSON include dirs are
# PRIVATE, so consumers must add them explicitly (as the test targets do).
target_include_directories(wolfram-cpp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/wolfram-cpp
    ${cjson_SOURCE_DIR}
    ${cjson_BINARY_DIR}
)
target_link_libraries(wolfram-cpp INTERFACE wolfram)

# --- Auto-generate RAII handle typedefs from C headers -------------------------
# Scans include/wolfram/**/*.h for `void wf_<T>_free(wf_<T> *...)` and emits
# unique_handle<wf_<T>, wf_<T>_free> typedefs. Regenerates whenever any C
# public header changes, so new types are picked up automatically.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(GEN_OWNERS_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/wolfram-cpp/tools/gen_owners.py)
set(GEN_OWNERS_OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/wolfram-cpp/wolfram/generated_owners.hpp)

file(GLOB WOLFRAM_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/include/wolfram/*.h"
    "${CMAKE_SOURCE_DIR}/include/wolfram/**/*.h"
)

add_custom_command(
    OUTPUT ${GEN_OWNERS_OUTPUT}
    COMMAND ${Python3_EXECUTABLE} ${GEN_OWNERS_SCRIPT}
            ${CMAKE_SOURCE_DIR}/include/wolfram
            ${GEN_OWNERS_OUTPUT}
    DEPENDS ${WOLFRAM_PUBLIC_HEADERS} ${GEN_OWNERS_SCRIPT}
    COMMENT "Regenerating wolfram-cpp owned handles from C headers"
    VERBATIM
)

add_custom_target(wolfram_gen_owners ALL DEPENDS ${GEN_OWNERS_OUTPUT})
add_dependencies(wolfram-cpp wolfram_gen_owners)

if(WOLFRAM_BUILD_TESTS)
    add_executable(wolfram-cpp-smoke wolfram-cpp/test/smoke.cpp)
    set_target_properties(wolfram-cpp-smoke PROPERTIES CXX_STANDARD 17)
    target_link_libraries(wolfram-cpp-smoke PRIVATE wolfram-cpp)
    add_test(NAME wolfram-cpp-smoke COMMAND wolfram-cpp-smoke)
endif()
