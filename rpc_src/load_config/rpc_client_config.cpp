
#include <fstream>
#include "log_manager.h"
#include "rpc_client_config.h"

namespace cookrpc
{

    bool RpcClientConfig::InitRpcClientConfig(const std::string &config_filename)
    {
        std::ifstream file(config_filename);
        if (!file.is_open())
        {
            LOG_ERROR("Failed to open config file: {}", config_filename);
            return false;
        }
        nlohmann::json config = nlohmann::json::parse(file);
        SetZkNamespace(config.value("zk_namespace", "cookrpc"));
        SetZkHost(config.value("zk_host", "localhost"));
        SetZkPort(config.value("zk_port", 2181));
        SetTimeout(config.value("timeout_ms", 3000));
        SetRetryTimes(config.value("retry_times", 3));
        SetServerPort(config.value("server_port", 8989));
        return true;
    }

    void RpcClientConfig::SetZkNamespace(const std::string &zk_namespace)
    {
        zk_namespace_ = zk_namespace;
    }

    void RpcClientConfig::SetZkHost(const std::string &zk_host)
    {
        zk_host_ = zk_host;
    }

    void RpcClientConfig::SetZkPort(int zk_port)
    {
        zk_port_ = zk_port;
    }

    void RpcClientConfig::SetTimeout(int timeout_ms)
    {
        timeout_ms_ = timeout_ms;
    }

    void RpcClientConfig::SetRetryTimes(int retry_times)
    {
        retry_times_ = retry_times;
    }
    void RpcClientConfig::SetServerPort(int server_port)
    {
        server_port_ = server_port;
    }

}