# Hosted Wolfram OAuth node.
#
# Build from the wolfram repository root:
#
#   docker build -f deploy/oauth-node/Dockerfile -t wolfram-oauth-node .
#
# or with the compose file next to this one:
#
#   docker compose -f deploy/oauth-node/compose.yaml up -d --build
#
# The node is memory-only: it has no state volume, state file or database.
# Completed sessions and pending pairings live in process memory and are lost
# on restart. Do not add a volume for /var/lib/wolfram-oauth-node.

FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        git \
        ca-certificates \
        libcurl4-openssl-dev \
        libssl-dev \
        libmicrohttpd-dev \
        libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

RUN cmake -S /src -B /build \
        -DCMAKE_BUILD_TYPE=Release \
        -DWOLFRAM_BUILD_OAUTH_NODE=ON \
        -DWOLFRAM_BUILD_TESTS=OFF \
        -DWOLFRAM_BUILD_EXAMPLES=OFF \
    && cmake --build /build --target wolfram-oauth-node -j"$(nproc)"

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        libcurl4 \
        libssl3 \
        libmicrohttpd12 \
        libsqlite3-0 \
        libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --user-group --uid 10001 --home /nonexistent \
        --shell /usr/sbin/nologin oauth

COPY --from=build /build/wolfram-oauth-node /usr/local/bin/wolfram-oauth-node
COPY --from=build /build/libwolfram.so /usr/local/lib/libwolfram.so
COPY --from=build /build/_deps/cjson-build/libcjson.so* /usr/local/lib/
COPY --from=build /build/_deps/libcbor-build/src/libcbor.so* /usr/local/lib/
RUN ldconfig

USER oauth
EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/wolfram-oauth-node"]
