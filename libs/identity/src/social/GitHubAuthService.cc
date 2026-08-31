// GitHub OAuth2 authentication service -- real implementation.
// Ported from libs/drogon/src/controllers/GitHubController.cc's login(),
// stopping short of OAuth2 token issuance -- see SocialAuthService.h's
// header comment for the scope-boundary rationale.

#ifdef WITH_SOCIAL

#include <fulla/identity/SocialAuthService.h>

#include "AccountLinkFlow.h"

namespace fulla::identity
{

GitHubAuthService::GitHubAuthService(
  std::shared_ptr<IOAuthHttpClient> httpClient,
  std::shared_ptr<ISocialAccountRepository> accountRepo,
  std::string clientId,
  std::string clientSecret
)
    : httpClient_(std::move(httpClient)),
      accountRepo_(std::move(accountRepo)),
      clientId_(std::move(clientId)),
      clientSecret_(std::move(clientSecret))
{
}

void GitHubAuthService::fetchProfile(
  const std::string &code,
  std::function<void(GitHubProfileResult)> &&callback
)
{
    if (!httpClient_)
    {
        GitHubProfileResult result;
        result.errorCode = "INTERNAL_ERROR";
        callback(std::move(result));
        return;
    }

    auto httpClient = httpClient_;
    auto sharedCb = std::make_shared<std::function<void(GitHubProfileResult)>>(std::move(callback));

    // Step 1: Exchange code for access token --
    // POST https://github.com/login/oauth/access_token
    std::vector<std::pair<std::string, std::string>> params =
      {{"client_id", clientId_}, {"client_secret", clientSecret_}, {"code", code}};

    httpClient_->postForm(
      "https://github.com/login/oauth/access_token",
      params,
      [httpClient, sharedCb](OAuthHttpResult tokenResp) {
          if (!tokenResp.transportOk || tokenResp.statusCode != 200)
          {
              GitHubProfileResult result;
              result.errorCode = "NET_CONNECTION_FAILED";
              (*sharedCb)(std::move(result));
              return;
          }

          if (!tokenResp.body.isMember("access_token"))
          {
              GitHubProfileResult result;
              result.errorCode = "VALIDATION_INVALID_INPUT";
              (*sharedCb)(std::move(result));
              return;
          }

          std::string accessToken = tokenResp.body["access_token"].asString();

          // Step 2: Fetch user info -- GET https://api.github.com/user
          httpClient->getWithBearerToken(
            "https://api.github.com/user",
            accessToken,
            [sharedCb](OAuthHttpResult userResp) {
                if (!userResp.transportOk || userResp.statusCode != 200)
                {
                    GitHubProfileResult result;
                    result.errorCode = "NET_CONNECTION_FAILED";
                    (*sharedCb)(std::move(result));
                    return;
                }

                std::string githubLogin = userResp.body.get("login", "").asString();
                std::string githubEmail = userResp.body.get("email", "").asString();
                int64_t githubId = userResp.body.get("id", 0).asInt64();

                if (githubLogin.empty() || githubId <= 0)
                {
                    GitHubProfileResult result;
                    result.errorCode = "VALIDATION_INVALID_INPUT";
                    (*sharedCb)(std::move(result));
                    return;
                }

                GitHubProfileResult result;
                result.githubId = githubId;
                result.login = githubLogin;
                result.email = githubEmail;
                (*sharedCb)(std::move(result));
            }
          );
      }
    );
}

void GitHubAuthService::login(
  const std::string &code,
  std::function<void(GitHubLoginResult)> &&callback
)
{
    if (!httpClient_ || !accountRepo_)
    {
        GitHubLoginResult result;
        result.errorCode = "INTERNAL_ERROR";
        callback(std::move(result));
        return;
    }

    auto accountRepo = accountRepo_;
    const bool autoCreate = autoCreate_;
    auto sharedCb = std::make_shared<std::function<void(GitHubLoginResult)>>(std::move(callback));

    // Steps 1-2 (code exchange + userinfo fetch) are shared with
    // fetchProfile -- one implementation of the provider protocol, two
    // consumers (login = fetchProfile + find-or-create; link =
    // fetchProfile only).
    fetchProfile(code, [accountRepo, autoCreate, sharedCb](GitHubProfileResult profile) {
        if (!profile.errorCode.empty())
        {
            GitHubLoginResult result;
            result.errorCode = profile.errorCode;
            (*sharedCb)(std::move(result));
            return;
        }

        // Step 3: Find or create local user linked to this GitHub account
        // (#70: the four-state flow, the auto-create gate, and the
        // username-collision retry now live in the provider-agnostic
        // AccountLinkFlow helper shared with Google/WeChat).
        social_detail::resolveOrCreateAccount(
          accountRepo,
          autoCreate,
          "github",
          std::to_string(profile.githubId),
          "gh_" + profile.login,
          profile.email,
          [sharedCb](int32_t userId, const std::string &username, bool isNewUser, const std::string &publicSub) {
              GitHubLoginResult result;
              result.userId = userId;
              result.username = username;
              result.isNewUser = isNewUser;
              result.publicSub = publicSub;
              (*sharedCb)(std::move(result));
          },
          [sharedCb](std::string errorCode) {
              GitHubLoginResult result;
              result.errorCode = std::move(errorCode);
              (*sharedCb)(std::move(result));
          }
        );
    });
}

}  // namespace fulla::identity

#endif  // WITH_SOCIAL
