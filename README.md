# CookRPC — A High-Performance C++ RPC Framework

English | [简体中文](#cookrpc--高性能-c-rpc-框架)

CookRPC is a from-scratch, C++17 remote procedure call framework for Linux/macOS. It combines a custom binary protocol, an `epoll`/`kqueue` event loop, a priority thread pool, ZooKeeper-based service discovery, pluggable serialization, Zstd compression, and a lightweight encryption layer.

## Features

- **Custom binary protocol** — a fixed 12-byte header (`magic`, `message_length`, `sequence_id`) followed by a serialized `RpcRequest` / `RpcResponse`.
- **Cross-platform event loop** — `epoll` on Linux, `kqueue` on macOS; non-blocking I/O with per-connection read/write buffers.
- **Connection management** — a thread-safe singleton registry keyed by file descriptor, with connection lifecycle callbacks.
- **Priority thread pool** — core/max threads, bounded queue, `HIGH`/`NORMAL`/`LOW` task priorities, idle-thread reclamation, and graceful shutdown.
- **Service registry and discovery** — services register to ZooKeeper; clients resolve live instances through a ZooKeeper handler.
- **Load balancing** — random, round-robin, and weighted round-robin strategies behind a common strategy interface.
- **Pluggable serialization** — JSON (`nlohmann/json`) and Protobuf backends selected at runtime.
- **Compression** — Zstd compression with configurable levels.
- **Encryption layer** — per-message random session key, wrapped with a master key, plus Base64 encoding.
- **JSON configuration** — server, client, thread-pool, and registry settings loaded at startup.
- **Graceful shutdown** — `SIGINT`/`SIGTERM` handling that drains connections, stops the event loop, and cleans up ZooKeeper.
- **Load testing** — a multi-threaded stress client and a parameterized `stress_test.sh` driver.

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

The client serializes the payload, wraps it in an `RpcRequest`, compresses and encrypts it, then writes it to the connection. The server's event loop reads the frame, dispatches the request to the thread pool, and routes it to the registered `Service` implementation. The response follows the same pipeline in reverse.

### Wire format

| Field            | Type       | Size | Description                                 |
| ---------------- | ---------- | ---- | ------------------------------------------- |
| `magic_number`   | `uint32_t` | 4 B  | Frame validation                            |
| `message_length` | `uint32_t` | 4 B  | Length of the serialized message body       |
| `sequence_id`    | `uint32_t` | 4 B  | Request/response correlation ID             |
| body             | bytes      | N    | Serialized `RpcRequest` or `RpcResponse`    |

## Repository layout

```text
rpc_src/
  core/            Server/client entry points, RpcClient, error codes
  network/         Event loop, TCP connection, connection manager, listen socket
  protocol/        Message header, request/response types
  service/         Service interface and service manager
  registry/        ZooKeeper service registry
  conn_balancer/   ZooKeeper handler and load-balancing strategies
  serializer/      JSON and Protobuf serializers
  compress_data/   Zstd compression wrapper
  encrypt/         Message encryption layer
  thread_pool/     Priority thread pool and its singleton
  load_config/     JSON configuration loaders
  tools/           Multi-threaded stress-test client
utils/             Logging and common helpers
config/            Sample server, client, and registry configuration
rpc_build.sh       Protobuf code generation + CMake build script
stress_test.sh     Load-test driver
test.cpp           Minimal spdlog smoke test
```

## Getting started

### Prerequisites

- CMake 3.15+ and a C++17 compiler (GCC 9+, Clang 10+)
- [vcpkg](https://github.com/microsoft/vcpkg) with: `spdlog`, `fmt`, `nlohmann-json`, `protobuf`, `zstd`, `zookeeper`
- A running ZooKeeper instance (default: `localhost:2181`)
- `protoc` matching the installed Protobuf version

### Build

```bash
git clone http://git.cpptrain.top/ningxuan/high-performance-remote-procedure-call.git
cd high-performance-remote-procedure-call

# Edit VCPKG_ROOT / PROTOC in the script if your paths differ
./rpc_build.sh
```

`rpc_build.sh` generates C++ sources from `protos/*.proto`, configures CMake with the vcpkg toolchain, and builds into `build/`.

### Configuration

| File                  | Purpose                                                                 |
| --------------------- | ----------------------------------------------------------------------- |
| `config/rpc_server.json` | Listen address/port, max connections, timeout, thread pool, scheduler |
| `config/rpc_client.json` | ZooKeeper endpoint and namespace, server port, timeout, retries       |
| `config/servers.json`    | Service name/version and seed registry nodes                          |

Example load-balancing strategies: `"random"`, `"round"`, `"weight"`.

### Run

```bash
# Terminal 1: server (reads ../config/rpc_server.json from build/)
cd build && ./rpc_server

# Terminal 2: client demo (sync + async calls)
cd build && ./rpc_client
```

### Stress test

```bash
# 50 concurrent clients, 200 requests each
./stress_test.sh -c 50 -r 200 -h 127.0.0.1 -p 8989

# Run for 120 seconds with verbose output
./stress_test.sh -c 20 -t 120 -v
```

## Project status

This repository contains the framework source and the vcpkg/CMake build scaffolding. A few pieces are still being integrated:

- The `protos/` directory (`message.proto` and generated `message.pb.*`) is not tracked; add it before running `rpc_build.sh`.
- The root `CMakeLists.txt` currently defines only the spdlog smoke-test target (`test.cpp`). An aggregate target for `rpc_src/` is on the roadmap.
- Stream framing assumes one message per read; partial-packet handling and network byte-order conversion are planned improvements.
- The encryption layer is a lightweight custom shift cipher for demonstration, not production-grade AES; swap in OpenSSL if you need real security.

## License

No license has been added yet. Add one before distributing or reusing this code.

---

# CookRPC —— 高性能 C++ RPC 框架

[English](#cookrpc--a-high-performance-c-rpc-framework) | 简体中文

CookRPC 是一个从零实现的 C++17 远程过程调用框架，支持 Linux / macOS。它包含自定义二进制协议、`epoll`/`kqueue` 事件循环、优先级线程池、基于 ZooKeeper 的服务注册与发现、可插拔序列化、Zstd 压缩以及轻量加密层。

## 特性

- **自定义二进制协议** —— 固定 12 字节消息头（`magic`、`message_length`、`sequence_id`），后接序列化后的 `RpcRequest` / `RpcResponse`。
- **跨平台事件循环** —— Linux 使用 `epoll`，macOS 使用 `kqueue`；非阻塞 I/O，每个连接独立读写缓冲区。
- **连接管理** —— 线程安全单例，以文件描述符为键统一管理连接，并提供连接生命周期回调。
- **优先级线程池** —— 支持核心/最大线程数、有界任务队列、`HIGH`/`NORMAL`/`LOW` 优先级、空闲线程回收与优雅关闭。
- **服务注册与发现** —— 服务端向 ZooKeeper 注册实例，客户端通过 ZooKeeper 获取可用节点。
- **负载均衡** —— 随机、轮询、加权轮询三种策略，统一抽象接口，可配置切换。
- **可插拔序列化** —— 运行时选择 JSON（`nlohmann/json`）或 Protobuf。
- **数据压缩** —— 基于 Zstd，可配置压缩级别。
- **加密层** —— 每次加密生成随机会话密钥，使用主密钥封装，再进行 Base64 编码。
- **JSON 配置** —— 服务端、客户端、线程池、注册中心配置均在启动时加载。
- **优雅关闭** —— 捕获 `SIGINT`/`SIGTERM`，停止事件循环、关闭连接、释放线程池与 ZooKeeper。
- **压测工具** —— 多线程压测客户端，以及参数化的 `stress_test.sh` 脚本。

## 架构

```text
客户端                                          服务端
------                                          ------
SerializerManager（JSON / Protobuf）            ServiceManager
        |                                              |
        v                                              v
   RpcRequest  ----------------------------->  RpcRequest
        |                                              |
   Zstd 压缩                                       线程池
        |                                              |
   加密（会话密钥）                                Service::HandleRequest
        |                                              |
        v                                              v
  +---------------- TCP（非阻塞） ----------------------+
        |                                              |
  RpcResponse <-----------------------------  RpcResponse
        |
   解密 -> 解压 -> 反序列化
```

客户端先序列化业务参数，封装为 `RpcRequest`，再压缩、加密并写入连接；服务端事件循环读取完整消息后投递到线程池，由 `ServiceManager` 路由到对应的 `Service` 实现。响应沿相反链路返回。

### 协议格式

| 字段             | 类型       | 长度 | 说明                           |
| ---------------- | ---------- | ---- | ------------------------------ |
| `magic_number`   | `uint32_t` | 4 B  | 魔数，用于校验消息             |
| `message_length` | `uint32_t` | 4 B  | 消息体序列化后的长度           |
| `sequence_id`    | `uint32_t` | 4 B  | 请求/响应关联 ID               |
| body             | bytes      | N    | `RpcRequest` 或 `RpcResponse`  |

## 目录结构

```text
rpc_src/
  core/            服务端/客户端入口、RpcClient、错误码
  network/         事件循环、TCP 连接、连接管理器、监听 socket
  protocol/        消息头、请求/响应类型
  service/         服务接口与服务管理器
  registry/        ZooKeeper 服务注册
  conn_balancer/   ZooKeeper 连接处理与负载均衡策略
  serializer/      JSON / Protobuf 序列化器
  compress_data/   Zstd 压缩封装
  encrypt/         消息加密层
  thread_pool/     优先级线程池及单例
  load_config/     JSON 配置加载
  tools/           多线程压测客户端
utils/             日志与通用工具
config/            服务端、客户端、注册中心示例配置
rpc_build.sh       Protobuf 代码生成 + CMake 构建脚本
stress_test.sh     压测脚本
test.cpp           spdlog 最小冒烟测试
```

## 快速开始

### 环境依赖

- CMake 3.15+ 与支持 C++17 的编译器（GCC 9+ / Clang 10+）
- [vcpkg](https://github.com/microsoft/vcpkg)，需安装：`spdlog`、`fmt`、`nlohmann-json`、`protobuf`、`zstd`、`zookeeper`
- 可用的 ZooKeeper 服务（默认 `localhost:2181`）
- 与 Protobuf 版本匹配的 `protoc`

### 构建

```bash
git clone http://git.cpptrain.top/ningxuan/high-performance-remote-procedure-call.git
cd high-performance-remote-procedure-call

# 如你的路径不同，请先修改脚本中的 VCPKG_ROOT / PROTOC
./rpc_build.sh
```

`rpc_build.sh` 会先由 `protos/*.proto` 生成 C++ 源码，再使用 vcpkg 工具链配置 CMake，最终产物输出到 `build/`。

### 配置说明

| 文件                     | 作用                                                       |
| ------------------------ | ---------------------------------------------------------- |
| `config/rpc_server.json` | 监听地址/端口、最大连接数、超时、线程池、注册中心          |
| `config/rpc_client.json` | ZooKeeper 地址与命名空间、服务端口、超时、重试次数          |
| `config/servers.json`    | 服务名/版本以及初始注册节点                                 |

负载均衡策略可选：`"random"`、`"round"`、`"weight"`。

### 运行

```bash
# 终端 1：服务端（从 build/ 读取 ../config/rpc_server.json）
cd build && ./rpc_server

# 终端 2：客户端示例（同步 + 异步调用）
cd build && ./rpc_client
```

### 压测

```bash
# 50 个并发客户端，每个发送 200 次请求
./stress_test.sh -c 50 -r 200 -h 127.0.0.1 -p 8989

# 持续压测 120 秒并输出详细日志
./stress_test.sh -c 20 -t 120 -v
```

## 项目状态

本仓库包含框架源码以及 vcpkg/CMake 构建脚手架，以下部分仍在整合中：

- `protos/` 目录（`message.proto` 及生成的 `message.pb.*`）尚未纳入版本管理，运行 `rpc_build.sh` 前需自行补齐。
- 根目录 `CMakeLists.txt` 目前只定义了 spdlog 冒烟测试目标（`test.cpp`），`rpc_src/` 的聚合构建目标已列入待办。
- 协议层目前假定一次读取即一条完整消息；半包/粘包处理与网络字节序转换有待完善。
- 加密层是用于演示的自定义移位加密，并非生产级 AES；如需真实安全性请替换为 OpenSSL 等实现。

## 许可证

暂未添加许可证。分发或复用前请先补充。
