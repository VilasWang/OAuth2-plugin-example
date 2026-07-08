#pragma once

// M1 storage interface split (design.md §7). See IClientRepository.h header
// comment for the general rationale (additive, non-migrating, see
// REPOSITORY_MAPPING.md for the full mapping).
//
// AuthorizationTransaction (and its OAuth2AuthCode sibling DTO) currently
// live as members/nested types of IOAuth2Storage. Rather than copy/duplicate
// that struct here (which would create two competing definitions of the
// same shape and risk drift), this header #includes IOAuth2Storage.h and
// reuses oauth2::IOAuth2Storage::AuthorizationTransaction as-is. This is a
// deliberate, temporary decision:
//   - No circular dependency is introduced: IOAuth2Storage.h does not
//     (and must not) include this file.
//   - When Task 17 (M2b) moves the Domain DTOs out of IOAuth2Storage.h into
//     oauth2::model (per design.md "Data Models" / DTO section), this
//     #include should be swapped for the new model header and
//     AuthorizationTransaction becomes a free-standing type instead of a
//     nested one. That is out of scope for Task 7.
#include <oauth2/storage/IOAuth2Storage.h>

#include <functional>
#include <optional>
#include <string>

namespace oauth2
{

/**
 * @brief Repository for authorization codes and authorization transactions
 * (the "grant" aggregate, i.e. everything that exists between an
 * authorization request and a token exchange).
 *
 * Carves out auth-code CRUD + the consent-flow authorization transaction
 * group from the former god interface IOAuth2Storage. See
 * REPOSITORY_MAPPING.md for the full 30-method mapping.
 *
 * SDK consumers MUST implement this interface (design.md §7.1).
 */
class IGrantRepository
{
  public:
    virtual ~IGrantRepository() = default;

    using AuthCodeCallback = std::function<void(std::optional<OAuth2AuthCode>)>;
    using VoidCallback = std::function<void()>;
    using BoolCallback = std::function<void(bool)>;
    using AuthorizationTransaction = IOAuth2Storage::AuthorizationTransaction;
    using TransactionCallback = std::function<void(std::optional<AuthorizationTransaction>)>;

    // ========== Authorization Code Operations ==========

    /**
     * @brief Save a new authorization code.
     * Original: IOAuth2Storage::saveAuthCode
     */
    virtual void saveAuthCode(const OAuth2AuthCode &code, VoidCallback &&cb) = 0;

    /**
     * @brief Get authorization code by code value.
     * Original: IOAuth2Storage::getAuthCode
     */
    virtual void getAuthCode(const std::string &code, AuthCodeCallback &&cb) = 0;

    /**
     * @brief Mark an authorization code as used (single-use enforcement).
     * Original: IOAuth2Storage::markAuthCodeUsed
     */
    virtual void markAuthCodeUsed(const std::string &code, VoidCallback &&cb) = 0;

    /**
     * @brief Atomic Consume: Get Code, Check if Used, Mark Used, Return Code.
     * If code not found OR already used, callback with std::nullopt.
     *
     * CRITICAL (preserved from IOAuth2Storage, design.md §7.2): per OAuth2
     * RFC 6749 Section 4.1.3, MUST validate redirect_uri matches the value
     * used in the authorization request. Returns nullopt on mismatch.
     *
     * Original: IOAuth2Storage::consumeAuthCode
     *
     * @param code Authorization code
     * @param redirectUri Redirect URI from token request (must match
     * authorization)
     * @param cb Callback with auth code data or nullopt if
     * invalid/used/mismatch
     */
    virtual void consumeAuthCode(
      const std::string &code,
      const std::string &redirectUri,
      AuthCodeCallback &&cb
    ) = 0;

    // ========== Authorization Transaction Operations ==========

    /**
     * @brief Save authorization transaction to storage.
     * Original: IOAuth2Storage::saveAuthorizationTransaction
     */
    virtual void saveAuthorizationTransaction(
      const AuthorizationTransaction &transaction,
      BoolCallback &&cb
    ) = 0;

    /**
     * @brief Get authorization transaction by ID.
     * Original: IOAuth2Storage::getAuthorizationTransaction
     */
    virtual void getAuthorizationTransaction(
      const std::string &transactionId,
      TransactionCallback &&cb
    ) = 0;

    /**
     * @brief Delete authorization transaction.
     * Original: IOAuth2Storage::deleteAuthorizationTransaction
     */
    virtual void deleteAuthorizationTransaction(
      const std::string &transactionId,
      VoidCallback &&cb
    ) = 0;

    /**
     * @brief Mark authorization transaction as consumed (prevent duplicate
     * submissions).
     * Original: IOAuth2Storage::markTransactionConsumed
     */
    virtual void markTransactionConsumed(const std::string &transactionId, BoolCallback &&cb) = 0;

    // ========== Cleanup ==========

    /**
     * @brief Purge expired auth codes and authorization transactions.
     *
     * Decision (see REPOSITORY_MAPPING.md): grants DO have expiry semantics
     * (OAuth2AuthCode::expiresAt, AuthorizationTransaction::expiresAt), and
     * the original IOAuth2Storage::deleteExpiredData() purged auth codes in
     * all three existing implementations. This is therefore a required,
     * pure-virtual method (not optional/default) so no implementation
     * silently forgets to purge expired grants.
     *
     * Replaces the auth-code/transaction portion of the former
     * IOAuth2Storage::deleteExpiredData(). Intended to be invoked by a
     * future product-level CleanupService (not implemented in Task 7);
     * this task only defines the interface method.
     */
    virtual void purgeExpired() = 0;
};

}  // namespace oauth2
