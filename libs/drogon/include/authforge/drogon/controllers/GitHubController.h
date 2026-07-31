#pragma once

// M3 Task 20 slice 7 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/GitHubController.h into
// authforge::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>

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
    OAuth2Plugin *plugin_ = nullptr;
    OAuth2Plugin *resolvePlugin() const;
#ifdef WITH_SOCIAL
    authforge::identity::GitHubAuthService *gitHubAuthService_ = nullptr;
#endif  // WITH_SOCIAL
};

}  // namespace authforge::drogon::controllers
