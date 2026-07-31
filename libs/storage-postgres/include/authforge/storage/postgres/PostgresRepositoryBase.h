#pragma once

#include <drogon/orm/DbClient.h>
#include <json/json.h>
#include <string>

namespace authforge::storage::postgres
{

/**
 * @brief Small shared mixin for the Postgres repository split (Task 9,
 * design.md §7 / REPOSITORY_MAPPING.md).
 *
 * Rationale (deliberately minimal, not a "framework"):
 * The pre-split `PostgresOAuth2Storage` had a single `initFromConfig()` that
 * looked up `dbClientMaster_`/`dbClientReader_` by name from config. All
 * seven repositories carved out of it (`PostgresClientRepository`,
 * `PostgresGrantRepository`, `PostgresTokenRepository`,
 * `PostgresConsentRepository`, `PostgresUserRepository`,
 * `PostgresRoleRepository`, `PostgresSubjectMappingRepository`) need the
 * exact same two members and the exact same lookup logic. Rather than
 * copy/paste that logic seven times (drift risk) or invent a heavier
 * dependency-injection abstraction (no requirement in design.md or
 * REPOSITORY_MAPPING.md calls for one), this is a plain non-virtual mixin:
 * it only holds the two `DbClientPtr` members + the two name strings + the
 * `initFromConfig()` body. It intentionally does NOT:
 *  - inherit `std::enable_shared_from_this<>` (each concrete repository
 *    inherits that itself, parameterized on its own concrete type, because
 *    the safe `self = shared_from_this()` capture pattern is bound to the
 *    concrete class per the original design comment on
 *    `PostgresOAuth2Storage`/`RedisOAuth2Storage`/`CachedOAuth2Storage`);
 *  - inherit any repository interface (it is a data/behavior mixin, not an
 *    `IXxxRepository` implementation);
 *  - abstract over *how* a repository decides master vs. reader client for
 *    a given call -- each repository's methods keep the exact per-call
 *    lazy-reinit checks the original `PostgresOAuth2Storage` had (e.g.
 *    `getClient`/`validateClient`'s "if reader is null, try to re-fetch it"
 *    block), copied verbatim into each new .cc file. That per-call logic is
 *    small, method-specific (some methods don't lazily reinit at all in the
 *    original), and inlining it directly keeps each repository's .cc a
 *    faithful, easy-to-diff copy of the corresponding slice of the original
 *    `PostgresOAuth2Storage.cc`.
 *
 * A heavier shared base (e.g. one that also owns connection-retry policy or
 * templated CRUD helpers) was considered and rejected: design.md and
 * REPOSITORY_MAPPING.md do not ask for one, and introducing one here would
 * be scope creep beyond "split the god class into per-aggregate files."
 */
class PostgresRepositoryBase
{
  public:
    virtual ~PostgresRepositoryBase() = default;

    /**
     * @brief Initialize dbClientMaster_/dbClientReader_ from config.
     * Verbatim port of PostgresOAuth2Storage::initFromConfig.
     */
    void initFromConfig(const Json::Value &config);

  protected:
    // M3 pitfall (see PostgresIdentityRepository.h): inside namespace
    // authforge::storage::postgres, bare "drogon::" can resolve to
    // authforge::storage::postgres::drogon if such a member exists elsewhere
    // in the include graph (OAuth2Plugin's StorageSubjectResolver.h declares
    // namespace authforge::drogon::adapters). Globally qualify to be safe.
    ::drogon::orm::DbClientPtr dbClientMaster_;
    ::drogon::orm::DbClientPtr dbClientReader_;
    std::string dbClientName_ = "default";
    std::string dbClientReaderName_ = "default";
};

}  // namespace authforge::storage::postgres
