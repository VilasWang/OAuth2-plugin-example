// Google OAuth2 authentication service -- real implementation.
// Ported from libs/drogon/src/controllers/GoogleController.cc's login().

#ifdef WITH_SOCIAL

#include <authforge/identity/SocialAuthService.h>

namespace authforge::identity
{

GoogleAuthService::GoogleAuthService(
  std::shared_ptr<IOAuthHttpClient> httpClient,
  std::string clientId,
  std::string clientSecret,
  std::string redirectUri
) :
  httpClient_(std::move(httpClient)),
  clientId_(std::move(clientId)),
  clientSecret_(std::move(clientSecret)),
  redirectUri_(std::move(redirectUri))
{
}

void GoogleAuthService::login(const std::string &code, std::function<void(GoogleLoginResult)> &&callback)
{
    if (!httpClient_)
    {
        GoogleLoginResult result;
        result.errorCode = "NET_CONNECTION_FAILED";
        callback(std::move(result));
        return;
    }

    auto httpClient = httpClient_;
    auto sharedCb = std::make_shared<std::function<void(GoogleLoginResult)>>(std::move(callback));

    // 1. Exchange code for access token -- POST https://oauth2.googleapis.com/token
    std::vector<std::pair<std::string, std::string>> params = {
      {"code", code},
      {"client_id", clientId_},
      {"client_secret", clientSecret_},
      {"redirect_uri", redirectUri_},
      {"grant_type", "authorization_code"}
    };

    httpClient_->postForm(
      "https://oauth2.googleapis.com/token",
      params,
      [httpClient, sharedCb](OAuthHttpResult tokenResp) {
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
            [sharedCb](OAuthHttpResult userResp) {
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
                (*sharedCb)(std::move(result));
            }
          );
      }
    );
}

}  // namespace authforge::identity

#endif  // WITH_SOCIAL
