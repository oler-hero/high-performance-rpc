#pragma once

#include "thread_pool.h"
#include <memory>
#include <mutex>
#include <chrono>
#include <future>

namespace cookrpc::threadpool {

/**
 * @brief 线程池单例类
 * @details 
 * 
 * **设计目标：**
 * 提供全局唯一的线程池实例，避免多个线程池竞争CPU资源，
 * 同时提供便捷的全局访问接口。
 * 
 * **单例模式特点：**
 * 1. **延迟初始化**: 第一次调用getInstance()时才创建实例
 * 2. **线程安全**: 使用互斥锁保护所有静态操作
 * 3. **配置灵活**: 支持使用不同配置初始化
 * 4. **生命周期管理**: 提供优雅关闭和强制销毁接口
 * 5. **状态查询**: 可以查询线程池是否存在和当前状态
 * 
 * **使用场景：**
 * - 全局任务调度中心
 * - 避免创建多个线程池导致的资源浪费
 * - 需要跨模块共享线程池的场景
 * 
 * **注意事项：**
 * - 所有方法都是线程安全的
 * - 初始化只能成功一次，重复初始化会返回false
 * - 销毁后可以重新初始化
 */
class ThreadPoolSingleton {
public:
    /**
     * @brief 使用线程数初始化线程池
     * @param threads 线程数量，默认为硬件并发数
     * @return 是否成功初始化（false表示已经初始化过）
     * 
     * @details 创建固定大小的线程池单例。
     * 如果已经初始化，则返回false，不会重新创建。
     * 这种设计避免了意外的重复初始化。
     */
    static bool init(size_t threads = std::thread::hardware_concurrency()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (instance_) {
            return false;  // 已经初始化
        }
        instance_ = std::make_unique<ThreadPool>(threads);
        return true;
    }

    /**
     * @brief 使用配置初始化线程池
     * @param config 线程池配置参数
     * @return 是否成功初始化（false表示已经初始化过）
     * 
     * @details 创建可动态调整大小的线程池单例。
     * 支持更丰富的配置选项，如动态线程管理、队列大小限制等。
     */
    static bool init(const ThreadPoolStruct& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (instance_) {
            return false;  // 已经初始化
        }
        instance_ = std::make_unique<ThreadPool>(config);
        return true;
    }

