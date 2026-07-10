#pragma once

#ifdef WITH_SOCIAL

// Task 24 slice 5 (authforge-sdk-refactor, design.md §4.1 rule 3):
// Adapter-layer implementation of authforge::identity::IOAuthHttpClient,
// backed by drogon::HttpClient. Lives in libs/drogon (not libs/identity)
// because it depends on Drogon, same placement rationale as
// libs/drogon/src/AuthService.cc's own header comment.

#include <authforge/identity/IOAuthHttpClient.h>

namespace authforge::drogon::adapters
{

class DrogonOAuthHttpClient : public authforge::identity::IOAuthHttpClient
{
  public:
    void postForm(
      const std::string &url,
      const std::vector<std::pair<std::string, std::string>> &params,
      ResultCallback &&cb
    ) override;

    void getWithBearerToken(
      const std::string &url,
      const std::string &bearerToken,
      ResultCallback &&cb
    ) override;
};

}  // namespace authforge::drogon::adapters

#endif  // WITH_SOCIAL
