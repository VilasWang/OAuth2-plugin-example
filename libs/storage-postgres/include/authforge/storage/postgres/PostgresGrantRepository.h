#pragma once

// Task 9 (design.md §7 / REPOSITORY_MAPPING.md): split of PostgresOAuth2Storage
// into per-aggregate implementation files. This one implements
// IGrantRepository (REPOSITORY_MAPPING.md #3-6, #22-25, plus the grant slice
// of #32 deleteExpiredData -> purgeExpired). It is ADDITIVE:
// PostgresOAuth2Storage / IOAuth2Storage are untouched and remain the
// production path used by OAuth2Plugin.cc today.
#include <authforge/oauth2/repository/IGrantRepository.h>
#include <authforge/storage/postgres/PostgresRepositoryBase.h>

#include <memory>

namespace authforge::storage::postgres
{

// Task 27.5: now implements the NEW Domain-layer interface
// authforge::oauth2::repository::IGrantRepository (+ authforge::oauth2::model::* DTOs) instead of
// the legacy oauth2 one. Types are qualified below (not aliased into this namespace) because the
// legacy oauth2::* DTOs still coexist in IOAuth2Storage.h during this transition.
using IGrantRepositoryBase = ::authforge::oauth2::repository::IGrantRepository;

/**
 * @brief PostgreSQL implementation of IGrantRepository.
 *
 * Auth-code operations (saveAuthCode/getAuthCode/markAuthCodeUsed/
 * consumeAuthCode) are a faithful port of PostgresOAuth2Storage's
 * implementation, including consumeAuthCode's atomic
 * "UPDATE ... WHERE used = false RETURNING ..." CAS query and its RFC 6749
 * §4.1.3 redirect_uri validation.
 *
 * Authorization-transaction operations
 * (saveAuthorizationTransaction/getAuthorizationTransaction/
 * deleteAuthorizationTransaction/markTransactionConsumed) are ALSO a
 * faithful port -- including the fact that the original
 * PostgresOAuth2Storage implementation is a documented placeholder (see
 * REPOSITORY_MAPPING.md "衔接提示" / the LOG_DEBUG comments in each method
 * body): it does not actually persist to a real
 * oauth2_authorization_transactions table. This split task intentionally
 * preserves that placeholder behavior verbatim rather than completing it
 * (that is explicitly out of scope per REPOSITORY_MAPPING.md).
 */
class PostgresGrantRepository : public IGrantRepositoryBase,
                                public PostgresRepositoryBase,
                                public std::enable_shared_from_this<PostgresGrantRepository>
{
  public:
    PostgresGrantRepository() = default;

    void saveAuthCode(
      const ::authforge::oauth2::model::OAuth2AuthCode &code,
      VoidCallback &&cb
    ) override;
    void getAuthCode(const std::string &code, AuthCodeCallback &&cb) override;
    void markAuthCodeUsed(const std::string &code, VoidCallback &&cb) override;
    void consumeAuthCode(
      const std::string &code,
      const std::string &redirectUri,
      AuthCodeCallback &&cb
    ) override;

    void saveAuthorizationTransaction(
      const ::authforge::oauth2::model::AuthorizationTransaction &transaction,
      BoolCallback &&cb
    ) override;
    void getAuthorizationTransaction(
      const std::string &transactionId,
      TransactionCallback &&cb
    ) override;
    void deleteAuthorizationTransaction(
      const std::string &transactionId,
      VoidCallback &&cb
    ) override;
    void markTransactionConsumed(const std::string &transactionId, BoolCallback &&cb) override;

    void purgeExpired() override;
};

}  // namespace authforge::storage::postgres
