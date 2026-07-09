using System;
using System.Runtime.InteropServices;
using Wolfram.Interop.Internal;

namespace Wolfram.Interop;

/// <summary>
/// SafeHandle owning a <c>wf_xrpc_client*</c>. Released via
/// <c>wf_xrpc_client_free</c> when disposed or finalized. No raw IntPtr escapes
/// the wrapper layer.
/// </summary>
public sealed class XrpcClientHandle : SafeHandle
{
    public XrpcClientHandle(IntPtr ptr, bool ownsHandle = true)
        : base(IntPtr.Zero, ownsHandle) => SetHandle(ptr);

    public override bool IsInvalid => handle == IntPtr.Zero;

    protected override bool ReleaseHandle()
    {
        Raw.wf_xrpc_client_free(handle);
        return true;
    }
}
