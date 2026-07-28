#include <authforge/drogon/controllers/GitHubController.h>
#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <authforge/drogon/utils/CryptoUtils.h>
#include <authforge/drogon/error/ErrorResponder.h>

#ifdef WITH_SOCIAL
// Task 24 slice 5 (authforge-sdk-refactor): identity-layer service this
// controller now optionally consumes.
#include <authforge/identity/SocialAuthService.h>
#endif  // WITH_SOCIAL

#include <authforge/storage/postgres/models/Oauth2AccessTokens.h>
#include <authforge/storage/postgres/models/Oauth2RefreshTokens.h>
#include <authforge/storage/postgres/models/Oauth2SubjectMappings.h>
#include <authforge/storage/postgres/models/UserRoles.h>
#include <authforge/storage/postgres/models/Users.h>

using namespace ::drogon::orm;
using namespace drogon_model::oauth2_db;

namespace authforge::drogon::controllers
{

namespace
{

std::string getGitHubConfig(const std::string &key)
{
    auto config = ::drogon::app().getCustomConfig();
    if (config.isMember("external_auth") && config["external_auth"].isMember("github"))
    {
        return config["external_auth"]["github"].get(key, "").asString();
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

struct GitHubControllerDocs
{
    GitHubControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo ep;
        ep.path = "/api/github/login";
        ep.method = "POST";
        ep.summary = "GitHub OAuth2 Login";
        ep.description = "Exchange GitHub authorization code for user information.";
        ep.tags = {"External Auth", "GitHub"};
        ep.requiresAuth = false;

        ::authforge::drogon::observability::openapi::ParameterInfo codeParam;
        codeParam.name = "code";
        codeParam.description = "Authorization code from GitHub OAuth2 callback";
        codeParam.type = ::authforge::drogon::observability::openapi::ParameterType::STRING;
        codeParam.location = ::authforge::drogon::observability::openapi::ParameterLocation::QUERY;
        codeParam.required = true;
        ep.parameters = {codeParam};

        ep.responses =
          {{200, "GitHub user info retrieved successfully"},
           {400, "Invalid request (missing or invalid code)"},
           {502, "Failed to contact GitHub API"}};

        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(ep);
    }
};

GitHubControllerDocs docs_;

}  // namespace

OAuth2Plugin *GitHubController::resolvePlugin() const
{
    return plugin_ ? plugin_ : ::drogon::app().getPlugin<OAuth2Plugin>();
}

void GitHubController::login(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    if (req->method() == ::drogon::Options)
    {
        callback(::drogon::HttpResponse::newHttpResponse());
        return;
    }

    // Extract code from POST body or query
    std::string code;
    auto jsonBody = req->getJsonObject();
    if (jsonBody && jsonBody->isMember("code"))
    {
        code = (*jsonBody)["code"].asString();
    }
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
          "github login: missing code parameter"
        );
        return;
    }

    std::string clientId = getGitHubConfig("client_id");
    std::string clientSecret = getGitHubConfig("client_secret");

    if (clientId.empty() || clientSecret.empty())
    {
        ::authforge::common::error::ErrorResponder::respond(
          req, std::move(callback), "INTERNAL_ERROR", "github login: GitHub OAuth not configured"
        );
        return;
    }

#ifdef WITH_SOCIAL
    // Task 24 slice 5: prefer the injected GitHubAuthService for the
    // code-exchange + userinfo-fetch + local-account find-or-create
    // steps, falling back to the pre-Task-24 drogon::HttpClient-direct
    // path when unwired. Token issuance (issueTokens below) stays in this
    // controller either way -- GitHubAuthService::login() deliberately
    // stops short of it (identity <-> oauth2 boundary, see
    // SocialAuthService.h's own scope-boundary comment).
    if (gitHubAuthService_)
    {
        auto callbackPtr = std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
          std::move(callback)
        );

