# RAII Handle System

The `unique_handle<T, Deleter>` template in `wolfram-cpp/wolfram/unique_handle.hpp` provides a lightweight, type‑safe wrapper around C resources. It behaves like `std::unique_ptr` but is header‑only and has no external dependencies.

## Usage

```cpp
#include "wolfram-cpp/wolfram/wolfram.hpp"

// Acquire a C resource
wf_actor *actor = wf_actor_new();

// Wrap it in a unique_handle
using actor_handle = unique_handle<wf_actor, wf_actor_free>;
actor_handle a(actor);

// Use the resource
wf_status st = wf_actor_get_did(a.get());

// No manual free needed – a is destroyed automatically
```

## Advantages

- **No manual `free`** – the deleter is called automatically when the handle goes out of scope.
- **Exception safety** – the deleter is invoked even if an exception propagates.
- **Zero runtime overhead** – the wrapper is a thin struct with a single pointer.
- **Explicit ownership** – the type clearly indicates that the caller owns the resource.

## Supported Types

The wrapper is instantiated for every public C type that has a matching `*_free` function. The generated header `generated_owners.hpp` contains the following typedefs:

```cpp
using wf_actor_handle = unique_handle<wf_actor, wf_actor_free>;
using wf_repo_handle  = unique_handle<wf_repo, wf_repo_free>;
// …
```

These can be used directly in C++ code.

---

**Note**: The C++ layer is *header‑only*; it does not add any new runtime dependencies beyond the existing C library.
