#pragma once
#include <string>
#include <vector>
#include <memory>

namespace cookrpc
{
    /**
     * @brief 使用单例模式的高安全性加密工具
     * 
     * 设计特点：
     * 单例模式：确保全局只有一个加密实例，避免多实例带来的安全隐患
     * 双重加密机制，使用主密钥加密会话密钥，会话密钥加密实际数据
     * 随机会话密钥，每次加密都生成新的随机会话密钥，大大提高安全性
     * Base64编码，确保加密结果可以安全地在网络中传输
     * 自定义移位加密，实现了基于位移的加密算法，个人习惯
     * 
     * 注意：虽然类名为 AesEncrypt，但实际使用的是自定义的移位加密算法，
     * 当然这里各位可以自己引入第三方加密库，比如openssl，这里只是提供一个思路
     * 各位可以自由发挥
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

        // 内部加密相关方法
        // 生成密钥，当前未使用，可以测试时使用
        std::string GenerateKey() const;
        
        /**
         * @brief Base64编码
         * @param input 待编码的二进制数据
         * @return 编码后的Base64字符串
         * 
         * 自实现的Base64编码，避免第三方库依赖
         */
        std::string Base64Encode(const std::string &input) const;
        
        /**
         * @brief Base64解码
         * @param input Base64编码的字符串
         * @return 解码后的二进制数据
         */
        std::string Base64Decode(const std::string &input) const;
        
        /**
         * @brief 移位加密算法
         * @param input 待加密的数据
         * @param key 加密密钥
         * @return 加密后的数据
         * 
         * 核心加密算法：使用密钥和位置偏移进行字符级别的移位加密
         * 算法特点：每个字符的加密结果不仅依赖于密钥，还依赖于字符在数据中的位置
         */
        std::string ShiftEncrypt(const std::string &input, const std::string &key) const;
        
        /**
         * @brief 移位解密算法
         * @param input 待解密的数据
         * @param key 解密密钥
         * @return 解密后的数据
         * 
         * 与ShiftEncrypt相对应的解密算法
         */
        std::string ShiftDecrypt(const std::string &input, const std::string &key) const;

        // === 成员变量 ===
        std::string master_key_;                 // 主密钥 - 用于加密会话密钥
        static constexpr size_t KEY_LENGTH = 32; // 会话密钥长度（字节）
        static constexpr size_t BLOCK_SIZE = 16; // 加密块大小（当前项目中未使用，各位同学可以自行拓展）
    };

} // namespace cookrpc