// M2b Task 17 slice 10 (authforge-sdk-refactor): basic unit tests for the
// relocated authforge::oauth2::JwkManager. Full concurrency/preservation
// coverage remains in tests/ (Property4_JwkBaselineTest.cc/
// CategoryB_JwkManagerRaceTest.cc) -- these are just Domain-layer smoke
// tests confirming the class works standalone (no Drogon, no injected
// logger required).
//
// Coverage additions (P1): the original tests only exercised the ephemeral
// fallback path. The additions below cover the production key-loading
// branches (OAUTH2_SIGNING_KEY / OAUTH2_JWT_KEY_PATH / config
// signing_key_path), the kid override, the logger forwarding path, and
// the JWKS use/e fields.

#include <authforge/common/testing/FakeLogger.h>
#include <authforge/oauth2/jwk/JwkManager.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <string>

namespace
{

using authforge::common::testing::FakeLogger;
using authforge::oauth2::JwkManager;

// RAII guard: save an env var, set it for the test body, restore it on
// destruction. Keeps env-var-mutating tests isolated from one another and
// from the host environment.
class EnvVarGuard
{
  public:
    explicit EnvVarGuard(const std::string &name, std::string value)
        : name_(name), hadValue_(std::getenv(name.c_str()) != nullptr),
          oldValue_(hadValue_ ? std::getenv(name.c_str()) : std::string{})
    {
        set(std::move(value));
    }

    ~EnvVarGuard()
    {
        if (hadValue_)
            set(oldValue_);
        else
            unset();
    }

  private:
    void set(const std::string &v)
    {
#ifdef _WIN32
        _putenv_s(name_.c_str(), v.c_str());
#else
        setenv(name_.c_str(), v.c_str(), 1);
#endif
    }
    void unset()
    {
#ifdef _WIN32
        _putenv_s(name_.c_str(), "");
#else
        unsetenv(name_.c_str());
#endif
    }

    std::string name_;
    bool hadValue_;
    std::string oldValue_;
};

// Generate a fresh RSA private key (PEM) for this test run. Used to feed
// a known-valid PEM into the env-var / file-path branches.
std::string generateTestPem()
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EXPECT_NE(ctx, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
    EVP_PKEY *pkey = nullptr;
    EVP_PKEY_keygen(ctx, &pkey);
    EVP_PKEY_CTX_free(ctx);
    EXPECT_NE(pkey, nullptr);

    BIO *bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    EVP_PKEY_free(pkey);

    char *data;
    long len = BIO_get_mem_data(bio, &data);
    std::string pem(data, static_cast<size_t>(len));
    BIO_free(bio);
    return pem;
}

// Write a PEM to a temp file and return its path.
std::string writeTempPem(const std::string &pem, const std::string &suffix)
{
    std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : "/tmp") +
                       "/jwktest_" + suffix + ".pem";
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << pem;
    f.close();
    return path;
}

TEST(JwkManagerTest, InitWithoutLogger_GeneratesEphemeralKey)
{
    JwkManager jwk;  // no logger injected -- log() must be a safe no-op
    EXPECT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    EXPECT_TRUE(jwk.isInitialized());
    EXPECT_FALSE(jwk.getKeyId().empty());
}

TEST(JwkManagerTest, SignJwt_BeforeInit_ReturnsEmptyString)
{
    JwkManager jwk;
    EXPECT_EQ(jwk.signJwt(Json::Value(Json::objectValue)), "");
}

TEST(JwkManagerTest, SignJwt_AfterInit_ProducesThreePartToken)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));

    Json::Value claims;
    claims["sub"] = "alice";
    std::string jwt = jwk.signJwt(claims);

    ASSERT_FALSE(jwt.empty());
    EXPECT_EQ(std::count(jwt.begin(), jwt.end(), '.'), 2);
}

TEST(JwkManagerTest, GetJwks_BeforeInit_ReturnsEmptyKeysArray)
{
    JwkManager jwk;
    Json::Value jwks = jwk.getJwks();
    ASSERT_TRUE(jwks.isMember("keys"));
    EXPECT_EQ(jwks["keys"].size(), 0u);
}

