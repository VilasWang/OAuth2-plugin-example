// WeChat OAuth2 authentication service -- real implementation.
// Ported from libs/drogon/src/controllers/WeChatController.cc's login().

#ifdef WITH_SOCIAL

#include <fulla/identity/SocialAuthService.h>

#include "AccountLinkFlow.h"

namespace fulla::identity
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

    // #111: a service constructed with empty/placeholder credentials (a
    // disabled provider that still got wired, or a direct construction)
    // must fail locally instead of calling the upstream with garbage.
    if (appId_.empty() || secret_.empty() || appId_.rfind("YOUR_", 0) == 0 ||
        secret_.rfind("YOUR_", 0) == 0)
    {
        WeChatLoginResult result;
        result.errorCode = "NET_CONNECTION_FAILED";
        callback(std::move(result));
        return;
    }

    auto httpClient = httpClient_;
    auto sharedCb = std::make_shared<std::function<void(WeChatLoginResult)>>(std::move(callback));

    // #70: after the profile fetch, resolve-or-create the local account
    // behind the WeChat identity (when a repository has been injected --
    // the assembly path always injects one; profile-only otherwise).
    auto accountRepo = accountRepo_;
    const bool autoCreate = autoCreate_;

    // 1. Exchange code for access token -- WeChat's API is GET-with-query
    // params (no POST body, no Bearer header): see IOAuthHttpClient.h's
    // header comment for the rationale on why this uses
    // getWithBearerToken() with an empty bearer token, mirroring
    // WeChatController.cc's own request construction verbatim.
    // Upstream API shape (official docs, "授权后接口调用"): the code-exchange
    // endpoint accepts the appid/secret ONLY as query parameters on GET (the
    // POST /cgi-bin/stable_token endpoint is for the app-level token and
    // cannot substitute). The secret therefore travels in the query string;
    // the URL is never logged (maintain that invariant) and the transport is
    // HTTPS.
    std::string tokenUrl = "https://api.weixin.qq.com/sns/oauth2/access_token?appid=" + appId_ +
                           "&secret=" + secret_ + "&code=" + code +
                           "&grant_type=authorization_code";

    httpClient_
      ->getWithBearerToken(tokenUrl, "", [httpClient, sharedCb, accountRepo, autoCreate](OAuthHttpResult tokenResp) {
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

          httpClient->getWithBearerToken(
            userInfoUrl,
            "",
            [sharedCb, accountRepo, autoCreate](OAuthHttpResult userResp) {
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

                if (!accountRepo)
                {
                    // Degraded (no repository injected): the pre-#70
                    // profile-only shape.
                    (*sharedCb)(std::move(result));
                    return;
                }

                // #70: four-state account resolution (see
                // AccountLinkFlow.h). Subject = WeChat `openid`; WeChat
                // provides no email (empty, mirroring GitHub's handling).
                const WeChatUserInfo profile = result.profile;
                social_detail::resolveOrCreateAccount(
                  accountRepo,
                  autoCreate,
                  "wechat",
                  result.profile.openid,
                  "wx_" + result.profile.openid.substr(0, 12),
                  "",
                  [sharedCb, profile](int32_t userId, const std::string &username, bool isNewUser, const std::string &publicSub) {
                      WeChatLoginResult out;
                      out.profile = profile;
                      out.userId = userId;
                      out.username = username;
                      out.isNewUser = isNewUser;
                      out.publicSub = publicSub;
                      (*sharedCb)(std::move(out));
                  },
                  [sharedCb](std::string errorCode) {
                      WeChatLoginResult out;
                      out.errorCode = std::move(errorCode);
                      (*sharedCb)(std::move(out));
                  }
                );
            }
          );
      });
}

}  // namespace fulla::identity

#endif  // WITH_SOCIAL
