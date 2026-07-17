#include <oauth2/storage/MemoryConsentRepository.h>
#include <drogon/drogon.h>
#include <chrono>

namespace oauth2
{

// Task 27.5: callback aliases now live on the new base interface; bring
// them into scope for the out-of-class method definitions below. The
// UserRef alias is safe at namespace scope HERE (this .cc does not include
// oauth2/storage/UserRef.h, so no oauth2::UserRef clash).
using BoolCallback = IConsentRepositoryBase::BoolCallback;
using VoidCallback = IConsentRepositoryBase::VoidCallback;
using UserRef = ::authforge::oauth2::model::UserRef;

int64_t MemoryConsentRepository::getCurrentTimestamp() const
{
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

void MemoryConsentRepository::hasUserConsent(
  const UserRef &user,
  const std::string &clientId,
  const std::string &scope,
  BoolCallback &&cb
)
{
    // F4: unwrap the opaque UserRef to the internal key the map needs. See
    // UserRef.h -- this is the one place (storage-layer implementation)
    // permitted to do so.
    int32_t internalUserId = user.internalUserId;

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string key = std::to_string(internalUserId) + ":" + clientId + ":" + scope;
    auto it = userConsents_.find(key);
    cb(it != userConsents_.end());
}

void MemoryConsentRepository::saveUserConsent(
  const UserRef &user,
  const std::string &clientId,
  const std::string &scope,
  BoolCallback &&cb
)
{
    int32_t internalUserId = user.internalUserId;

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string key = std::to_string(internalUserId) + ":" + clientId + ":" + scope;
    userConsents_[key] = getCurrentTimestamp();
    LOG_DEBUG << "Saved user consent: " << key;
    cb(true);
}

void MemoryConsentRepository::revokeUserConsent(
  const UserRef &user,
  const std::string &clientId,
  const std::string &scope,
  VoidCallback &&cb
)
{
    int32_t internalUserId = user.internalUserId;

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string key = std::to_string(internalUserId) + ":" + clientId + ":" + scope;
    size_t erased = userConsents_.erase(key);
    LOG_DEBUG << "Revoked user consent: " << key << " (erased: " << erased << ")";
    cb();
}

}  // namespace oauth2
