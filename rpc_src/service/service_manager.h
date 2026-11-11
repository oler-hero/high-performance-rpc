#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <future>
#include "service.h"
#include "log_manager.h"
#include "thread_pool_singleton.h"

namespace cookrpc
{

    /**
     * @brief 服务管理器（单例模式）
     * 
     * 负责注册、获取和调用 RPC 服务。所有服务实例通过服务名称唯一标识。
     * 提供同步和异步两种请求处理方式，异步处理依赖全局线程池（ThreadPoolSingleton）。
     * 该类是线程安全的，内部使用互斥锁保护服务映射表的并发访问。
     */
    class ServiceManager
    {
    public:
        /**
         * @brief 获取单例实例（线程安全）
         * @return ServiceManager& 唯一的单例引用
         */
        static ServiceManager &GetInstance()
        {
            static ServiceManager instance;  // C++11 保证静态局部变量初始化线程安全
            return instance;
        }

        /**
         * @brief 注册一个服务
         * @param service 要注册的服务智能指针，不能为空
         * @return true 注册成功；false 服务为空或已存在同名服务
         * 
         * @note 服务名通过 service->GetServiceName() 获取，重复注册会被拒绝并记录警告日志
         */
        bool RegisterService(std::shared_ptr<Service> service)
        {
            if (!service)
            {
                LOG_ERROR("register null service");
                return false;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            std::string service_name = service->GetServiceName();

            // 检查服务是否已经注册
            if (services_.find(service_name) != services_.end())
            {
                LOG_WARN("service already exists: {}", service_name);
                return false;
            }

            services_[service_name] = service;

            return true;
        }

        /**
         * @brief 根据服务名获取服务实例
         * @param service_name 服务名称
         * @return std::shared_ptr<Service> 服务指针，若未找到则返回 nullptr 并记录警告
         */
        std::shared_ptr<Service> GetService(const std::string &service_name)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = services_.find(service_name);
            if (it != services_.end())
            {
                return it->second;
            }
            LOG_WARN("service not found get service: {}", service_name);
            return nullptr;
        }

        /**
         * @brief 异步处理 RPC 请求（推荐使用）
         * @param service_name 服务名
         * @param method_name 方法名
         * @param args 请求参数（序列化后的字符串）
         * @param result 用于接收结果的 shared_ptr<string>（由调用方保证生命周期）
         * @return std::future<bool> 异步任务的 future，可通过 get() 获取处理结果（true/false）
         * 
         * @note 内部自动确保线程池已初始化（若未初始化则使用 CPU 核心数创建）
         *       任务以高优先级（HIGH）提交到线程池，适合对响应时间敏感的 RPC 场景
         */
        std::future<bool> HandleRpcRequestAsync(const std::string &service_name,
                                               const std::string &method_name,
                                               const std::string &args,
                                               std::shared_ptr<std::string> result)
        {
            // 确保线程池已初始化（若不存在则创建，线程数 = CPU 核心数）
            if (!meeting_ctrl::ThreadPoolSingleton::exists()) {
                meeting_ctrl::ThreadPoolSingleton::init(std::thread::hardware_concurrency());
                // LOG_INFO("ThreadPool initialized with {} threads", std::thread::hardware_concurrency());
            }

            // 使用高优先级将任务提交到线程池，任务内部调用同步处理函数
            return meeting_ctrl::ThreadPoolSingleton::getInstance().enqueue(
                meeting_ctrl::TaskPriority::HIGH,
                [this, service_name, method_name, args, result]() -> bool {
                    return this->HandleRpcRequestSync(service_name, method_name, args, *result);
                }
            );
        }

        /**
         * @brief 同步处理 RPC 请求（向后兼容接口）
         * @param service_name 服务名
         * @param method_name 方法名
         * @param args 请求参数
         * @param result 输出参数，用于接收结果字符串
         * @return true 处理成功；false 失败（服务不存在或方法调用出错）
         * 
         * @note 该接口直接在当前线程中执行，不会使用线程池。
         *       内部实现与 HandleRpcRequestAsync 中的同步部分完全一致。
         */
        bool HandleRpcRequest(const std::string &service_name,
                              const std::string &method_name,
                              const std::string &args,
                              std::string &result)
        {
            return HandleRpcRequestSync(service_name, method_name, args, result);
        }

        /**
         * @brief 获取线程池的统计信息（如队列大小、活跃线程数等）
         * @return meeting_ctrl::ThreadPool::Stats 统计结构体
         * 
         * @note 若线程池未初始化，返回空的 Stats（所有字段为0）
         */
        meeting_ctrl::ThreadPool::Stats GetThreadPoolStats() const
        {
            if (meeting_ctrl::ThreadPoolSingleton::exists()) {
                return meeting_ctrl::ThreadPoolSingleton::getInstance().get_stats();
            }
            return meeting_ctrl::ThreadPool::Stats{};
        }

    private:
        /**
         * @brief 私有构造函数（单例模式）
         */
        ServiceManager() = default;

        // 禁止拷贝和赋值（单例模式）
        ServiceManager(const ServiceManager &) = delete;
        ServiceManager &operator=(const ServiceManager &) = delete;

        /**
         * @brief 同步处理 RPC 请求的内部实现
         * @param service_name 服务名
         * @param method_name 方法名
         * @param args 请求参数
         * @param result 输出引用，接收结果
         * @return true 成功，false 失败
         * 
         * @note 该函数被同步接口和异步任务的 lambda 共同调用
         */
        bool HandleRpcRequestSync(const std::string &service_name,
                                 const std::string &method_name,
                                 const std::string &args,
                                 std::string &result)
        {
            auto service = GetService(service_name);
            if (!service)
            {
                LOG_ERROR("service not found handle rpc request: {}", service_name);
                return false;
            }

            if (!service->HandleRequest(method_name, args, result))
            {
                LOG_ERROR("handle request failed: service={}, method={}",
                          service_name, method_name);
                return false;
            }

            return true;
        }

        // 服务映射表：服务名 -> 服务实例智能指针
        std::unordered_map<std::string, std::shared_ptr<Service>> services_;

        // 互斥锁，保证对 services_ 的并发访问安全（读/写均需加锁）
        mutable std::mutex mutex_;
    };

} // namespace cookrpc