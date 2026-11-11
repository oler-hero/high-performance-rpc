// ============================================================================
// 文件名: create_socket.cpp
// 描述: 实现 CreateSocket 类，用于创建并配置 TCP 服务器监听 socket。
//       支持设置超时、地址重用、非阻塞、TCP_NODELAY 及 keepalive 等选项。
//       同时处理 macOS 平台的特殊常量。
// ============================================================================

#include "create_socket.h"
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// 针对 macOS 的特殊处理
#ifdef __APPLE__
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
// macOS 下某些常量可能不同，此处补充定义
#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif
#endif

namespace cookrpc
{

    // ========================================================================
    // 静态工厂方法：使用基本参数创建 CreateSocket 实例
    // 参数：
    //   servers_name_prefix  - 服务器名称前缀（仅用于标识，未在本实现中使用）
    //   servers_port         - 绑定的端口号
    //   servers_max_connections - 监听队列最大长度（backlog）
    //   socket_timeout_ms    - 收发超时时间（毫秒）
    //   servers_ip           - 绑定的 IP 地址（空字符串表示 INADDR_ANY）
    // 返回：
    //   成功返回 shared_ptr，失败返回 nullptr
    // ========================================================================
    std::shared_ptr<CreateSocket> CreateSocket::Create(const std::string &servers_name_prefix,
                                                       uint16_t servers_port,
                                                       int servers_max_connections,
                                                       int socket_timeout_ms,
                                                       const std::string &servers_ip)
    {
        auto socket_ptr = std::make_shared<CreateSocket>(
            servers_name_prefix, servers_port, servers_max_connections, socket_timeout_ms, servers_ip);
        if (!socket_ptr->Init())
        {
            return nullptr;
        }
        return socket_ptr;
    }

    // ========================================================================
    // 静态工厂方法：使用配置对象创建 CreateSocket 实例
    // 参数：
    //   config - RpcServerConfig 配置对象
    // 返回：
    //   成功返回 shared_ptr，失败返回 nullptr
    // ========================================================================
    std::shared_ptr<CreateSocket> CreateSocket::Create(const cookrpc::RpcServerConfig &config)
    {
        auto socket_ptr = std::make_shared<CreateSocket>(config);
        if (!socket_ptr->Init())
        {
            return nullptr;
        }
        return socket_ptr;
    }

    // ========================================================================
    // 构造函数（基本参数）
    // ========================================================================
    CreateSocket::CreateSocket(const std::string &servers_name_prefix, uint16_t servers_port, int servers_max_connections, int socket_timeout_ms, const std::string &servers_ip)
        : fd_(-1),
          is_init_(false),
          servers_name_prefix_(servers_name_prefix),
          servers_ip_(servers_ip),
          servers_port_(servers_port),
          servers_max_connections_(servers_max_connections),
          socket_timeout_ms_(socket_timeout_ms)
    {
    }

    // ========================================================================
    // 构造函数（配置对象）
    // ========================================================================
    CreateSocket::CreateSocket(const cookrpc::RpcServerConfig &config)
        : fd_(-1),
          is_init_(false),
          servers_name_prefix_(config.GetServersNamePrefix()),
          servers_ip_(config.GetServersIp()),
          servers_port_(config.GetServersPort()),
          servers_max_connections_(config.GetMaxConnections()),
          socket_timeout_ms_(config.GetTimeout())
    {
    }