        auto issueTokens = [this, callbackPtr, req](int64_t userId, const std::string &) {
            auto plugin = resolvePlugin();
            if (!plugin)
            {
                respondError(
                  req, callbackPtr, "INTERNAL_ERROR", "github login: OAuth2Plugin not available"
                );
                return;
            }
            std::string accessToken = ::authforge::drogon::utils::generateSecureToken();
            std::string refreshToken = ::authforge::drogon::utils::generateSecureToken();
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch()
            )
                         .count();
            auto db2 = ::drogon::app().getDbClient();
            try
            {
                Oauth2AccessTokens atModel;
                atModel.setToken(accessToken);
                atModel.setClientId("vue-client");
                atModel.setUserId(std::to_string(userId));
                atModel.setScope("openid profile email");
                atModel.setIssuedAt(now);
                atModel.setExpiresAt(now + 3600);
                Mapper<Oauth2AccessTokens>(db2).insert(
                  atModel,
                  [callbackPtr, accessToken, refreshToken, db2, userId, req](
                    const Oauth2AccessTokens &
                  ) {
                      auto now2 = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch()
                      )
                                    .count();
                      Oauth2RefreshTokens rtModel;
                      rtModel.setToken(refreshToken);
                      rtModel.setAccessToken(accessToken);
                      rtModel.setClientId("vue-client");
                      rtModel.setUserId(std::to_string(userId));
                      rtModel.setScope("openid profile email");
                      rtModel.setExpiresAt(now2 + 2592000);
                      Mapper<Oauth2RefreshTokens>(db2).insert(
                        rtModel,
                        [callbackPtr, accessToken, refreshToken](const Oauth2RefreshTokens &) {
                            Json::Value result;
                            result["access_token"] = accessToken;
                            result["refresh_token"] = refreshToken;
                            result["token_type"] = "Bearer";
                            result["expires_in"] = 3600;
                            (*callbackPtr)(::drogon::HttpResponse::newHttpJsonResponse(result));
                        },
                        [callbackPtr, req](const ::drogon::orm::DrogonDbException &e) {
                            respondError(
                              req,
                              callbackPtr,
                              "DB_QUERY_ERROR",
                              std::string("github login: failed to create refresh token: ") +
                                e.base().what()
                            );
                        }
                      );
                  },
                  [callbackPtr, req](const ::drogon::orm::DrogonDbException &e) {
                      respondError(
                        req,
                        callbackPtr,
                        "DB_QUERY_ERROR",
                        std::string("github login: failed to create access token: ") +
                          e.base().what()
                      );
                  }
                );
            }
            catch (const std::exception &e)
            {
                LOG_ERROR << "GitHubController::issueTokens Mapper exception: " << e.what();
                respondError(
                  req,
                  callbackPtr,
                  "DB_QUERY_ERROR",
                  std::string("github login: failed to issue tokens: ") + e.what()
                );
            }
            catch (...)
            {
                LOG_ERROR << "GitHubController::issueTokens Mapper unknown exception";
                respondError(
                  req, callbackPtr, "DB_QUERY_ERROR", "github login: failed to issue tokens"
                );
            }
        };

        gitHubAuthService_->login(
          code, [callbackPtr, req, issueTokens](authforge::identity::GitHubLoginResult result) {
              if (!result.errorCode.empty())
              {
                  respondError(
                    req, callbackPtr, result.errorCode, "github login: " + result.errorCode
                  );
                  return;
              }
              issueTokens(result.userId, result.username);
          }
        );
        return;
    }
