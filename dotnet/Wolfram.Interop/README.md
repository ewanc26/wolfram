# Wolfram.Interop (C# wrapper for wolfram / libwolfram)

A **pure pass-through** C# wrapper over the wolfram C23 SDK (`libwolfram`). It
reimplements nothing — every call forwards to `libwolfram` (no crypto,
transport, DAG-CBOR, or server logic lives here).

## Layout

- `Wolfram.Interop/` — the library.
  - `Internal/Raw.cs` — **raw tier**: `LibraryImport` P/Invoke declarations
    (1:1 with the C ABI). UTF-8 marshalling, `nuint` for `size_t`, opaque
    handles as `IntPtr`. A combined `DllImportResolver` loads `libwolfram`
    (honoring `WOLFRAM_NATIVE_LIB`) and the platform libc (only to free owned
    strings the SDK returns).
  - `SafeHandle/XrpcClientHandle.cs` — `SafeHandle` owning a `wf_xrpc_client*`,
    released via `wf_xrpc_client_free`. No raw `IntPtr` escapes the layer.
  - `Status.cs` / `WolframException.cs` — `wf_status` mirror and the exception
    raised on non-`Ok` results (honest stubs surface here, never faked).
  - `Wolfram.cs` / `WolframClient.cs` — managed, idiomatic API.
- `Wolfram.Interop.Tests/` — xUnit smoke test (offline; mirrors the C++
  `wolfram-cpp-smoke`).
- `tools/generate.rsp` — ClangSharp response file to regenerate/expand the raw
  tier from `include/wolfram/*.h`.

## Building & running

The .NET SDK and a built `libwolfram` are required.

```sh
# 1) build the native core (produces build/libwolfram.dylib | .so | .dll)
cmake -S . -B build && cmake --build build

# 2) make libwolfram loadable by the tests
cp build/libwolfram.dylib Wolfram.Interop.Tests/bin/Debug/net8.0/   # macOS
# or set WOLFRAM_NATIVE_LIB=/abs/path/to/libwolfram.dylib

# 3) build + test
dotnet test dotnet/Wolfram.Interop.Tests/Wolfram.Interop.Tests.csproj
```

The smoke test constructs an XRPC client and canonicalizes JSON offline — it
never touches the network.
