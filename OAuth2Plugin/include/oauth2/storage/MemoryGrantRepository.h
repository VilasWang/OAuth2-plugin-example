#pragma once

// Task 10 (design.md §7 / REPOSITORY_MAPPING.md): split of
// MemoryOAuth2Storage into per-aggregate implementation files, mirroring the
// Task 9 Postgres split. This one implements IGrantRepository
// (REPOSITORY_MAPPING.md #3-6, #22-25, plus the grant slice of #32
// deleteExpiredData -> purgeExpired). It is ADDITIVE: MemoryOAuth2Storage /
// IOAuth2Storage are untouched and remain the production path used by
// OAuth2Plugin.cc and existing tests today.
//
// State ownership (see MemoryClientRepository.h header comment for the
// general rationale): this class owns `authCodes_` and `transactions_` --
// the two maps every IGrantRepository method touches -- and its own private
// mutex. No cross-repository sharing is needed.
#include <authforge/oauth2/repository/IGrantRepository.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace oauth2
{

// Task 27.5 (authforge-sdk-refactor): now implements the NEW Domain-layer
// interface authforge::oauth2::repository::IGrantRepository (+ the
// authforge::oauth2::model::* DTOs) instead of the legacy oauth2 one from
// IOAuth2Storage.h. Old/new DTOs are field-identical. Types are fully
// qualified (not aliased) because the legacy oauth2::OAuth2AuthCode /
// oauth2::AuthorizationTransaction still coexist in IOAuth2Storage.h during
// this transition (see MemoryClientRepository.h for the clash rationale).
using IGrantRepositoryBase = ::authforge::oauth2::repository::IGrantRepository;

/**
 * @brief In-memory implementation of IGrantRepository.
 *
 * Faithful port of MemoryOAuth2Storage's auth-code CRUD (including
 * consumeAuthCode's RFC 6749 §4.1.3 redirect_uri validation and lazy
 * expiry-on-read eviction in getAuthCode) and authorization-transaction CRUD
 * (including getAuthorizationTransaction's lazy expiry-on-read eviction and
 * markTransactionConsumed's single-consume guard).
 */
class MemoryGrantRepository : public IGrantRepositoryBase
{
  public:
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

    /**
     * @brief Purge expired auth codes and authorization transactions.
     *
     * Grant-side slice of the original
     * MemoryOAuth2Storage::deleteExpiredData(): only the authCodes_ sweep.
     * (Transactions are evicted lazily on read in
     * getAuthorizationTransaction, exactly as the original
     * MemoryOAuth2Storage did -- deleteExpiredData() never proactively swept
     * transactions_ either, so this override does not add that sweep; see
     * .cc for the full rationale.)
     */
    void purgeExpired() override;

  private:
    std::recursive_mutex mutex_;
    std::unordered_map<std::string, ::authforge::oauth2::model::OAuth2AuthCode> authCodes_;
    std::unordered_map<std::string, ::authforge::oauth2::model::AuthorizationTransaction>
      transactions_;

    int64_t getCurrentTimestamp() const;
};

}  // namespace oauth2
