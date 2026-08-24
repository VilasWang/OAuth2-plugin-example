#include <authforge/storage/redis/RedisCachedTokenRepository.h>
#include <authforge/storage/redis/DelayedDoubleDelete.h>

#include <drogon/drogon.h>
#include <json/json.h>

#include <sstream>
#include <string_view>

namespace authforge::storage::redis
{

// DTO + callback aliases (safe at namespace scope: this .cc does not include
// IOAuth2Storage.h, so no oauth2::* clash).
using OAuth2AccessToken = ::authforge::oauth2::model::OAuth2AccessToken;
using TokenIntrospection = ::authforge::oauth2::model::TokenIntrospection;
using AccessTokenCallback = RedisCachedTokenRepositoryBase::AccessTokenCallback;
using TokenIntrospectionCallback = RedisCachedTokenRepositoryBase::TokenIntrospectionCallback;
using VoidCallback = RedisCachedTokenRepositoryBase::VoidCallback;

using namespace ::drogon;
using namespace ::drogon::nosql;

namespace
{
// ---------------------------------------------------------------------------
// D1 (C6): complete (de)serializers for the two cached DTOs. Every field is
// round-tripped — a missing field on read would silently default-construct
// and break introspection/audit consumers. Uses the same Json::StreamWriter/
// CharReader idiom as RedisCachedClientRepository.cc.
// ---------------------------------------------------------------------------

std::string serializeAccessToken(const OAuth2AccessToken &t)
{
    Json::Value j;
    j["token"] = t.token;
    j["clientId"] = t.clientId;
    j["userId"] = t.userId;
    j["scope"] = t.scope;
    j["expiresAt"] = static_cast<Json::Int64>(t.expiresAt);
    j["revoked"] = t.revoked;
    j["issuedAt"] = static_cast<Json::Int64>(t.issuedAt);
    j["issuer"] = t.issuer;
    j["audience"] = t.audience;
    j["notBefore"] = static_cast<Json::Int64>(t.notBefore);
    j["introspectCount"] = t.introspectCount;
    j["revokedAt"] = static_cast<Json::Int64>(t.revokedAt);
    j["revokedBy"] = t.revokedBy;
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return Json::writeString(b, j);
}

bool deserializeAccessToken(const std::string &s, OAuth2AccessToken &out)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream is(s);
    if (!Json::parseFromStream(b, is, &root, &errs))
    {
        LOG_ERROR << "RedisCachedTokenRepository: access-token JSON parse error: " << errs;
        return false;
    }
    // token + expiresAt are the load-bearing fields; the rest default safely.
    if (!root.isMember("token") || !root.isMember("expiresAt"))
        return false;
    out.token = root["token"].asString();
    out.clientId = root.get("clientId", "").asString();
    out.userId = root.get("userId", "").asString();
    out.scope = root.get("scope", "").asString();
    out.expiresAt = root.get("expiresAt", Json::Int64(0)).asInt64();
    out.revoked = root.get("revoked", false).asBool();
    out.issuedAt = root.get("issuedAt", Json::Int64(0)).asInt64();
    out.issuer = root.get("issuer", "").asString();
    out.audience = root.get("audience", "").asString();
    out.notBefore = root.get("notBefore", Json::Int64(0)).asInt64();
    out.introspectCount = root.get("introspectCount", 0).asInt();
    out.revokedAt = root.get("revokedAt", Json::Int64(0)).asInt64();
    out.revokedBy = root.get("revokedBy", "").asString();
    return true;
}

std::string serializeIntrospection(const TokenIntrospection &t)
{
    Json::Value j;
    j["active"] = t.active;
    j["clientId"] = t.clientId;
    j["tokenType"] = t.tokenType;
    j["exp"] = static_cast<Json::Int64>(t.exp);
    j["iat"] = static_cast<Json::Int64>(t.iat);
    j["nbf"] = static_cast<Json::Int64>(t.nbf);
    j["sub"] = t.sub;
    j["aud"] = t.aud;
    j["iss"] = t.iss;
    j["scope"] = t.scope;
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return Json::writeString(b, j);
}

bool deserializeIntrospection(const std::string &s, TokenIntrospection &out)
{
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream is(s);
    if (!Json::parseFromStream(b, is, &root, &errs))
    {
        LOG_ERROR << "RedisCachedTokenRepository: introspection JSON parse error: " << errs;
        return false;
    }
    if (!root.isMember("active"))
        return false;
    out.active = root["active"].asBool();
    out.clientId = root.get("clientId", "").asString();
    out.tokenType = root.get("tokenType", "Bearer").asString();
    out.exp = root.get("exp", Json::Int64(0)).asInt64();
    out.iat = root.get("iat", Json::Int64(0)).asInt64();
    out.nbf = root.get("nbf", Json::Int64(0)).asInt64();
    out.sub = root.get("sub", "").asString();
    out.aud = root.get("aud", "").asString();
    out.iss = root.get("iss", "").asString();
    out.scope = root.get("scope", "").asString();
    return true;
}

const ::authforge::common::ports::MetricLabels kHitLabels{{"repo", "token"}, {"outcome", "hit"}};
const ::authforge::common::ports::MetricLabels kMissLabels{{"repo", "token"}, {"outcome", "miss"}};
const ::authforge::common::ports::MetricLabels kErrorLabels{{"repo", "token"}, {"outcome", "error"}};

// Wave-2 P2 (docs/performance-optimization/optimization-wave-2-plan.md):
// one EVAL round-trip replaces the serial EXISTS(revoked) → GET(value) pair
// (~450µs each per the instrumented report §2) on both token read paths.
// Reply shapes (Lua truncates a trailing nil):
//   {1, ''}          → revoked (negative-cache hit)
//   {0, <string>}    → not revoked, value present
//   {0}              → not revoked, no value (miss)
const char kRevokedThenGetScript[] =
  "local r = redis.call('EXISTS', KEYS[1]) "
  "if r > 0 then return {1, ''} end "
  "return {0, redis.call('GET', KEYS[2])}";

// Tri-state decoder for kRevokedThenGetScript replies.
enum class RevokedThenGet { kRevoked, kValue, kMiss };

RevokedThenGet decodeRevokedThenGet(const RedisResult &result, std::string &valueOut)
{
    if (result.type() != RedisResultType::kArray)
        return RevokedThenGet::kMiss;
    const auto arr = result.asArray();
    if (arr.empty() || arr[0].type() != RedisResultType::kInteger)
        return RevokedThenGet::kMiss;
    if (arr[0].asInteger() > 0)
        return RevokedThenGet::kRevoked;
    if (arr.size() > 1 && arr[1].type() == RedisResultType::kString)
    {
        valueOut = arr[1].asString();
        return RevokedThenGet::kValue;
    }
    return RevokedThenGet::kMiss;
}
}  // namespace

