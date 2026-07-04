/**
 * test_identity.c — the one bit of identity.c that's actually
 * implemented so far: DID method sniffing.
 */

#include "wolfram/identity.h"
#include "test.h"

int main(void) {
    WF_CHECK(wf_did_method_of("did:plc:ofrbh253gwicbkc5nktqepol") == WF_DID_METHOD_PLC);
    WF_CHECK(wf_did_method_of("did:web:example.com") == WF_DID_METHOD_WEB);
    WF_CHECK(wf_did_method_of("not-a-did") == WF_DID_METHOD_UNKNOWN);
    WF_CHECK(wf_did_method_of(NULL) == WF_DID_METHOD_UNKNOWN);

    WF_TEST_SUMMARY();
}
