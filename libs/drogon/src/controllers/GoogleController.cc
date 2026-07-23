#include <authforge/drogon/controllers/GoogleController.h>
#include <drogon/HttpClient.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>

#ifdef WITH_SOCIAL
// Task 24 slice 5 (authforge-sdk-refactor): identity-layer service this
// controller now optionally consumes.
#include <authforge/identity/SocialAuthService.h>
#endif  // WITH_SOCIAL

namespace authforge::drogon::controllers
{

namespace
{

// TODO: REPLACE WITH YOUR REAL GOOGLE CREDENTIALS
const std::string GOOGLE_CLIENT_ID_KEY = "client_id";
const std::string GOOGLE_CLIENT_SECRET_KEY = "client_secret";
const std::string GOOGLE_REDIRECT_URI_KEY = "redirect_uri";

std::string getGoogleConfig(const std::string &key)
{
    auto config = ::drogon::app().getCustomConfig();
    if (config.isMember("external_auth") && config["external_auth"].isMember("google"))
    {
        return config["external_auth"]["google"].get(key, "").asString();
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
    ::authforge::common::error::ErrorResponder::respond(
      req,
      [cb](const ::drogon::HttpResponsePtr &r) { (*cb)(r); },
      std::move(code),
      std::move(detailForLog)
    );
}

// Register OpenAPI documentation (executed once at startup)
struct GoogleControllerDocs
{
    GoogleControllerDocs()
    {
        using namespace ::authforge::drogon::observability::openapi;

        Json::Value successExample;
        successExample["sub"] = "123456789012345678901";
        successExample["name"] = "John Doe";
        successExample["email"] = "john.doe@gmail.com";
        successExample["picture"] = "https://lh3.googleusercontent.com/...";

        Json::Value errorExample;
        errorExample["error"] = "Missing code parameter";

        // C++17 compatible initialization (avoid designated initializers)
        ::authforge::drogon::observability::openapi::EndpointInfo googleEndpoint;
        googleEndpoint.path = "/api/google/login";
        googleEndpoint.method = "POST";
        googleEndpoint.summary = "Google OAuth2 Login";
        googleEndpoint.description =
          "Exchange Google authorization code for user information. "
          "This endpoint handles the server-side OAuth2 flow with "
          "Google "
          "Identity Platform.";
        googleEndpoint.tags = {"External Auth", "Google"};

        // Initialize parameters
        ::authforge::drogon::observability::openapi::ParameterInfo codeParam;
        codeParam.name = "code";
        codeParam.description = "Authorization code from Google OAuth2 callback (required)";
        codeParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
        codeParam.location = ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
        codeParam.required = true;
        googleEndpoint.parameters = {codeParam};

        // Initialize responses
        googleEndpoint.responses =
          {{200, "Google user info retrieved successfully"},
           {400, "Invalid request (missing or invalid code)"},
           {502, "Failed to contact Google API"}};

        // Initialize response examples
        googleEndpoint.responseExamples = {{200, successExample}, {400, errorExample}};

        googleEndpoint.requiresAuth = false;

        OpenApiGenerator::addEndpoint(googleEndpoint);
    }
};

GoogleControllerDocs docs_;

}  // namespace

void GoogleController::login(
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

    // Try to get code from POST body first, then fallback to query parameter
    std::string code;

    // Check Content-Type and parse accordingly
    auto contentType = req->getHeader("Content-Type");
    if (contentType.find("application/x-www-form-urlencoded") != std::string::npos)
    {
        // Parse from POST body
        auto body = req->getBody();
        // Simple parsing for "code=xxx" format
        size_t codePos = body.find("code=");
        if (codePos != std::string::npos)
        {
            size_t valueStart = codePos + 5;  // "code=" length
            size_t valueEnd = body.find("&", valueStart);
            if (valueEnd == std::string::npos)
            {
                valueEnd = body.length();
            }
            code = body.substr(valueStart, valueEnd - valueStart);
        }
    }

    // Fallback to query parameter
    if (code.empty())
    {
        code = req->getParameter("code");
    }

    if (code.empty())
    {
        ::authforge::common::error::ErrorResponder::respond(
          req,
          std::move(callback),
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "google login: missing code parameter"
        );
        return;
    }

#ifdef WITH_SOCIAL
    // Task 24 slice 5: prefer the injected GoogleAuthService (constructed
    // once at startup by bootstrap::wireIdentityServices(), backed by
    // DrogonOAuthHttpClient), falling back to the pre-Task-24
    // drogon::HttpClient-direct path when unwired -- same
    // injected-with-fallback pattern established by SessionController's
    // Task 24 slice 4.
    if (googleAuthService_)
    {
        auto sharedCb = std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
          std::move(callback)
        );
        googleAuthService_
          ->login(code, [sharedCb, req](authforge::identity::GoogleLoginResult result) {
              if (!result.errorCode.empty())
              {
                  respondError(
                    req, sharedCb, result.errorCode, "google login: " + result.errorCode
                  );
                  return;
              }
              Json::Value filteredJson;
              filteredJson["sub"] = result.profile.sub;
              filteredJson["name"] = result.profile.name;
              filteredJson["email"] = result.profile.email;
              filteredJson["picture"] = result.profile.picture;
              (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(filteredJson));
          });
        return;
    }
#endif  // WITH_SOCIAL

