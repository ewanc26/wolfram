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

if(WOLFRAM_BUILD_TESTS)
    add_executable(wolfram-cpp-smoke wolfram-cpp/test/smoke.cpp)
    set_target_properties(wolfram-cpp-smoke PROPERTIES CXX_STANDARD 17)
    target_link_libraries(wolfram-cpp-smoke PRIVATE wolfram-cpp)
    add_test(NAME wolfram-cpp-smoke COMMAND wolfram-cpp-smoke)
endif()
