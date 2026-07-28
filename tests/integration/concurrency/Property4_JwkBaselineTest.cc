// tests/integration/concurrency/Property4_JwkBaselineTest.cc
//
// Spec: concurrency-lifetime-safety-audit — Task 4 (Property 4: Preservation).
// Behavioral Equivalence on ¬C(X). Observation object **3.3**: after init, with
// run-time READ-ONLY access to the JwkManager, the SAME signing key / `kid` /
// RS256 algorithm must keep producing consistent id_token / JWKS across the
// category-B fix (7.2, re-verified at 7.4).
//
// ─────────────────────────────────────────────────────────────────────────
// WHAT IS PINNED (and what intentionally is NOT)
// ─────────────────────────────────────────────────────────────────────────
// The signing key in the dev/test path is an EPHEMERAL RSA key generated per
// JwkManager instance, so the exact `n` / signature BYTES differ per process
// and are NOT a stable baseline. What IS stable — and what the 7.2 fix
// (init-once-then-read-only / shared_ptr<const JwkManager>) must preserve — are
// the STRUCTURAL and CONSISTENCY invariants of a SINGLE initialized instance:
//   * JWKS shape: one key, kty=RSA, use=sig, alg=RS256, kid == getKeyId(), with
//     non-empty base64url n / e.
//   * id_token (signJwt) shape: header.payload.signature with a decodable
//     header { alg:RS256, typ:JWT, kid:<getKeyId()> } and a payload that
//     round-trips the claims.
//   * SAME-INSTANCE CONSISTENCY: every signJwt / getJwks on one initialized
//     instance uses the SAME kid and the SAME public modulus, and RS256
//     (PKCS#1 v1.5, deterministic) re-signing the identical payload yields the
//     IDENTICAL signature. This is precisely the "run-time read-only =>
//     consistent key state" property 3.3 promises.
//
// METHODOLOGY: observation-first, runs on UNFIXED code (F), MUST PASS. The
// access here is the ¬C(X) ordering: init() fully completes BEFORE any
// signJwt()/getJwks() read (no concurrent mutation). No production code changed.
//
// **Validates: Requirements 3.3**
//
// _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7_

#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>
#include <json/json.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include <authforge/oauth2/jwk/JwkManager.h>

#include "Property4_PreservationSupport.h"

using namespace authforge::test::concurrency;
using authforge::oauth2::JwkManager;

namespace
{
// Split "a.b.c" into its dot-separated segments.
std::vector<std::string> splitDots(const std::string &s)
{
    std::vector<std::string> out;
    std::string cur;
    std::istringstream is(s);
    while (std::getline(is, cur, '.'))
        out.push_back(cur);
    return out;
}

bool parseJson(const std::string &str, Json::Value &out)
{
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream is(str);
    return Json::parseFromStream(b, is, &out, &errs);
}

// base64url-decode a JWT segment (no padding): translate the URL-safe alphabet
// back to standard base64, re-pad to a multiple of 4, then use Drogon's decoder
// (this Drogon's base64Decode takes only the encoded string).
std::string b64UrlDecode(const std::string &seg)
{
    std::string s = seg;
    for (auto &c : s)
    {
        if (c == '-')
            c = '+';
        else if (c == '_')
            c = '/';
    }
    while (s.size() % 4 != 0)
        s.push_back('=');
    return drogon::utils::base64Decode(s);
}

Json::Value sampleClaims()
{
    Json::Value p;
    p["iss"] = "http://localhost:5555";
    p["sub"] = "alice";
    p["aud"] = "test-client";
    p["iat"] = (Json::Int64)1700000000;
    p["exp"] = (Json::Int64)1700003600;
    p["nonce"] = "nonce-xyz";
    return p;
}
}  // namespace

// 3.3 PRESERVATION (JWKS shape): a single initialized JwkManager exposes exactly
// one RS256 signing key whose kid matches getKeyId() and whose n/e are present.
DROGON_TEST(Property4_3_3_Jwks_Shape_Baseline)
{
    JwkManager jwk;
    REQUIRE(jwk.init(Json::Value(Json::objectValue)) == true);
    REQUIRE(jwk.isInitialized() == true);

    Json::Value jwks = jwk.getJwks();
    REQUIRE(jwks.isMember("keys"));
    REQUIRE(jwks["keys"].isArray());
    REQUIRE(jwks["keys"].size() == 1);

    const Json::Value &key = jwks["keys"][0];
    CHECK(key["kty"].asString() == "RSA");
    CHECK(key["use"].asString() == "sig");
    CHECK(key["alg"].asString() == "RS256");
    CHECK(key["kid"].asString() == jwk.getKeyId());
    CHECK(!key["n"].asString().empty());
    CHECK(!key["e"].asString().empty());
    // RSA F4 public exponent 65537 -> base64url(0x010001) == "AQAB".
    CHECK(key["e"].asString() == "AQAB");
}

