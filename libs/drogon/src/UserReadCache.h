#pragma once
// libs/drogon/src/UserReadCache.h — src-internal (deliberately NOT under
// include/authforge: public-SDK surface would drift api-diff).
//
// Wave-2 P1 (docs/performance-optimization/optimization-wave-2-plan.md):
// Redis cache-aside for the two S6 userinfo read funnels —
//   OAuth2Plugin::getUserRoles  (roles, JSON array of role names)
//   OAuth2Plugin::getUserInfo   (profile JSON {id,username,email,email_verified})
// eliminating the 3 serial PG round-trips per userinfo request (~54% of its
// end-to-end latency per the instrumented hotspot report).
//
// Semantics (aligned with the RedisCached* decorators):
//   - miss → run the caller's fetch (the uncached path), fire-and-forget fill;
//   - soft-fail: any Redis error → fetch (never a wrong answer);
//   - negative cache: a profile miss (user absent) is cached 60s under a
//     "__user_absent__" marker (documented deviation from the client-repo
//     "don't cache nullopt" rule — public_sub is minted at registration and
//     userinfo is a read path, so the shadow window is harmless);
//   - roles may legitimately be an empty array — cached as "[]";
//   - key = the reader's subject string AS GIVEN (numeric id or public_sub
//     form). Write-path invalidation passes every form it knows (see
//     UserCacheInvalidator); a form that no write path can reach is bounded
//     by the TTL (120s roles / 300s profile).
//
// Singletons (one OAuth2Plugin per process): UserReadCache::instance() is
// configured by the plugin's cache branch; before configuration (or when the
// cache is disabled / Redis unavailable) it is disabled and every read goes
// straight to the fetch path.

#include <drogon/drogon.h>
#include <drogon/nosql/RedisClient.h>
#include <json/json.h>

#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace authforge::drogon
{

namespace userreadcache
{
inline constexpr int kDefaultProfileTtlSeconds = 300;
inline constexpr int kDefaultRolesTtlSeconds = 120;
inline constexpr int kNegativeTtlSeconds = 60;
inline const std::string kAbsentMarker = "{\"__user_absent__\":true}";

inline std::string profileKey(const std::string &subject)
{
    return "authforge:cache:user:profile:" + subject;
}
inline std::string rolesKey(const std::string &subject)
{
    return "authforge:cache:user:roles:" + subject;
}
}  // namespace userreadcache

// ---------------------------------------------------------------------------
// Write-path invalidation registry (same pattern as ClientCacheInvalidator).
// The plugin registers a DEL hook when the cache is active; user/role write
// paths call invalidateUser(subject[, publicSub]) after a successful write.
// ---------------------------------------------------------------------------
class UserCacheInvalidator
{
  public:
    using Hook = std::function<void(const std::string &subject)>;

    static UserCacheInvalidator &instance()
    {
        static UserCacheInvalidator inst;
        return inst;
    }

    void registerHook(Hook hook)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hook_ = std::make_shared<Hook>(std::move(hook));
    }

    // DEL both cache kinds for every known subject form of the user.
    void invalidateUser(const std::string &subject, const std::string &publicSub = "")
    {
        std::shared_ptr<Hook> hook;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            hook = hook_;
        }
        if (hook && *hook)
        {
            if (!subject.empty())
                (*hook)(subject);
            if (!publicSub.empty() && publicSub != subject)
                (*hook)(publicSub);
        }
    }

  private:
    UserCacheInvalidator() = default;
    std::mutex mutex_;
    std::shared_ptr<Hook> hook_;
};

// ---------------------------------------------------------------------------
// The cache-aside read cache itself.
// ---------------------------------------------------------------------------
class UserReadCache
{
  public:
    using RolesCallback = std::function<void(std::vector<std::string>)>;
    using ProfileCallback = std::function<void(std::optional<Json::Value>)>;

    static UserReadCache &instance()
    {
        static UserReadCache inst;
        return inst;
    }

    void configure(
      ::drogon::nosql::RedisClientPtr redisClient,
      int profileTtlSeconds = userreadcache::kDefaultProfileTtlSeconds,
      int rolesTtlSeconds = userreadcache::kDefaultRolesTtlSeconds
    )
    {
        redisClient_ = std::move(redisClient);
        profileTtl_ = profileTtlSeconds > 0 ? profileTtlSeconds : userreadcache::kDefaultProfileTtlSeconds;
        rolesTtl_ = rolesTtlSeconds > 0 ? rolesTtlSeconds : userreadcache::kDefaultRolesTtlSeconds;
    }

    bool enabled() const
    {
        return redisClient_ != nullptr;
    }

    using RolesFetch = std::function<void(const std::string &, RolesCallback)>;
    using ProfileFetch = std::function<void(const std::string &, ProfileCallback)>;

