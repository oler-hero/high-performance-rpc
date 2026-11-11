#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include "log_manager.h"
namespace cookrpc
{
    class JSONSerializer
    {
    public:
        template <typename T>
        static std::string serialize(const T &data)
        {
            try
            {
                nlohmann::json j = data;
                return j.dump();
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("JSON serialization error: {}", e.what());
                return "";
            }
        }

        template <typename T>
        static bool deserialize(const std::string &buffer, T &data)
        {
            try
            {
                nlohmann::json j = nlohmann::json::parse(buffer);
                data = j.get<T>();
                return true;
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("JSON deserialization error: {}", e.what());
                return false;
            }
        }
    };
}