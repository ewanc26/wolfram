# wolfram-cpp: header-only C++ RAII wrapper around libwolfram. The C23 core
# stays authoritative; this layer only wraps it.
add_library(wolfram-cpp INTERFACE)
# wolfram's public headers include <cJSON.h>, but its cJSON include dirs are
# PRIVATE, so consumers must add them explicitly (as the test targets do).
target_include_directories(
  wolfram-cpp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/wolfram-cpp
                        ${cjson_SOURCE_DIR} ${cjson_BINARY_DIR})
target_link_libraries(wolfram-cpp INTERFACE wolfram)

# --- Build the gen_owners tool (C++ replacement for Python script)
# ---------------
add_executable(gen_owners_tool wolfram-cpp/tools/gen_owners.cpp)
target_include_directories(gen_owners_tool PRIVATE ${CMAKE_SOURCE_DIR}/include)
set_target_properties(gen_owners_tool PROPERTIES CXX_STANDARD 23)

# --- Auto-generate RAII handle typedefs from C headers
# -------------------------
# Scans include/wolfram/**/*.h for `void wf_<T>_free(wf_<T> *...)` and emits
# unique_handle<wf_<T>, wf_<T>_free> typedefs. Regenerates whenever any C public
# header changes, so new types are picked up automatically. First run: use
# existing generated_owners.hpp if it exists
set(GEN_OWNERS_OUTPUT
    ${CMAKE_CURRENT_SOURCE_DIR}/wolfram-cpp/wolfram/generated_owners.hpp)

file(
  GLOB
  WOLFRAM_PUBLIC_HEADERS
  CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/include/wolfram/*.h"
  "${CMAKE_SOURCE_DIR}/include/wolfram/**/*.h")

# Create directory for generated file if needed
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/wolfram-cpp/wolfram)

add_custom_command(
  OUTPUT ${GEN_OWNERS_OUTPUT}
  COMMAND gen_owners_tool ${CMAKE_SOURCE_DIR}/include/wolfram
          ${GEN_OWNERS_OUTPUT}
  DEPENDS ${WOLFRAM_PUBLIC_HEADERS} gen_owners_tool
  COMMENT "Regenerating wolfram-cpp owned handles from C headers"
  VERBATIM)

add_custom_target(wolfram_gen_owners ALL DEPENDS ${GEN_OWNERS_OUTPUT})
add_dependencies(wolfram-cpp wolfram_gen_owners)

if(WOLFRAM_BUILD_TESTS)
  add_executable(wolfram-cpp-smoke wolfram-cpp/test/smoke.cpp)
  set_target_properties(wolfram-cpp-smoke PROPERTIES CXX_STANDARD 23)
  target_link_libraries(wolfram-cpp-smoke PRIVATE wolfram-cpp)
  add_test(NAME wolfram-cpp-smoke COMMAND wolfram-cpp-smoke)
endif()
