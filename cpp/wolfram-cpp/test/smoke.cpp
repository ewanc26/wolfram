// Offline smoke test for the wolfram-cpp RAII wrapper. Links libwolfram and
// exercises the wrapper mechanics (status -> error_code, RAII handle lifetime,
// JSON string ownership) and the draft/temp/bookmark typed parsers and agent
// wrappers, without any network access.
//
// Must pass in Release builds, where -DNDEBUG strips <cassert>'s assert(), so
// checks use a CHECK macro that always evaluates its expression instead of
// assert().

#include <cstdlib>
#include <iostream>
#include <string>

#include <wolfram/wolfram.hpp>
#include <wolfram/json.h>

namespace {

int g_failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << "FAILED: " << #expr << " at smoke.cpp:" << __LINE__   \
                      << "\n";                                                 \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

} // namespace

int main() {
    using namespace wolfram;

    // 1) wf_status maps into std::error_code; WF_OK is success.
    std::error_code bad = WF_ERR_INVALID_ARG;
    CHECK(bad);
    CHECK(std::string(bad.category().name()) == "wolfram");
    CHECK(!std::error_code(WF_OK));

    // 2) RAII handle over an xrpc client (no network needed to construct).
    wf_xrpc_client_handle client(wf_xrpc_client_new("https://bsky.social"));
    CHECK(client);
    char *base_url = wf_xrpc_get_base_url(client.get());
    CHECK(base_url && std::string(base_url) == "https://bsky.social");
    std::free(base_url); /* get_base_url returns a caller-owned copy */

    // 3) Offline JSON round-trip. canonicalize preserves key order and emits
    //    compact JSON; the owned char* is released by the cstring RAII wrapper.
    const char *in = "{\"b\":1,\"a\":2}";
    char *raw = nullptr;
    require(wf_json_canonicalize(in, std::char_traits<char>::length(in), &raw));
    cstring json(raw);
    std::cout << "canonical: " << json.get() << "\n";
    CHECK(std::string(json.get()) == "{\"b\":1,\"a\":2}");

    // 4) The generated RAII owner handles bind to their _free functions. The
    //    typed result structs are caller-allocated (the C _free routines own
    //    the members, not the struct itself), so the handle wraps a
    //    stack-allocated result: on scope exit it releases the owned `id`
    //    string and zeroes the struct.
    {
        const char *dj = "{\"id\":\"at://did/app.bsky.draft.getDrafts/abc\"}";
        wf_draft_createDraft_result dr = {};
        CHECK(wf_draft_createDraft_parse(dj, std::char_traits<char>::length(dj),
                                         &dr) == WF_OK);
        CHECK(std::string(dr.id) == "at://did/app.bsky.draft.getDrafts/abc");
        wf_draft_createDraft_result_handle drh(&dr); // takes ownership
        CHECK(drh.get() == &dr);
    }

    // 5) Success path for every new typed result: check the parsed fields.
    //    draft createDraft returns {id}; update/delete expose `ok`; temp
    //    addReservedHandle echoes an optional `handle` and sets `ok`=1.
    const char *draft_json =
        "{\"id\":\"at://did/app.bsky.draft.getDrafts/abc\"}";
    size_t draft_len = std::char_traits<char>::length(draft_json);

    wf_draft_createDraft_result dr = {};
    CHECK(wf_draft_createDraft_parse(draft_json, draft_len, &dr) == WF_OK);
    CHECK(std::string(dr.id) == "at://did/app.bsky.draft.getDrafts/abc");
    wf_draft_createDraft_result_free(&dr);

    wf_draft_updateDraft_result dur = {};
    CHECK(wf_draft_updateDraft_parse(draft_json, draft_len, &dur) == WF_OK);
    CHECK(dur.ok == true);
    wf_draft_updateDraft_result_free(&dur);

    wf_draft_deleteDraft_result ddr = {};
    CHECK(wf_draft_deleteDraft_parse("{}", 2, &ddr) == WF_OK);
    CHECK(ddr.ok == true);
    wf_draft_deleteDraft_result_free(&ddr);

    // temp addReservedHandle: handle echo when present, ok=1 even for {}.
    const char *tar_json = "{\"handle\":\"reserved.bsky.app\"}";
    wf_temp_add_reserved_handle_result tar = {};
    CHECK(wf_temp_add_reserved_handle_parse(
              tar_json, std::char_traits<char>::length(tar_json), &tar) ==
          WF_OK);
    CHECK(tar.ok == 1);
    CHECK(tar.handle && std::string(tar.handle) == "reserved.bsky.app");
    wf_temp_add_reserved_handle_result_free(&tar);

    wf_temp_add_reserved_handle_result tar2 = {};
    CHECK(wf_temp_add_reserved_handle_parse("{}", 2, &tar2) == WF_OK);
    CHECK(tar2.ok == 1);
    CHECK(tar2.handle == nullptr);
    wf_temp_add_reserved_handle_result_free(&tar2);

    wf_temp_request_phone_verification_result tpr = {};
    CHECK(wf_temp_request_phone_verification_parse("{}", 2, &tpr) == WF_OK);
    CHECK(tpr.ok == 1);
    wf_temp_request_phone_verification_result_free(&tpr);

    wf_temp_revoke_account_credentials_result trc = {};
    CHECK(wf_temp_revoke_account_credentials_parse("{}", 2, &trc) == WF_OK);
    CHECK(trc.ok == 1);
    wf_temp_revoke_account_credentials_result_free(&trc);

    wf_bookmark_create_result bcr = {};
    CHECK(wf_bookmark_create_parse("{}", 2, &bcr) == WF_OK);
    CHECK(bcr.ok == true);
    wf_bookmark_create_result_free(&bcr);

    wf_bookmark_delete_result bdr = {};
    CHECK(wf_bookmark_delete_parse("{}", 2, &bdr) == WF_OK);
    CHECK(bdr.ok == true);
    wf_bookmark_delete_result_free(&bdr);

    // 6) Error path: malformed JSON yields WF_ERR_PARSE and a reset struct.
    wf_draft_createDraft_result err = {};
    CHECK(wf_draft_createDraft_parse("not json", 8, &err) == WF_ERR_PARSE);
    CHECK(err.id == nullptr);
    wf_draft_createDraft_result_free(&err);

    wf_temp_add_reserved_handle_result terr = {};
    CHECK(wf_temp_add_reserved_handle_parse("not json", 8, &terr) ==
          WF_ERR_PARSE);
    CHECK(terr.ok == 0 && terr.handle == nullptr);
    wf_temp_add_reserved_handle_result_free(&terr);

    // 7) The new agent typed wrappers validate arguments up front: a NULL agent
    //    or empty inputs return WF_ERR_INVALID_ARG (offline, no network).
    wf_agent *agent = nullptr;

    wf_draft_createDraft_result r1 = {};
    CHECK(wf_agent_draft_createDraft_typed(agent, "", &r1) ==
          WF_ERR_INVALID_ARG);
    wf_draft_updateDraft_result r2 = {};
    CHECK(wf_agent_draft_updateDraft_typed(agent, "", "", &r2) ==
          WF_ERR_INVALID_ARG);
    wf_draft_deleteDraft_result r3 = {};
    CHECK(wf_agent_draft_deleteDraft_typed(agent, "", &r3) ==
          WF_ERR_INVALID_ARG);

    wf_temp_add_reserved_handle_result r4 = {};
    CHECK(wf_agent_temp_add_reserved_handle_typed(agent, "", &r4) ==
          WF_ERR_INVALID_ARG);
    wf_temp_request_phone_verification_result r5 = {};
    CHECK(wf_agent_temp_request_phone_verification_typed(agent, "", &r5) ==
          WF_ERR_INVALID_ARG);
    wf_temp_revoke_account_credentials_result r6 = {};
    CHECK(wf_agent_temp_revoke_account_credentials_typed(agent, "", &r6) ==
          WF_ERR_INVALID_ARG);

    wf_bookmark_create_result r7 = {};
    CHECK(wf_agent_bookmark_create_typed(agent, "", &r7) == WF_ERR_INVALID_ARG);
    wf_bookmark_delete_result r8 = {};
    CHECK(wf_agent_bookmark_delete_typed(agent, "", &r8) == WF_ERR_INVALID_ARG);

    std::cout << "wolfram-cpp smoke " << (g_failures ? "FAILED" : "OK") << "\n";
    return g_failures ? 1 : 0;
}
