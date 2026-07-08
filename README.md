# wolfram

A C SDK for the AT Protocol.

The runtime library and all generated client code are pure C11. The optional
Lexicon generator is a development-time Python tool and is never linked into,
embedded in, or required by applications using `libwolfram`.

**Status:** Early development — core transport, identity, repo, agent, OAuth,
and sync layers are implemented and tested, but it is not yet at full feature
parity with the official SDKs.

## Documentation

Per-module usage guides (runnable C snippets):

- [`docs/agent.md`](docs/agent.md) — high-level `wf_agent_*` API
- [`docs/sync.md`](docs/sync.md) — repo CAR, firehose, commit verification
- [`docs/validate.md`](docs/validate.md) — `wf_validate_value` / `wf_validate_record`
- [`docs/oauth.md`](docs/oauth.md) — OAuth/DPoP, PKCE, PAR, callback flow

Topic guides:

- [Design & rationale](docs/design.md) — overview, the name, and why C not Rust
- [Getting started](docs/getting-started.md) — install, build, persistence, Lexicon generation, examples
- [Modules](docs/modules.md) — full module/status table
- [Roadmap](docs/roadmap.md) — what's built and what's next
- [CLI reference](docs/cli.md) — the `wolfram` command-line client

## Quick start

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

```sh
./build/create_post https://bsky.social you@example.com yourpassword "Hello from wolfram!"
```

`wolfram` is organized into small, layered modules — transport → identity →
repo → agent. See [docs/modules.md](docs/modules.md) for the full status table.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT
