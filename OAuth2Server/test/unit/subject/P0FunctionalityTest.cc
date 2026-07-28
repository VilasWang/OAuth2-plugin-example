#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <authforge/drogon/utils/SubjectGenerator.h>
#include <authforge/storage/memory/MemoryIdentityRepository.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <json/json.h>
#include <chrono>
#include <future>

// Phase 1.5e: subject-mapping tests retargeted from the legacy
// oauth2::MemorySubjectMappingRepository (via MemoryRepositoryBundle's
// subjectMappingRepository()) to the NEW authforge::identity::* backing
// store (MemoryIdentityRepository). The SubjectGenerator unit tests below
// need no storage.

using namespace authforge::common::utils;

// ========== SubjectGenerator Tests ==========

DROGON_TEST(Unit_P0_SubjectGenerator_Legacy_BasicFunctionality)
{
    // Test local user
    CHECK(SubjectGenerator::forLocalUser("alice") == "local:alice");

    // Test google user
    CHECK(SubjectGenerator::forGoogleUser("google123") == "google:google123");

    // Test wechat user
    CHECK(SubjectGenerator::forWeChatUser("openid123") == "wechat:openid123");
}

DROGON_TEST(Unit_P0_SubjectGenerator_Legacy_Parsing)
{
    // Parse google
    auto [p1, s1] = SubjectGenerator::parse("google:abc123");
    CHECK(p1 == "google");
    CHECK(s1 == "abc123");

    // Parse local (implicit)
    auto [p2, s2] = SubjectGenerator::parse("alice");
    CHECK(p2 == "local");
    CHECK(s2 == "alice");

    // Parse local (explicit)
    auto [p3, s3] = SubjectGenerator::parse("local:bob");
    CHECK(p3 == "local");
    CHECK(s3 == "bob");
}

DROGON_TEST(Unit_P0_SubjectMapping_Legacy_ProviderIsolation)
{
    authforge::storage::memory::MemoryIdentityRepository sm;

    std::promise<void> p1;
    sm.createSubjectMapping("alice", 1, "local", [&](bool success) {
        CHECK(success);
        p1.set_value();
    });
    p1.get_future().get();

    std::promise<void> p2;
    sm.getInternalUserId("alice", "local", [&](auto userId) {
        CHECK(userId.has_value());
        CHECK(*userId == 1);
        p2.set_value();
    });
    p2.get_future().get();

    // Different provider same subject name
    std::promise<void> p3;
    sm.createSubjectMapping("alice", 2, "google", [&](bool success) {
        CHECK(success);
        p3.set_value();
    });
    p3.get_future().get();

    std::promise<void> p4;
    sm.getInternalUserId("alice", "google", [&](auto userId) {
        CHECK(userId.has_value());
        CHECK(*userId == 2);
        p4.set_value();
    });
    p4.get_future().get();
}

// ========== PKCE and Security ==========

DROGON_TEST(Unit_P0_PKCE_Legacy_Hashing)
{
    std::string verifier = "testVerifier1234567890123456789012345678901234567890";
    std::string challenge = OAuth2Plugin::generateSha256Hash(verifier);
    CHECK(!challenge.empty());

    CHECK(OAuth2Plugin::validatePkceCodeVerifier(verifier, challenge, "S256"));
    CHECK(!OAuth2Plugin::validatePkceCodeVerifier("wrong", challenge, "S256"));
}

DROGON_TEST(Unit_P0_StateParameter_Legacy_Validation)
{
    std::string state = "secureState123";
    CHECK(state.length() >= 8);
    CHECK(state.find('?') == std::string::npos);
}

// ========== Performance Tests ==========

DROGON_TEST(Performance_P1_Benchmark_P0_Legacy_SubjectGenerationPerformance)
{
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 1000; ++i)
    {
        std::string subject = SubjectGenerator::forLocalUser("user" + std::to_string(i));
        CHECK(subject.substr(0, 6) == "local:");
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    CHECK(duration.count() < 500);
}

// ========== Security Tests ==========

DROGON_TEST(Security_P0_Legacy_InjectionPrevention)
{
    std::string malicious = "local:alice' OR '1'='1";
    CHECK(SubjectGenerator::isValid(malicious));

    auto [p, s] = SubjectGenerator::parse(malicious);
    CHECK(p == "local");
    CHECK(s == "alice' OR '1'='1");
}

// ========== Edge Cases ==========

DROGON_TEST(Unit_P0_EdgeCases_Legacy_EmptyVerifier)
{
    // In many implementations, if challenge method is plain or empty, it might work differently
    // but here we just check our validator
    CHECK(!OAuth2Plugin::validatePkceCodeVerifier("", "challenge", "S256"));
}

DROGON_TEST(Unit_P0_EdgeCases_Legacy_SubjectProviderIsolation)
{
    authforge::storage::memory::MemoryIdentityRepository sm;

    sm.createSubjectMapping("testuser", 1, "local", [&](bool) {});
    sm.createSubjectMapping("testuser", 2, "google", [&](bool) {});

    std::promise<void> p;
    sm.getInternalUserId("testuser", "local", [&](auto id) {
        CHECK(id.has_value());
        CHECK(*id == 1);
        p.set_value();
    });
    p.get_future().get();
}
