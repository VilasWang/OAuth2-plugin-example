#pragma once

#include <oauth2/error/ErrorTypes.h>
#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <functional>

namespace authforge::common::error
{
using ::drogon::orm::DrogonDbException;

class ErrorHandler
{
  public:
    // Handle specific exception types
    static Error handleDbException(
      const DrogonDbException &e,
      const ::drogon::HttpRequestPtr &req = nullptr
    );
    static Error handleValidationError(
      const std::string &field,
      const std::string &reason,
      const ::drogon::HttpRequestPtr &req = nullptr
    );

    // Utility functions
    [[deprecated("Use RequestId::generate() for consistent UUID format")]]
    static std::string generateRequestId();
    static void logError(const Error &error, const std::string &context = "");
};

}  // namespace authforge::common::error
