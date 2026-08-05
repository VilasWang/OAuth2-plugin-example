#pragma once

// M3 Task 20 slice 7 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/GitHubController.h into
// authforge::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>
#include <functional>
#include <memory>
#include <string>

// M3 Task 23 (authforge-sdk-refactor, evaluation H4): see
// HealthController.h's identical comment for the rationale.
class OAuth2Plugin;

#ifdef WITH_SOCIAL
// Task 24 slice 5 (authforge-sdk-refactor): see SessionController.h's
// identical forward-declaration comment.
namespace authforge::identity
{
class GitHubAuthService;
}  // namespace authforge::identity
#endif  // WITH_SOCIAL

namespace authforge::drogon::controllers
{

class GitHubController : public ::drogon::HttpController<GitHubController, false>
{
  public:
    // M3 Task 23: see HealthController::setPlugin()'s comment.
    void setPlugin(OAuth2Plugin *plugin)
    {
        plugin_ = plugin;
    }

#ifdef WITH_SOCIAL
    // Task 24 slice 5: identity-layer service injection, same pattern as
    // GoogleController::setGoogleAuthService(). Falls back to the
    // pre-Task-24 raw-SQL + drogon::HttpClient path when unset --
    // GitHubAuthService::login() stops at "local user identified or
    // created" (see SocialAuthService.h's own scope-boundary comment);
    // this controller still owns minting/persisting the OAuth2 tokens
    // for the resulting user either way.
    void setGitHubAuthService(authforge::identity::GitHubAuthService *service)
    {
        gitHubAuthService_ = service;
    }
#endif  // WITH_SOCIAL

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(GitHubController::login, "/api/github/login", ::drogon::Post, ::drogon::Options);
    METHOD_LIST_END

    void login(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

  private:
    // Intentional [this] capture: like every Drogon HttpController in this
    // codebase (see TokenEndpointController.cc:1306-1311 for the canonical
    // rationale), GitHubController is a process-wide singleton managed by
    // Drogon via raw pointer; it lives for the whole process and is NOT a
    // shared_ptr, so shared_from_this() is unavailable and capturing `this`
    // in async callbacks is safe. Every step helper below therefore captures
    // `this` (plus req/callbackPtr) rather than a self shared_ptr.

    // Shared ownership wrapper around the drogon response callback, so that
    // the innermost callback of an async chain can still invoke the original
    // response callback regardless of nesting depth (TECH_SPECS.md §一
    // "Callback 生命周期" convention). Declared on the controller so every
    // step helper below shares the same type without re-spelling it.
    using CallbackPtr = std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>>;

    OAuth2Plugin *plugin_ = nullptr;
    OAuth2Plugin *resolvePlugin() const;
#ifdef WITH_SOCIAL
    authforge::identity::GitHubAuthService *gitHubAuthService_ = nullptr;
#endif  // WITH_SOCIAL

    // ---- login() step helpers ------------------------------------------------
    // The pre-refactor login() body was a single ~560-line method with up to
    // 7 nested async callbacks (callback hell). Each step below is the body
    // of one of those callbacks, lifted into a named member so the flow reads
    // top-to-bottom and shared logic (token issuance, error responses) is no
    // longer copy-pasted across two divergent paths. Behaviour is identical
    // to the pre-refactor implementation; only structure changed.

    // Mint access + refresh tokens for @p userId and persist them, then emit
    // the JSON token response via @p callbackPtr. Shared by both the
    // WITH_SOCIAL path (GitHubAuthService yields a userId) and the fallback
    // path (raw HttpClient + DB find-or-create). Collapses the two near-
    // verbatim `issueTokens` lambdas that previously lived inline.
    void issueTokensForUser(
      const ::drogon::HttpRequestPtr &req,
      const CallbackPtr &callbackPtr,
      int64_t userId
    );

    // Persist an access token; on success persist the matching refresh token,
    // then emit the token response. (Inner steps of issueTokensForUser.)
    void persistAccessToken(
      const ::drogon::HttpRequestPtr &req,
      const CallbackPtr &callbackPtr,
      std::string accessToken,
      std::string refreshToken,
      int64_t userId
    );
    void persistRefreshToken(
      const ::drogon::HttpRequestPtr &req,
      const CallbackPtr &callbackPtr,
      std::string accessToken,
      std::string refreshToken,
      int64_t userId
    );

    // ---- fallback (pre-Task-24) path step helpers ---------------------------
    // Step 1: exchange the GitHub authorization code for an access token at
    // https://github.com/login/oauth/access_token, then proceed to
    // fetchGitHubUserInfo().
    void exchangeCodeForToken(
      const ::drogon::HttpRequestPtr &req,
      const CallbackPtr &callbackPtr,
      const std::string &clientId,
      const std::string &clientSecret,
      const std::string &code
    );
    // Step 2: fetch /user from the GitHub API with the obtained access token,
    // then proceed to resolveSubjectMapping().
    void fetchGitHubUserInfo(
      const ::drogon::HttpRequestPtr &req,
      const CallbackPtr &callbackPtr,
      const std::string &accessToken
    );
    // Step 3: look up the (provider, subject) mapping; branch to
    // linkExistingUser() for a known account or createNewLinkedUser() for a
    // first-time GitHub login.
    void resolveSubjectMapping(
      const ::drogon::HttpRequestPtr &req,
      const CallbackPtr &callbackPtr,
      const std::string &githubLogin,
      const std::string &githubEmail,
      int64_t githubId
    );
    // Step 4a: existing mapping -- look up the username, then issue tokens.
    void linkExistingUser(
      const ::drogon::HttpRequestPtr &req,
      const CallbackPtr &callbackPtr,
      int32_t userId
    );
    // Step 4b: no mapping -- create the local user, subject mapping and
    // default role, then issue tokens.
    void createNewLinkedUser(
      const ::drogon::HttpRequestPtr &req,
      const CallbackPtr &callbackPtr,
      const std::string &githubLogin,
      const std::string &githubEmail,
      const std::string &provider,
      const std::string &subject
    );
};

}  // namespace authforge::drogon::controllers
