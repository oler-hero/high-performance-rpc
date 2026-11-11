#include "service_registry.h"
#include <iostream>
#include <thread>
#include <chrono>

const std::string ServiceRegistry::ROOT_PATH = "/cookrpc";

ServiceRegistry::ServiceRegistry(const std::string& zk_hosts)
    : zk_handle_(nullptr), is_connected_(false) {
    
    zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);

    zk_handle_ = zookeeper_init(
        zk_hosts.c_str(),
        globalWatcher,
        30000,
        0,
        this,
        0
    );

    if (!zk_handle_) {
        throw std::runtime_error("Failed to connect to ZooKeeper");
    }

    int retry = 0;
     while (!is_connected_ && retry < 10)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        retry++;
    }

    if (!is_connected_)
    {
        LOG_WARN("ZooKeeper connection timeout, but continuing...");
    }
    else
    {
        LOG_INFO("Connected to ZooKeeper: {}", zk_hosts);
    }

}

ServiceRegistry::~ServiceRegistry()
{
    if (zk_handle_)
    {
        try {
            // LOG_INFO("Closing ZooKeeper handle in ServiceRegistry destructor");
            zookeeper_close(zk_handle_);
            zk_handle_ = nullptr;
            // LOG_INFO("ZooKeeper handle closed successfully");
        } catch (const std::exception& e) {
            std::cerr << "Exception closing ZooKeeper handle in ServiceRegistry destructor: " 
                      << e.what() << std::endl;
            zk_handle_ = nullptr;
        } catch (...) {
            std::cerr << "Unknown exception closing ZooKeeper handle in ServiceRegistry destructor" 
                      << std::endl;
            zk_handle_ = nullptr;
        }
    }
}

void ServiceRegistry::globalWatcher(zhandle_t *zh, int type,
                                    int state, const char *path,
                                    void *watcherCtx)
{
    // 1. 检查上下文指针
    if (!watcherCtx)
    {
        return;
    }

    // 2. 转换上下文为 ServiceRegistry 指针
    ServiceRegistry *registry = static_cast<ServiceRegistry *>(watcherCtx);

    // 3. 处理会话事件
    if (type == ZOO_SESSION_EVENT)
    {
        if (state == ZOO_CONNECTED_STATE)
        {
            // 连接成功
            registry->is_connected_ = true;
            LOG_INFO("Connected to ZooKeeper");
        }
        else if (state == ZOO_EXPIRED_SESSION_STATE)
        {
            // 会话过期 (需要重新连接)
            registry->is_connected_ = false;
            LOG_ERROR("ZooKeeper session expired");
        }
        else if (state == ZOO_AUTH_FAILED_STATE)
        {
            // 认证失败
            registry->is_connected_ = false;
            LOG_ERROR("ZooKeeper authentication failed");
        }
        else
        {
            // 其他状态 (如断开连接)
            registry->is_connected_ = false;
            LOG_WARN("ZooKeeper connection state changed: {}", state);
        }
    }
}

bool ServiceRegistry::ensurePath(const std::string &path)
{
    // 1. 空路径检查
    if (path.empty())
    {
        return true;
    }

    // 2. ZooKeeper 句柄检查
    if (!zk_handle_)
    {
        LOG_ERROR("ZooKeeper handle is null");
        return false;
    }

    // 3. 递归创建父路径
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos && pos > 0)
    {
        std::string parent = path.substr(0, pos);
        if (!parent.empty() && !ensurePath(parent))
        {
            LOG_ERROR("Failed to create parent path: {}", parent);
            return false;
        }
    }

    // 4. 检查节点是否存在
    struct Stat stat;
    int ret = zoo_exists(zk_handle_, path.c_str(), 0, &stat);

    if (ret == ZOK)
    {
        // 节点已存在
        LOG_DEBUG("Path exists: {}", path);
        return true;
    }
    else if (ret == ZNONODE)
    {
        // 5. 节点不存在，创建持久节点
        ret = zoo_create(
            zk_handle_,
            path.c_str(),
            "",                      // 空数据
            0,                       // 数据长度
            &ZOO_OPEN_ACL_UNSAFE,    // 开放 ACL (无权限控制)
            0,                       // flags=0 表示持久节点
            NULL,                    // 不需要返回路径
            0                        // 路径缓冲区大小
        );

        if (ret == ZOK)
        {
            LOG_INFO("Created persistent path: {}", path);
            return true;
        }
        else if (ret == ZNODEEXISTS)
        {
            // 节点已存在 (并发创建，竞态条件)
            LOG_DEBUG("Path already exists (race condition): {}", path);
            return true;
        }
        else
        {
            LOG_ERROR("Failed to create path: {}, error: {}", path, zerror(ret));
            return false;
        }
    }
    else
    {
        LOG_ERROR("Failed to check path: {}, error: {}", path, zerror(ret));
        return false;
    }
}

bool ServiceRegistry::createNode(const std::string &path,
                                 const std::string &data,
                                 int flags)
{
    // 1. ZooKeeper 句柄检查
    if (!zk_handle_)
    {
        return false;
    }

    // 2. 创建节点
    int ret = zoo_create(
        zk_handle_,
        path.c_str(),
        data.c_str(),
        data.length(),
        &ZOO_OPEN_ACL_UNSAFE,   // 开放 ACL
        flags,                  // 节点类型 (0=持久, ZOO_EPHEMERAL=临时)
        NULL,
        0
    );

    // 3. 处理结果
    if (ret == ZOK)
    {
        LOG_INFO("Created node: {} (flags={})", path, flags);
        return true;
    }
    else if (ret == ZNODEEXISTS)
    {
        LOG_WARN("Node already exists: {}", path);
        return true; // 节点已存在也算成功
    }
    else
    {
        LOG_ERROR("Failed to create node: {}, error: {}", path, zerror(ret));
        return false;
    }
}

bool ServiceRegistry::registerService(const std::string &service_name,
                                      const std::string &service_address)
{
    // 1. 连接检查
    if (!isConnected() || !zk_handle_)
    {
        LOG_ERROR("Not connected to ZooKeeper");
        return false;
    }

    // 2. 确保服务类型路径存在(持久节点)
    // 例如: /cookrpc/UserService
    std::string service_path = ROOT_PATH + "/" + service_name;
    if (!ensurePath(service_path))
    {
        LOG_ERROR("Failed to ensure service path: {}", service_path);
        return false;
    }

    // 3. 创建服务实例节点(临时节点)
    // 例如: /cookrpc/UserService/192.168.1.100:8001
    std::string instance_path = service_path + "/" + service_address;
    if (!createNode(instance_path, service_address, ZOO_EPHEMERAL))
    {
        LOG_ERROR("Failed to create service instance: {}", instance_path);
        return false;
    }

    LOG_INFO("Service registered: {} at {}", service_name, service_address);
    return true;
}

bool ServiceRegistry::isConnected() const
{
    return is_connected_ && zk_handle_ != nullptr;
}