#include "load_balancer.h"
#include "random.h"
#include "round.h"
#include "weight.h"
#include <unordered_map>
#include <functional>

namespace cookrpc
{

    // 静态成员初始化
    std::mutex LoadBalancer::mutex_;
    std::shared_ptr<LoadBalancer> LoadBalancer::instance_ = nullptr;
    
    namespace
    {
    
    /**
     * @brief 负载均衡器工厂
     *
     * 职责:
     * 1. 提供创建各种负载均衡器的工厂方法
     * 2. 维护类型字符串到工厂函数的映射表
     */
        struct BalancerFactory
        {
            static std::shared_ptr<LoadBalancer> createRandom()
            {
                return std::make_shared<RandomLoadBalancer>();
            }
        
            static std::shared_ptr<LoadBalancer> createRoundRobin()
            {
                return std::make_shared<RoundRobinLoadBalancer>();
            }
        
            static std::shared_ptr<LoadBalancer> createWeighted()
            {
                return std::make_shared<WeightedRoundRobinLoadBalancer>();
            }
        
            /**
             * @brief 获取工厂映射表
             * @return 类型字符串 -> 工厂函数的映射
             */
            static const std::unordered_map<std::string,
                                            std::function<std::shared_ptr<LoadBalancer>()>>
            &getMap()
            {
                static const std::unordered_map<std::string,
                                                std::function<std::shared_ptr<LoadBalancer>()>> 
                    map = {
                        {"random", createRandom},
                        {"round", createRoundRobin},
                        {"weight", createWeighted}
                };
                return map;
            }
        };
    } // anonymous namespace

// 实现省略, 见后续步骤...







} // namespace cookrpc