RedisCachedTokenRepository::RedisCachedTokenRepository(
  std::shared_ptr<RedisCachedTokenRepositoryBase> impl,
  RedisClientPtr redisClient,
  std::shared_ptr<::authforge::common::ports::IMetrics> metrics,
  int accessTokenMaxTtlSeconds
) : impl_(std::move(impl)),
    redisClient_(std::move(redisClient)),
    metrics_(std::move(metrics)),
    accessTokenMaxTtlSeconds_(accessTokenMaxTtlSeconds > 0 ? accessTokenMaxTtlSeconds : 60)
{
}

void RedisCachedTokenRepository::emitMetric(const char *outcome) const
{
    if (!metrics_)
        return;
    const auto &labels = std::string_view(outcome) == "hit"   ? kHitLabels
                         : std::string_view(outcome) == "miss" ? kMissLabels
                                                               : kErrorLabels;
    metrics_->incrementCounter("authforge_cache_total", labels);
}

// ---------------------------------------------------------------------------
// getAccessToken (C1: revoked check on every return; C7: TTL guard)
// ---------------------------------------------------------------------------
void RedisCachedTokenRepository::getAccessToken(const std::string &token, AccessTokenCallback &&cb)
{
    auto sharedCb = std::make_shared<AccessTokenCallback>(std::move(cb));
    auto fired = std::make_shared<std::atomic<bool>>(false);
    auto self = shared_from_this();
    std::string key = "authforge:cache:token:access:" + token;
    std::string revokedKey = "authforge:cache:token:revoked:" + token;

    if (!redisClient_)
    {
        emitMetric("miss");
        impl_->getAccessToken(token, std::move(*sharedCb));
        return;
    }

    // C1: check the negative cache FIRST (revoked → nullopt, never consult
    // the positive cache), then the positive cache — both in ONE EVAL
    // round-trip (Wave-2 P2; was two serial EXISTS+GET hops).
    redisClient_->execCommandAsync(
      [self, sharedCb, fired, token](const RedisResult &result) {
          std::string value;
          const auto state = decodeRevokedThenGet(result, value);
          if (state == RevokedThenGet::kRevoked)
          {
              self->emitMetric("hit");  // hit on the negative cache
              if (!fired->exchange(true))
                  (*sharedCb)(std::nullopt);
              return;
          }
          if (state == RevokedThenGet::kValue)
          {
              OAuth2AccessToken cached;
              if (deserializeAccessToken(value, cached))
              {
                  self->emitMetric("hit");
                  if (!fired->exchange(true))
                      (*sharedCb)(std::move(cached));
                  return;
              }
              // Corrupt entry → treat as a miss.
          }
          // Miss → delegate + fill.
          self->emitMetric("miss");
          auto fillCb = std::make_shared<AccessTokenCallback>(
            [self, sharedCb, fired, token](const std::optional<OAuth2AccessToken> &t) {
                if (t && self->redisClient_)
                {
                    // C7: only cache if the token has positive remaining
                    // lifetime. Redis rejects EX ≤ 0; an expired token is
                    // not worth caching anyway.
                    int64_t now = std::time(nullptr);
                    int64_t remaining = t->expiresAt - now;
                    if (remaining > 0)
                    {
                        int ttl = static_cast<int>(
                          std::min<int64_t>(remaining, self->accessTokenMaxTtlSeconds_)
                        );
                        std::string payload = serializeAccessToken(*t);
                        std::string k = "authforge:cache:token:access:" + token;
                        self->redisClient_->execCommandAsync(
                          [](const RedisResult &) {},
                          [k](const RedisException &e) {
                              LOG_DEBUG << "RedisCachedTokenRepository: access fill SET "
                                           "failed for "
                                        << k << ": " << e.what();
                          },
                          "SET %s %s EX %d",
                          k.c_str(),
                          payload.c_str(),
                          ttl
                        );
                    }
                }
                if (!fired->exchange(true))
                    (*sharedCb)(t);
            }
          );
          self->impl_->getAccessToken(token, std::move(*fillCb));
      },
      [self, sharedCb, fired, token](const RedisException &e) {
          LOG_WARN << "RedisCachedTokenRepository: EVAL revoked+access error: " << e.what();
          self->emitMetric("error");
          if (!fired->exchange(true))
              self->impl_->getAccessToken(token, std::move(*sharedCb));
      },
      "EVAL %s 2 %s %s",
      kRevokedThenGetScript,
      revokedKey.c_str(),
      key.c_str()
    );
}

