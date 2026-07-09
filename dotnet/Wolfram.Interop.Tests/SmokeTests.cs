using Wolfram.Interop;
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
}
