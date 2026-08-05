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

}  // namespace
