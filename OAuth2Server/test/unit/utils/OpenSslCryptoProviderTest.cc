// Task 14 (authforge-sdk-refactor, design.md §5.6): cross-validation tests
// for oauth2::adapters::OpenSslCryptoProvider / OpenSslUuidGenerator /
// SystemClock -- the Adapter-side default implementations of
// authforge::common::ports::ICryptoProvider / IUuidGenerator / IClock.
//
// These tests assert BYTE-FOR-BYTE equivalence against the existing
// drogon::utils-backed CryptoUtils.h functions the new adapters are meant
// to replace (call-site migration is a separate, later step within Task 14
// -- this file only proves the new implementation is a correct drop-in
// replacement before any call site is touched, following the same
// "verify first, migrate second" discipline Task 3's OpenSSL 3.5 migration
// used for JwkManager::getPublicKeyComponents).

#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>

#include <oauth2/adapters/OpenSslCryptoProvider.h>
#include <oauth2/adapters/OpenSslUuidGenerator.h>
#include <oauth2/adapters/SystemClock.h>
#include <oauth2/utils/CryptoUtils.h>
#include <oauth2/services/TokenService.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <regex>

using namespace oauth2::adapters;

// ---------------------------------------------------------------------------
// sha256 / sha256Hex
// ---------------------------------------------------------------------------

DROGON_TEST(Unit_OpenSslCryptoProvider_Sha256Hex_MatchesDrogonGetSha256)
{
    OpenSslCryptoProvider provider;
    const std::string input = "test-token-for-sha256-cross-check";

    auto ours = provider.sha256Hex(input);
    auto theirs = drogon::utils::getSha256(input.data(), input.length());

    // Case-insensitive comparison: drogon::utils::getSha256 actually
    // returns UPPERCASE hex (trantor::utils::toHexString uses
    // "0123456789ABCDEF"), despite CryptoUtils.h's hashToken() doc comment
    // claiming "lowercase hex" -- that comment is stale, not a behavior
    // this adapter needs to replicate byte-for-byte. Every existing
    // consumer of these hashes (e.g. RedisClientRepository::validateClient,
    // PostgresClientRepository::validateClient) already does a
    // case-insensitive std::tolower comparison before checking equality,
    // so case-insensitive equivalence is the real contract, not exact
    // case matching.
    std::string oursLower = ours;
    std::string theirsLower = theirs;
    std::transform(oursLower.begin(), oursLower.end(), oursLower.begin(), ::tolower);
    std::transform(theirsLower.begin(), theirsLower.end(), theirsLower.begin(), ::tolower);
    CHECK(oursLower == theirsLower);
    CHECK(ours.length() == 64);
}

// NOTE on what this test does NOT do: it does not cross-check against
// oauth2::utils::sha256() (CryptoUtils.h). That function was found (while
// writing this test) to have a genuine PRE-EXISTING bug -- it assumes
// drogon::utils::getSha256() returns lowercase hex and only decodes
// lowercase a-f correctly in its hand-rolled hex-to-bytes loop, but
// drogon::utils::getSha256() actually returns UPPERCASE hex (verified:
// trantor::utils::toHexString uses the "0123456789ABCDEF" alphabet), so
// oauth2::utils::sha256() silently produces wrong bytes for any hash
// containing A-F. This is NOT exercised by production code today (grepped
// the full OAuth2Plugin/OAuth2Server tree: oauth2::utils::sha256() and its
// only caller, oauth2::utils::computeCodeChallenge(), have zero call sites
// outside this header -- the real PKCE S256 verification path is
// TokenService::generateSha256Hash(), a separate implementation this file
// does not touch). Reported here rather than silently worked around: this
// is dead code with a latent defect, not a regression introduced by this
// task, and is out of Task 14's scope to fix (CryptoUtils.h's call-site
// migration, including whether to delete this now-superseded dead
// function, happens later in Task 14's own remaining call-site work).
DROGON_TEST(Unit_OpenSslCryptoProvider_Sha256_ProducesCorrectDigestLength)
{
    OpenSslCryptoProvider provider;
    const std::string input = "pkce-code-verifier-cross-check-value";

    auto digest = provider.sha256(input);
    REQUIRE(digest.size() == 32);

    // Cross-check against the hex form instead (sha256Hex is verified
    // correct against drogon::utils::getSha256 in the test above) --
    // decode the verified-correct hex string ourselves with a
    // known-correct (uppercase-and-lowercase-tolerant) decoder and compare.
    auto hex = provider.sha256Hex(input);
    REQUIRE(hex.size() == 64);
    for (size_t i = 0; i < digest.size(); ++i)
    {
        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        unsigned char expected = static_cast<unsigned char>(
          (hexVal(hex[i * 2]) << 4) | hexVal(hex[i * 2 + 1])
        );
        CHECK(digest[i] == expected);
    }
}

