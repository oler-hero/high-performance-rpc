# High-Performance RPC

[English](README.md) | 简体中文

一个用 C++17 从零实现的分布式 RPC 框架：自定义二进制协议、epoll/kqueue 非阻塞事件循环、优先级线程池、ZooKeeper 服务发现、三种负载均衡、JSON/Protobuf 双序列化、Zstd 压缩与两级密钥封装加密。项目附带可复现的压测客户端与日志 A/B 数据，用来验证每一次性能优化是否真实有效。

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-lightgrey)
![Registry](https://img.shields.io/badge/registry-ZooKeeper-green)
![Serialization](https://img.shields.io/badge/serialization-JSON%20%7C%20Protobuf-informational)
![Build](https://img.shields.io/badge/build-CMake%20%2B%20vcpkg-brightgreen)

## 项目亮点

- **完整链路，而不是单点 Demo**：协议编解码 → 非阻塞网络 → 优先级线程池 → 序列化 → 压缩/加密 → 服务注册/发现 → 负载均衡，每一层都有可运行的代码与配置。
- **可复现的性能数据**：50 并发客户端、Release 构建，真实 2C4G cgroup **19.4k QPS / 1.51ms**（60 秒 112 万请求 0 失败），无间隔饱和 **20.2k QPS** 且服务端 CPU 打满 2 核；6 核原生 **20.0k QPS / 1.44ms**（受客户端 1ms 节奏限制）。
- **工程可靠性**：连接状态使用 `atomic` + `mutex` 保护，修复 worker 写响应与事件循环关闭连接之间的竞态；修复后真实 2C4G、50 并发、60 秒 **0 失败**（112 万请求全部通过）。客户端重连路径同样消除了重复加锁导致的死锁，服务端重启后约 **2 秒**自动恢复。
- **可插拔设计**：JSON/Protobuf 序列化、`random`/`round`/`weight` 负载均衡均通过统一接口 + 配置切换，不绑定具体实现。

## 架构

```text
客户端                                              服务端
------                                              ------
SerializerManager（JSON / Protobuf）                ServiceManager
        |                                                  |
        v                                                  v
   RpcRequest  ----------------------------------->  RpcRequest
        |                                                  |
   Zstd 压缩                                        优先级线程池
        |                                                  |
   会话密钥加密                                    Service::HandleRequest
        |                                                  |
        v                                                  v
  +------------------ TCP（非阻塞） -------------------------+
        |                                                  |
  RpcResponse <-------------------------------------  RpcResponse
        |
   解密 -> 解压 -> 反序列化
```

客户端先序列化业务参数，封装为 `RpcRequest`，再压缩、加密并写入连接；服务端事件循环读取完整消息后投递到优先级线程池，由 `ServiceManager` 路由到对应的 `Service` 实现。响应沿相反链路返回。

### 协议格式

| 字段             | 类型       | 长度 | 说明                           |
| ---------------- | ---------- | ---- | ------------------------------ |
| `magic_number`   | `uint32_t` | 4 B  | 魔数，用于校验消息             |
| `message_length` | `uint32_t` | 4 B  | 消息体序列化后的长度           |
| `sequence_id`    | `uint32_t` | 4 B  | 请求/响应关联 ID               |
| body             | bytes      | N    | `RpcRequest` 或 `RpcResponse`  |

## 核心特性

- **自定义二进制协议** —— 固定 12 字节消息头（`magic`、`message_length`、`sequence_id`），外加 4 字节长度前缀实现消息分帧，正确处理 TCP 半包与粘包。
- **非阻塞事件循环** —— Linux 使用 `epoll`（边缘触发），macOS 使用 `kqueue`；每个连接拥有独立读写缓冲区，就绪 socket 交给工作线程池处理。
- **优先级线程池** —— 支持核心/最大线程数、有界队列、`HIGH`/`NORMAL`/`LOW` 优先级、空闲线程回收与优雅关闭。
- **服务注册与发现** —— 服务端在 ZooKeeper 命名空间下创建临时节点；会话超时后节点自动摘除，客户端断线重连时重新解析可用实例；ZK 暂时不可用时保留最近一次成功获取的实例列表作为本地缓存兜底。
- **负载均衡** —— 随机、轮询、加权轮询三种策略，统一接口，可通过配置选择。
- **可插拔序列化** —— 运行时选择 JSON（`nlohmann/json`）或 Protobuf，并在编译期做类型检查。
- **压缩与加密** —— 支持可配置级别的 Zstd 压缩；每条消息生成随机 256 位会话密钥，使用主密钥封装会话密钥，再使用 **AES-256-GCM**（OpenSSL EVP）加密消息体并 Base64 编码。这是两层对称加密，不是混合加密；生产环境还应增加 **ECDH** 密钥交换、密钥轮换与重放保护。
- **连接生命周期加固** —— `Connection::state_` 原子化，`Read` / `Write` / `Close` 使用同一把互斥锁，避免“worker 正在写响应、事件循环同时关闭连接”的竞态。
- **配置化** —— 服务端、客户端、线程池与注册中心配置均在启动时加载，无需重新编译。
- **优雅关闭** —— 捕获 `SIGINT`/`SIGTERM`，停止事件循环、关闭连接、排空线程池并清理 ZooKeeper。
- **压测工具链** —— 多线程压测客户端 + 参数化脚本，支持按请求数或按时长压测，便于做日志开/关等 A/B 对照。

## 快速开始

### 环境要求

- CMake 3.15+、支持 C++17 的编译器（GCC 9+、Clang 10+）
- [vcpkg](https://github.com/microsoft/vcpkg)，需安装 `protobuf`、`nlohmann-json`、`spdlog`、`zstd`、`openssl`、`zookeeper`
- 一个带同步 API 的线程版 ZooKeeper C client（`libzookeeper_mt`）
- 可用的 ZooKeeper 服务（默认 `localhost:2181`）

> 说明：部分 vcpkg 的 `zookeeper` 包只提供单线程库，缺少 `zoo_create` / `zoo_exists` 等同步符号。此时需要自行编译带 `-DTHREADED` 的 C client，并在 CMake 中通过 `RPC_ZOOKEEPER_MT_LIB` / `RPC_ZOOKEEPER_HASHTABLE_LIB` 指定。

### 构建

```bash
export VCPKG_ROOT=/path/to/vcpkg

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DRPC_ZOOKEEPER_MT_LIB=/path/to/libzookeeper_mt.a \
  -DRPC_ZOOKEEPER_HASHTABLE_LIB=/path/to/libzookeeper_hashtable.a

cmake --build build -j"$(nproc)"
```

如果你的 vcpkg `zookeeper` 包已经提供同步 API，可以省略两个 `RPC_ZOOKEEPER_*` 参数；如果头文件不在默认路径，再追加 `-DZOOKEEPER_INCLUDE_DIR=/path/to/include`。

构建产物：

```text
build/rpc_server
build/rpc_client
build/stress_test_client
```

### 启动 ZooKeeper

```bash
zkServer.sh start
```

### 运行

```bash
# 终端 1：服务端（从 build/ 读取 ../config/rpc_server.json）
cd build && ./rpc_server

# 终端 2：示例客户端（同步 + 异步调用）
cd build && ./rpc_client
```

本地测试前，把 `config/servers.json` 里的注册节点改成 `127.0.0.1:8989`，避免使用公网或 `0.0.0.0` 地址。

### 压测

```bash
# 50 个并发客户端，每个发送 400 次请求，共 2 万请求
cd build
./stress_test_client -c 50 -n 20000 -f ../config/rpc_client.json

# 50 个并发客户端，持续压测 60 秒（默认每请求间隔 1ms）
./stress_test_client -c 50 -t 60 -f ../config/rpc_client.json

# 饱和压测：关闭请求间隔，测服务端极限吞吐
./stress_test_client -c 50 -t 60 -i 0 -f ../config/rpc_client.json
```

> 请求间隔用于控制发包节奏，避免压测客户端自己成为瓶颈；它计入 QPS 的墙钟时间，但不计入平均响应时间。`-i 0` 可关闭间隔做饱和压测。

### 日志 A/B

用两个构建目录，只切换 `SPDLOG_ACTIVE_LEVEL`，其他参数完全一致：

```bash
# 基线：逐请求日志开
cmake -S . -B build-verbose -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DRPC_ZOOKEEPER_MT_LIB=/path/to/libzookeeper_mt.a \
  -DRPC_ZOOKEEPER_HASHTABLE_LIB=/path/to/libzookeeper_hashtable.a
cmake --build build-verbose -j"$(nproc)"

# 静默：info/debug 日志编译期移除
cmake -S . -B build-quiet -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_WARN" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DRPC_ZOOKEEPER_MT_LIB=/path/to/libzookeeper_mt.a \
  -DRPC_ZOOKEEPER_HASHTABLE_LIB=/path/to/libzookeeper_hashtable.a
cmake --build build-quiet -j"$(nproc)"
```

## 性能基准

测试条件：Release 构建，50 个并发客户端，`RpcService/Echo`，JSON 序列化，Zstd 压缩 + 会话密钥加密，60 秒稳态，成功率 100%。2C4G 通过 systemd cgroup 限制为 2 核 / 4G（`CPUQuota=200%`、`MemoryMax=4G`），服务端在 cgroup 内、客户端在宿主机，避免客户端抢占服务端配额。

### 稳态吞吐与延迟分位（2C4G）

| 环境 | 请求数 | QPS | 平均 | P50 | P90 | P99 | 成功率 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 真实 2C4G（cgroup 2C/4G） | 1,120,245 | 19,444.65 | 1.51 ms | 1.46 ms | — | 4.47 ms | 100% |
| 6 核 / 7.8G WSL2 | 1,141,633 | 19,990.77 | 1.44 ms | 1.42 ms | — | 3.05 ms | 100% |

> 6 核与 2C4G 的 QPS 接近，是因为 50 个客户端、每请求 1ms 间隔把上限压在约 20k；2C4G 在无间隔饱和下同样是 20.2k，说明两种情况都已接近该配置的上限。两个环境均为 0 失败、0 响应内容校验失败。

> P50 与平均值的差距说明典型请求很快，均值主要被尾部拉高；2C4G 下的尾延迟来自 CPU 配额下的调度与线程池排队。

### 日志 A/B（6 核与 2C4G，改造前版本测得）

| 环境 | 配置 | 请求数 | QPS | 平均响应 | 成功率 |
|---|---|---:|---:|---:|---:|
| 6 核 / 7.8G WSL2 | 日志开（60s 稳态） | 986,276 | 15,768.56 | 1.90 ms | 100% |
| 6 核 / 7.8G WSL2 | 日志关（60s 稳态） | 989,897 | 15,811.54 | 1.73 ms | 100% |
| 真实 2C4G（cgroup 2C/4G） | 日志开（60s 稳态） | 774,493 | 12,287.69 | 2.81 ms | 100% |
| 真实 2C4G（cgroup 2C/4G） | 日志关（60s 稳态） | 736,286 | 11,660.80 | 2.88 ms | 100% |

相对提升：

| 环境 | QPS | 平均延迟 |
|---|---:|---:|
| 6 核（60s 稳态） | +0.3% | -9.0% |
| 真实 2C4G（60s 稳态） | -5.1% | +2.5%（噪声范围内） |

> 6 核下 QPS 受客户端 1ms 间隔限制，日志开关对 QPS 影响很小，平均延迟是更敏感的指标。2C4G 下日志开关差异落在运行噪声内，说明瓶颈是 CPU 配额而不是日志。绝对 QPS 仍会受 CPU 型号、虚拟化、磁盘和客户端部署方式影响。

### 饱和压测：瓶颈归因（改造前版本测得）

为验证 1ms 间隔是否掩盖了服务端能力，另做一组无间隔压测：

| 配置 | 请求数 | QPS | 平均 | P50 | P90 | P99 | 服务端 CPU | 客户端 CPU |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1ms 间隔 | 778,104 | 12,587.62 | 2.77 ms | 1.89 ms | 2.61 ms | 23.80 ms | — | — |
| 无间隔（饱和） | 740,467 | 11,827.98 | 4.20 ms | 3.03 ms | 4.05 ms | 28.88 ms | 1.91 / 2 核 | 1.63 / 6 核 |

> 去掉 1ms 间隔后 QPS 反而下降、延迟翻倍：服务端 CPU 已打满 2 核配额，客户端仅使用 1.63 核。结论是 2C4G 下的瓶颈在服务端 CPU，12.6k QPS 已接近该规格上限，而不是压测客户端节奏造成的虚高。

> 加密实现替换后重测（当前版本）：2C4G 无间隔饱和压测 **20,211.75 QPS / 2.45ms**，服务端 CPU 2.0/2 核；带 1ms 间隔时 19,444.65 QPS / 1.51ms。两种口径接近，说明当前版本已由服务端 CPU 决定上限。微基准显示单条消息的密钥生成从 32.7µs 降到 0.49µs，一次 RPC 的加解密开销从约 68µs 降到约 3µs。

## 工程细节

### 1. 热路径日志的代价

服务端线程池在 `enqueue` 时打印 INFO 日志，且原先位于队列锁内；客户端每收到一个响应也会打印一条 INFO。压测时这会和请求处理争抢 CPU 与 I/O。项目提供日志开/关的 A/B 构建方式，用数据而不是直觉判断日志成本。6 核 60 秒稳态下，关闭日志使平均延迟下降约 9%，QPS 基本持平；真实 2C4G 下日志开关的差异落在噪声范围内，说明 2C4G 的瓶颈是 CPU 配额而不是日志。

### 2. 连接写/关闭竞态

早期实现中，`Connection` 的 `state_`、`fd_` 和 `write_buffer_` 没有同步保护：worker 线程正在写响应时，事件循环可能关闭同一个连接，导致 `write error` / `Connection is invalid`。现在的实现将 `state_` 原子化，并用 `io_mutex_` 串行化 `Read` / `Write` / `Close`；修复后真实 2C4G、50 并发、60 秒 0 失败（112 万请求，19.4k QPS）。

### 3. 客户端重连的重复加锁

`Call` 全程持有 `mutex_`，而它调用的 `checkConnection()` 在连接失效时会走到 `Reconnect() → Disconnect()`，后者再次对同一把非递归 `std::mutex` 加锁。同一线程二次加锁属于未定义行为，glibc 下表现为永久阻塞——服务端一旦重启，客户端不是重连而是卡死。

修复方式是把加锁边界收敛到对外入口：`Connect()` / `Disconnect()` 负责加锁，内部只调用不加锁的 `ConnectLocked()` / `DisconnectLocked()`；`checkConnection()` 因为调用方已持锁，直接使用不加锁版本。同时读超时也会把连接标记为失效，使下一次调用真正触发重连。

故障注入验证：10 个并发客户端运行中 `kill -9` 服务端，40 秒后重启，客户端约 2 秒内恢复请求，程序正常退出而非卡死。

### 4. Release vs Debug

仓库原始构建脚本使用 `Debug`，压测数据会明显偏低。性能测试请使用 `Release`；同时注意 2C4G 上线程池不宜开得过大，建议从 `core_threads=2~4` 开始调优。

## 目录结构

```text
rpc_src/
  core/           服务端/客户端入口、RpcClient
  network/        epoll/kqueue、Connection、ConnectionManager
  protocol/       12B 协议头、RpcRequest / RpcResponse
  serializer/     JSON / Protobuf 适配
  compress_data/  Zstd 封装
  encrypt/        会话密钥 + 主密钥封装
  conn_balancer/  负载均衡与 ZooKeeper 处理器
  registry/       临时节点注册
  thread_pool/    优先级线程池
  load_config/    JSON 配置加载
  tools/          压测客户端
protos/           message.proto 与生成代码
config/           服务端 / 客户端 / 注册中心配置
utils/            日志与工具函数
```

## 限制与路线图

- **ZooKeeper Watch**：目前依赖临时节点 + 会话超时 + 客户端重连发现，并在 ZK 不可用时使用最近一次成功获取的实例列表兜底；后续增加 Watch 与健康检查，缩短故障摘除时间。
- **加密原语**：当前使用 AES-256-GCM（OpenSSL EVP）和演示用主密钥；生产环境应从密钥管理系统读取主密钥，并补充 ECDH 密钥交换、密钥轮换与重放保护。
- **压测指标**：客户端已输出 QPS / 平均 / P50 / P90 / P99 / 最大延迟；后续补充 CPU / 内存占用与更长时长的稳定性测试。
- **客户端间隔**：默认每请求间隔 1ms 以控制发包节奏；`-i 0` 可关闭，用于饱和吞吐测试。

## 许可证

本项目基于 [MIT License](LICENSE) 发布。
