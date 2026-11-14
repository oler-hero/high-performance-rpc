#include "message_cycle.h"
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include "zstd_compress.h"
#include "aes_encrypt.h"
#include "thread_pool.h"

namespace cookrpc
{

    // 全局原子停止标志：用于跨线程/信号处理函数通知事件循环退出
    // 使用 atomic 保证多线程读写时的可见性和原子性
    static std::atomic<bool> stop_flag(false);

    // 信号处理函数：必须在命名空间外部声明为 static，避免外部链接
    // 注意：信号处理函数中只能调用"异步信号安全"的函数，
    // 这里仅设置一个原子标志，是安全的做法
    static void signalHandler(int signum)
    {
        stop_flag = true;
    }

    // 构造函数：初始化事件循环所需的底层资源（kqueue 或 epoll）
    MessageCycle::MessageCycle(ConnectionManager *manager)
        : running_(true), connection_manager_(manager)
    {
#ifdef __APPLE__
        // macOS 平台：使用 kqueue 作为 I/O 多路复用机制
        kqueue_fd_ = kqueue();
        if (kqueue_fd_ == -1)
        {
            LOG_ERROR("Failed to create kqueue: {} (errno: {})", strerror(errno), errno);
            throw std::runtime_error("Failed to create kqueue");
        }
#else
        // Linux 平台：使用 epoll 作为 I/O 多路复用机制
        // epoll_create1(0) 表示不使用 EPOLL_CLOEXEC 标志（若需要防止 fork 后泄露可加）
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ == -1)
        {
            LOG_ERROR("Failed to create epoll: {} (errno: {})", strerror(errno), errno);
            throw std::runtime_error("Failed to create epoll");
        }
#endif

        // 注册 SIGINT 信号处理函数（Ctrl+C 时优雅退出）
        if (::signal(SIGINT, signalHandler) == SIG_ERR)
        {
            LOG_ERROR("Failed to register signal handler");
            throw std::runtime_error("Signal handler registration failed");
        }
    }

#ifdef __APPLE__
    // 处理 kqueue 返回的事件数组
    void MessageCycle::HandleKqueueEvents(struct kevent *events, int nfds)
    {
        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].ident; // kqueue 中 ident 即文件描述符

            // 快速校验 fd 合法性，避免处理已关闭的 fd
            if (fd < 0 || !IsValidFileDescriptor(fd))
            {
                continue; // 跳过无效的文件描述符
            }

            // 处理 kqueue 事件错误
            if (events[i].flags & EV_ERROR)
            {
                LOG_ERROR("event error on fd: {}", fd);
                RemoveConnection(fd); // 移除无效的连接
                continue;
            }

            // 这样写是为了限制 lock 的生命周期，提高并发性能
            // 只在访问 listen_fds_ 时加锁，避免长时间持锁
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (listen_fds_.find(fd) != listen_fds_.end())
                {
                    // 该 fd 是监听 socket，处理新连接
                    HandleNewConnection(fd);
                    continue;
                }
            }

            // 检查连接是否存在（防止处理已失效的 fd）
            auto conn = connection_manager_->GetConnection(fd);
            if (!conn)
            {
                LOG_ERROR("Connection not found for fd handle kqueue events: {}", fd);
                RemoveConnection(fd);
                continue;
            }

            // 处理客户端可读数据（kqueue 使用 filter 区分事件类型）
            if (events[i].filter == EVFILT_READ)
            {
                HandleClientData(fd);
            }
        }
    }
#else
    // 处理 epoll 返回的事件数组
    void MessageCycle::HandleEpollEvents(struct epoll_event *events, int nfds)
    {
        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd; // epoll 事件中的 fd 存储在 data 联合体中

            // 处理错误/挂断事件，先关闭连接
            if (events[i].events & (EPOLLERR | EPOLLHUP))
            {
                LOG_ERROR("event error on fd: {}", fd);
                RemoveConnection(fd);
                continue;
            }

            // 使用 listen_fds_ 集合判断是否为监听 socket
            // 同样使用局部作用域限制锁的生命周期
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (listen_fds_.find(fd) != listen_fds_.end())
                {
                    // 该 fd 是监听 socket，处理新连接
                    HandleNewConnection(fd);
                    continue;
                }
            }

            // 处理客户端可读数据（EPOLLIN 表示有数据可读）
            if (events[i].events & EPOLLIN)
            {
                HandleClientData(fd);
            }
        }
    }
