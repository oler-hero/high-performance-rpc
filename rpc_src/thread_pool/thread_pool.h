#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <atomic>
#include <chrono>
#include "log_manager.h"
#include <iostream>
#include <optional>

namespace cookrpc::threadpool {

/**
 * @brief 任务优先级枚举
 * @details 支持三种优先级，用于优先级队列排序
 * HIGH > NORMAL > LOW，数值越大优先级越高
 */
enum class TaskPriority {
    LOW,        ///< 低优先级任务
    NORMAL,     ///< 普通优先级任务
    HIGH        ///< 高优先级任务
};

/**
 * @brief 线程池状态枚举
 * @details 线程池生命周期状态管理，支持原子操作
 */
enum class ThreadPoolState {
    RUNNING,        ///< 正常运行状态，接受新任务
    PAUSED,         ///< 暂停状态，不处理新任务但保持线程存活
    SHUTTING_DOWN,  ///< 优雅关闭中，完成队列中已有任务但不接受新任务
    STOPPED         ///< 已停止，所有线程退出
};

/**
 * @brief 线程池配置参数结构体
 * @details 支持动态线程管理和队列大小限制
 */
struct ThreadPoolStruct {
    size_t core_threads = std::thread::hardware_concurrency();      ///< 核心线程数
    size_t max_threads = std::thread::hardware_concurrency() * 2;   ///< 最大线程数
    size_t max_queue_size = 1000;                                   ///< 最大队列大小，0表示无限制
    std::chrono::milliseconds keep_alive_time{60000};               ///< 非核心线程空闲超时时间(60秒)
};

/**
 * @brief 任务包装器类
 * @details 巧妙设计：
 * 1. 封装std::function，支持任意可调用对象
 * 2. 内置优先级，配合std::priority_queue实现任务优先级调度
 * 3. 提供valid()检查，验证任务的有效性
 * 4. 重载operator<，用于优先级队列的排序（注意：优先级队列是大顶堆，所以这里是反向比较）
 */
class TaskWrapper {
public:
    /**
     * @brief 构造函数
     * @param t 任务函数对象
     * @param p 任务优先级
     */
    TaskWrapper(std::function<void()>&& t, TaskPriority p)
        : task_(std::move(t))
        , priority_(p)
    {}

    /**
     * @brief 默认构造函数，创建空任务
     * @details 用于在worker_thread中初始化局部变量，避免不必要的复制
     */
    TaskWrapper() : priority_(TaskPriority::NORMAL) {}

    /**
     * @brief 执行任务 
     * @details 先检查任务有效性，防止执行空函数导致崩溃
     */
    void execute() { 
        if (valid()) {
            task_(); 
        }
    }
    
    /**
     * @brief 检查任务是否有效
     * @return true表示任务可执行，false表示空任务
     */
    bool valid() const { 
        return static_cast<bool>(task_); 
    }
    
    /**
     * @brief 优先级比较运算符
     * @details 保证 HIGH 优先级的任务会排在队列顶部
     * @param other 另一个任务包装器
     * @return true表示当前任务优先级低于other
     */
    bool operator<(const TaskWrapper& other) const {
        return priority_ < other.priority_;
    }

private:
    std::function<void()> task_;     ///< 任务函数对象
    TaskPriority priority_;          ///< 任务优先级
};

/**
 * @brief 高性能线程池类
 * @details 特性：
 * 1. 支持任务优先级调度
 * 2. 动态线程管理（核心线程+按需创建）
 * 3. 多种线程池状态管理
 * 4. 队列大小限制和背压控制
 * 5. 优雅关闭和强制停止
 */
class ThreadPool {
public:
    /**
     * @brief 线程池统计信息结构体
     * @details 提供运行时性能监控数据
     */
    struct Stats {
        size_t tasks_completed = 0;    ///< 已完成任务数
        size_t tasks_failed = 0;       ///< 失败任务数
        double avg_task_time_ms = 0;   ///< 平均任务执行时间（毫秒）
        size_t active_threads = 0;     ///< 当前活跃线程数
    };

    /**
     * @brief 构造线程池（固定大小）
     * @param threads 线程数量，默认为硬件支持的并发线程数, 这块的内容是在配置文件中配置的
     */
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency());

    /**
     * @brief 使用配置构造线程池（动态大小）
     * @param config 线程池配置参数
     * @details 支持动态线程管理，根据负载自动调整线程数量
     */
    explicit ThreadPool(const ThreadPoolStruct& config);

