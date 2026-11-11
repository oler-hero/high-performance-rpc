#include "zstd_compress.h"
#include <stdexcept>
#include <iostream>
namespace cookrpc

{
    /**
     * @brief 压缩字符串实现
     * @param src 待压缩的源字符串
     * @param dst 压缩后的目标字符串
     * @param level 压缩级别
     * @return bool 压缩成功返回true，失败返回false
     */
    bool ZstdCompress::CompressString(const std::string &src, std::string &dst, Level level)
    {
        // 处理空字符串的情况
        if (src.empty())
        {
            dst.clear();
            return true;
        }

        // 用于估算压缩后的最大字节数
        // 压缩算法通常会让数据变小，但在某些极端情况下（例如已经高度压缩的数据）
        // 压缩后的数据可能比原始数据更大。ZSTD_compressBound() 就是为了处理这种情况，
        // 确保输出缓冲区足够大
        size_t const compressed_size = ZSTD_compressBound(src.size());
        // 预分配足够的空间 
        // 在添加元素时可能需要重新分配内存，这会导致性能开销。
        // 通过预先调整大小，可以避免这种开销
        dst.resize(compressed_size);

        // 执行压缩操作
        size_t const actual_size = ZSTD_compress(
            dst.data(), compressed_size,        // 目标缓冲区和大小
            src.data(), src.size(),             // 源数据和大小
            static_cast<int>(level));           // 压缩级别

        // 检查压缩是否成功
        if (ZSTD_isError(actual_size))
        {
            return false;
        }

        // 调整目标字符串大小为实际压缩后的大小
        dst.resize(actual_size);
        return true;
    }

    //解压字符串实现
    bool ZstdCompress::DecompressString(const std::string &src, std::string &dst)
    {
        // 处理空字符串的情况
        if (src.empty())
        {
            dst.clear();
            return true;
        }

        // 获取解压后的原始数据大小
        unsigned long long const decompressed_size = ZSTD_getFrameContentSize(src.data(), src.size());
        
        // 检查是否能够获取到有效的解压大小
        if (decompressed_size == ZSTD_CONTENTSIZE_ERROR || decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN)
        {
            return false;
        }

        // 分配足够的空间用于解压
        dst.resize(decompressed_size);
        
        // 执行解压操作
        size_t const actual_size = ZSTD_decompress(
            dst.data(), decompressed_size,      // 目标缓冲区和大小
            src.data(), src.size());            // 源压缩数据和大小

        // 检查解压是否成功
        if (ZSTD_isError(actual_size))
        {
            return false;
        }

        // 调整目标字符串大小为实际解压后的大小
        dst.resize(actual_size);
        return true;
    }

    // 压缩二进制数据实现
    bool ZstdCompress::CompressData(const char *src, size_t src_len, std::vector<char> &dst, Level level)
    {
        // 处理空数据的情况
        if (!src || src_len == 0)
        {
            dst.clear();
            return true;
        }

        size_t const compressed_size = ZSTD_compressBound(src_len);
        dst.resize(compressed_size);

        // 执行压缩操作
        size_t const actual_size = ZSTD_compress(
            dst.data(), compressed_size,        // 目标缓冲区和大小
            src, src_len,                       // 源数据和大小
            static_cast<int>(level));           // 压缩级别

        // 检查压缩是否成功
        if (ZSTD_isError(actual_size))
        {
            return false;
        }

        // 调整目标容器大小为实际压缩后的大小
        dst.resize(actual_size);
        return true;
    }

    bool ZstdCompress::DecompressData(const char *src, size_t src_len, std::vector<char> &dst)
    {
        // 处理空数据的情况
        if (!src || src_len == 0)
        {
            dst.clear();
            return true;
        }

        // 获取解压后的原始数据大小
        unsigned long long const decompressed_size = ZSTD_getFrameContentSize(src, src_len);
        
        // 检查是否能够获取到有效的解压大小
        if (decompressed_size == ZSTD_CONTENTSIZE_ERROR || decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN)
        {
            return false;
        }

        dst.resize(decompressed_size);

        size_t const actual_size = ZSTD_decompress(
            dst.data(), decompressed_size,      // 目标缓冲区和大小
            src, src_len);                      // 源压缩数据和大小

        if (ZSTD_isError(actual_size))
        {
            return false;
        }

        dst.resize(actual_size);
        return true;
    }

   
    ZstdCompress::~ZstdCompress()
    {
    }

} // namespace cookrpc