#endif

    // 添加监听 socket 到事件循环
    bool MessageCycle::AddListenFd(int fd)
    {
        if (fd < 0)
        {
            LOG_ERROR("add listen fd is error");
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

#ifdef __APPLE__
        // kqueue：注册 EVFILT_READ 事件
        struct kevent kev;
        EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        if (kevent(kqueue_fd_, &kev, 1, nullptr, 0, nullptr) == -1)
        {
            LOG_ERROR("Failed to add fd {} to kqueue: {}", fd, strerror(errno));
            return false;
        }
#else
        // epoll：注册 EPOLLIN 事件（水平触发，监听 socket 不需要边沿触发）
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1)
        {
            LOG_ERROR("Failed to add fd {} to epoll: {}", fd, strerror(errno));
            return false;
        }
#endif

        // 记录到监听 fd 集合中，用于后续快速判定
        listen_fds_.insert(fd);
        return true;
    }

    // 从事件循环中移除监听 socket
    bool MessageCycle::RemoveListenFd(int fd)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 若 fd 不在集合中，说明没有注册过
        if (listen_fds_.find(fd) == listen_fds_.end())
        {
            LOG_ERROR("File descriptor {} not found remove listen fd", fd);
            return false;
        }

#ifdef __APPLE__
        // kqueue：删除注册的 EVFILT_READ 事件
        struct kevent kev;
        EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        if (kevent(kqueue_fd_, &kev, 1, nullptr, 0, nullptr) == -1)
        {
            LOG_ERROR("Failed to remove fd {} from kqueue: {}", fd, strerror(errno));
        }
#else
        // epoll：删除注册的事件
        struct epoll_event ev;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1)
        {
            LOG_ERROR("Failed to remove fd {} from epoll: {}", fd, strerror(errno));
        }
#endif

        listen_fds_.erase(fd);
        return true;
    }

    // 返回当前监听 fd 的数量（加锁保证线程安全）
    size_t MessageCycle::GetListenFdCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return listen_fds_.size();
    }

    // 析构函数：释放所有资源，包括监听 fd、事件循环 fd
    MessageCycle::~MessageCycle()
    {
        // 先通知所有相关线程停止运行
        running_ = false;
        stop_flag = true;

        try {
            std::lock_guard<std::mutex> lock(mutex_);

            // 清理监听文件描述符
            for (int fd : listen_fds_)
            {
                try {
#ifdef __APPLE__
                    // 从 kqueue 中删除
                    struct kevent kev;
                    EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
                    kevent(kqueue_fd_, &kev, 1, nullptr, 0, nullptr);
#else
                    // 从 epoll 中删除
                    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
#endif
                    // 关闭监听 socket
                    close(fd);
                    LOG_DEBUG("Closed listen fd: {}", fd);
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception closing listen fd {}: {}", fd, e.what());
                } catch (...) {
                    LOG_ERROR("Unknown exception closing listen fd: {}", fd);
                }
            }
            listen_fds_.clear();

            // 清理事件循环文件描述符（kqueue_fd_ 或 epoll_fd_）
#ifdef __APPLE__
            if (kqueue_fd_ >= 0)
            {
                close(kqueue_fd_);
                kqueue_fd_ = -1;
                LOG_DEBUG("Closed kqueue fd");
            }
#else
            if (epoll_fd_ >= 0)
            {
                close(epoll_fd_);
                epoll_fd_ = -1;
                LOG_DEBUG("Closed epoll fd");
            }
#endif
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in MessageCycle destructor: {}", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception in MessageCycle destructor");
        }
    }

    // 处理新的客户端连接：accept + 初始化 Connection 对象
    void MessageCycle::HandleNewConnection(int listen_fd)
    {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        // 接受新连接（注意：listen_fd 已设置为非阻塞时 accept 可能返回 EAGAIN）
        int client_fd = ::accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0)
        {
            LOG_ERROR("Accept failed, errno: {}, error: {}", errno, strerror(errno));
            return;
        }

        // LOG_INFO("New connection from {}:{}, fd: {}",
        //          inet_ntoa(client_addr.sin_addr),
        //          ntohs(client_addr.sin_port),
        //          client_fd);

        try
        {
            // 设置非阻塞：读操作返回 EAGAIN 时交由事件循环继续等待
            int flags = fcntl(client_fd, F_GETFL, 0);
            if (flags < 0 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0)
            {
                throw std::runtime_error("Set nonblock failed");
            }

            // 设置 TCP_NODELAY：禁用 Nagle 算法，降低 RPC 小包延迟
            int optval = 1;
            if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) < 0)
            {
                throw std::runtime_error("Set TCP_NODELAY failed");
            }

            // 创建连接对象（智能指针管理生命周期）
            auto conn = std::make_shared<Connection>(client_fd);

            // 设置消息回调：收到完整 RPC 请求时交由 HandleRpcRequest 处理
            conn->SetMessageCallback(
                [this](const std::shared_ptr<Connection> &conn, const RpcRequest &request)
                {
                    HandleRpcRequest(conn, request);
                });

            // 设置关闭回调：连接关闭时移除连接
            conn->SetCloseCallback(
                [this](const std::shared_ptr<Connection> &conn)
                {
                    RemoveConnection(conn->GetFd());
                });

            // 添加到连接管理器（由管理器统一管理连接生命周期）
            connection_manager_->AddConnection(conn);

            // 注册到事件循环（kqueue 或 epoll）
