#pragma once
#include "load_balancer.h"
#include <atomic>

namespace cookrpc
{

/**
 * @brief 轮询负载均衡器
 *
 * 算法:
 * 1. 维护一个原子计数器 current_index_
 * 2. 每次请求时递增计数器并对实例数量取模
 * 3. 返回对应索引的实例
 *
 * 优点:
 * - 绝对公平: 每个实例被调用次数相同
 * - 线程安全: 使用 std::atomic 原子操作
 * - 可预测: 按顺序依次调用实例
 *
 * 缺点:
 * - 无法处理实例性能差异 (所有实例权重相同)
 * - 计数器可能溢出 (虽然使用模运算缓解)
 */
class RoundRobinLoadBalancer : public LoadBalancer
{
public:
    RoundRobinLoadBalancer() : current_index_(0) {}

    std::string select(const std::vector<std::string> &instances) override
    {
        // 1. 空列表检查
        if (instances.empty())
        {
            return "";
        }

        // 2. 原子操作: 获取当前索引并递增
        // fetch_add(1) 返回旧值, 然后将 current_index_ 加 1
        size_t index = current_index_.fetch_add(1) % instances.size();

        // 3. 返回对应实例
        return instances[index];
    }

private:
    std::atomic<size_t> current_index_; // 原子计数器 (线程安全)
};

} // namespace cookrpc