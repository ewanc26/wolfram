# Companion language bindings (C++ and C#)

wolfram's core SDK **must remain pure C11**. The C11 core (`libwolfram`) is the
single source of truth: it owns transport, crypto, DAG-CBOR, DNS, and the
server. Companion languages are **consumer layers** that link `libwolfram` and
call into it — they must never re-implement signing, hashing, serialization,
transport, or server logic.

This document records the agreed conventions for the two C-adjacent bindings:
**C++** (`wolfram-cpp`) and **C#** (`Wolfram.Interop`).

## Hard constraints (apply to every binding)

1. **C11 core is authoritative.** A binding calls `libwolfram` functions; it does
   not copy their logic. If wolfram has an honest stub (`WF_ERR_INVALID_ARG` +
   `TODO`), the binding surfaces that as an error/exception — never a fabricated
   success.
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

C++ includes the C headers directly, so there is **no binding generator** — just
a thin RAII wrapper over the C API.

- **Owned outputs:** `std::unique_ptr<wf_T, TFree>` with one stateless deleter
  functor per `wf_T_free`. Deleters are fully regular (`wf_*_free` set), so they
  can be enumerated and generated once.
- **JSON strings:** a `CString` RAII type whose deleter calls the *specific*
  `wf_*_json_free` — never `std::free`/`delete`.
- **Errors:** map `wf_status` to `std::error_code` via a custom `error_category`
  (static singleton). Provide both `error_code` and throwing overloads.
- **Inputs:** fluent helpers over the plain-C `wf_*_input` structs.
- **Build:** header-only; consumers `target_link_libraries(app wolfram)`.

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
