#include <cassert>
#include "wolfram-cpp/wolfram/wolfram.hpp"

int main() {
    // Test RAII handle auto-free
    auto handle = wolfram::make_handle(wf_actor_new());
    assert(handle != nullptr);
    // No explicit free needed

    // Test generated owners
    using actor_handle = wolfram::unique_handle<wf_actor, wf_actor_free>;
    actor_handle actor(wf_actor_new());
    assert(actor.get() != nullptr);
    // Destructs automatically
    return 0;
}