#include "thread_pool.h"
#include <iostream>
#include <sstream>

namespace meeting_ctrl {



ThreadPool::ThreadPool(size_t threads)
    : stop_(false)
    , state_(ThreadPoolState::RUNNING)
    , tasks_completed_(0)
    , tasks_failed_(0)
    , active_threads_(0)
    , total_task_time_(0.0)
    , current_threads_(0)
{
    // 使用默认配置，但设置核心线程数和最大线程数为指定值
    config_.core_threads = threads;
    config_.max_threads = threads;
    config_.max_queue_size = 0;  // 无限制
    
    // 预分配容器空间，避免动态扩容
    workers_.reserve(threads);
    thread_last_active_.resize(threads);
    
    // 创建工作线程
    for(size_t i = 0; i < threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_thread, this); // 创建工作线程
        thread_last_active_[i] = std::chrono::steady_clock::now();
    }
    current_threads_ = threads;
}

/**
 * @brief 构造可动态调整大小的线程池
 * @param config 线程池配置参数
 * @details 支持动态线程管理，根据负载自动创建和销毁非核心线程
 */
ThreadPool::ThreadPool(const ThreadPoolStruct& config)
    : config_(config)
    , stop_(false)
    , state_(ThreadPoolState::RUNNING)
    , tasks_completed_(0)
    , tasks_failed_(0)
    , active_threads_(0)
    , total_task_time_(0.0)
    , current_threads_(0)
{
    // 为最大线程数预分配空间
    workers_.reserve(config_.max_threads);
    thread_last_active_.resize(config_.max_threads);
    
    // 只创建核心线程，其他线程按需创建
    for(size_t i = 0; i < config_.core_threads; ++i) {
        workers_.emplace_back(&ThreadPool::worker_thread, this); // 创建工作线程
        thread_last_active_[i] = std::chrono::steady_clock::now();
    }
    current_threads_ = config_.core_threads;
}



// 工作线程主函数，线程池的核心函数
void ThreadPool::worker_thread() {
    // 获取当前线程ID并转换为字符串，用于后期的日志标识，方便调试
    std::thread::id thread_id = std::this_thread::get_id();
    std::ostringstream oss;
    oss << thread_id;
    std::string thread_id_str = oss.str();
    
    // 记录线程最后活跃时间，用于超时管理
    auto last_active = std::chrono::steady_clock::now();
    
    while(true) {
        TaskWrapper task_wrapper{};  // 初始化空任务包装器
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // 让线程在指定条件下等待，等待条件是：线程池停止，线程池运行状态且有新任务，线程池优雅关闭状态且有任务
            auto wait_timeout = config_.keep_alive_time; // 等待的最大时间
            bool has_task = condition_.wait_for(lock, wait_timeout, [this] {
                return stop_ || 
                       (state_ == ThreadPoolState::RUNNING && !tasks_.empty()) || 
                       (state_ == ThreadPoolState::SHUTTING_DOWN && !tasks_.empty());
            });
            
            // 线程回收，主要有以下条件：
            // 1. 没有获得任务，
            // 2. 当前线程数超过核心线程数，说明是非核心线程
            // 3. 距离上次活跃时间超过 keep_alive_time
            if(!has_task && current_threads_ > config_.core_threads) {
                auto now = std::chrono::steady_clock::now();
                if(now - last_active > config_.keep_alive_time) {
                    current_threads_--;
                    LOG_INFO("[ThreadPool] Worker thread {} exiting due to timeout, remaining threads: {}", 
                                thread_id_str, current_threads_.load());
                    return;
                }
            }
            
           
            // 线程池已经停止，且任务队列为空，线程退出
            if((stop_ || state_ == ThreadPoolState::STOPPED) && tasks_.empty()) {
                // LOG_INFO("[ThreadPool] Worker thread {} exiting", thread_id_str);
                return;
            }
            
            /**
             * 暂停状态处理
             * 在暂停状态下，线程不处理任务但保持存活，目前项目中没有用到，各位可以执行拓展
             */
            if(state_ == ThreadPoolState::PAUSED) {
                continue;
            }
            
            /**
             * 任务获取
             * 从优先级队列顶部获取最高优先级的任务
             */
            if(!tasks_.empty()) {
                task_wrapper = std::move(const_cast<TaskWrapper&>(tasks_.top()));
                tasks_.pop(); // 获取优先级最高的任务
                last_active = std::chrono::steady_clock::now(); // 更新线程最后活跃时间
                
                LOG_INFO("[ThreadPool] Thread {} picked up task, queue size: {}, active threads: {}", 
                            thread_id_str, tasks_.size(), active_threads_.load() + 1);
                
                /**
                 * 背压控制：通知等待队列空间的生产者线程
                 * 当队列从满状态变为非满状态时（说明有空间了），唤醒等待的 enqueue 调用，注意，这里不是唤醒所有线程，而是唤醒一个线程
                 */
                if(config_.max_queue_size > 0 && tasks_.size() < config_.max_queue_size) {
                    not_full_condition_.notify_one();
                }
            }
        } // 释放锁，在执行任务时不持有锁，减小锁的粒度
        
        // 好了，上面校验完毕之后，开始执行任务啦
        if(task_wrapper.valid()) {
            auto start_time = std::chrono::high_resolution_clock::now();
            active_threads_++;  // 执行任务时才递增，避免在等待任务时递增, 用来统计当前活跃线程数，原子操作
            
            // LOG_INFO("[ThreadPool] Thread {} starting task execution, active threads: {}", 
            //             thread_id_str, active_threads_.load());
            
            try {
                
                // 执行任务
                // 那这个线程池的含义，就是将要执行的函数，包装成任务，放到任务队列里，这就是生产者
                // 线程则是消费者，从任务队列里获取任务后，执行任务里面包装的函数
                task_wrapper.execute(); // 看 wrapper 的 execute 函数，里面调用了 task_ 函数,task_ 看构造函数
                tasks_completed_++; // 任务完成，原子递增任务完成数
                
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                double duration_ms = duration.count() / 1000.0;
                
                LOG_INFO("[ThreadPool] Thread {} completed task successfully in {}ms, total completed: {}", 
                            thread_id_str, duration_ms, tasks_completed_.load());
                
            } catch(const std::exception& e) {
                /**
                 * 异常处理
                 * 任务执行失败不会影响线程池的运行，只是记录统计信息
                 * 异常会通过future传播给调用者
                 */
                tasks_failed_++;
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                double duration_ms = duration.count() / 1000.0;
                
                LOG_INFO("[ThreadPool] Thread {} task failed after {}ms, error: {}, total failed: {}", 
                            thread_id_str, duration_ms, e.what(), tasks_failed_.load());
            }
            
            /**
             * 统计信息更新
             * total_task_time需要互斥锁保护，因为它不是原子类型
             */
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                total_task_time_ += duration.count() / 1000.0; // 转换为毫秒
            }
            
            active_threads_--;  // 原子递减活跃线程数
            
            // LOG_INFO("[ThreadPool] Thread {} finished, active threads: {}", 
                        // thread_id_str, active_threads_.load());
            
            /**
             * 优雅关闭通知
             * 当线程池处于关闭中状态，且所有任务都已完成时，
             * 通知可能在shutdown()中等待的线程
             */
            if(state_ == ThreadPoolState::SHUTTING_DOWN && active_threads_ == 0 && tasks_.empty()) {
                // LOG_INFO("[ThreadPool] All tasks_ completed, notifying shutdown");
                condition_.notify_all();
            }
        }
    } // while(true)
}

