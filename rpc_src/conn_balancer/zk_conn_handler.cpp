/**
 * @file zk_conn_handler.cpp
 * @brief ZooKeeper连接处理器实现文件
 * 
 * 本文件实现了ZooKeeper连接处理器，负责：
 * 1. 管理与ZooKeeper集群的连接
 * 2. 服务注册和维护
 * 3. 连接故障恢复和重试机制
 * 
 * 设计理念：
 * - 单例模式确保全局唯一的ZooKeeper连接管理
 * - 线程安全设计，支持多线程并发访问
 * - 自动重连机制，提高系统可靠性
 * - 资源自动清理，防止内存泄漏
 */

#include "zk_conn_handler.h"
#include "load_balancer.h"
#include "registry_config.h"
#include "service_registry.h"
#include <stdexcept>
#include <iostream>

#include <zookeeper/zookeeper.h>
#include <nlohmann/json.hpp>
#include <thread>

namespace cookrpc {

    void ZkConnHandler::setZkNamespace(const std::string &zk_namespace)
    {
        zk_namespace_ = zk_namespace;
    }

    // 设置重试间隔，网络不稳定时，提供容错能力
    void ZkConnHandler::setZkRetryInterval(int seconds)
    {
        retry_interval_ = std::chrono::seconds(seconds);
    }

    // 设置ZooKeeper主机地址，使用互斥锁保护并发访问，提供合理的默认值，确保系统可用性
    void ZkConnHandler::setZkHost(const std::string &host)
    {
        std::lock_guard<std::mutex> lock(mutex_);  // 线程安全保护
        try
        {
            if (host.empty())
            {
                LOG_WARN("Empty host provided, using default");
                zk_host_ = "localhost";  // 提供合理的默认值
            }
            else
            {
                zk_host_ = host;
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Failed to set ZooKeeper host: {}", e.what());
            zk_host_ = "localhost";  // 出错时使用默认值，确保系统可用
        }
    }

    void ZkConnHandler::setZkPort(int port)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        zk_port_ = (port > 0 && port < 65536) ? port : 2181;  // 端口范围验证
    }

    /**
     * @brief 获取指定命名空间下的所有服务器列表
     * @param zk_namespace ZooKeeper命名空间
     * @return 服务器地址列表
     * 
     * 实现细节说明：
     * - 首先确保ZooKeeper连接可用
     * - 使用互斥锁保护服务器列表的并发访问
     * - 遍历ZooKeeper子节点获取服务器信息
     * - 每个子节点的数据存储服务器的IP:Port信息
     * - 异常处理确保函数的健壮性
     */
    std::vector<std::string> ZkConnHandler::getAllServers(const std::string &zk_namespace, bool *zk_available)
    {
        // 先假设 ZooKeeper 不可用，只有成功取到子节点列表才置为 true
        if (zk_available)
        {
            *zk_available = false;
        }

        // 确保ZooKeeper连接可用，这是所有操作的前提
        if (!ensureZkConnection())
        {
            LOG_ERROR("ZooKeeper connection not available");
            return {};  // 返回空列表而不是抛异常，更友好
        }

        std::lock_guard<std::mutex> lock(servers_mutex_);  // 保护服务器列表的并发访问

        try
        {
            struct String_vector children = {0};  
            
            // 获取指定路径下的所有子节点（服务实例）
            int rc = zoo_get_children(zk_client_, zk_namespace.c_str(), 0, &children);
            
            // ZOK 是 ZooKeeper C API 的成功错误码，值等于 0，意思是"操作成功"。
            if (rc != ZOK)
            {
                LOG_ERROR("Failed to get children: {}", zerror(rc));
                return {};
            }
            
            std::vector<std::string> servers;
            
            // 遍历每个子节点，获取服务器地址信息
            for (int i = 0; i < children.count; i++)
            {
                std::string node_path = zk_namespace + "/" + children.data[i];
                char buffer[1024];  // 足够大的缓冲区存储服务器地址
                int buffer_len = sizeof(buffer);

                // 获取节点的数据（存储的是服务器的IP:Port信息）
                // 后面可以考虑使用json格式存储（更多的节点信息）方便解析
                int rc = zoo_get(zk_client_, node_path.c_str(), 0, buffer, &buffer_len, nullptr);
                if (rc == ZOK && buffer_len > 0)
                {
                    std::string server_ip(buffer, buffer_len);
                    servers.push_back(server_ip);
                }
                else
                {
                    // 单个节点失败不影响其他节点的处理
                    LOG_WARN("Failed to get data for node: {}, error: {}", node_path, zerror(rc));
                    
                }
            }

            // 释放ZooKeeper分配的内存，防止内存泄漏
            deallocate_String_vector(&children);

            // ZK 可访问：即使列表为空也代表"当前确实没有实例"
            if (zk_available)
            {
                *zk_available = true;
            }

            return servers;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception in getAllServers: {}", e.what());
            return {};  // 异常时返回空列表
        }
    }

