#pragma once

#include <string>
#include <unordered_map>

namespace cookrpc
{

    /**
     * @brief RPC 错误码枚举
     * 
     * 定义了整个 RPC 框架中可能出现的所有错误类型，按功能模块划分区间：
     * - 0：成功
     * - 1~999：系统级错误
     * - 1000~1999：网络相关错误
     * - 2000~2999：序列化/协议错误
     * - 3000~3999：服务注册/查找错误
     * - 4000~4999：参数/配置错误
     * - 5000~5999：业务逻辑错误
     * - 6000~6999：客户端调用错误
     */
    enum class ErrorCode : uint32_t
    {
        // ========== 成功 ==========
        SUCCESS = 0,                        ///< 操作成功

        // ========== 系统级错误 (1-999) ==========
        SYSTEM_ERROR = 1,                   ///< 系统内部错误（如系统调用失败）
        UNKNOWN_ERROR = 2,                  ///< 未知错误，无法归类
        OPERATION_TIMEOUT = 3,              ///< 操作超时
        MEMORY_ALLOCATION_ERROR = 4,        ///< 内存分配失败

        // ========== 网络相关错误 (1000-1999) ==========
        NETWORK_ERROR = 1000,               ///< 通用网络错误
        CONNECTION_LOST = 1001,             ///< 连接已断开
        SOCKET_ERROR = 1002,                ///< Socket 操作错误
        BIND_FAILED = 1003,                 ///< 绑定地址失败
        LISTEN_FAILED = 1004,               ///< 监听端口失败
        CONNECT_FAILED = 1005,              ///< 连接远程服务失败
        SEND_FAILED = 1006,                 ///< 发送数据失败
        RECEIVE_FAILED = 1007,              ///< 接收数据失败

        // ========== 序列化相关错误 (2000-2999) ==========
        SERIALIZE_ERROR = 2000,             ///< 序列化对象失败
        DESERIALIZE_ERROR = 2001,           ///< 反序列化数据失败
        INVALID_MESSAGE_FORMAT = 2002,      ///< 消息格式无效（不符合协议）
        PROTOCOL_ERROR = 2003,              ///< 协议层级错误（如版本不匹配）

        // ========== 服务相关错误 (3000-3999) ==========
        SERVICE_NOT_FOUND = 3000,           ///< 未找到指定的服务
        METHOD_NOT_FOUND = 3001,            ///< 未找到服务中的指定方法
        SERVICE_ALREADY_EXISTS = 3002,      ///< 服务已注册，重复注册
        INVALID_SERVICE_NAME = 3003,        ///< 服务名称不合法（空或格式错误）
        SERVICE_UNAVAILABLE = 3004,         ///< 服务暂时不可用（如正在关闭）

        // ========== 参数相关错误 (4000-4999) ==========
        INVALID_ARGUMENT = 4000,            ///< 传入的参数无效
        ARGUMENT_OUT_OF_RANGE = 4001,       ///< 参数值超出允许范围
        INVALID_CONFIG = 4002,              ///< 配置项无效

        // ========== 业务错误 (5000-5999) ==========
        BUSINESS_ERROR = 5000,              ///< 业务逻辑错误（具体由业务层定义）

        // ========== 客户端错误 (6000-6999) ==========
        INVALID_REQUEST = 6000,             ///< 客户端请求不合法
        METHOD_ERROR = 6001,                ///< 方法调用失败（如参数类型不匹配）
        INTERNAL_ERROR = 6002,              ///< 客户端内部状态错误
    };

