#pragma once

// Task 17 slice 3 (fulla-sdk-refactor, design.md §6/§7): ports the M1
// repository interface oauth2::IGrantRepository (OAuth2Plugin/include/
// oauth2/storage/IGrantRepository.h) into fulla::oauth2::repository.
// See IClientRepository.h's header comment for the general
// decoupling-from-OAuth2Plugin rationale.
//
// Unlike the original (which reused oauth2::IOAuth2Storage::
// AuthorizationTransaction via #include to avoid a duplicate definition),
// this port can depend directly on fulla::oauth2::model::
// AuthorizationTransaction (Task 17 slice 2) as a free-standing type --
// the original header's own comment anticipated exactly this: "当 Task 17
// (M2b) 移动 Domain DTOs 出 IOAuth2Storage.h 到 oauth2::model...这个
// #include 应该换成新的 model 头，AuthorizationTransaction 变成
// free-standing 类型而不是嵌套类型".

#include <fulla/oauth2/model/Dto.h>

#include <functional>
#include <optional>
#include <string>

namespace fulla::oauth2::repository
{

/**
 * @brief Repository for authorization codes and authorization transactions
 * (the "grant" aggregate, i.e. everything that exists between an
 * authorization request and a token exchange).
 *
 * SDK consumers MUST implement this interface (design.md §7.1).
 */
class IGrantRepository
{
  public:
    virtual ~IGrantRepository() = default;

    using AuthCodeCallback =
      std::function<void(std::optional<fulla::oauth2::model::OAuth2AuthCode>)>;
    using VoidCallback = std::function<void()>;
    using BoolCallback = std::function<void(bool)>;
    using AuthorizationTransaction = fulla::oauth2::model::AuthorizationTransaction;
    using TransactionCallback = std::function<void(std::optional<AuthorizationTransaction>)>;

    // ========== Authorization Code Operations ==========

    /// Save a new authorization code. Original: IOAuth2Storage::saveAuthCode.
    virtual void saveAuthCode(
      const fulla::oauth2::model::OAuth2AuthCode &code,
      VoidCallback &&cb
    ) = 0;

    /// Get authorization code by code value. Original: IOAuth2Storage::getAuthCode.
    virtual void getAuthCode(const std::string &code, AuthCodeCallback &&cb) = 0;

    /// Mark an authorization code as used (single-use enforcement).
    /// Original: IOAuth2Storage::markAuthCodeUsed.
    virtual void markAuthCodeUsed(const std::string &code, VoidCallback &&cb) = 0;

    /**
     * @brief Atomic Consume: Get Code, Check if Used, Mark Used, Return Code.
     * If code not found OR already used, callback with std::nullopt.
     *
     * CRITICAL (preserved, design.md §7.2): per OAuth2 RFC 6749 §4.1.3,
     * MUST validate redirect_uri matches the value used in the
     * authorization request. Returns nullopt on mismatch.
     *
     * Original: IOAuth2Storage::consumeAuthCode.
     */
    virtual void consumeAuthCode(
      const std::string &code,
      const std::string &redirectUri,
      AuthCodeCallback &&cb
    ) = 0;

    // ========== Authorization Transaction Operations ==========

    /// Save authorization transaction to storage.
    /// Original: IOAuth2Storage::saveAuthorizationTransaction.
    virtual void saveAuthorizationTransaction(
      const AuthorizationTransaction &transaction,
      BoolCallback &&cb
    ) = 0;

    /// Get authorization transaction by ID.
    /// Original: IOAuth2Storage::getAuthorizationTransaction.
    virtual void getAuthorizationTransaction(
      const std::string &transactionId,
      TransactionCallback &&cb
    ) = 0;

    /// Delete authorization transaction.
    /// Original: IOAuth2Storage::deleteAuthorizationTransaction.
    virtual void deleteAuthorizationTransaction(
      const std::string &transactionId,
      VoidCallback &&cb
    ) = 0;

    /// Mark authorization transaction as consumed (prevent duplicate
    /// submissions). Original: IOAuth2Storage::markTransactionConsumed.
    virtual void markTransactionConsumed(const std::string &transactionId, BoolCallback &&cb) = 0;

    // ========== Cleanup ==========

    /**
     * @brief Purge expired auth codes and authorization transactions.
     * Required pure-virtual method (grants have expiry semantics).
     * Intended to be invoked by a future product-level CleanupService.
     */
    virtual void purgeExpired() = 0;
};

}  // namespace fulla::oauth2::repository
