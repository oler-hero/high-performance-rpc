#pragma once
#include "load_balancer.h"
#include <random>

namespace cookrpc
{

/**
 * @brief 随机负载均衡器
 *
 * 算法:
 * 1. 使用 std::random_device 获取真随机种子
 * 2. 使用 std::mt19937 生成伪随机数
 * 3. 使用 std::uniform_int_distribution 生成均匀分布的索引
 *
 * 优点:
 * - 简单高效
 * - 天然支持多线程 (每个请求独立随机)
 * - 自动负载均衡 (长期来看请求均匀分布)
 *
 * 缺点:
 * - 短期内可能不均匀
 * - 无法保证每个实例被依次调用
 */
class RandomLoadBalancer : public LoadBalancer
{
public:
    RandomLoadBalancer() : rd_(), gen_(rd_()) {}

    std::string select(const std::vector<std::string> &instances) override
    {
        // 1. 空列表检查
        if (instances.empty())
        {
            return "";
        }

        // 2. 生成 [0, instances.size()-1] 范围内的随机索引
        std::uniform_int_distribution<> dis(0, instances.size() - 1);
        size_t index = dis(gen_);

        // 3. 返回随机选中的实例
        return instances[index];
    }

private:
    std::random_device rd_; // 真随机数生成器 (硬件)
    std::mt19937 gen_;     // Mersenne Twister 伪随机数生成器
};

} // namespace cookrpc