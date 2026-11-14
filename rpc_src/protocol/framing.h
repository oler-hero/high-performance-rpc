#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <string>

namespace cookrpc
{
    /**
     * @brief 线上分帧格式
     *
     * 每条消息在网络上表示为：
     *   [4 字节长度（网络字节序）][payload]
     *
     * 长度只描述 payload 的字节数，不包含长度字段自身的 4 字节。
     *
     * 分帧放在加密层之外，原因是：12 字节协议头位于密文内部，
     * 接收端必须先知道消息边界才能解密，而解密前无法读取密文内部的长度，
     * 因此在明文位置额外加一个长度前缀，避免循环依赖。
     */
    namespace framing
    {
        /// 单帧最大允许长度（字节），用于拒绝异常长度，避免内存被恶意撑大
        static const size_t MAX_FRAME_SIZE = 8 * 1024 * 1024;

        enum class Status
        {
            OK,        ///< 成功取出一个完整帧
            NEED_MORE, ///< 数据不足，需要等待更多字节
            INVALID    ///< 长度字段非法，连接应关闭
        };

        /// 把一条完整消息编码为 [长度][payload]
        inline std::string Encode(const std::string &payload)
        {
            uint32_t net_len = htonl(static_cast<uint32_t>(payload.size()));
            std::string frame;
            frame.resize(4 + payload.size());
            std::memcpy(&frame[0], &net_len, sizeof(net_len));
            if (!payload.empty())
            {
                std::memcpy(&frame[4], payload.data(), payload.size());
            }
            return frame;
        }

        /// 从缓冲区头部尝试取出一帧；成功时从 buffer 中移除已消费的字节
        inline Status TryDecode(std::string &buffer, std::string &payload,
                                size_t max_frame_size = MAX_FRAME_SIZE)
        {
            if (buffer.size() < 4)
            {
                return Status::NEED_MORE;
            }

            uint32_t net_len = 0;
            std::memcpy(&net_len, buffer.data(), sizeof(net_len));
            uint32_t len = ntohl(net_len);

            if (len == 0 || len > max_frame_size)
            {
                return Status::INVALID;
            }
            if (buffer.size() < 4 + static_cast<size_t>(len))
            {
                return Status::NEED_MORE;
            }

            payload.assign(buffer, 4, len);
            buffer.erase(0, 4 + static_cast<size_t>(len));
            return Status::OK;
        }
    } // namespace framing
} // namespace cookrpc
