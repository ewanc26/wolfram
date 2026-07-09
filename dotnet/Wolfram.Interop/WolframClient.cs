using System;
using Wolfram.Interop.Internal;

namespace Wolfram.Interop;

/// <summary>
/// Managed wrapper around a wolfram XRPC client. The underlying native handle
/// is owned by an XrpcClientHandle; this class only forwards calls.
/// </summary>
public sealed class WolframClient : IDisposable
{
    private readonly XrpcClientHandle _handle;

    public WolframClient(string serviceBaseUrl)
    {
        _handle = Wolfram.CreateClient(serviceBaseUrl);
    }

    /// <summary>The service base URL (borrowed native string, copied out).</summary>
    public string BaseUrl
    {
        get
        {
            bool release = false;
            _handle.DangerousAddRef(ref release);
            try
            {
                return Raw.wf_xrpc_get_base_url(_handle.DangerousGetHandle());
            }
            finally
            {
                if (release)
                    _handle.DangerousRelease();
            }
        }
    }

    public void SetAuth(string accessJwt)
    {
        bool release = false;
        _handle.DangerousAddRef(ref release);
        try
        {
            Raw.wf_xrpc_client_set_auth(_handle.DangerousGetHandle(), accessJwt);
        }
        finally
        {
            if (release)
                _handle.DangerousRelease();
        }
    }

    public void Dispose() => _handle.Dispose();
}
