#pragma once
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <future>
#include <sys/socket.h>
#include <netinet/in.h>
#include "message.pb.h"
#include "message.pb.h"
#include "connection.h"
#include "rpc_protocol.h"
#include "zstd_compress.h"
#include "aes_encrypt.h"
#include "framing.h"
#include "serializer_manager.h"
#include "log_manager.h"
#include "error_code.h"
#include "rpc_protocol.h"
namespace cookrpc
{

    class RpcClient
    {
    public:
        // 定义回调函数类型
        using ResponseCallback = std::function<void(const std::string &, bool)>;

        // 构造函数
        explicit RpcClient(const std::string &config_path = "../config/rpc_client.json");
        ~RpcClient();

        // 禁止拷贝和赋值
        RpcClient(const RpcClient &) = delete;
        RpcClient &operator=(const RpcClient &) = delete;

        // 初始化配置
        bool loadConfig(const std::string &conf_path);

        // 连接服务器
        bool Connect();

        std::string GetServerAddress(const std::string &zk_namespace) const;

        bool initSocket(int &fd);
        bool validateServerInfo(const std::string &ip, int port, int fd);
        bool tryConnect(int fd, const struct sockaddr_in &server_addr, int retry_count);
        bool waitForConnection(int fd, int retry_count);

        // 断开连接
        void Disconnect();

        // 同步调用
        template <typename Response, typename Request>
        bool Call(const std::string &service_name,
                  const std::string &method_name,
                  const Request &request,
                  SerializeType serialize_type,
                  Response &response)
        {
            try
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!checkConnection())
                {
                    LOG_ERROR("Connection check failed");
                    return false;
                }
                std::string encrypted_data;
                if (!prepareAndSerializeRequest(service_name, method_name, request,
                                                serialize_type, encrypted_data))
                {
                    return false;
                }
                if (!sendAndReceiveResponse(encrypted_data))
                {
                    return false;
                }
                // CLIENT_INFO("sendAndReceiveResponse success, fd: {}", socket_fd_);