    // ========================================================================
    // 析构函数：关闭 socket 描述符
    // ========================================================================
    CreateSocket::~CreateSocket()
    {
        if (fd_ > 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }

    // ========================================================================
    // 初始化方法：创建 socket、设置选项、绑定地址、开始监听
    // 返回：成功 true，失败 false
    // ========================================================================
    bool CreateSocket::Init()
    {
        // 1. 创建 TCP socket
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0)
        {
            LOG_ERROR("create socket error, errno: %d, error: %s", errno, strerror(errno));
            return false;
        }

        // 2. 设置各种 socket 选项（超时、重用、非阻塞、NODELAY、keepalive）
        if (!SetSocketOpt())
        {
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // 3. 准备 sockaddr_in 结构
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(servers_port_);

        // 如果未指定 IP，则绑定到所有接口 (INADDR_ANY)
        if (servers_ip_.empty())
        {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        }
        else
        {
            // 否则转换 IPv4 字符串地址
            if (inet_pton(AF_INET, servers_ip_.c_str(), &addr.sin_addr) <= 0)
            {
                LOG_ERROR("invalid ip address: %s", servers_ip_.c_str());
                ::close(fd_);
                fd_ = -1;
                return false;
            }
        }

        // 4. 绑定地址和端口
        if (::bind(fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            LOG_ERROR("bind error, ip: {}, port: {}, errno: {}, error: {}",
                      servers_ip_.c_str(), servers_port_, errno, strerror(errno));
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // 5. 开始监听，最大连接数由 servers_max_connections_ 指定
        if (::listen(fd_, servers_max_connections_) < 0)
        {
            LOG_ERROR("listen error, errno: %d, error: %s", errno, strerror(errno));
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        is_init_ = true;
        return true;
    }

    // ========================================================================
    // 设置 socket 选项：超时、地址重用、TCP_NODELAY、非阻塞、keepalive
    // 返回：全部成功 true，任一失败 false
    // ========================================================================
    bool CreateSocket::SetSocketOpt()
    {
        // 设置发送超时（SO_SNDTIMEO）
        struct timeval send_timeout;
        send_timeout.tv_sec = socket_timeout_ms_ / 1000;           // 转换为秒
        send_timeout.tv_usec = (socket_timeout_ms_ % 1000) * 1000; // 剩余毫秒转换为微秒
        if (setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout)) < 0)
        {
            LOG_ERROR("set send timeout error, errno: {}, error: {}", errno, strerror(errno));
            return false;
        }

        // 设置接收超时（SO_RCVTIMEO）
        struct timeval recv_timeout;
        recv_timeout.tv_sec = socket_timeout_ms_ / 1000;
        recv_timeout.tv_usec = (socket_timeout_ms_ % 1000) * 1000;
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout)) < 0)
        {
            LOG_ERROR("set recv timeout error, errno: {}, error: {}", errno, strerror(errno));
            return false;
        }

        // 设置地址重用（SO_REUSEADDR），允许快速重启服务器
        int reuse = 1;
        if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        {
            LOG_ERROR("set SO_REUSEADDR error, errno: %d, error: %s", errno, strerror(errno));
            return false;
        }

#ifdef __APPLE__
        // macOS 特定：设置端口重用（SO_REUSEPORT），允许同一端口多个进程绑定
        if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0)
        {
            LOG_ERROR("set SO_REUSEPORT error, errno: %d, error: %s", errno, strerror(errno));
            return false;
        }
#endif

        // 设置 TCP_NODELAY，禁用 Nagle 算法，减少延迟
        int nodelay = 1;
        if (::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0)
        {
            LOG_ERROR("set TCP_NODELAY error, errno: %d, error: %s", errno, strerror(errno));
            return false;
        }

        // 获取当前文件描述符状态标志
        int flags = ::fcntl(fd_, F_GETFL, 0);
        if (flags < 0)
        {
            LOG_ERROR("get socket flags error, errno: %d, error: %s", errno, strerror(errno));
            return false;
        }

        // 设置为非阻塞模式（O_NONBLOCK），使得 accept/recv/send 不会阻塞
        if (::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            LOG_ERROR("set socket nonblock error, errno: %d, error: %s", errno, strerror(errno));
            return false;
        }

        // 启用 TCP keepalive 探测，检测死连接
        int keepalive = 1;
        if (::setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) < 0)
        {
            LOG_ERROR("set SO_KEEPALIVE error, errno: %d, error: %s", errno, strerror(errno));
            return false;
        }

        return true;
    }

    // ========================================================================
    // 接受一个新的客户端连接（非阻塞）
    // 返回：成功返回客户端 socket fd，失败或无连接返回 -1
    // 注意：在非阻塞模式下，若无连接会立即返回 -1 且 errno 为 EAGAIN/EWOULDBLOCK
    // ========================================================================
    int CreateSocket::Accept()
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        // 接受新连接
        int client_fd = ::accept(fd_, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return -1; // 非阻塞模式下正常返回
            }
            LOG_ERROR("accept error, errno: {}, error: {}", errno, strerror(errno));
            return -1;
        }

        // 将新连接的 socket 也设为非阻塞
        int flags = fcntl(client_fd, F_GETFL, 0);
        if (flags < 0 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            LOG_ERROR("set nonblock failed, fd: {}", client_fd);
            close(client_fd);
            return -1;
        }

        // 可选：记录客户端 IP 地址（此处仅用于示例，未使用）
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

        return client_fd;
    }
}