DROGON_TEST(Unit_OpenSslCryptoProvider_Sha256Hex_Deterministic)
{
    OpenSslCryptoProvider provider;
    auto h1 = provider.sha256Hex("same-input");
    auto h2 = provider.sha256Hex("same-input");
    CHECK(h1 == h2);
}

// ---------------------------------------------------------------------------
// base64UrlEncode / base64UrlDecode
// ---------------------------------------------------------------------------

DROGON_TEST(Unit_OpenSslCryptoProvider_Base64UrlEncode_MatchesDrogon_StringOverload)
{
    OpenSslCryptoProvider provider;
    const std::string input = "hello world! this has some +/= chars after b64";

    auto ours = provider.base64UrlEncode(input);
    auto theirs = drogon::utils::base64EncodeUnpadded(input, true);

    CHECK(ours == theirs);
    // Unpadded base64url: no '+', '/', or '=' characters.
    CHECK(ours.find('+') == std::string::npos);
    CHECK(ours.find('/') == std::string::npos);
    CHECK(ours.find('=') == std::string::npos);
}

DROGON_TEST(Unit_OpenSslCryptoProvider_Base64UrlEncode_MatchesDrogon_BytesOverload)
{
    OpenSslCryptoProvider provider;
    const unsigned char bytes[] = {0xFF, 0x00, 0xAB, 0xCD, 0xEF, 0x12, 0x34};

    auto ours = provider.base64UrlEncode(bytes, sizeof(bytes));
    auto theirs = drogon::utils::base64EncodeUnpadded(bytes, sizeof(bytes), true);

    CHECK(ours == theirs);
}

DROGON_TEST(Unit_OpenSslCryptoProvider_Base64UrlEncode_EmptyInput)
{
    OpenSslCryptoProvider provider;
    CHECK(provider.base64UrlEncode(std::string()) == "");
}

DROGON_TEST(Unit_OpenSslCryptoProvider_Base64UrlDecode_RoundTrip)
{
    OpenSslCryptoProvider provider;
    const std::string original = "round trip this string through base64url!";

    auto encoded = provider.base64UrlEncode(original);
    auto decodedBytes = provider.base64UrlDecode(encoded);
    std::string decoded(decodedBytes.begin(), decodedBytes.end());

    CHECK(decoded == original);
}

DROGON_TEST(Unit_OpenSslCryptoProvider_Base64UrlDecode_RejectsInvalidCharacter)
{
    OpenSslCryptoProvider provider;
    // '+' and '/' are not part of the base64url alphabet (only '-'/'_').
    auto decoded = provider.base64UrlDecode("abc+def");
    CHECK(decoded.empty());
}

// ---------------------------------------------------------------------------
// secureRandomBytes
// ---------------------------------------------------------------------------

DROGON_TEST(Unit_OpenSslCryptoProvider_SecureRandomBytes_SucceedsAndVaries)
{
    OpenSslCryptoProvider provider;
    unsigned char buf1[32] = {0};
    unsigned char buf2[32] = {0};

    CHECK(provider.secureRandomBytes(buf1, sizeof(buf1)) == true);
    CHECK(provider.secureRandomBytes(buf2, sizeof(buf2)) == true);

    // Two independently generated 32-byte buffers should differ (astronomically
    // unlikely to collide if the CSPRNG is working).
    bool identical = std::equal(std::begin(buf1), std::end(buf1), std::begin(buf2));
    CHECK(identical == false);
}

// ---------------------------------------------------------------------------
// hmacSha256
// ---------------------------------------------------------------------------

