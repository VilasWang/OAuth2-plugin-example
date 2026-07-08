#include <oauth2/storage/MemoryGrantRepository.h>
#include <drogon/drogon.h>
#include <chrono>

namespace oauth2
{

int64_t MemoryGrantRepository::getCurrentTimestamp() const
{
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

void MemoryGrantRepository::saveAuthCode(const OAuth2AuthCode &code, VoidCallback &&cb)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    authCodes_[code.code] = code;
    if (cb)
        cb();
}

void MemoryGrantRepository::getAuthCode(const std::string &code, AuthCodeCallback &&cb)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Clean up expired codes lazily or just check expiry
    auto it = authCodes_.find(code);
    if (it != authCodes_.end())
    {
        if (it->second.expiresAt > getCurrentTimestamp())
        {
            cb(it->second);
            return;
        }
        else
        {
            authCodes_.erase(it);
        }
    }
    cb(std::nullopt);
}

void MemoryGrantRepository::markAuthCodeUsed(const std::string &code, VoidCallback &&cb)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = authCodes_.find(code);
    if (it != authCodes_.end())
    {
        it->second.used = true;
    }
    if (cb)
        cb();
}

void MemoryGrantRepository::consumeAuthCode(
  const std::string &code,
  const std::string &redirectUri,
  AuthCodeCallback &&cb
)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = authCodes_.find(code);
    if (it != authCodes_.end())
    {
        if (!it->second.used)
        {
            // CRITICAL: Validate redirect_uri matches authorization
            // Per OAuth2 RFC 6749 Section 4.1.3
            if (!redirectUri.empty() && redirectUri != it->second.redirectUri)
            {
                LOG_WARN << "[SECURITY] redirect_uri mismatch in token exchange. "
                         << "Expected: " << it->second.redirectUri << ", Got: " << redirectUri
                         << ", Code: " << code;
                cb(std::nullopt);
                return;
            }

            it->second.used = true;
            cb(it->second);
            return;
        }
    }
    cb(std::nullopt);
}

// ========== Authorization Transaction Operations ==========

void MemoryGrantRepository::saveAuthorizationTransaction(
  const AuthorizationTransaction &transaction,
  BoolCallback &&cb
)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    transactions_[transaction.transactionId] = transaction;
    LOG_DEBUG << "Saved authorization transaction: " << transaction.transactionId;
    cb(true);
}

void MemoryGrantRepository::getAuthorizationTransaction(
  const std::string &transactionId,
  TransactionCallback &&cb
)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = transactions_.find(transactionId);
    if (it != transactions_.end())
    {
        // Check if expired
        int64_t now = getCurrentTimestamp();
        if (it->second.expiresAt < now)
        {
            LOG_DEBUG << "Transaction expired: " << transactionId;
            transactions_.erase(it);
            cb(std::nullopt);
            return;
        }
        cb(it->second);
    }
    else
    {
        cb(std::nullopt);
    }
}

void MemoryGrantRepository::deleteAuthorizationTransaction(
  const std::string &transactionId,
  VoidCallback &&cb
)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    transactions_.erase(transactionId);
    LOG_DEBUG << "Deleted authorization transaction: " << transactionId;
    cb();
}

void MemoryGrantRepository::markTransactionConsumed(
  const std::string &transactionId,
  BoolCallback &&cb
)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = transactions_.find(transactionId);
    if (it != transactions_.end() && !it->second.consumed)
    {
        it->second.consumed = true;
        LOG_DEBUG << "Marked transaction as consumed: " << transactionId;
        cb(true);
    }
    else
    {
        LOG_DEBUG << "Transaction already consumed or not found: " << transactionId;
        cb(false);
    }
}

// ========== Cleanup ==========

void MemoryGrantRepository::purgeExpired()
{
    // Grant-side slice of the original MemoryOAuth2Storage::deleteExpiredData():
    // only the authCodes_ sweep. transactions_ was never proactively swept by
    // the original deleteExpiredData() either (it only touched authCodes_ /
    // accessTokens_ / refreshTokens_) -- transaction expiry is handled lazily
    // on read in getAuthorizationTransaction, which this split preserves
    // unchanged. Faithfully porting "what deleteExpiredData() actually did"
    // means NOT inventing a proactive transactions_ sweep here.
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    int64_t now = getCurrentTimestamp();

    for (auto it = authCodes_.begin(); it != authCodes_.end();)
    {
        if (it->second.expiresAt < now)
        {
            it = authCodes_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

}  // namespace oauth2
