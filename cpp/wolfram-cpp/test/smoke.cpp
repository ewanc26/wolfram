// Offline smoke test for the wolfram-cpp RAII wrapper. Links libwolfram and
// exercises the wrapper mechanics (status -> error_code, RAII handle lifetime,
// JSON string ownership) and the new draft/temp/bookmark typed parsers and
// agent wrappers, without any network access.

#include <cassert>
#include <iostream>
#include <string>

#include <wolfram/wolfram.hpp>
#include <wolfram/json.h>

int main() {
    using namespace wolfram;

    // 1) wf_status maps into std::error_code; WF_OK is success.
    std::error_code bad = WF_ERR_INVALID_ARG;
    assert(bad);
    assert(std::string(bad.category().name()) == "wolfram");
    assert(!std::error_code(WF_OK));

    // 2) RAII handle over an xrpc client (no network needed to construct).
    wf_xrpc_client_handle client(wf_xrpc_client_new("https://bsky.social"));
    assert(client);
    assert(std::string(wf_xrpc_get_base_url(client.get())) == "https://bsky.social");

    // 3) Offline JSON round-trip. canonicalize preserves key order and emits
    //    compact JSON; the owned char* is released by the cstring RAII wrapper.
    const char *in = "{\"b\":1,\"a\":2}";
    char *raw = nullptr;
    require(wf_json_canonicalize(in, std::char_traits<char>::length(in), &raw));
    cstring json(raw);
    std::cout << "canonical: " << json.get() << "\n";
    assert(std::string(json.get()) == "{\"b\":1,\"a\":2}");

    // 4) Regenerated RAII owner handles for the new typed result types exist
    //    and bind to their _free functions. Allocate a result on the heap,
    //    parse into it, and hand ownership to the generated unique_handle so it
    //    frees both the struct and its owned `id` string on scope exit.
    {
        const char *dj = "{\"id\":\"at://did/app.bsky.draft.getDrafts/abc\"}";
        wf_draft_createDraft_result *dr = new wf_draft_createDraft_result();
        assert(wf_draft_createDraft_parse(
                   dj, std::char_traits<char>::length(dj), dr) == WF_OK);
        assert(std::string(dr->id) == "at://did/app.bsky.draft.getDrafts/abc");
        wf_draft_createDraft_result_handle drh(dr); // takes ownership
        (void)drh;
        // scope exit: drh -> wf_draft_createDraft_result_free(dr) (frees dr->id)
    }

    // 5) Success path for every new typed result: assert the parsed fields.
    //    draft createDraft returns {id}; update/delete expose `ok`; temp
    //    addReservedHandle echoes an optional `handle` and sets `ok`=1.
    const char *draft_json = "{\"id\":\"at://did/app.bsky.draft.getDrafts/abc\"}";
    size_t draft_len = std::char_traits<char>::length(draft_json);

    wf_draft_createDraft_result dr;
    assert(wf_draft_createDraft_parse(draft_json, draft_len, &dr) == WF_OK);
    assert(std::string(dr.id) == "at://did/app.bsky.draft.getDrafts/abc");
    wf_draft_createDraft_result_free(&dr);

    wf_draft_updateDraft_result dur;
    assert(wf_draft_updateDraft_parse(draft_json, draft_len, &dur) == WF_OK);
    assert(dur.ok == true);
    wf_draft_updateDraft_result_free(&dur);

    wf_draft_deleteDraft_result ddr;
    assert(wf_draft_deleteDraft_parse("{}", 2, &ddr) == WF_OK);
    assert(ddr.ok == true);
    wf_draft_deleteDraft_result_free(&ddr);

    // temp addReservedHandle: handle echo when present, ok=1 even for {}.
    const char *tar_json = "{\"handle\":\"reserved.bsky.app\"}";
    wf_temp_add_reserved_handle_result tar;
    assert(wf_temp_add_reserved_handle_parse(
               tar_json, std::char_traits<char>::length(tar_json), &tar) ==
           WF_OK);
    assert(tar.ok == 1);
    assert(tar.handle && std::string(tar.handle) == "reserved.bsky.app");
    wf_temp_add_reserved_handle_result_free(&tar);

    wf_temp_add_reserved_handle_result tar2;
    assert(wf_temp_add_reserved_handle_parse("{}", 2, &tar2) == WF_OK);
    assert(tar2.ok == 1);
    assert(tar2.handle == nullptr);
    wf_temp_add_reserved_handle_result_free(&tar2);

    wf_temp_request_phone_verification_result tpr;
    assert(wf_temp_request_phone_verification_parse("{}", 2, &tpr) == WF_OK);
    assert(tpr.ok == 1);
    wf_temp_request_phone_verification_result_free(&tpr);

    wf_temp_revoke_account_credentials_result trc;
    assert(wf_temp_revoke_account_credentials_parse("{}", 2, &trc) == WF_OK);
    assert(trc.ok == 1);
    wf_temp_revoke_account_credentials_result_free(&trc);

    wf_bookmark_create_result bcr;
    assert(wf_bookmark_create_parse("{}", 2, &bcr) == WF_OK);
    assert(bcr.ok == true);
    wf_bookmark_create_result_free(&bcr);

    wf_bookmark_delete_result bdr;
    assert(wf_bookmark_delete_parse("{}", 2, &bdr) == WF_OK);
    assert(bdr.ok == true);
    wf_bookmark_delete_result_free(&bdr);

    // 6) Error path: malformed JSON yields WF_ERR_PARSE and a reset struct.
    wf_draft_createDraft_result err;
    assert(wf_draft_createDraft_parse("not json", 8, &err) == WF_ERR_PARSE);
    assert(err.id == nullptr);
    wf_draft_createDraft_result_free(&err);

    wf_temp_add_reserved_handle_result terr;
    assert(wf_temp_add_reserved_handle_parse("not json", 8, &terr) ==
           WF_ERR_PARSE);
    assert(terr.ok == 0 && terr.handle == nullptr);
    wf_temp_add_reserved_handle_result_free(&terr);

    // 7) The new agent typed wrappers validate arguments up front: a NULL agent
    //    or empty inputs return WF_ERR_INVALID_ARG (offline, no network).
    wf_agent *agent = nullptr;

    wf_draft_createDraft_result r1;
    assert(wf_agent_draft_createDraft_typed(agent, "", &r1) ==
           WF_ERR_INVALID_ARG);
    wf_draft_updateDraft_result r2;
    assert(wf_agent_draft_updateDraft_typed(agent, "", "", &r2) ==
           WF_ERR_INVALID_ARG);
    wf_draft_deleteDraft_result r3;
    assert(wf_agent_draft_deleteDraft_typed(agent, "", &r3) ==
           WF_ERR_INVALID_ARG);

    wf_temp_add_reserved_handle_result r4;
    assert(wf_agent_temp_add_reserved_handle_typed(agent, "", &r4) ==
           WF_ERR_INVALID_ARG);
    wf_temp_request_phone_verification_result r5;
    assert(wf_agent_temp_request_phone_verification_typed(agent, "", &r5) ==
           WF_ERR_INVALID_ARG);
    wf_temp_revoke_account_credentials_result r6;
    assert(wf_agent_temp_revoke_account_credentials_typed(agent, "", &r6) ==
           WF_ERR_INVALID_ARG);

    wf_bookmark_create_result r7;
    assert(wf_agent_bookmark_create_typed(agent, "", &r7) ==
           WF_ERR_INVALID_ARG);
    wf_bookmark_delete_result r8;
    assert(wf_agent_bookmark_delete_typed(agent, "", &r8) ==
           WF_ERR_INVALID_ARG);

    std::cout << "wolfram-cpp smoke OK\n";
    return 0;
}
