using System.Runtime.InteropServices;
using Wolfram.Interop;
using Wolfram.Interop.Internal;
using Xunit;

namespace Wolfram.Interop.Tests;

// Mirrors the offline C++ wolfram-cpp smoke test. No network access.
public class SmokeTests
{
    [Fact]
    public void StatusMapping()
    {
        Assert.Equal(Status.Ok, (Status)0);
        Assert.NotEqual(Status.Ok, Status.ErrInvalidArg);
    }

    [Fact]
    public void ClientAndCanonicalize()
    {
        using var client = new WolframClient("https://bsky.social");
        Assert.Equal("https://bsky.social", client.BaseUrl);

        string canonical = Wolfram.CanonicalizeJson("{\"b\":1,\"a\":2}");
        Assert.Equal("{\"b\":1,\"a\":2}", canonical);
    }

    // The opaque *_result structs are filled in place by the C parse functions
    // (a pointer to a caller-allocated buffer, not pointer-to-pointer). Mirror
    // the C layouts so parsed fields can be read back via marshalling.
    [StructLayout(LayoutKind.Sequential)]
    private struct DraftCreateResult
    {
        public IntPtr id; // owned UTF-8 string
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct TempAddResult
    {
        public int ok;       // 1 on success
        public IntPtr handle; // optional echoed handle, owned
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct OkIntResult
    {
        public int ok;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct OkBoolResult
    {
        public bool ok;
    }

    private static IntPtr AllocResult() => Marshal.AllocHGlobal(64);

    [Fact]
    public void DraftCreateParse_ReadsId()
    {
        string json = "{\"id\":\"at://did/app.bsky.draft.getDrafts/abc\"}";
        IntPtr p = AllocResult();
        int rc = Raw.wf_draft_createDraft_parse(json, (nuint)json.Length, p);
        Assert.Equal(0, rc);
        var r = Marshal.PtrToStructure<DraftCreateResult>(p);
        Assert.Equal("at://did/app.bsky.draft.getDrafts/abc",
                      Marshal.PtrToStringUTF8(r.id));
        Raw.wf_draft_createDraft_result_free(p);
        Marshal.FreeHGlobal(p);
    }

    [Fact]
    public void DraftCreateParse_Invalid()
    {
        string json = "not json";
        IntPtr p = AllocResult();
        int rc = Raw.wf_draft_createDraft_parse(json, (nuint)json.Length, p);
        Assert.NotEqual(0, rc);
        // On error the out struct is left fully reset.
        Assert.Equal(IntPtr.Zero, Marshal.PtrToStructure<DraftCreateResult>(p).id);
        Marshal.FreeHGlobal(p);
    }

    [Fact]
    public void DraftCreateParse_NullInput()
    {
        IntPtr p = AllocResult();
        int rc = Raw.wf_draft_createDraft_parse(null, 0, p);
        Assert.NotEqual(0, rc);
        Marshal.FreeHGlobal(p);
    }

    [Fact]
    public void AgentDraftCreateInvalidArg()
    {
        int rc = Raw.wf_agent_draft_createDraft_typed(IntPtr.Zero, "{}", IntPtr.Zero);
        Assert.NotEqual(0, rc);
    }

    [Fact]
    public void TempAddReservedHandle_ReadsHandleEcho()
    {
        string json = "{\"handle\":\"reserved.bsky.app\"}";
        IntPtr p = AllocResult();
        int rc = Raw.wf_temp_add_reserved_handle_parse(json, (nuint)json.Length, p);
        Assert.Equal(0, rc);
        var r = Marshal.PtrToStructure<TempAddResult>(p);
        Assert.Equal(1, r.ok);
        Assert.NotEqual(IntPtr.Zero, r.handle);
        Assert.Equal("reserved.bsky.app", Marshal.PtrToStringUTF8(r.handle));
        Raw.wf_temp_add_reserved_handle_result_free(p);
        Marshal.FreeHGlobal(p);
    }

    [Fact]
    public void TempAddReservedHandle_EmptyBodyOk()
    {
        string json = "{}";
        IntPtr p = AllocResult();
        int rc = Raw.wf_temp_add_reserved_handle_parse(json, (nuint)json.Length, p);
        Assert.Equal(0, rc);
        var r = Marshal.PtrToStructure<TempAddResult>(p);
        Assert.Equal(1, r.ok);
        Assert.Equal(IntPtr.Zero, r.handle);
        Raw.wf_temp_add_reserved_handle_result_free(p);
        Marshal.FreeHGlobal(p);
    }

    [Fact]
    public void TempAddReservedHandle_Invalid()
    {
        string json = "not json";
        IntPtr p = AllocResult();
        int rc = Raw.wf_temp_add_reserved_handle_parse(json, (nuint)json.Length, p);
        Assert.NotEqual(0, rc);
        Marshal.FreeHGlobal(p);
    }

    [Fact]
    public void TempRequestPhoneVerification_OkAndInvalidArg()
    {
        string json = "{}";
        IntPtr p = AllocResult();
        int rc = Raw.wf_temp_request_phone_verification_parse(json, (nuint)json.Length, p);
        Assert.Equal(0, rc);
        Assert.Equal(1, Marshal.PtrToStructure<OkIntResult>(p).ok);
        Raw.wf_temp_request_phone_verification_result_free(p);
        Marshal.FreeHGlobal(p);

        Assert.NotEqual(0,
            Raw.wf_agent_temp_request_phone_verification_typed(IntPtr.Zero, "", IntPtr.Zero));
    }

    [Fact]
    public void TempRevokeAccountCredentials_OkAndInvalidArg()
    {
        string json = "{}";
        IntPtr p = AllocResult();
        int rc = Raw.wf_temp_revoke_account_credentials_parse(json, (nuint)json.Length, p);
        Assert.Equal(0, rc);
        Assert.Equal(1, Marshal.PtrToStructure<OkIntResult>(p).ok);
        Raw.wf_temp_revoke_account_credentials_result_free(p);
        Marshal.FreeHGlobal(p);

        Assert.NotEqual(0,
            Raw.wf_agent_temp_revoke_account_credentials_typed(IntPtr.Zero, "", IntPtr.Zero));
    }

    [Fact]
    public void BookmarkCreate_ReadsOkAndInvalidArg()
    {
        string json = "{}";
        IntPtr p = AllocResult();
        int rc = Raw.wf_bookmark_create_parse(json, (nuint)json.Length, p);
        Assert.Equal(0, rc);
        Assert.True(Marshal.PtrToStructure<OkBoolResult>(p).ok);
        Raw.wf_bookmark_create_result_free(p);
        Marshal.FreeHGlobal(p);

        Assert.NotEqual(0,
            Raw.wf_agent_bookmark_create_typed(IntPtr.Zero, "at://x", IntPtr.Zero));
    }

    [Fact]
    public void BookmarkDelete_ReadsOkAndInvalidArg()
    {
        string json = "{}";
        IntPtr p = AllocResult();
        int rc = Raw.wf_bookmark_delete_parse(json, (nuint)json.Length, p);
        Assert.Equal(0, rc);
        Assert.True(Marshal.PtrToStructure<OkBoolResult>(p).ok);
        Raw.wf_bookmark_delete_result_free(p);
        Marshal.FreeHGlobal(p);

        Assert.NotEqual(0,
            Raw.wf_agent_bookmark_delete_typed(IntPtr.Zero, "at://x", IntPtr.Zero));
    }
}
