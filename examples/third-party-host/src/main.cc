// examples/third-party-host (B8b / Task 28b, authforge-sdk-refactor design.md
// §1.1): a minimal standalone SDK consumer. It proves the authforge oauth2
// protocol engine is independently consumable -- assembled here with only the
// SDK packages (authforge-oauth2 + common + common::testing + storage-memory),
// WITHOUT the product's OAuth2Plugin / libs/drogon / libs/storage-postgres --
// and that it runs the authorization-code flow's core steps end-to-end.
//
// This is a plain executable, not an HTTP server (that heavier validation is
// deferred). Exit code: 0 = all checks passed, 1 = any check failed.
//
// What it exercises:
//   1. evaluateScopes() on the engine's AuthorizationService (scope decision).
//   2. generateAuthorizationCode() on TokenService (auth-code issuance).
//   3. exchangeCodeForToken() on TokenService (code -> access/refresh token).
// The memory repositories + FakeCryptoProvider back the engine with zero
// external deps (no DB, no Redis). FakeCryptoProvider uses real OpenSSL for
// hashing/HMAC/base64url (only secureRandomBytes is determinized) -- so the
// tokens/PKCE are real-crypto correct, just reproducible.

#include <authforge/common/testing/FakeCryptoProvider.h>
#include <authforge/oauth2/access/ScopeDecisionEngine.h>
#include <authforge/oauth2/protocol/AuthorizationService.h>
#include <authforge/oauth2/protocol/TokenService.h>
#include <authforge/storage/memory/MemoryClientRepository.h>
#include <authforge/storage/memory/MemoryConsentRepository.h>
#include <authforge/storage/memory/MemoryGrantRepository.h>
#include <authforge/storage/memory/MemoryTokenRepository.h>

#include <json/json.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <string>

namespace
{

// Fail-loud assertion: prints to stderr and returns 1 from main on failure.
// (No gtest here -- examples stay framework-light, per the plan.)
#define CHECK(cond, msg)                                          \
    do                                                            \
    {                                                             \
        if (!(cond))                                              \
        {                                                         \
            std::fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__,  \
                         __LINE__, msg);                          \
            return 1;                                             \
        }                                                         \
    } while (0)

// Build the minimal clients-config JSON MemoryClientRepository::initFromConfig
// expects: the object's MEMBER NAMES are client ids; each value has fields
// `type` (PUBLIC/CONFIDENTIAL), `redirect_uri`, `allowed_scopes`. (Mirrors the
// parser in MemoryClientRepository.cc -- note field is `type`, not
// `client_type`, and there is no `clients` wrapper.)
Json::Value makeClientsConfig()
{
    Json::Value root;
    Json::Value c;
    c["type"] = "PUBLIC";
    c["redirect_uri"] = "https://app.example/callback";
    Json::Value scopes(Json::arrayValue);
    scopes.append("openid");
    c["allowed_scopes"] = scopes;
    root["test-client"] = c;
    return root;
}

}  // namespace

