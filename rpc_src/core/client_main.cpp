#include "rpc_client.h"
#include "error_code.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace cookrpc;

// 测试同步调用
void testSyncCall(RpcClient &client)
{
    std::cout << "\n=== Testing Synchronous Call ===" << std::endl;

    // 构造请求参数
    nlohmann::json request;
    request["message"] = "hello, i am rpc client";

    nlohmann::json response;
    bool success = client.Call<nlohmann::json, nlohmann::json>(
        "RpcService",        // 服务名
        "Echo",              // 方法名
        request,             // 请求参数
        SerializeType::JSON, // 序列化类型
        response             // 响应结果
    );

    if (success)
    {
        std::cout << "Sync call succeeded, response: " << response.dump() << std::endl;
    }
    else
    {
        std::cout << "Sync call failed" << std::endl;
    }
}

// 测试异步调用
void testAsyncCall(RpcClient &client)
{
    std::cout << "\n=== Testing Asynchronous Call ===" << std::endl;

    // 发起多个异步调用
    std::vector<std::future<nlohmann::json>> futures;

    for (int i = 0; i < 300; ++i)
    {
        json request;
        request["message"] = "Hello from async client " + std::to_string(i);

        // 发起异步调用
        auto future = client.AsyncCall<nlohmann::json, json>(
            "RpcService",
            "Echo",
            request,
            SerializeType::JSON);

        futures.push_back(std::move(future));

        // 模拟一些其他工作
        std::cout << "Async call " << i << " sent, doing other work..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 获取异步调用结果
    for (size_t i = 0; i < futures.size(); ++i)
    {
        try
        {
            nlohmann::json response = futures[i].get();
            std::cout << "Async call " << i << " succeeded, response: " << response.dump() << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cout << "Async call " << i << " failed: " << e.what() << std::endl;
        }
    }
}

// 测试错误处理
void testErrorHandling(RpcClient &client)
{
    std::cout << "\n=== Testing Error Handling ===" << std::endl;

    json request;
    request["message"] = "Test error handling";

    nlohmann::json response;
    bool success = client.Call<nlohmann::json, json>(
        "NonExistentService", // 不存在的服务
        "Echo",
        request,
        SerializeType::JSON,
        response);

    if (!success)
    {
        std::cout << "Expected error occurred: Service not found" << std::endl;
    }
}

int main()
{
    try
    {
        // 创建 RPC 客户端
        RpcClient client("../config/rpc_client.json");

        std::cout << "Connected to RPC server" << std::endl;

        // 测试同步调用
        testSyncCall(client);

        // 测试异步调用
        testAsyncCall(client);

        // 测试错误处理
        // testErrorHandling(client);

        std::cout << "\nAll tests completed" << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
