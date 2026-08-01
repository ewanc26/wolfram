# Release Notes

## v0.3.1 — C++ Migration Complete

### C++ API

- **RAII Handle System** (`cpp/wolfram-cpp/wolfram/unique_handle.hpp`): Header-only `unique_handle<T, Deleter>` template that wraps C resources with automatic cleanup. Eliminates manual `*_free` calls in C++ code.
- **Generated Owners** (`cpp/wolfram-cpp/tools/gen_owners.cpp`): C++ replacement for the Python `gen_owners.py` script. Automatically scans C public headers and emits `unique_handle` typedefs.
- **Safe Handles** (`dotnet/Wolfram.Interop/tools/gen_safehandles.cpp`): C# interop tool rewritten in C++ for consistent cross-language code generation.
- **Unspecced Wrappers** (`tools/wf_gen_unspecced_wrappers.cpp`): C++ replacement for the Python lexicon wrapper generator.

### Migration Summary

| Component | Before | After |
|---|---|---|
| `gen_owners` | Python script | C++ executable |
| `gen_safehandles` | Python script | C++ executable |
| `wf_gen_unspecced_wrappers` | Python script | C++ executable |
| `test_lexgen` | Python unittest | C test executable |
| RAII ownership | Manual `free()` | Automatic (RAII) |
| C++ policy | Wrappers only | Full C++ support with `extern "C"` boundary |

### Build Changes

- Root `CMakeLists.txt` removed; build is now driven from `cpp/CMakeLists.txt`
- C++ standard set to C++17 for all tools
- `WOLFRAM_BUILD_CPP` option remains for the `wolfram-cpp` library
- No Python runtime dependency required for any build target

### Documentation

- New `docs/cpp/unique_handle.md` documents the RAII handle system
- `AGENTS.md` updated with C++ policy and integration guidelines
- `README.md` updated to reflect C++ capabilities