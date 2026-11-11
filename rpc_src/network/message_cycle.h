#pragma once

// ============================================================================
// 消息循环类 (MessageCycle)
// ----------------------------------------------------------------------------
// 功能说明：
//   本类是 RPC 服务端的核心事件循环，负责监听网络事件、接受客户端连接、
//   读取客户端数据、解析 RPC 请求、分发到服务处理并回写响应。
//   底层使用了平台相关的 I/O 多路复用机制：
//     - macOS  : kqueue
//     - Linux  : epoll
// ============================================================================

#include <set>
#include <mutex>
#include <atomic>
#include <pthread.h>
#include <unordered_map>
#include <future>

#include "connection_manager.h"
#include "connection.h"
#include "rpc_protocol.h"
#include "error_code.h"
#include "log_manager.h"
#include "service_manager.h"
#include "error_code.h"        // 注意：重复包含，建议清理
#include "rpc_protocol.h"      // 注意：重复包含，建议清理
#include "thread_pool_singleton.h"

#include <netinet/tcp.h>

// 系统相关头文件（区分平台）
#ifdef __APPLE__
#include <sys/event.h>   // kqueue
#include <sys/time.h>
#include <sys/types.h>
#else
#include <sys/epoll.h>   // epoll
#endif

namespace cookrpc
{

class MessageCycle
{
public:
    // 构造函数：需要传入一个连接管理器，用于统一管理所有客户端连接
    MessageCycle(ConnectionManager *manager);

    // 虚析构函数：保证继承时可正确释放资源
    virtual ~MessageCycle();

    // 添加监听的文件描述符（通常是监听的 socket fd）
    bool AddListenFd(int fd);

    // 移除监听的文件描述符
    bool RemoveListenFd(int fd);

    // 获取当前监听的文件描述符数量（线程安全）
    size_t GetListenFdCount() const;

    // 启动事件循环（阻塞式），一般运行在独立线程中
    void Loop();

    // 停止事件循环，退出 Loop
    void Stop();

    // 处理 RPC 请求（同步包装入口，内部可能转异步）
    void HandleRpcRequest(const std::shared_ptr<Connection> &conn,
                          const RpcRequest &request);

    // 异步处理 RPC 请求（提交到线程池执行，避免阻塞事件循环）
    void HandleRpcRequestAsync(const std::shared_ptr<Connection> &conn,
                               const RpcRequest &request);

private:
    pthread_t thread_id_;             // 事件循环所在线程 ID
    std::set<int> listen_fds_;        // 当前监听的所有 fd 集合
    std::atomic<bool> running_;       // 事件循环运行标志（原子，用于跨线程停止）
    mutable std::mutex mutex_;        // 保护 listen_fds_ 等共享数据的互斥锁

    // 平台相关的事件循环文件描述符
#ifdef __APPLE__
    int kqueue_fd_;                   // macOS 下 kqueue 的 fd
#else
    int epoll_fd_;                    // Linux 下 epoll 的 fd
#endif

    // 平台相关的事件处理函数：遍历就绪事件并分发处理
#ifdef __APPLE__
    void HandleKqueueEvents(struct kevent *events, int nfds);
#else
    void HandleEpollEvents(struct epoll_event *events, int nfds);
#endif

    // 处理新客户端连接（accept + 注册到事件循环 + 加入 connection_manager）
    void HandleNewConnection(int fd);

    // 处理某个客户端 fd 上的可读数据（读 + 解析 + 分发）
    void HandleClientData(int fd);

    // 移除一个已断开的连接（清理事件注册 + connection_manager）
    void RemoveConnection(int fd);

    ConnectionManager *connection_manager_;       // 连接管理器（不拥有所有权）
    static const int MAX_EVENTS = 1024;           // 单次 epoll/kqueue 返回的最大事件数

    // 从事件循环中移除一个已失效的 fd
    void RemoveInvalidFileDescriptor(int fd);

    // 判断给定的 fd 是否仍有效
    bool IsValidFileDescriptor(int fd);

    // 校验 RPC 请求的合法性（如序号、方法名、序列化格式等）
    bool ValidateRequest(const RpcRequest &request);

    // 发送成功响应：多封装一层，提升代码可读性与复用性
    void SendSuccessResponse(const std::shared_ptr<Connection> &conn,
                             uint64_t sequence_id,
                             const std::string &result);

    // 发送错误响应：统一错误码 + 错误消息格式
    void SendErrorResponse(const std::shared_ptr<Connection> &conn,
                           uint64_t sequence_id,
                           cookrpc::ErrorCode error_code,
                           const std::string &error_message);

    // 通用响应发送方法（内部使用）：由上面两个方法调用
    // response_type 用于日志/调试中标识响应类型
    void SendResponse(const std::shared_ptr<Connection> &conn,
                      const RpcResponse &response,
                      const std::string &response_type);

    // 内部同步处理 RPC 请求：真正执行业务方法并返回结果
    void HandleRpcRequestSync(const std::shared_ptr<Connection> &conn,
                              const RpcRequest &request);
};

} // namespace cookrpc