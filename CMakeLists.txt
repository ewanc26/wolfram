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
    src/agent/notification.c
    src/agent/agent_ext.c
    src/agent/feed_typed.c
    src/agent/thread_typed.c
    src/agent/graph_typed.c
    src/agent/list_typed.c
    src/agent/moderation_typed.c
    src/agent/feedgen_typed.c
    src/agent/chat_typed.c
    src/agent/moderation_actions.c
    src/agent/lexcall.c
src/agent/push_endpoint.c
    src/agent/create_record.c
    src/agent/moderation.c
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

    add_test(NAME moderation_demo COMMAND moderation_demo)
endif()
