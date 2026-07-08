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

# cJSON v1.7.18 and libcbor v0.13.0 declare an old cmake_minimum_required
# (<3.5) which CMake 4.x rejects. Allow them to configure anyway.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

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
    # Label subscriptions
    src/label/label.c
    # Syntax validation
    src/syntax/syntax.c
    # Lexicon validation
    src/validate/validate.c
    # High-level agent API
    src/agent/agent.c
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
    src/agent/list_typed.c
    src/agent/moderation_typed.c
    src/agent/feedgen_typed.c
    src/agent/chat_typed.c
    src/agent/threadgate_postgate.c
    src/agent/moderation_actions.c
    src/agent/lexcall.c
    src/agent/pagination.c
src/agent/push_endpoint.c
    src/agent/create_record.c
    src/agent/moderation.c
    src/agent/moderation_apply.c
    src/agent/label_persist.c
    # Rich text / facet detection
    src/richtext/richtext.c
    src/richtext/detection.c
    # Firehose subscribeRepos subscription
    src/sync/sync_subscribe.c
    # Firehose commit verification
    src/sync/sync_verify.c
    # Generated lexicon endpoint wrappers
    src/atproto_lex.c
    # Server account management
    src/server/server.c
    # New utilities and embed support
    src/util.c
    src/blob.c
    src/embed.c
    # Ozone moderation / labeler helper
    src/ozone/ozone.c
    # Generic JSON round-trip / schema-subset validation
    src/json/json.c
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
    message(STATUS "libidn2 found (${IDN2_LIBRARIES}) — building IDNA/punycode handle resolution")
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

# Optional SQLite3-backed persistence module (wf_store). OFF by default so the
# default build keeps its small dependency set. When ON, the system SQLite3
# library is required (no network FetchContent is used).
option(WOLFRAM_BUILD_STORE "Build the optional SQLite3-backed persistence module" OFF)

# Optional at-rest encryption of persisted sessions. Requires WOLFRAM_BUILD_STORE
# and the system libsodium library. OFF by default. When ON, the session blob is
# encrypted with libsodium (crypto_secretbox_easy) using a key derived from a
# caller-supplied passphrase via libsodium's crypto_pwhash (Argon2id). No network
# FetchContent is used — libsodium must be installed on the system.
option(WOLFRAM_BUILD_STORE_CRYPTO "Encrypt persisted sessions at rest with libsodium" OFF)

# Optional mock PDS for offline HTTP integration tests. When ON, the system
# libmicrohttpd (GNU MHD) library is required (no network FetchContent). It
# builds test/mock_pds.c and the offline integration test that points the SDK's
# XRPC client at a real local HTTP server. OFF by default so the default test
# suite has no extra dependency.
option(WOLFRAM_BUILD_TEST_HTTPD "Build the optional libmicrohttpd-backed mock PDS and offline HTTP integration test" OFF)

