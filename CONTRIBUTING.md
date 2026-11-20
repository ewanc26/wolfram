# Contributing to Wolfram

Wolfram is a C/C++ SDK for the AT Protocol: a client-side, wire-level implementation, not a port of the upstream service backends. Thanks for wanting to help!

Wolfram is also the foundation under several other projects ([MetalBear](https://github.com/ewanc26/metalbear), [Cobalt](https://github.com/ewanc26/cobalt), [Indigo](https://github.com/ewanc26/indigo), [Platinum](https://github.com/ewanc26/platinum)), so changes here ripple. Keep that in mind both for API care and for the value of a fix.

## Getting started

Follow the build instructions in the [README](README.md) (CMake). CI runs on every push; keep it green.

## Notes for this project

- **C is the default language.** C++ is only for complex or sensitive components (RAII resource management, performance-critical code, third-party integrations), and all C++ must expose a C ABI via `extern "C"` so applications never need a C++ toolchain at runtime. The full policy lives in [AGENTS.md](AGENTS.md); read it before adding C++.
- The Lexicon generator is a **development-time** tool (C++ `tools/wf_lexgen.cpp`, `wf_lexgen_tool` CMake target) and must never be linked into, embedded in, or required by applications using `libwolfram`.
- Generated client code must stay pure C23.
- Constrained platforms (Wii U, 3DS, Classic Mac OS 9) consume this SDK: avoid allocations, dependencies, and language features that wouldn't survive there. If your change makes a target harder, say so in the PR.

## Before opening a PR

- Build with CMake and run the tests; link the relevant lexicon or AT Protocol spec link in the PR description when touching protocol behaviour.
- Follow the existing conventional-commit style (`feat:`, `fix:`, `docs:`, …).
- Keep PRs focused, one change each, describing how you verified.
- AI-assisted PRs are welcome; stand behind every line and actually run the build and tests.

## Licence

See [LICENSE](LICENSE). By submitting a contribution you agree it's licensed under the same terms.