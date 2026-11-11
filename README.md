# High-Performance RPC

English | [简体中文](README.zh-CN.md)

A C++17 RPC framework built around a **custom binary protocol**, a **non-blocking epoll/kqueue event loop**, a **priority thread pool**, and **ZooKeeper-based service discovery**. It is designed for services that need synchronous and asynchronous remote calls with pluggable serialization, compressed and encrypted payloads, and predictable behavior under load.

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey)
![Registry](https://img.shields.io/badge/registry-ZooKeeper-green)
![Serialization](https://img.shields.io/badge/serialization-JSON%20%7C%20Protobuf-informational)

## Features

- **Custom binary protocol** — a fixed 12-byte header (`magic`, `message_length`, `sequence_id`) carries a serialized `RpcRequest` or `RpcResponse`, keeping framing explicit and language-agnostic.
- **Non-blocking event loop** — `epoll` on Linux and `kqueue` on macOS; every connection owns independent read/write buffers, and ready sockets are dispatched to the worker pool.
- **Connection management** — a thread-safe singleton keyed by file descriptor tracks connection state, peer address, and close callbacks.
- **Priority thread pool** — core and maximum thread counts, a bounded queue, `HIGH`/`NORMAL`/`LOW` priorities, idle-thread reclamation, and graceful shutdown.
- **Service discovery** — servers register their address under a ZooKeeper namespace, and clients resolve live instances through a dedicated ZooKeeper handler.
- **Load balancing** — random, round-robin, and weighted round-robin strategies behind a common interface, selectable from configuration.
- **Pluggable serialization** — JSON (`nlohmann/json`) and Protobuf backends chosen at runtime, with compile-time type checks.
- **Compressed and encrypted payloads** — Zstd compression with configurable levels, plus a per-message session key wrapped by a master key and Base64 encoding.
- **JSON configuration** — server, client, thread pool, and registry settings are loaded at startup without recompilation.
- **Graceful shutdown** — `SIGINT`/`SIGTERM` stops the event loop, closes connections, drains the thread pool, and cleans up ZooKeeper.
- **Load testing** — a multi-threaded stress client and a parameterized shell driver for repeatable benchmarks.

## Architecture

```text
Client                                          Server
------                                          ------
SerializerManager (JSON / Protobuf)             ServiceManager
        |                                              |
        v                                              v
   RpcRequest  ----------------------------->  RpcRequest
        |                                              |
   Zstd compress                                   Thread pool
        |                                              |
   encrypt (session key)                           Service::HandleRequest
        |                                              |
        v                                              v
  +---------------- TCP (non-blocking) ----------------+
        |                                              |
  RpcResponse <-----------------------------  RpcResponse
        |
   decrypt -> decompress -> deserialize
```

The client serializes the payload, wraps it in an `RpcRequest`, compresses and encrypts it, then writes the frame to the connection. The server's event loop reads the frame, dispatches it to the thread pool, and routes it to the registered `Service` implementation. The response follows the same pipeline in reverse.

### Wire format

| Field            | Type       | Size | Description                   |
| ---------------- | ---------- | ---- | ----------------------------- |
| `magic_number`   | `uint32_t` | 4 B  | Frame validation              |
| `message_length` | `uint32_t` | 4 B  | Length of the serialized body |
| `sequence_id`    | `uint32_t` | 4 B  | Request/response correlation  |
| body             | bytes      | N    | `RpcRequest` / `RpcResponse`  |

## Requirements

- CMake 3.15+ and a C++17 compiler (GCC 9+, Clang 10+)
- [vcpkg](https://github.com/microsoft/vcpkg) with `spdlog`, `fmt`, `nlohmann-json`, `protobuf`, `zstd`, and `zookeeper`
- A running ZooKeeper instance (default `localhost:2181`)
- A `protoc` binary matching the installed Protobuf version

## Build

```bash
git clone https://github.com/oler-hero/high-performance-rpc.git
cd high-performance-rpc

# Update VCPKG_ROOT and PROTOC in the script if your paths differ
./rpc_build.sh
```

The script generates C++ sources from `protos/*.proto`, configures CMake with the vcpkg toolchain, and writes the build output to `build/`.

## Configuration

| File                     | Purpose                                                              |
| ------------------------ | -------------------------------------------------------------------- |
| `config/rpc_server.json` | Listen address and port, max connections, timeout, thread pool       |
| `config/rpc_client.json` | ZooKeeper endpoint and namespace, server port, timeout, retries      |
| `config/servers.json`    | Service name and version, seed registry nodes                        |

Load-balancing strategies: `"random"`, `"round"`, `"weight"`.

## Run

```bash
# Terminal 1: server (reads ../config/rpc_server.json from build/)
cd build && ./rpc_server

# Terminal 2: client demo (sync + async calls)
cd build && ./rpc_client
```

## Stress test

```bash
# 50 concurrent clients, 200 requests each
./stress_test.sh -c 50 -r 200 -h 127.0.0.1 -p 8989

# 120-second run with verbose output
./stress_test.sh -c 20 -t 120 -v
```

## Project status

This repository contains the framework source and the vcpkg/CMake build scaffolding. A few pieces are still being integrated:

- The `protos/` directory (`message.proto` and generated `message.pb.*`) is not tracked; add it before running `rpc_build.sh`.
- The root `CMakeLists.txt` currently defines only the spdlog smoke-test target (`test.cpp`); an aggregate target for `rpc_src/` is on the roadmap.
- Stream framing assumes one message per read; partial-packet handling and network byte-order conversion are planned improvements.
- The encryption layer is a lightweight custom shift cipher for demonstration, not production-grade AES.

## License

No license has been added yet. Add one before distributing or reusing this code.
