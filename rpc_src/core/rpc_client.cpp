#include "rpc_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "log_manager.h"
#include "zk_conn_handler.h"
#include "rpc_client_config.h"
#include "zstd_compress.h"
#include "aes_encrypt.h"

namespace cookrpc
{

    RpcClient::RpcClient(const std::string &config_path)
    {
        try
        {
            // 1. 加载配置
            if (!loadConfig(config_path))
            {
                throw std::runtime_error("Failed to load RPC client config");
            }
            // 2. 初始化连接
            if (!initSocket(socket_fd_))
            {
                throw std::runtime_error("Failed to initialize socket");
            }

            // 3. 连接服务器
            if (!Connect())
            {
                throw std::runtime_error("Failed to connect to RPC server");
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("RPC client initialization failed: {}", e.what());
            Disconnect();
            throw;
        }
    }
    RpcClient::~RpcClient()
    {
        Disconnect();
    }

    bool RpcClient::initSocket(int &fd)
    {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
            LOG_ERROR("Create socket failed: {}", strerror(errno));
            return false;
        }

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        {
            LOG_ERROR("Set nonblock failed: {}", strerror(errno));
            close(fd);
            return false;
        }
        return true;
    }

    bool RpcClient::loadConfig(const std::string &conf_path)
    {
        std::string config_filename = conf_path;
        auto &rpc_client_config = RpcClientConfig::GetInstance();
        if (!rpc_client_config.InitRpcClientConfig(config_filename))
        {
            LOG_ERROR("load config failed: {}", config_filename);
            return false;
        }
        retry_times_ = rpc_client_config.GetRetryTimes();
        timeout_ms_ = rpc_client_config.GetTimeout();
        zk_namespace_ = rpc_client_config.GetZkNamespace();
        
        // 初始化ZkConnHandler
        auto &zk_conn_handler = ZkConnHandler::GetInstance();
        nlohmann::json zk_config;
        zk_config["zk_host"] = rpc_client_config.GetZkHost();
        zk_config["zk_port"] = rpc_client_config.GetZkPort();
        zk_config["zk_namespace"] = rpc_client_config.GetZkNamespace();
        zk_config["zk_retry_interval"] = 5;
        
        if (!zk_conn_handler.initZkConnHandler(zk_config))
        {
            LOG_ERROR("Failed to initialize ZkConnHandler");
            return false;
        }
        
        return true;
    }

    bool RpcClient::validateServerInfo(const std::string &ip, int port, int fd)
    {

        // 验证 IP 地址
        if (ip.empty())
        {
            LOG_ERROR("Empty IP address");
            close(fd);
            return false;
        }

        // 验证 IP 地址格式
        struct sockaddr_in sa;
        if (inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) != 1)
        {
            LOG_ERROR("Invalid IP address format: {}", ip);
            close(fd);
            return false;
        }

        // 验证端口号
        if (port <= 0 || port > 65535)
        {
            LOG_ERROR("Invalid port number: {}", port);
            close(fd);
            return false;
        }

