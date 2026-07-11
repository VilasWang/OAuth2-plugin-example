#pragma once

// Task 13 (authforge-sdk-refactor, design.md §3.3/§5.1/§6): libs/common
// Domain kernel. AuditEvent is the framework-agnostic counterpart of
// oauth2::observability::AuditEvent (OAuth2Plugin/include/oauth2/
// observability/AuditLogger.h), which is a plain data struct EXCEPT for one
// field (`details`) declared as Json::Value and one convenience overload
// of AuditLogger::log() that takes a drogon::HttpRequestPtr to pull ip/
// user-agent/request-id out of. This type ports the data struct only
// (Json::Value is allowed in the Domain layer per design.md §4.1 rule 1);
// the HttpRequestPtr-convenience overload and the actual async DB-writing
// AuditLogger::log(const AuditEvent&) implementation both stay Adapter-side
// (they depend on Drogon's HttpRequestPtr and, transitively through the
// existing implementation, drogon::orm), consistent with design.md §3.3's
// "Observability（可观测） | 审计/metrics 模型 -> common；导出器 -> 适配器"
// split: the *model* moves to common, the *sink* (how/where an event gets
// persisted or exported) stays an Adapter concern.

#include <json/json.h>
#include <string>

namespace authforge::common::observability
{

/**
 * @brief Structured audit event (design.md §3.3's AuditEvent model).
 * Framework-agnostic: Domain-layer code can construct and pass this value
 * around without depending on Drogon; only the eventual sink (a future
 * Adapter-side audit writer) needs Drogon/DB types.
 */
struct AuditEvent
{
    std::string actorType;   ///< "user", "client", "system"
    std::string actorId;     ///< user identifier or client_id
    std::string action;      ///< "login_success", "token_issued", etc.
    std::string targetType;  ///< "token", "user", "client", etc.
    std::string targetId;    ///< target identifier
    std::string outcome;     ///< "success" or "failure"
    std::string ip;
    std::string userAgent;
    std::string requestId;
    Json::Value details;  ///< Additional context (Json::Value; jsoncpp is
                          ///< allowed in the Domain layer, design.md §4.1).
};

}  // namespace authforge::common::observability