    /**
     * @brief 初始化ZooKeeper连接处理器
     * @param config JSON格式的配置信息
     * @return 初始化是否成功
     * 
     * 初始化流程设计：
     * 1. 配置验证 - 确保配置有效性
     * 2. 参数解析 - 安全地提取配置参数
     * 3. 连接建立 - 建立ZooKeeper连接
     * 4. 异常处理 - 确保失败时正确清理资源
     */
    bool ZkConnHandler::initZkConnHandler(const nlohmann::json &config)
    {
        try
        {
            // 检查配置有效性 - 空配置无法工作
            if (config.empty())
            {
                LOG_ERROR("Empty configuration");
                return false;
            }

            // 安全地读取配置参数，提供合理的默认值
            try
            {
                // 使用value()方法提供默认值，避免键不存在时抛异常
                std::string zk_host = config.value("zk_host", "localhost");
                setZkHost(zk_host);
                
                int zk_port = config.value("zk_port", 2181);
                setZkPort(zk_port);
               
                setZkNamespace(config.value("zk_namespace", "/cookrpc"));
                setZkRetryInterval(config.value("zk_retry_interval", 5));
            }
            catch (const nlohmann::json::exception &e)
            {
                LOG_ERROR("Failed to parse configuration: {}", e.what());
                cleanup();  // 配置解析失败时清理已分配的资源
                return false;
            }

            // 尝试建立ZooKeeper连接
            bool is_connected = ensureZkConnection();

            // 双重检查：确保客户端和连接都正常
            if (!zk_client_)
            {
                LOG_ERROR("Failed to initialize ZooKeeper client: {}", strerror(errno));
                return false;
            }
            
            if (!is_connected)
            {
                LOG_ERROR("Failed to connect to ZooKeeper after retries");
                return false;
            }

            return true;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Exception in initZkConnHandler: {}", e.what());
            cleanup();  // 异常时确保资源被正确清理
            return false;
        }
    }

