#include "MockWeChatController.h"

void MockWeChatController::authorize(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    // Extract parameters
    auto redirectUri = req->getParameter("redirect_uri");
    auto state = req->getParameter("state");

    // Mimic WeChat's QR Code Page
    std::string html =
        "<html><head><title>WeChat Login</title>"
        "<style>body{text-align:center; font-family:sans-serif; "
        "margin-top:50px;}</style></head>"
        "<body>"
        "<h1>WeChat Login</h1>"
        "<div style='border:1px solid #ccc; padding:20px; "
        "display:inline-block; border-radius:10px;'>"
        "<p>Please scan the QR Code (Simulation)</p>"
        "<div style='width:200px; height:200px; background:#eee; margin:20px "
        "auto; line-height:200px;'>[Fake QR Code]</div>"
        "<button onclick=\"location.href='" +
        redirectUri + "?code=MOCK_WECHAT_CODE&state=" + state +
        "'\" "
        "style='background:#07c160; color:white; padding:10px 20px; "
        "border:none; border-radius:5px; cursor:pointer;'>Scan Success</button>"
        "</div>"
        "</body></html>";

    auto resp = HttpResponse::newHttpResponse();
    resp->setBody(html);
    callback(resp);
}

void MockWeChatController::token(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    // Handle OPTIONS for CORS
    if (req->method() == Options)
    {
        auto resp = HttpResponse::newHttpResponse();
        callback(resp);
        return;
    }

    // Return Mock Token
    Json::Value json;
    json["access_token"] = "MOCK_WECHAT_ACCESS_TOKEN";
    json["expires_in"] = 7200;
    json["refresh_token"] = "MOCK_WECHAT_REFRESH_TOKEN";
    json["openid"] = "MOCK_WECHAT_OPENID_123456";
    json["scope"] = "snsapi_userinfo";

    auto resp = HttpResponse::newHttpJsonResponse(json);
    callback(resp);
}

void MockWeChatController::userInfo(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    // Handle OPTIONS for CORS
    if (req->method() == Options)
    {
        auto resp = HttpResponse::newHttpResponse();
        callback(resp);
        return;
    }

    // Return Mock UserInfo
    Json::Value json;
    json["openid"] = "MOCK_WECHAT_OPENID_123456";
    json["nickname"] = "WeChat Mock User";
    json["sex"] = 1;
    json["language"] = "zh_CN";
    json["city"] = "Shenzhen";
    json["province"] = "Guangdong";
    json["country"] = "CN";
    // Using a placeholder image for avatar
    json["headimgurl"] =
        "https://ui-avatars.com/api/"
        "?name=WeChat+User&background=07c160&color=fff";
    json["privilege"] = Json::arrayValue;

    auto resp = HttpResponse::newHttpJsonResponse(json);
    callback(resp);
}
