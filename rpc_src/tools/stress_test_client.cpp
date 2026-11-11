#include "../core/rpc_client.h"
#include "../core/error_code.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <future>
#include <iomanip>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace cookrpc;

// 全局统计变量（线程安全的原子操作）
std::atomic<int> total_requests{0};        // 总请求数
std::atomic<int> success_count{0};         // 成功请求数
std::atomic<int> error_count{0};           // 失败请求数
std::atomic<long long> total_response_time{0}; // 总响应时间（微秒）

// 全量延迟样本（每个线程本地收集，结束后合并，避免逐请求加锁影响测量）
std::mutex g_latency_mutex;
std::vector<long long> g_latencies;

// 计算分位值：sorted 为升序样本，p 为百分位（如 99 表示 P99）
double Percentile(const std::vector<long long> &sorted, double p)
{
    if (sorted.empty())
    {
        return 0.0;
    }
    size_t index = static_cast<size_t>(std::ceil(p / 100.0 * sorted.size())) - 1;
    if (index >= sorted.size())
    {
        index = sorted.size() - 1;
    }
    return sorted[index] / 1000.0; // 微秒转毫秒
}

/**
 * @brief 压测配置结构体，具体参数自己配置哈
 * @description 包含所有压测相关的配置参数
 */
struct StressTestConfig {
    int num_requests = 100;           // 总请求数（基于请求数测试时使用）
    int concurrent_clients = 10;      // 并发客户端数量
    int test_duration_seconds = 0;    // 测试时长（秒），0表示按请求数测试
    int interval_ms = 1;              // 每请求后的间隔（毫秒），0表示不等待（饱和压测）
    std::string server_config = "/usr/team_project/rpc-tutorial/config/rpc_client.json"; // 这里建议配置成相对路径
    bool verbose = false;             // 是否显示详细日志
};

/**
 * @brief 执行单个RPC请求并统计性能数据
 * @param client RPC客户端引用
 * @param request_id 请求的唯一标识ID
 * @param verbose 是否输出详细日志
 */
void performSingleRequest(RpcClient& client, int request_id, bool verbose,
                          std::vector<long long>& latencies) {
    // 记录请求开始时间
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        // 构造测试请求数据
        json request;
        request["message"] = "stress test request " + std::to_string(request_id);
        request["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        json response;
        // 调用RPC服务的Echo方法
        bool success = client.Call<json, json>(
            "RpcService",     // 服务名
            "Echo",          // 方法名
            request,         // 请求参数
            SerializeType::JSON, // 序列化类型
            response         // 响应结果
        );
        
        // 记录请求结束时间并计算耗时
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // 原子操作更新全局统计数据
        total_requests.fetch_add(1);
        total_response_time.fetch_add(duration.count());
        latencies.push_back(duration.count());
        
        if (success) {
            success_count.fetch_add(1);
            if (verbose) {
                std::cout << "[请求 " << request_id << "] 成功 - 耗时: " 
                         << duration.count() / 1000.0 << "ms" << std::endl;
            }
        } else {
            error_count.fetch_add(1);
            if (verbose) {
                std::cout << "[请求 " << request_id << "] 失败" << std::endl;
            }
        }
    } catch (const std::exception& e) {
        // 异常处理：记录错误并更新统计数据
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        total_requests.fetch_add(1);
        total_response_time.fetch_add(duration.count());
        latencies.push_back(duration.count());
        error_count.fetch_add(1);
        
        if (verbose) {
            std::cout << "[请求 " << request_id << "] 异常: " << e.what() << std::endl;
        }
    }
}

/**
 * @brief 客户端工作线程函数
 * @param worker_id 工作线程ID
 * @param config 压测配置参数
 * @description 每个工作线程创建一个RPC客户端，根据配置进行压测
 */
