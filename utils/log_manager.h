#pragma once
#include <memory>
#include <string>

#include <memory>
#include <string>
#include <filesystem>
#include <iostream>
#include "spdlog/spdlog.h"
// 添加以下头文件
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "fmt/format.h"
// ... rest of the code ...


// 定义宏，使用更方便
#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_FATAL(...) SPDLOG_CRITICAL(__VA_ARGS__)

// 客户端专用日志宏，使用INFO级别但添加[CLIENT]前缀
#define CLIENT_INFO(...) SPDLOG_INFO("[CLIENT] " __VA_ARGS__)
#define CLIENT_DEBUG(...) SPDLOG_DEBUG("[CLIENT] " __VA_ARGS__)
#define CLIENT_WARN(...) SPDLOG_WARN("[CLIENT] " __VA_ARGS__)
#define CLIENT_ERROR(...) SPDLOG_ERROR("[CLIENT] " __VA_ARGS__)
#define CLIENT_FATAL(...) SPDLOG_CRITICAL("[CLIENT] " __VA_ARGS__)

template <>
struct fmt::formatter<std::thread::id> : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(const std::thread::id & id, FormatContext& cxt) {
        std::ostringstream oss;
        oss << id;
        return fmt::formatter<std::string>::format(oss.str(), cxt);
    }
};

class Logger {
public:
    static Logger &GetInstance() {
        static Logger instance;
        return instance;
    }

    /// @brief 初始化日志记录器
    /// @param log_dir 日志目录，默认 "../logs"
    /// @return true 如果初始化成功，否则返回 false
    bool Init(const std::string &log_dir = "../logs") 
    {
        try
        {
            // 创建日志目录
            std::filesystem::path log_path(log_dir);
            if (!std::filesystem::exists(log_path))
            {
                if (!std::filesystem::create_directories(log_path))
                {
                    std::cerr << "Failed to create log directory: " << log_dir << std::endl;
                    return false;
                }
            }

            // 创建控制台和文件日志记录器
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            if (!console_sink)
            {
                std::cerr << "Failed to create console sink" << std::endl;
                return false;
            }

            std::string log_file = log_dir + "/cookrpc.log";
            auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                log_file, 0, 0);
            if (!file_sink)
            {
                std::cerr << "Failed to create file sink: " << log_file << std::endl;
                return false;
            }

            std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink}; // 需要先把控制台日志器添加到vector中 随后才能够添加进logger
            logger_ = std::make_shared<spdlog::logger>("main", sinks.begin(), sinks.end());
            if (!logger_)
            {
                std::cerr << "Failed to create logger" << std::endl;
                return false;
            }

            // 设置日志格式
            logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
            logger_->set_level(spdlog::level::trace);

            // 设置为默认logger
            spdlog::set_default_logger(logger_);

            // 测试日志是否可写
            try
            {
                // LOG_INFO("Logger initialized successfully");
            }
            catch (const std::exception &e)
            {
                std::cerr << "Failed to write test log: " << e.what() << std::endl;
                return false;
            }

            return true;
        }
        catch (const std::exception &e) // 捕获所有异常
        {
            std::cerr << "Logger initialization failed: " << e.what() << std::endl;
            return false;
        }
    }



    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

private:
    Logger() = default;
    ~Logger() = default;
    
    std::shared_ptr<spdlog::logger> logger_;
};