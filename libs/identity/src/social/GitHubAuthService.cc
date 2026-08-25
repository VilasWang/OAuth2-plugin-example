// GitHub OAuth2 authentication service -- real implementation.
// Ported from libs/drogon/src/controllers/GitHubController.cc's login(),
// stopping short of OAuth2 token issuance -- see SocialAuthService.h's
// header comment for the scope-boundary rationale.

#ifdef WITH_SOCIAL

#include <fulla/identity/SocialAuthService.h>

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
    auto sharedCb = std::make_shared<std::function<void(GitHubLoginResult)>>(std::move(callback));

    // Steps 1-2 (code exchange + userinfo fetch) are shared with
    // fetchProfile -- one implementation of the provider protocol, two
    // consumers (login = fetchProfile + find-or-create; link =
    // fetchProfile only).
    fetchProfile(code, [accountRepo, sharedCb](GitHubProfileResult profile) {
        if (!profile.errorCode.empty())
        {
            GitHubLoginResult result;
            result.errorCode = profile.errorCode;
            (*sharedCb)(std::move(result));
            return;
        }

        // Step 3: Find or create local user linked to this GitHub
        // account.
        std::string provider = "github";
        std::string subject = std::to_string(profile.githubId);

        accountRepo->findLinkedUser(
          provider,
          subject,
          [accountRepo, sharedCb, provider, subject, profile](
            SocialLinkStatus status, const SocialAccountLookup &existing
          ) {
              switch (status)
              {
              case SocialLinkStatus::Linked:
              {
                  GitHubLoginResult result;
                  result.userId = existing.userId;
                  result.username = existing.username;
                  result.isNewUser = false;
                  (*sharedCb)(std::move(result));
                  return;
              }
              case SocialLinkStatus::AccountUnavailable:
              {
                  // #54 (V024 contract): the linked local user is
                  // soft-deleted or locked — reject with the same
                  // generic auth error the password path uses, so the
                  // response leaks no account-status information.
                  GitHubLoginResult result;
                  result.errorCode = "AUTH_INVALID_CREDENTIALS";
                  (*sharedCb)(std::move(result));
                  return;
              }
              case SocialLinkStatus::RepositoryError:
              {
                  // DB failure must NOT fall through to account
                  // creation (the old optional<nullopt> contract
                  // conflated this with "no mapping yet").
                  GitHubLoginResult result;
                  result.errorCode = "DB_QUERY_ERROR";
                  (*sharedCb)(std::move(result));
                  return;
              }
              case SocialLinkStatus::NoMapping:
                  break;
              }

              // New GitHub user -- create local account + link +
              // default role (repository-owned, mirrors
              // GitHubController.cc's INSERT users / INSERT
              // oauth2_subject_mappings / INSERT user_roles
              // sequence).
              std::string username = "gh_" + profile.login;
              accountRepo->createLinkedUser(
                provider,
                subject,
                username,
                profile.email,
                [sharedCb](std::optional<LinkNewSocialAccountResult> created) {
                    if (!created)
                    {
                        GitHubLoginResult result;
                        result.errorCode = "DB_QUERY_ERROR";
                        (*sharedCb)(std::move(result));
                        return;
                    }

                    GitHubLoginResult result;
                    result.userId = created->userId;
                    result.username = created->username;
                    result.isNewUser = true;
                    (*sharedCb)(std::move(result));
                }
              );
          }
        );
    });
}

}  // namespace fulla::identity

#endif  // WITH_SOCIAL
