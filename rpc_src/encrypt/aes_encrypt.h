#pragma once
#include <array>
#include <cstddef>
#include <string>

namespace cookrpc
{
    /**
     * @brief AES-256-GCM 加密工具（OpenSSL EVP 实现）
     *
     * 每条消息生成一个随机的 256 位会话密钥：
     * - 会话密钥使用 AES-256-GCM 加密消息体
     * - 主密钥使用 AES-256-GCM 包装会话密钥
     * - 最终结果做 Base64 编码，便于在文本协议中传输
     *
     * 说明：这是“会话密钥 + 主密钥”的两层对称加密，不是
     * “非对称 + 对称”的混合加密；如需混合加密，应增加 ECDH/RSA 密钥交换。
     */
    class AesEncrypt
    {
    public:
        // 单例模式
        static AesEncrypt &getInstance()
        {
            static AesEncrypt instance;
            return instance;
        }

        // 加密数据
        bool Encrypt(const std::string &data, std::string &ciphertext);

        // 解密数据
        bool Decrypt(const std::string &encrypted_data, std::string &plaintext);

        // 删除拷贝构造和赋值操作 - 单例模式的标准做法
        AesEncrypt(const AesEncrypt &) = delete;
        AesEncrypt &operator=(const AesEncrypt &) = delete;

    private:
        AesEncrypt();
        ~AesEncrypt() = default;

        /**
         * @brief Base64 编码
         */
        std::string Base64Encode(const std::string &input) const;

        /**
         * @brief Base64 解码
         */
        std::string Base64Decode(const std::string &input) const;

        /**
         * @brief 使用 AES-256-GCM 加密一段数据
         * @param plaintext 明文
         * @param key 32 字节密钥
         * @param nonce 12 字节随机数
         * @param ciphertext 输出密文
         * @param tag 输出 16 字节认证标签
         */
        bool AesGcmEncrypt(const std::string &plaintext,
                           const unsigned char *key,
                           const unsigned char *nonce,
                           std::string &ciphertext,
                           unsigned char *tag) const;

        /**
         * @brief 使用 AES-256-GCM 解密一段数据并校验认证标签
         */
        bool AesGcmDecrypt(const std::string &ciphertext,
                           const unsigned char *key,
                           const unsigned char *nonce,
                           const unsigned char *tag,
                           std::string &plaintext) const;

        static constexpr size_t KEY_LENGTH = 32;    // AES-256 密钥长度
        static constexpr size_t NONCE_LENGTH = 12;  // GCM 推荐 nonce 长度
        static constexpr size_t TAG_LENGTH = 16;    // GCM 认证标签长度
        static constexpr unsigned char FORMAT_VERSION = 0x01;

        std::array<unsigned char, KEY_LENGTH> master_key_{};
    };

} // namespace cookrpc