#endif  // WITH_SOCIAL

    // Step 1: Exchange code for access token
    auto client = ::drogon::HttpClient::newHttpClient("https://github.com");
    auto request = ::drogon::HttpRequest::newHttpRequest();
    request->setMethod(::drogon::Post);
    request->setPath("/login/oauth/access_token");
    request->addHeader("Accept", "application/json");
    request->setParameter("client_id", clientId);
    request->setParameter("client_secret", clientSecret);
    request->setParameter("code", code);

    auto callbackPtr =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    client->sendRequest(
      request,
      [this,
       callbackPtr,
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
                    "github login: failed to contact GitHub Token API"
                  );
                  return;
              }

              auto json = response->getJsonObject();
              if (!json || !json->isMember("access_token") || !(*json)["access_token"].isString())
              {
                  std::string detail = "github login: GitHub returned invalid token response";
                  if (
                    json && json->isMember("error_description") &&
                    (*json)["error_description"].isString()
                  )
                      detail += ": " + (*json)["error_description"].asString();
                  respondError(req, callbackPtr, "VALIDATION_INVALID_INPUT", detail);
                  return;
              }

              std::string accessToken = (*json)["access_token"].asString();

              // Step 2: Fetch user info from GitHub API
              auto apiClient = ::drogon::HttpClient::newHttpClient("https://api.github.com");
              auto userReq = ::drogon::HttpRequest::newHttpRequest();
              userReq->setPath("/user");
              userReq->addHeader("Authorization", "Bearer " + accessToken);
              userReq->addHeader("User-Agent", "OAuth2Server");
              userReq->addHeader("Accept", "application/json");

              apiClient->sendRequest(
                userReq,
                [this,
                 callbackPtr,
                 req](::drogon::ReqResult res2, const ::drogon::HttpResponsePtr &resp2) {
                    try
                    {
                        if (
                          res2 != ::drogon::ReqResult::Ok || !resp2 ||
                          resp2->getStatusCode() != ::drogon::k200OK
                        )
                        {
                            respondError(
                              req,
                              callbackPtr,
                              "NET_CONNECTION_FAILED",
                              "github login: failed to fetch GitHub user info"
                            );
                            return;
                        }

                        auto githubData = resp2->getJsonObject();
                        std::string githubLogin = (*githubData).get("login", "").asString();
                        std::string githubEmail = (*githubData).get("email", "").asString();
                        int64_t githubId = (*githubData).get("id", 0).asInt64();

                        if (githubLogin.empty())
                        {
                            respondError(
                              req,
                              callbackPtr,
                              "VALIDATION_INVALID_INPUT",
                              "github login: GitHub returned no user login"
                            );
                            return;
                        }

                        // Step 3: Find or create local user linked to this GitHub account
                        auto db = ::drogon::app().getDbClient();
                        std::string provider = "github";
                        std::string subject = std::to_string(githubId);

                        // Check if this GitHub account is already linked
                        try
                        {
                            Criteria crit(
                              Oauth2SubjectMappings::Cols::_provider, CompareOperator::EQ, provider
                            );
                            crit =
                              crit &&
                              Criteria(
                                Oauth2SubjectMappings::Cols::_subject, CompareOperator::EQ, subject
                              );
                            Mapper<Oauth2SubjectMappings>(db).findBy(
                              crit,
                              [this,
                               callbackPtr,
                               db,
                               githubLogin,
                               githubEmail,
                               provider,
                               subject,
                               req](const std::vector<Oauth2SubjectMappings> &mappings) {
                                  auto issueTokens = [this, callbackPtr, req](
                                                       int userId, const std::string &username
                                                     ) {
                                      // Issue access_token and refresh_token
                                      auto plugin = resolvePlugin();
                                      if (!plugin)
                                      {
                                          respondError(
                                            req,
                                            callbackPtr,
                                            "INTERNAL_ERROR",
                                            "github login: OAuth2Plugin not available"
                                          );
                                          return;
                                      }

                                      std::string accessToken =
                                        ::authforge::drogon::utils::generateSecureToken();
                                      std::string refreshToken =
                                        ::authforge::drogon::utils::generateSecureToken();
                                      auto now =
                                        std::chrono::duration_cast<std::chrono::seconds>(
                                          std::chrono::system_clock::now().time_since_epoch()
                                        )
                                          .count();

                                      auto db2 = ::drogon::app().getDbClient();
                                      Oauth2AccessTokens atModel;
                                      atModel.setToken(accessToken);
                                      atModel.setClientId("vue-client");
                                      atModel.setUserId(std::to_string(userId));
                                      atModel.setScope("openid profile email");
                                      atModel.setIssuedAt(now);
                                      atModel.setExpiresAt(now + 3600);
                                      Mapper<Oauth2AccessTokens>(db2).insert(
                                        atModel,
                                        [callbackPtr, accessToken, refreshToken, db2, userId, req](
                                          const Oauth2AccessTokens &
                                        ) {
                                            auto now2 =
                                              std::chrono::duration_cast<std::chrono::seconds>(
                                                std::chrono::system_clock::now().time_since_epoch()
                                              )
                                                .count();
                                            Oauth2RefreshTokens rtModel;
                                            rtModel.setToken(refreshToken);
                                            rtModel.setAccessToken(accessToken);
                                            rtModel.setClientId("vue-client");
                                            rtModel.setUserId(std::to_string(userId));
                                            rtModel.setScope("openid profile email");
                                            rtModel.setExpiresAt(now2 + 2592000);
                                            Mapper<Oauth2RefreshTokens>(db2).insert(
                                              rtModel,
                                              [callbackPtr,
                                               accessToken,
                                               refreshToken](const Oauth2RefreshTokens &) {
                                                  Json::Value result;
                                                  result["access_token"] = accessToken;
                                                  result["refresh_token"] = refreshToken;
                                                  result["token_type"] = "Bearer";
                                                  result["expires_in"] = 3600;
                                                  (*callbackPtr)(
                                                    ::drogon::HttpResponse::newHttpJsonResponse(
                                                      result
                                                    )
                                                  );
                                              },
                                              [callbackPtr,
                                               req](const ::drogon::orm::DrogonDbException &e) {
                                                  respondError(
                                                    req,
                                                    callbackPtr,
                                                    "DB_QUERY_ERROR",
                                                    std::string(
                                                      "github login: failed to create refresh "
                                                      "token: "
                                                    ) +
                                                      e.base().what()
                                                  );
                                              }
                                            );
                                        },
                                        [callbackPtr,
                                         req](const ::drogon::orm::DrogonDbException &e) {
                                            respondError(
                                              req,
                                              callbackPtr,
                                              "DB_QUERY_ERROR",
                                              std::string(
                                                "github login: failed to create access "
                                                "token: "
                                              ) +
                                                e.base().what()
                                            );
                                        }
                                      );
                                  };

                                  if (!mappings.empty())
                                  {
                                      // Existing linked account - issue tokens
                                      int32_t userId = mappings[0].getValueOfInternalUserId();
                                      // Get username
                                      Mapper<Users>(db).findBy(
                                        Criteria(Users::Cols::_id, CompareOperator::EQ, userId),
                                        [callbackPtr,
                                         issueTokens,
                                         userId](const std::vector<Users> &users) {
                                            std::string username =
                                              users.empty() ? "user"
                                                            : users[0].getValueOfUsername();
                                            issueTokens(static_cast<int>(userId), username);
                                        },
                                        [callbackPtr,
                                         req](const ::drogon::orm::DrogonDbException &e) {
                                            respondError(
                                              req,
                                              callbackPtr,
                                              "DB_QUERY_ERROR",
                                              std::string("github login: failed to fetch user: ") +
                                                e.base().what()
                                            );
                                        }
                                      );
                                  }
                                  else
                                  {
                                      // New GitHub user - create local account + link
                                      std::string username = "gh_" + githubLogin;
                                      std::string passwordHash =
                                        ::authforge::drogon::utils::generateSecureToken();
                                      // Exemption (db-operations.md §3): INSERT...RETURNING to
                                      // capture auto-generated user id for subsequent subject-
                                      // mapping and role inserts.
                                      db->execSqlAsync(
                                        "INSERT INTO users (username, password_hash, salt, email, "
                                        "email_verified) "
                                        "VALUES ($1, $2, '', $3, true) "
                                        "ON CONFLICT (username) DO UPDATE SET email = "
                                        "EXCLUDED.email, "
                                        "email_verified = true "
                                        "RETURNING id",
                                        [callbackPtr,
                                         db,
                                         issueTokens,
                                         provider,
                                         subject,
                                         username,
                                         req](const ::drogon::orm::Result &userResult) {
                                            int32_t userId = userResult[0]["id"].as<int32_t>();
                                            // Create subject mapping
                                            Oauth2SubjectMappings mapping;
                                            mapping.setSubject(subject);
                                            mapping.setInternalUserId(userId);
                                            mapping.setProvider(provider);
                                            Mapper<Oauth2SubjectMappings>(db).insert(
                                              mapping,
                                              [callbackPtr, issueTokens, userId, username, db](
                                                const Oauth2SubjectMappings &
                                              ) {
                                                  // Assign default 'user' role
                                                  UserRoles ur;
                                                  ur.setUserId(userId);
                                                  Mapper<UserRoles>(db).insert(
                                                    ur,
                                                    [issueTokens, userId, username](
                                                      const UserRoles &
                                                    ) { issueTokens(userId, username); },
                                                    [issueTokens, userId, username](
                                                      const ::drogon::orm::DrogonDbException &
                                                    ) { issueTokens(userId, username); }
                                                  );
                                              },
                                              [callbackPtr,
                                               req](const ::drogon::orm::DrogonDbException &e) {
                                                  respondError(
                                                    req,
                                                    callbackPtr,
                                                    "DB_QUERY_ERROR",
                                                    std::string(
                                                      "github login: failed to link GitHub "
                                                      "account: "
                                                    ) +
                                                      e.base().what()
                                                  );
                                              }
                                            );
                                        },
                                        [callbackPtr,
                                         req](const ::drogon::orm::DrogonDbException &e) {
                                            respondError(
                                              req,
                                              callbackPtr,
                                              "DB_QUERY_ERROR",
                                              std::string(
                                                "github login: failed to create user "
                                                "account: "
                                              ) +
                                                e.base().what()
                                            );
                                        },
                                        username,
                                        passwordHash,
                                        githubEmail
                                      );
                                  }
                              },
                              [callbackPtr, req](const ::drogon::orm::DrogonDbException &e) {
                                  respondError(
                                    req,
                                    callbackPtr,
                                    "DB_QUERY_ERROR",
                                    std::string(
                                      "github login: database error during account linking: "
                                    ) +
                                      e.base().what()
                                  );
                              }
                            );
                        }
                        catch (const std::exception &e)
                        {
                            LOG_ERROR << "GitHubController::login Mapper exception: " << e.what();
                            respondError(
                              req,
                              callbackPtr,
                              "DB_QUERY_ERROR",
                              std::string("github login: database error: ") + e.what()
                            );
                        }
                        catch (...)
                        {
                            LOG_ERROR << "GitHubController::login Mapper unknown exception";
                            respondError(
                              req,
                              callbackPtr,
                              "DB_QUERY_ERROR",
                              "github login: unknown database error"
                            );
                        }
                    }
                    catch (const std::exception &e)
                    {
                        LOG_ERROR << "GitHubController::login inner async callback exception: "
                                  << e.what();
                        respondError(
                          req,
                          callbackPtr,
                          "INTERNAL_ERROR",
                          "github login: " + std::string(e.what())
                        );
                    }
                    catch (...)
                    {
                        LOG_ERROR
                          << "GitHubController::login inner async callback unknown exception";
                        respondError(
                          req, callbackPtr, "INTERNAL_ERROR", "github login: unknown error"
                        );
                    }
                }
              );
          }
          catch (const std::exception &e)
          {
              LOG_ERROR << "GitHubController::login async callback exception: " << e.what();
              respondError(
                req, callbackPtr, "INTERNAL_ERROR", "github login: " + std::string(e.what())
              );
          }
          catch (...)
          {
              LOG_ERROR << "GitHubController::login async callback unknown exception";
              respondError(req, callbackPtr, "INTERNAL_ERROR", "github login: unknown error");
          }
      }
    );
}

}  // namespace authforge::drogon::controllers
