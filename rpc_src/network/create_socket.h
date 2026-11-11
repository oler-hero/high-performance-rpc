#pragma once

#include "utils/log_manager.h"
#include "load_config/rpc_server_config.h"
#include <memory>
#include <string>
#include <netinet/tcp.h>

namespace cookrpc
{

    /**
     * @brief 创建并管理服务器监听套接字的类
     * 
     * 该类封装了 TCP 监听套接字的创建、配置和接受客户端连接的功能。
     * 支持通过配置对象或独立参数进行初始化，并提供了获取套接字描述符、
     * IP 和端口等信息的接口。
     */
    class CreateSocket
    {
    public:
        /**
         * @brief 使用独立参数创建 CreateSocket 实例的静态工厂方法
         * 
         * @param servers_name_prefix 服务器名称前缀（用于日志或标识）
         * @param servers_port 服务器监听的端口号
         * @param servers_max_connections 最大待办连接数（backlog）
         * @param socket_timeout_ms 套接字超时时间（毫秒）
         * @param servers_ip 服务器绑定的 IP 地址
         * @return std::shared_ptr<CreateSocket> 成功则返回实例，失败返回空指针
         */
        static std::shared_ptr<CreateSocket> Create(const std::string &servers_name_prefix,
                                                    uint16_t servers_port,
                                                    int servers_max_connections,
                                                    int socket_timeout_ms,
                                                    const std::string &servers_ip);

        /**
         * @brief 使用 RpcServerConfig 配置对象创建 CreateSocket 实例的静态工厂方法
         * 
         * @param config 包含服务器配置信息的 RpcServerConfig 对象
         * @return std::shared_ptr<CreateSocket> 成功则返回实例，失败返回空指针
         */
        static std::shared_ptr<CreateSocket> Create(const cookrpc::RpcServerConfig &config);

        /**
         * @brief 析构函数，自动关闭套接字描述符
         */
        ~CreateSocket();

        /**
         * @brief 获取监听套接字的文件描述符
         * @return int 套接字 fd
         */
        int GetFd() const { return fd_; }

        /**
         * @brief 获取服务器绑定的 IP 地址
         * @return const std::string& IP 地址字符串
         */
        const std::string &GetIp() const { return servers_ip_; }

        /**
         * @brief 获取服务器监听的端口号
         * @return uint16_t 端口号
         */
        uint16_t GetPort() const { return servers_port_; }

        /**
         * @brief 使用独立参数的构造函数（内部使用，建议通过 Create 工厂方法创建）
         * 
         * @param servers_name_prefix 服务器名称前缀
         * @param servers_port 端口号
         * @param servers_max_connections backlog 大小
         * @param socket_timeout_ms 超时时间（毫秒）
         * @param servers_ip 绑定 IP
         */
        CreateSocket(const std::string &servers_name_prefix,
                     uint16_t servers_port,
                     int servers_max_connections,
                     int socket_timeout_ms,
                     const std::string &servers_ip);

        /**
         * @brief 使用配置对象的构造函数（内部使用，建议通过 Create 工厂方法创建）
         * 
         * @param config RpcServerConfig 配置对象
         */
        CreateSocket(const cookrpc::RpcServerConfig &config);

        /**
         * @brief 接受一个新的客户端连接
         * 
         * 阻塞等待客户端的连接请求，成功则返回客户端套接字的文件描述符，
         * 失败返回 -1。
         * 
         * @return int 客户端套接字 fd，失败返回 -1
         */
        int Accept();

    private:
        /**
         * @brief 初始化监听套接字，包括创建、绑定和监听
         * 
         * @return true 初始化成功
         * @return false 初始化失败
         */
        bool Init();

        /**
         * @brief 设置服务器监听套接字的选项（如地址重用、超时等）
         * 
         * @return true 设置成功
         * @return false 设置失败
         */
        bool SetSocketOpt();

        /**
         * @brief 设置已连接客户端套接字的选项（如禁用 Nagle 算法、超时等）
         * 
         * @param client_fd 客户端套接字文件描述符
         * @return true 设置成功
         * @return false 设置失败
         */
        bool SetClientSocketOpt(int client_fd);

    private:
        int fd_;                        ///< 监听套接字文件描述符
        bool is_init_;                  ///< 是否成功初始化的标志
        std::string servers_name_prefix_; ///< 服务器名称前缀（用于日志标识）
        std::string servers_ip_;        ///< 绑定的 IP 地址
        uint16_t servers_port_;         ///< 绑定的端口号
        int servers_max_connections_;   ///< 最大待办连接数（backlog）
        int socket_timeout_ms_;         ///< 套接字超时时间（毫秒）
    };
} // namespace cookrpc