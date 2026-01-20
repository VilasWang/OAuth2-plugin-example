#include "OAuth2Controller.h"
#include "../models/Users.h"
#include <drogon/drogon.h>
#include "../plugins/OAuth2Metrics.h"
#include <drogon/utils/Utilities.h>
#include <algorithm>

using namespace oauth2;

void OAuth2Controller::authorize(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto params = req->getParameters();
    std::string responseType = params["response_type"];
    std::string clientId = params["client_id"];
    std::string redirectUri = params["redirect_uri"];
    std::string scope = params["scope"];
    std::string state = params["state"];
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    if (!plugin)
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k500InternalServerError);
        resp->setBody("OAuth2Plugin not loaded");
        callback(resp);
        return;
    }

    // Validate Client (Async)
    plugin->validateClient(
        clientId,
        "",
        [=, callback = std::move(callback)](bool validClient) mutable {
            if (!validClient)
            {
                Metrics::incRequest("authorize", 400);
                Metrics::incLoginFailure("invalid_client_id");

                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setBody("Invalid client_id");
                callback(resp);
                return;
            }

            // Validate Redirect URI (Async)
            plugin->validateRedirectUri(
                clientId,
                redirectUri,
                [=, callback = std::move(callback)](bool validUri) mutable {
                    if (!validUri)
                    {
                        auto resp = HttpResponse::newHttpResponse();
                        resp->setStatusCode(k400BadRequest);
                        resp->setBody("Invalid redirect_uri");
                        callback(resp);
                        return;
                    }

                    // Check Session
                    auto userId = req->session()->get<std::string>("userId");
                    if (!userId.empty())
                    {
                        // Generate Code (Async)
                        plugin->generateAuthorizationCode(
                            clientId,
                            userId,
                            scope,
                            [=,
                             callback = std::move(callback)](std::string code) {
                                std::string location =
                                    redirectUri + "?code=" + code;
                                if (!state.empty())
                                    location += "&state=" + state;
                                auto resp =
                                    HttpResponse::newRedirectionResponse(
                                        location);
                                Metrics::incRequest("authorize", 302);
                                callback(resp);
                            });
                        return;
                    }

                    // Render Login Page
                    HttpViewData data;
                    data.insert("client_id", clientId);
                    data.insert("redirect_uri", redirectUri);
                    data.insert("scope", scope);
                    data.insert("state", state);
                    data.insert("response_type", responseType);
                    auto resp =
                        HttpResponse::newHttpViewResponse("login.csp", data);
                    callback(resp);
                });
        });
}

void OAuth2Controller::login(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    // Handle form submission
    auto params = req->getParameters();
    std::string username = params["username"];
    std::string password = params["password"];

    // Hidden fields from the form
    std::string clientId = params["client_id"];
    std::string redirectUri = params["redirect_uri"];
    std::string scope = params["scope"];
    std::string state = params["state"];

    // Mock Authentication
    // ORM Authentication
    try
    {
        auto mapper = drogon::orm::Mapper<drogon_model::oauth_test::Users>(
            drogon::app().getDbClient());
        auto user =
            mapper.findOne({drogon_model::oauth_test::Users::Cols::_username,
                            drogon::orm::CompareOperator::EQ,
                            username});

        // Compute Hash
        std::string salt = user.getValueOfSalt();
        std::string dbHash = user.getValueOfPasswordHash();
        std::string inputHash = drogon::utils::getSha256(password + salt);

        // Compare (Case insensitive for Hex)
        bool valid = false;
        if (inputHash.length() == dbHash.length())
        {
            std::transform(inputHash.begin(),
                           inputHash.end(),
                           inputHash.begin(),
                           ::tolower);
            std::transform(dbHash.begin(),
                           dbHash.end(),
                           dbHash.begin(),
                           ::tolower);
            if (inputHash == dbHash)
                valid = true;
        }

        if (valid)
        {
            // Success
            req->session()->insert("userId", username);
            auto plugin = drogon::app().getPlugin<OAuth2Plugin>();

            plugin->generateAuthorizationCode(
                clientId,
                username,
                scope,
                [=, callback = std::move(callback)](std::string code) {
                    std::string location = redirectUri + "?code=" + code;
                    if (!state.empty())
                        location += "&state=" + state;
                    auto resp = HttpResponse::newRedirectionResponse(location);
                    callback(resp);
                });
        }
        else
        {
            // Fail (Bad Password)
            Metrics::incLoginFailure("bad_credentials");
            auto resp = HttpResponse::newHttpResponse();
            resp->setBody("Login Failed: Invalid Credentials");
            callback(resp);
        }
    }
    catch (const drogon::orm::DrogonDbException &e)
    {
        LOG_WARN << "Login Failed: DB Error or User Not Found. "
                 << e.base().what();
        // User not found throws exception in findOne
        Metrics::incLoginFailure("user_not_found");
        auto resp = HttpResponse::newHttpResponse();
        resp->setBody("Login Failed: User Not Found");
        callback(resp);
    }
}

