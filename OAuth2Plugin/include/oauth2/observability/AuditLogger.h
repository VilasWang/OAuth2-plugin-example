#pragma once

#include <string>
#include <json/json.h>
#include <drogon/HttpRequest.h>

namespace authforge::drogon::observability
{

/**
 * @brief Structured audit event
 */
struct AuditEvent
{
    std::string actorType;   // "user", "client", "system"
    std::string actorId;     // user UUID or client_id
    std::string action;      // "login_success", "token_issued", etc.
    std::string targetType;  // "token", "user", "client", etc.
    std::string targetId;    // target identifier
    std::string outcome;     // "success" or "failure"
    std::string ip;
    std::string userAgent;
    std::string requestId;
    Json::Value details;  // Additional context
};

/**
 * @brief Asynchronous audit logger
 * Writes audit events to the database without blocking the main flow.
 */
class AuditLogger
{
  public:
    /**
     * @brief Log an audit event asynchronously
     * @param event The audit event to record
     */
    static void log(const AuditEvent &event);

    /**
     * @brief Convenience: log from an HTTP request context
     */
    static void log(
      const std::string &action,
      const std::string &outcome,
      const ::drogon::HttpRequestPtr &req,
      const std::string &actorId = "",
      const std::string &targetType = "",
      const std::string &targetId = "",
      const Json::Value &details = Json::Value()
    );
};

}  // namespace authforge::drogon::observability
