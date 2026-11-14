#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include "rpc_protocol.h"
#include "log_manager.h"

namespace cookrpc
{

    // 前向声明
    class RpcMessage;
    class RpcRequest;
    class RpcResponse;

    /**
     * @brief 表示一个RPC网络连接，管理socket fd、读写缓冲区、状态及回调
     * 
     * Connection 封装了单个TCP连接的生命周期，提供非阻塞读写、消息解析、
     * 状态管理等功能。它使用 shared_ptr 管理自身，便于异步回调中保持有效。
     */
    class Connection : public std::enable_shared_from_this<Connection>
    {
    public:
        /**
         * @brief 连接状态枚举
         */
        enum class State
        {
            CONNECTED,      // 已连接，可正常收发
            DISCONNECTING,  // 正在断开（主动关闭中）
            DISCONNECTED    // 已断开，不可再使用
        };

        // 定义消息回调函数类型：当收到完整请求时调用
        using MessageCallback = std::function<void(const std::shared_ptr<Connection> &, const RpcRequest &)>;
        // 定义连接关闭回调函数类型：当连接关闭时调用
        using CloseCallback = std::function<void(const std::shared_ptr<Connection> &)>;

        /**
         * @brief 构造一个 Connection 对象
         * @param fd 已建立的socket文件描述符
         */
        explicit Connection(int fd);

        /**
         * @brief 析构函数，关闭fd并释放资源
         */
        ~Connection();

        // ---------- 基础 I/O 操作 ----------

        /**
         * @brief 从socket读取数据到读缓冲区，并尝试解析完整消息
         * @return true 读取成功（可能尚未解析出完整消息），false 发生错误
         */
        bool Read();

        /**
         * @brief 将字符串数据写入发送缓冲区并尝试发送
         * @param data 待发送的二进制数据
         * @return true 写入缓冲区成功（不一定已发送完毕），false 失败
         */
        bool Write(const std::string &data);

        /**
         * @brief 将 RpcResponse 对象序列化后写入发送缓冲区并尝试发送
         * @param response RPC响应对象
         * @return true 成功，false 失败
         */
        bool Write(const RpcResponse &response);

        /**
         * @brief 主动关闭连接，触发关闭回调
         */
        void Close();

        /**
         * @brief 检查连接是否有效（fd有效且状态为 CONNECTED）
         * @return true 有效，false 无效
         */
        bool IsValid() const { return fd_ > 0 && state_ == State::CONNECTED; }

        /**
         * @brief 带超时的读取操作（用于客户端同步调用场景）
         * @param timeout_ms 超时时间（毫秒），若 <=0 则无限等待
         * @return true 在超时前读取到完整数据，false 超时或出错
         */
        bool ReadWithTimeout(int timeout_ms);

        // ---------- 获取连接信息 ----------

        int GetFd() const { return fd_; }
        const std::string &GetPeerAddress() const { return peer_addr_; }
        uint16_t GetPeerPort() const { return peer_port_; }
        State GetState() const { return state_; }

        /**
         * @brief 获取并清空读缓冲区中的数据（供客户端使用）
         * @return 缓冲区中的原始数据副本
         */
        std::string GetReadBufferData() 
        {
            std::string data(read_buffer_.begin(), read_buffer_.end());
            read_buffer_.clear(); // 清空缓冲区
            return data;
        }

        // ---------- 设置回调 ----------

        void SetMessageCallback(const MessageCallback &cb) { message_callback_ = cb; }
        void SetCloseCallback(const CloseCallback &cb) { close_callback_ = cb; }

        /// 最大允许的单条消息大小（1MB），防止恶意过大消息
        static const size_t MAX_MESSAGE_SIZE = 1024 * 1024; // 1MB

        /**
         * @brief 尝试从读缓冲区中解析出一个完整的 RpcRequest 消息
         * @return true 成功解析并调用消息回调，false 数据不完整或解析失败
         * @note 如果解析成功，会从读缓冲区中移除已处理的数据
         */
        bool ProcessMessage();

    private:
        /**
         * @brief 处理已解析的请求消息（调用用户回调）
         */
        void HandleMessage(const RpcRequest &request);

        /**
         * @brief 处理一个已完成分帧的消息体：解密 -> 解压 -> 反序列化 -> 回调
         * @param encrypted_data 去掉长度前缀后的消息体
         * @return 成功返回 true，失败返回 false
         */
        bool HandleFrame(const std::string &encrypted_data);

        /**
         * @brief 处理错误，关闭连接并触发错误日志
         */
        void HandleError();

        /**
         * @brief 将写缓冲区中的数据尽量发送出去
         * @return true 发送成功或正在等待，false 发生错误
         */
        bool SendInBuffer();

        /**
         * @brief 读缓冲区中是否已包含至少一个完整帧
         */
        bool HasCompleteFrame() const;

        // ---------- 成员变量 ----------

        int fd_ = -1;                   // socket fd
        std::string peer_addr_ = "";    // 对端IP地址
        uint16_t peer_port_ = 0;        // 对端端口

        std::vector<char> read_buffer_;  // 读缓冲区（接收数据）
        std::vector<char> write_buffer_; // 写缓冲区（待发送数据）

        MessageCallback message_callback_ = nullptr; // 消息到达回调
        CloseCallback close_callback_ = nullptr;     // 连接关闭回调

        // 读缓冲区上限：一个最大帧 + 4 字节长度前缀
        static const size_t MAX_BUFFER_SIZE = 8 * 1024 * 1024 + 4;
        // 写缓冲区上限：防止对端长期不消费导致内存无界增长
        static const size_t MAX_WRITE_BUFFER_SIZE = 16 * 1024 * 1024;

        std::atomic<State> state_{State::DISCONNECTED}; // 当前连接状态
        std::atomic<bool> is_writing_ = false;          // 是否正在写入（防止并发写）
        mutable std::mutex io_mutex_;                   // 保护 fd/状态/写缓冲
    };

} // namespace cookrpc