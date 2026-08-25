#pragma once

// M8 Task 40 (fulla-sdk-refactor, design.md §5.2 decision b): the
// AuditEvent model now lives solely in fulla::common::observability
// (libs/common, the Domain-layer AuditEvent ported in Task 13). This header
// no longer re-declares a parallel AuditEvent struct -- DrogonAuditSink and
// every call site use fulla::common::observability::AuditEvent directly.
//
// The HttpRequestPtr-convenience overload of log() (which extracted ip /
// user-agent / request-id from a Drogon request) moved to DrogonAuditSink
// (logFromRequest) -- that Drogon-coupled helper is an Adapter concern, while
// this AuditLogger keeps only the Adapter-side async DB-write of a plain
// AuditEvent. Domain code reaches audit via the IAuditSink port, not here.

#include <fulla/common/observability/AuditEvent.h>

namespace fulla::drogon::observability
{

/**
 * @brief Asynchronous audit logger (Adapter side). Writes a
 * fulla::common::observability::AuditEvent to the database without
 * blocking the main flow. Domain-layer code should NOT call this directly --
 * it goes through fulla::common::ports::IAuditSink (DrogonAuditSink), which
 * forwards here.
 */
class AuditLogger
{
  public:
    /**
     * @brief Log an audit event asynchronously (fire-and-forget DB write).
     */
    static void log(const fulla::common::observability::AuditEvent &event);
};

}  // namespace fulla::drogon::observability
