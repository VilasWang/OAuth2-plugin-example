#pragma once

// M3 Task 20 slice 7 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/GitHubController.h into
// authforge::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

class GitHubController : public ::drogon::HttpController<GitHubController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(
      GitHubController::login, "/api/github/login", ::drogon::Post, ::drogon::Options
    );
    METHOD_LIST_END

    void login(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace authforge::drogon::controllers
