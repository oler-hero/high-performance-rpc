/**
 * @file zstd_compress.h
 * @brief Zstandard压缩算法封装类的头文件
 * @details 提供基于Zstandard库的字符串和二进制数据压缩/解压功能
 */

 #pragma once
 #include <memory>
 #include <string>
 #include <vector>
 #include <zstd.h>
 #include <mutex>
 
 namespace cookrpc
 {
     enum class Level
     {
         FASTEST = 1, ///< 最快压缩速度，最低压缩率
         DEFAULT = 3, ///< 默认压缩级别，平衡速度和压缩率
         BETTER = 7,  ///< 较好的压缩率，中等速度
         BEST = 19,   ///< 最佳压缩率，但仍保持合理速度
         MAX = 22     ///< 最大压缩率，速度最慢, 这块可以自行选择压缩级别
     };
     class ZstdCompress
     {
     public:
         // 线程安全的单例模式
         static ZstdCompress &getInstance()
         {
             static ZstdCompress instance;
             return instance;
         }
 
         /**
          * @brief 压缩字符串
          * @param src 待压缩的源字符串
          * @param dst 压缩后的目标字符串，会被自动调整大小
          * @param level 压缩级别，默认为Level::DEFAULT
          * @return bool 压缩成功返回true，失败返回false
          * @details 如果源字符串为空，目标字符串会被清空并返回true
          */
         bool CompressString(const std::string &src, std::string &dst, Level level = Level::DEFAULT);
 
         /**
          * @brief 解压字符串
          * @param src 待解压的压缩字符串
          * @param dst 解压后的目标字符串，会被自动调整大小
          * @return bool 解压成功返回true，失败返回false
          * @details 如果源字符串为空，目标字符串会被清空并返回true
          */
         bool DecompressString(const std::string &src, std::string &dst);
 
         
         bool CompressData(const char *src, size_t src_len, std::vector<char> &dst, Level level = Level::DEFAULT);
 
      
         bool DecompressData(const char *src, size_t src_len, std::vector<char> &dst);
 
         // 获取压缩后的最大可能大小
         size_t GetCompressBound(size_t src_size) const
         {
             return ZSTD_compressBound(src_size);
         }
 
         // 禁止拷贝构造和拷贝赋值
         ZstdCompress(const ZstdCompress &) = delete;
         ZstdCompress &operator=(const ZstdCompress &) = delete;
         
         // 禁止移动构造和移动赋值
         ZstdCompress(ZstdCompress &&) = delete;
         ZstdCompress &operator=(ZstdCompress &&) = delete;
 
     private:
         ZstdCompress() = default;
         ~ZstdCompress();
     };
 }