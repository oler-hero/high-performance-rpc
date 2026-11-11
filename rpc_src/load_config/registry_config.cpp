#include "registry_config.h"
#include "log_manager.h"
#include "nlohmann/json.hpp"
#include <fstream>

using json = nlohmann::json;

/**
 加载 registry_config.json 文件，并设置服务名、版本、注册中心节点信息, 没太多难的东西
 **/
namespace cookrpc
{
    bool ServiceRegistryConfig::InitRegistryConfig(const std::string &config_file)
    {

        std::ifstream file(config_file);
        if (!file.is_open())
        {
            LOG_ERROR("Failed to open register config file: {}", config_file);
            return false;
        }

        json config = json::parse(file);

        std::string service_name = config.value("service_name", "");
        if (service_name.empty())
        {
            LOG_ERROR("service_name is empty");
            return false;
        }

        SetServiceName(service_name);
        std::string service_version = config.value("service_version", "");
        if (service_version.empty())
        {
            LOG_ERROR("service_version is empty");
            return false;
        }
        SetServiceVersion(service_version);

        if (!config.contains("registry_nodes"))
        {
            LOG_ERROR("registry_nodes is empty");
            return false;
        }
        json registry_nodes_json = config["registry_nodes"];
        std::vector<RegistryInfo> tmp_registry_nodes;
        for (const auto &node : registry_nodes_json)
        {
            RegistryInfo tmp_node;
            if (!node.contains("address") || !node.contains("port"))
            {
                return false;
            }
            tmp_node.address = node["address"];
            tmp_node.port = node["port"];
            tmp_registry_nodes.push_back(tmp_node);
        }
        std::swap(registry_nodes_, tmp_registry_nodes);
        return true;
    }
}