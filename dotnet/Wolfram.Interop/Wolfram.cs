using System;
using System.Runtime.InteropServices;
using System.Text;
using Wolfram.Interop.Internal;

namespace Wolfram.Interop;

/// <summary>
/// Static, allocation-light entry points. All wrappers ultimately call into
/// libwolfram; nothing is reimplemented here.
/// </summary>
public static class Wolfram
{
    /// <summary>
    /// Canonicalize JSON (a key-order-preserving round-trip). The native output
    /// is heap-owned and freed via libc free.
    /// </summary>
    public static string CanonicalizeJson(string json)
    {
        int status = Raw.wf_json_canonicalize(
            json,
            (nuint)Encoding.UTF8.GetByteCount(json),
            out IntPtr native);

        if (native == IntPtr.Zero)
            throw new WolframException((Status)status);

        try
        {
            return Marshal.PtrToStringUTF8(native) ?? string.Empty;
        }
        finally
        {
            LibC.free(native);
        }
    }

    /// <summary>Create an XRPC client. Throws on allocation failure.</summary>
    public static XrpcClientHandle CreateClient(string serviceBaseUrl)
    {
        IntPtr ptr = Raw.wf_xrpc_client_new(serviceBaseUrl);
        if (ptr == IntPtr.Zero)
            throw new WolframException(Status.ErrAlloc);
        return new XrpcClientHandle(ptr);
    }
}
