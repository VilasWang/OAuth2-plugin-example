// Task 15 (authforge-sdk-refactor, design.md §6/§8): pure gtest unit tests
// proving FakeCryptoProvider gives Domain code deterministic, reproducible
// randomness (its one faked primitive) while every real crypto primitive
// (SHA-256/HMAC/PBKDF2/base64url/RSA-sign) stays a real, correct
// implementation.

#include <authforge/common/testing/FakeCryptoProvider.h>

#include <gtest/gtest.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#include <cstring>

using namespace authforge::common::testing;

TEST(FakeCryptoProviderTest, SecureRandomBytesIsDeterministicForSameSeed)
{
    FakeCryptoProvider a(42);
    FakeCryptoProvider b(42);

    unsigned char bufA[32];
    unsigned char bufB[32];
    ASSERT_TRUE(a.secureRandomBytes(bufA, sizeof(bufA)));
    ASSERT_TRUE(b.secureRandomBytes(bufB, sizeof(bufB)));

    EXPECT_EQ(0, std::memcmp(bufA, bufB, sizeof(bufA)));
}

TEST(FakeCryptoProviderTest, SecureRandomBytesDiffersForDifferentSeeds)
{
    FakeCryptoProvider a(1);
    FakeCryptoProvider b(2);

    unsigned char bufA[32];
    unsigned char bufB[32];
    ASSERT_TRUE(a.secureRandomBytes(bufA, sizeof(bufA)));
    ASSERT_TRUE(b.secureRandomBytes(bufB, sizeof(bufB)));

    EXPECT_NE(0, std::memcmp(bufA, bufB, sizeof(bufA)));
}

TEST(FakeCryptoProviderTest, ResetReplaysSameSequence)
{
    FakeCryptoProvider provider(7);

    unsigned char first[16];
    ASSERT_TRUE(provider.secureRandomBytes(first, sizeof(first)));

    provider.reset();

    unsigned char second[16];
    ASSERT_TRUE(provider.secureRandomBytes(second, sizeof(second)));

    EXPECT_EQ(0, std::memcmp(first, second, sizeof(first)));
}

TEST(FakeCryptoProviderTest, SubsequentCallsProduceDifferentBytes)
{
    // Sanity check that the fake isn't degenerately returning the same
    // buffer contents on every call within one sequence (that would make
    // it a poor stand-in for "looks like independent random draws" even
    // though it's deterministic across runs).
    FakeCryptoProvider provider(99);

    unsigned char first[16];
    unsigned char second[16];
    ASSERT_TRUE(provider.secureRandomBytes(first, sizeof(first)));
    ASSERT_TRUE(provider.secureRandomBytes(second, sizeof(second)));

    EXPECT_NE(0, std::memcmp(first, second, sizeof(first)));
}

TEST(FakeCryptoProviderTest, Sha256HexIsRealAndDeterministic)
{
    FakeCryptoProvider provider;
    // Real SHA-256("abc") test vector (FIPS 180-2 example).
    EXPECT_EQ(
      provider.sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
}

TEST(FakeCryptoProviderTest, Base64UrlEncodeDecodeRoundTrip)
{
    FakeCryptoProvider provider;
    const std::string original = "round trip through fake crypto provider";
    auto encoded = provider.base64UrlEncode(original);
    auto decodedBytes = provider.base64UrlDecode(encoded);
    std::string decoded(decodedBytes.begin(), decodedBytes.end());
    EXPECT_EQ(decoded, original);
}

TEST(FakeCryptoProviderTest, HmacSha256IsRealAndDeterministic)
{
    FakeCryptoProvider provider;
    auto mac1 = provider.hmacSha256("key", "message");
    auto mac2 = provider.hmacSha256("key", "message");
    ASSERT_EQ(mac1.size(), 32u);
    EXPECT_EQ(mac1, mac2);
}

TEST(FakeCryptoProviderTest, Pbkdf2IsRealAndDeterministic)
{
    FakeCryptoProvider provider;
    auto key1 = provider.pbkdf2HmacSha256("password", "salt", 1000, 32);
    auto key2 = provider.pbkdf2HmacSha256("password", "salt", 1000, 32);
    ASSERT_EQ(key1.size(), 32u);
    EXPECT_EQ(key1, key2);
}

TEST(FakeCryptoProviderTest, RsaSignProducesVerifiableSignature)
{
    FakeCryptoProvider provider;

    EVP_PKEY_CTX *genCtx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    ASSERT_NE(genCtx, nullptr);
    ASSERT_GT(EVP_PKEY_keygen_init(genCtx), 0);
    ASSERT_GT(EVP_PKEY_CTX_set_rsa_keygen_bits(genCtx, 2048), 0);
    EVP_PKEY *pkey = nullptr;
    ASSERT_GT(EVP_PKEY_keygen(genCtx, &pkey), 0);
    EVP_PKEY_CTX_free(genCtx);

    BIO *bio = BIO_new(BIO_s_mem());
    ASSERT_NE(bio, nullptr);
    ASSERT_EQ(1, PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr));
    char *pemData = nullptr;
    long pemLen = BIO_get_mem_data(bio, &pemData);
    std::string privateKeyPem(pemData, static_cast<size_t>(pemLen));
    BIO_free(bio);

    const std::string data = "test-signing-data";
    auto signature = provider.rsaSign(privateKeyPem, "SHA256", data);
    ASSERT_FALSE(signature.empty());

    EVP_MD_CTX *verifyCtx = EVP_MD_CTX_new();
    ASSERT_NE(verifyCtx, nullptr);
    ASSERT_GT(EVP_DigestVerifyInit(verifyCtx, nullptr, EVP_sha256(), nullptr, pkey), 0);
    ASSERT_GT(EVP_DigestVerifyUpdate(verifyCtx, data.data(), data.size()), 0);
    int result = EVP_DigestVerifyFinal(verifyCtx, signature.data(), signature.size());
    EVP_MD_CTX_free(verifyCtx);
    EVP_PKEY_free(pkey);

    EXPECT_EQ(result, 1);
}
