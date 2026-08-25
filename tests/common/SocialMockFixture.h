// tests/common/SocialMockFixture.h
//
// Test fixture for HTTP integration tests that exercise the Social OAuth
// controllers (GitHub/Google/WeChat) WITHOUT real outbound network. The
// controllers are HttpController<T,false> (AutoCreation=false) process
// singletons with raw-pointer setXxxAuthService(...) setters. The setters are
// the only mock seam: a test constructs the REAL XxxAuthService backed by the
// shared FakeOAuthHttpClient (and FakeSocialAccountRepository for GitHub),
// then installs it on the controller singleton via
// drogon::DrClassMap::getSingleInstance<XxxController>()->setXxxService(...).
// The controller checks `if (service_)` first (GitHubController.cc:180,
// GoogleController.cc:168, WeChatController.cc:162), so the fake wins over
// the legacy inline-drogon::HttpClient fallback path.
//
// Wiring timing (verified): test_main.cc:374-391 runs bootstrap::wireIdentityServices()
// inside registerBeginningAdvice on the server thread BEFORE test::run() runs
// test cases on the main thread. In Postgres mode wireIdentityServices wires
// the REAL services (real DrogonOAuthHttpClient → real network); our setter,
// called later from a test case, overwrites them. In MEMORY mode
// wireIdentityServices early-returns (IdentityAssembly.cc:74-80) leaving the
// service pointers null → our fake is the ONLY wiring, so these tests run in
// every CI leg (Windows memory-mode included).
//
// Lifetime safety (review I-3): the fake services are stored in function-local
// `static shared_ptr`s -- process lifetime, never destroyed -- so the raw
// pointer handed to the controller stays valid for the whole test run. This
// avoids the use-after-free hazard that a per-case stack-local fake would
// create (DROGON_TEST case ordering is DrClassMap registration order, not
// source order, so "restore on scope exit" would be fragile). A test that
// wants a FRESH fake (clean response queues) calls the inject helper again,
// which constructs a new process-lifetime service.
//
// Thread-safety note (review I-2): the setter writes a non-atomic raw pointer
// from the test (main) thread while the server (loop) thread may read it. In
// practice this is benign-by-timing (the store happens-before the request),
// but it WILL flag under ThreadSanitizer. These mock-injection tests are
// therefore not suitable for -FULLA_SANITIZER=thread runs; run them under the
// default or ASan legs.

#pragma once

#include <fulla/drogon/controllers/GitHubController.h>
#include <fulla/drogon/controllers/GoogleController.h>
#include <fulla/drogon/controllers/WeChatController.h>
#include <fulla/drogon/controllers/UserSelfServiceController.h>
#include <fulla/identity/SocialAuthService.h>
#include <fulla/identity/SocialLinkService.h>
#include <fulla/identity/testing/FakeOAuthHttpClient.h>
#include <fulla/identity/testing/FakeSocialAccountRepository.h>

#include <drogon/drogon.h>  // DrClassMap

#include <memory>
#include <string>
#include <vector>

namespace fulla::test::social
{

using fulla::identity::testing::FakeOAuthHttpClient;
using fulla::identity::testing::FakeSocialAccountRepository;

// ---------------------------------------------------------------------------
// Per-provider injection helpers. Each returns a shared_ptr to the FakeOAuthHttpClient
// the installed service is bound to, so the caller can seed response queues and
// inspect recorded calls. The installed service itself lives for the process
// (static shared_ptr) -- see the header comment on why process-lifetime storage
// is required.
// ---------------------------------------------------------------------------

// Install a GoogleAuthService backed by `http` onto the GoogleController
// singleton. Credentials are arbitrary (the fake does not validate them); the
// test seeds the fake's response queues.
//
// Implementation note: the service is rebuilt fresh on every call (so each
// test case gets a clean FakeOAuthHttpClient with its own response queues),
// and stored in a process-lifetime `static vector<shared_ptr<...>>` so the
// raw pointer handed to the controller stays valid for the whole test run
// (DROGON_TEST case order is not source order; a stack-local or single
// `static auto svc` would either dangle or reuse a stale bound http object
// across cases -- both cause use-after-free / wrong-fake bugs).
inline std::shared_ptr<FakeOAuthHttpClient> injectGoogleFake()
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    auto svc = std::make_shared<fulla::identity::GoogleAuthService>(
      http, "test-client-id", "test-client-secret", "https://example.test/cb");
    static std::vector<std::shared_ptr<fulla::identity::GoogleAuthService>> keepAlive;
    keepAlive.push_back(svc);
    auto ctrl = ::drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::GoogleController>();
    if (ctrl)
        ctrl->setGoogleAuthService(svc.get());
    return http;
}

