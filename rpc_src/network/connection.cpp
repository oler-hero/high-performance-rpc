#include "connection.h"
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <chrono>

#include "serializer_manager.h"
#include "aes_encrypt.h"
#include "zstd_compress.h"
#include "framing.h"

namespace cookrpc
{
    // 静态成员变量定义（MAX_MESSAGE_SIZE 在头文件中声明）
    const size_t Connection::MAX_MESSAGE_SIZE;

    /**
     * 构造函数：初始化连接对象，验证文件描述符的有效性并获取对端地址信息
     * @param fd 已建立的 socket 文件描述符（应为非阻塞）
     */
    Connection::Connection(int fd) : fd_(fd)
    {
        // 先检查 fd 的有效性
        if (fd_ < 0)
        {
            LOG_ERROR("connection fd is invalid: {}", fd_);
            state_ = State::DISCONNECTED;
            return;
        }

        // 检查是否是 socket 类型（避免传入普通文件或管道等）
        int type;
        socklen_t type_len = sizeof(type);
        if (getsockopt(fd_, SOL_SOCKET, SO_TYPE, &type, &type_len) < 0)
        {
            LOG_ERROR("Not a socket fd: {}, errno: {}", fd_, errno);
            state_ = State::DISCONNECTED;
            return;
        }

        // 获取对端地址信息（IP 和端口），用于日志和标识
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        memset(&addr, 0, sizeof(addr));

        if (getpeername(fd_, (struct sockaddr *)&addr, &len) == 0)
        {
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
            peer_addr_ = ip;
            peer_port_ = ntohs(addr.sin_port);
            state_ = State::CONNECTED;
            // // LOG_INFO("Connection established with {}:{}, fd: {}", peer_addr_, peer_port_, fd_);
        }
        else
        {
            LOG_ERROR("getpeername failed, fd: {}, errno: {} ({})",
                      fd_, errno, strerror(errno));
            state_ = State::DISCONNECTED;
        }
    }

    /**
     * 析构函数：确保连接被关闭，并捕获所有异常避免抛出
     */
    Connection::~Connection()
    {
        try {
            Close();
        } catch (const std::exception& e) {
            // 在析构函数中不应该抛出异常，只记录错误
            // 注意：这里不能使用LOG_ERROR，因为Logger可能已经被销毁
            std::cerr << "Exception in Connection destructor: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception in Connection destructor" << std::endl;
        }
    }

    /**
     * 带超时的读取函数，使用 select 监控可读事件，然后调用 Read()
     * @param timeout_ms 超时时间（毫秒）
     * @return 成功读取并处理返回 true，超时或出错返回 false
     */
    bool Connection::ReadWithTimeout(int timeout_ms)
    {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);

        while (true)
        {
            // 已经凑够一帧就直接返回，避免多余等待
            if (HasCompleteFrame())
            {
                return true;
            }

            auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                LOG_ERROR("read timeout after {} ms, fd: {}, buffered: {}",
                          timeout_ms, fd_, read_buffer_.size());
                return false;
            }

            auto remaining_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

            fd_set read_set;
            struct timeval timeout;
            timeout.tv_sec = remaining_ms / 1000;
            timeout.tv_usec = (remaining_ms % 1000) * 1000;

            // 每次循环都需要重新设置 fd_set
            FD_ZERO(&read_set);
            FD_SET(fd_, &read_set);

            int ret = select(fd_ + 1, &read_set, nullptr, nullptr, &timeout);
            if (ret < 0)
            {
                if (errno == EINTR)
                {
                    // 如果是被信号中断，则继续等待
                    continue;
                }
                LOG_ERROR("select error, fd: {}, errno: {}", fd_, errno);
                return false;
            }

            if (ret == 0)
            {
                // 超时
                LOG_ERROR("read timeout after {} ms, fd: {}, buffered: {}",
                          timeout_ms, fd_, read_buffer_.size());
                return false;
            }