// 3.3 PRESERVATION (id_token shape): signJwt produces header.payload.signature
// with a decodable RS256/JWT/kid header and a claims-preserving payload.
DROGON_TEST(Property4_3_3_SignJwt_Shape_Baseline)
{
    JwkManager jwk;
    REQUIRE(jwk.init(Json::Value(Json::objectValue)) == true);

    const std::string jwt = jwk.signJwt(sampleClaims());
    REQUIRE(!jwt.empty());

    auto segs = splitDots(jwt);
    REQUIRE(segs.size() == 3);
    CHECK(!segs[0].empty());
    CHECK(!segs[1].empty());
    CHECK(!segs[2].empty());

    // Header decodes to the frozen RS256/JWT/kid envelope.
    Json::Value header;
    REQUIRE(parseJson(b64UrlDecode(segs[0]), header));
    CHECK(header["alg"].asString() == "RS256");
    CHECK(header["typ"].asString() == "JWT");
    CHECK(header["kid"].asString() == jwk.getKeyId());

    // Payload round-trips the claims.
    Json::Value payload;
    REQUIRE(parseJson(b64UrlDecode(segs[1]), payload));
    CHECK(payload["iss"].asString() == "http://localhost:5555");
    CHECK(payload["sub"].asString() == "alice");
    CHECK(payload["aud"].asString() == "test-client");
    CHECK(payload["nonce"].asString() == "nonce-xyz");
}

// 3.3 PRESERVATION (same-instance consistency): repeated reads of one
// initialized instance use the SAME kid and the SAME public modulus, and RS256
// re-signing the identical payload is deterministic (identical signature). This
// is exactly the "run-time read-only => consistent key" guarantee.
DROGON_TEST(Property4_3_3_SameInstance_Consistency_Baseline)
{
    JwkManager jwk;
    REQUIRE(jwk.init(Json::Value(Json::objectValue)) == true);

    const std::string kid = jwk.getKeyId();
    const std::string n0 = jwk.getJwks()["keys"][0]["n"].asString();

    Json::Value claims = sampleClaims();
    const std::string jwtA = jwk.signJwt(claims);
    const std::string jwtB = jwk.signJwt(claims);

    // Deterministic RS256 (PKCS#1 v1.5): identical input -> identical token.
    CHECK(jwtA == jwtB);

    // Many reads -> identical kid + modulus every time (no key churn at runtime).
    for (int i = 0; i < 8; ++i)
    {
        Json::Value jwks = jwk.getJwks();
        CHECK(jwks["keys"][0]["kid"].asString() == kid);
        CHECK(jwks["keys"][0]["n"].asString() == n0);

        const std::string jwt = jwk.signJwt(claims);
        // kid in every freshly-signed header stays constant.
        auto segs = splitDots(jwt);
        REQUIRE(segs.size() == 3);
        Json::Value header;
        REQUIRE(parseJson(b64UrlDecode(segs[0]), header));
        CHECK(header["kid"].asString() == kid);
    }
}

// 3.3 PRESERVATION (PBT over random claim payloads): for random-but-reproducible
// normal claim sets, the signed token keeps the frozen header and round-trips
// its payload. The signing key/kid stays constant across all of them.
DROGON_TEST(Property4_3_3_SignJwt_RandomizedClaims_Baseline)
{
    JwkManager jwk;
    REQUIRE(jwk.init(Json::Value(Json::objectValue)) == true);
    const std::string kid = jwk.getKeyId();

    PreservationInputGen gen(0x33CC33CCu);

    constexpr int kRounds = 16;
    for (int round = 0; round < kRounds; ++round)
    {
        Json::Value claims;
        claims["iss"] = "http://localhost:5555";
        claims["sub"] = "user-" + std::to_string(gen.intInRange(1, 99999));
        claims["aud"] = gen.clientId();
        claims["scope"] = gen.scope();
        claims["iat"] = (Json::Int64)(1700000000 + gen.intInRange(0, 100000));

        const std::string jwt = jwk.signJwt(claims);
        REQUIRE(!jwt.empty());
        auto segs = splitDots(jwt);
        REQUIRE(segs.size() == 3);

        Json::Value header;
        REQUIRE(parseJson(b64UrlDecode(segs[0]), header));
        CHECK(header["alg"].asString() == "RS256");
        CHECK(header["typ"].asString() == "JWT");
        CHECK(header["kid"].asString() == kid);

        Json::Value payload;
        REQUIRE(parseJson(b64UrlDecode(segs[1]), payload));
        CHECK(payload["sub"].asString() == claims["sub"].asString());
        CHECK(payload["aud"].asString() == claims["aud"].asString());
        CHECK(payload["scope"].asString() == claims["scope"].asString());
    }
}
