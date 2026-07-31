#pragma once

// Task 13 (authforge-sdk-refactor, design.md §3.1/§3.3/§5.1/§6): value
// objects for the shared Domain kernel. TenantId is explicitly a "reserved
// dimension seam" per design.md §1.2 non-goals ("多租户强隔离...本次仅预留
// TenantId 维度缝") and §3.3 ("Tenancy（租户）| TenantId 值对象 -> common；
// Organization 管理 -> 产品层"): this project does not implement real
// multi-tenant isolation (schema-per-tenant/RLS/sharding) in this
// refactor -- Organization management (the actual product feature) lives
// in apps/server (a later M5 task), NOT here. TenantId only exists so
// Domain code that will eventually carry a tenant dimension (e.g. a future
// per-tenant Client/Scope scoping) has a stable, typed seam to grow into
// without a breaking signature change later.
//
// Unlike the other value objects in this directory, TenantId explicitly
// ALLOWS an empty/default value: today nothing in the Domain actually
// enforces multi-tenancy, so most callers are expected to construct
// TenantId::none() (or default-construct) rather than being forced to
// invent a tenant identifier that doesn't yet mean anything operationally.

#include <string>
#include <utility>

namespace authforge::common::model
{

/**
 * @brief Reserved multi-tenancy dimension seam (see file header for scope).
 * An empty value() means "no tenant" / single-tenant mode, which is the
 * only mode this refactor actually implements.
 */
class TenantId
{
  public:
    /// Default-constructs to the "no tenant" value (equivalent to none()).
    TenantId() = default;

    /// Construct from a raw tenant identifier string. An empty string is
    /// permitted and means "no tenant" (see class doc comment).
    explicit TenantId(std::string value) : value_(std::move(value))
    {
    }

    /// The canonical "no tenant" / single-tenant-mode value.
    static TenantId none()
    {
        return TenantId();
    }

    const std::string &value() const noexcept
    {
        return value_;
    }

    /// True iff this is the "no tenant" value (empty string).
    bool isNone() const noexcept
    {
        return value_.empty();
    }

    bool operator==(const TenantId &other) const noexcept
    {
        return value_ == other.value_;
    }

    bool operator!=(const TenantId &other) const noexcept
    {
        return !(*this == other);
    }

  private:
    std::string value_;
};

}  // namespace authforge::common::model
