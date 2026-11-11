// rpc_server_config.h

#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include <log_manager.h>





namespace cookrpc { 


    class RpcServerConfig {
    public:
        static RpcServerConfig *GetInstance() {
            static RpcServerConfig instance;
            return &instance;
        }

        bool InitRpcServerConfig(const std::string &config_file);
        const std::string &GetServersNamePrefix() const { return servers_name_prefix_; }
        const std::string &GetServersIp() const { return servers_ip_; }
        uint16_t GetServersPort() const { return servers_port_; }
        // int GetThreadNum() const { return thread_pool_config_->GetThreadNum(); }
        int GetTimeout() const { return timeout_; }
        int GetMaxConnections() const { return max_connections_; }

        // 获取子配置对象
        const ThreadPoolConfig* GetThreadPoolConfig() const {return thread_pool_config_.get();} 
        const ThreadPoolConfig* GetThreadPoolConfig() const { return thread_pool_config_.get(); }

        // 热更新
        void SetServersNamePrefix(const std::string &servers_name_prefix) { servers_name_prefix_ = servers_name_prefix; }
        void SetServersIp(const std::string &servers_ip) { servers_ip_ = servers_ip; }
        void SetServersPort(uint16_t servers_port) { servers_port_ = servers_port; }
        void SetTimeout(uint32_t timeout) { timeout_ = timeout; }
        void SetMaxConnections(uint16_t max_connections) { max_connections_ = max_connections; }
        RpcServerConfig(const RpcServerConfig &) = delete;
        RpcServerConfig &operator=(const RpcServerConfig &) = delete;

        RpcServerConfig(RpcServerConfig &&) = delete;
        RpcServerConfig &operator=(RpcServerConfig &&) = delete;



    private:
        std::string servers_ip_; // "0.0.0.0"
        uint16_t servers_port_; // 8080
        std::string servers_name_prefix_; // "rpc_service"
        uint32_t timeout_; // 30 s
        uint16_t max_connections_; // 0 (0通常表示不限制)

        std::unique_ptr<ThreadPoolConfig> thread_pool_config_;
        std::unique_ptr<ServiceRegistryConfig> service_registry_config_;
    };


}