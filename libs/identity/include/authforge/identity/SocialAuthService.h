#pragma once

#ifdef WITH_SOCIAL

// M2.5 identity completion, Social auth slice (authforge-sdk-refactor,
// design.md §5.1/§6): real (non-placeholder) implementation, replacing
// the previous `#ifdef WITH_SOCIAL ... TODO` placeholder. Ports the
// business logic out of
// libs/drogon/src/controllers/{Google,WeChat,GitHub}Controller.cc's
// login() handlers into three framework-independent services, following
// AuthService.h/MfaService.h/WebAuthnService.h's established pattern:
// dependencies (repository/port) are injected through the constructor,
// no Drogon/DB-client/HTTP-client types appear anywhere in these
// classes -- the actual outbound HTTP calls to each provider's token/
// userinfo endpoints go through the injected IOAuthHttpClient port (see
// IOAuthHttpClient.h's header comment for the Adapter/Domain split
// rationale) instead of drogon::HttpClient directly.
//
// Scope per provider, confirmed by reading each source controller in
// full before writing this file:
//   - GoogleController.cc's login() and WeChatController.cc's login()
//     ONLY exchange the code + fetch/filter profile info and hand it
//     back to the caller -- neither one touches the `users` table, the
//     `oauth2_subject_mappings` table, or issues any OAuth2 tokens. So
//     GoogleAuthService/WeChatAuthService mirror that exact narrow
//     scope: login() in, filtered profile struct out, nothing more.
//   - GitHubController.cc's login() is broader: after exchanging the
//     code + fetching the GitHub profile, it also finds-or-creates a
//     local `users` row, links it via `oauth2_subject_mappings`, and
//     assigns the default 'user' role (via the injected
//     ISocialAccountRepository here) -- but it explicitly does NOT stop
//     there: it goes on to mint and persist OAuth2 access_token/
//     refresh_token rows directly (raw INSERT INTO
//     oauth2_access_tokens/oauth2_refresh_tokens). That token-issuance
//     step is an oauth2-domain concern (design.md §4.1 rule 2, identity
//     <-> oauth2 互不依赖) -- identical scope-boundary rationale to
//     MfaService.h's own header comment on why "verify code, then issue
//     OAuth2 tokens" orchestration does not belong in this package.
//     GitHubAuthService::login() therefore stops at "local user
//     identified or created, default role assigned" and returns that
//     outcome to the caller; minting/persisting OAuth2 tokens for the
//     resulting user is future product-level assembly (Task 24), same
//     as MfaService::verifyLoginCode()'s and AuthService::validateUser()'s
//     existing precedent of stopping short of token issuance.

