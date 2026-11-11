# High-Performance RPC

[English](README.md) | 简体中文

一个 C++17 RPC 框架，围绕**自定义二进制协议**、**非阻塞 epoll/kqueue 事件循环**、**优先级线程池**和 **ZooKeeper 服务发现**构建。它为需要同步/异步远程调用、可插拔序列化、压缩加密传输，以及在压力下保持可预期行为的服务而设计。

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey)
![Registry](https://img.shields.io/badge/registry-ZooKeeper-green)
![Serialization](https://img.shields.io/badge/serialization-JSON%20%7C%20Protobuf-informational)

## 特性

- **自定义二进制协议** —— 固定 12 字节消息头（`magic`、`message_length`、`sequence_id`）承载序列化后的 `RpcRequest` 或 `RpcResponse`，分帧明确且与语言无关。
- **非阻塞事件循环** —— Linux 使用 `epoll`，macOS 使用 `kqueue`；每个连接拥有独立读写缓冲区，就绪的 socket 交由工作线程池处理。
- **连接管理** —— 线程安全单例，以文件描述符为键，统一维护连接状态、对端地址与关闭回调。
- **优先级线程池** —— 支持核心/最大线程数、有界队列、`HIGH`/`NORMAL`/`LOW` 优先级、空闲线程回收与优雅关闭。
- **服务发现** —— 服务端在 ZooKeeper 命名空间下注册地址，客户端通过专用 ZooKeeper 处理器解析可用实例。
- **负载均衡** —— 随机、轮询、加权轮询三种策略，统一接口，可通过配置选择。
- **可插拔序列化** —— 运行时选择 JSON（`nlohmann/json`）或 Protobuf，并在编译期做类型检查。
- **压缩与加密** —— 支持可配置级别的 Zstd 压缩；每条消息生成随机会话密钥，由主密钥封装后再进行 Base64 编码。
- **JSON 配置** —— 服务端、客户端、线程池与注册中心配置均在启动时加载，无需重新编译。
- **优雅关闭** —— 捕获 `SIGINT`/`SIGTERM`，停止事件循环、关闭连接、排空线程池并清理 ZooKeeper。
- **压测支持** —— 多线程压测客户端与参数化脚本，便于重复基准测试。

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

## 环境要求

- CMake 3.15+ 与支持 C++17 的编译器（GCC 9+、Clang 10+）
- [vcpkg](https://github.com/microsoft/vcpkg)，需安装 `spdlog`、`fmt`、`nlohmann-json`、`protobuf`、`zstd`、`zookeeper`
- 可用的 ZooKeeper 服务（默认 `localhost:2181`）
- 与 Protobuf 版本匹配的 `protoc`

## 构建

```bash
git clone https://github.com/oler-hero/high-performance-rpc.git
cd high-performance-rpc

# 如路径不同，请先修改脚本中的 VCPKG_ROOT 与 PROTOC
./rpc_build.sh
```

脚本会先由 `protos/*.proto` 生成 C++ 源码，再使用 vcpkg 工具链配置 CMake，构建产物输出到 `build/`。

## 配置

| 文件                     | 作用                                                     |
| ------------------------ | -------------------------------------------------------- |
| `config/rpc_server.json` | 监听地址与端口、最大连接数、超时、线程池                 |
| `config/rpc_client.json` | ZooKeeper 地址与命名空间、服务端口、超时、重试次数       |
| `config/servers.json`    | 服务名与版本、初始注册节点                               |

负载均衡策略可选：`"random"`、`"round"`、`"weight"`。

## 运行

```bash
# 终端 1：服务端（从 build/ 读取 ../config/rpc_server.json）
cd build && ./rpc_server

# 终端 2：客户端示例（同步 + 异步调用）
cd build && ./rpc_client
```

## 压测

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
- 加密层是用于演示的自定义移位加密，并非生产级 AES。

## 许可证

暂未添加许可证。分发或复用前请先补充。