void clientWorker(int worker_id, const StressTestConfig& config) {
    try {
        // 为每个工作线程创建独立的RPC客户端
        RpcClient client(config.server_config);
        std::vector<long long> local_latencies;
        local_latencies.reserve(65536);
        
        if (config.test_duration_seconds > 0) {
            // 基于时间的测试模式
            auto start_time = std::chrono::steady_clock::now();
            auto end_time = start_time + std::chrono::seconds(config.test_duration_seconds);
            
            int request_id = 0;
            // 在指定时间内持续发送请求
            while (std::chrono::steady_clock::now() < end_time) {
                performSingleRequest(client, worker_id * 100000 + request_id, config.verbose, local_latencies);
                request_id++;
                
                // 请求间隔：0 表示不等待，用于测服务端饱和吞吐
                if (config.interval_ms > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(config.interval_ms));
                }
            }
        } else {
            // 基于请求数的测试模式
            int requests_per_client = config.num_requests / config.concurrent_clients;
            // 将余数分配给第一个客户端，确保请求总数正确
            if (worker_id == 0) {
                requests_per_client += config.num_requests % config.concurrent_clients;
            }
            
            // 发送指定数量的请求
            for (int i = 0; i < requests_per_client; i++) {
                performSingleRequest(client, worker_id * 100000 + i, config.verbose, local_latencies);
                
                // 请求间隔：0 表示不等待，用于测服务端饱和吞吐
                if (config.interval_ms > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(config.interval_ms));
                }
            }
        }
        
        // 合并本线程的延迟样本（只加一次锁，避免影响压测过程）
        {
            std::lock_guard<std::mutex> lock(g_latency_mutex);
            g_latencies.insert(g_latencies.end(), local_latencies.begin(), local_latencies.end());
        }

        if (config.verbose) {
            std::cout << "[客户端 " << worker_id << "] 完成" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[客户端 " << worker_id << "] 错误: " << e.what() << std::endl;
    }
}

/**
 * @brief 显示帮助信息
 * @param program_name 程序名称
 */
void showHelp(const char* program_name) {
    std::cout << "RPC 压测客户端" << std::endl;
    std::cout << "使用方法: " << program_name << " [选项]" << std::endl;
    std::cout << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -n, --requests NUM     总请求数 (默认: 100)" << std::endl;
    std::cout << "  -c, --clients NUM      并发客户端数 (默认: 10)" << std::endl;
    std::cout << "  -t, --time SECONDS     测试时长(秒), 0表示按请求数测试 (默认: 0)" << std::endl;
    std::cout << "  -i, --interval MS      每请求后的间隔(毫秒), 0表示不等待/饱和压测 (默认: 1)" << std::endl;
    std::cout << "  -f, --config FILE      客户端配置文件 (默认: ../config/rpc_client.json)" << std::endl;
    std::cout << "  -v, --verbose          显示详细日志" << std::endl;
    std::cout << "  -h, --help             显示帮助信息" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << program_name << " -n 1000 -c 50" << std::endl;
    std::cout << "  " << program_name << " -t 60 -c 20 -v" << std::endl;
}

/**
 * @brief 解析命令行参数
 * @param argc 参数个数
 * @param argv 参数数组
 * @return StressTestConfig 解析后的配置结构体
 */
StressTestConfig parseArgs(int argc, char* argv[]) {
    StressTestConfig config;
    
    // 遍历所有命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-n" || arg == "--requests") {
            if (i + 1 < argc) {
                config.num_requests = std::stoi(argv[++i]);
            }
        } else if (arg == "-c" || arg == "--clients") {
            if (i + 1 < argc) {
                config.concurrent_clients = std::stoi(argv[++i]);
            }
        } else if (arg == "-t" || arg == "--time") {
            if (i + 1 < argc) {
                config.test_duration_seconds = std::stoi(argv[++i]);
            }
        } else if (arg == "-i" || arg == "--interval") {
            if (i + 1 < argc) {
                config.interval_ms = std::stoi(argv[++i]);
            }
        } else if (arg == "-f" || arg == "--config") {
            if (i + 1 < argc) {
                config.server_config = argv[++i];
            }
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "-h" || arg == "--help") {
            showHelp(argv[0]);
            exit(0);
        } else {
            std::cerr << "未知参数: " << arg << std::endl;
            showHelp(argv[0]);
            exit(1);
        }
    }
    
    return config;
}

/**
 * @brief 主函数
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return int 程序退出码，0表示成功
 */