TEST(JwkManagerTest, GetJwks_AfterInit_ContainsOneRsaKey)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));

    Json::Value jwks = jwk.getJwks();
    ASSERT_EQ(jwks["keys"].size(), 1u);
    EXPECT_EQ(jwks["keys"][0]["kty"].asString(), "RSA");
    EXPECT_EQ(jwks["keys"][0]["alg"].asString(), "RS256");
    EXPECT_EQ(jwks["keys"][0]["kid"].asString(), jwk.getKeyId());
    EXPECT_FALSE(jwks["keys"][0]["n"].asString().empty());
}

TEST(JwkManagerTest, InitCalledTwice_SecondCallIsNoOpAndReturnsTrue)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    const std::string firstKid = jwk.getKeyId();

    EXPECT_TRUE(jwk.init(Json::Value(Json::objectValue)));  // no-op, not a failure
    EXPECT_EQ(jwk.getKeyId(), firstKid);                    // key unchanged
}

// ---------------------------------------------------------------------------
// Coverage additions (P1): production key-loading branches + logger + kid.
// ---------------------------------------------------------------------------

// init: OAUTH2_SIGNING_KEY env with a valid PEM -> loads from env, kid
// defaults to "key-1" (JwkManager.cc:48).
TEST(JwkManagerTest, Init_FromOauth2SigningKeyEnv_LoadsPem)
{
    const std::string pem = generateTestPem();
    EnvVarGuard guard("OAUTH2_SIGNING_KEY", pem);
    JwkManager jwk;
    Json::Value config(Json::objectValue);
    EXPECT_TRUE(jwk.init(config));
    EXPECT_TRUE(jwk.isInitialized());
    EXPECT_EQ(jwk.getKeyId(), "key-1");  // default kid for the PEM path
    // A JWT signed with the loaded key must be a 3-part token.
    EXPECT_FALSE(jwk.signJwt(Json::Value(Json::objectValue)).empty());
}

// init: OAUTH2_SIGNING_KEY env with an invalid PEM -> falls through to the
// ephemeral fallback (JwkManager.cc:46 returns false, falls through).
TEST(JwkManagerTest, Init_FromOauth2SigningKeyEnv_InvalidPem_FallsThroughToEphemeral)
{
    EnvVarGuard guard("OAUTH2_SIGNING_KEY", "not-a-valid-pem");
    FakeLogger logger;
    JwkManager jwk(&logger);
    EXPECT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    EXPECT_TRUE(jwk.isInitialized());
    EXPECT_EQ(jwk.getKeyId(), "ephemeral-dev-key");
    // The invalid-PEM parse failure was logged.
    EXPECT_TRUE(logger.hasMessageContaining("Failed to parse PEM"));
}

// init: OAUTH2_JWT_KEY_PATH env pointing at a readable file with a valid
// PEM -> loads from the file (JwkManager.cc:58-76).
TEST(JwkManagerTest, Init_FromOauth2JwtKeyPathEnv_ReadsFileAndLoads)
{
    const std::string pem = generateTestPem();
    const std::string path = writeTempPem(pem, "jwtkeypath_ok");
    EnvVarGuard guard("OAUTH2_JWT_KEY_PATH", path);
    JwkManager jwk;
    EXPECT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    EXPECT_TRUE(jwk.isInitialized());
    EXPECT_EQ(jwk.getKeyId(), "key-1");
}

// init: OAUTH2_JWT_KEY_PATH env pointing at a non-existent file -> warns
// and falls through to ephemeral (JwkManager.cc:78-82).
TEST(JwkManagerTest, Init_FromOauth2JwtKeyPathEnv_UnreadableFile_WarnsAndFallsThrough)
{
    EnvVarGuard guard("OAUTH2_JWT_KEY_PATH", "/nonexistent/path/key.pem");
    FakeLogger logger;
    JwkManager jwk(&logger);
    EXPECT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    EXPECT_EQ(jwk.getKeyId(), "ephemeral-dev-key");
    EXPECT_TRUE(logger.hasMessageContaining("Failed to load key from OAUTH2_JWT_KEY_PATH"));
}