# Optional libidn2-backed IDNA2008 / punycode handle resolution. When ON, the
# system libidn2 library (GNU, MIT) is required (no network FetchContent). It
# converts Unicode handles to ASCII punycode (xn--…) before the DNS/HTTPS
# lookup and decodes punycode responses back to Unicode for display. OFF by
# default so the default build stays dependency-free and passes handles through
# unchanged. Install via `brew install libidn2` / `apt install libidn2-dev`.
option(WOLFRAM_BUILD_IDN "Build with libidn2 IDNA/punycode handle resolution" OFF)

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
        message(FATAL_ERROR
            "WOLFRAM_BUILD_STORE=ON but SQLite3 was not found on this system. "
            "Install it (e.g. `brew install sqlite3` / `apt install libsqlite3-dev`) "
            "or build with WOLFRAM_BUILD_STORE=OFF.")
    endif()

    target_sources(wolfram PRIVATE src/store/sqlite_store.c)
    target_include_directories(wolfram PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(wolfram PRIVATE ${SQLite3_LIBRARIES})
    target_compile_definitions(wolfram PRIVATE WOLFRAM_BUILD_STORE)
    message(STATUS "SQLite3 found (${SQLite3_LIBRARIES}) — building optional wf_store persistence module")

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
            message(FATAL_ERROR
                "WOLFRAM_BUILD_STORE_CRYPTO=ON but libsodium was not found on this "
                "system. Install it (e.g. `brew install libsodium` / `apt install "
                "libsodium-dev`) or build with WOLFRAM_BUILD_STORE_CRYPTO=OFF.")
        endif()

        target_include_directories(wolfram PRIVATE ${sodium_INCLUDE_DIRS})
        target_link_libraries(wolfram PRIVATE ${sodium_LIBRARIES})
        target_compile_definitions(wolfram PRIVATE WOLFRAM_BUILD_STORE_CRYPTO)
        message(STATUS "libsodium found (${sodium_LIBRARIES}) — encrypting persisted sessions at rest")
    endif()
endif()

# wolfram command-line client — a thin end-to-end demonstration over the SDK.
# Always built (not gated on WOLFRAM_BUILD_EXAMPLES) so the CLI is part of the
# default build. The target is named wolfram_cli because the wolfram library
# already owns the `wolfram` target name; OUTPUT_NAME keeps the binary `wolfram`.
add_executable(wolfram_cli src/cli/main.c)
target_link_libraries(wolfram_cli PRIVATE wolfram)
target_include_directories(wolfram_cli PRIVATE ${cjson_SOURCE_DIR} ${cjson_BINARY_DIR})
set_target_properties(wolfram_cli PROPERTIES OUTPUT_NAME wolfram)

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
    target_compile_definitions(test_identity PRIVATE
        WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
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

    add_executable(test_crypto_interop test/test_crypto_interop.c)
    target_link_libraries(test_crypto_interop PRIVATE wolfram)
    target_include_directories(test_crypto_interop PRIVATE test ${cjson_SOURCE_DIR})
    target_compile_definitions(test_crypto_interop PRIVATE WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/crypto")
    if(SECP256K1_FOUND)
        target_compile_definitions(test_crypto_interop PRIVATE HAVE_LIBSECP256K1)
    endif()
    add_test(NAME crypto_interop COMMAND test_crypto_interop)

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

    add_executable(test_label_subscribe test/test_label_subscribe.c)
    target_link_libraries(test_label_subscribe PRIVATE wolfram)
    target_include_directories(test_label_subscribe PRIVATE test)
    add_test(NAME label_subscribe COMMAND test_label_subscribe)
    set_tests_properties(label_subscribe PROPERTIES TIMEOUT 30)

    add_executable(test_oauth test/test_oauth.c)
    target_link_libraries(test_oauth PRIVATE wolfram)
    target_include_directories(test_oauth PRIVATE test ${cjson_SOURCE_DIR})
    add_test(NAME oauth COMMAND test_oauth)

    add_executable(test_oauth_refresh test/test_oauth_refresh.c)
    target_link_libraries(test_oauth_refresh PRIVATE wolfram)
    target_include_directories(test_oauth_refresh PRIVATE test ${cjson_SOURCE_DIR})
    add_test(NAME oauth_refresh COMMAND test_oauth_refresh)

    add_executable(test_syntax test/test_syntax.c)
    target_link_libraries(test_syntax PRIVATE wolfram)
    target_include_directories(test_syntax PRIVATE test)
    add_test(NAME syntax COMMAND test_syntax)

    add_executable(test_syntax_interop test/test_syntax_interop.c)
    target_link_libraries(test_syntax_interop PRIVATE wolfram)
    target_include_directories(test_syntax_interop PRIVATE test)
    add_test(NAME syntax_interop COMMAND test_syntax_interop)

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

    add_executable(test_diff test/test_diff.c)
    target_link_libraries(test_diff PRIVATE wolfram)
    target_include_directories(test_diff PRIVATE test)
    add_test(NAME diff COMMAND test_diff)

    add_executable(test_validate test/test_validate.c)
    target_link_libraries(test_validate PRIVATE wolfram)
    target_include_directories(test_validate PRIVATE test)
    add_test(NAME validate COMMAND test_validate)

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
    target_compile_definitions(test_validate_corpus PRIVATE
        WF_ATPROTO_LEXICONS="/Volumes/Storage/Developer/Local/atproto/lexicons")
    add_test(NAME validate_corpus COMMAND test_validate_corpus)

    add_executable(test_embed test/test_embed.c)
    target_link_libraries(test_embed PRIVATE wolfram)
    target_include_directories(test_embed PRIVATE test ${cjson_SOURCE_DIR})
    add_test(NAME embed COMMAND test_embed)

    add_executable(test_agent_preferences test/test_agent_preferences.c)
    target_link_libraries(test_agent_preferences PRIVATE wolfram)
    target_include_directories(test_agent_preferences PRIVATE test ${cjson_SOURCE_DIR})
    add_test(NAME agent_preferences COMMAND test_agent_preferences)

    add_executable(test_agent_notifications test/test_agent_notifications.c)
    target_link_libraries(test_agent_notifications PRIVATE wolfram)
    target_include_directories(test_agent_notifications PRIVATE test ${cjson_SOURCE_DIR})
    target_compile_definitions(test_agent_notifications PRIVATE
        WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    add_test(NAME agent_notifications COMMAND test_agent_notifications)

    add_executable(test_feed_typed test/test_feed_typed.c)
    target_link_libraries(test_feed_typed PRIVATE wolfram)
    target_include_directories(test_feed_typed PRIVATE test ${cjson_SOURCE_DIR})
    target_compile_definitions(test_feed_typed PRIVATE
        WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    add_test(NAME feed_typed COMMAND test_feed_typed)

    add_executable(test_thread_typed test/test_thread_typed.c)
    target_link_libraries(test_thread_typed PRIVATE wolfram)
    target_include_directories(test_thread_typed PRIVATE test ${cjson_SOURCE_DIR})
    target_compile_definitions(test_thread_typed PRIVATE
        WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    add_test(NAME thread_typed COMMAND test_thread_typed)

    add_executable(test_graph_typed test/test_graph_typed.c)
    target_link_libraries(test_graph_typed PRIVATE wolfram)
    target_include_directories(test_graph_typed PRIVATE test ${cjson_SOURCE_DIR})
    target_compile_definitions(test_graph_typed PRIVATE
        WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    add_test(NAME graph_typed COMMAND test_graph_typed)

    add_executable(test_list_typed test/test_list_typed.c)
    target_link_libraries(test_list_typed PRIVATE wolfram)
    target_include_directories(test_list_typed PRIVATE test ${cjson_SOURCE_DIR})
    target_compile_definitions(test_list_typed PRIVATE
        WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    add_test(NAME list_typed COMMAND test_list_typed)

    add_executable(test_moderation_typed test/test_moderation_typed.c)
    target_link_libraries(test_moderation_typed PRIVATE wolfram)
    target_include_directories(test_moderation_typed PRIVATE test ${cjson_SOURCE_DIR})
    target_compile_definitions(test_moderation_typed PRIVATE
        WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    add_test(NAME moderation_typed COMMAND test_moderation_typed)

    add_executable(test_feedgen_typed test/test_feedgen_typed.c)
    target_link_libraries(test_feedgen_typed PRIVATE wolfram)
    target_include_directories(test_feedgen_typed PRIVATE test ${cjson_SOURCE_DIR})
    target_compile_definitions(test_feedgen_typed PRIVATE
        WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    add_test(NAME feedgen_typed COMMAND test_feedgen_typed)

    add_executable(test_chat_typed test/test_chat_typed.c)
    target_link_libraries(test_chat_typed PRIVATE wolfram)
    target_include_directories(test_chat_typed PRIVATE test ${cjson_SOURCE_DIR})
    target_compile_definitions(test_chat_typed PRIVATE
        WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    add_test(NAME chat_typed COMMAND test_chat_typed)

    add_executable(examples_live test/test_examples_live.c)
    target_link_libraries(examples_live PRIVATE wolfram)
    target_include_directories(examples_live PRIVATE test ${cjson_SOURCE_DIR})
    add_test(NAME examples_live COMMAND examples_live)

    add_executable(test_moderation_actions test/test_moderation_actions.c)
    target_link_libraries(test_moderation_actions PRIVATE wolfram)
    target_include_directories(test_moderation_actions PRIVATE test ${cjson_SOURCE_DIR})
    target_compile_definitions(test_moderation_actions PRIVATE
        WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    add_test(NAME moderation_actions COMMAND test_moderation_actions)

    add_executable(test_lexcall test/test_lexcall.c)
    target_link_libraries(test_lexcall PRIVATE wolfram)
    target_include_directories(test_lexcall PRIVATE test ${cjson_SOURCE_DIR})
    target_compile_definitions(test_lexcall PRIVATE
        WF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
    add_test(NAME lexcall COMMAND test_lexcall)

    add_executable(test_moderation test/test_moderation.c)
    target_link_libraries(test_moderation PRIVATE wolfram)
    target_include_directories(test_moderation PRIVATE test)
    add_test(NAME moderation COMMAND test_moderation)

    add_executable(test_agent_moderation test/test_agent_moderation.c)
    target_link_libraries(test_agent_moderation PRIVATE wolfram)
    target_include_directories(test_agent_moderation PRIVATE test ${cjson_SOURCE_DIR})
    add_test(NAME agent_moderation COMMAND test_agent_moderation)

    add_test(NAME moderation_demo COMMAND moderation_demo)

    add_executable(test_agent_pagination test/test_agent_pagination.c)
    target_link_libraries(test_agent_pagination PRIVATE wolfram)
    target_include_directories(test_agent_pagination PRIVATE test ${cjson_SOURCE_DIR})
    add_test(NAME agent_pagination COMMAND test_agent_pagination)

    add_executable(test_json_roundtrip test/test_json_roundtrip.c)
    target_link_libraries(test_json_roundtrip PRIVATE wolfram)
    target_include_directories(test_json_roundtrip PRIVATE test ${cjson_SOURCE_DIR})
    add_test(NAME json_roundtrip COMMAND test_json_roundtrip)

    if(WOLFRAM_BUILD_STORE)
        if(WOLFRAM_BUILD_STORE_CRYPTO)
            add_executable(test_store_crypto test/test_store_crypto.c)
            target_link_libraries(test_store_crypto PRIVATE wolfram)
            target_include_directories(test_store_crypto PRIVATE test)
            target_compile_definitions(test_store_crypto PRIVATE
                WOLFRAM_BUILD_STORE WOLFRAM_BUILD_STORE_CRYPTO)
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
        target_include_directories(test_mirror_persist PRIVATE test ${cjson_SOURCE_DIR})
        target_compile_definitions(test_mirror_persist PRIVATE WOLFRAM_BUILD_STORE)
        add_test(NAME mirror_persist COMMAND test_mirror_persist)

        add_executable(test_label_persist test/test_label_persist.c)
        target_link_libraries(test_label_persist PRIVATE wolfram)
        target_include_directories(test_label_persist PRIVATE test ${cjson_SOURCE_DIR})
        target_compile_definitions(test_label_persist PRIVATE WOLFRAM_BUILD_STORE)
        add_test(NAME label_persist COMMAND test_label_persist)
    endif()

    if(WOLFRAM_BUILD_TEST_HTTPD)
        find_path(MICROHTTPD_INCLUDE_DIR NAMES microhttpd.h REQUIRED)
        find_library(MICROHTTPD_LIBRARY NAMES microhttpd REQUIRED)

        add_executable(test_mock_pds test/test_mock_pds.c test/mock_pds.c)
        target_link_libraries(test_mock_pds PRIVATE wolfram ${MICROHTTPD_LIBRARY})
        target_include_directories(test_mock_pds PRIVATE
            test
            ${MICROHTTPD_INCLUDE_DIR}
            ${cjson_SOURCE_DIR}
        )
        add_test(NAME mock_pds COMMAND test_mock_pds)
    endif()
endif()
