// 防止头文件重复包含（兼容大多数编译器）
#pragma once

#include "json_serializer.h"      // JSON 序列化器实现
#include "log_manager.h"          // 日志记录管理（用于错误输出）
#include "protobuf_serializer.h"  // Protobuf 序列化器实现

namespace cookrpc
{
    /**
     * @brief 支持的序列化类型枚举
     * 
     * 用于在运行时指定使用哪种序列化格式。
     */
    enum class SerializeType
    {
        JSON,      ///< JSON 格式（使用 nlohmann::json 库）
        PROTOBUF   ///< Protocol Buffers 格式（Google Protobuf）
    };

    /**
     * @class SerializerManager
     * @brief 序列化/反序列化的统一管理类（静态方法）
     * 
     * 本类提供模板化的序列化与反序列化接口，根据传入的 SerializeType
     * 和模板类型 T，在编译期进行类型检查，并调用对应的具体序列化器。
     * 所有方法均为静态，无需实例化。
     */
    class SerializerManager
    {
    public:
        /**
         * @brief 将数据序列化为字符串
         * 
         * @tparam T 待序列化的数据类型（必须与 type 匹配）
         * @param data 待序列化的数据引用
         * @param type 指定的序列化类型（JSON 或 PROTOBUF）
         * @return std::string 序列化后的二进制或文本字符串，失败返回空字符串
         * 
         * @note 编译期类型检查：
         *       - 当 type == JSON 时，T 必须是 nlohmann::json 类型；
         *       - 当 type == PROTOBUF 时，T 必须继承自 google::protobuf::Message。
         *       若不满足，会在编译期报错（if constexpr）或运行时记录错误。
         */
        template <typename T>
        static std::string serialize(const T &data, SerializeType type)
        {
            try
            {
                switch (type)
                {
                case SerializeType::JSON:
                    // 编译期检查：只有 T 是 nlohmann::json 时才允许编译此分支
                    if constexpr (std::is_same_v<T, nlohmann::json>)
                    {
                        return serialize_json(data);
                    }
                    else
                    {
                        // 若类型不匹配，记录错误并返回空字符串（运行时）
                        LOG_ERROR("Invalid type for JSON serialization");
                        return "";
                    }
                case SerializeType::PROTOBUF:
                    // 编译期检查：只有 T 是 protobuf Message 的子类时才允许编译此分支
                    if constexpr (std::is_base_of_v<google::protobuf::Message, T>)
                    {
                        return serialize_protobuf(data);
                    }
                    else
                    {
                        LOG_ERROR("Invalid type for Protobuf serialization");
                        return "";
                    }
                default:
                    LOG_ERROR("Unknown serialization type");
                    return "";
                }
            }
            catch (const std::exception &e)
            {
                // 捕获序列化过程中的任何异常，记录日志并返回空
                LOG_ERROR("Serialization error: {}", e.what());
                return "";
            }
        }

        /**
         * @brief 从字符串反序列化为数据对象
         * 
         * @tparam T 目标数据类型（必须与 type 匹配）
         * @param buffer 包含序列化数据的字符串
         * @param data 用于接收反序列化结果的引用
         * @param type 指定的序列化类型
         * @return bool 反序列化是否成功
         * 
         * @note 类型检查规则同 serialize()，失败时记录错误并返回 false。
         */
        template <typename T>
        static bool deserialize(const std::string &buffer, T &data, SerializeType type)
        {
            try
            {
                switch (type)
                {
                case SerializeType::JSON:
                    if constexpr (std::is_same_v<T, nlohmann::json>)
                    {
                        return deserialize_json(buffer, data);
                    }
                    else
                    {
                        LOG_ERROR("Invalid type for JSON deserialization");
                        return false;
                    }
                case SerializeType::PROTOBUF:
                    if constexpr (std::is_base_of_v<google::protobuf::Message, T>)
                    {
                        return deserialize_protobuf(buffer, data);
                    }
                    else
                    {
                        LOG_ERROR("Invalid type for Protobuf deserialization");
                        return false;
                    }
                default:
                    LOG_ERROR("Unknown serialization type");
                    return false;
                }
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Deserialization error: {}", e.what());
                return false;
            }
        }

    private:
        // ------------------- 私有序列化/反序列化辅助函数 -------------------

        /**
         * @brief 调用 ProtobufSerializer 进行序列化（转发）
         */
        template <typename T>
        static std::string serialize_protobuf(const T &data)
        {
            return ProtobufSerializer::serialize(data);
        }

        /**
         * @brief 调用 JSONSerializer 进行序列化（转发）
         */
        static std::string serialize_json(const nlohmann::json &data)
        {
            return JSONSerializer::serialize(data);
        }

        /**
         * @brief 调用 ProtobufSerializer 进行反序列化（转发）
         */
        template <typename T>
        static bool deserialize_protobuf(const std::string &buffer, T &data)
        {
            return ProtobufSerializer::deserialize(buffer, data);
        }

        /**
         * @brief 调用 JSONSerializer 进行反序列化（转发）
         */
        template <typename T>
        static bool deserialize_json(const std::string &buffer, T &data)
        {
            return JSONSerializer::deserialize(buffer, data);
        }
    };
}