    // 向线程池提交任务
    template<class F, class... Args>
    auto enqueue(TaskPriority priority, F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    // 暂停线程池：暂停后工作线程不取新任务，但保持存活
    void pause();

    // 恢复线程池
    void resume();

    // 获取线程池统计信息
    Stats get_stats() const;

    // 获取线程池大小
    size_t size() const noexcept { return workers_.size(); }

    // 获取队列中的任务数量
    size_t queue_size() const noexcept;

    /**
     * @brief 优雅关闭线程池
     * @param wait_timeout_ms 等待超时时间(毫秒)，默认无限等待
     * @return 是否成功关闭（true表示所有任务都完成了）
     * 
     * @details 优雅关闭流程：
     * 1. 设置状态为SHUTTING_DOWN，不再接受新任务
     * 2. 等待所有队列中的任务完成
     * 3. 等待所有活跃线程空闲
     * 4. 设置stop标志，通知所有线程退出
     * 5. 等待所有线程join
     */
    bool shutdown(std::chrono::milliseconds wait_timeout_ms = std::chrono::milliseconds::max());

    // 立即停止线程池
    void stop_now();

    // 获取线程池状态
    ThreadPoolState get_state() const noexcept;

    // 析构函数
    ~ThreadPool();

    // 禁止拷贝和赋值 - 线程池是重资源对象，不应该被复制
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    // 工作线程主函数
    void worker_thread();
    
    void adjust_thread_count();
    
    // === 线程管理 ===
    std::vector<std::thread> workers_;                                    ///< 工作线程容器
    std::atomic<size_t> current_threads_{0};                             ///< 当前线程数（原子操作）
    std::vector<std::chrono::steady_clock::time_point> thread_last_active_; ///< 线程最后活跃时间（用于超时管理）
    
    // === 任务队列 ===
    std::priority_queue<TaskWrapper> tasks_;                             ///< 优先级任务队列
    
    // === 配置 ===
    ThreadPoolStruct config_;                                           ///< 线程池配置参数
    
    // === 同步控制 ===
    mutable std::mutex queue_mutex;                                     ///< 队列访问互斥锁
    std::condition_variable condition_;                                  ///< 任务可用条件变量
    std::condition_variable not_full_condition_;                        ///< 队列非满条件变量（背压控制）
    bool stop_{false};                                                          ///< 停止标志
    
    // === 状态管理 ===
    std::atomic<ThreadPoolState> state_{ThreadPoolState::RUNNING};      ///< 线程池状态（原子操作）
    
    // === 统计信息（线程安全） ===
    mutable std::mutex stats_mutex_;                                    ///< 统计信息互斥锁
    std::atomic<size_t> tasks_completed_{0};                           ///< 已完成任务数
    std::atomic<size_t> tasks_failed_{0};                              ///< 失败任务数
    std::atomic<size_t> active_threads_{0};                            ///< 当前活跃线程数
    double total_task_time_{0};                                         ///< 总任务执行时间（需要互斥锁保护）
};

/**
 * @brief 模板方法实现 - 任务提交
 * @details 线程池核心接口：类型推导、完美转发、异步结果、背压控制与动态扩容
 */
template<class F, class... Args>
auto ThreadPool::enqueue(TaskPriority priority, F&& f, Args&&... args) 
    -> std::future<typename std::invoke_result<F, Args...>::type>
{
    using return_type = typename std::invoke_result<F, Args...>::type;

    // 创建任务包装器 
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    // 创建一个与任务结果关联的 future，用于获取任务的返回值
    std::future<return_type> res = task->get_future();
    
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        // 检查线程池状态
        if(stop_) {
            return std::future<return_type>{};
        }

        if(config_.max_queue_size > 0) {
            // 当队列已满（tasks.size() >= max_queue_size）时，生产者线程会阻塞等待
            // 当队列有空间（其他线程取出任务后）或线程池停止时，生产者线程被唤醒
            // 唤醒后再次检查条件：
            // 如果队列仍满且未停止，则继续等待
            // 否则继续执行后续代码，通常是向队列添加新任务
            not_full_condition_.wait(lock, [this] {
                return stop_ || tasks_.size() < config_.max_queue_size;
            });
            
            if(stop_) {
                return std::future<return_type>{};
            }
        }

   
        

        // 将任务添加到优先级队列
        tasks_.emplace(
            [task]() { (*task)(); },  // lambda捕获shared_ptr，确保任务生命周期
            priority
        );

        // 记录任务提交信息
        std::string priority_str = (priority == TaskPriority::HIGH) ? "HIGH" : 
                                  (priority == TaskPriority::NORMAL) ? "NORMAL" : "LOW";
        LOG_INFO("[ThreadPool] Enqueuing task with priority: {}, current queue size: {}/{}, active threads: {}/{}", 
                    priority_str, tasks_.size(), config_.max_queue_size, active_threads_.load(), current_threads_.load());
        
        // 动态线程创建逻辑：如果任务数超过活跃线程数且未达到最大线程数，创建新的线程，提升速率
        if(tasks_.size() > active_threads_ && current_threads_ < config_.max_threads) {
            adjust_thread_count();
        }
    }
    
    // 唤醒一个正在等待任务的工作线程
    condition_.notify_one();
    return res;
}

} // namespace cookrpc::threadpool