void OAuth2Controller::registerUser(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto params = req->getParameters();
    std::string username = params["username"];
    std::string password = params["password"];
    std::string email = params["email"];

    if (username.empty() || password.empty())
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("Username and password required");
        callback(resp);
        return;
    }

    // Hash Password
    std::string salt = drogon::utils::getUuid();
    std::string passwordHash = drogon::utils::getSha256(password + salt);

    drogon_model::oauth_test::Users newUser;
    newUser.setUsername(username);
    newUser.setPasswordHash(passwordHash);
    newUser.setSalt(salt);
    if (!email.empty())
        newUser.setEmail(email);

    auto mapper = drogon::orm::Mapper<drogon_model::oauth_test::Users>(
        drogon::app().getDbClient());

    // Async Insert
    mapper.insert(
        newUser,
        [callback](const drogon_model::oauth_test::Users &u) {
            auto resp = HttpResponse::newHttpResponse();
            resp->setBody("User Registered: " + u.getValueOfUsername());
            callback(resp);
        },
        [callback](const drogon::orm::DrogonDbException &e) {
            LOG_ERROR << "Register Failed: " << e.base().what();
            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k500InternalServerError);
            resp->setBody("Registration Failed (Username likely exists)");
            callback(resp);
        });
}

void OAuth2Controller::token(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto plugin = drogon::app().getPlugin<OAuth2Plugin>();
    // Expect raw params or json? Standard says form-urlencoded.
    // Drogon parses parameters automatically.
    std::string grantType = req->getParameter("grant_type");
    std::string code = req->getParameter("code");
    std::string redirectUri = req->getParameter("redirect_uri");
    std::string clientId = req->getParameter("client_id");
    std::string clientSecret = req->getParameter("client_secret");

    if (grantType == "authorization_code")
    {
        plugin->exchangeCodeForToken(
            code,
            clientId,
            [callback = std::move(callback)](const Json::Value &result) {
                if (result.isMember("error"))
                {
                    auto resp = HttpResponse::newHttpJsonResponse(result);
                    resp->setStatusCode(k400BadRequest);
                    callback(resp);
                    return;
                }

                auto resp = HttpResponse::newHttpJsonResponse(result);
                Metrics::incRequest("token", 200);
                Metrics::updateActiveTokens(1);
                callback(resp);
            });
    }
    else if (grantType == "refresh_token")
    {
        std::string refreshToken = req->getParameter("refresh_token");
        plugin->refreshAccessToken(
            refreshToken,
            clientId,
            [callback = std::move(callback)](const Json::Value &result) {
                if (result.isMember("error"))
                {
                    auto resp = HttpResponse::newHttpJsonResponse(result);
                    resp->setStatusCode(
                        k400BadRequest);  // Or 401? Spec says 400 uses
                                          // invalid_grant
                    callback(resp);
                    return;
                }

                auto resp = HttpResponse::newHttpJsonResponse(result);
                Metrics::incRequest("token", 200);
                callback(resp);
            });
    }
    else
    {
        Metrics::incRequest("token", 400);
        Json::Value json;
        json["error"] = "unsupported_grant_type";
        auto resp = HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
    }
}

void OAuth2Controller::userInfo(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    if (req->method() == Options)
    {
        auto resp = HttpResponse::newHttpResponse();
        callback(resp);
        return;
    }

    // This endpoint is protected by OAuth2Middleware.
    // If we are here, we have a valid token.

    // Attributes set by OAuth2Middleware
    std::string userId;
    auto attrs = req->getAttributes();
    if (attrs->find("userId"))
        userId = attrs->get<std::string>("userId");

    Json::Value json;
    json["sub"] = userId;
    json["name"] = userId;
    json["email"] = userId + "@example.com";
    auto resp = HttpResponse::newHttpJsonResponse(json);
    callback(resp);
}