    /**
     * @brief 确保ZooKeeper连接可用
     * @return 连接是否成功建立
     */
    bool ZkConnHandler::ensureZkConnection()
    {
        // 惰性初始化：只在客户端不存在时才创建
        if (!zk_client_)
        {
            // zk_host_ 和 zk_port_ 在 initZkConnHandler 中设置
            std::string conn_string = zk_host_ + ":" + std::to_string(zk_port_); 
    
            // 初始化ZooKeeper客户端
            zk_client_ = zookeeper_init(
                conn_string.c_str(),  // 连接字符串
                global_watcher,       // 全局监听器
                30000,               // 30秒超时
                nullptr,             // 客户端ID（新会话）
                this,                // 监听器上下文
                0                    // 标志位
            );

            if (!zk_client_)
            {
                LOG_ERROR("Failed to initialize ZooKeeper client: {} (errno: {})",
                          strerror(errno), errno);
                return false;
            }
        }

        // 等待连接建立 - ZooKeeper连接是异步的
        int retry_count = 0;
        const int max_retries = 3;  // 最多重试 3 次，这里可以使用配置文件加载
        
        while (retry_count++ < max_retries)
        {
            int state = zoo_state(zk_client_);
            const char *state_desc;

            // 比较详细的状态描述，便于调试和监控，不是必须的
            if (state == ZOO_CONNECTED_STATE)
                state_desc = "CONNECTED";
            else if (state == ZOO_CONNECTING_STATE)
                state_desc = "CONNECTING";
            else if (state == ZOO_EXPIRED_SESSION_STATE)
                state_desc = "EXPIRED";
            else if (state == ZOO_AUTH_FAILED_STATE)
                state_desc = "AUTH_FAILED";
            else if (state == ZOO_ASSOCIATING_STATE)
                state_desc = "ASSOCIATING";
            else
            {
                state_desc = "UNKNOWN";
                // 遇到未知状态时，输出更多诊断信息，帮助调试，也不是必须的
                LOG_WARN("Unexpected state value: {} (0x{:x})", state, state);
                LOG_WARN("Client handle valid: {}", (zk_client_ != nullptr));
                
                if (zk_client_)
                {
                    // 尝试一个简单的操作测试连接，帮助调试，也不是必须的
                    struct Stat stat = {0};
                    int rc = zoo_exists(zk_client_, "/", 0, &stat);
                    LOG_WARN("zoo_exists test result: {}", rc);
                }
            }

            // 连接成功
            if (state == ZOO_CONNECTED_STATE)
            {
                return true;
            }
            // 不可恢复的错误状态
            else if (state == ZOO_EXPIRED_SESSION_STATE ||
                     state == ZOO_AUTH_FAILED_STATE ||
                     (state != ZOO_CONNECTING_STATE && retry_count > 10)) 
            {
                LOG_ERROR("Connection failed with state: {} ({})", state, state_desc);
                cleanup();
                return false;
            }

            // 短暂等待后重试
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        LOG_ERROR("Failed to connect after {} retries", max_retries);
        cleanup();
        return false;
    }

    /**
     * @brief 获取单个服务器地址
     * @param zk_namespace ZooKeeper命名空间
     * @return 选中的服务器地址
     * 
     * 服务发现和负载均衡的入口函数：
     * 1. 更新服务器列表 - 确保使用最新的服务器信息
     * 2. 负载均衡选择 - 使用负载均衡算法选择服务器
     * 3. 异常处理 - 无可用服务器时的处理，各位也可以换成错误码哈
     * 
     * 设计考虑：
     * - 每次调用都更新服务器列表，确保实时性
     * - 后期可优化为定期更新+缓存机制，训练营的 meeting 后端项目是采用的这种方式，需要的可以查看一下
     * - 使用单例的负载均衡器，保持状态一致性
     */
    std::string ZkConnHandler::getServer(const std::string &zk_namespace)
    {
        // 每次访问前更新服务器列表，确保使用最新信息
        // 注释说明了后期可优化为定时更新+内存缓存模式
        updateServersFromZk(zk_namespace);
        std::lock_guard<std::mutex> lock(servers_mutex_);
        
        if (servers_.empty())
        {
            LOG_ERROR("No available servers");
            return "";  // 返回空字符串表示无可用服务器
        }
        
        // 使用负载均衡器选择服务器，负载均衡类型是在 initBalancer 中设置的
        std::string server = LoadBalancer::selectServer(servers_);
        return server;
    }

    /**
     * @brief 清理资源
     * 
     * 资源清理的重要性：
     * 1. 防止内存泄漏
     * 2. 正确关闭网络连接
     * 3. 避免僵尸进程
     * 4. 处理程序退出时的竞态条件
     * 
     * 设计细节：
     * - 使用原子变量确保只执行一次清理
     * - 异常安全的资源释放
     * - 在程序退出阶段避免使用可能已销毁的Logger
     */
    void ZkConnHandler::cleanup()
    {
        // 通过原子标志确保在多线程环境中清理操作只被执行一次，各位也可以去了解一下其应用场景
        static std::atomic<bool> cleanup_done{false};
        if (cleanup_done.exchange(true)) {
            return;
        }
        
        // 清理ServiceRegistry实例 - 智能指针自动管理内存
        service_registry_.reset();  // 等价于 service_registry_ = nullptr; 会自动调用析构函数
        
        // 清理ZooKeeper客户端连接
        if (zk_client_)
        {
            try {
                zookeeper_close(zk_client_);  // 正确关闭ZooKeeper连接
                zk_client_ = nullptr;
            } catch (const std::exception& e) {
                zk_client_ = nullptr; // 防止重复关闭
            } catch (...) {
                zk_client_ = nullptr; // 防止重复关闭
            }
        }
        
        // 重置运行状态
        running_ = false;
    }

    /**
     * @brief 从ZooKeeper更新服务器列表
     * @param zk_namespace ZooKeeper命名空间
     * 
     * 获取最新的服务器列表
     **/
    void ZkConnHandler::updateServersFromZk(const std::string &zk_namespace)
    {
        bool zk_available = false;
        auto new_servers = getAllServers(zk_namespace, &zk_available);

        // ZooKeeper 暂时不可用：保留上一次成功获取的列表，作为本地缓存兜底
        if (!zk_available)
        {
            std::lock_guard<std::mutex> lock(servers_mutex_);
            if (!servers_.empty())
            {
                LOG_WARN("ZooKeeper unavailable, keeping cached server list ({} entries)", servers_.size());
            }
            else
            {
                LOG_ERROR("ZooKeeper unavailable and no cached server list");
            }
            return;
        }

        // ZooKeeper 正常：即使返回空列表也更新，表示当前确实没有可用实例
        std::lock_guard<std::mutex> lock(servers_mutex_);
        servers_ = std::move(new_servers);
    }

    /**
     * @brief 全局ZooKeeper事件监听器
     * @param zh ZooKeeper句柄
     * @param type 事件类型
     * @param state 连接状态
     * @param path 事件路径
     * @param watcherCtx 监听器上下文
     * 
     * 监听器的作用：
     * 1. 监控连接状态变化
     * 2. 处理会话过期事件
     * 3. 响应网络断开重连
     * 4. 提供连接状态的实时反馈
     * 
     * 为什么需要全局监听器：
     * - ZooKeeper的异步特性需要事件驱动
     * - 连接状态变化需要及时响应
     * - 便于调试和监控
     */
    void ZkConnHandler::global_watcher(zhandle_t *zh, int type, int state,
                                       const char *path, void *watcherCtx)
    {
        // 目前主要处理会话事件
        if (type == ZOO_SESSION_EVENT)
        {
            const char *state_desc;
            // 状态描述，便于日志记录和调试
            if (state == ZOO_CONNECTED_STATE)
                state_desc = "CONNECTED";
            else if (state == ZOO_CONNECTING_STATE)
                state_desc = "CONNECTING";
            else if (state == ZOO_EXPIRED_SESSION_STATE)
                state_desc = "EXPIRED";
            else if (state == ZOO_AUTH_FAILED_STATE)
                state_desc = "AUTH_FAILED";
            else if (state == ZOO_ASSOCIATING_STATE)
                state_desc = "ASSOCIATING";
            else
                state_desc = "UNKNOWN";
                
            // 注意：这里没有输出日志，可能是为了避免日志过多
            // 实际使用中可以根据需要添加日志记录
        }
    }

    /**
     * @brief 析构函数
     * 
     * RAII原则的体现：
     * - 对象销毁时自动清理资源
     * - 确保不会有资源泄漏
     * - 即使程序异常退出也能正确清理
     */
    ZkConnHandler::~ZkConnHandler()
    {
        cleanup();
    }

    /**
     * @brief 注册单个服务
     * @param service_name 服务名称
     * @param service_address 服务地址
     * @return 注册是否成功
     * 
     * 服务注册的重要性：
     * 1. 让其他服务能够发现此服务
     * 2. 支持动态服务发现
     * 3. 实现服务的高可用性
     * 
     * 实现细节：
     * - 惰性创建ServiceRegistry实例
     * - 等待连接建立后再注册
     * - 完整的错误处理和日志记录
     */
    bool ZkConnHandler::registerService(const std::string &service_name, const std::string &service_address)
    {
        try 
        {
            // 使用新的方法获取或创建 ServiceRegistry 实例
            ServiceRegistry* registry = getOrCreateServiceRegistry();
            if (!registry) {
                LOG_ERROR("Failed to get or create ServiceRegistry for service registration");
                return false;
            }
            
            // 执行服务注册
            if (registry->registerService(service_name, service_address))
            {
                // // LOG_INFO("Successfully registered service: {} at {}", service_name, service_address);
                return true;
            }
            else
            {
                LOG_ERROR("Failed to register service: {} at {}", service_name, service_address);
                return false;
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception during service registration: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 从配置文件注册多个服务
     * @param registry_config 服务注册配置
     * @return 注册是否成功
     * 
     * 这块其实是为了测试负载均衡而存在，注册了多个相同的机器（使用了外网 ip 和 0.0.0.0 的 ip）
     * 可以将 rpc server 部署在不同的机器上
     * client 通过负载均衡器来选择不同的机器
     */
    bool ZkConnHandler::registerServicesFromConfig(const cookrpc::ServiceRegistryConfig* registry_config)
    {
        // 没有配置不算错误，某些服务可能不需要注册到ZooKeeper，这块各位可以看自己的需求
        if (!registry_config || registry_config->GetRegistryNodesSize() == 0)
        {
            LOG_WARN("No registry configuration found, skipping ZooKeeper registration");
            return true; // 没有配置不算错误
        }

        try 
        {
            // 使用新的方法获取或创建 ServiceRegistry 实例
            ServiceRegistry* registry = getOrCreateServiceRegistry();
            if (!registry) {
                LOG_ERROR("Failed to get or create ServiceRegistry for batch service registration");
                return false;
            }
            
            // 注册配置文件中的所有服务节点
            const auto& registry_nodes = registry_config->GetRegistryNodes();
            const std::string& service_name = registry_config->GetServiceName();
            
            bool all_success = true;
            for (const auto& node : registry_nodes)
            {
                std::string service_address = node.address + ":" + std::to_string(node.port);
                if (registry->registerService(service_name, service_address))
                {
                    // // LOG_INFO("Successfully registered service: {} at {}", service_name, service_address);
                }
                else
                {
                    // LOG_ERROR("Failed to register service: {} at {}", service_name, service_address);
                    all_success = false;  // 记录失败但继续处理其他节点
                }
            }
            
            // 重要：不在这里销毁service_registry_，保持连接以维持临时节点
            // ZooKeeper的临时节点在连接断开时会自动删除，这是服务发现的重要机制
            // 智能指针会在对象销毁时自动管理内存
            // // LOG_INFO("Service registration completed. Keeping ZooKeeper connection alive to maintain ephemeral nodes.");
            
            return all_success;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Exception during service registration: {}", e.what());
            return false;
        }
    }

    /**
     * @brief 获取或创建 ServiceRegistry 实例（线程安全）
     * @return ServiceRegistry 指针，失败时返回 nullptr
     * 
     * 这个方法提供了一个安全的方式来获取 ServiceRegistry 实例：
     * 1. 如果实例已存在，直接返回
     * 2. 如果不存在，自动创建并初始化
     * 3. 线程安全，可以在多线程环境中安全调用
     */
    ServiceRegistry* ZkConnHandler::getOrCreateServiceRegistry()
    {
        // 使用双重检查锁定模式确保线程安全
        if (!service_registry_) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!service_registry_) {  // 再次检查
                if (!createServiceRegistryIfNeeded()) {
                    return nullptr;
                }
            }
        }
        return service_registry_.get();
    }

    /**
     * @brief 内部创建 ServiceRegistry 的辅助方法
     * @return 创建是否成功
     * 
     * 这个方法封装了 ServiceRegistry 的创建逻辑：
     * - 构建连接字符串
     * - 创建 ServiceRegistry 实例
     * - 等待连接建立
     * - 验证连接状态
     */
    bool ZkConnHandler::createServiceRegistryIfNeeded()
    {
        try {
            // 构建 ZooKeeper 连接字符串
            std::string zk_connection = zk_host_ + ":" + std::to_string(zk_port_);
            // // LOG_INFO("Creating ServiceRegistry with connection: {}", zk_connection);
            
            // 创建 ServiceRegistry 实例
            service_registry_ = std::make_unique<ServiceRegistry>(zk_connection);
            
            // 等待连接建立 - ZooKeeper 连接是异步的
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // 验证连接状态
            if (!service_registry_->isConnected()) {
                LOG_ERROR("Failed to connect ServiceRegistry to ZooKeeper at {}", zk_connection);
                service_registry_.reset();  // 清理失败的实例
                return false;
            }
            
            // // LOG_INFO("ServiceRegistry connected successfully to ZooKeeper at {}", zk_connection);
            return true;
            
        } catch (const std::exception& e) {
            LOG_ERROR("Exception creating ServiceRegistry: {}", e.what());
            service_registry_.reset();
            return false;
        }
    }


}