int main(int argc, char* argv[]) {
    try {
        // 解析命令行参数
        StressTestConfig config = parseArgs(argc, argv);
        
        // 验证参数合法性
        if (config.num_requests <= 0 || config.concurrent_clients <= 0) {
            std::cerr << "错误: 请求数和并发数必须大于0" << std::endl;
            return 1;
        }
        
        if (config.concurrent_clients > config.num_requests && config.test_duration_seconds == 0) {
            std::cerr << "错误: 并发数不能大于总请求数（基于请求数测试时）" << std::endl;
            return 1;
        }

        if (config.interval_ms < 0) {
            std::cerr << "错误: 请求间隔不能为负数" << std::endl;
            return 1;
        }
        
        // 显示测试配置
        std::cout << "========== 压测配置 ==========" << std::endl;
        if (config.test_duration_seconds > 0) {
            std::cout << "测试模式: 基于时间 (" << config.test_duration_seconds << "秒)" << std::endl;
        } else {
            std::cout << "测试模式: 基于请求数 (" << config.num_requests << "个请求)" << std::endl;
        }
        std::cout << "并发客户端: " << config.concurrent_clients << std::endl;
        std::cout << "请求间隔: " << config.interval_ms << " ms" << std::endl;
        std::cout << "服务器配置: " << config.server_config << std::endl;
        std::cout << "=============================" << std::endl;
        
        // 记录压测开始时间
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // 启动多个工作线程进行并发压测
        std::vector<std::thread> workers;
        for (int i = 0; i < config.concurrent_clients; i++) {
            workers.emplace_back(clientWorker, i, std::ref(config));
        }
        
        // 等待所有工作线程完成
        for (auto& worker : workers) {
            worker.join();
        }
        
        // 记录压测结束时间
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // 计算统计信息
        int total_reqs = total_requests.load();
        int success = success_count.load();
        int errors = error_count.load();
        long long total_time_us = total_response_time.load();
        
        double test_duration_sec = duration.count() / 1000.0;              // 测试总耗时（秒）
        double success_rate = total_reqs > 0 ? (double)success / total_reqs * 100.0 : 0.0; // 成功率
        double qps = test_duration_sec > 0 ? success / test_duration_sec : 0.0;   // 每秒查询数（QPS）
        double avg_response_time = total_reqs > 0 ? (double)total_time_us / total_reqs / 1000.0 : 0.0; // 平均响应时间（毫秒）

        // 计算延迟分位（P50 / P90 / P99）
        std::sort(g_latencies.begin(), g_latencies.end());
        double p50 = Percentile(g_latencies, 50);
        double p90 = Percentile(g_latencies, 90);
        double p99 = Percentile(g_latencies, 99);
        double max_latency = g_latencies.empty() ? 0.0 : g_latencies.back() / 1000.0;
        size_t sample_count = g_latencies.size();
        
        // 输出压测结果
        std::cout << std::endl;
        std::cout << "========== 压测结果 ==========" << std::endl;
        std::cout << "测试时长: " << test_duration_sec << " 秒" << std::endl;
        std::cout << "总请求数: " << total_reqs << std::endl;
        std::cout << "成功请求: " << success << std::endl;
        std::cout << "失败请求: " << errors << std::endl;
        std::cout << "成功率: " << std::fixed << std::setprecision(2) << success_rate << "%" << std::endl;
        std::cout << "QPS: " << std::fixed << std::setprecision(2) << qps << std::endl;
        std::cout << "平均响应时间: " << std::fixed << std::setprecision(2) << avg_response_time << " ms" << std::endl;
        std::cout << "P50 响应时间: " << std::fixed << std::setprecision(2) << p50 << " ms" << std::endl;
        std::cout << "P90 响应时间: " << std::fixed << std::setprecision(2) << p90 << " ms" << std::endl;
        std::cout << "P99 响应时间: " << std::fixed << std::setprecision(2) << p99 << " ms" << std::endl;
        std::cout << "最大响应时间: " << std::fixed << std::setprecision(2) << max_latency << " ms" << std::endl;
        std::cout << "延迟样本数: " << sample_count << std::endl;
        std::cout << "=============================" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "压测失败: " << e.what() << std::endl;
        return 1;
    }
} 