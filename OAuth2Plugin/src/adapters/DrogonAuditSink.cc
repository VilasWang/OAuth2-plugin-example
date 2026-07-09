#include <oauth2/adapters/DrogonAuditSink.h>
#include <oauth2/observability/AuditLogger.h>

namespace oauth2::adapters
{

void DrogonAuditSink::record(const authforge::common::observability::AuditEvent &event)
{
    oauth2::observability::AuditEvent legacyEvent;
    legacyEvent.actorType = event.actorType;
    legacyEvent.actorId = event.actorId;
    legacyEvent.action = event.action;
    legacyEvent.targetType = event.targetType;
    legacyEvent.targetId = event.targetId;
    legacyEvent.outcome = event.outcome;
    legacyEvent.ip = event.ip;
    legacyEvent.userAgent = event.userAgent;
    legacyEvent.requestId = event.requestId;
    legacyEvent.details = event.details;

    oauth2::observability::AuditLogger::log(legacyEvent);
}

}  // namespace oauth2::adapters
