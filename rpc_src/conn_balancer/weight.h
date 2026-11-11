#pragma once
#include "load_balancer.h"
#include <map>
#include <atomic>

namespace cookrpc
{

/**
 * @brief 加权轮询负载均衡器
 *
 * 算法:
 * 1. 为每个实例分配权重 (权重越高, 被选中概率越大)
 * 2. 计算总权重 total_weight
 * 3. 维护计数器 current_index_, 在 [0, total_weight) 范围内循环
 * 4. 根据累计权重确定选中的实例
 *
 * 优点:
 * - 支持实例性能差异 (高性能实例设置更高权重)
 * - 长期来看请求分配符合权重比例
 *
 * 缺点:
 * - 实现复杂度高于简单轮询
 * - 需要配置权重 (本实现简化为等权重)
 */
class WeightedRoundRobinLoadBalancer : public LoadBalancer
{
public:
    WeightedRoundRobinLoadBalancer() : current_index_(0) {}

    std::string select(const std::vector<std::string> &instances) override
    {
        // 1. 空列表检查
        if (instances.empty())
        {
            return "";
        }

        // 2. 简化实现: 所有实例权重为 1 (等同于普通轮询)
        // 实际应用中应从配置或服务注册中心获取权重
        std::vector<int> weights(instances.size(), 1);

        // 3. 计算总权重
        int total_weight = 0;
        for (int weight : weights)
        {
            total_weight += weight;
        }

        // 4. 递增计数器并取模
        current_index_ = (current_index_ + 1) % total_weight;

        // 5. 根据累计权重找到对应实例
        int current_weight = 0;
        for (size_t i = 0; i < instances.size(); ++i)
        {
            current_weight += weights[i];
            if (current_index_ < current_weight)
            {
                return instances[i];
            }
        }

        // 6. 降级: 返回第一个实例
        return instances[0];
    }

private:
    std::atomic<int> current_index_;               // 当前索引
    std::map<std::string, int> weights_;            // 实例权重映射 (预留)

    /**
     * @brief 设置实例权重 (预留接口)
     * @param instance 实例地址
     * @param weight 权重值
     */
    void setWeight(const std::string &instance, int weight)
    {
        weights_[instance] = weight;
    }
};

} // namespace cookrpc