#include "util_fun.h"
#include <regex>

bool IsValidIpAddress(const std::string &ip)
{
    const std::regex ip_pattern(
        R"((\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3}))");
    std::smatch match;

    if (std::regex_match(ip, match, ip_pattern))
    {
        for (int i = 1; i <= 4; ++i)
        {
            int octet = std::stoi(match[i].str());
            if (octet < 0 || octet > 255)
            {
                return false;
            }
        }
        return true;
    }
    return false;
}