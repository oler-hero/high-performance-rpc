#pragma once

#include <memory>
#include <unordered_map>
#include <mutex>
#include "connection.h"

namespace cookrpc
{

    /**
     * @brief 连接管理器（单例）
     *
     * 负责统一管理所有客户端连接（Connection），以文件描述符 fd 作为唯一键。
     * 内部使用 mutex 保证多线程环境下的线程安全。
     *
     * 典型用法：
     *   auto& mgr = ConnectionManager::GetInstance();
     *   mgr.AddConnection(conn);
     *   auto c = mgr.GetConnection(fd);
     *   mgr.RemoveConnection(fd);
     *
     * 注意：本类禁止拷贝、移动，仅允许通过 GetInstance() 获取全局唯一实例。
     */
    class ConnectionManager
    {
    public:
        /**
         * @brief 获取全局单例实例
         *
         * 使用 C++11 的静态局部变量保证线程安全的懒加载（Meyers Singleton）。
         * @return ConnectionManager& 单例引用
         */
        static ConnectionManager &GetInstance()
        {
            static ConnectionManager instance;
            return instance;
        }

        /**
         * @brief 添加一个连接
         * @param conn 待添加的连接（shared_ptr）
         *
         * 若相同 fd 已存在，通常由 Connection 内部负责覆盖或忽略，视具体实现而定。
         */
        void AddConnection(std::shared_ptr<Connection> conn);

        /**
         * @brief 根据文件描述符移除连接
         * @param fd 连接对应的文件描述符
         *
         * 移除后该连接不再被管理器持有；若外部无引用则会被自动析构。
         */
        void RemoveConnection(int fd);

        /**
         * @brief 根据文件描述符获取连接
         * @param fd 连接对应的文件描述符
         * @return std::shared_ptr<Connection> 找到则返回对应连接，否则返回 nullptr
         */
        std::shared_ptr<Connection> GetConnection(int fd);

        /**
         * @brief 获取当前管理的连接总数
         * @return size_t 连接数量（线程安全）
         */
        size_t GetConnectionCount() const;

        /**
         * @brief 关闭并清空所有连接
         *
         * 一般用于服务停止或析构阶段，主动释放所有连接资源。
         */
        void CloseAll();

        /// @brief 析构函数（内部通常会调用 CloseAll）
        ~ConnectionManager();

        // 禁用拷贝和移动
        ConnectionManager(const ConnectionManager &) = delete;
        ConnectionManager &operator=(const ConnectionManager &) = delete;
        ConnectionManager(ConnectionManager &&) = delete;
        ConnectionManager &operator=(ConnectionManager &&) = delete;

    private:
        /// @brief 私有构造，单例模式，禁止外部直接实例化
        ConnectionManager() = default;

        /// @brief fd -> 连接 的映射表，所有访问均需加锁保护
        std::unordered_map<int, std::shared_ptr<Connection>> connections_;

        /// @brief 保护 connections_ 的互斥锁；mutable 使其可在 const 方法中加锁
        mutable std::mutex mutex_;
    };
} // namespace cookrpc