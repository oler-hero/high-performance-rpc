#pragma once
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "log_manager.h"

namespace cookrpc
{

/**
 * @brief 负载均衡器抽象接口
 *
 * 设计模式: 策略模式
 * - LoadBalancer 是抽象策略
 * - RandomLoadBalancer、RoundRobinLoadBalancer 等是具体策略
 * - 客户端通过统一接口调用，无需关心具体算法
 *
 * 职责:
 * 1. 定义 select() 接口，由子类实现具体算法
 * 2. 提供单例访问和初始化接口
 * 3. 提供静态方法 selectServer() 供外部调用
 */
class LoadBalancer
{
public:
    virtual ~LoadBalancer() = default;

    /**
     * @brief 从实例列表中选择一个服务实例 (纯虚函数)
     * @param instances 服务实例列表 (如 ["192.168.1.100:8001", "192.168.1.101:8001"])
     * @return 选中的服务实例地址
     *
     * 由子类实现具体的负载均衡算法:
     * - RandomLoadBalancer: 随机选择
     * - RoundRobinLoadBalancer: 轮询选择
     * - WeightedRoundRobinLoadBalancer: 加权轮询
     */
    virtual std::string select(const std::vector<std::string> &instances) = 0;

    /**
     * @brief 获取全局单例实例
     * @return 负载均衡器智能指针
     *
     * 双重检查锁定 (Double‑Checked Locking) 确保线程安全
     */
    static std::shared_ptr<LoadBalancer> getInstance();

    /**
     * @brief 初始化负载均衡器
     * @param type 负载均衡器类型 ("random", "round", "weight")
     * @return 成功返回 true，失败返回 false
     *
     * 使用工厂模式根据类型字符串创建对应的负载均衡器
     * 如果类型未知，则降级为随机负载均衡器
     */
    static bool initBalancer(const std::string &type = "random");

    /**
     * @brief 从服务器列表中选择一个服务器 (静态接口)
     * @param servers 服务器地址列表
     * @return 选中的服务器地址
     *
     * 这是对外提供的主要接口, 由 ZkConnHandler::getServer() 调用
     */
    static std::string selectServer(const std::vector<std::string> &servers);

protected:
    // 允许子类访问构造函数
    LoadBalancer() noexcept = default;

private:
    static std::shared_ptr<LoadBalancer> instance_; // 单例实例
    static std::mutex mutex_;                       // 保护单例创建

    // 禁止拷贝和赋值
    LoadBalancer(const LoadBalancer &) = delete;
    LoadBalancer &operator=(const LoadBalancer &) = delete;
};

} // namespace cookrpc