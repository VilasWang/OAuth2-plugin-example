#pragma once

// Task 24 slice 5 (fulla-sdk-refactor, design.md §4.1 rule 3):
// Adapter-layer implementation of fulla::identity::IOAuthHttpClient,
// backed by drogon::HttpClient. Lives in libs/drogon (not libs/identity)
// because it depends on Drogon, same placement rationale as
// libs/drogon/src/AuthService.cc's own header comment.
//
// B1: also backs the OIDC backchannel-logout notifier's outbound POSTs;
// no longer WITH_SOCIAL-gated (the port it implements was ungated).

#include <fulla/identity/IOAuthHttpClient.h>

namespace fulla::drogon::adapters
{

class DrogonOAuthHttpClient : public fulla::identity::IOAuthHttpClient
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

}  // namespace fulla::drogon::adapters