// init: config["signing_key_path"] with a readable valid-PEM file -> loads
// (JwkManager.cc:84-101).
TEST(JwkManagerTest, Init_FromConfigSigningKeyPath_LoadsPem)
{
    const std::string pem = generateTestPem();
    const std::string path = writeTempPem(pem, "configpath_ok");
    Json::Value config(Json::objectValue);
    config["signing_key_path"] = path;
    JwkManager jwk;
    EXPECT_TRUE(jwk.init(config));
    EXPECT_TRUE(jwk.isInitialized());
    EXPECT_EQ(jwk.getKeyId(), "key-1");
}

// init: config["signing_key_path"] pointing at an unreadable file -> warns
// and falls through (JwkManager.cc:103-105).
TEST(JwkManagerTest, Init_FromConfigSigningKeyPath_InvalidPem_WarnsAndFallsThrough)
{
    const std::string path = writeTempPem("garbage-not-a-pem", "configpath_bad");
    Json::Value config(Json::objectValue);
    config["signing_key_path"] = path;
    FakeLogger logger;
    JwkManager jwk(&logger);
    EXPECT_TRUE(jwk.init(config));
    EXPECT_EQ(jwk.getKeyId(), "ephemeral-dev-key");
    EXPECT_TRUE(logger.hasMessageContaining("Failed to parse PEM"));
}

// init: config["kid"] override is reflected in getKeyId and the JWKS.
TEST(JwkManagerTest, Init_KidFromConfig_IsReflectedInKeyIdAndJwks)
{
    const std::string pem = generateTestPem();
    EnvVarGuard guard("OAUTH2_SIGNING_KEY", pem);
    Json::Value config(Json::objectValue);
    config["kid"] = "my-custom-kid";
    JwkManager jwk;
    EXPECT_TRUE(jwk.init(config));
    EXPECT_EQ(jwk.getKeyId(), "my-custom-kid");
    Json::Value jwks = jwk.getJwks();
    ASSERT_EQ(jwks["keys"].size(), 1u);
    EXPECT_EQ(jwks["keys"][0]["kid"].asString(), "my-custom-kid");
}

// init: ephemeral fallback sets kid to "ephemeral-dev-key"
// (JwkManager.cc:114).
TEST(JwkManagerTest, Init_EphemeralKey_KidIsEphemeralDevKey)
{
    JwkManager jwk;
    EXPECT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    EXPECT_EQ(jwk.getKeyId(), "ephemeral-dev-key");
}

// init: a second init() with a logger injected forwards the
// init-once-violation warning through ILogger (the logger-truthy branch of
// log(), JwkManager.cc:16-19).
TEST(JwkManagerTest, Init_WithLogger_ForwardsLogLines)
{
    FakeLogger logger;
    JwkManager jwk(&logger);
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    size_t afterFirst = logger.entries().size();
    EXPECT_TRUE(jwk.init(Json::Value(Json::objectValue)));  // no-op
    // The init-once-violation warning was forwarded.
    bool sawInitOnce = false;
    for (size_t i = afterFirst; i < logger.entries().size(); ++i)
    {
        if (logger.entries()[i].message.find("init() called more than once") != std::string::npos)
        {
            sawInitOnce = true;
            break;
        }
    }
    EXPECT_TRUE(sawInitOnce);
}

// getJwks: after init, the JWKS key carries use == "sig" and a non-empty
// "e" exponent (the original test only checked kty/alg/kid/n).
TEST(JwkManagerTest, GetJwks_AfterInit_AssertsUseAndEFields)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    Json::Value jwks = jwk.getJwks();
    ASSERT_EQ(jwks["keys"].size(), 1u);
    EXPECT_EQ(jwks["keys"][0]["use"].asString(), "sig");
    EXPECT_FALSE(jwks["keys"][0]["e"].asString().empty());
}

