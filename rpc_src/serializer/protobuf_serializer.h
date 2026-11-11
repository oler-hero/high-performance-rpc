#pragma once
#include <string>
#include "message.pb.h"
#include "log_manager.h"

namespace cookrpc
{
    class ProtobufSerializer
    {
    public:
        template <typename T>
        static std::string serialize(const T &data)
        {
            try
            {
                return data.SerializeAsString();
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Protobuf serialization error: {}", e.what());
                return "";
            }
        }

        template <typename T>
        static bool deserialize(const std::string &buffer, T &data)
        {
            try
            {
                T new_data;
                if (!new_data.ParseFromString(buffer))
                {
                    LOG_ERROR("Failed to parse protobuf message");
                    return false;
                }
                data = new_data;
                return true;
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Protobuf deserialization error: {}", e.what());
                return false;
            }
        }
    };
}