    /**
     * @brief 错误信息工具类
     * 
     * 提供错误码到可读字符串的映射，方便日志输出和调试。
     */
    class Error
    {
    public:
        /**
         * @brief 根据错误码获取对应的描述信息
         * @param code 错误码
         * @return 对应的字符串描述，若未找到则返回 "Unknown error code"
         */
        static std::string getErrorMessage(ErrorCode code)
        {
            static const std::unordered_map<ErrorCode, std::string> error_messages = {
                // 成功
                {ErrorCode::SUCCESS, "Success"},
                // 系统级
                {ErrorCode::SYSTEM_ERROR, "System error"},
                {ErrorCode::UNKNOWN_ERROR, "Unknown error"},
                {ErrorCode::OPERATION_TIMEOUT, "Operation timeout"},
                {ErrorCode::MEMORY_ALLOCATION_ERROR, "Memory allocation failed"},
                // 网络
                {ErrorCode::NETWORK_ERROR, "Network error"},
                {ErrorCode::CONNECTION_LOST, "Connection lost"},
                {ErrorCode::SOCKET_ERROR, "Socket error"},
                {ErrorCode::BIND_FAILED, "Bind failed"},
                {ErrorCode::LISTEN_FAILED, "Listen failed"},
                {ErrorCode::CONNECT_FAILED, "Connect failed"},
                {ErrorCode::SEND_FAILED, "Send failed"},
                {ErrorCode::RECEIVE_FAILED, "Receive failed"},
                // 序列化
                {ErrorCode::SERIALIZE_ERROR, "Serialize error"},
                {ErrorCode::DESERIALIZE_ERROR, "Deserialize error"},
                {ErrorCode::INVALID_MESSAGE_FORMAT, "Invalid message format"},
                {ErrorCode::PROTOCOL_ERROR, "Protocol error"},
                // 服务
                {ErrorCode::SERVICE_NOT_FOUND, "Service not found"},
                {ErrorCode::METHOD_NOT_FOUND, "Method not found"},
                {ErrorCode::SERVICE_ALREADY_EXISTS, "Service already exists"},
                {ErrorCode::INVALID_SERVICE_NAME, "Invalid service name"},
                {ErrorCode::SERVICE_UNAVAILABLE, "Service unavailable"},
                // 参数
                {ErrorCode::INVALID_ARGUMENT, "Invalid argument"},
                {ErrorCode::ARGUMENT_OUT_OF_RANGE, "Argument out of range"},
                {ErrorCode::INVALID_CONFIG, "Invalid configuration"},
                // 业务
                {ErrorCode::BUSINESS_ERROR, "Business error"},
            };

            auto it = error_messages.find(code);
            return it != error_messages.end() ? it->second : "Unknown error code";
        }
    };

    /**
     * @brief 错误结果类
     * 
     * 封装了 RPC 调用或操作的结果，包含错误码和可选的附加错误信息。
     * 可通过 isOk() 判断是否成功，并获取详细的错误描述。
     */
    class ErrorResult
    {
    public:
        /**
         * @brief 默认构造函数，表示成功（错误码为 SUCCESS）
         */
        ErrorResult() : code_(ErrorCode::SUCCESS) {}

        /**
         * @brief 仅使用错误码构造
         * @param code 错误码
         */
        explicit ErrorResult(ErrorCode code) : code_(code) {}

        /**
         * @brief 使用错误码和附加消息构造
         * @param code 错误码
         * @param message 附加的错误描述信息
         */
        ErrorResult(ErrorCode code, const std::string &message)
            : code_(code), message_(message) {}

        /**
         * @brief 判断当前结果是否表示成功
         * @return true 表示成功，false 表示出错
         */
        bool isOk() const { return code_ == ErrorCode::SUCCESS; }

        /**
         * @brief 获取错误码
         */
        ErrorCode code() const { return code_; }

        /**
         * @brief 获取附加的错误消息（可能为空）
         */
        const std::string &message() const { return message_; }

        /**
         * @brief 生成完整的错误描述字符串
         * @return 格式为 "错误描述: 附加信息" 或仅有错误描述
         */
        std::string toString() const
        {
            if (message_.empty())
            {
                return Error::getErrorMessage(code_);
            }
            return Error::getErrorMessage(code_) + ": " + message_;
        }

    private:
        ErrorCode code_;        ///< 错误码
        std::string message_;   ///< 附加错误信息（可选）
    };

} // namespace cookrpc