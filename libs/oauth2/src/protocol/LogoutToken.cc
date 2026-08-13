#include <authforge/oauth2/protocol/LogoutToken.h>

#include <openssl/rand.h>

#include <atomic>
#include <cstdint>
#include <random>
#include <string>

namespace authforge::oauth2::protocol
{

std::string generateJti()
{
    unsigned char buf[16] = {0};
    if (RAND_bytes(buf, static_cast<int>(sizeof(buf))) != 1)
    {
        // OpenSSL PRNG failure is extraordinarily rare; fall back to a
        // std::random_device value salted with a process-local atomic counter
        // so 128-bit uniqueness still holds.
        static std::atomic<std::uint64_t> counter{0};
        std::random_device rd;
        const std::uint64_t c = counter.fetch_add(1, std::memory_order_relaxed);
        const std::uint64_t r = (static_cast<std::uint64_t>(rd()) << 32) |
                                static_cast<std::uint64_t>(rd());
        for (int i = 0; i < 8; ++i)
            buf[i] = static_cast<unsigned char>((c >> (8 * i)) & 0xff);
        for (int i = 0; i < 8; ++i)
            buf[8 + i] = static_cast<unsigned char>((r >> (8 * i)) & 0xff);
    }

    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(sizeof(buf) * 2);
    for (const unsigned char byte : buf)
    {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0f]);
    }
    return out;
}

Json::Value buildLogoutTokenClaims(
  const std::string &issuer,
  const std::string &subject,
  const std::string &audience,
  std::int64_t issuedAtSeconds,
  int ttlSeconds,
  const std::string &jti)
{
    Json::Value claims;
    claims["iss"] = issuer;
    claims["sub"] = subject;
    claims["aud"] = audience;
    claims["iat"] = static_cast<Json::Int64>(issuedAtSeconds);
    claims["exp"] = static_cast<Json::Int64>(issuedAtSeconds + ttlSeconds);
    claims["jti"] = jti;

    // §2.4: events MUST be present and contain the backchannel-logout URN
    // mapped to an empty JSON object.
    Json::Value events(Json::objectValue);
    events[kBackchannelLogoutEventUrn] = Json::Value(Json::objectValue);
    claims["events"] = events;

    return claims;
}

}  // namespace authforge::oauth2::protocol