    // 1. Exchange Code for Access Token
    // API: https://oauth2.googleapis.com/token
    auto client = ::drogon::HttpClient::newHttpClient("https://oauth2.googleapis.com");
    auto request = ::drogon::HttpRequest::newHttpRequest();
    request->setMethod(::drogon::Post);
    request->setPath("/token");
    request->setParameter("code", code);
    request->setParameter("client_id", getGoogleConfig(GOOGLE_CLIENT_ID_KEY));
    request->setParameter("client_secret", getGoogleConfig(GOOGLE_CLIENT_SECRET_KEY));
    request->setParameter("redirect_uri", getGoogleConfig(GOOGLE_REDIRECT_URI_KEY));
    request->setParameter("grant_type", "authorization_code");

    auto callbackPtr =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    client->sendRequest(
      request,
      [callbackPtr,
       client,
       req](::drogon::ReqResult result, const ::drogon::HttpResponsePtr &response) {
          if (
            result != ::drogon::ReqResult::Ok || !response ||
            response->getStatusCode() != ::drogon::k200OK
          )
          {
              respondError(
                req,
                callbackPtr,
                "NET_CONNECTION_FAILED",
                "google login: failed to contact Google Token API"
              );
              return;
          }

          auto json = response->getJsonObject();
          if (!json || !json->isMember("access_token"))
          {
              respondError(
                req, callbackPtr, "VALIDATION_INVALID_INPUT", "google login: invalid token response"
              );
              return;
          }

          std::string accessToken = (*json)["access_token"].asString();

          // 2. Fetch User Info
          // API: https://www.googleapis.com/oauth2/v3/userinfo
          auto client2 = ::drogon::HttpClient::newHttpClient("https://www.googleapis.com");
          auto req2 = ::drogon::HttpRequest::newHttpRequest();
          req2->setPath("/oauth2/v3/userinfo");
          req2->addHeader("Authorization", "Bearer " + accessToken);

          client2->sendRequest(
            req2,
            [callbackPtr, req](::drogon::ReqResult res2, const ::drogon::HttpResponsePtr &resp2) {
                if (res2 != ::drogon::ReqResult::Ok || !resp2)
                {
                    respondError(
                      req,
                      callbackPtr,
                      "NET_CONNECTION_FAILED",
                      "google login: failed to fetch Google UserInfo"
                    );
                    return;
                }

                // Filter response to only include necessary fields
                // (security best practice)
                auto googleData = resp2->getJsonObject();
                Json::Value filteredJson;
                filteredJson["sub"] = (*googleData).get("sub", "").asString();
                filteredJson["name"] = (*googleData).get("name", "").asString();
                filteredJson["email"] = (*googleData).get("email", "").asString();
                filteredJson["picture"] = (*googleData).get("picture", "").asString();

                auto finalResp = ::drogon::HttpResponse::newHttpJsonResponse(filteredJson);
                (*callbackPtr)(finalResp);
            }
          );
      }
    );
}

}  // namespace authforge::drogon::controllers
