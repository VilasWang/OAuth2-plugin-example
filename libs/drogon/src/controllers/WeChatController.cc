#include <fulla/drogon/controllers/WeChatController.h>
#include <drogon/HttpClient.h>
#include <fulla/drogon/observability/openapi/OpenApiGenerator.h>
#include <fulla/drogon/error/ErrorResponder.h>

#ifdef WITH_SOCIAL
// Task 24 slice 5 (fulla-sdk-refactor): identity-layer service this
// controller now optionally consumes.
#include <fulla/identity/SocialAuthService.h>
#endif  // WITH_SOCIAL

namespace fulla::drogon::controllers
{

namespace
{

// Provider configuration lives in custom_config "external_auth.wechat"
// (appid / secret); see the deployment docs. A missing or YOUR_*-placeholder
// credential disables the provider (#111).
const std::string WECHAT_APPID_KEY = "appid";
const std::string WECHAT_SECRET_KEY = "secret";

std::string getWeChatConfig(const std::string &key)
{
    auto config = ::drogon::app().getCustomConfig();
    if (config.isMember("external_auth") && config["external_auth"].isMember("wechat"))
    {
        return config["external_auth"]["wechat"].get(key, "").asString();
    }
    return "";
}

// Emit an Application error via the unified ErrorResponder entry point so the
// body is always an Error Envelope (Requirement 7.1 / 7.3 / 7.5).
void respondError(
  const ::drogon::HttpRequestPtr &req,
  const std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>> &cb,
  std::string code,
  std::string detailForLog = ""
)
{
    ::fulla::common::error::ErrorResponder::respond(
      req,
      [cb](const ::drogon::HttpResponsePtr &r) { (*cb)(r); },
      std::move(code),
      std::move(detailForLog)
    );
}

// Register OpenAPI documentation (executed once at startup)
struct WeChatControllerDocs
{
    WeChatControllerDocs()
    {
        using namespace ::fulla::drogon::observability::openapi;

        Json::Value successExample;
        successExample["openid"] = "oXXXXXXXXXXXXXXXXXXXXXXXXXX";
        successExample["nickname"] = "WeChat User";
        successExample["headimgurl"] = "https://thirdwx.qlogo.cn/...";

        Json::Value errorExample;
        errorExample["error"] = "Missing code parameter";

        // C++17 compatible initialization (avoid designated initializers)
        ::fulla::drogon::observability::openapi::EndpointInfo weChatEndpoint;
        weChatEndpoint.path = "/api/wechat/login";
        weChatEndpoint.method = "POST";
        weChatEndpoint.summary = "WeChat OAuth2 Login";
        weChatEndpoint.description =
          "Exchange WeChat authorization code for user information. "
          "This endpoint handles the server-side OAuth2 flow with "
          "WeChat Open Platform.";
        weChatEndpoint.tags = {"External Auth", "WeChat"};

        // Initialize parameters
        ::fulla::drogon::observability::openapi::ParameterInfo codeParam;
        codeParam.name = "code";
        codeParam.description = "Authorization code from WeChat OAuth2 callback (required)";
        codeParam.type = ::fulla::drogon::observability::openapi::ParameterType::STRING;
        codeParam.location = ::fulla::drogon::observability::openapi::ParameterLocation::QUERY;
        codeParam.required = true;
        weChatEndpoint.parameters = {codeParam};

        // Initialize responses
        weChatEndpoint.responses =
          {{200, "WeChat user info retrieved successfully"},
           {400, "Invalid request (missing or invalid code)"},
           {502, "Failed to contact WeChat API"}};

        // Initialize response examples
        weChatEndpoint.responseExamples = {{200, successExample}, {400, errorExample}};

        weChatEndpoint.requiresAuth = false;

        OpenApiGenerator::addEndpoint(weChatEndpoint);
    }
};

WeChatControllerDocs docs_;

}  // namespace

void WeChatController::login(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    // Handle OPTIONS for CORS
    if (req->method() == ::drogon::Options)
    {
        auto resp = ::drogon::HttpResponse::newHttpResponse();
        callback(resp);
        return;
    }

    try
    {
        // Extract the authorization code from a form-urlencoded body
        // (Drogon parses application/x-www-form-urlencoded bodies into
        // getParameter automatically) or a query parameter. The previous
        // hand-rolled body.find("code=") parser was buggy: it matched the
        // substring anywhere (so "notcode=xyz" parsed as code="xyz") and did
        // not URL-decode. Drogon's getParameter handles both correctly.
        // See GoogleController.cc for the same fix.
        std::string code = req->getParameter("code");

        if (code.empty())
        {
            ::fulla::common::error::ErrorResponder::respond(
              req,
              std::move(callback),
              "VALIDATION_MISSING_REQUIRED_FIELD",
              "wechat login: missing code parameter"
            );
            return;
        }

#ifdef WITH_SOCIAL
        // Task 24 slice 5: prefer the injected WeChatAuthService, falling
        // back to the pre-Task-24 drogon::HttpClient-direct path when
        // unwired.
        if (weChatAuthService_)
        {
            auto sharedCb =
              std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
                std::move(callback)
              );
            weChatAuthService_
              ->login(code, [sharedCb, req](fulla::identity::WeChatLoginResult result) {
                  if (!result.errorCode.empty())
                  {
                      respondError(
                        req, sharedCb, result.errorCode, "wechat login: " + result.errorCode
                      );
                      return;
                  }
                  Json::Value filteredJson;
                  filteredJson["openid"] = result.profile.openid;
                  filteredJson["nickname"] = result.profile.nickname;
                  filteredJson["headimgurl"] = result.profile.headimgurl;
                  filteredJson["sex"] = result.profile.sex;
                  filteredJson["city"] = result.profile.city;
                  filteredJson["province"] = result.profile.province;
                  filteredJson["country"] = result.profile.country;
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(filteredJson));
              });
            return;
        }
#endif  // WITH_SOCIAL

