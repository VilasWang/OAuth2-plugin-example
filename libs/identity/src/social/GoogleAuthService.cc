// Google OAuth2 authentication service -- real implementation.
// Ported from libs/drogon/src/controllers/GoogleController.cc's login().

#ifdef WITH_SOCIAL

#include <fulla/identity/SocialAuthService.h>

#include "AccountLinkFlow.h"

namespace fulla::identity
{

GoogleAuthService::GoogleAuthService(
  std::shared_ptr<IOAuthHttpClient> httpClient,
  std::string clientId,
  std::string clientSecret,
  std::string redirectUri
)
    : httpClient_(std::move(httpClient)),
      clientId_(std::move(clientId)),
      clientSecret_(std::move(clientSecret)),
      redirectUri_(std::move(redirectUri))
{
}

void GoogleAuthService::login(
  const std::string &code,
  std::function<void(GoogleLoginResult)> &&callback
)
{
    if (!httpClient_)
    {
        GoogleLoginResult result;
        result.errorCode = "NET_CONNECTION_FAILED";
        callback(std::move(result));
        return;
    }

    // #111: a service constructed with empty/placeholder credentials (a
    // disabled provider that still got wired, or a direct construction)
    // must fail locally instead of calling the upstream with garbage.
    if (clientId_.empty() || clientSecret_.empty() || clientId_.rfind("YOUR_", 0) == 0 ||
        clientSecret_.rfind("YOUR_", 0) == 0)
    {
        GoogleLoginResult result;
        result.errorCode = "NET_CONNECTION_FAILED";
        callback(std::move(result));
        return;
    }

    auto httpClient = httpClient_;
    auto sharedCb = std::make_shared<std::function<void(GoogleLoginResult)>>(std::move(callback));

    // 1. Exchange code for access token -- POST https://oauth2.googleapis.com/token
    std::vector<std::pair<std::string, std::string>> params =
      {{"code", code},
       {"client_id", clientId_},
       {"client_secret", clientSecret_},
       {"redirect_uri", redirectUri_},
       {"grant_type", "authorization_code"}};

    // #70: after the profile fetch, resolve-or-create the local account
    // behind the Google identity (when a repository has been injected —
    // the assembly path always injects one; profile-only otherwise).
    auto accountRepo = accountRepo_;
    const bool autoCreate = autoCreate_;

    httpClient_->postForm(
      "https://oauth2.googleapis.com/token",
      params,
      [httpClient, sharedCb, accountRepo, autoCreate](OAuthHttpResult tokenResp) {
          if (!tokenResp.transportOk || tokenResp.statusCode != 200)
          {
              GoogleLoginResult result;
              result.errorCode = "NET_CONNECTION_FAILED";
              (*sharedCb)(std::move(result));
              return;
          }

          if (!tokenResp.body.isMember("access_token"))
          {
              GoogleLoginResult result;
              result.errorCode = "VALIDATION_INVALID_INPUT";
              (*sharedCb)(std::move(result));
              return;
          }

          std::string accessToken = tokenResp.body["access_token"].asString();

          // 2. Fetch user info -- GET https://www.googleapis.com/oauth2/v3/userinfo
          httpClient->getWithBearerToken(
            "https://www.googleapis.com/oauth2/v3/userinfo",
            accessToken,
            [sharedCb, accountRepo, autoCreate](OAuthHttpResult userResp) {
                if (!userResp.transportOk)
                {
                    GoogleLoginResult result;
                    result.errorCode = "NET_CONNECTION_FAILED";
                    (*sharedCb)(std::move(result));
                    return;
                }

                // Filter response to only include necessary fields
                // (security best practice) -- mirrors GoogleController.cc.
                GoogleLoginResult result;
                result.profile.sub = userResp.body.get("sub", "").asString();
                result.profile.name = userResp.body.get("name", "").asString();
                result.profile.email = userResp.body.get("email", "").asString();
                result.profile.picture = userResp.body.get("picture", "").asString();

                if (!accountRepo)
                {
                    // Degraded (no repository injected): the pre-#70
                    // profile-only shape.
                    (*sharedCb)(std::move(result));
                    return;
                }

                // #70: four-state account resolution (see
                // AccountLinkFlow.h). Subject = Google `sub`; username
                // derivation is prefix-truncated to avoid unbounded
                // provider-controlled strings in the local username.
                const GoogleUserInfo profile = result.profile;
                social_detail::resolveOrCreateAccount(
                  accountRepo,
                  autoCreate,
                  "google",
                  result.profile.sub,
                  "google_" + result.profile.sub.substr(0, 12),
                  result.profile.email,
                  [sharedCb, profile](int32_t userId, const std::string &username, bool isNewUser, const std::string &publicSub) {
                      GoogleLoginResult out;
                      out.profile = profile;
                      out.userId = userId;
                      out.username = username;
                      out.isNewUser = isNewUser;
                      out.publicSub = publicSub;
                      (*sharedCb)(std::move(out));
                  },
                  [sharedCb](std::string errorCode) {
                      GoogleLoginResult out;
                      out.errorCode = std::move(errorCode);
                      (*sharedCb)(std::move(out));
                  }
                );
            }
          );
      }
    );
}

}  // namespace fulla::identity

#endif  // WITH_SOCIAL