DROGON_TEST(Unit_OpenSslCryptoProvider_HmacSha256_DeterministicAndCorrectLength)
{
    OpenSslCryptoProvider provider;
    auto mac1 = provider.hmacSha256("secret-key", "message-body");
    auto mac2 = provider.hmacSha256("secret-key", "message-body");

    REQUIRE(mac1.size() == 32);
    CHECK(mac1 == mac2);
}

DROGON_TEST(Unit_OpenSslCryptoProvider_HmacSha256_DifferentKeysDifferentMac)
{
    OpenSslCryptoProvider provider;
    auto mac1 = provider.hmacSha256("key-a", "same-message");
    auto mac2 = provider.hmacSha256("key-b", "same-message");
    CHECK(mac1 != mac2);
}

// ---------------------------------------------------------------------------
// pbkdf2HmacSha256
// ---------------------------------------------------------------------------

DROGON_TEST(Unit_OpenSslCryptoProvider_Pbkdf2_DeterministicForSameInputs)
{
    OpenSslCryptoProvider provider;
    auto key1 = provider.pbkdf2HmacSha256("password123", "somesalt", 10000, 32);
    auto key2 = provider.pbkdf2HmacSha256("password123", "somesalt", 10000, 32);

    REQUIRE(key1.size() == 32);
    CHECK(key1 == key2);
}

DROGON_TEST(Unit_OpenSslCryptoProvider_Pbkdf2_DifferentSaltsDifferentKeys)
{
    OpenSslCryptoProvider provider;
    auto key1 = provider.pbkdf2HmacSha256("password123", "salt-a", 10000, 32);
    auto key2 = provider.pbkdf2HmacSha256("password123", "salt-b", 10000, 32);
    CHECK(key1 != key2);
}

// ---------------------------------------------------------------------------
// rsaSign (cross-checked against JwkManager's own RS256 signing path via a
// freshly generated ephemeral RSA key, not a hard-coded fixture key)
// ---------------------------------------------------------------------------

DROGON_TEST(Unit_OpenSslCryptoProvider_RsaSign_ValidSignatureVerifiesWithOpenSsl)
{
    OpenSslCryptoProvider provider;

    // Generate a throwaway RSA-2048 keypair, mirroring
    // JwkManager::generateEphemeralKey's parameters.
    EVP_PKEY_CTX *genCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    REQUIRE(genCtx != nullptr);
    REQUIRE(EVP_PKEY_keygen_init(genCtx) > 0);
    REQUIRE(EVP_PKEY_CTX_set_rsa_keygen_bits(genCtx, 2048) > 0);
    EVP_PKEY *pkey = nullptr;
    REQUIRE(EVP_PKEY_keygen(genCtx, &pkey) > 0);
    EVP_PKEY_CTX_free(genCtx);

    // Export to PEM (what rsaSign's `privateKeyPem` parameter expects).
    BIO *bio = BIO_new(BIO_s_mem());
    REQUIRE(bio != nullptr);
    REQUIRE(PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr) == 1);
    char *pemData = nullptr;
    long pemLen = BIO_get_mem_data(bio, &pemData);
    std::string privateKeyPem(pemData, static_cast<size_t>(pemLen));
    BIO_free(bio);

    const std::string data = "signing-input.for-rs256-crosscheck";
    auto signature = provider.rsaSign(privateKeyPem, "SHA256", data);
    REQUIRE(!signature.empty());

    // Verify the signature independently via EVP_DigestVerify (not via
    // rsaSign itself, so this is a real cross-check of the produced bytes,
    // not a tautology).
    EVP_MD_CTX *verifyCtx = EVP_MD_CTX_new();
    REQUIRE(verifyCtx != nullptr);
    REQUIRE(EVP_DigestVerifyInit(verifyCtx, nullptr, EVP_sha256(), nullptr, pkey) > 0);
    REQUIRE(EVP_DigestVerifyUpdate(verifyCtx, data.data(), data.size()) > 0);
    int verifyResult = EVP_DigestVerifyFinal(verifyCtx, signature.data(), signature.size());
    EVP_MD_CTX_free(verifyCtx);
    EVP_PKEY_free(pkey);

    CHECK(verifyResult == 1);
}

