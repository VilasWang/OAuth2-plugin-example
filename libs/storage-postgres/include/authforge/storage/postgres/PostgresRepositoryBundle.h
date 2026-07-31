#pragma once

// Task 9 (design.md §7): aggregates the four Postgres oauth2 repository
// implementations behind a single construction/initialization entry point
// for product assembly code. This is ADDITIVE -- it does not replace
// PostgresOAuth2Storage/IOAuth2Storage, which remain the production path
// wired up by OAuth2Plugin.cc today.
// Phase 1.5e: the 3 identity repos that used to live here are removed; the
// identity domain now has its own authforge::identity::* backing stores
// (see OAuth2Plugin.cc initStorage).
#include <authforge/storage/postgres/PostgresClientRepository.h>
#include <authforge/storage/postgres/PostgresGrantRepository.h>
#include <authforge/storage/postgres/PostgresTokenRepository.h>
#include <authforge/storage/postgres/PostgresConsentRepository.h>

#include <json/json.h>
#include <memory>

namespace authforge::storage::postgres
{

// Task 27.5: the 4 oauth2-aggregate accessors now expose the NEW authforge::oauth2::repository::*
// interfaces the split-repos implement.
using IClientRepository = ::authforge::oauth2::repository::IClientRepository;
using IGrantRepository = ::authforge::oauth2::repository::IGrantRepository;
using ITokenRepository = ::authforge::oauth2::repository::ITokenRepository;
using IConsentRepository = ::authforge::oauth2::repository::IConsentRepository;

/**
 * @brief Aggregates all four Postgres oauth2 repository implementations
 * behind a single initFromConfig() call, mirroring the ergonomics of the
 * original PostgresOAuth2Storage::initFromConfig() (one config block, in
 * this case shared verbatim across all four since they all read the same
 * db_client_name/db_client_reader keys -- see PostgresRepositoryBase).
 *
 * Usage (future product assembly code, not part of Task 9's scope to wire
 * up into OAuth2Plugin.cc -- that remains on IOAuth2Storage per the task's
 * "additive, not a replacement" constraint):
 *
 *   PostgresRepositoryBundle bundle;
 *   bundle.initFromConfig(config["postgres"]);
 *   someService(bundle.clientRepository(), bundle.tokenRepository(), ...);
 *
 * Design choice -- shared_ptr getters returning the interface type (not the
 * concrete Postgres type): consumers of a repository bundle should depend
 * on IClientRepository/IGrantRepository/etc., not on
 * PostgresClientRepository/etc., so that swapping in a different backend
 * (Redis/Memory bundle, once Task 10 exists) only requires constructing a
 * different bundle type, not changing every call site's type. Each getter
 * still hands back the concrete shared_ptr (which implicitly converts to
 * shared_ptr<Interface>), so callers that need shared_from_this-style
 * lifetime sharing between components still work.
 */
class PostgresRepositoryBundle
{
  public:
    PostgresRepositoryBundle();

    /**
     * @brief Initialize all four oauth2 repositories' DB clients from a
     * single config block. Delegates to each repository's own
     * PostgresRepositoryBase::initFromConfig() (verbatim port of
     * PostgresOAuth2Storage::initFromConfig, see that header for the
     * db_client_name/db_client_reader lookup semantics).
     */
    void initFromConfig(const Json::Value &config);

    std::shared_ptr<IClientRepository> clientRepository() const
    {
        return clientRepository_;
    }

    std::shared_ptr<IGrantRepository> grantRepository() const
    {
        return grantRepository_;
    }

    std::shared_ptr<ITokenRepository> tokenRepository() const
    {
        return tokenRepository_;
    }

    std::shared_ptr<IConsentRepository> consentRepository() const
    {
        return consentRepository_;
    }

  private:
    std::shared_ptr<PostgresClientRepository> clientRepository_;
    std::shared_ptr<PostgresGrantRepository> grantRepository_;
    std::shared_ptr<PostgresTokenRepository> tokenRepository_;
    std::shared_ptr<PostgresConsentRepository> consentRepository_;
};

}  // namespace authforge::storage::postgres
