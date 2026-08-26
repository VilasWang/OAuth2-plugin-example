// #71: Redis-backed ISocialLinkStateStore. State tokens are one-time,
// short-TTL, and bound to (internal user id, provider):
//   issue  -> SET social_link_state:{token} {json} NX EX 600
//   consume-> GETDEL social_link_state:{token}   (atomic single use, Redis >= 6.2)
// The value is a small JSON object; the token is 128-bit random hex from
// ICryptoProvider::secureRandomBytes.

#include <fulla/drogon/adapters/RedisSocialLinkStateStore.h>

#ifdef WITH_SOCIAL

#include <drogon/drogon.h>
#include <fulla/common/ports/ICryptoProvider.h>

#include <chrono>
#include <cstring>
#include <sstream>

namespace fulla::drogon::adapters
{

namespace
{
std::string stateKey(const std::string &token)
{
    return "social_link_state:" + token;
}

std::string encodeValue(const fulla::identity::SocialLinkStateData &data)
{
    Json::Value json;
    json["user_id"] = data.internalUserId;
    json["provider"] = data.provider;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, json);
}

fulla::identity::SocialLinkStateData decodeValue(const std::string &value)
{
    fulla::identity::SocialLinkStateData data;
    Json::CharReaderBuilder builder;
    std::istringstream iss(value);
    Json::Value json;
    if (Json::parseFromStream(builder, iss, &json, nullptr))
    {
        if (json.isMember("user_id") && json["user_id"].isNumeric())
            data.internalUserId = json["user_id"].asInt();
        if (json.isMember("provider") && json["provider"].isString())
            data.provider = json["provider"].asString();
    }
    return data;
}
}  // namespace

RedisSocialLinkStateStore::RedisSocialLinkStateStore(
  ::drogon::nosql::RedisClientPtr redisClient,
  std::shared_ptr<fulla::common::ports::ICryptoProvider> cryptoProvider,
  int ttlSeconds)
    : redisClient_(std::move(redisClient)),
      cryptoProvider_(std::move(cryptoProvider)),
      ttlSeconds_(ttlSeconds > 0 ? ttlSeconds : 600)
{
}

void RedisSocialLinkStateStore::issue(
  int32_t internalUserId,
  const std::string &provider,
  IssueCallback &&cb)
{
    if (!redisClient_ || !cryptoProvider_)
    {
        cb(std::nullopt);  // fail-closed: linking must not proceed stateless
        return;
    }
    // 16 random bytes -> 32 hex chars; the token itself carries no data.
    unsigned char raw[16];
    if (!cryptoProvider_->secureRandomBytes(raw, sizeof(raw)))
    {
        cb(std::nullopt);  // entropy source unavailable -> fail-closed
        return;
    }
    std::string token;
    token.reserve(sizeof(raw) * 2);
    static const char *hex = "0123456789abcdef";
    for (unsigned char byte : raw)
    {
        token += hex[byte >> 4];
        token += hex[byte & 0x0F];
    }
    const std::string value = encodeValue(fulla::identity::SocialLinkStateData{internalUserId, provider});
    auto sharedCb = std::make_shared<IssueCallback>(std::move(cb));
    redisClient_->execCommandAsync(
      [sharedCb, token](const ::drogon::nosql::RedisResult &r) {
          // SET ... NX: a nil reply means the key existed (collision) --
          // treat as unavailable rather than minting a second token.
          (*sharedCb)(r.isNil() ? std::nullopt : std::optional<std::string>(token));
      },
      [sharedCb](const ::drogon::nosql::RedisException &) {
          (*sharedCb)(std::nullopt);
      },
      "SET %s %s NX EX %d",
      stateKey(token).c_str(),
      value.c_str(),
      ttlSeconds_
    );
}

void RedisSocialLinkStateStore::consume(const std::string &state, ConsumeCallback &&cb)
{
    if (!redisClient_ || state.empty())
    {
        cb(std::nullopt);
        return;
    }
    auto sharedCb = std::make_shared<ConsumeCallback>(std::move(cb));
    // GETDEL (Redis >= 6.2): fetch-and-delete atomically -> replay-proof.
    redisClient_->execCommandAsync(
      [sharedCb](const ::drogon::nosql::RedisResult &r) {
          if (r.isNil() || r.type() != ::drogon::nosql::RedisResultType::kString)
          {
              (*sharedCb)(std::nullopt);
              return;
          }
          (*sharedCb)(decodeValue(r.asString()));
      },
      [sharedCb](const ::drogon::nosql::RedisException &) {
          (*sharedCb)(std::nullopt);
      },
      "GETDEL %s",
      stateKey(state).c_str()
    );
}

}  // namespace fulla::drogon::adapters

#endif  // WITH_SOCIAL
