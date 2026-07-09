// Offline smoke test for the wolfram-cpp RAII wrapper. Links libwolfram and
// exercises the wrapper mechanics (status -> error_code, RAII handle lifetime,
// JSON string ownership) without any network access.

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

    // 4) Destructor paths run implicitly on scope exit (client + json freed).
    std::cout << "wolfram-cpp smoke OK\n";
    return 0;
}
