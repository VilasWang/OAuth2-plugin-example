#pragma once

// B1 (OIDC Back-Channel Logout 1.0): this port is also consumed by the
// backchannel-logout notifier (libs/drogon adapters) to POST a signed
// logout_token to each relying party; logout is a core feature, so this
// port is no longer WITH_SOCIAL-gated (the social services remain gated).

// M2.5 identity completion, Social auth slice (authforge-sdk-refactor,
// design.md §4.1 rule 1 / §5.1/§6): outbound-HTTP port backing
// GoogleAuthService/WeChatAuthService/GitHubAuthService. Exchanging an
// OAuth2 authorization code with a third-party provider's token endpoint,
// then fetching the user's profile, both require making outbound HTTP
// calls -- but this is a Domain-layer package (must not depend on Drogon
// or call drogon::HttpClient directly, same rule that keeps
// AuthService/MfaService/WebAuthnService free of Drogon types). This
// port abstracts exactly the two HTTP shapes
// libs/drogon/src/controllers/{Google,WeChat,GitHub}Controller.cc's
// login() handlers need:
//
//   1. postForm  -- POST url-encoded form parameters to a URL, used for
//      the token-exchange call (Google/GitHub's
//      "POST .../token"/"POST .../access_token" with client_id/
//      client_secret/code/grant_type as form params).
//   2. getWithBearerToken -- GET a URL, optionally with an
//      "Authorization: Bearer <token>" header, used for the userinfo
//      fetch (Google's "GET .../userinfo" with a Bearer header) and,
//      with an empty bearerToken, for WeChat's token-exchange and
//      userinfo calls -- WeChat's API is GET-with-query-params for both
//      steps (no POST body, no Bearer header; the access_token/openid
//      values travel in the query string instead), so WeChatAuthService
//      builds the full query string into `url` itself and calls this
//      method with bearerToken == "" (meaning: no Authorization header
///     is added). Keeping the port to these two methods (rather than a
//      fully generic "arbitrary method + arbitrary headers" HTTP client)
//      matches design.md's own port-sizing guidance: cover exactly what
//      the three providers need, nothing more.
//
// The real (Adapter-layer) implementation of this port -- backed by
// drogon::HttpClient, same as the existing controllers -- is NOT part of
// this task; only this interface + the services' use of it + a
// hand-written test fake are in scope (design.md §4.1 rule 1: the
// drogon::HttpClient-backed implementation belongs in libs/drogon,
// analogous to how ICryptoProvider's OpenSSL implementation lives in an
// Adapter package, not in libs/common).

#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <json/json.h>

namespace authforge::identity
{

/**
 * @brief Result of a single outbound OAuth2 HTTP call.
 */
struct OAuthHttpResult
{
    // True iff the request was actually sent and a response was
    // received (mirrors the source controllers' `result ==
    // ::drogon::ReqResult::Ok && response` check) -- independent of
    // `statusCode`, so callers can decide for themselves whether a
    // non-200 status should be treated as failure (the source
    // controllers are inconsistent about this: the token-exchange step
    // additionally requires statusCode == 200, the userinfo-fetch step
    // does not check status at all -- see GoogleAuthService.cc's/
    // WeChatAuthService.cc's/GitHubAuthService.cc's own comments for
    // where each nuance is preserved).
    bool transportOk = false;

    int statusCode = 0;

    // Parsed JSON response body. Json::Value() (null) if the body could
    // not be parsed as JSON or was empty.
    Json::Value body;
};

/**
 * @brief Outbound HTTP port for OAuth2 code-exchange + userinfo-fetch
 * calls to a third-party identity provider. See this header's top
 * comment for the Adapter/Domain split rationale.
 */
class IOAuthHttpClient
{
  public:
    virtual ~IOAuthHttpClient() = default;

    using ResultCallback = std::function<void(OAuthHttpResult)>;

    /**
     * @brief POST `params` as an application/x-www-form-urlencoded body
     * to `url`, expecting a JSON(-ish) response.
     */
    virtual void postForm(
      const std::string &url,
      const std::vector<std::pair<std::string, std::string>> &params,
      ResultCallback &&cb
    ) = 0;

    /**
     * @brief GET `url`, expecting a JSON(-ish) response. If
     * `bearerToken` is non-empty, an "Authorization: Bearer
     * <bearerToken>" header is added; if empty, no Authorization header
     * is sent at all (used by providers, like WeChat, whose GET-based
     * calls carry their credentials in the query string instead of a
     * bearer header -- see this header's top comment).
     */
    virtual void getWithBearerToken(
      const std::string &url,
      const std::string &bearerToken,
      ResultCallback &&cb
    ) = 0;
};

}  // namespace authforge::identity
