#include "GoogleController.h"
#include <drogon/HttpClient.h>

// TODO: REPLACE WITH YOUR REAL GOOGLE CREDENTIALS
const std::string GOOGLE_CLIENT_ID_KEY = "client_id";
const std::string GOOGLE_CLIENT_SECRET_KEY = "client_secret";
const std::string GOOGLE_REDIRECT_URI_KEY = "redirect_uri";

std::string getGoogleConfig(const std::string &key) {
    auto config = drogon::app().getCustomConfig();
    if (config.isMember("external_auth") && config["external_auth"].isMember("google")) {
        return config["external_auth"]["google"].get(key, "").asString();
    }
    return "";
}

void GoogleController::login(const HttpRequestPtr &req,
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
    // API: https://oauth2.googleapis.com/token
    auto client = HttpClient::newHttpClient("https://oauth2.googleapis.com");
    auto request = HttpRequest::newHttpRequest();
    request->setMethod(Post);
    request->setPath("/token");
    request->setParameter("code", code);
    request->setParameter("client_id", getGoogleConfig(GOOGLE_CLIENT_ID_KEY));
    request->setParameter("client_secret", getGoogleConfig(GOOGLE_CLIENT_SECRET_KEY));
    request->setParameter("redirect_uri", getGoogleConfig(GOOGLE_REDIRECT_URI_KEY));
    request->setParameter("grant_type", "authorization_code");

    auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));

    client->sendRequest(request, [callbackPtr, client](ReqResult result, const HttpResponsePtr &response) {
        if (result != ReqResult::Ok || !response || response->getStatusCode() != k200OK) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k502BadGateway);
            resp->setBody("Failed to contact Google Token API");
            (*callbackPtr)(resp);
            return;
        }

        auto json = response->getJsonObject();
        if (!json || !json->isMember("access_token")) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k400BadRequest);
            resp->setBody("Google Error: Invalid token response");
            (*callbackPtr)(resp);
            return;
        }

        std::string accessToken = (*json)["access_token"].asString();

        // 2. Fetch User Info
        // API: https://www.googleapis.com/oauth2/v3/userinfo
        auto client2 = HttpClient::newHttpClient("https://www.googleapis.com");
        auto req2 = HttpRequest::newHttpRequest();
        req2->setPath("/oauth2/v3/userinfo");
        req2->addHeader("Authorization", "Bearer " + accessToken);

        client2->sendRequest(req2, [callbackPtr](ReqResult res2, const HttpResponsePtr &resp2) {
            if (res2 != ReqResult::Ok || !resp2) {
                auto errResp = HttpResponse::newHttpResponse();
                errResp->setStatusCode(k502BadGateway);
                errResp->setBody("Failed to fetch Google UserInfo");
                (*callbackPtr)(errResp);
                return;
            }

            // Filter response to only include necessary fields (security best practice)
            auto googleData = resp2->getJsonObject();
            Json::Value filteredJson;
            filteredJson["sub"] = (*googleData).get("sub", "").asString();
            filteredJson["name"] = (*googleData).get("name", "").asString();
            filteredJson["email"] = (*googleData).get("email", "").asString();
            filteredJson["picture"] = (*googleData).get("picture", "").asString();
            
            auto finalResp = HttpResponse::newHttpJsonResponse(filteredJson);
            (*callbackPtr)(finalResp);
        });
    });
}
