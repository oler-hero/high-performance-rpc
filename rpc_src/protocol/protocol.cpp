#include "rpc_protocol.h"
#include "log_manager.h"
#include <cstring>

namespace cookrpc
{
    // RPC 协议魔数，用于校验消息合法性
    const uint32_t RpcHeader::MAGIC = 0x12345678;

    // -------------------- RpcRequest 序列化 --------------------
    bool RpcRequest::Serialize(std::string &out) const
    {
        try
        {
            // 计算消息体（不含头部）的总长度
            // 包括三个字符串（service_name_, method_name_, payload_）各自的长度字段（uint32_t）和实际内容
            size_t body_size = service_name_.size() + method_name_.size() + payload_.size() +
                               3 * sizeof(uint32_t); // 3个字符串长度字段

            // 预分配输出缓冲区：头部 + 消息体
            out.resize(sizeof(RpcHeader) + body_size);

            // 填充固定头部
            RpcHeader header;
            header.magic_number = RpcHeader::MAGIC;   // 魔数
            header.message_length = body_size;        // 消息体长度
            header.sequence_id = sequence_id_;        // 请求序列号
            std::memcpy(&out[0], &header, sizeof(header)); // 写入头部到缓冲区起始

            size_t pos = sizeof(header); // 当前写入位置

            // ----- 写入 service_name -----
            uint32_t len = service_name_.size();
            std::memcpy(&out[pos], &len, sizeof(len)); // 写入长度
            pos += sizeof(len);
            std::memcpy(&out[pos], service_name_.data(), len); // 写入内容
            pos += len;

            // ----- 写入 method_name -----
            len = method_name_.size();
            std::memcpy(&out[pos], &len, sizeof(len));
            pos += sizeof(len);
            std::memcpy(&out[pos], method_name_.data(), len);
            pos += len;

            // ----- 写入 payload（请求参数） -----
            len = payload_.size();
            std::memcpy(&out[pos], &len, sizeof(len));
            pos += sizeof(len);
            std::memcpy(&out[pos], payload_.data(), len);

            return true;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Serialize failed: {}", e.what());
            return false;
        }
    }

    // -------------------- RpcRequest 反序列化 --------------------
    bool RpcRequest::Deserialize(const std::string &in)
    {
        try
        {
            // 检查输入长度是否至少包含一个头部
            if (in.size() < sizeof(RpcHeader))
            {
                LOG_ERROR("Input too small for header Deserialize. Size: {}, Need: {}",
                          in.size(), sizeof(RpcHeader));
                return false;
            }

            // 读取头部
            RpcHeader header;
            std::memcpy(&header, in.data(), sizeof(header));

            // 校验魔数
            if (header.magic_number != RpcHeader::MAGIC)
            {
                LOG_ERROR("Invalid magic number: {:#x}, expected: {:#x}",
                          header.magic_number, RpcHeader::MAGIC);
                return false;
            }

            size_t pos = sizeof(RpcHeader); // 当前读取位置
            uint32_t len = 0;

            // ----- 读取 service_name -----
            if (pos + sizeof(uint32_t) > in.size())
            {
                LOG_ERROR("Cannot read service_name length. pos: {}, size: {}", pos, in.size());
                return false;
            }
            std::memcpy(&len, &in[pos], sizeof(len));
            pos += sizeof(len);

            if (len > in.size() - pos) // 防止越界
            {
                LOG_ERROR("Service name too long: len={}, remaining={}", len, in.size() - pos);
                return false;
            }
            service_name_ = in.substr(pos, len);
            pos += len;

            // ----- 读取 method_name -----
            if (pos + sizeof(uint32_t) > in.size())
            {
                LOG_ERROR("Cannot read method_name length. pos: {}, size: {}", pos, in.size());
                return false;
            }
            std::memcpy(&len, &in[pos], sizeof(len));
            pos += sizeof(len);

            if (len > in.size() - pos)
            {
                LOG_ERROR("Method name too long: len={}, remaining={}", len, in.size() - pos);
                return false;
            }
            method_name_ = in.substr(pos, len);
            pos += len;

            // ----- 读取 payload -----
            if (pos + sizeof(uint32_t) > in.size())
            {
                LOG_ERROR("Cannot read payload length. pos: {}, size: {}", pos, in.size());
                return false;
            }
            std::memcpy(&len, &in[pos], sizeof(len));
            pos += sizeof(len);

            if (len > in.size() - pos)
            {
                LOG_ERROR("Payload too long: len={}, remaining={}", len, in.size() - pos);
                return false;
            }
            payload_ = in.substr(pos, len);

            // 保存序列号
            sequence_id_ = header.sequence_id;
            return true;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Deserialize failed: {}", e.what());
            return false;
        }
    }

