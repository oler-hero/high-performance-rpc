# High-Performance RPC

English | [简体中文](README.zh-CN.md)

A C++17 distributed RPC framework built from scratch: a custom binary protocol, an epoll/kqueue non-blocking event loop, a priority thread pool, ZooKeeper-based service discovery, three load-balancing strategies, JSON/Protobuf serialization, Zstd compression, and two-tier key-wrapping encryption. The repository ships a reproducible stress client and log on/off A/B data so every performance claim can be verified rather than assumed.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey)
![Registry](https://img.shields.io/badge/registry-ZooKeeper-green)
![Serialization](https://img.shields.io/badge/serialization-JSON%20%7C%20Protobuf-informational)
![Build](https://img.shields.io/badge/build-CMake%20%2B%20vcpkg-brightgreen)

## Highlights

- **End-to-end, not a single-point demo**: protocol codec → non-blocking networking → priority thread pool → serialization → compression/encryption → service registry/discovery → load balancing.
- **Reproducible performance**: 50 concurrent clients in a Release build, real 2C4G cgroup at **19.4k QPS / 1.51 ms** (1.12M requests, zero failures over 60 seconds), **20.2k QPS** under no-interval saturation with the server saturating both cores; 6-core native at **20.0k QPS / 1.44 ms**, capped by the client's 1 ms pacing.
- **Engineering reliability**: connection state is atomic and `Read`/`Write`/`Close` share a mutex, eliminating the worker-write vs event-loop-close race. After the fix, a 60-second run on real 2C4G completes with **zero failures** (774k requests). The client reconnect path no longer double-locks a non-recursive mutex, so it recovers about **2 seconds** after a server restart instead of deadlocking.
- **Pluggable by design**: JSON/Protobuf serialization and `random`/`round`/`weight` load balancing sit behind common interfaces and are selected via configuration.

## Architecture

```text
Client                                              Server
------                                              ------
SerializerManager (JSON / Protobuf)                 ServiceManager
        |                                                  |
        v                                                  v
   RpcRequest  ----------------------------------->  RpcRequest
        |                                                  |
   Zstd compression                                 Priority thread pool
        |                                                  |
   session-key encryption                          Service::HandleRequest
        |                                                  |
        v                                                  v
  +------------------ TCP (non-blocking) ------------------+
        |                                                  |
  RpcResponse <-------------------------------------  RpcResponse
        |
   decrypt -> decompress -> deserialize
```

The client serializes the payload, wraps it in an `RpcRequest`, compresses and encrypts it, then writes the frame to the connection. The server's event loop reads the frame, dispatches it to the priority thread pool, and routes it to the registered `Service` implementation. The response follows the same pipeline in reverse.

### Wire format

| Field             | Type       | Size | Description                   |
| ----------------- | ---------- | ---- | ----------------------------- |
| `magic_number`    | `uint32_t` | 4 B  | Frame validation              |
| `message_length`  | `uint32_t` | 4 B  | Length of the serialized body |
| `sequence_id`     | `uint32_t` | 4 B  | Request/response correlation  |
| body              | bytes      | N    | `RpcRequest` / `RpcResponse`  |

## Features

- **Custom binary protocol** — a fixed 12-byte header (`magic`, `message_length`, `sequence_id`) plus a 4-byte length prefix that frames messages on the wire, so partial and coalesced TCP packets are handled correctly.
- **Non-blocking event loop** — `epoll` (edge-triggered) on Linux and `kqueue` on macOS; every connection owns independent read/write buffers, and ready sockets are dispatched to the worker pool.
- **Priority thread pool** — core/max thread counts, a bounded queue, `HIGH`/`NORMAL`/`LOW` priorities, idle-thread reclamation, and graceful shutdown.
- **Service discovery** — servers create ephemeral nodes under a ZooKeeper namespace; session expiry removes them automatically, and clients re-resolve live instances on reconnect. If ZooKeeper is temporarily unavailable, the client keeps the last successfully fetched instance list as a local cache.
- **Load balancing** — random, round-robin, and weighted round-robin strategies behind a common interface, selectable from configuration.
- **Pluggable serialization** — JSON (`nlohmann/json`) and Protobuf backends chosen at runtime, with compile-time type checks.
- **Compression and encryption** — configurable Zstd compression; each message uses a random 256-bit session key wrapped by a master key, then encrypted with **AES-256-GCM** (OpenSSL EVP) and Base64-encoded. This is two-layer symmetric encryption, not hybrid encryption; production should add **ECDH** key exchange plus key rotation and replay protection.
- **Hardened connection lifecycle** — `Connection::state_` is atomic and `Read`/`Write`/`Close` share `io_mutex_`, preventing a worker from writing a response while the event loop closes the same connection.
- **Configuration-driven** — server, client, thread pool, and registry settings are loaded at startup without recompilation.
- **Graceful shutdown** — `SIGINT`/`SIGTERM` stops the event loop, closes connections, drains the thread pool, and cleans up ZooKeeper.
- **Load-testing toolchain** — a multi-threaded stress client plus a parameterized shell driver, supporting request-count and duration modes for reproducible log on/off A/B tests.

## Quick start

### Requirements

- CMake 3.15+ and a C++17 compiler (GCC 9+, Clang 10+)
- [vcpkg](https://github.com/microsoft/vcpkg) with `protobuf`, `nlohmann-json`, `spdlog`, `zstd`, `openssl`, and `zookeeper`
- A threaded ZooKeeper C client (`libzookeeper_mt`) that exposes the synchronous API
- A running ZooKeeper instance (default `localhost:2181`)

> Some vcpkg `zookeeper` packages ship only the single-threaded library and omit `zoo_create` / `zoo_exists`. In that case, build the C client with `-DTHREADED` and point CMake at it with `RPC_ZOOKEEPER_MT_LIB` / `RPC_ZOOKEEPER_HASHTABLE_LIB`.

### Build

```bash
export VCPKG_ROOT=/path/to/vcpkg

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DRPC_ZOOKEEPER_MT_LIB=/path/to/libzookeeper_mt.a \
  -DRPC_ZOOKEEPER_HASHTABLE_LIB=/path/to/libzookeeper_hashtable.a

cmake --build build -j"$(nproc)"
```

If your vcpkg `zookeeper` package already provides the synchronous API, omit the two `RPC_ZOOKEEPER_*` arguments. Add `-DZOOKEEPER_INCLUDE_DIR=/path/to/include` if the headers are not on the default search path.

Artifacts:

```text
build/rpc_server
build/rpc_client
build/stress_test_client
```

### Start ZooKeeper

```bash
zkServer.sh start
```

### Run

```bash
# Terminal 1: server (reads ../config/rpc_server.json from build/)
cd build && ./rpc_server

# Terminal 2: demo client (sync + async calls)
cd build && ./rpc_client
```

For local testing, change the registration node in `config/servers.json` to `127.0.0.1:8989` instead of a public or `0.0.0.0` address.

### Stress test

```bash
# 50 concurrent clients, 400 requests each (20,000 total)
cd build
./stress_test_client -c 50 -n 20000 -f ../config/rpc_client.json

# 50 concurrent clients for 60 seconds (1 ms interval per request by default)
./stress_test_client -c 50 -t 60 -f ../config/rpc_client.json

# Saturation run: disable the interval to measure maximum server throughput
./stress_test_client -c 50 -t 60 -i 0 -f ../config/rpc_client.json
```

> The interval paces the load so the client itself does not become the bottleneck. It is included in wall-clock QPS but excluded from average response time. Use `-i 0` to disable it for saturation tests.

### Log on/off A/B

Build twice and change only `SPDLOG_ACTIVE_LEVEL`:

```bash
# Baseline: per-request logs enabled
cmake -S . -B build-verbose -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DRPC_ZOOKEEPER_MT_LIB=/path/to/libzookeeper_mt.a \
  -DRPC_ZOOKEEPER_HASHTABLE_LIB=/path/to/libzookeeper_hashtable.a
cmake --build build-verbose -j"$(nproc)"

# Quiet: info/debug logs compiled out
cmake -S . -B build-quiet -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_WARN" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DRPC_ZOOKEEPER_MT_LIB=/path/to/libzookeeper_mt.a \
  -DRPC_ZOOKEEPER_HASHTABLE_LIB=/path/to/libzookeeper_hashtable.a
cmake --build build-quiet -j"$(nproc)"
```

## Benchmark

Test conditions: Release build, 50 concurrent clients, `RpcService/Echo`, JSON serialization, Zstd compression + session-key encryption, 60-second steady state, 100% success. 2C4G is enforced by a systemd cgroup (2 cores / 4 GB, `CPUQuota=200%`, `MemoryMax=4G`); the server runs inside the cgroup and the client runs on the host so they do not compete for the same quota.

### Steady-state throughput and latency percentiles (2C4G)

| Environment | Requests | QPS | Average | P50 | P90 | P99 | Success |
|---|---:|---:|---:|---:|---:|---:|---:|
| Real 2C4G (cgroup 2C/4G) | 1,120,245 | 19,444.65 | 1.51 ms | 1.46 ms | — | 4.47 ms | 100% |
| 6 cores / 7.8 GB WSL2 | 1,141,633 | 19,990.77 | 1.44 ms | 1.42 ms | — | 3.05 ms | 100% |

> The 6-core and 2C4G numbers are close because 50 clients with a 1 ms interval cap throughput near 20k; the 2C4G saturation run also lands at 20.2k, so both configurations are near their limit. Both runs had zero failures and zero payload mismatches.

> The gap between P50 and the average shows typical requests are fast; the mean is pulled up by the tail. On 2C4G the tail comes from CPU-quota scheduling and thread-pool queueing.

### Log on/off A/B (6 cores and 2C4G, measured before the cipher rewrite)

| Environment | Configuration | Requests | QPS | Average latency | Success |
|---|---|---:|---:|---:|---:|
| 6 cores / 7.8 GB WSL2 | logs on (60 s steady state) | 986,276 | 15,768.56 | 1.90 ms | 100% |
| 6 cores / 7.8 GB WSL2 | logs off (60 s steady state) | 989,897 | 15,811.54 | 1.73 ms | 100% |
| Real 2C4G (cgroup 2C/4G) | logs on (60 s steady state) | 774,493 | 12,287.69 | 2.81 ms | 100% |
| Real 2C4G (cgroup 2C/4G) | logs off (60 s steady state) | 736,286 | 11,660.80 | 2.88 ms | 100% |

Relative improvement:

| Environment | QPS | Average latency |
|---|---:|---:|
| 6 cores (60 s steady state) | +0.3% | -9.0% |
| Real 2C4G (60 s steady state) | -5.1% | +2.5% (within noise) |

> On 6 cores, QPS is capped by the client-side 1 ms interval, so the log switch barely moves QPS; average latency is the more sensitive metric. On 2C4G, the log on/off difference is within run-to-run noise, indicating CPU quota—not logging—is the bottleneck. Absolute QPS still depends on CPU model, virtualization, disk, and client placement.

### Saturation run: bottleneck attribution (measured before the cipher rewrite)

To check whether the 1 ms interval hides server capacity, a second run removes the interval entirely:

| Configuration | Requests | QPS | Average | P50 | P90 | P99 | Server CPU | Client CPU |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 ms interval | 778,104 | 12,587.62 | 2.77 ms | 1.89 ms | 2.61 ms | 23.80 ms | — | — |
| No interval (saturated) | 740,467 | 11,827.98 | 4.20 ms | 3.03 ms | 4.05 ms | 28.88 ms | 1.91 / 2 cores | 1.63 / 6 cores |

> Removing the interval lowers QPS and doubles latency: the server already saturates its 2-core quota, while the client uses only 1.63 cores. The 2C4G bottleneck is server CPU, so 12.6k QPS is close to the limit of this configuration rather than an artifact of client pacing.

> Re-measured after the cipher rewrite (current code): no-interval saturation on 2C4G reaches **20,211.75 QPS / 2.45 ms** with the server at 2.0/2 cores; with the 1 ms interval it is 19,444.65 QPS / 1.51 ms. The two are close, so the server CPU bound now sets the ceiling. A micro-benchmark shows per-message key generation dropping from 32.7 µs to 0.49 µs, and per-RPC crypto from about 68 µs to about 3 µs.

## Engineering notes

### 1. The cost of hot-path logging

The server's thread pool prints an INFO line inside `enqueue` while holding the queue mutex, and the client logs one line per response. Under load this competes with request processing for CPU and I/O. The repository provides log-on/off builds so the cost is measured, not guessed. In the 60-second steady state on 6 cores, turning logs off reduces average latency by about 9% while QPS stays flat; on real 2C4G the difference is within noise, where CPU quota is the bottleneck.

### 2. Connection write/close race

In the original implementation, `Connection::state_`, `fd_`, and `write_buffer_` were unsynchronized. A worker could be writing a response while the event loop closed the same connection, producing `write error` / `Connection is invalid`. The current implementation makes `state_` atomic and serializes `Read` / `Write` / `Close` with `io_mutex_`; after the fix, a 60-second run on real 2C4G completes with zero failures (774k requests).

### 3. Client reconnect double-lock

`Call` holds `mutex_` for its whole body, and the `checkConnection()` it invokes reaches `Reconnect() → Disconnect()` when the connection is stale. `Disconnect()` locks the same non-recursive `std::mutex` again, which is undefined behavior and blocks forever under glibc — a server restart would hang the client instead of reconnecting it.

The fix moves the lock boundary to the public entry points: `Connect()` / `Disconnect()` take the lock and delegate to `ConnectLocked()` / `DisconnectLocked()`, while `checkConnection()` uses the unlocked variants because its caller already holds the lock. Read timeouts also mark the connection stale so the next call actually reconnects.

Fault injection: with 10 concurrent clients running, the server was killed with `kill -9` and restarted 40 seconds later; the client resumed within about 2 seconds and the process exited normally instead of hanging.

### 4. Release vs Debug

The original build script used `Debug`, which significantly depresses benchmark numbers. Use `Release` for performance work. On a 2C4G box, start with `core_threads=2~4` instead of the default 8/10 to avoid excessive context switching.

## Project layout

```text
rpc_src/
  core/           server/client entrypoints, RpcClient
  network/        epoll/kqueue, Connection, ConnectionManager
  protocol/       12-byte header, RpcRequest / RpcResponse
  serializer/     JSON / Protobuf adapters
  compress_data/  Zstd wrapper
  encrypt/        session-key + master-key wrapping
  conn_balancer/  load balancing and ZooKeeper handler
  registry/       ephemeral-node registration
  thread_pool/    priority thread pool
  load_config/    JSON configuration
  tools/          stress test client
protos/           message.proto and generated sources
config/           server / client / registry configuration
utils/            logging and helpers
```

## Limitations and roadmap

- **ZooKeeper Watch**: discovery currently relies on ephemeral nodes, session timeout, and reconnect-based re-resolution, with the last successfully fetched instance list retained as a local cache when ZooKeeper is unavailable. Watch-based updates and health checks are on the roadmap.
- **Encryption primitive**: the current implementation uses AES-256-GCM (OpenSSL EVP) with a demo master key; production should load the master key from a secret manager and add ECDH key exchange, key rotation, and replay protection.
- **Benchmark metrics**: the client reports QPS, average latency, P50/P90/P99, and max latency. CPU/memory usage and longer soak tests are planned.
- **Client interval**: a 1 ms interval paces the load by default; `-i 0` disables it for saturation throughput tests.

## License

Released under the [MIT License](LICENSE).
