using System;

namespace Wolfram.Interop;

/// <summary>
/// Raised when a libwolfram call returns a non-<see cref="Status.Ok"/> status.
/// The wrapper never fabricates success: an honest C stub (WF_ERR_INVALID_ARG
/// + TODO) surfaces here as this exception.
/// </summary>
public sealed class WolframException : Exception
{
    public Status Status { get; }

    public WolframException(Status status)
        : base($"wolfram error: {status}") => Status = status;

    public WolframException(Status status, string message)
        : base(message) => Status = status;
}
