#pragma once

#include <string>
#include <stdint.h>
#include "serializer_manager.h"

namespace cookrpc
{

    /**
     * @brief RPC 协议头部结构，固定长度，位于每个消息的起始位置
     */
    struct RpcHeader
    {
        uint32_t magic_number;   // 魔数，用于校验消息的有效性
        uint32_t message_length; // 消息体（即 RpcMessage 序列化后的字节流）的长度
        uint32_t sequence_id;    // 请求/响应的序列号，用于匹配调用和返回

        static const uint32_t MAGIC; // 静态魔数常量，具体值在实现文件中定义
    };

    /**
     * @brief RPC 消息基类，所有请求和响应消息的抽象接口
     *        提供了序列化/反序列化方法，并携带序列号
     */
    class RpcMessage
    {
    public:
        virtual ~RpcMessage() = default;

        /**
         * @brief 将当前消息对象序列化为二进制字符串
         * @param out 输出的序列化数据
         * @return 成功返回 true，否则 false
         */
        virtual bool Serialize(std::string &out) const = 0;

        /**
         * @brief 从二进制字符串反序列化恢复消息对象
         * @param in 输入的序列化数据
         * @return 成功返回 true，否则 false
         */
        virtual bool Deserialize(const std::string &in) = 0;

        uint32_t sequence_id_; // 消息序列号

        uint32_t getSequenceId() const { return sequence_id_; }
        void setSequenceId(uint32_t sequence_id) { sequence_id_ = sequence_id; }
    };

    /**
     * @brief RPC 请求消息，继承自 RpcMessage
     *        包含服务名、方法名以及序列化的参数负载
     */
    class RpcRequest : public RpcMessage
    {
    public:
        // 设置请求负载（参数序列化后的数据）
        void setPayload(const std::string &payload) { payload_ = payload; }
        // 设置目标服务名称
        void setServiceName(const std::string &service_name) { service_name_ = service_name; }
        // 设置目标方法名称
        void setMethodName(const std::string &method_name) { method_name_ = method_name; }

        // 获取负载数据
        std::string getPayload() const { return payload_; }
        // 获取序列号（直接返回基类成员）
        uint32_t getSequenceId() const { return sequence_id_; }
        // 获取服务名
        std::string getServiceName() const { return service_name_; }
        // 获取方法名
        std::string getMethodName() const { return method_name_; }

        // 实现序列化接口
        bool Serialize(std::string &out) const override;
        // 实现反序列化接口
        bool Deserialize(const std::string &in) override;

    private:
        std::string service_name_; // 服务名称，如 "UserService"
        std::string method_name_;  // 方法名称，如 "GetUserInfo"
        std::string payload_;      // 方法参数的序列化数据（由上层序列化器处理）
    };

    /**
     * @brief RPC 响应消息，继承自 RpcMessage
     *        包含结果数据或错误信息（错误码 + 错误描述）
     */
    class RpcResponse : public RpcMessage
    {
    public:
        // 实现序列化接口
        bool Serialize(std::string &out) const override;
        // 实现反序列化接口
        bool Deserialize(const std::string &in) override;

        // 获取正常返回的结果数据
        std::string getResultData() const { return result_data_; }
        // 获取错误描述信息
        std::string getErrorMessage() const { return error_message_; }
        // 获取错误码（0 表示成功，非零表示失败）
        uint32_t getErrorCode() const { return error_code_; }

        // 设置结果数据
        void setResultData(const std::string &result_data) { result_data_ = result_data; }
        // 设置错误描述
        void setErrorMessage(const std::string &error_message) { error_message_ = error_message; }
        // 设置错误码
        void setErrorCode(uint32_t error_code) { error_code_ = error_code; }

    private:
        std::string result_data_;   // 方法返回值的序列化数据
        std::string error_message_; // 错误详细信息（如异常文本）
        uint32_t error_code_;       // 错误码，0 代表无错误
    };

} // namespace cookrpc