// ---------------------------------------------------------------------------
// introspectToken (N2 discriminator: cache only when access cache exists)
// ---------------------------------------------------------------------------
void RedisCachedTokenRepository::introspectToken(
  const std::string &token,
  TokenIntrospectionCallback &&cb
)
{
    auto sharedCb = std::make_shared<TokenIntrospectionCallback>(std::move(cb));
    auto fired = std::make_shared<std::atomic<bool>>(false);
    auto self = shared_from_this();
    std::string revokedKey = "authforge:cache:token:revoked:" + token;
    std::string introKey = "authforge:cache:token:introspect:" + token;

    if (!redisClient_)
    {
        emitMetric("miss");
        impl_->introspectToken(token, std::move(*sharedCb));
        return;
    }

    // Negative cache (revoked → active=false) then the positive introspection
    // cache — both in ONE EVAL round-trip (Wave-2 P2).
    redisClient_->execCommandAsync(
      [self, sharedCb, fired, token](const RedisResult &result) {
          std::string value;
          const auto state = decodeRevokedThenGet(result, value);
          if (state == RevokedThenGet::kRevoked)
          {
              self->emitMetric("hit");
              if (!fired->exchange(true))
              {
                  TokenIntrospection inactive;
                  inactive.active = false;
                  (*sharedCb)(std::move(inactive));
              }
              return;
          }
          if (state == RevokedThenGet::kValue)
          {
              TokenIntrospection cached;
              if (deserializeIntrospection(value, cached))
              {
                  self->emitMetric("hit");
                  if (!fired->exchange(true))
                      (*sharedCb)(std::move(cached));
                  return;
              }
              // Corrupt entry → treat as a miss.
          }
          // Miss → delegate. The N2 discriminator runs after.
          self->emitMetric("miss");
          auto wrapCb = std::make_shared<TokenIntrospectionCallback>(
            [self, sharedCb, fired, token](const std::optional<TokenIntrospection> &res) {
                // N2: only cache when active AND the token is confirmed
                // access-token-only (its access-cache key exists). The
                // access key can only be populated by getAccessToken /
                // saveAccessToken, both access-token-only — so its
                // presence proves this introspection did NOT come from
                // the refresh-token fallthrough.
                if (res && res->active && self->redisClient_)
                {
                    std::string accessKey = "authforge:cache:token:access:" + token;
                    self->redisClient_->execCommandAsync(
                      [self, res, token](const RedisResult &ex) {
                          if (ex.type() == RedisResultType::kInteger && ex.asInteger() > 0)
                          {
                              int64_t now = std::time(nullptr);
                              int64_t remaining = res->exp - now;
                              if (remaining > 0)
                              {
                                  int ttl = static_cast<int>(std::min<int64_t>(
                                    remaining, self->accessTokenMaxTtlSeconds_
                                  ));
                                  std::string payload = serializeIntrospection(*res);
                                  std::string k = "authforge:cache:token:introspect:" + token;
                                  self->redisClient_->execCommandAsync(
                                    [](const RedisResult &) {},
                                    [k](const RedisException &e) {
                                        LOG_DEBUG << "RedisCachedTokenRepository: introspect "
                                                     "fill SET failed for "
                                                  << k << ": " << e.what();
                                    },
                                    "SET %s %s EX %d",
                                    k.c_str(),
                                    payload.c_str(),
                                    ttl
                                  );
                              }
                          }
                      },
                      [](const RedisException &) {},
                      "EXISTS %s",
                      accessKey.c_str()
                    );
                }
                if (!fired->exchange(true))
                    (*sharedCb)(res);
            }
          );
          self->impl_->introspectToken(token, std::move(*wrapCb));
      },
      [self, sharedCb, fired, token](const RedisException &e) {
          LOG_WARN << "RedisCachedTokenRepository: EVAL revoked+introspect error: " << e.what();
          self->emitMetric("error");
          if (!fired->exchange(true))
              self->impl_->introspectToken(token, std::move(*sharedCb));
      },
      "EVAL %s 2 %s %s",
      kRevokedThenGetScript,
      revokedKey.c_str(),
      introKey.c_str()
    );
}