#ifdef __APPLE__
            struct kevent kev;
            EV_SET(&kev, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
            if (kevent(kqueue_fd_, &kev, 1, nullptr, 0, nullptr) == -1)
            {
                throw std::runtime_error("Failed to add to kqueue");
            }
#else
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET; // 使用边缘触发：减少 epoll_wait 唤醒次数，提高性能
            ev.data.fd = client_fd;
            if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev) == -1)
            {
                throw std::runtime_error("Failed to add to epoll");
            }
#endif

            // LOG_INFO("Successfully added new connection, fd: {}", client_fd);
        }
        catch (const std::exception &e)
        {
            // 初始化过程中出错，需回滚并释放已分配的资源
            LOG_ERROR("Failed to handle new connection: {}, fd: {}", e.what(), client_fd);
            RemoveConnection(client_fd);
        }
    }

    // 处理 RPC 请求的总入口（当前默认走异步路径）
    void MessageCycle::HandleRpcRequest(const std::shared_ptr<Connection> &conn,
                                        const RpcRequest &request)
    {
        // 默认使用异步处理来提高性能
        HandleRpcRequestAsync(conn, request);
    }

    // 将 RPC 请求提交到线程池异步执行
    void MessageCycle::HandleRpcRequestAsync(const std::shared_ptr<Connection> &conn,
                                             const RpcRequest &request)
    {
        // 确保线程池已初始化（懒加载 + 单例）
        if (!cookrpc::threadpool::ThreadPoolSingleton::exists()) {
            cookrpc::threadpool::ThreadPoolSingleton::init(std::thread::hardware_concurrency());
            LOG_INFO("ThreadPool initialized in MessageCycle with {} threads",
                     std::thread::hardware_concurrency());
        }

        // 使用高优先级异步处理 RPC 请求
        auto future = cookrpc::threadpool::ThreadPoolSingleton::getInstance().enqueue(
            cookrpc::threadpool::TaskPriority::HIGH,
            [this, conn, request]() {
                this->HandleRpcRequestSync(conn, request);
            }
        );

        // 检查任务是否成功提交（future 无效通常意味着线程池已停止）
        if (!future.valid()) {
            LOG_ERROR("Failed to enqueue RPC request - ThreadPool may be stopped: service={}, method={}, sequence={}",
                      request.getServiceName(), request.getMethodName(), request.getSequenceId());

            // 线程池不可用时，直接发送错误响应给客户端（降级处理）
            SendErrorResponse(conn, request.getSequenceId(),
                              ErrorCode::INTERNAL_ERROR,
                              "Service temporarily unavailable");
            return;
        }

        LOG_DEBUG("RPC request enqueued for async processing: service={}, method={}, sequence={}",
                  request.getServiceName(), request.getMethodName(), request.getSequenceId());
    }

    // 在工作线程中同步执行 RPC 请求的实际处理逻辑
    void MessageCycle::HandleRpcRequestSync(const std::shared_ptr<Connection> &conn,
                                            const RpcRequest &request)
    {
        RpcResponse response;
        response.setSequenceId(request.getSequenceId()); // 设置相同的序列号，用于客户端匹配请求

        try
        {
            // 参数验证：避免无效请求进入业务逻辑
            if (!ValidateRequest(request))
            {
                SendErrorResponse(conn, request.getSequenceId(),
                                  ErrorCode::INVALID_REQUEST,
                                  Error::getErrorMessage(ErrorCode::INVALID_REQUEST));
                return;
            }

            // 使用 ServiceManager 的同步处理方法，避免双重异步
            std::string result;
            bool success = ServiceManager::GetInstance().HandleRpcRequest(
                request.getServiceName(),
                request.getMethodName(),
                request.getPayload(),
                result
            );

            // 服务端业务处理失败
            if (!success) {
                LOG_ERROR("Failed to handle request: method={}", request.getMethodName());
                SendErrorResponse(conn, request.getSequenceId(),
                                  ErrorCode::SERVICE_NOT_FOUND,
                                  Error::getErrorMessage(ErrorCode::SERVICE_NOT_FOUND));
                return;
            }

            // 发送成功响应
            SendSuccessResponse(conn, request.getSequenceId(), result);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception while handling RPC request: {}", e.what());
            SendErrorResponse(conn, request.getSequenceId(),
                              ErrorCode::INTERNAL_ERROR,
                              Error::getErrorMessage(ErrorCode::INTERNAL_ERROR));
        }
        catch (...)
        {
            // 兜底：防止非 std::exception 派生异常导致线程异常终止
            LOG_ERROR("Unknown exception while handling RPC request");
            SendErrorResponse(conn, request.getSequenceId(),
                              ErrorCode::INTERNAL_ERROR,
                              Error::getErrorMessage(ErrorCode::INTERNAL_ERROR));
        }
    }

    // 请求合法性校验：至少要包含服务名、方法名、有效序列号
    bool MessageCycle::ValidateRequest(const RpcRequest &request)
    {
        return !request.getServiceName().empty() &&
               !request.getMethodName().empty() &&
               request.getSequenceId() > 0;
    }

    // 构造并发送错误响应
    void MessageCycle::SendErrorResponse(const std::shared_ptr<Connection> &conn,
                                         uint64_t sequence_id,
                                         ErrorCode error_code,
                                         const std::string &error_message)
    {
        RpcResponse response;
        response.setSequenceId(sequence_id);
        response.setErrorCode(static_cast<uint32_t>(error_code));
        response.setErrorMessage(error_message);

        SendResponse(conn, response, "error");
    }

    // 构造并发送成功响应
    void MessageCycle::SendSuccessResponse(const std::shared_ptr<Connection> &conn,
                                           uint64_t sequence_id,
                                           const std::string &result)
    {
        RpcResponse response;
        response.setSequenceId(sequence_id);
        response.setErrorCode(static_cast<uint32_t>(cookrpc::ErrorCode::SUCCESS));
        response.setErrorMessage(cookrpc::Error::getErrorMessage(cookrpc::ErrorCode::SUCCESS));
        response.setResultData(result);

        SendResponse(conn, response, "success");
    }

    // 统一的响应发送流程：序列化 -> 压缩 -> 加密 -> 写回客户端
    void MessageCycle::SendResponse(const std::shared_ptr<Connection> &conn,
                                    const RpcResponse &response,
                                    const std::string &response_type)
    {
        // 序列化响应（protobuf 等）
        std::string serialized_data;
        if (!response.Serialize(serialized_data))
        {
            LOG_ERROR("Failed to serialize {} response", response_type);
            return;
        }

        // 压缩数据：减少网络带宽占用
        std::string compressed_data;
        if (!ZstdCompress::getInstance().CompressString(serialized_data, compressed_data))
        {
            LOG_ERROR("Failed to compress {} response", response_type);
            return;
        }

        // 加密数据：保证传输安全
        std::string encrypted_data;
        if (!AesEncrypt::getInstance().Encrypt(compressed_data, encrypted_data))
        {
            LOG_ERROR("Failed to encrypt {} response", response_type);
            return;
        }

        // 通过 Connection 写回客户端
        if (!conn->Write(encrypted_data))
        {
            LOG_ERROR("Failed to send {} response: sequence_id={}", response_type, response.getSequenceId());
        }
    }

    // 处理客户端可读数据：读 -> 解包 -> 分发消息
    void MessageCycle::HandleClientData(int fd)
    {
        // 从连接管理器获取连接对象
        auto conn = connection_manager_->GetConnection(fd);
        if (!conn)
        {
            LOG_ERROR("Connection not found for fd handle client data: {}", fd);
            RemoveConnection(fd);
            return;
        }

        // 检查连接是否有效
        if (!conn->IsValid())
        {
            LOG_ERROR("Connection is invalid, fd: {}, state: {}", fd, static_cast<int>(conn->GetState()));
            RemoveConnection(fd);
            return;
        }

        // 读取数据（可能返回 false 表示连接断开或 EAGAIN）
        if (!conn->Read())
        {
            // LOG_ERROR("Failed to read data from fd: {}", fd);
            RemoveConnection(fd);
            return;
        }

        // 处理读取到的数据（粘包/拆包处理，最终回调 MessageCallback）
        if (!conn->ProcessMessage())
        {
            LOG_ERROR("Failed to process message from fd: {}", fd);
            RemoveConnection(fd);
            return;
        }
    }

    // 从事件循环和连接管理器中彻底移除一个连接
    void MessageCycle::RemoveConnection(int fd)
    {
        // 首先从 epoll/kqueue 中移除文件描述符
        RemoveInvalidFileDescriptor(fd);

        // 然后从连接管理器中移除连接（触发 Connection 析构，关闭 fd）
        connection_manager_->RemoveConnection(fd);
    }

    // 主事件循环：驱动整个 RPC 服务端的事件分发
    void MessageCycle::Loop()
    {
        LOG_INFO("Message cycle loop started");
        running_ = true; // 确保循环开始时 running_ 为 true

        const int MAX_EVENTS = 1024; // 一次最多处理的事件数量
        while (running_ && !stop_flag.load())  // 检查两个停止条件
        {
#ifdef __APPLE__
            struct kevent events[MAX_EVENTS];
            struct timespec timeout = {1, 0}; // 1秒超时：定期检查停止标志

            int nev = kevent(kqueue_fd_, nullptr, 0, events, MAX_EVENTS, &timeout);
            if (nev < 0)
            {
                // EINTR 表示被信号中断，属于正常情况
                if (errno == EINTR || stop_flag.load())
                {
                    // LOG_INFO("Message cycle interrupted by signal");
                    break;
                }
                LOG_ERROR("kevent error: {} (errno: {})", strerror(errno), errno);
                break;
            }

            if (nev > 0 && !stop_flag.load())  // 只有在没有停止信号时才处理事件
            {
                HandleKqueueEvents(events, nev);
            }
#else
            struct epoll_event events[MAX_EVENTS];
            int nev = epoll_wait(epoll_fd_, events, MAX_EVENTS, 1000); // 1秒超时

            if (nev < 0)
            {
                if (errno == EINTR || stop_flag.load())  // 信号中断时也退出
                {
                    // LOG_INFO("Message cycle interrupted by signal");
                    break;
                }
                LOG_ERROR("epoll_wait error: {} (errno: {})", strerror(errno), errno);
                break;
            }

            if (nev > 0 && !stop_flag.load())  // 只有在没有停止信号时才处理事件
            {
                HandleEpollEvents(events, nev);
            }
#endif

            // 定期检查停止标志（防御式编程：万一事件处理过程中置位了停止标志）
            if (stop_flag.load())
            {
                // LOG_INFO("Stop flag detected, exiting message loop");
                break;
            }
        }

        running_ = false;
        // LOG_INFO("Message cycle loop ended");
    }

    // 判断给定的 fd 是否为有效的监听或客户端连接
    bool MessageCycle::IsValidFileDescriptor(int fd)
    {
        if (fd < 0)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // 检查是否是监听 socket
        if (listen_fds_.find(fd) != listen_fds_.end())
        {
            return true;
        }

        // 检查是否是有效的客户端连接
        if (connection_manager_->GetConnection(fd))
        {
            return true;
        }

        LOG_ERROR("fd {} is neither in listen_fds_ nor in connections", fd);
        return false;
    }

    // 仅从事件循环中注销 fd（不关闭 fd，由连接管理器负责）
    void MessageCycle::RemoveInvalidFileDescriptor(int fd)
    {
        std::lock_guard<std::mutex> lock(mutex_);

#ifdef __APPLE__
        struct kevent kev;
        EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        kevent(kqueue_fd_, &kev, 1, nullptr, 0, nullptr);
#else
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
#endif
    }

    // 手动触发停止事件循环（例如外部管理线程调用）
    void MessageCycle::Stop()
    {
        // LOG_INFO("Stop method called, setting flags to stop message cycle");
        running_ = false;
        stop_flag = true;
    }

}