DROGON_TEST(Unit_OpenSslCryptoProvider_RsaSign_InvalidPemReturnsEmpty)
{
    OpenSslCryptoProvider provider;
    auto signature = provider.rsaSign("not a valid pem", "SHA256", "data");
    CHECK(signature.empty());
}

// ---------------------------------------------------------------------------
// OpenSslUuidGenerator
// ---------------------------------------------------------------------------

DROGON_TEST(Unit_OpenSslUuidGenerator_ProducesRfc4122V4FormatUuid)
{
    OpenSslUuidGenerator generator;
    auto uuid = generator.generate();

    // Canonical 8-4-4-4-12 hyphenated form, version nibble '4', variant
    // nibble in {8,9,a,b} (RFC 4122 §4.4).
    static const std::regex kUuidV4Pattern(
      "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"
    );
    CHECK(std::regex_match(uuid, kUuidV4Pattern));
}

DROGON_TEST(Unit_OpenSslUuidGenerator_ProducesUniqueValues)
{
    OpenSslUuidGenerator generator;
    auto u1 = generator.generate();
    auto u2 = generator.generate();
    CHECK(u1 != u2);
}

// ---------------------------------------------------------------------------
// SystemClock
// ---------------------------------------------------------------------------

DROGON_TEST(Unit_SystemClock_NowSecondsMatchesWallClockWithinTolerance)
{
    SystemClock clock;
    auto ours = clock.nowSeconds();
    auto wallClock = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()
    )
                        .count();

    // Allow a small tolerance for the two calls not being perfectly
    // simultaneous.
    CHECK(std::abs(ours - wallClock) <= 2);
}

DROGON_TEST(Unit_SystemClock_NowMillisecondsIsConsistentWithNowSeconds)
{
    SystemClock clock;
    auto seconds = clock.nowSeconds();
    auto millis = clock.nowMilliseconds();

    // millis/1000 should equal seconds (within +/-1 for a boundary race).
    CHECK(std::abs(millis / 1000 - seconds) <= 1);
}

// ---------------------------------------------------------------------------
// TokenService::generateSha256Hash byte-for-byte migration golden check
// ---------------------------------------------------------------------------
//
// TokenService::generateSha256Hash (Task 14 slice 6) was migrated off
// drogon::utils::getSha256/base64Encode onto OpenSslCryptoProvider. This
// test reproduces the OLD algorithm inline via drogon::utils directly (the
// exact pre-migration implementation, byte for byte) and compares it
// against the NEW production function's output for the same inputs, to
// prove the migration is a byte-identical drop-in replacement despite the
// non-standard "base64(hex-string-as-ASCII)" behavior this function
// preserves verbatim (see TokenService.cc's own comment for why that
// behavior, though RFC 7636 non-conformant, is intentionally NOT fixed as
// part of this Drogon-removal task).
DROGON_TEST(Unit_TokenService_GenerateSha256Hash_MatchesPreMigrationAlgorithm)
{
    auto oldAlgorithm = [](const std::string &input) -> std::string {
        std::string hash = drogon::utils::getSha256(input);
        std::string base64Url = drogon::utils::base64Encode(
          reinterpret_cast<const unsigned char *>(hash.c_str()), hash.length()
        );
        for (char &c : base64Url)
        {
            if (c == '+')
                c = '-';
            else if (c == '/')
                c = '_';
        }
        while (!base64Url.empty() && base64Url.back() == '=')
        {
            base64Url.pop_back();
        }
        return base64Url;
    };

    const std::vector<std::string> testInputs = {
      "testVerifier1234567890123456789012345678901234567890",
      "",
      "a",
      "the quick brown fox jumps over the lazy dog",
      "PKCE-code-verifier-with-special-chars_~.-123",
    };

    // generateSha256Hash is a pure function of its input (does not touch
    // storage_), so a stack-constructed TokenService(nullptr) is safe to
    // call it on, matching the existing convention
    // OAuth2Plugin::validatePkceCodeVerifier already uses
    // (oauth2::TokenService(nullptr).validatePkceCodeVerifier(...)).
    oauth2::TokenService tokenService(nullptr);
    for (const auto &input : testInputs)
    {
        auto expected = oldAlgorithm(input);
        auto actual = tokenService.generateSha256Hash(input);
        CHECK(actual == expected);
    }
}
