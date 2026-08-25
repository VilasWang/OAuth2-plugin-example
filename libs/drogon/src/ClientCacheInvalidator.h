#pragma once
// libs/drogon/src/ClientCacheInvalidator.h — src-internal (deliberately NOT
// under include/fulla: adding public SDK surface would drift api-diff).
//
// Wave-2 P0 (docs/performance-optimization/optimization-wave-2-plan.md):
// validateClient now verifies secrets against the Redis-cached client row,
// so client writes must invalidate fulla:cache:client:<id> or a rotated
// secret / deleted client / changed scopes stays trusted for up to the
// cache TTL (300s). The write paths (ClientManagementService update/delete/
// updateScopes) bypass the repository interface (direct Mapper calls), so
// the invalidation travels through this process-wide registry instead:
// OAuth2Plugin registers the DEL hook when the cached decorator is active;
// the admin write paths call invalidate() after a successful write. When the
// cache is disabled nothing is registered and invalidate() is a no-op.
//
// Registration happens once during plugin init (before serving); invalidate()
// may be called from any IO thread afterwards. The hook itself is
// fire-and-forget best-effort, mirroring the decorator's fill semantics —
// a lost DEL is bounded by the cache TTL.

#include <drogon/drogon.h>

#include <memory>
#include <mutex>
#include <string>

namespace fulla::drogon
{

class ClientCacheInvalidator
{
  public:
    using Hook = std::function<void(const std::string &clientId)>;

    static ClientCacheInvalidator &instance()
    {
        static ClientCacheInvalidator inst;
        return inst;
    }

    void registerHook(Hook hook)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hook_ = std::make_shared<Hook>(std::move(hook));
    }

    void invalidate(const std::string &clientId)
    {
        std::shared_ptr<Hook> hook;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hook = hook_;
        }
        if (hook && *hook)
            (*hook)(clientId);
    }

  private:
    ClientCacheInvalidator() = default;
    std::mutex mutex_;
    std::shared_ptr<Hook> hook_;
};

}  // namespace fulla::drogon