// ---------------------------------------------------------------------------
// #78: verifyJwt() matrix -- signature + claim-policy verification for
// /oauth2/end_session's id_token_hint gate. One test per rejection reason,
// plus the happy-path roundtrip and the exp boundary.
// ---------------------------------------------------------------------------

namespace
{
const char *kTestIssuer = "https://auth.example.test";

Json::Value validClaims(long long expOffsetSecs)
{
    Json::Value claims;
    claims["iss"] = kTestIssuer;
    claims["sub"] = "user-123";
    claims["exp"] = static_cast<Json::Int64>(std::time(nullptr) + expOffsetSecs);
    return claims;
}

// Minimal base64url encoder (test-side only) so adversarial headers (alg=none,
// HS256) can be hand-crafted without going through signJwt.
std::string b64Url(const std::string &raw)
{
    static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    unsigned int buffer = 0;
    int bits = 0;
    for (unsigned char c : raw)
    {
        buffer = (buffer << 8) | c;
        bits += 8;
        while (bits >= 6)
        {
            bits -= 6;
            out += alphabet[(buffer >> bits) & 0x3F];
        }
    }
    if (bits > 0)
        out += alphabet[(buffer << (6 - bits)) & 0x3F];
    return out;
}
}  // namespace

TEST(JwkManagerTest, VerifyJwt_SignedBySameManager_ReturnsOk)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    const std::string jwt = jwk.signJwt(validClaims(600));
    ASSERT_FALSE(jwt.empty());
    EXPECT_EQ(
      jwk.verifyJwt(jwt, kTestIssuer, std::time(nullptr)),
      JwkManager::JwtVerificationResult::Ok
    );
}

TEST(JwkManagerTest, VerifyJwt_TamperedPayload_IsBadSignature)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    std::string jwt = jwk.signJwt(validClaims(600));
    // Flip one payload-segment character (base64url alphabet-safe swap).
    const size_t payloadStart = jwt.find('.') + 1;
    jwt[payloadStart] = (jwt[payloadStart] == 'A' ? 'B' : 'A');
    EXPECT_EQ(
      jwk.verifyJwt(jwt, kTestIssuer, std::time(nullptr)),
      JwkManager::JwtVerificationResult::BadSignature
    );
}

TEST(JwkManagerTest, VerifyJwt_TamperedSignature_IsBadSignature)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    std::string jwt = jwk.signJwt(validClaims(600));
    const size_t sigStart = jwt.rfind('.') + 1;
    jwt[sigStart] = (jwt[sigStart] == 'A' ? 'B' : 'A');
    EXPECT_EQ(
      jwk.verifyJwt(jwt, kTestIssuer, std::time(nullptr)),
      JwkManager::JwtVerificationResult::BadSignature
    );
}

TEST(JwkManagerTest, VerifyJwt_AlgNone_IsBadAlg)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    // alg=none with a non-empty (decodable) signature segment: must be
    // rejected by the alg policy, never reach the signature check.
    const std::string header = b64Url(R"({"alg":"none","typ":"JWT"})");
    const std::string payload = b64Url(R"({"iss":")" + std::string(kTestIssuer) +
                                      R"(","sub":"u","exp":9999999999})");
    EXPECT_EQ(
      jwk.verifyJwt(header + "." + payload + ".AAAA", kTestIssuer, std::time(nullptr)),
      JwkManager::JwtVerificationResult::BadAlg
    );
}

TEST(JwkManagerTest, VerifyJwt_AlgHs256_IsBadAlg)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    const std::string header = b64Url(R"({"alg":"HS256","typ":"JWT"})");
    const std::string payload = b64Url(R"({"iss":")" + std::string(kTestIssuer) +
                                      R"(","sub":"u","exp":9999999999})");
    EXPECT_EQ(
      jwk.verifyJwt(header + "." + payload + ".AAAA", kTestIssuer, std::time(nullptr)),
      JwkManager::JwtVerificationResult::BadAlg
    );
}

