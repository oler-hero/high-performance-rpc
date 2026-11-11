#include "connection_manager.h"
#include "log_manager.h"

namespace cookrpc
{

    /**
     * @brief 添加一个连接到连接管理器
     * @param conn 连接对象，使用 shared_ptr 管理生命周期
     *
     * 以连接对应的 fd 作为唯一 key 保存到 connections_ 中。
     * 如果传入空指针或 fd 已存在，则拒绝添加。
     */
    void ConnectionManager::AddConnection(std::shared_ptr<Connection> conn)
    {
        // 空连接直接拒绝，避免后续解引用崩溃
        if (!conn)
        {
            LOG_ERROR("add null connection");
            return;
        }

        // 加锁保护 connections_，保证多线程下安全访问
        std::lock_guard<std::mutex> lock(mutex_);

        // 使用 fd 作为连接的唯一标识
        int fd = conn->GetFd();

        // 检查是否已经存在相同 fd 的连接，避免重复添加
        auto it = connections_.find(fd);
        if (it != connections_.end())
        {
            LOG_WARN("connection already exists, fd: {}", fd);
            return;
        }

        // 保存连接到 map 中
        connections_[fd] = conn;
    }

    /**
     * @brief 根据 fd 移除连接
     * @param fd 连接对应的文件描述符
     *
     * 注意：这里只是从管理器中移除，并不会主动调用 Close()。
     * 如果需要关闭连接，应由外部逻辑或 Connection 自身负责。
     */
    void ConnectionManager::RemoveConnection(int fd)
    {
        // 加锁保护 connections_
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = connections_.find(fd);
        if (it != connections_.end())
        {
            // 找到则从 map 中删除
            connections_.erase(it);
        }
        else
        {
            // 未找到连接，记录警告
            LOG_WARN("connection not found, fd: {}", fd);
        }
    }

    /**
     * @brief 根据 fd 获取连接
     * @param fd 连接对应的文件描述符
     * @return 找到返回 shared_ptr<Connection>，找不到返回 nullptr
     *
     * 返回 shared_ptr 会延长连接对象的生命周期，
     * 即使该连接之后被 RemoveConnection 移除，调用方仍可安全持有。
     */
    std::shared_ptr<Connection> ConnectionManager::GetConnection(int fd)
    {
        // 加锁保护 connections_
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = connections_.find(fd);
        if (it != connections_.end())
        {
            return it->second;
        }

        return nullptr;
    }

    /**
     * @brief 获取当前管理的连接数量
     * @return 连接数量
     *
     * const 成员函数中也需要加锁，因此头文件里的 mutex_ 通常需要声明为 mutable。
     */
    size_t ConnectionManager::GetConnectionCount() const
    {
        // 加锁保护 connections_
        std::lock_guard<std::mutex> lock(mutex_);
        return connections_.size();
    }

    /**
     * @brief 关闭所有连接并清空连接管理器
     *
     * 遍历 connections_，对每个有效连接调用 Close()，
     * 最后清空 map。
     */
    void ConnectionManager::CloseAll()
    {
        // 加锁保护 connections_
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto &pair : connections_)
        {
            // 防止 shared_ptr 为空
            if (auto conn = pair.second)
            {
                conn->Close();
            }
        }

        // 清空所有连接记录
        connections_.clear();
    }

    /**
     * @brief 析构函数
     *
     * 对象销毁时自动关闭所有连接，避免资源泄漏。
     */
    ConnectionManager::~ConnectionManager()
    {
        CloseAll();
    }

} // namespace cookrpc