        // #111: empty OR still the YOUR_* template -> the provider is
        // disabled; fail fast with an envelope instead of a doomed upstream
        // call. See docs on provider configuration (external_auth.wechat).
        {
            const auto credentialConfigured = [](const std::string &v) {
                return !v.empty() && v.rfind("YOUR_", 0) != 0;
            };
            if (!credentialConfigured(getWeChatConfig(WECHAT_APPID_KEY)) ||
                !credentialConfigured(getWeChatConfig(WECHAT_SECRET_KEY)))
            {
                respondError(
                  req,
                  std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
                    std::move(callback)
                  ),
                  "INTERNAL_ERROR",
                  "wechat login: WeChat OAuth not configured"
                );
                return;
            }
        }

        // 1. Exchange Code for Access Token
        // Upstream API shape (WeChat Open Platform "授权后接口调用"):
        // GET /sns/oauth2/access_token with appid/secret/code/grant_type as
        // QUERY parameters -- the official docs specify GET only (POST
        // variants like /cgi-bin/stable_token are for the APP-level token,
        // not the code exchange, and cannot substitute). The secret therefore
        // must travel in the query string here; it is NEVER logged (the URL
        // is not passed to any log statement -- keep it that way) and the
        // transport is HTTPS.
        // API:
        // https://api.weixin.qq.com/sns/oauth2/access_token?appid=APPID&secret=SECRET&code=CODE&grant_type=authorization_code
        auto client = ::drogon::HttpClient::newHttpClient("https://api.weixin.qq.com");
        auto request = ::drogon::HttpRequest::newHttpRequest();
        std::string path = "/sns/oauth2/access_token?appid=" + getWeChatConfig(WECHAT_APPID_KEY) +
                           "&secret=" + getWeChatConfig(WECHAT_SECRET_KEY) + "&code=" + code +
                           "&grant_type=authorization_code";
        request->setPath(path);

        // Keep the main callback alive
        auto callbackPtr = std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
          std::move(callback)
        );

        client->sendRequest(
          request,
          [callbackPtr,
           client,
           req](::drogon::ReqResult result, const ::drogon::HttpResponsePtr &response) {
              try
              {
                  if (
                    result != ::drogon::ReqResult::Ok || !response ||
                    response->getStatusCode() != ::drogon::k200OK
                  )
                  {
                      respondError(
                        req,
                        callbackPtr,
                        "NET_CONNECTION_FAILED",
                        "wechat login: failed to contact WeChat API"
                      );
                      return;
                  }

                  auto json = *response->getJsonObject();
                  if (
                    json.isMember("errcode") && json["errcode"].isInt() &&
                    json["errcode"].asInt() != 0
                  )
                  {
                      std::string errMsg = json.isMember("errmsg") && json["errmsg"].isString()
                                             ? json["errmsg"].asString()
                                             : "unknown";
                      respondError(
                        req,
                        callbackPtr,
                        "VALIDATION_INVALID_INPUT",
                        "wechat login: WeChat error: " + errMsg
                      );
                      return;
                  }

                  if (
                    !json.isMember("access_token") || !json["access_token"].isString() ||
                    !json.isMember("openid") || !json["openid"].isString()
                  )
                  {
                      respondError(
                        req,
                        callbackPtr,
                        "VALIDATION_INVALID_INPUT",
                        "wechat login: missing access_token or openid in WeChat response"
                      );
                      return;
                  }
                  std::string accessToken = json["access_token"].asString();
                  std::string openid = json["openid"].asString();

                  // 2. Fetch User Info
                  // API:
                  // https://api.weixin.qq.com/sns/userinfo?access_token=ACCESS_TOKEN&openid=OPENID
                  auto client2 = ::drogon::HttpClient::newHttpClient("https://api.weixin.qq.com");
                  auto req2 = ::drogon::HttpRequest::newHttpRequest();
                  req2->setPath("/sns/userinfo?access_token=" + accessToken + "&openid=" + openid);

                  client2->sendRequest(
                    req2,
                    [callbackPtr,
                     req](::drogon::ReqResult res2, const ::drogon::HttpResponsePtr &resp2) {
                        try
                        {
                            if (res2 != ::drogon::ReqResult::Ok || !resp2)
                            {
                                respondError(
                                  req,
                                  callbackPtr,
                                  "NET_CONNECTION_FAILED",
                                  "wechat login: failed to fetch WeChat UserInfo"
                                );
                                return;
                            }

                            // Filter response to only include necessary fields
                            // (security best practice)
                            auto wechatData = resp2->getJsonObject();
                            Json::Value filteredJson;
                            filteredJson["openid"] = (*wechatData).get("openid", "").asString();
                            filteredJson["nickname"] = (*wechatData).get("nickname", "").asString();
                            filteredJson["headimgurl"] =
                              (*wechatData).get("headimgurl", "").asString();
                            filteredJson["sex"] = (*wechatData).get("sex", 0).asInt();
                            filteredJson["city"] = (*wechatData).get("city", "").asString();
                            filteredJson["province"] = (*wechatData).get("province", "").asString();
                            filteredJson["country"] = (*wechatData).get("country", "").asString();

                            auto finalResp =
                              ::drogon::HttpResponse::newHttpJsonResponse(filteredJson);
                            (*callbackPtr)(finalResp);
                        }
                        catch (const std::exception &e)
                        {
                            LOG_ERROR << "WeChatController::login inner async callback exception: "
                                      << e.what();
                            respondError(
                              req,
                              callbackPtr,
                              "INTERNAL_ERROR",
                              "wechat login: " + std::string(e.what())
                            );
                        }
                        catch (...)
                        {
                            LOG_ERROR
                              << "WeChatController::login inner async callback unknown exception";
                            respondError(
                              req, callbackPtr, "INTERNAL_ERROR", "wechat login: unknown error"
                            );
                        }
                    }
                  );
              }
              catch (const std::exception &e)
              {
                  LOG_ERROR << "WeChatController::login async callback exception: " << e.what();
                  respondError(
                    req, callbackPtr, "INTERNAL_ERROR", "wechat login: " + std::string(e.what())
                  );
              }
              catch (...)
              {
                  LOG_ERROR << "WeChatController::login async callback unknown exception";
                  respondError(req, callbackPtr, "INTERNAL_ERROR", "wechat login: unknown error");
              }
          }
        );
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "WeChatController::login exception: " << e.what();
        ::fulla::common::error::ErrorResponder::respond(
          req, std::move(callback), "INTERNAL_ERROR", "wechat login: " + std::string(e.what())
        );
    }
    catch (...)
    {
        LOG_ERROR << "WeChatController::login unknown exception";
        ::fulla::common::error::ErrorResponder::respond(
          req, std::move(callback), "INTERNAL_ERROR", "wechat login: unknown error"
        );
    }
}

}  // namespace fulla::drogon::controllers
