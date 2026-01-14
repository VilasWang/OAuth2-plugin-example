#include "WeChatController.h"
#include <drogon/HttpClient.h>

// TODO: REPLACE WITH YOUR REAL CREDENTIALS
const std::string WECHAT_APPID_KEY = "appid";
const std::string WECHAT_SECRET_KEY = "secret";

std::string getWeChatConfig(const std::string &key) {
    auto config = drogon::app().getCustomConfig();
    if (config.isMember("external_auth") && config["external_auth"].isMember("wechat")) {
        return config["external_auth"]["wechat"].get(key, "").asString();
    }
    return "";
}

void WeChatController::login(const HttpRequestPtr &req,
                             std::function<void(const HttpResponsePtr &)> &&callback)
{
    // Handle OPTIONS for CORS
    if (req->method() == Options) {
        auto resp = HttpResponse::newHttpResponse();
        callback(resp);
        return;
    }

    auto code = req->getParameter("code");
    if (code.empty()) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("Missing code parameter");
        callback(resp);
        return;
    }

    // 1. Exchange Code for Access Token
    // API: https://api.weixin.qq.com/sns/oauth2/access_token?appid=APPID&secret=SECRET&code=CODE&grant_type=authorization_code
    auto client = HttpClient::newHttpClient("https://api.weixin.qq.com");
    auto request = HttpRequest::newHttpRequest();
    std::string path = "/sns/oauth2/access_token?appid=" + getWeChatConfig(WECHAT_APPID_KEY) + 
                       "&secret=" + getWeChatConfig(WECHAT_SECRET_KEY) + 
                       "&code=" + code + 
                       "&grant_type=authorization_code";
    request->setPath(path);

    // Keep the main callback alive
    auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));

    client->sendRequest(request, [callbackPtr, client](ReqResult result, const HttpResponsePtr &response) {
        if (result != ReqResult::Ok || !response || response->getStatusCode() != k200OK) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k502BadGateway);
            resp->setBody("Failed to contact WeChat API");
            (*callbackPtr)(resp);
            return;
        }

        auto json = *response->getJsonObject();
        if (json.isMember("errcode") && json["errcode"].asInt() != 0) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            resp->setBody("WeChat Error: " + json["errmsg"].asString());
            (*callbackPtr)(resp);
            return;
        }

        std::string accessToken = json["access_token"].asString();
        std::string openid = json["openid"].asString();

        // 2. Fetch User Info
        // API: https://api.weixin.qq.com/sns/userinfo?access_token=ACCESS_TOKEN&openid=OPENID
        auto client2 = HttpClient::newHttpClient("https://api.weixin.qq.com");
        auto req2 = HttpRequest::newHttpRequest();
        req2->setPath("/sns/userinfo?access_token=" + accessToken + "&openid=" + openid);

        client2->sendRequest(req2, [callbackPtr](ReqResult res2, const HttpResponsePtr &resp2) {
            if (res2 != ReqResult::Ok || !resp2) {
                auto errResp = HttpResponse::newHttpResponse();
                errResp->setStatusCode(k502BadGateway);
                errResp->setBody("Failed to fetch WeChat UserInfo");
                (*callbackPtr)(errResp);
                return;
            }

            // Filter response to only include necessary fields (security best practice)
            auto wechatData = resp2->getJsonObject();
            Json::Value filteredJson;
            filteredJson["openid"] = (*wechatData).get("openid", "").asString();
            filteredJson["nickname"] = (*wechatData).get("nickname", "").asString();
            filteredJson["headimgurl"] = (*wechatData).get("headimgurl", "").asString();
            filteredJson["sex"] = (*wechatData).get("sex", 0).asInt();
            filteredJson["city"] = (*wechatData).get("city", "").asString();
            filteredJson["province"] = (*wechatData).get("province", "").asString();
            filteredJson["country"] = (*wechatData).get("country", "").asString();
            
            auto finalResp = HttpResponse::newHttpJsonResponse(filteredJson);
            (*callbackPtr)(finalResp);
        });
    });
}
