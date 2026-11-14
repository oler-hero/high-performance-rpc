#pragma once
#include "service/service.h"
#include <nlohmann/json.hpp>

namespace cookrpc
{

    class RpcService : public Service
    {
    public:
        std::string GetServiceName() const override
        {
            return "RpcService"; // 服务名，这里可以自己定义
        }

        bool HandleRequest(const std::string &method_name,
                           const std::string &args,
                           std::string &result) override
        {
            if (method_name == "Echo")
            {
                // 解析请求参数
                try
                {
                    nlohmann::json request = nlohmann::json::parse(args);
                    
                    // 创建JSON响应
                    nlohmann::json response;
                    response["echo"] = "hello from cookrpc server";
                    response["received_message"] = request.value("message", "");
                    
                    // 序列化为字符串返回
                    result = response.dump();
                    return true;
                }
                catch (const std::exception &e)
                {
                    LOG_ERROR("Failed to process Echo request: {}", e.what());
                    
                    // 返回错误的JSON响应
                    nlohmann::json error_response;
                    error_response["error"] = "Invalid request format";
                    result = error_response.dump();
                    return false;
                }
            }

            LOG_ERROR("Unknown method: {}", method_name);
            return false;
        }
    };

} // namespace cookrpc