namespace Wolfram.Interop;

/// <summary>
/// wolfram status codes. Values mirror the C11 <c>wf_status</c> enum
/// (canonical in xrpc.h). A managed copy is required because the wrapper must
/// not reimplement the codes — it only forwards them from libwolfram.
/// </summary>
public enum Status
{
    Ok = 0,
    ErrInvalidArg = 1,
    ErrAlloc = 2,
    ErrNetwork = 3,
    ErrHttp = 4,
    ErrParse = 5,
    ErrNotFound = 6,
    ErrWouldBlock = 7,
    ErrDidResolve = 8,
    ErrDidDocumentNotFound = 9,
    ErrHandleResolve = 10,
    ErrHandleDocumentNotFound = 11,
    ErrHandleTtlExpired = 12,
    ErrHandleCacheKey = 13,
    ErrCrypto = 14,
    ErrValidation = 15,
    ErrState = 16,
    ErrConfig = 17,
    ErrTimeout = 18,
    ErrUnsupported = 19,
    ErrPermission = 20,
    ErrRateLimit = 21,
    ErrDuplicate = 22,
    ErrConflict = 23,
    ErrNotImplemented = 24,
    ErrInternal = 25,
    ErrUnknown = 26,
}
