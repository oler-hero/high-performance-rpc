#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

int main() {
    auto console_logger = spdlog::stdout_color_mt("console");
    console_logger->info("Welcome to spdlog!");

    auto file_logger = spdlog::basic_logger_mt("file_logger", "logs/minirpc.log");
    file_logger->info("This is a file log message");

    spdlog::set_level(spdlog::level::info);
    spdlog::info("This is a global log message");

    try {
        throw std::runtime_error("An error occurred");
    } catch (const std::exception &e) {
        spdlog::error("Exception caught: {}", e.what());
    }
    return 0;
}