            // 有数据可读，继续累积到读缓冲区
            if (!Read())
            {
                LOG_ERROR("read failed after select, fd: {}", fd_);
                return false;
            }
        }
    }

    /**
     * 读缓冲区中是否已包含至少一个完整帧
     * 长度字段非法时同样返回 true，交由上层解析时报错
     */
    bool Connection::HasCompleteFrame() const
    {
        if (read_buffer_.size() < 4)
        {
            return false;
        }

        uint32_t net_len = 0;
        std::memcpy(&net_len, read_buffer_.data(), sizeof(net_len));
        uint32_t len = ntohl(net_len);

        if (len == 0 || len > framing::MAX_FRAME_SIZE)
        {
            return true;
        }
        return read_buffer_.size() >= 4 + static_cast<size_t>(len);
    }

    /**
     * 非阻塞读取数据，采用边缘触发模式（ET），循环读取直到 EAGAIN
     * 将读取到的数据追加到 read_buffer_ 中
     * @return 成功返回 true（包括未读到数据但连接正常的情况），失败返回 false
     */
    bool Connection::Read()
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        // 检查连接状态
        if (state_ != State::CONNECTED)
        {
            LOG_ERROR("Connection is not in CONNECTED state, fd: {}, state: {}", 
                     fd_, static_cast<int>(state_.load()));
            return false;
        }

        char buf[4096];
        bool has_read_data = false;
        
        // 在边缘触发模式下，需要循环读取直到EAGAIN
        while (true)
        {
            ssize_t n = read(fd_, buf, sizeof(buf));
            if (n < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // 在边缘触发模式下，这表示所有数据都已读取完毕
                    LOG_DEBUG("No more data available now, fd: {} (read {} bytes total)", 
                             fd_, has_read_data ? "some" : "no");
                    return has_read_data || !has_read_data; // 即使没读到数据也不算错误
                }
                LOG_ERROR("read error, fd: {}, errno: {}, error: {}", fd_, errno, strerror(errno));
                state_ = State::DISCONNECTED;
                return false;
            }

            if (n == 0)
            {
                // 对端正常关闭连接
                // // LOG_INFO("connection closed by peer, fd: {}", fd_);
                state_ = State::DISCONNECTED;
                return false; // 连接关闭
            }

            has_read_data = true;
            LOG_DEBUG("Read {} bytes from fd: {}", n, fd_);

            // 检查缓冲区大小，防止内存溢出
            if (read_buffer_.size() + n > MAX_BUFFER_SIZE)
            {
                LOG_ERROR("read buffer overflow, fd: {}, current size: {}, trying to add: {}", 
                         fd_, read_buffer_.size(), n);
                state_ = State::DISCONNECTED;
                return false;
            }

            read_buffer_.insert(read_buffer_.end(), buf, buf + n);
            
            // 如果读取的数据小于缓冲区大小，可能已经读完了，退出循环
            if (n < sizeof(buf))
            {
                break;
            }
        }
        return true;
    }

    /**
     * 处理读缓冲区中的消息：按长度前缀分帧 -> 解密 -> 解压 -> 反序列化 -> 回调处理
     * 支持 TCP 半包与粘包：不足一帧时等待更多数据，包含多帧时循环处理
     * @return 成功处理或等待更多数据返回 true，失败返回 false
     */
    bool Connection::ProcessMessage()
    {
        std::string buffer(read_buffer_.begin(), read_buffer_.end());

        while (true)
        {
            std::string encrypted_data;
            auto status = framing::TryDecode(buffer, encrypted_data);

            if (status == framing::Status::NEED_MORE)
            {
                break; // 剩余数据不足一帧，等待下一次读取
            }
            if (status == framing::Status::INVALID)
            {
                LOG_ERROR("Invalid frame length, fd: {}", fd_);
                return false;
            }

            if (!HandleFrame(encrypted_data))
            {
                return false;
            }
        }

        // 把尚未消费的残缺帧写回读缓冲区
        read_buffer_.assign(buffer.begin(), buffer.end());
        return true;
    }

    /**
     * 处理单个已分帧的消息体：解密 -> 解压 -> 反序列化 -> 回调
     */
    bool Connection::HandleFrame(const std::string &encrypted_data)
    {
        try
        {
            if (encrypted_data.empty())
            {
                LOG_ERROR("Encrypted data is empty, fd: {}", fd_);
                return false;
            }

            // 解密数据
            std::string decrypted_data;
            if (!AesEncrypt::getInstance().Decrypt(encrypted_data, decrypted_data))
            {
                LOG_ERROR("Failed to decrypt data, encrypted size: {}, fd: {}",
                          encrypted_data.size(), fd_);
                return false;
            }

            if (decrypted_data.empty())
            {
                LOG_ERROR("Decrypted data is empty, fd: {}", fd_);
                return false;
            }

            // 解压数据（Zstd）
            std::string decompressed_data;
            if (!ZstdCompress::getInstance().DecompressString(decrypted_data, decompressed_data))
            {
                LOG_ERROR("Failed to decompress data, decrypted size: {}, fd: {}",
                          decrypted_data.size(), fd_);
                return false;
            }

            if (decompressed_data.size() < sizeof(RpcHeader))
            {
                LOG_ERROR("Decompressed data too small, size: {}, fd: {}",
                          decompressed_data.size(), fd_);
                return false;
            }

            // 反序列化请求对象
            RpcRequest request;
            if (!request.Deserialize(decompressed_data))
            {
                LOG_ERROR("Failed to deserialize request body, decompressed size: {}, fd: {}",
                          decompressed_data.size(), fd_);
                return false;
            }

            // 处理消息（调用用户注册的回调）
            HandleMessage(request);
            return true;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Error in HandleFrame: {}, fd: {}", e.what(), fd_);
            return false;
        }
    }

    /**
     * 发送原始数据（将数据追加到写缓冲并尝试发送）
     * @param data 要发送的二进制数据
     * @return 成功返回 true（可能部分数据暂存于缓冲区），失败返回 false
     */
    bool Connection::Write(const std::string &data)
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        // 检查连接状态
        if (state_ != State::CONNECTED)
        {
            LOG_ERROR("Connection is not in CONNECTED state for write, fd: {}, state: {}", 
                     fd_, static_cast<int>(state_.load()));
            return false;
        }

        if (data.empty())
        {
            LOG_ERROR("write data is empty, fd: {}", fd_);
            return false;
        }

        // 写缓冲上限保护：对端长期不读时避免内存无界增长
        if (write_buffer_.size() + data.size() > MAX_WRITE_BUFFER_SIZE)
        {
            LOG_ERROR("write buffer overflow, fd: {}, buffered: {}, incoming: {}",
                      fd_, write_buffer_.size(), data.size());
            state_ = State::DISCONNECTED;
            return false;
        }

        // 添加到写缓冲
        write_buffer_.insert(write_buffer_.end(), data.begin(), data.end());

        // 尝试发送数据（非阻塞写，可能只发送一部分）
        while (!write_buffer_.empty())
        {
            ssize_t n = write(fd_, write_buffer_.data(), write_buffer_.size());
            if (n < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break; // 非阻塞模式下暂时无法发送，等待下次 epoll 触发
                }
                LOG_ERROR("write error, fd: {}, errno: {}", fd_, errno);
                state_ = State::DISCONNECTED;
                return false;
            }
            write_buffer_.erase(write_buffer_.begin(), write_buffer_.begin() + n);
        }
        return true;
    }

    /**
     * 发送 RpcResponse 对象（自动序列化后调用 Write）
     * @param response 响应对象
     * @return 成功返回 true，失败返回 false
     */
    bool Connection::Write(const RpcResponse &response)
    {
        std::string data;
        if (!response.Serialize(data))
        {
            LOG_ERROR("serialize response failed");
            return false;
        }
        return Write(data);
    }

    /**
     * 处理错误：触发关闭回调并关闭连接
     */
    void Connection::HandleError()
    {
        if (close_callback_)
        {
            close_callback_(shared_from_this());
        }
        Close();
    }

    /**
     * 关闭连接，重置文件描述符和状态
     */
    void Connection::Close()
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        if (fd_ >= 0)
        {
            if (state_ == State::CONNECTED)
            {
                state_ = State::DISCONNECTING;
                // // LOG_INFO("Closing connection to {}:{}, fd: {}", peer_addr_, peer_port_, fd_);
            }
            
            ::close(fd_);
            fd_ = -1;
            state_ = State::DISCONNECTED;
            
            // // LOG_INFO("Connection closed, was connected to {}:{}", peer_addr_, peer_port_);
        }
    }

    /**
     * 内部消息处理函数：调用用户设置的消息回调
     * @param request 已解析的 RPC 请求
     */
    void Connection::HandleMessage(const RpcRequest &request)
    {
        if (!message_callback_)
        {
            LOG_WARN("No message callback set, fd: {}", fd_);
            return;
        }

        try
        {
            message_callback_(shared_from_this(), request);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Message callback failed: {}, service: {}, method: {}, fd: {}",
                      e.what(), request.getServiceName(), request.getMethodName(), fd_);
        }
        catch (...)
        {
            LOG_ERROR("Unknown exception in message callback, fd: {}", fd_);
        }
    }

} // namespace cookrpc