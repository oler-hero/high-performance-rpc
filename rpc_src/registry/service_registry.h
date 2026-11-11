#pragma once
#include <string>
#include <vector>
#include <memory>
#include <zookeeper/zookeeper.h>
#include "log_manager.h"

class ServiceRegistry {
public:
    ServiceRegistry(const std::string& zk_hosts);
    ~ServiceRegistry();
    // 注册
    bool registerService(const std::string& service_name,
                         const std::string& service_address);

    // 
    bool isConnected() const;

private:
    /**
     * @brief 全局 Watcher 回调函数
     * @param zh ZooKeeper 句柄
     * @param type 事件类型 (ZOO_SESSION_EVENT 等)
     * @param state 连接状态 (ZOO_CONNECTED_STATE 等)
     * @param path 触发事件的路径
     * @param watcherCtx 用户上下文 (this 指针)
     *
     * 处理的事件:
     * - ZOO_CONNECTED_STATE: 连接成功
     * - ZOO_EXPIRED_SESSION_STATE: 会话过期
     * - ZOO_AUTH_FAILED_STATE: 认证失败
     */
    static void globalWatcher(zhandle_t *zh, int type
                             int state, const char *path, void* watcherCtx);

    bool createNode(const std::string &path, const std::string& data,
                    int flags = 0);
    bool ensurePath(const std::string& path);

    zhandle_t *zk_handle_;              // ZooKeeper 句柄
    static const std::string ROOT_PATH; // 根路径: "/cookrpc"
    bool is_connected_;                 // 连接状态

};

