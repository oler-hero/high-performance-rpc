#include "thread_pool_config.h"

namespace cookrpc {

    bool ThreadPoolConfig::InitThreadPoolConfig(const nlohmann::json & config) {
        try {
            uint16_t max_threads = config.value("max_threads", 10);     // 如果 config 这个 JSON 对象中存在 "max_threads" 这个键，就取它的值；否则就用默认值 10。
            
            if (max_threads == 0) {
                LOG_ERROR("Invalid max_threads: {}", max_threads);
                return false;
            }
            SetMaxThreads(max_threads);

                        uint32_t queue_size = config.value("queue_size", 100);
            if (queue_size == 0)
            {
                LOG_ERROR("Invalid queue_size");
                return false;
            }
            SetQueueSize(queue_size);

            uint32_t keep_alive_time = config.value("keep_alive_time", 60);
            if (keep_alive_time == 0)
            {
                LOG_ERROR("Invalid keep_alive_time");
                return false;
            }
            SetKeepAliveTime(keep_alive_time);

            return true;

        }
        catch (const std::exception &e) {
            LOG_ERROR("Failed to init thread pool config: {}", e.what());
            return false;
        }
    }
}   // namespace cookrpc
