#pragma once

// 解决 C++ 与 C 库混用的问题
#ifdef __cplusplus
extern "C"
{
#endif

#include <zookeeper/zookeeper.h>
#include <zookeeper/zookeeper_version.h>
#include <zookeeper/proto.h>

#ifdef __cplusplus
}
#endif

#include <atomic>
#include <memory>
#include <string>
#include <chrono>
#include <mutex>
#include <vector>
#include <nlohmann/json.hpp>
#include <log_manager.h>
#include "service_registry.h"

class LoadBalancer;  // 前向声明

namespace cookrpc
{
class ServiceRegistryConfig;

/**
 * @brief ZooKeeper 连接处理器
 *
 * 职责:
 * 1. 管理与 ZooKeeper 集群的连接
 * 2. 服务发现: 从 ZooKeeper 获取服务实例列表
 * 3. 负载均衡集成: 选择最优服务实例
 * 4. 服务注册: 将服务注册到 ZooKeeper
 * 5. 连接监控: 监听连接状态变化
 *
 * 设计模式:
 * - 单例模式: 全局唯一的连接管理器
 * - RAII: 自动资源管理
 * - 双重检查锁定: 线程安全的惰性初始化
 */
class ZkConnHandler
{
public:
    /**
     * @brief 获取单例实例
     * @return ZkConnHandler 引用
     *
     * 使用 Meyer's Singleton 保证线程安全
     */
    static ZkConnHandler &GetInstance()
    {
        static ZkConnHandler instance;
        return instance;
    }

    // 禁止拷贝和移动 (单例模式要求)
    ZkConnHandler(const ZkConnHandler &) = delete;
    ZkConnHandler &operator=(const ZkConnHandler &) = delete;
    ZkConnHandler(ZkConnHandler &&) = delete;
    ZkConnHandler &operator=(ZkConnHandler &&) = delete;

    /**
     * @brief 初始化 ZooKeeper 连接处理器
     * @param config JSON 格式的配置
     * @return 初始化是否成功
     *
     * 配置格式:
     * {
     *   "zk_host": "127.0.0.1",
     *   "zk_port": 2181,
     *   "zk_namespace": "/cookrpc",
     *   "zk_retry_interval": 5
     * }
     */
    bool initZkConnHandler(const nlohmann::json &config);

    /**
     * @brief 获取指定命名空间下的所有服务器列表
     * @param zk_namespace ZooKeeper 命名空间 (如 "/cookrpc/UserService")
     * @return 服务器地址列表 (如 ["192.168.1.100:8001", "192.168.1.101:8001"])
     *
     * 实现细节:
     * 1. 确保 ZooKeeper 连接可用
     * 2. 使用 zoo_get_children() 获取子节点列表
     * 3. 遍历子节点, 使用 zoo_get() 获取节点数据 (IP:Port)
     * 4. 返回服务器地址列表
     */
    std::vector<std::string> getAllServers(const std::string &zk_namespace, bool *zk_available = nullptr);

    /**
     * @brief 获取单个服务器地址 (服务发现 + 负载均衡)
     * @param zk_namespace ZooKeeper 命名空间
     * @return 选中的服务器地址
     *
     * 流程:
     * 1. updateServersFromZk() - 更新服务器列表
     * 2. LoadBalancer::selectServer() - 负载均衡选择
     * 3. 返回选中的服务器地址
     */
    std::string getServer(const std::string &zk_namespace);

    /**
     * @brief 注册单个服务
     * @param service_name 服务名称
     * @param service_address 服务地址 (如 "192.168.1.100:8001")
     * @return 注册是否成功
     *
     * 使用 ServiceRegistry 在 ZooKeeper 中创建临时节点
     */
    bool registerService(const std::string &service_name,
                         const std::string &service_address);

    /**
     * @brief 从配置文件批量注册服务
     * @param registry_config 服务注册配置
     * @return 注册是否成功
     *
     * 用于启动时一次性注册多个服务实例
     */
    bool registerServicesFromConfig(const cookrpc::ServiceRegistryConfig* registry_config);

    /**
     * @brief 获取 ServiceRegistry 实例
     */
    ServiceRegistry* getServiceRegistry() { return service_registry_.get(); }
    const ServiceRegistry* getServiceRegistry() const { return service_registry_.get(); }
    bool hasServiceRegistry() const { return service_registry_ != nullptr; }

    /**
     * @brief 获取或创建 ServiceRegistry 实例 (线程安全)
     * @return ServiceRegistry 指针, 失败时返回 nullptr
     *
     * 使用双重检查锁定实现惰性初始化
     */
    ServiceRegistry* getOrCreateServiceRegistry();

    // 配置设置方法
    void setZkHost(const std::string &host);
    void setZkPort(int port);
    void setZkRetryInterval(int seconds);
    void setZkNamespace(const std::string &zk_namespace);

    /**
     * @brief 确保 ZooKeeper 连接可用
     * @return 连接是否成功
     *
     * 如果连接不存在, 创建新连接; 如果已断开, 尝试重连
     */
    bool ensureZkConnection();

    /**
     * @brief 从 ZooKeeper 更新服务器列表
     * @param zk_namespace ZooKeeper 命名空间
     */
    void updateServersFromZk(const std::string &zk_namespace);

    /**
     * @brief 清理资源
     *
     * 释放 ZooKeeper 连接和 ServiceRegistry 实例
     */
    void cleanup();

    /**
     * @brief 析构函数
     *
     * RAII 原则: 自动调用 cleanup()
     */
    ~ZkConnHandler();

private:
    /**
     * @brief 私有构造函数 (单例模式)
     */
    ZkConnHandler()
        : zk_client_(nullptr), running_(false), service_registry_(nullptr)
    {
    }

    /**
     * @brief 全局 Watcher 回调函数
     * @param zh ZooKeeper 句柄
     * @param type 事件类型
     * @param state 连接状态
     * @param path 事件路径
     * @param watcherCtx Watcher 上下文 (指向 ZkConnHandler 实例)
     *
     * 监听 ZooKeeper 连接状态变化 (CONNECTED/EXPIRED/AUTH_FAILED 等)
     */
    static void global_watcher(zhandle_t *zh, int type, int state,
                              const char *path, void *watcherCtx);

    /**
     * @brief 创建 ServiceRegistry 实例 (内部方法)
     * @return 创建是否成功
     */
    bool createServiceRegistryIfNeeded();

    /**
     * @brief 初始化 ZooKeeper 客户端
     * @return 初始化是否成功
     */
    bool initZkClient();

    // 成员变量
    std::atomic<bool> running_;                     // 运行状态
    std::chrono::seconds retry_interval_;            // 重试间隔

    std::mutex mutex_;                              // 保护成员变量访问
    std::mutex servers_mutex_;                      // 保护服务器列表
    std::vector<std::string> servers_;              // 缓存的服务器列表

    zhandle_t *zk_client_;                          // ZooKeeper 客户端句柄

    // 配置参数
    std::string zk_host_;                           // ZooKeeper 主机地址
    int zk_port_;                                   // ZooKeeper 端口
    std::string zk_namespace_;                      // 命名空间

    // ServiceRegistry 实例 (智能指针自动管理)
    std::unique_ptr<ServiceRegistry> service_registry_;
};

} // namespace cookrpc