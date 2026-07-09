#pragma once

// M2b Task 17 slice 5 (authforge-sdk-refactor, design.md §3.3/§5.6):
// Adapter-side default implementation of
// authforge::common::ports::IAuditSink, backed by the existing
// oauth2::observability::AuditLogger (which persists to the database via
// drogon::app().getDbClient() -- an Adapter-layer concern, design.md
// §4.1 rule 3: Adapter layer is allowed to depend on Drogon).
//
// This is a thin translation adapter: it converts a
// authforge::common::observability::AuditEvent (the Domain-facing model,
// Task 13) into the pre-existing oauth2::observability::AuditEvent (the
// struct AuditLogger::log() already accepts) and forwards it, field for
// field. AuditLogger itself is NOT modified or duplicated -- this class
// exists purely so Domain-layer code (e.g. libs/oauth2's TokenService,
// once migrated) can depend on the port (IAuditSink) instead of directly
// depending on AuditLogger/Drogon/drogon::orm.
//
// Placement note: same rationale as DrogonLogger.h/OpenSslCryptoProvider.h
// -- kept under OAuth2Plugin/include/oauth2/adapters/ rather than a
// not-yet-created libs/drogon (M3, Task 20); a later milestone's directory
// move (Task 39) is expected to relocate this file verbatim.

#include <authforge/common/ports/IAuditSink.h>

namespace oauth2::adapters
{

/**
 * @brief Forwards IAuditSink::record() calls to
 * oauth2::observability::AuditLogger::log(const AuditEvent&), translating
 * between the two (structurally identical) AuditEvent shapes. Stateless;
 * safe to use a single shared instance from any thread (AuditLogger::log
 * is itself documented as safe to call concurrently -- it is a
 * fire-and-forget async DB write with no shared mutable state of its
 * own).
 */
class DrogonAuditSink : public authforge::common::ports::IAuditSink
{
  public:
    void record(const authforge::common::observability::AuditEvent &event) override;
};

}  // namespace oauth2::adapters
