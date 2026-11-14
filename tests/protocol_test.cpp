#include <cstring>
#include <iostream>
#include <string>

#include "aes_encrypt.h"
#include "framing.h"
#include "protocol.h"

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char *expr, int line)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << expr << " (line " << line << ")" << std::endl;
            ++g_failures;
        }
    }

#define CHECK(cond) Check((cond), #cond, __LINE__)

    void TestRequestRoundTrip()
    {
        cookrpc::RpcRequest request;
        request.setServiceName("RpcService");
        request.setMethodName("Echo");
        request.setPayload(std::string("\x01\x02\x03payload", 11));
        request.setSequenceId(42);

        std::string encoded;
        CHECK(request.Serialize(encoded));
        CHECK(encoded.size() == sizeof(cookrpc::RpcHeader) +
                                    request.getServiceName().size() +
                                    request.getMethodName().size() +
                                    request.getPayload().size() + 3 * sizeof(uint32_t));

        cookrpc::RpcRequest decoded;
        CHECK(decoded.Deserialize(encoded));
        CHECK(decoded.getServiceName() == request.getServiceName());
        CHECK(decoded.getMethodName() == request.getMethodName());
        CHECK(decoded.getPayload() == request.getPayload());
        CHECK(decoded.getSequenceId() == request.getSequenceId());
    }

    void TestResponseRoundTrip()
    {
        cookrpc::RpcResponse response;
        response.setResultData("hello");
        response.setErrorMessage("");
        response.setErrorCode(0);
        response.setSequenceId(7);

        std::string encoded;
        CHECK(response.Serialize(encoded));

        cookrpc::RpcResponse decoded;
        CHECK(decoded.Deserialize(encoded));
        CHECK(decoded.getResultData() == "hello");
        CHECK(decoded.getErrorCode() == 0);
        CHECK(decoded.getSequenceId() == 7);
    }

    void TestInvalidInputs()
    {
        cookrpc::RpcRequest request;
        request.setServiceName("S");
        request.setMethodName("M");
        request.setPayload("P");
        request.setSequenceId(1);

        std::string encoded;
        CHECK(request.Serialize(encoded));

        // 魔数错误
        std::string bad_magic = encoded;
        bad_magic[0] = static_cast<char>(bad_magic[0] ^ 0xFF);
        cookrpc::RpcRequest decoded;
        CHECK(!decoded.Deserialize(bad_magic));

        // 输入被截断
        std::string truncated = encoded.substr(0, encoded.size() - 1);
        cookrpc::RpcRequest decoded2;
        CHECK(!decoded2.Deserialize(truncated));

        // 响应头中的 message_length 与实际长度不一致
        cookrpc::RpcResponse response;
        response.setResultData("data");
        response.setErrorCode(0);
        response.setSequenceId(2);
        std::string response_bytes;
        CHECK(response.Serialize(response_bytes));

        cookrpc::RpcHeader header;
        std::memcpy(&header, response_bytes.data(), sizeof(header));
        header.message_length += 1;
        std::memcpy(&response_bytes[0], &header, sizeof(header));

        cookrpc::RpcResponse decoded_response;
        CHECK(!decoded_response.Deserialize(response_bytes));
    }

    void TestFramingSingle()
    {
        const std::string payload = "hello frame";
        std::string buffer = cookrpc::framing::Encode(payload);

        std::string decoded;
        CHECK(cookrpc::framing::TryDecode(buffer, decoded) == cookrpc::framing::Status::OK);
        CHECK(decoded == payload);
        CHECK(buffer.empty());
    }

    void TestFramingSticky()
    {
        // 两个帧粘在一起，应当被依次取出
        std::string buffer = cookrpc::framing::Encode("first") + cookrpc::framing::Encode("second");

        std::string first;
        CHECK(cookrpc::framing::TryDecode(buffer, first) == cookrpc::framing::Status::OK);
        CHECK(first == "first");

        std::string second;
        CHECK(cookrpc::framing::TryDecode(buffer, second) == cookrpc::framing::Status::OK);
        CHECK(second == "second");
        CHECK(buffer.empty());
    }

    void TestFramingPartial()
    {
        // 半包：只给前半段，应当返回 NEED_MORE
        std::string full = cookrpc::framing::Encode("partial payload");
        std::string half = full.substr(0, full.size() / 2);

        std::string decoded;
        CHECK(cookrpc::framing::TryDecode(half, decoded) == cookrpc::framing::Status::NEED_MORE);

        // 补上剩余部分后应当能完整取出
        half += full.substr(full.size() / 2);
        CHECK(cookrpc::framing::TryDecode(half, decoded) == cookrpc::framing::Status::OK);
        CHECK(decoded == "partial payload");
    }

    void TestFramingInvalid()
    {
        std::string decoded;

        // 长度字段为 0
        std::string zero_len(4, '\0');
        CHECK(cookrpc::framing::TryDecode(zero_len, decoded) == cookrpc::framing::Status::INVALID);

        // 长度字段超过上限
        std::string huge = cookrpc::framing::Encode(std::string(8, 'x'));
        unsigned char *raw = reinterpret_cast<unsigned char *>(&huge[0]);
        raw[0] = 0xFF;
        raw[1] = 0xFF;
        raw[2] = 0xFF;
        raw[3] = 0xFF;
        CHECK(cookrpc::framing::TryDecode(huge, decoded) == cookrpc::framing::Status::INVALID);
    }

    void TestEncryptionRoundTrip()
    {
        const std::string plaintext = "AES-256-GCM round trip payload";

        std::string ciphertext;
        CHECK(cookrpc::AesEncrypt::getInstance().Encrypt(plaintext, ciphertext));
        CHECK(!ciphertext.empty());
        CHECK(ciphertext != plaintext);

        std::string decrypted;
        CHECK(cookrpc::AesEncrypt::getInstance().Decrypt(ciphertext, decrypted));
        CHECK(decrypted == plaintext);
    }

} // namespace

int main()
{
    TestRequestRoundTrip();
    TestResponseRoundTrip();
    TestInvalidInputs();
    TestFramingSingle();
    TestFramingSticky();
    TestFramingPartial();
    TestFramingInvalid();
    TestEncryptionRoundTrip();

    if (g_failures == 0)
    {
        std::cout << "All protocol tests passed." << std::endl;
        return 0;
    }

    std::cerr << g_failures << " check(s) failed." << std::endl;
    return 1;
}