int main()
{
    std::fprintf(stderr, "[probe] start\n");
    std::fflush(stderr);
    // ---- Assemble the SDK engine (no OAuth2Plugin, no Drogon) ----
    auto crypto = std::make_shared<authforge::common::testing::FakeCryptoProvider>();
    std::fprintf(stderr, "[probe] crypto ok\n");
    std::fflush(stderr);

    auto clientRepo = std::make_shared<authforge::storage::memory::MemoryClientRepository>();
    std::fprintf(stderr, "[probe] clientRepo ctor ok\n");
    std::fflush(stderr);
    clientRepo->initFromConfig(makeClientsConfig());
    std::fprintf(stderr, "[probe] initFromConfig ok\n");
    std::fflush(stderr);
    auto grantRepo = std::make_shared<authforge::storage::memory::MemoryGrantRepository>();
    auto tokenRepo = std::make_shared<authforge::storage::memory::MemoryTokenRepository>();
    auto consentRepo = std::make_shared<authforge::storage::memory::MemoryConsentRepository>();
    std::fprintf(stderr, "[probe] all repos ok\n");
    std::fflush(stderr);

    // Must be std::make_shared: TokenService inherits from enable_shared_from_this
    // and its async methods (exchangeCodeForToken, refreshAccessToken) call
    // shared_from_this() for lifetime safety. Stack allocation would cause
    // std::bad_weak_ptr → terminate.
    authforge::oauth2::protocol::AuthorizationService authService(
      clientRepo, consentRepo, /*subjectResolver=*/nullptr, /*roleProvider=*/nullptr);
    auto tokenService = std::make_shared<authforge::oauth2::protocol::TokenService>(
      clientRepo, grantRepo, tokenRepo, crypto);
    std::fprintf(stderr, "[probe] services constructed ok\n");
    std::fflush(stderr);

    authforge::oauth2::access::ScopeValidationSummary summary;
    std::fprintf(stderr, "[probe] calling evaluateScopes\n");
    std::fflush(stderr);
    authService.evaluateScopes(
      "test-client", "local:alice", {"openid"},
      [&](authforge::oauth2::access::ScopeValidationSummary s) { summary = std::move(s); });
    std::fprintf(stderr, "[probe] evaluateScopes returned\n");
    std::fflush(stderr);
    std::fprintf(stderr, "[probe] hasErrors=%d needsConsent=%d\n",
                 summary.hasErrors(), summary.needsConsent());
    std::fflush(stderr);
    CHECK(!summary.hasErrors(), "evaluateScopes: openid should not be an error");
    std::fprintf(stderr, "[probe] CHECK1 passed\n");
    std::fflush(stderr);
    CHECK(summary.needsConsent(), "evaluateScopes: openid should require consent");
    std::fprintf(stderr, "[probe] CHECK2 passed\n");
    std::fflush(stderr);

    // Unknown client -> all invalid (engine distinguishes client_not_found).
    authforge::oauth2::access::ScopeValidationSummary unknownSummary;
    std::fprintf(stderr, "[probe] calling evaluateScopes(unknown)\n");
    std::fflush(stderr);
    authService.evaluateScopes(
      "no-such-client", "local:alice", {"openid"},
      [&](authforge::oauth2::access::ScopeValidationSummary s) {
          unknownSummary = std::move(s);
      });
    std::fprintf(stderr, "[probe] evaluateScopes(unknown) returned\n");
    std::fflush(stderr);

    // ---- Step 2: generateAuthorizationCode (no PKCE -> empty challenge/verifier)
    bool genOk = false;
    std::string code;
    std::string genErr;
    std::fprintf(stderr, "[probe] calling generateAuthorizationCode\n");
    std::fflush(stderr);
    tokenService->generateAuthorizationCode(
      "test-client", "local:alice", "openid", "https://app.example/callback",
      /*codeChallenge=*/"", /*codeChallengeMethod=*/"", /*nonce=*/"",
      [&](bool ok, const std::string &c, const std::string &e) {
          genOk = ok;
          code = c;
          genErr = e;
      });
    CHECK(genOk, "generateAuthorizationCode failed (see genErr)");
    CHECK(!code.empty(), "generateAuthorizationCode: code must be non-empty");

    // ---- Step 3: exchangeCodeForToken (code -> access/refresh token JSON)
    Json::Value tokenJson;
    tokenService->exchangeCodeForToken(
      code, "test-client", /*clientSecret=*/"", "https://app.example/callback",
      /*codeVerifier=*/"", [&](const Json::Value &v) { tokenJson = v; });
    CHECK(!tokenJson.isNull(), "exchangeCodeForToken: response must be non-null");
    CHECK(tokenJson.isMember("access_token") && !tokenJson["access_token"].asString().empty(),
          "exchangeCodeForToken: access_token must be present and non-empty");

    std::printf("[+] third-party-host SDK smoke PASSED: engine assembled via SDK packages, "
                "auth-code flow core steps (evaluateScopes + generateCode + exchangeCode) ran.\n");
    return 0;
}
