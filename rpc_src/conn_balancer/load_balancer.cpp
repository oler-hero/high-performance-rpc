#include "load_balancer.h"
#include "random.h"
#include "round.h"
#include "weight.h"
#include <unordered_map>
#include <functional>

namespace cookrpc
{

    // 负载均衡器工厂映射表
    std::mutex LoadBalancer::mutex_;
    std::shared_ptr<LoadBalancer> LoadBalancer::instance_ = nullptr;

    namespace
    {
        // 负载均衡器工厂函数
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

            // 工厂映射表
            static const std::unordered_map<std::string, std::function<std::shared_ptr<LoadBalancer>()>> &getMap()
            {
                static const std::unordered_map<std::string,
                                                std::function<std::shared_ptr<LoadBalancer>()>>
                    map = {
                        {"random", createRandom},
                        {"round", createRoundRobin},
                        {"weight", createWeighted}};
                return map;
            }
        };
    }

    // 返回 Instance
    std::shared_ptr<LoadBalancer> LoadBalancer::getInstance()
    {
        if (!instance_) 
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!instance_) 
            {
                instance_ = BalancerFactory::createRandom();
            }
        }
        return instance_; // 直接返回，自动共享所有权
    }
    
    // 初始化
    bool LoadBalancer::initBalancer(const std::string &type)
    {
        // 通过工厂类的映射表查找对应的负载均衡器创建函数
        std::lock_guard<std::mutex> lock(mutex_);
        const auto &balancer_map = BalancerFactory::getMap();
        auto it = balancer_map.find(type);

        if (it != balancer_map.end())
        {
            try
            {   
                // 如果查询到，则直接返回对应的
                instance_ = it->second();
                return true;
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Failed to initialize {} load balancer: {}", type, e.what());
                // 如果查询失败，则使用随机负载均衡器
                instance_ = BalancerFactory::createRandom();
                return false;
            }
        }

        // 用随机负载均衡兜底
        LOG_WARN("Unknown load balancer type: {}, falling back to random", type);
        instance_ = BalancerFactory::createRandom();
        return false;
    }

    // 选择一台服务器，可以查询 select 的调用方式，这里的 servers 为所有节点集合
    // ZkConnHandler::getServer 函数内调用
    std::string LoadBalancer::selectServer(const std::vector<std::string> &servers)
    {
        if (!instance_)
        {
            LOG_WARN("Load balancer not initialized, using default random balancer");
            std::lock_guard<std::mutex> lock(mutex_);
            if (!instance_)
            {
                instance_ = std::make_shared<RandomLoadBalancer>();
            }
        }

        if (servers.empty())
        {
            LOG_ERROR("No instances available for load balancing");
            return "";
        }

        try
        {
            return instance_->select(servers); // 调用负载均衡器的选择方法
        } 
        catch (const std::exception &e)
        {
            LOG_ERROR("Error in load balancer select: {}", e.what());
            return servers[0]; // 发生错误时返回第一个实例作为后备
        }
    }
}