    // Roles: hit → parsed JSON array; miss/error → fetch (result filled back).
    void getRoles(const std::string &subject, RolesCallback &&cb, RolesFetch fetch)
    {
        if (!enabled())
        {
            fetch(subject, std::move(cb));
            return;
        }
        auto sharedCb = std::make_shared<RolesCallback>(std::move(cb));
        auto sharedFetch = std::make_shared<RolesFetch>(std::move(fetch));
        auto redis = redisClient_;
        std::string key = userreadcache::rolesKey(subject);
        redis->execCommandAsync(
          [this, sharedCb, subject, sharedFetch](const ::drogon::nosql::RedisResult &r) {
              if (r.type() == ::drogon::nosql::RedisResultType::kString)
              {
                  Json::Value root;
                  Json::CharReaderBuilder rb;
                  std::string errs;
                  std::istringstream is(r.asString());
                  if (Json::parseFromStream(rb, is, &root, &errs) && root.isArray())
                  {
                      std::vector<std::string> roles;
                      for (const auto &v : root)
                          roles.push_back(v.asString());
                      (*sharedCb)(std::move(roles));
                      return;
                  }
                  // Corrupt entry → fall through to fetch.
              }
              fetchAndFillRoles(subject, std::move(*sharedCb), *sharedFetch);
          },
          [this, sharedCb, subject, sharedFetch](const ::drogon::nosql::RedisException &) {
              // Soft-fail: Redis unhealthy → uncached path.
              fetchAndFillRoles(subject, std::move(*sharedCb), *sharedFetch);
          },
          "GET %s",
          key.c_str()
        );
    }

    // Profile: hit → parsed JSON object (absent marker → nullopt);
    // miss/error → fetch (result filled back; nullopt filled negatively).
    void getProfile(const std::string &subject, ProfileCallback &&cb, ProfileFetch fetch)
    {
        if (!enabled())
        {
            fetch(subject, std::move(cb));
            return;
        }
        auto sharedCb = std::make_shared<ProfileCallback>(std::move(cb));
        auto sharedFetch = std::make_shared<ProfileFetch>(std::move(fetch));
        auto redis = redisClient_;
        std::string key = userreadcache::profileKey(subject);
        redis->execCommandAsync(
          [this, sharedCb, subject, sharedFetch](const ::drogon::nosql::RedisResult &r) {
              if (r.type() == ::drogon::nosql::RedisResultType::kString)
              {
                  if (r.asString() == userreadcache::kAbsentMarker)
                  {
                      (*sharedCb)(std::nullopt);
                      return;
                  }
                  Json::Value root;
                  Json::CharReaderBuilder rb;
                  std::string errs;
                  std::istringstream is(r.asString());
                  if (Json::parseFromStream(rb, is, &root, &errs) && root.isObject())
                  {
                      (*sharedCb)(std::move(root));
                      return;
                  }
                  // Corrupt entry → fall through to fetch.
              }
              fetchAndFillProfile(subject, std::move(*sharedCb), *sharedFetch);
          },
          [this, sharedCb, subject, sharedFetch](const ::drogon::nosql::RedisException &) {
              fetchAndFillProfile(subject, std::move(*sharedCb), *sharedFetch);
          },
          "GET %s",
          key.c_str()
        );
    }

  private:
    UserReadCache() = default;

    void fetchAndFillRoles(const std::string &subject, RolesCallback cb, const RolesFetch &fetch)
    {
        fetch(
          subject,
          [this, subject, cb = std::make_shared<RolesCallback>(std::move(cb))](
            std::vector<std::string> roles) mutable {
              fillRoles(subject, roles);
              (*cb)(std::move(roles));
          }
        );
    }

    void fetchAndFillProfile(
      const std::string &subject,
      ProfileCallback cb,
      const ProfileFetch &fetch
    )
    {
        fetch(
          subject,
          [this, subject, cb = std::make_shared<ProfileCallback>(std::move(cb))](
            std::optional<Json::Value> profile) mutable {
              fillProfile(subject, profile);
              (*cb)(std::move(profile));
          }
        );
    }

    void fillRoles(const std::string &subject, const std::vector<std::string> &roles)
    {
        if (!enabled())
            return;
        Json::Value arr(Json::arrayValue);
        for (const auto &r : roles)
            arr.append(r);
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        postFill(userreadcache::rolesKey(subject), Json::writeString(wb, arr), rolesTtl_);
    }

    void fillProfile(const std::string &subject, const std::optional<Json::Value> &profile)
    {
        if (!enabled())
            return;
        if (!profile)
        {
            // Negative cache (user absent).
            postFill(userreadcache::profileKey(subject), userreadcache::kAbsentMarker,
                     userreadcache::kNegativeTtlSeconds);
            return;
        }
        Json::StreamWriterBuilder wb;
        wb["indentation"] = "";
        postFill(userreadcache::profileKey(subject), Json::writeString(wb, *profile), profileTtl_);
    }

    void postFill(const std::string &key, const std::string &payload, int ttl)
    {
        auto redis = redisClient_;
        if (!redis)
            return;
        std::string k = key;
        redis->execCommandAsync(
          [](const ::drogon::nosql::RedisResult &) {},
          [k](const ::drogon::nosql::RedisException &e) {
              LOG_DEBUG << "UserReadCache: fill SET failed for " << k << ": " << e.what();
          },
          "SET %s %s EX %d",
          k.c_str(),
          payload.c_str(),
          ttl
        );
    }

    ::drogon::nosql::RedisClientPtr redisClient_;
    int profileTtl_ = userreadcache::kDefaultProfileTtlSeconds;
    int rolesTtl_ = userreadcache::kDefaultRolesTtlSeconds;
};

}  // namespace authforge::drogon