TEST(JwkManagerTest, VerifyJwt_SignedByOtherKey_IsKidMismatch)
{
    JwkManager verifier;
    ASSERT_TRUE(verifier.init(Json::Value(Json::objectValue)));
    JwkManager forger;
    ASSERT_TRUE(forger.init(Json::Value(Json::objectValue)));
    // Different ephemeral keys -> different kids; the kid check fires before
    // the signature check, so this is KidMismatch (never Ok).
    const std::string jwt = forger.signJwt(validClaims(600));
    EXPECT_EQ(
      verifier.verifyJwt(jwt, kTestIssuer, std::time(nullptr)),
      JwkManager::JwtVerificationResult::KidMismatch
    );
}

TEST(JwkManagerTest, VerifyJwt_WrongIssuer_IsIssuerMismatch)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    const std::string jwt = jwk.signJwt(validClaims(600));
    EXPECT_EQ(
      jwk.verifyJwt(jwt, "https://someone-else.test", std::time(nullptr)),
      JwkManager::JwtVerificationResult::IssuerMismatch
    );
}

TEST(JwkManagerTest, VerifyJwt_Expired_IsExpired_IncludingExactBoundary)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    const long long now = std::time(nullptr);
    const std::string clearlyExpired = jwk.signJwt(validClaims(-10));
    EXPECT_EQ(
      jwk.verifyJwt(clearlyExpired, kTestIssuer, now),
      JwkManager::JwtVerificationResult::Expired
    );
    // exp == now is already expired (strict <= rejection).
    const std::string boundary = jwk.signJwt(validClaims(0));
    EXPECT_EQ(
      jwk.verifyJwt(boundary, kTestIssuer, now),
      JwkManager::JwtVerificationResult::Expired
    );
}

TEST(JwkManagerTest, VerifyJwt_MissingSubject_IsMissingSubject)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    Json::Value claims = validClaims(600);
    claims.removeMember("sub");
    const std::string jwt = jwk.signJwt(claims);
    EXPECT_EQ(
      jwk.verifyJwt(jwt, kTestIssuer, std::time(nullptr)),
      JwkManager::JwtVerificationResult::MissingSubject
    );
}

TEST(JwkManagerTest, VerifyJwt_MissingExp_IsMalformed)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    Json::Value claims = validClaims(600);
    claims.removeMember("exp");
    const std::string jwt = jwk.signJwt(claims);
    EXPECT_EQ(
      jwk.verifyJwt(jwt, kTestIssuer, std::time(nullptr)),
      JwkManager::JwtVerificationResult::Malformed
    );
}

TEST(JwkManagerTest, VerifyJwt_StructurallyInvalidInputs_AreMalformed)
{
    JwkManager jwk;
    ASSERT_TRUE(jwk.init(Json::Value(Json::objectValue)));
    const long long now = std::time(nullptr);
    EXPECT_EQ(jwk.verifyJwt("", kTestIssuer, now), JwkManager::JwtVerificationResult::Malformed);
    EXPECT_EQ(
      jwk.verifyJwt("garbage", kTestIssuer, now), JwkManager::JwtVerificationResult::Malformed
    );
    EXPECT_EQ(
      jwk.verifyJwt("only.two", kTestIssuer, now), JwkManager::JwtVerificationResult::Malformed
    );
    // Empty signature segment.
    const std::string jwt = jwk.signJwt(validClaims(600));
    EXPECT_EQ(
      jwk.verifyJwt(jwt.substr(0, jwt.rfind('.') + 1), kTestIssuer, now),
      JwkManager::JwtVerificationResult::Malformed
    );
}

TEST(JwkManagerTest, VerifyJwt_NotInitialized_FailsClosed)
{
    JwkManager jwk;  // never init()'d
    JwkManager signer;
    ASSERT_TRUE(signer.init(Json::Value(Json::objectValue)));
    const std::string jwt = signer.signJwt(validClaims(600));
    EXPECT_EQ(
      jwk.verifyJwt(jwt, kTestIssuer, std::time(nullptr)),
      JwkManager::JwtVerificationResult::NotInitialized
    );
}

}  // namespace
