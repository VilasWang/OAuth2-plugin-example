#pragma once

// M2b Task 17 slice 5 (fulla-sdk-refactor, design.md §3.3/§5.1/§6):
// libs/common ports. IAuditSink is the port Domain code (e.g. libs/oauth2's
// TokenService, once migrated) uses to emit an
// fulla::common::observability::AuditEvent without depending on the
// concrete audit sink implementation.
//
// design.md §3.3 splits Observability as "审计/metrics 模型 → common；
// 导出器 → 适配器" (model -> common, exporter/sink -> Adapter) and Task 13
// already ported the AuditEvent model itself
// (observability/AuditEvent.h). It did not declare a port for the sink
// side, because at that point no Domain-layer code needed to emit an
// audit event yet. This port now exists because TokenService's migration
// (M2b) needs one: the existing fulla::drogon::observability::AuditLogger
// (OAuth2Plugin/include/oauth2/observability/AuditLogger.h) depends on
// drogon::app().getDbClient() and drogon::orm::DrogonDbException directly,
// which the Domain layer must not depend on (design.md §4.1 rule 1).
//
// Mirrors IMetrics.h's shape/rationale exactly: a small, generic
// interface (one method, taking the already-common AuditEvent) rather
// than porting AuditLogger's own hard-coded method names. The default
// production implementation is Adapter-side (a thin wrapper around the
// existing fulla::drogon::observability::AuditLogger::log(const AuditEvent&),
// translating between the two AuditEvent struct shapes at the boundary);
// a test double can capture emitted events without any DB dependency.

#include <fulla/common/observability/AuditEvent.h>

namespace fulla::common::ports
{

/**
 * @brief Audit event emission port for Domain code (design.md §3.3). The
 * default production implementation is Adapter-side, persisting the event
 * (e.g. to a database) or forwarding it to whatever concrete audit sink
 * the product wires up; a test double can simply record emitted events.
 */
class IAuditSink
{
  public:
    virtual ~IAuditSink() = default;

    /// Record an audit event. Implementations MUST NOT block the calling
    /// thread on I/O (mirrors the existing AuditLogger::log's "fire and
    /// forget" contract) and MUST NOT throw.
    virtual void record(const fulla::common::observability::AuditEvent &event) = 0;
};

}  // namespace fulla::common::ports
