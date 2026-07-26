# Reproducible build environment for wolfram.
#
# Mirrors the `full-features` CI job (.github/workflows/ci.yml): every optional
# module enabled, so a local build exercises the same surface CI does rather
# than the smaller default configuration.
#
#   docker build -t wolfram .
#   docker run --rm wolfram                    # build + full test suite
#   docker run --rm -it wolfram bash           # poke around
#   docker run --rm wolfram wolfram --version  # the CLI
#
# Mount a working tree over /src to build your checkout instead of the copy
# baked in at image build time:
#
#   docker run --rm -v "$PWD:/src" wolfram
FROM ubuntu:24.04

# g++ is needed for WOLFRAM_BUILD_CPP; python3 for the lexicon generator tests
# that ctest runs; git because the build fetches pinned cJSON/libcbor sources.
# libcjson-dev is separate from that fetched copy: the lexgen tests compile the
# code they generate as a standalone program and link it against a system
# cJSON, so without it those four tests fail to link.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        git \
        ca-certificates \
        libcurl4-openssl-dev \
        libssl-dev \
        libsecp256k1-dev \
        libsodium-dev \
        libsqlite3-dev \
        libmicrohttpd-dev \
        libzstd-dev \
        libc-ares-dev \
        libidn2-dev \
        python3 \
        libcjson-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

# Kept in one layer so `docker build` fails loudly if either step does.
RUN cmake -S /src -B /build \
        -DCMAKE_BUILD_TYPE=Debug \
        -DWOLFRAM_BUILD_SERVER=ON \
        -DWOLFRAM_BUILD_STORE=ON \
        -DWOLFRAM_BUILD_STORE_CRYPTO=ON \
        -DWOLFRAM_BUILD_IDN=ON \
        -DWOLFRAM_BUILD_CPP=ON \
    && cmake --build /build -j"$(nproc)"

ENV PATH="/build:${PATH}"

# Default: run the suite, which is what a contributor wants to reproduce.
CMD ["ctest", "--test-dir", "/build", "--output-on-failure", "-j4"]
