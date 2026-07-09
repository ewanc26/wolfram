// Raw P/Invoke tier. This is the blittable, 1:1 layer over libwolfram's C ABI.
//
// It can be regenerated/expanded with ClangSharpPInvokeGenerator (see
// tools/generate.rsp); the hand-written declarations below cover the
// functions the managed tier currently wraps. Every declaration here is a
// direct pass-through to libwolfram — no logic is reimplemented.
//
// Conventions enforced:
//   * LibraryImport (source-generated, NativeAOT/trim-safe) over DllImport.
//   * UTF-8 strings explicitly (the C API is UTF-8); no reliance on defaults.
//   * nuint for size_t.
//   * Opaque handles are passed as IntPtr; ownership is handled by SafeHandle
//     in the managed tier, never here.

using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;

namespace Wolfram.Interop.Internal;

internal static unsafe partial class Raw
{
    static Raw()
    {
        NativeLibrary.SetDllImportResolver(typeof(Raw).Assembly, Resolve);
    }

    // Single combined resolver: handles both "wolfram" (the SDK) and "libc"
    // (used only to free heap strings the SDK returns via the C allocator).
    private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (string.Equals(libraryName, "wolfram", StringComparison.OrdinalIgnoreCase) &&
            TryLoadWolfram(out IntPtr handle))
        {
            return handle;
        }
        if (string.Equals(libraryName, "libc", StringComparison.OrdinalIgnoreCase) &&
            TryLoadLibC(out IntPtr h))
        {
            return h;
        }
        return IntPtr.Zero;
    }

    private static bool TryLoadWolfram(out IntPtr handle)
    {
        handle = IntPtr.Zero;

        string? custom = Environment.GetEnvironmentVariable("WOLFRAM_NATIVE_LIB");
        if (!string.IsNullOrEmpty(custom) && NativeLibrary.TryLoad(custom, out handle))
            return true;

        string baseDir = AppContext.BaseDirectory;
        string[] candidates =
        {
            Path.Combine(baseDir, "libwolfram.dylib"),
            Path.Combine(baseDir, "libwolfram.so"),
            Path.Combine(baseDir, "wolfram.dll"),
            "libwolfram.dylib",
            "libwolfram.so",
            "wolfram.dll",
        };
        foreach (var c in candidates)
            if (NativeLibrary.TryLoad(c, out handle))
                return true;
        return false;
    }

    private static bool TryLoadLibC(out IntPtr handle)
    {
        handle = IntPtr.Zero;
        string name = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "msvcrt"
                    : RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "libSystem"
                    : "libc";
        if (NativeLibrary.TryLoad(name, out handle))
            return true;
        foreach (var c in new[] { "libc.so.6", "libc.so" })
            if (NativeLibrary.TryLoad(c, out handle))
                return true;
        return false;
    }

    // --- XRPC client (opaque handle, owned by the caller) ---

    [LibraryImport("wolfram", StringMarshalling = StringMarshalling.Utf8)]
    public static partial IntPtr wf_xrpc_client_new(string serviceBaseUrl);

    [LibraryImport("wolfram")]
    public static partial void wf_xrpc_client_free(IntPtr client);

    [LibraryImport("wolfram")]
    public static partial void wf_xrpc_client_set_auth(IntPtr client, [MarshalAs(UnmanagedType.LPUTF8Str)] string accessJwt);

    // Returns a borrowed (not owned) C string; marshalled as a copy, never freed.
    [LibraryImport("wolfram", StringMarshalling = StringMarshalling.Utf8)]
    [return: MarshalAs(UnmanagedType.LPUTF8Str)]
    public static partial string wf_xrpc_get_base_url(IntPtr client);

    // --- JSON ---

    // out IntPtr is the native char**; the caller copies it and frees via libc.
    [LibraryImport("wolfram")]
    public static partial int wf_json_canonicalize([MarshalAs(UnmanagedType.LPUTF8Str)] string input, nuint len, out IntPtr output);
}

// Free a heap pointer allocated by libwolfram (its C allocator == the process
// libc). Used only to release owned strings the SDK returns.
internal static partial class LibC
{
    static LibC()
    {
        // Ensure the combined resolver (registered by Raw) is in place before
        // any "libc" import is resolved.
        RuntimeHelpers.RunClassConstructor(typeof(Raw).TypeHandle);
    }

    [LibraryImport("libc")]
    public static partial void free(IntPtr ptr);
}