// ---------------------------------------------------------------------------
// revokeAccessToken (negative-cache-before-DEL, §5.4)
// ---------------------------------------------------------------------------
void RedisCachedTokenRepository::revokeAccessToken(
  const std::string &token,
  const std::string &revokedBy,
  VoidCallback &&cb
)
{
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));
    auto self = shared_from_this();

    impl_->revokeAccessToken(token, revokedBy, [self, sharedCb, token]() {
        // After the Postgres write succeeds, invalidate the caches. Order
        // matters: SET the negative entry BEFORE DEL-ing the positive entries
        // so a concurrent reader that re-populates the positive cache is
        // corrected on its next read (it sees the revoked marker). The
        // negative entry's 60s TTL is the N3 fixed value (revokeAccessToken
        // has no exp). The SET stays fire-and-forget (best-effort); the
        // positive-entry DELs now go through the shared double-delete helper
        // (#79: the delayed second DEL closes the refill race this comment's
        // marker alone only self-corrects on the NEXT read; #80: failures
        // become WARN/ERROR + counter instead of invisible). The user
        // callback fires regardless.
        if (self->redisClient_)
        {
            std::string revokedKey = "authforge:cache:token:revoked:" + token;
            std::string accessKey = "authforge:cache:token:access:" + token;
            std::string introKey = "authforge:cache:token:introspect:" + token;
            // SET negative entry (60s).
            self->redisClient_->execCommandAsync(
              [](const RedisResult &) {},
              [](const RedisException &) {},
              "SET %s 1 EX 60",
              revokedKey.c_str()
            );
            // DEL positive entries (double-delete, failure-observable).
            invalidateWithDoubleDelete(
              self->redisClient_, accessKey, self->metrics_, "token"
            );
            invalidateWithDoubleDelete(
              self->redisClient_, introKey, self->metrics_, "token"
            );
        }
        if (*sharedCb)
            (*sharedCb)();
    });
}

// ---------------------------------------------------------------------------
// saveAccessToken (cache warming — improves cold-start for the N2 discriminator)
// ---------------------------------------------------------------------------
void RedisCachedTokenRepository::saveAccessToken(
  const OAuth2AccessToken &token,
  VoidCallback &&cb
)
{
    auto sharedCb = std::make_shared<VoidCallback>(std::move(cb));
    auto self = shared_from_this();

    impl_->saveAccessToken(token, [self, sharedCb, token]() {
        // Warm the access cache: a freshly-saved token is definitely active
        // and unexpired. This populates token:access:{hash} so the first
        // introspectToken (N2 discriminator) + validateAccessToken both hit.
        // C7: skip if the token is already expired.
        if (self->redisClient_)
        {
            int64_t now = std::time(nullptr);
            int64_t remaining = token.expiresAt - now;
            if (remaining > 0)
            {
                int ttl = static_cast<int>(
                  std::min<int64_t>(remaining, self->accessTokenMaxTtlSeconds_)
                );
                std::string payload = serializeAccessToken(token);
                std::string key = "authforge:cache:token:access:" + token.token;
                self->redisClient_->execCommandAsync(
                  [](const RedisResult &) {},
                  [key](const RedisException &e) {
                      LOG_DEBUG << "RedisCachedTokenRepository: warm SET failed for " << key << ": "
                                << e.what();
                  },
                  "SET %s %s EX %d",
                  key.c_str(),
                  payload.c_str(),
                  ttl
                );
            }
        }
        if (*sharedCb)
            (*sharedCb)();
    });
}

}  // namespace authforge::storage::redis
