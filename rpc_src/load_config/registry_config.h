// 服务注册配置
#include <string>
#include <cstdint>
#include <vector>
#include <nlohmann/json.hpp>

namespace cookrpc
{
    enum class LoadBalanceStrategy
    {
        ROUND_ROBIN,       // 轮询
        RANDOM,            // 随机
        WEIGHT_ROUND_ROBIN // 加权轮询
    };
    struct RegistryInfo
    {
        std::string address;
        uint16_t port;
    };

    class ServiceRegistryConfig
    {

    public:
        ~ServiceRegistryConfig() = default;
        ServiceRegistryConfig() = default;

        bool InitRegistryConfig(const std::string &config_file);

        void SetServiceName(const std::string &name) { service_name_ = name; }
        const std::string &GetServiceName() const { return service_name_; }

        void SetServiceVersion(const std::string &version) { service_version_ = version; }
        const std::string &GetServiceVersion() const { return service_version_; }

        const std::vector<RegistryInfo> &GetRegistryNodes() const { return registry_nodes_; }
        size_t GetRegistryNodesSize() const { return registry_nodes_.size(); }

        ServiceRegistryConfig(const ServiceRegistryConfig &) = delete;
        ServiceRegistryConfig &operator=(const ServiceRegistryConfig &) = delete;

        ServiceRegistryConfig(ServiceRegistryConfig &&) = delete;
        ServiceRegistryConfig &operator=(ServiceRegistryConfig &&) = delete;

    private:
        std::vector<RegistryInfo> registry_nodes_; // 注册节点
        std::string service_name_;    // 服务名称
        std::string service_version_; // 服务版本
    };
} // namespace cookrpc