                return processResponse(response, serialize_type);
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("RPC call exception: {}", e.what());
                return false;
            }
        }

        // 异步调用
        template <typename Response, typename Request>
        std::future<Response> AsyncCall(
            const std::string &service_name,
            const std::string &method_name,
            const Request &request,
            SerializeType serialize_type)
        {

            return std::async(std::launch::async,
                              [this, service_name, method_name, request, serialize_type]()
                              {
                                  Response response;
                                  if (this->Call<Response, Request>(
                                          service_name, method_name, request, serialize_type, response))
                                  {
                                      return response;
                                  }
                                  throw std::runtime_error("RPC call failed");
                              });
        }

        // 获取连接状态
        bool IsConnected() const { return is_connected_; }

    private:
        // 重连在 checkConnection 内完成：调用方已持有 mutex_，
        // 因此这里使用不加锁的 ConnectLocked / DisconnectLocked，避免重复加锁
        bool ConnectLocked();
        void DisconnectLocked();

        // 生成请求序列号
        uint64_t GenerateSequenceId();

        // 辅助函数声明
        bool checkConnection();

        template <typename Request>
        bool prepareAndSerializeRequest(
            const std::string &service_name,
            const std::string &method_name,
            const Request &request,
            SerializeType serialize_type,
            std::string &encrypted_data)
        {
            // 序列化用户数据
            std::string user_serialized_data = SerializerManager::serialize(request, serialize_type);
            if (user_serialized_data.empty())
            {
                LOG_ERROR("Failed to serialize request");
                return false;
            }
            // CLIENT_INFO("User data serialized, size: {}", user_serialized_data.size());
            
            // 创建和序列化 RPC 请求
            RpcRequest rpc_request;
            rpc_request.setServiceName(service_name);
            rpc_request.setMethodName(method_name);
            rpc_request.setPayload(user_serialized_data);
            rpc_request.setSequenceId(GenerateSequenceId());

            std::string rpc_serialized_data;
            if (!rpc_request.Serialize(rpc_serialized_data))
            {
                LOG_ERROR("RpcRequest serialize failed");
                return false;
            }
            // CLIENT_INFO("RPC request serialized, size: {}", rpc_serialized_data.size());

            // 压缩数据
            std::string compressed_data;
            if (!ZstdCompress::getInstance().CompressString(rpc_serialized_data, compressed_data))
            {
                LOG_ERROR("RpcRequest compress failed");
                return false;
            }
            // CLIENT_INFO("Data compressed, size: {} -> {}", rpc_serialized_data.size(), compressed_data.size());

            // 加密数据
            if (!AesEncrypt::getInstance().Encrypt(compressed_data, encrypted_data))
            {
                LOG_ERROR("RpcRequest encrypt failed");
                return false;
            }
            // CLIENT_INFO("Data encrypted, size: {} -> {}", compressed_data.size(), encrypted_data.size());

            return true;
        }

        bool sendAndReceiveResponse(const std::string &encrypted_data);

        template <typename Response>
        bool processResponse(Response &response, SerializeType serialize_type)
        {
            // 等待并读取响应数据
            if (!connection_->ReadWithTimeout(timeout_ms_))
            {
                LOG_ERROR("Failed to read response data within timeout");
                // 读超时说明连接已不可用，标记断开，下次调用走重连
                is_connected_ = false;
                return false;
            }

            // 获取读取到的原始数据
            std::string raw_response_data = connection_->GetReadBufferData();
            if (raw_response_data.empty())
            {
                LOG_ERROR("No response data received");
                is_connected_ = false;
                return false;
            }

            // 去掉 4 字节长度前缀，取出真正的消息体
            std::string framed_data = raw_response_data;
            if (framing::TryDecode(framed_data, raw_response_data) != framing::Status::OK)
            {
                LOG_ERROR("Incomplete response frame, buffered size: {}", framed_data.size());
                is_connected_ = false;
                return false;
            }

            // CLIENT_INFO("Received raw response data, size: {}", raw_response_data.size());

            // 解密响应数据
            std::string decrypted_data;
            if (!AesEncrypt::getInstance().Decrypt(raw_response_data, decrypted_data))
            {
                LOG_ERROR("Failed to decrypt response data");
                return false;
            }

            // CLIENT_INFO("Decrypted response data, size: {}", decrypted_data.size());

            // 解压缩响应数据  
            std::string decompressed_data;
            if (!ZstdCompress::getInstance().DecompressString(decrypted_data, decompressed_data))
            {
                LOG_ERROR("Failed to decompress response data");
                return false;
            }

            // CLIENT_INFO("Decompressed response data, size: {}", decompressed_data.size());

            // 反序列化RPC响应
            RpcResponse rpc_response;
            if (!rpc_response.Deserialize(decompressed_data))
            {
                LOG_ERROR("Failed to deserialize RPC response");
                return false;
            }

            // 检查RPC调用是否成功
            if (rpc_response.getErrorCode() != static_cast<uint32_t>(ErrorCode::SUCCESS))
            {
                LOG_ERROR("RPC call failed: {} (code: {})",
                          rpc_response.getErrorMessage(),
                          rpc_response.getErrorCode());
                return false;
            }

            // 反序列化用户响应数据
            std::string result_data = rpc_response.getResultData();
            if (!SerializerManager::deserialize(result_data, response, serialize_type))
            {
                LOG_ERROR("Failed to deserialize user response data");
                return false;
            }

            CLIENT_INFO("Successfully processed response");
            return true;
        }

    private:
        int timeout_ms_;           // 超时时间
        int retry_times_;          // 重试次数
        std::string zk_namespace_; // 用来记录 server 的 zk 命名空间
        int socket_fd_{-1};        // 序列号
        // int server_port_{-1};      // server 端口

        std::shared_ptr<Connection> connection_;
        std::mutex mutex_; // 保护连接对象

        std::atomic<uint64_t> sequence_id_{0}; // 请求序列号
        std::atomic<bool> is_connected_{false};
    };

} // namespace cookrpc