    /**
     * @brief 获取线程池实例（懒初始化）
     * @return ThreadPool引用
     * 
     * @details 如果未初始化，则使用默认参数自动初始化。
     * 这种懒初始化模式确保在任何时候调用都能获得可用的线程池。
     * 
     * **线程安全保证：**
     * 使用互斥锁确保多线程环境下的安全初始化。
     */
    static ThreadPool& getInstance() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!instance_) {
            instance_ = std::make_unique<ThreadPool>(std::thread::hardware_concurrency());
        }
        return *instance_;
    }

    /**
     * @brief 使用配置获取线程池实例
     * @param config 如果需要初始化时使用的配置
     * @return ThreadPool引用
     * 
     * @details 如果未初始化，则使用提供的配置进行初始化。
     * 如果已经初始化，config参数将被忽略。
     */
    static ThreadPool& getInstance(const ThreadPoolStruct& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!instance_) {
            instance_ = std::make_unique<ThreadPool>(config);
        }
        return *instance_;
    }

    /**
     * @brief 便捷方法：提交任务到单例线程池
     * @tparam F 函数类型
     * @tparam Args 参数类型
     * @param priority 任务优先级
     * @param f 任务函数
     * @param args 任务函数参数
     * @return std::future 用于获取任务结果
     * 
     * @details 这是一个便捷的静态方法，避免了先获取实例再提交任务的两步操作。
     * 内部会自动调用getInstance()确保线程池可用。
     * 
     * **使用示例：**
     * ```cpp
     * auto future = ThreadPoolSingleton::enqueue(TaskPriority::HIGH, [](){
     *     return 42;
     * });
     * int result = future.get();
     * ```
     */
    template<class F, class... Args>
    static auto enqueue(TaskPriority priority, F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        return getInstance().enqueue(priority, std::forward<F>(f), std::forward<Args>(args)...);
    }

    /**
     * @brief 获取线程池统计信息
     * @return Stats结构体，如果线程池不存在则返回空统计
     * 
     * @details 线程安全地获取性能统计信息。
     * 如果线程池未初始化，返回默认的空统计信息。
     */
    static ThreadPool::Stats getStats() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!instance_) {
            return ThreadPool::Stats{};  // 返回空统计
        }
        return instance_->get_stats();
    }

    /**
     * @brief 获取当前队列大小
     * @return 队列中的任务数量，如果线程池不存在则返回0
     */
    static size_t getQueueSize() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!instance_) {
            return 0;
        }
        return instance_->queue_size();
    }

    /**
     * @brief 获取线程池大小
     * @return 工作线程数量，如果线程池不存在则返回0
     */
    static size_t getPoolSize() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!instance_) {
            return 0;
        }
        return instance_->size();
    }

    /**
     * @brief 暂停线程池
     * @return 是否成功暂停（false表示线程池不存在）
     * 
     * @details 暂停后线程继续存活但不处理新任务。
     * 适用于临时需要停止处理任务的场景。
     */
    static bool pause() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!instance_) {
            return false;
        }
        instance_->pause();
        return true;
    }

    /**
     * @brief 恢复线程池
     * @return 是否成功恢复（false表示线程池不存在）
     * 
     * @details 从暂停状态恢复到正常运行状态。
     */
    static bool resume() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!instance_) {
            return false;
        }
        instance_->resume();
        return true;
    }

    /**
     * @brief 优雅关闭线程池
     * @param timeout 等待超时时间，默认无限等待
     * @return 是否成功关闭（true表示所有任务都完成了）
     * 
     * @details 优雅关闭流程：
     * 1. 完成所有队列中的任务
     * 2. 等待所有活跃任务完成
     * 3. 关闭所有线程
     * 4. 释放线程池实例
     * 
     * **与destroy()的区别：**
     * - shutdown()会等待任务完成
     * - destroy()会立即终止并丢弃未完成的任务
     * 
     * **重要提示：**
     * 成功关闭后，实例会被销毁，可以重新初始化。
     */
    static bool shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!instance_) {
            return true; // 已经是关闭状态
        }
        
        // 优雅关闭
        bool success = instance_->shutdown(timeout);
        if (success) {
            instance_.reset(); // 成功关闭后释放资源
        }
        return success;
    }

    /**
     * @brief 立即销毁线程池实例
     * 
     * @details 强制销毁流程：
     * 1. 立即停止所有线程
     * 2. 丢弃所有未完成的任务
     * 3. 释放所有资源
     * 4. 重置实例指针
     * 
     * **使用场景：**
     * - 程序即将退出
     * - 需要立即释放资源
     * - 不在乎未完成任务的情况
     * 
     * **注意：**
     * 调用后可以重新初始化线程池。
     */
    static void destroy() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (instance_) {
            instance_->stop_now(); // 立即停止
            instance_.reset();     // 释放资源
        }
    }

    /**
     * @brief 检查线程池是否存在
     * @return true表示已初始化，false表示未初始化
     * 
     * @details 用于在调用其他方法前检查线程池状态。
     * 线程安全的查询操作。
     */
    static bool exists() {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<bool>(instance_);
    }

    /**
     * @brief 获取线程池状态
     * @return ThreadPoolState，如果不存在则返回STOPPED
     * 
     * @details 安全地查询线程池当前状态。
     * 如果线程池未初始化，返回STOPPED状态。
     */
    static ThreadPoolState getState() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!instance_) {
            return ThreadPoolState::STOPPED;
        }
        return instance_->get_state();
    }

    // 禁止拷贝和赋值 - 单例模式的基本要求
    ThreadPoolSingleton(const ThreadPoolSingleton&) = delete;
    ThreadPoolSingleton& operator=(const ThreadPoolSingleton&) = delete;

private:
    /**
     * @brief 私有构造函数
     * @details 防止外部直接创建实例，确保单例模式的正确性
     */
    ThreadPoolSingleton() = default;
    
    /**
     * @brief 线程池实例
     * @details 使用unique_ptr管理生命周期，支持自动资源释放
     */
    static std::unique_ptr<ThreadPool> instance_;
    
    /**
     * @brief 全局互斥锁
     * @details 保护所有静态操作的线程安全性。
     * 注意：这个锁保护的是单例的创建/销毁操作，
     * 不是线程池内部的操作（线程池有自己的锁）
     */
    static std::mutex mutex_;
};

} // namespace cookrpc::threadpool
