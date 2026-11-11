#include <cstdio>
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <cstdlib>
#include <fcntl.h>
#include "rpc_server_config.h"
#include "log_manager.h"
#include "create_socket.h"
#include "message_cycle.h"
#include "rpc_service.h"
#include "service_manager.h"
#include "zk_conn_handler.h"
#include "thread_pool_singleton.h"

// 全局变量声明，用于优雅关闭
static std::atomic<bool> g_shutdown_requested{false};
static cookrpc::MessageCycle* g_message_cycle = nullptr;
static std::shared_ptr<cookrpc::CreateSocket> g_socket_server = nullptr;

void SignalHandler(int sig)
{
    LOG_INFO("Received signal: {}, initiating graceful shutdown", sig);
    
    // 设置关闭标志
    g_shutdown_requested = true;
    
    // 停止消息循环
    if (g_message_cycle) {
        g_message_cycle->Stop();
    } else {
        LOG_WARN("Message cycle not available");
    }
}

// 优雅关闭函数
void GracefulShutdown()
{
    LOG_INFO("Starting graceful shutdown...");
    
    // 停止接受新连接
    if (g_socket_server) {
        LOG_INFO("Closing server socket...");
        g_socket_server.reset();  // 这会调用析构函数关闭socket
    }
    
    // 关闭所有现有连接
    LOG_INFO("Closing all connections...");
    auto& connection_manager = cookrpc::ConnectionManager::GetInstance();
    connection_manager.CloseAll();
    
    // 优雅关闭线程池
    LOG_INFO("Shutting down ThreadPool...");
    if (meeting_ctrl::ThreadPoolSingleton::exists()) {
        if (meeting_ctrl::ThreadPoolSingleton::shutdown(std::chrono::seconds(10))) {
            LOG_INFO("ThreadPool shut down successfully");
        } else {
            LOG_WARN("ThreadPool shutdown timeout, forcing stop");
            meeting_ctrl::ThreadPoolSingleton::destroy();
        }
    }
    
    // 清理ZooKeeper连接 - 在其他资源清理完成后
    LOG_INFO("Cleaning up ZooKeeper connections...");
    try {
        auto& zk_handler = cookrpc::ZkConnHandler::GetInstance();
        zk_handler.cleanup();
        // LOG_INFO("ZooKeeper cleanup completed");
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during ZooKeeper cleanup: {}", e.what());
    } catch (...) {
        LOG_ERROR("Unknown exception during ZooKeeper cleanup");
    }
    
    LOG_INFO("Graceful shutdown completed");
}

int main(int argc, char *argv[])
{
    // 注册信号处理器
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    
    // 确保在异常退出时也能清理资源
    std::atexit([]() {
        if (!g_shutdown_requested.load()) {
            LOG_WARN("Unexpected exit, performing cleanup...");
            GracefulShutdown();
        }
    });

    try {
        // 初始化日志
        if (!Logger::GetInstance().Init())
        {
            std::cerr << "Failed to initialize logger" << std::endl;
            return -1;
        }

        // 初始化配置
        std::string config_filename = "../config/rpc_server.json";
        auto &rpc_server_config = cookrpc::RpcServerConfig::GetInstance();
        if (!rpc_server_config.InitRpcServerConfig(config_filename))
        {
            LOG_ERROR("load config failed: {}", config_filename);
            return 1;
        }

        // 线程池配置
        auto thread_pool_config = rpc_server_config.GetThreadPoolConfig();

        // 初始化线程池 - 在服务器启动时初始化
        LOG_INFO("Initializing ThreadPool...");
        meeting_ctrl::ThreadPoolStruct config;
        config.core_threads = thread_pool_config->GetCoreThreads();
        config.max_threads = thread_pool_config->GetMaxThreads();
        config.max_queue_size = thread_pool_config->GetQueueSize();
        config.keep_alive_time = std::chrono::seconds(thread_pool_config->GetKeepAliveTime());

        if (!meeting_ctrl::ThreadPoolSingleton::init(config)) { 
            LOG_WARN("ThreadPool already initialized or failed to initialize");
        } else {
            LOG_INFO("ThreadPool initialized successfully with {} threads", config.max_threads);
        }

         // 服务端相关配置
        std::string servers_ip = rpc_server_config.GetServersIp();
        int servers_port = rpc_server_config.GetServersPort();
        int servers_max_connections = rpc_server_config.GetMaxConnections();
        int socket_timeout_ms = rpc_server_config.GetTimeout();
        std::string servers_name_prefix = rpc_server_config.GetServersNamePrefix();

        // 创建socket服务器
        g_socket_server = cookrpc::CreateSocket::Create(servers_name_prefix, servers_port, servers_max_connections, socket_timeout_ms, servers_ip);
        if (!g_socket_server)
        {
            LOG_ERROR("create socket server failed");
            return 1;
        }

        // 创建消息循环
        auto &connection_manager = cookrpc::ConnectionManager::GetInstance();
        auto message_cycle = std::make_unique<cookrpc::MessageCycle>(&connection_manager);

        if (!message_cycle)
        {
            LOG_ERROR("init message cycle failed");
            return 1;
        }

        // 添加监听套接字
        if (!message_cycle->AddListenFd(g_socket_server->GetFd()))
        {
            LOG_ERROR("add server socket to message cycle failed");
            return 1;
        }

        // 在服务器启动时注册服务
        auto rpc_service = std::make_shared<cookrpc::RpcService>();
        if (!cookrpc::ServiceManager::GetInstance().RegisterService(rpc_service))
        {
            LOG_ERROR("Failed to register rpc service");
            return 1;
        }

        // 向 ZooKeeper 注册服务实例
        auto& zk_handler = cookrpc::ZkConnHandler::GetInstance();
        auto registry_config = rpc_server_config.GetServiceRegistryConfig();
        if (!zk_handler.registerServicesFromConfig(registry_config))
        {
            LOG_ERROR("Failed to register services to ZooKeeper");
            return 1;
        }

        g_message_cycle = message_cycle.get();
        LOG_INFO("Server starting, listening on {}:{}", servers_ip, servers_port);
        
        // 运行消息循环
        message_cycle->Loop();

        LOG_INFO("Message loop ended, starting shutdown process");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in main: {}", e.what());
        GracefulShutdown();
        return 1;
    } catch (...) {
        LOG_ERROR("Unknown exception in main");
        GracefulShutdown();
        return 1;
    }
    
    // 正常退出时的清理
    g_shutdown_requested = true;
    GracefulShutdown();
    
    return 0;
}