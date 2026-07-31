#include <authforge/drogon/adapters/DrogonLogger.h>
#include <drogon/drogon.h>

namespace authforge::drogon::adapters
{

using authforge::common::ports::LogLevel;

void DrogonLogger::log(LogLevel level, const std::string &message)
{
    switch (level)
    {
        case LogLevel::Debug:
            LOG_DEBUG << message;
            break;
        case LogLevel::Info:
            LOG_INFO << message;
            break;
        case LogLevel::Warn:
            LOG_WARN << message;
            break;
        case LogLevel::Error:
            LOG_ERROR << message;
            break;
        case LogLevel::Fatal:
            LOG_FATAL << message;
            break;
    }
}

}  // namespace authforge::drogon::adapters
