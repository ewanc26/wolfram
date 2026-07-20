# Wolfram Library

cmake_minimum_required(VERSION 3.10)
project(wolfram C)

# Ensure C++ is not enabled by default project-wide
set(CMAKE_CXX_LANGUAGE_STANDARD "")
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Global Settings ---
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# --- Global Include Directories ---
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- Library Targets ---
add_library(wolfram STATIC
    src/util.c
    src/repo_store.c
    src/cid.c
    src/repo_typed.c
    src/sync_typed.c
    src/ws_client.c
    src/agent.c
    src/agent_typed.c
    src/bsky_agent.c
    src/chat_typed.c
    src/ozone_typed.c
    src/moderation.c
    src/label.c
    src/labeler_typed.c
    src/label_subscribe_typed.c
    src/unspecced.c
    src/unspecced_typed.c
    src/oauth/oauth.c
    src/oauth/verify.c
    src/oauth/token.c
    src/oauth/dpop.c
    src/oauth/jwk.c
    src/oauth/par.c
    src/oauth/client.c
    src/oauth/state.c
    src/oauth/nonce.c
    src/jetstream.c
    src/validate.c
    src/json.c
    src/syntax.c
    src/richtext.c
    src/feedgen_server.c # Keep feedgen_server as it might not be BlobStore specific
    # Removed blob_store.c
    # Removed blob_store_server.c
)

# --- Dependencies ---
# Add any external libraries Wolfram depends on here
# target_link_libraries(wolfram PRIVATE <dependency>)

# --- Include Directories ---
target_include_directories(wolfram PUBLIC
    include
    ${CMAKE_CURRENT_BINARY_DIR}
)

# --- Optional Modules ---
# Conditional compilation for optional modules.
# If the blob store functionality was tied to a specific server build
# gate (e.g., WOLFRAM_BUILD_SERVER), and that gate is now removed or
# does not affect blob store code removal, this section might need
# adjustments. Here, we assume the blob store server parts were gated
# and are now removed, so no specific gate is needed for their absence.

# build the C++ bindings if enabled
if(WOLFRAM_BUILD_CPP)
    add_subdirectory(cpp)
endif()

# build the XRPC server components if enabled
if(WOLFRAM_BUILD_SERVER)
    # If blob store server was part of WOLFRAM_BUILD_SERVER, and is now being removed,
    # no change is needed here unless other server components depend on it.
    # Assuming it's independent, removing blob store removes its dependency.
    message(STATUS "WOLFRAM_BUILD_SERVER enabled. XRPC server components will be built.")
    # If blob store server was also part of this, it's implicitly removed.
else()
    message(STATUS "WOLFRAM_BUILD_SERVER disabled. XRPC server components will NOT be built.")
endif()

# build the persistence store if enabled
if(WOLFRAM_BUILD_STORE)
    message(STATUS "WOLFRAM_BUILD_STORE enabled. Persistence store will be built.")
    # Dependencies for store (e.g., SQLite) would be linked here.
    # target_link_libraries(wolfram PRIVATE sqlite3)
else()
    message(STATUS "WOLFRAM_BUILD_STORE disabled. Persistence store will NOT be built.")
endif()

# build other optional modules
# ... (other build gates and their corresponding source files/dependencies)

# --- Install ---
# install(TARGETS wolfram DESTINATION lib)
# install(DIRECTORY include/wolfram/ DESTINATION include/wolfram)

