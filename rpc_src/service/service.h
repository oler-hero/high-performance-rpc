
#pragma once
#include <string>

namespace cookrpc
{
    // Service 是 RPC 服务的抽象基类，所有具体的 RPC 服务实现都应继承自该类。
    // 它定义了服务名称获取和请求处理的标准接口，用于服务注册、路由和调用。
    class Service
    {
    public:
        // 虚析构函数，确保派生类对象能够正确析构，释放资源。
        virtual ~Service() = default;

        // 获取服务的名称。
        // 该名称通常用于服务注册中心或 RPC 路由，以便客户端能够定位到正确的服务。
        // 返回值：服务名称字符串，建议使用全限定名（如 "UserService"）以保证唯一性。
        virtual std::string GetServiceName() const = 0;

        // 处理 RPC 请求。
        // 参数：
        //   method_name - 要调用的具体方法名称（如 "GetUserInfo"）
        //   args        - 请求参数，通常为序列化后的数据（如 JSON、Protobuf、Thrift 等格式）
        //   result      - 输出参数，用于存放处理结果，同样为序列化后的数据
        // 返回值：true 表示处理成功，false 表示处理失败（如方法不存在、参数错误等）。
        // 注意：实现类需要根据 method_name 分发到具体的业务逻辑，并将结果序列化到 result 中。
        virtual bool HandleRequest(const std::string &method_name,
                                   const std::string &args,
                                   std::string &result) = 0;
    };

} // namespace cookrpc
