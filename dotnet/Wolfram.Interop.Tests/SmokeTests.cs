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

    // The opaque *_result structs are caller-allocated and filled in place by
    // the C parse functions (pointer, not pointer-to-pointer). Allocate a
    // zeroed buffer large enough for any of the result structs and free both
    // the inner heap fields (via the _result_free) and the buffer itself.
    private static IntPtr AllocResult() => Marshal.AllocHGlobal(64);

    [Fact]
    public void DraftCreateParse_Valid()
    {
        string json = "{\"id\":\"at://did/app.bsky.draft.getDrafts/abc\"}";
        IntPtr p = AllocResult();
        int rc = Raw.wf_draft_createDraft_parse(json, (nuint)json.Length, p);
        Assert.Equal(0, rc);
        Assert.NotEqual(IntPtr.Zero, p);
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
        Marshal.FreeHGlobal(p);
    }

    [Fact]
    public void AgentDraftCreateInvalidArg()
    {
        int rc = Raw.wf_agent_draft_createDraft_typed(IntPtr.Zero, "{}", IntPtr.Zero);
        Assert.NotEqual(0, rc);
    }

    [Fact]
    public void TempAddReservedHandle_Parse()
    {
        string valid = "{}";
        IntPtr p = AllocResult();
        int ok = Raw.wf_temp_add_reserved_handle_parse(valid, (nuint)valid.Length, p);
        Assert.Equal(0, ok);
        Assert.NotEqual(IntPtr.Zero, p);
        Raw.wf_temp_add_reserved_handle_result_free(p);
        Marshal.FreeHGlobal(p);

        string bad = "not json";
        IntPtr q = AllocResult();
        int err = Raw.wf_temp_add_reserved_handle_parse(bad, (nuint)bad.Length, q);
        Assert.NotEqual(0, err);
        Marshal.FreeHGlobal(q);

        int argErr = Raw.wf_agent_temp_add_reserved_handle_typed(IntPtr.Zero, "x", IntPtr.Zero);
        Assert.NotEqual(0, argErr);
    }

    [Fact]
    public void BookmarkCreate_Parse()
    {
        string valid = "{}";
        IntPtr p = AllocResult();
        int ok = Raw.wf_bookmark_create_parse(valid, (nuint)valid.Length, p);
        Assert.Equal(0, ok);
        Assert.NotEqual(IntPtr.Zero, p);
        Raw.wf_bookmark_create_result_free(p);
        Marshal.FreeHGlobal(p);

        int argErr = Raw.wf_agent_bookmark_create_typed(IntPtr.Zero, "at://x", IntPtr.Zero);
        Assert.NotEqual(0, argErr);
    }
}