#include <authforge/identity/IOAuthHttpClient.h>
#include <authforge/identity/ISocialAccountRepository.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace authforge::identity
{

// ---------------------------------------------------------------------
// Google
// ---------------------------------------------------------------------

/**
 * @brief Filtered Google userinfo fields -- mirrors GoogleController.cc's
 * own field allow-list (sub/name/email/picture), which is itself a
 * deliberate security-conscious filter over the raw
 * https://www.googleapis.com/oauth2/v3/userinfo response (see that
 * file's "Filter response to only include necessary fields" comment).
 */
struct GoogleUserInfo
{
    std::string sub;
    std::string name;
    std::string email;
    std::string picture;
};

/**
 * @brief Result of GoogleAuthService::login.
 */
struct GoogleLoginResult
{
    // Empty on success. On failure, a structured Error_Code (registered
    // in ErrorCatalog) mirroring GoogleController.cc's existing
    // ErrorResponder calls verbatim: "NET_CONNECTION_FAILED" (token
    // exchange or userinfo fetch could not be contacted / did not return
    // HTTP 200) or "VALIDATION_INVALID_INPUT" (token response missing
    // access_token).
    std::string errorCode;
    GoogleUserInfo profile;
};

/**
 * @brief Google OAuth2 authorization-code exchange + userinfo fetch.
 * Framework-independent -- the actual HTTP calls go through the injected
 * IOAuthHttpClient port.
 */
class GoogleAuthService
{
  public:
    /**
     * @param httpClient Outbound HTTP port (required).
     * @param clientId Google OAuth2 client_id.
     * @param clientSecret Google OAuth2 client_secret.
     * @param redirectUri Google OAuth2 redirect_uri (must match the one
     * registered with Google / used to obtain `code`).
     */
    GoogleAuthService(
      std::shared_ptr<IOAuthHttpClient> httpClient,
      std::string clientId,
      std::string clientSecret,
      std::string redirectUri
    );

    /**
     * @brief Exchange an authorization code for Google userinfo.
     * @param code Authorization code from Google's OAuth2 callback
     * (caller is responsible for extracting it from the request --
     * mirrors GoogleController.cc's own "missing code" check happening
     * before this business logic runs).
     * @param callback Result with the filtered profile on success, or a
     * non-empty errorCode on failure.
     */
    void login(const std::string &code, std::function<void(GoogleLoginResult)> &&callback);

  private:
    std::shared_ptr<IOAuthHttpClient> httpClient_;
    std::string clientId_;
    std::string clientSecret_;
    std::string redirectUri_;
};

// ---------------------------------------------------------------------
// WeChat
// ---------------------------------------------------------------------

/**
 * @brief Filtered WeChat userinfo fields -- mirrors WeChatController.cc's
 * own field allow-list (openid/nickname/headimgurl/sex/city/province/
 * country).
 */
struct WeChatUserInfo
{
    std::string openid;
    std::string nickname;
    std::string headimgurl;
    int sex = 0;
    std::string city;
    std::string province;
    std::string country;
};

/**
 * @brief Result of WeChatAuthService::login.
 */
struct WeChatLoginResult
{
    // Empty on success. On failure, a structured Error_Code mirroring
    // WeChatController.cc's existing ErrorResponder calls verbatim:
    // "NET_CONNECTION_FAILED" (token exchange or userinfo fetch could
    // not be contacted / did not return HTTP 200) or
    // "VALIDATION_INVALID_INPUT" (WeChat returned a non-zero errcode
    // during token exchange).
    std::string errorCode;
    WeChatUserInfo profile;
};

/**
 * @brief WeChat OAuth2 authorization-code exchange + userinfo fetch.
 * Framework-independent -- the actual HTTP calls go through the injected
 * IOAuthHttpClient port. Unlike Google/GitHub, WeChat's API is
 * GET-with-query-params for both the token-exchange and userinfo steps
 * (no POST body, no Bearer header) -- see IOAuthHttpClient.h's header
 * comment for how that maps onto the shared port.
 */
class WeChatAuthService
{
  public:
    /**
     * @param httpClient Outbound HTTP port (required).
     * @param appId WeChat Open Platform appid.
     * @param secret WeChat Open Platform secret.
     */
    WeChatAuthService(
      std::shared_ptr<IOAuthHttpClient> httpClient,
      std::string appId,
      std::string secret
    );

    /**
     * @brief Exchange an authorization code for WeChat userinfo.
     * @param code Authorization code from WeChat's OAuth2 callback.
     * @param callback Result with the filtered profile on success, or a
     * non-empty errorCode on failure.
     */
    void login(const std::string &code, std::function<void(WeChatLoginResult)> &&callback);

  private:
    std::shared_ptr<IOAuthHttpClient> httpClient_;
    std::string appId_;
    std::string secret_;
};

// ---------------------------------------------------------------------
// GitHub
// ---------------------------------------------------------------------

/**
 * @brief Result of GitHubAuthService::login.
 */
struct GitHubLoginResult
{
    // Empty on success. On failure, a structured Error_Code mirroring
    // GitHubController.cc's existing ErrorResponder calls verbatim:
    // "NET_CONNECTION_FAILED" (token exchange or userinfo fetch could
    // not be contacted / did not return HTTP 200), "VALIDATION_INVALID_INPUT"
    // (token response missing access_token, or GitHub returned no user
    // login), or "DB_QUERY_ERROR"/"INTERNAL_ERROR" (repository failure
    // during account lookup/creation -- collapsed from the controller's
    // several distinct DrogonDbException call sites into the single
    // repository-level failure signal ISocialAccountRepository exposes).
    std::string errorCode;

    int32_t userId = 0;      // Internal user id (existing or newly created).
    std::string username;    // Local username (existing, or "gh_" + github login for new accounts).
    bool isNewUser = false;  // True iff a new local account was created by this call.
};

/**
 * @brief GitHub OAuth2 authorization-code exchange + userinfo fetch +
 * local-account find-or-create/link. Framework-independent -- the actual
 * HTTP calls go through the injected IOAuthHttpClient port, and the
 * account-linking persistence goes through the injected
 * ISocialAccountRepository. Does NOT issue OAuth2 tokens -- see this
 * header's top comment for the scope-boundary rationale.
 */
class GitHubAuthService
{
  public:
    /**
     * @param httpClient Outbound HTTP port (required).
     * @param accountRepo Local-account find-or-create/link persistence
     * (required).
     * @param clientId GitHub OAuth App client_id.
     * @param clientSecret GitHub OAuth App client_secret.
     */
    GitHubAuthService(
      std::shared_ptr<IOAuthHttpClient> httpClient,
      std::shared_ptr<ISocialAccountRepository> accountRepo,
      std::string clientId,
      std::string clientSecret
    );

    /**
     * @brief Exchange an authorization code for a GitHub profile, then
     * find or create the linked local account.
     * @param code Authorization code from GitHub's OAuth2 callback.
     * @param callback Result with the local user id/username on
     * success, or a non-empty errorCode on failure.
     */
    void login(const std::string &code, std::function<void(GitHubLoginResult)> &&callback);

  private:
    std::shared_ptr<IOAuthHttpClient> httpClient_;
    std::shared_ptr<ISocialAccountRepository> accountRepo_;
    std::string clientId_;
    std::string clientSecret_;
};

}  // namespace authforge::identity

#endif  // WITH_SOCIAL