// Install a WeChatAuthService backed by `http` onto the WeChatController singleton.
// Same process-lifetime keepAlive pattern as injectGoogleFake (see comment there).
inline std::shared_ptr<FakeOAuthHttpClient> injectWeChatFake()
{
    auto http = std::make_shared<FakeOAuthHttpClient>();
    auto svc = std::make_shared<fulla::identity::WeChatAuthService>(
      http, "test-appid", "test-secret");
    static std::vector<std::shared_ptr<fulla::identity::WeChatAuthService>> keepAlive;
    keepAlive.push_back(svc);
    auto ctrl = ::drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::WeChatController>();
    if (ctrl)
        ctrl->setWeChatAuthService(svc.get());
    return http;
}

// Install a GitHubAuthService backed by `http` + `accountRepo` onto the
// GitHubController singleton. Returns the account-repo shared_ptr so the caller
// can pre-seed linked accounts or set failCreate. The http shared_ptr is
// accessible via the returned repo's... no -- return both via out-params.
//
// NOTE (supersedes the old "review B1" warning): issueTokensForUser USED to
// call drogon::app().getDbClient() directly, which under memory storage hit
// an uncatchable assert() -- so GitHub fakes had to return errors only.
// Token issuance was since re-routed through OAuth2Plugin::saveTokenPair
// (the storage abstraction; MemoryTokenRepository in memory mode), so the
// GitHub HAPPY PATH is now safe and testable in every storage mode -- see
// Integration_P0_GitHubLogin_FakeExchange_ReturnsTokens. Queueing successful
// token+userinfo exchanges is allowed; only the legacy fallback path still
// touches the DB directly.
struct GitHubFakeHandle
{
    std::shared_ptr<FakeOAuthHttpClient> http;
    std::shared_ptr<FakeSocialAccountRepository> accountRepo;
};

inline GitHubFakeHandle injectGitHubFake()
{
    GitHubFakeHandle h;
    h.http = std::make_shared<FakeOAuthHttpClient>();
    h.accountRepo = std::make_shared<FakeSocialAccountRepository>();
    auto svc = std::make_shared<fulla::identity::GitHubAuthService>(
      h.http, h.accountRepo, "test-client-id", "test-client-secret");
    static std::vector<std::shared_ptr<fulla::identity::GitHubAuthService>> keepAlive;
    keepAlive.push_back(svc);
    auto ctrl = ::drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::GitHubController>();
    if (ctrl)
        ctrl->setGitHubAuthService(svc.get());
    return h;
}

// ---------------------------------------------------------------------------
// B2 social link/unlink: install a SocialLinkService (all three provider
// services sharing one FakeOAuthHttpClient + one FakeSocialAccountRepository)
// onto the UserSelfServiceController singleton. The controller resolves the
// acting user via Postgres (public_sub/numeric dispatch), then the service
// runs entirely against the fakes -- so a PG-backed test can drive the full
// link/list/unlink lifecycle without any provider network or mapping-row DB
// writes. Same process-lifetime keepAlive contract as the helpers above.
// ---------------------------------------------------------------------------

struct SocialLinkFakeHandle
{
    std::shared_ptr<FakeOAuthHttpClient> http;
    std::shared_ptr<FakeSocialAccountRepository> accountRepo;
};

inline SocialLinkFakeHandle injectSocialLinkFake()
{
    SocialLinkFakeHandle h;
    h.http = std::make_shared<FakeOAuthHttpClient>();
    h.accountRepo = std::make_shared<FakeSocialAccountRepository>();
    auto github = std::make_shared<fulla::identity::GitHubAuthService>(
      h.http, h.accountRepo, "test-client-id", "test-client-secret");
    auto google = std::make_shared<fulla::identity::GoogleAuthService>(
      h.http, "test-client-id", "test-client-secret", "https://example.test/cb");
    auto wechat =
      std::make_shared<fulla::identity::WeChatAuthService>(h.http, "test-appid", "test-secret");
    auto svc = std::make_shared<fulla::identity::SocialLinkService>(
      github, google, wechat, h.accountRepo);
    // Keep every dependency alive for the process -- the service holds them,
    // and the controller holds only a raw pointer to the service.
    static std::vector<std::shared_ptr<void>> keepAlive;
    keepAlive.push_back(github);
    keepAlive.push_back(google);
    keepAlive.push_back(wechat);
    keepAlive.push_back(svc);
    auto ctrl =
      ::drogon::DrClassMap::getSingleInstance<fulla::drogon::controllers::UserSelfServiceController>(
      );
    if (ctrl)
        ctrl->setSocialLinkService(svc.get());
    return h;
}

}  // namespace fulla::test::social