        return true;
    }

    bool RpcClient::tryConnect(int socket_fd, const struct sockaddr_in &server_addr, int retry_count)
    {

        // 设置非阻塞模式
        int flags = fcntl(socket_fd, F_GETFL, 0);
        fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);

        // 尝试连接
        int ret = connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        if (ret == 0)
        {
            // 连接立即成功：恢复进入本函数前的 flags。
            // 注意 initSocket 已把该 fd 设为非阻塞，所以这里恢复后仍然是非阻塞。
            fcntl(socket_fd, F_SETFL, flags);
            return true;
        }

        // 检查连接错误
        if (errno != EINPROGRESS && errno != EALREADY)
        {
            LOG_ERROR("Connect failed (attempt {}/{}): {} (errno: {})", 
                     retry_count + 1, retry_times_, strerror(errno), errno);
            return false;
        }

        // 等待连接完成
        fd_set write_fds;
        struct timeval timeout;
        timeout.tv_sec = timeout_ms_ / 1000;
        timeout.tv_usec = (timeout_ms_ % 1000) * 1000;

        FD_ZERO(&write_fds);
        FD_SET(socket_fd, &write_fds);

        ret = select(socket_fd + 1, nullptr, &write_fds, nullptr, &timeout);
        if (ret <= 0)
        {
            LOG_ERROR("Connect timeout or error (attempt {}/{}): {}", 
                     retry_count + 1, retry_times_, 
                     ret == 0 ? "timeout" : strerror(errno));
            return false;
        }

        // 检查连接是否真的建立成功
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0)
        {
            LOG_ERROR("Connection failed after select (attempt {}/{}): {}", 
                     retry_count + 1, retry_times_, 
                     error == 0 ? "getsockopt failed" : strerror(error));
            return false;
        }

        // 恢复进入本函数前的 flags。
        // 注意 initSocket 已把该 fd 设为非阻塞，所以这里恢复后仍然是非阻塞；
        // 不要改成真正的阻塞模式：Connection::Read 依赖 EAGAIN 判断读完。
        fcntl(socket_fd, F_SETFL, flags);

        return true;
    }

    bool RpcClient::waitForConnection(int fd, int retry_count)
    {
        fd_set write_fds;
        struct timeval tv;
        FD_ZERO(&write_fds);
        FD_SET(fd, &write_fds);
        tv.tv_sec = timeout_ms_ / 1000;
        tv.tv_usec = (timeout_ms_ % 1000) * 1000;

        int ret = select(fd + 1, nullptr, &write_fds, nullptr, &tv);
        if (ret <= 0)
        {
            LOG_ERROR("Connect timeout or error (attempt {}/{}): {}",
                      retry_count + 1, retry_times_, strerror(errno));
            return false;
        }
        return true;
    }

    bool RpcClient::Connect()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ConnectLocked();
    }

    // 建立连接：调用方必须已持有 mutex_
    bool RpcClient::ConnectLocked()
    {
        if (is_connected_)
        {
            return true;
        }

        std::string server_ip_and_port = ZkConnHandler::GetInstance().getServer(zk_namespace_);
        if (server_ip_and_port.empty())
        {
            LOG_ERROR("fail to get server_ip");
            return false;
        }

        std::string server_ip = server_ip_and_port.substr(0, server_ip_and_port.find(":"));
        int server_port = std::stoi(server_ip_and_port.substr(server_ip_and_port.find(":") + 1));

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());
        server_addr.sin_port = htons(server_port);

        int retry_count = 0;
        // 重试连接
        while (retry_count < retry_times_)
        {
            // CLIENT_INFO("try connect, server_ip: {}, server_port: {}, retry_count: {}", 
            //             server_ip, server_port, retry_count);
            
            // 每次重试都创建新的socket
            int socket_fd;
            if (!initSocket(socket_fd))
            {
                LOG_ERROR("Init socket failed");
                retry_count++;
                continue;
            }

    
            if (!validateServerInfo(server_ip, server_port, socket_fd))
            {
                close(socket_fd);
                LOG_ERROR("Validate server info failed");
                retry_count++;
                continue;
            }

            if (tryConnect(socket_fd, server_addr, retry_count))
            {
                connection_ = std::make_shared<Connection>(socket_fd);
                is_connected_ = true;
                // CLIENT_INFO("connect success, fd: {}", socket_fd);
                return true;
            }
            
            // 连接失败，关闭socket
            close(socket_fd);
            retry_count++;
        }

        return false;
    }

    void RpcClient::Disconnect()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        DisconnectLocked();
    }

    // 断开连接：调用方必须已持有 mutex_
    void RpcClient::DisconnectLocked()
    {
        if (connection_)
        {
            connection_->Close();
            connection_.reset();
        }
        is_connected_ = false;
    }

    uint64_t RpcClient::GenerateSequenceId()
    {
        return ++sequence_id_;
    }

    // 调用方（Call）已持有 mutex_，这里直接使用不加锁版本
    bool RpcClient::checkConnection()
    {
        if (!is_connected_)
        {
            DisconnectLocked();
            if (!ConnectLocked())
            {
                LOG_ERROR("Not connected to server");
                return false;
            }
        }
        return true;
    }

    bool RpcClient::sendAndReceiveResponse(const std::string &encrypted_data)
    {
        // CLIENT_INFO("Sending encrypted data, size: {}, fd: {}", 
        //         encrypted_data.size(), connection_->GetFd());
        
        if (!connection_->Write(framing::Encode(encrypted_data)))
        {
            LOG_ERROR("Failed to send request");
            is_connected_ = false;
            return false;
        }

        // CLIENT_INFO("send request success, fd: {}", connection_->GetFd());
        return true;
    }

} // namespace cookrpc
