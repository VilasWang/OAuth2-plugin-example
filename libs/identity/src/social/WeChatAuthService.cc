// WeChat OAuth2 authentication service -- real implementation.
// Ported from libs/drogon/src/controllers/WeChatController.cc's login().

#ifdef WITH_SOCIAL

#include <authforge/identity/SocialAuthService.h>

namespace authforge::identity
{

WeChatAuthService::WeChatAuthService(
  std::shared_ptr<IOAuthHttpClient> httpClient,
  std::string appId,
  std::string secret
)
    : httpClient_(std::move(httpClient)), appId_(std::move(appId)), secret_(std::move(secret))
{
}

void WeChatAuthService::login(
  const std::string &code,
  std::function<void(WeChatLoginResult)> &&callback
)
{
    if (!httpClient_)
    {
        WeChatLoginResult result;
        result.errorCode = "NET_CONNECTION_FAILED";
        callback(std::move(result));
        return;
    }

    auto httpClient = httpClient_;
    auto sharedCb = std::make_shared<std::function<void(WeChatLoginResult)>>(std::move(callback));

    // 1. Exchange code for access token -- WeChat's API is GET-with-query
    // params (no POST body, no Bearer header): see IOAuthHttpClient.h's
    // header comment for the rationale on why this uses
    // getWithBearerToken() with an empty bearer token, mirroring
    // WeChatController.cc's own request construction verbatim.
    std::string tokenUrl = "https://api.weixin.qq.com/sns/oauth2/access_token?appid=" + appId_ +
                           "&secret=" + secret_ + "&code=" + code +
                           "&grant_type=authorization_code";

    httpClient_
      ->getWithBearerToken(tokenUrl, "", [httpClient, sharedCb](OAuthHttpResult tokenResp) {
          if (!tokenResp.transportOk || tokenResp.statusCode != 200)
          {
              WeChatLoginResult result;
              result.errorCode = "NET_CONNECTION_FAILED";
              (*sharedCb)(std::move(result));
              return;
          }

          if (tokenResp.body.isMember("errcode") && tokenResp.body["errcode"].asInt() != 0)
          {
              WeChatLoginResult result;
              result.errorCode = "VALIDATION_INVALID_INPUT";
              (*sharedCb)(std::move(result));
              return;
          }

          std::string accessToken = tokenResp.body["access_token"].asString();
          std::string openid = tokenResp.body["openid"].asString();

          // 2. Fetch user info -- also GET-with-query-params.
          std::string userInfoUrl =
            "https://api.weixin.qq.com/sns/userinfo?access_token=" + accessToken +
            "&openid=" + openid;

          httpClient->getWithBearerToken(userInfoUrl, "", [sharedCb](OAuthHttpResult userResp) {
              if (!userResp.transportOk)
              {
                  WeChatLoginResult result;
                  result.errorCode = "NET_CONNECTION_FAILED";
                  (*sharedCb)(std::move(result));
                  return;
              }

              // Filter response to only include necessary fields
              // (security best practice) -- mirrors WeChatController.cc.
              WeChatLoginResult result;
              result.profile.openid = userResp.body.get("openid", "").asString();
              result.profile.nickname = userResp.body.get("nickname", "").asString();
              result.profile.headimgurl = userResp.body.get("headimgurl", "").asString();
              result.profile.sex = userResp.body.get("sex", 0).asInt();
              result.profile.city = userResp.body.get("city", "").asString();
              result.profile.province = userResp.body.get("province", "").asString();
              result.profile.country = userResp.body.get("country", "").asString();
              (*sharedCb)(std::move(result));
          });
      });
}

}  // namespace authforge::identity

#endif  // WITH_SOCIAL
