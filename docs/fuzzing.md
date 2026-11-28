# Fuzzing

Wolfram parses bytes it did not write: firehose and label frames, chat moderation events, CAR files, repo blocks, Jetstream messages and every XRPC response body. `test/fuzz/` has a [libFuzzer](https://llvm.org/docs/LibFuzzer.html) harness for each of those decoders, so the next malformed-input bug turns up in a fuzzer instead of in a client.

Fuzzing is off by default. A normal build, the test suite and the console and Windows cross-builds don't change.

## The harnesses

Each harness is one `LLVMFuzzerTestOneInput` over a public function.

| Harness | Decoder | What feeds it in real use |
| --- | --- | --- |
| `fuzz_cbor` | `wf_cbor_parse`, `wf_cbor_scan_item`, `wf_cbor_serialize` | every DAG-CBOR block |
| `fuzz_car` | `wf_car_parse`, then `wf_commit_parse`, `wf_mst_list`, `wf_mst_get_all_cids`, `wf_mst_find` from each root, and `wf_car_write` | `getRepo`, `getBlocks`, firehose `blocks` |
| `fuzz_mst_node` | `wf_mst_node_parse` | MST nodes inside a CAR |
| `fuzz_commit` | `wf_commit_parse` | signed commit blocks |
| `fuzz_firehose_frame` | `wf_subscribe_decode_frame` | `com.atproto.sync.subscribeRepos` |
| `fuzz_label_frame` | `wf_label_frame_parse_cbor` | binary `subscribeLabels` frames |
| `fuzz_label_message` | `wf_label_message_parse` | JSON `subscribeLabels` messages |
| `fuzz_chat_mod_frame` | `wf_chat_mod_frame_parse_cbor` | `chat.bsky.moderation.subscribeModEvents` |
| `fuzz_chat_mod_event` | `wf_agent_parse_mod_event` | JSON chat moderation events |
| `fuzz_jetstream` | `wf_jetstream_event_parse`, `_parse_v2`, `_parse_typed` | Jetstream messages |
| `fuzz_lex_decode` | every generated `wf_lex_*_decode_json` | XRPC response bodies |

`fuzz_lex_decode` covers all of the generated output decoders. CMake reads their names from `include/wolfram/atproto_lex.h` when it configures, so a regenerated header is picked up without a table to edit by hand. An input to that harness is `<decoder>\n<json>`, where `<decoder>` is the name between `wf_lex_` and `_decode_json`, for example `app_bsky_feed_get_timeline_main_output`. A name that matches no decoder picks one by hash, so every input still reaches a decoder.

Some harnesses also check invariants. A CBOR item that parses has to serialize to a fixed point, a CAR that parses has to write back out and parse to the same blocks, and a commit's fixed-size fields have to stay terminated. A decoder that rejects an input is the expected result and doesn't count as a finding.

## Building

You need Clang with libFuzzer. Apple's Clang doesn't ship it, so on macOS use Homebrew's LLVM:

```sh
LLVM=$(brew --prefix llvm)
cmake -S . -B build-fuzz \
  -DCMAKE_C_COMPILER=$LLVM/bin/clang -DCMAKE_CXX_COMPILER=$LLVM/bin/clang++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DWOLFRAM_BUILD_FUZZ=ON -DWOLFRAM_BUILD_TESTS=OFF -DWOLFRAM_BUILD_EXAMPLES=OFF
cmake --build build-fuzz --target fuzz_cbor fuzz_car   # or any of the harnesses
```

On Linux, `clang-18` and `libclang-rt-18-dev` are enough.

`-DWOLFRAM_BUILD_FUZZ=ON` builds everything with AddressSanitizer and UndefinedBehaviorSanitizer, cJSON and libcbor included, because the fuzzer can only steer by coverage it can see. With any compiler other than Clang the harnesses link a replay-only driver, `test/fuzz/replay_main.c`, instead of libFuzzer. It runs the corpus under the same sanitizers but doesn't mutate anything. The option is refused on console, Windows and Mac OS 9 builds.

## Running

Replay the committed corpus, which is what CI does on every pull request:

```sh
ctest --test-dir build-fuzz -L fuzz
```

Fuzz every harness for a bounded time:

```sh
tools/fuzz.sh build-fuzz 300            # five minutes each
tools/fuzz.sh build-fuzz 60 fuzz_car    # one harness
```

`tools/fuzz.sh` starts each harness from a scratch copy of its corpus, so it never writes into the repository. When a harness stops on a finding, the script replays that input three more times and only fails if it crashes again. Reproducers go into `build-fuzz/fuzz-findings/`. To run a single input, pass it to the harness: `build-fuzz/test/fuzz/fuzz_car path/to/input`.

## The corpus

`test/fuzz/corpus/<harness>/` holds the seeds and every input worth keeping.

- `seed-*` files are written by `python3 test/fuzz/make_seeds.py` from the existing fixtures and vectors (`test/fixtures/`), plus small hand-built frames in each wire shape. The script uses only Python's standard library, CBOR encoder included, so a seed never inherits a bug from Wolfram's own encoder. Running it again rewrites only `seed-*` files.
- `crash-*` files are inputs that once crashed a harness, kept after the fix so the replay test catches any return of the bug. Each fix also adds a unit test in the decoder's own test file.

## When a harness finds something

1. Reproduce it with the harness binary and read the sanitizer report.
2. Fix the decoder in its own `fix(<scope>): ...` commit, with a regression test in the decoder's existing test file and the input added to the corpus as `crash-<what>`.
3. If the bug can be reached from the network, treat it as a security issue. Report it through the private advisory link in `.github/ISSUE_TEMPLATE/config.yml`, not in a public issue.

## CI

The `fuzz` job in `.github/workflows/ci.yml` is part of the `CI gate`. Its gcc leg builds the harnesses with the replay driver and replays the corpus. Its clang leg replays the corpus, then runs `tools/fuzz.sh` for 45 seconds per harness and uploads any reproducible finding as the `fuzz-findings` artifact. Those 45 seconds catch shallow regressions. Longer runs, like the ones in the table below, are done locally.

## Findings so far

| Harness | Finding | Fixed in |
| --- | --- | --- |
| `fuzz_jetstream` | `wf_jetstream_event_parse_typed` read `seq` and `time_us` through a NULL pointer on an `info` message, which carries neither | `fix(jetstream): bound seq and time_us in the typed parser` |
| `fuzz_jetstream` | the same parser cast a `seq` of any size to `int64_t` (undefined behaviour), as did the sync-family payload `seq` | same commit |

Bring-up run (2026-11-27, macOS arm64, Homebrew LLVM 23, ASan and UBSan): every harness ran for 5 minutes and then for 10 more after the fix, about 160 million executions in all. Apart from the two above, nothing was found.
