#pragma once

// Task 9 (design.md §7 / REPOSITORY_MAPPING.md): split of PostgresOAuth2Storage
// into per-aggregate implementation files. This one implements
// ITokenRepository (REPOSITORY_MAPPING.md #7-14, #29-31, plus the token
// slice of #32 deleteExpiredData -> purgeExpired, plus the
// supportsTransactions()/supportsCas() capability flags). It is ADDITIVE:
// PostgresOAuth2Storage / IOAuth2Storage are untouched and remain the
// production path used by OAuth2Plugin.cc today.
#include <oauth2/storage/ITokenRepository.h>
#include <oauth2/storage/PostgresRepositoryBase.h>

#include <memory>

namespace oauth2
{

/**
 * @brief PostgreSQL implementation of ITokenRepository.
 *
 * saveTokenPair() is overridden (not the ITokenRepository default) to use a
 * real DB transaction (drogon::orm::DbClient::newTransaction()), exactly as
 * PostgresOAuth2Storage::saveTokenPair did -- this is the "Postgres override
 * uses a database transaction for real atomicity" case the base interface's
 * doc comment describes.
 *
 * Capability flags (verified against this file's actual implementation, not
 * assumed):
 *  - supportsTransactions() -> true: saveTokenPair() wraps both inserts in a
 *    single drogon::orm::DbClient transaction (newTransaction() +
 *    execSqlAsync chained on the same transPtr), so either both writes
 *    succeed or neither does at the SQL layer.
 *  - supportsCas() -> true: atomicRevokeRefreshToken() and consumeAuthCode's
 *    sibling in PostgresGrantRepository both use a single
 *    "UPDATE ... WHERE <not-yet-revoked/used> ... RETURNING ..." statement,
 *    which is a real compare-and-swap at the database level (the WHERE
 *    clause guards against a second concurrent UPDATE seeing the row as
 *    still eligible).
 */
class PostgresTokenRepository : public ITokenRepository,
                                public PostgresRepositoryBase,
                                public std::enable_shared_from_this<PostgresTokenRepository>
{
  public:
    PostgresTokenRepository() = default;

    void saveAccessToken(const OAuth2AccessToken &token, VoidCallback &&cb) override;
    void getAccessToken(const std::string &token, AccessTokenCallback &&cb) override;

    void saveTokenPair(
      const OAuth2AccessToken &at,
      const OAuth2RefreshToken &rt,
      VoidCallback &&cb
    ) override;

    void saveRefreshToken(const OAuth2RefreshToken &token, VoidCallback &&cb) override;
    void getRefreshToken(const std::string &token, RefreshTokenCallback &&cb) override;
    void revokeRefreshToken(const std::string &token, VoidCallback &&cb) override;
    void atomicRevokeRefreshToken(const std::string &token, RefreshTokenCallback &&cb) override;
    void revokeTokenFamily(const std::string &familyId, VoidCallback &&cb) override;

    void introspectToken(const std::string &token, TokenIntrospectionCallback &&cb) override;
    void incrementIntrospectCount(const std::string &token, VoidCallback &&cb) override;
    void revokeAccessToken(
      const std::string &token,
      const std::string &revokedBy,
      VoidCallback &&cb
    ) override;

    void purgeExpired() override;

    bool supportsTransactions() const override
    {
        return true;
    }

    bool supportsCas() const override
    {
        return true;
    }
};

}  // namespace oauth2