    // -------------------- RpcResponse 序列化 --------------------
    bool RpcResponse::Serialize(std::string &out) const
    {
        try
        {
            // 计算消息体长度：
            // 包含 result_data_ 和 error_message_ 两个字符串（各带长度字段），
            // 再加上 error_code_ 和 sequence_id_ 两个固定字段。
            size_t body_size = result_data_.size() + error_message_.size() +
                               2 * sizeof(uint32_t) + // 两个字符串长度字段
                               sizeof(error_code_) +  // 错误码
                               sizeof(sequence_id_);  // 序列号

            // 预分配缓冲区
            out.resize(sizeof(RpcHeader) + body_size);

            // 填充头部
            RpcHeader header;
            header.magic_number = RpcHeader::MAGIC;
            header.message_length = body_size;
            header.sequence_id = sequence_id_;
            std::memcpy(&out[0], &header, sizeof(header));

            size_t pos = sizeof(header);

            // ----- 写入 result_data（返回结果） -----
            uint32_t len = result_data_.size();
            std::memcpy(&out[pos], &len, sizeof(len));
            pos += sizeof(len);
            std::memcpy(&out[pos], result_data_.data(), len);
            pos += len;

            // ----- 写入 error_message（错误信息） -----
            len = error_message_.size();
            std::memcpy(&out[pos], &len, sizeof(len));
            pos += sizeof(len);
            std::memcpy(&out[pos], error_message_.data(), len);
            pos += len;

            // ----- 写入 error_code（错误码） -----
            std::memcpy(&out[pos], &error_code_, sizeof(error_code_));
            pos += sizeof(error_code_);

            // ----- 写入 sequence_id（响应对应的请求序列号） -----
            std::memcpy(&out[pos], &sequence_id_, sizeof(sequence_id_));
            pos += sizeof(sequence_id_);

            return true;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Response serialize failed: {}", e.what());
            return false;
        }
    }

    // -------------------- RpcResponse 反序列化 --------------------
    bool RpcResponse::Deserialize(const std::string &in)
    {
        try
        {
            // 检查输入长度是否至少包含头部
            if (in.size() < sizeof(RpcHeader))
            {
                LOG_ERROR("Input too small for header Deserialize. Size: {}, Need: {}",
                          in.size(), sizeof(RpcHeader));
                return false;
            }

            // 读取头部
            RpcHeader header;
            std::memcpy(&header, in.data(), sizeof(header));

            // 校验魔数
            if (header.magic_number != RpcHeader::MAGIC)
            {
                // 此处原代码未记录错误日志，保持一致（但建议添加）
                return false;
            }

            // 校验消息总长度是否与头部声明一致
            if (in.size() != sizeof(header) + header.message_length)
            {
                LOG_ERROR("Invalid message length: expected {}, got {}",
                          sizeof(header) + header.message_length, in.size());
                return false;
            }

            size_t pos = sizeof(header);
            uint32_t len = 0;

            // ----- 读取 result_data -----
            if (pos + sizeof(uint32_t) > in.size())
            {
                LOG_ERROR("Cannot read result_data length. pos: {}, size: {}", pos, in.size());
                return false;
            }
            std::memcpy(&len, &in[pos], sizeof(len));
            pos += sizeof(len);

            if (pos + len > in.size()) // 确保长度不越界
            {
                LOG_ERROR("Result data too long: len={}, remaining={}", len, in.size() - pos);
                return false;
            }
            result_data_ = in.substr(pos, len);
            pos += len;

            // ----- 读取 error_message -----
            if (pos + sizeof(uint32_t) > in.size())
            {
                LOG_ERROR("Cannot read error_message length. pos: {}, size: {}", pos, in.size());
                return false;
            }
            std::memcpy(&len, &in[pos], sizeof(len));
            pos += sizeof(len);

            if (pos + len > in.size())
            {
                LOG_ERROR("Error message too long: len={}, remaining={}", len, in.size() - pos);
                return false;
            }
            error_message_ = in.substr(pos, len);
            pos += len;

            // ----- 读取 error_code -----
            if (pos + sizeof(error_code_) > in.size())
            {
                LOG_ERROR("Cannot read error_code. pos: {}, size: {}", pos, in.size());
                return false;
            }
            std::memcpy(&error_code_, &in[pos], sizeof(error_code_));
            pos += sizeof(error_code_);

            // ----- 读取 sequence_id -----
            if (pos + sizeof(sequence_id_) > in.size())
            {
                LOG_ERROR("Cannot read sequence_id. pos: {}, size: {}", pos, in.size());
                return false;
            }
            std::memcpy(&sequence_id_, &in[pos], sizeof(sequence_id_));
            pos += sizeof(sequence_id_);

            return true;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Response deserialize failed: {}", e.what());
            return false;
        }
    }

} // namespace cookrpc