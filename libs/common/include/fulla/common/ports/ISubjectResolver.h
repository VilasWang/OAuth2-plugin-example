#pragma once

// Task 13 (fulla-sdk-refactor, design.md §5.2/§5.3/§6): port interfaces
// for the shared Domain kernel.
//
// ISubjectResolver is the port oauth2 uses to resolve an opaque
// fulla::common::model::Subject (e.g. "local:alice") into whatever
// internal identifier identity's repositories need, WITHOUT oauth2 ever
// compiling a dependency on libs/identity (design.md §5.2's "端口解耦：
// 产品层装配注入" decision, 方案 A: "端口接口下沉到 common")。identity's
// concrete implementation of this port (backed by
// ISubjectMappingRepository, per the M1 split) is injected by the product
// layer (apps/server, a later M3 task) into oauth2's service constructors
// -- this file only declares the port shape both sides agree on.
//
// Async, not sync (design consistency, not in the original design.md text
// but required by it): every existing repository this port will eventually
// be backed by (ISubjectMappingRepository::getInternalUserId, etc) is
// asynchronous/callback-based (design.md §7: "Implementations use
// ASYNCHRONOUS CALLBACKS") because Drogon repositories run on an event
// loop where blocking is not acceptable. A synchronous port signature here
// would force identity's future implementation to either block the event
// loop waiting on an async DB call, or fake synchronicity in a way that
// defeats the point -- so this port mirrors the existing callback
// convention instead of introducing a new, incompatible synchronous shape
// at the one seam meant to connect to that async machinery.
//
// Boundary type constraint (design.md §5.2 评审 B7): every parameter/return
// type crossing this port MUST be a common:: type (value object /
// primitive), never an oauth2 or identity private type -- otherwise the
// "oauth2 与 identity 互不编译依赖" invariant breaks at the type level even
// if no #include exists. See UserRef.h (oauth2-side today) for the
// existing placeholder this port is intended to eventually replace;
// oauth2::UserRef is NOT referenced here for exactly that reason.

#include <fulla/common/model/Subject.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace fulla::common::ports
{

/**
 * @brief Resolves an opaque Subject into identity's internal user
 * identifier.
 *
 * The internal identifier is deliberately typed as a plain int32_t rather
 * than a new common:: value object: it is a storage-layer primary key
 * identity's repositories already use verbatim (see
 * ISubjectMappingRepository::getInternalUserId's existing
 * `OptionalIntCallback` shape), and wrapping it in a "InternalUserId" value
 * object here would just be a same-shaped renaming with no new invariant
 * to enforce -- oauth2 is expected to treat the returned int32_t as opaque
 * regardless of its C++ type.
 */
class ISubjectResolver
{
  public:
    using ResolveCallback = std::function<void(std::optional<int32_t>)>;

    virtual ~ISubjectResolver() = default;

    /**
     * @brief Resolve a Subject to identity's internal user id.
     * Invokes `cb` with the internal user id, or std::nullopt if the
     * subject does not resolve to a known user.
     */
    virtual void resolve(const model::Subject &subject, ResolveCallback &&cb) = 0;
};

}  // namespace fulla::common::ports
