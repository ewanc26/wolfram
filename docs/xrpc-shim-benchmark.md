# XRPC shim benchmark: keep-alive, route count and listen backlog

This records what the libmicrohttpd shim (`src/server/mhd_shim.c`) does under
load, so the limitations in [eval-event-loop.md](eval-event-loop.md) rest on
measurements rather than on the code alone. It is a loopback benchmark on one
Mac. It is not a substitute for a test on the real network path.

## What was measured

Three limitations from `eval-event-loop.md`:

- **No keep-alive.** Every response closes its connection.
- **Route lookup is a linear scan** under `routes_mutex`, and the doc's
  threshold is fewer than 100 routes.
- **The listen backlog is 16** (`mhd_shim.c`, `listen(..., 16)`).

## Method

- Host: macOS, arm64 (Apple silicon). Build: Release, Ninja, shim forced on with
  `-DWOLFRAM_MHD_SHIM=ON -DWOLFRAM_BUILD_SERVER=ON -DWOLFRAM_BUILD_TESTS=OFF`.
  The configure log reports `XRPC server using the libmicrohttpd shim`.
- Load target: `tools/bench/xrpc_shim_bench.c`. It starts one server on
  `127.0.0.1` with N extra `GET /r/<i>` routes, then one `GET /hit` route
  registered last. `/hit` is therefore the worst case for a linear scan.
- Client: ApacheBench (`ab`), 5000 requests per run, one run per setting.
  `-k` turns on keep-alive. ab's `Keep-Alive requests` line reports how many
  responses reused a connection.

Build and run (from the repository root):

```sh
cmake -S . -B build-shim -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DWOLFRAM_MHD_SHIM=ON -DWOLFRAM_BUILD_SERVER=ON -DWOLFRAM_BUILD_TESTS=OFF
cmake --build build-shim -j8
cc -O2 -std=c11 -Iinclude -I build-shim/_deps/cjson-src \
   tools/bench/xrpc_shim_bench.c -L build-shim -lwolfram \
   -Wl,-rpath,"$PWD/build-shim" -o xrpc_shim_bench
./xrpc_shim_bench 9201 1 &          # 1 extra route
ab -n 5000 -c 8 -k http://127.0.0.1:9201/hit
```

The `cjson-src` path is where CMake's FetchContent puts cJSON; adjust it if
your build directory differs.

## Results

Requests per second and mean latency per request (ab's "Time per request",
across all concurrent requests), 5000 requests per row. Keep-alive reuse was
zero in every `-k` row.

| Routes | Concurrency | Keep-alive | Req/s | Mean ms | Failed | Keep-alive reuse |
|-------:|------------:|:----------:|------:|--------:|-------:|-----------------:|
| 1 | 1 | no | 14474 | 0.069 | 0 | n/a |
| 1 | 1 | yes | 14860 | 0.067 | 0 | 0 |
| 1 | 8 | no | 36713 | 0.218 | 0 | n/a |
| 1 | 8 | yes | 27988 | 0.286 | 0 | 0 |
| 1 | 128 | no | 34619 | 3.697 | 0 | n/a |
| 120 | 1 | no | 14397 | 0.069 | 0 | n/a |
| 120 | 1 | yes | 14961 | 0.067 | 0 | 0 |
| 120 | 8 | no | 35432 | 0.226 | 0 | n/a |
| 120 | 8 | yes | 26778 | 0.299 | 0 | 0 |
| 120 | 128 | no | 32630 | 3.923 | 0 | n/a |

## What the numbers show

- **Keep-alive is not implemented.** Every `-k` run reports zero reused
  connections, so the shim closes each connection after its response, as the
  limitation says. At concurrency 8, the `-k` runs were slower than the
  connection-per-request runs. That is consistent with paying a connection
  setup on each request, but this run does not isolate that cost, so treat the
  gap as a sign, not a measurement. At concurrency 1 the difference is within
  noise.
- **Route count does not show up at 120 routes.** `/hit` is the last route
  registered, so every request scans all 121 entries. Throughput at 120 routes
  is within a few per cent of 1 route in every row, and some rows are faster.
  The linear scan costs less than the per-request overhead measured here, so
  the `<100 routes` threshold is conservative. A hash or trie would not change
  anything measurable at this size.
- **The backlog did not drop connections on loopback.** At concurrency 128,
  with a backlog of 16, every run completed with zero failed requests. The
  worst mean latency was about 3.9 ms. Loopback does not reproduce the SYN drops
  that the limitation describes. Those would need a real network burst, and
  this benchmark does not test that.

## What this does not show

- Loopback only, from a single client. Results on a real network, through a
  reverse proxy, or from many clients may differ.
- One run per setting. The noise between rows is several per cent, so only
  large differences mean anything.
- Nothing about the desktop build, which uses real libmicrohttpd and not the
  shim.
