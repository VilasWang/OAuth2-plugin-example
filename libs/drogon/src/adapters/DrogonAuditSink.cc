#include <fulla/drogon/adapters/DrogonAuditSink.h>
#include <fulla/drogon/observability/AuditLogger.h>
#include <fulla/drogon/adapters/OpenSslUuidGenerator.h>

namespace fulla::drogon::adapters
{

void DrogonAuditSink::record(const fulla::common::observability::AuditEvent &event)
{
    // M8 Task 40: AuditEvent is now unified (fulla::common::observability::
    // AuditEvent); forward verbatim -- no shape translation.
    fulla::drogon::observability::AuditLogger::log(event);
}

void DrogonAuditSink::logFromRequest(
  const std::shared_ptr<fulla::common::ports::IAuditSink> &sink,
  const std::string &action,
  const std::string &outcome,
  const ::drogon::HttpRequestPtr &req,
  const std::string &actorId,
  const std::string &targetType,
  const std::string &targetId,
  const Json::Value &details
)
{
    if (!sink)
    {
        return;
    }

    fulla::common::observability::AuditEvent event;
    event.action = action;
    event.outcome = outcome;
    event.actorId = actorId;
    event.targetType = targetType;
    event.targetId = targetId;
    event.details = details;

    // Determine actor type
    if (actorId.empty())
        event.actorType = "anonymous";
    else if (actorId.find("client:") == 0)
        event.actorType = "client";
    else
        event.actorType = "user";

    // Extract request context
    if (req)
    {
        // IP: prefer X-Forwarded-For, then X-Real-IP, then peer
        event.ip = req->getHeader("X-Forwarded-For");
        if (event.ip.empty())
            event.ip = req->getHeader("X-Real-IP");
        if (event.ip.empty())
            event.ip = req->getPeerAddr().toIp();

        event.userAgent = req->getHeader("User-Agent");
        event.requestId = req->getHeader("X-Request-ID");
        if (event.requestId.empty())
        {
            // Task 14 (design.md §5.6): migrated off drogon::utils::getUuid()
            // onto the fulla::common::ports::IUuidGenerator Adapter
            // implementation (OpenSslUuidGenerator).
            static fulla::drogon::adapters::OpenSslUuidGenerator uuidGenerator;
            event.requestId = uuidGenerator.generate();
        }
    }

    sink->record(event);
}

}  // namespace fulla::drogon::adapters
