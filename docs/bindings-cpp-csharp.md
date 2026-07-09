# Companion language bindings (C++ and C#)

wolfram's core SDK **must remain pure C11**. The C11 core (`libwolfram`) is the
single source of truth: it owns transport, crypto, DAG-CBOR, DNS, and the
server. Companion languages are **consumer layers** that link `libwolfram` and
call into it — they must never re-implement signing, hashing, serialization,
transport, or server logic.

This document records the agreed conventions for the two C-adjacent bindings:
**C++** (`wolfram-cpp`) and **C#** (`Wolfram.Interop`).

## Hard constraints (apply to every binding)

1. **Wrappers only — never reimplement.** The C++ and C# layers are thin
   pass-through wrappers over `libwolfram`. They must **not** reimplement any
   wolfram logic: no signing/hashing (libsecp256k1/OpenSSL), no DAG-CBOR or
   CAR (libcbor), no transport/DNS (libcurl/c-ares), no server (libmicrohttpd).
   Every wrapper ultimately calls a `libwolfram` function; the binding layer
   only adds ownership (RAII/`SafeHandle`), error mapping, and idiomatic
   naming. If wolfram has an honest stub (`WF_ERR_INVALID_ARG` + `TODO`), the
   binding surfaces that as an error/exception — never a fabricated success.
2. **Ownership mirrored 1:1.** wolfram exposes a uniform contract: every owned
   output type `wf_T` has a matching `wf_T_free`, and JSON strings use
   `wf_*_json_free(char*)` (freed via `cJSON_free`, **not** `free`/`delete`).
   Each binding wraps these with its native ownership primitive.
3. **Status handling.** The `wf_status` enum (`_result.h`: `WF_OK = 0`,
   `WF_ERR_INVALID_ARG`, `WF_ERR_ALLOC`, `WF_ERR_NETWORK`, …) maps to the
   language's native error model; `WF_OK` is success.
4. **Encoding is UTF-8.** All `char*` strings are UTF-8. Marshalling must be
   explicit; do not rely on platform defaults (UTF-16 in .NET/JS).
5. **Optional modules are feature-gated.** `WOLFRAM_BUILD_SERVER`,
   `WOLFRAM_BUILD_STORE`, `WOLFRAM_BUILD_STORE_CRYPTO` change which symbols exist
   in `libwolfram`; bindings gate those surfaces accordingly.
6. **Public headers are `extern "C"`.** Both languages consume the existing
   `include/wolfram/*.h` directly (C++ includes them; C# parses them with
   ClangSharp). No hand-maintained signature duplication where a generator fits.

## C++ — `wolfram-cpp` (header-only RAII layer)

Implemented as `cpp/wolfram-cpp/` (build with `-DWOLFRAM_BUILD_CPP=ON`; the
`wolfram-cpp-smoke` test exercises it offline). C++ includes the C headers
directly, so there is **no binding generator** for the wrapper itself — just a
thin RAII layer:

- `wolfram/unique_handle.hpp` — `unique_handle<T, Free>` RAII owner mirroring
  wolfram's `wf_T` + `wf_T_free` contract (move-only, `get()`/`release()`/`reset()`).
- `wolfram/cstring.hpp` — RAII owner of a heap `char*`; default deleter is
  `std::free`, pass a `wf_*_json_free` for cJSON-backed strings.
- `wolfram/status.hpp` — `wf_status` → `std::error_code` via a custom
  `error_category`, plus `wolfram::require(wf_status)` that throws
  `std::system_error`. `wf_status` is `is_error_code_enum`-enabled so it
  constructs `std::error_code` directly.
- `tools/gen_owners.py` — scans `include/wolfram/*.h` for `void wf_<T>_free(...)`
  and emits `wolfram/generated_owners.hpp` with one `wf_<T>_handle` typedef per
  owned type (372 handles from 51 headers). Regenerate after changing headers.
- `wolfram/wolfram.hpp` — umbrella include.

### Core changes required for C++ consumption
The C public headers were made C++-clean as part of adding this wrapper:
- Unified the two status enums: `wf_status` (xrpc.h) is now the single canonical
  enum (full 27-code set, values preserved from the old `wf_error`); `wf_error`
  (in `_result.h`) is a `typedef` alias of `wf_status`. Previously the same
  enumerators existed in both enums with divergent values (e.g. `WF_ERR_RATE_LIMIT`
  was 8 vs 21), which broke any translation unit that included both.
- `_result.h` now wraps its declarations in `extern "C"` and no longer has a stray
  `type` token before `static inline` (a latent bug that also failed to compile
  in C++).
- `project(... LANGUAGES C CXX)` so the C++ target is buildable.

## C# — `Wolfram.Interop` (P/Invoke, AOT-capable)

*Not yet scaffolded.* Planned approach:

## C# — `Wolfram.Interop` (P/Invoke, AOT-capable)

- **Generation:** **ClangSharpPInvokeGenerator** parses `include/wolfram/*.h`
  into raw blittable P/Invoke bindings (response file:
  `-f include/wolfram/*.h -n Wolfram -o Wolfram.gen.cs`). This is the raw
  tier; a hand-written managed tier sits on top (ClangSharp's tier-3 pattern).
- **Interop style:** prefer **`LibraryImport`** (.NET 7+, source-generated,
  NativeAOT/trim-safe) over `DllImport` (runtime IL stubs, not AOT-safe). Use
  `DllImport` only for .NET Framework targets.
- **Handles:** every owned output `wf_T` → a `THandle : SafeHandle` whose
  `ReleaseHandle()` calls `wf_T_free`. No raw `IntPtr` escapes the interop
  layer.
- **Strings:** specify UTF-8 explicitly — `StringMarshalling = Utf8` /
  `[MarshalAs(UnmanagedType.LPUTF8Str)]`.
- **Widths:** `nuint`/`nint` for `size_t`; `CLong`/`CULong` for C `long` (with
  `[assembly: DisableRuntimeMarshalling]`). Never map `size_t` to `ulong`.
- **Errors:** `bool Try…(out …)` plus a typed exception on non-`WF_OK`.
- **Buffers:** callee-alloc/caller-free outputs → `SafeHandle`; caller-provided
  output buffers → `Span<T>`/`stackalloc` + `Utf8StringMarshaller`.

## Layout

- `cpp/wolfram-cpp/` — header-only C++ RAII wrapper (links `libwolfram`).
- `dotnet/Wolfram.Interop/` — ClangSharp-generated raw bindings + hand-written
  managed layer (links `libwolfram`).

Both consume the same built `libwolfram`; neither ships crypto/transport code.