/**
 * @brief 暂停线程池
 * @details 使用原子比较交换操作，确保状态转换的原子性
 * 只有在RUNNING状态才能转换到PAUSED状态
 */
void ThreadPool::pause() {
    auto expected = ThreadPoolState::RUNNING;
    if(state_.compare_exchange_strong(expected, ThreadPoolState::PAUSED)) {
        condition_.notify_all();  // 唤醒所有等待的线程，让它们检查新状态
    }
}

/**
 * @brief 恢复线程池
 * @details 只有在PAUSED状态才能转换到RUNNING状态
 */
void ThreadPool::resume() {
    auto expected = ThreadPoolState::PAUSED;
    if(state_.compare_exchange_strong(expected, ThreadPoolState::RUNNING)) {
        condition_.notify_all();  // 唤醒所有线程开始处理任务
    }
}

/**
 * @brief 优雅关闭线程池
 * @param wait_timeout_ms 等待超时时间
 * @return 是否成功关闭（所有任务都完成）
 * 
 * @details 优雅关闭的完整流程：
 * 
 * 第一阶段：状态转换
 * - 使用原子操作从RUNNING转换到SHUTTING_DOWN，这里可以全局搜一下 SHUTTING_DOWN
 * - 这个状态转换是关键：新任务将被拒绝，但现有任务会被完成
 * 
 * 第二阶段：等待任务完成
 * - 使用条件变量等待所有队列任务完成
 * - 等待所有活跃线程空闲
 * - 支持超时机制，避免无限等待，超过时间后，则强制停止
 * 
 * 第三阶段：线程退出
 * - 设置stop标志，通知所有线程退出
 * - 等待所有线程join完成
 * 
 * 与stop_now()的区别：
 * - shutdown()会完成所有已提交的任务
 * - stop_now()会丢弃未完成的任务
 */
