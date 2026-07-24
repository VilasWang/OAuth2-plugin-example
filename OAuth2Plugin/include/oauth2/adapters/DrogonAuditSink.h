#pragma once

// M2b Task 17 slice 5 / M8 Task 40 (authforge-sdk-refactor, design.md §3.3/
// §5.2 decision b): Adapter-side default implementation of
// authforge::common::ports::IAuditSink, backed by
// authforge::drogon::observability::AuditLogger (which persists to the database
// via ::drogon::app().getDbClient() -- an Adapter-layer concern, design.md
// §4.1 rule 3: Adapter layer is allowed to depend on Drogon).
//
// M8 Task 40: AuditEvent is now unified (authforge::common::observability::
// AuditEvent) -- record() forwards it to AuditLogger::log verbatim, no shape
// translation. The HttpRequestPtr-convenience helper logFromRequest() (moved
// here from AuditLogger's old convenience overload) builds a common AuditEvent
// from a Drogon request and forwards via record(); it is the path Drogon-layer
// controllers use (via getAuditSink()->...).

#include <authforge/common/observability/AuditEvent.h>
#include <authforge/common/ports/IAuditSink.h>

#include <drogon/HttpRequest.h>
#include <json/json.h>
#include <memory>
#include <string>

namespace authforge::drogon::adapters
{

class DrogonAuditSink : public authforge::common::ports::IAuditSink
{
  public:
    /// Record (fire-and-forget DB write) a fully-formed audit event.
    void record(const authforge::common::observability::AuditEvent &event) override;

    /**
     * @brief Convenience: build an AuditEvent from an HTTP request context and
     * record it. Mirrors the former AuditLogger::log(action, outcome, req, ...)
     * overload -- extracts ip (X-Forwarded-For / X-Real-IP / peer), user-agent,
     * and request-id (X-Request-ID, else a generated UUID), infers actorType
     * from actorId, and forwards via record(). Null sink -> no-op.
     */
    static void logFromRequest(
      const std::shared_ptr<authforge::common::ports::IAuditSink> &sink,
      const std::string &action,
      const std::string &outcome,
      const ::drogon::HttpRequestPtr &req,
      const std::string &actorId = "",
      const std::string &targetType = "",
      const std::string &targetId = "",
      const Json::Value &details = Json::Value()
    );
};

}  // namespace authforge::drogon::adapters