bool ThreadPool::shutdown(std::chrono::milliseconds wait_timeout_ms) {
    ThreadPoolState expected = ThreadPoolState::RUNNING;
    // 只有在运行状态才能进入关闭中状态
    if(!state_.compare_exchange_strong(expected, ThreadPoolState::SHUTTING_DOWN)) {
        // 如果已经在关闭或已停止，返回true
        return state_ == ThreadPoolState::STOPPED;
    }
    
    // 通知所有线程继续处理队列中的任务
    condition_.notify_all();
    
    // 等待所有任务完成
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        bool tasks_completed = condition_.wait_for(lock, wait_timeout_ms, [this] {
            return tasks_.empty() && active_threads_ == 0;
        });
        
        if(!tasks_completed) {
            // 超时，未完成所有任务
            return false;
        }
        
        // 设置为已停止状态
        state_ = ThreadPoolState::STOPPED;
        stop_ = true;
    }
    
    // 通知所有线程退出
    condition_.notify_all();
    
    // 等待所有线程结束
    for(std::thread &worker: workers_) {
        if(worker.joinable()) {
            worker.join();
        }
    }
    
    return true;
}

/**
 * @brief 立即停止线程池
 * @details 强制停止，不等待任务完成：这里和 shutdown 的区别是，
 * shutdown 会等待所有任务完成，而 stop_now 会立即停止，不等待任务完成，丢弃未执行的任务
 * 1. 立即清空任务队列（丢弃未执行的任务）
 * 2. 设置停止标志
 * 3. 通知所有线程立即退出
 * 4. 等待所有线程join
 */
void ThreadPool::stop_now() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        state_ = ThreadPoolState::STOPPED;
        stop_ = true;
        
        // 清空任务队列 - 这些任务将被丢弃
        while(!tasks_.empty()) {
            tasks_.pop();
        }
    }
    
    // 通知所有线程
    condition_.notify_all();
    
    // 等待所有线程结束
    for(std::thread &worker: workers_) {
        if(worker.joinable()) {
            worker.join();
        }
    }
}

/**
 * @brief 获取线程池状态
 * @return 当前状态（原子操作，线程安全）
 */
ThreadPoolState ThreadPool::get_state() const noexcept {
    return state_;
}

/**
 * @brief 获取线程池统计信息
 * @return Stats结构体包含各种性能指标
 * @details 线程安全实现：
 * - 原子变量可以直接读取
 * - total_task_time需要互斥锁保护
 * - 平均时间在有任务完成的情况下才计算
 */
ThreadPool::Stats ThreadPool::get_stats() const {
    Stats stats;
    stats.tasks_completed = tasks_completed_;
    stats.tasks_failed = tasks_failed_;
    stats.active_threads = active_threads_;
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if(tasks_completed_ > 0) {
        stats.avg_task_time_ms = total_task_time_ / tasks_completed_;
    }
    
    return stats;
}

/**
 * @brief 获取当前队列大小
 * @return 队列中等待执行的任务数量
 * @details 需要加锁保护，因为队列可能被其他线程修改
 */
size_t ThreadPool::queue_size() const noexcept {
    std::unique_lock<std::mutex> lock(queue_mutex);
    return tasks_.size();
}

/**
 * @brief 析构函数
 * @details 确保线程池正确关闭，防止资源泄漏
 */
ThreadPool::~ThreadPool() {
    // 如果还未停止，立即停止
    if(state_ != ThreadPoolState::STOPPED) {
        stop_now();
    }
}

/**
 * @brief 动态调整线程数量
 * @details 核心的动态扩容逻辑：
 * 
 * 调用前提
 * - 必须在queue_mutex已锁定的情况下调用
 * - 避免了重复加锁的开销
 * 
 * 扩容条件
 * 1. 当前线程数 < 最大线程数（配置文件里配置的，表示仍有扩容空间）
 * 2. 队列中的任务数 > 当前活跃线程数
 * 
 * 线程创建
 * - 使用emplace_back直接在容器中构造线程对象
 * - 记录线程创建时间，用于超时管理
 * - 异常安全：创建失败时回滚current_threads计数
 * 
 * 注意点
 * - 只在真正需要时才创建线程（懒加载）
 * - 使用原子变量避免锁竞争
 */
void ThreadPool::adjust_thread_count() {
    // 在queue_mutex已锁定的情况下调用
    if(current_threads_ < config_.max_threads && tasks_.size() > active_threads_) {
        size_t new_thread_id = current_threads_++;
        if(new_thread_id < thread_last_active_.size()) {
            thread_last_active_[new_thread_id] = std::chrono::steady_clock::now();
        }
        
        try {
            workers_.emplace_back(&ThreadPool::worker_thread, this); // 创建新的工作线程
            // LOG_INFO("[ThreadPool] Created new worker thread, total threads: {}/{}", 
                        // current_threads_.load(), config_.max_threads);
        } catch(const std::exception& e) {
            // 创建失败，回滚计数
            current_threads_--;
            // LOG_INFO("[ThreadPool] Failed to create worker thread: {}", e.what());
        }
    }
}

} // namespace meeting_ctrl 