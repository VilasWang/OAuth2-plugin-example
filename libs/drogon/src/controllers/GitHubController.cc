#include <authforge/drogon/controllers/GitHubController.h>
#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <authforge/drogon/utils/CryptoUtils.h>
#include <authforge/drogon/error/ErrorResponder.h>

#ifdef WITH_SOCIAL
// Task 24 slice 5 (authforge-sdk-refactor): identity-layer service this
// controller now optionally consumes.
#include <authforge/identity/SocialAuthService.h>
#endif  // WITH_SOCIAL

// OAuth2 token DTOs for issueTokensForUser -> plugin->saveTokenPair (the
// storage-abstraction route; replaces the former direct Mapper<Oauth2Access/
//RefreshTokens> persistence).
#include <authforge/oauth2/model/Dto.h>

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

// Shorthand for the (very common) DB-exception path: report a DB_QUERY_ERROR
// with a "github login: <ctx>: <what>" detail string. Collapses the ~6 places
// that previously spelled this template out inline.
void respondDbError(
  const ::drogon::HttpRequestPtr &req,
  const std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>> &cb,
  const char *ctx,
  const ::drogon::orm::DrogonDbException &e
)
{
    respondError(
      req, cb, "DB_QUERY_ERROR", std::string("github login: ") + ctx + ": " + e.base().what()
    );
}

// Same as above but for a generic std::exception (used by the try/catch guards
// around synchronous Mapper construction inside async callbacks, where the
// thrown type is not necessarily a DrogonDbException).
void respondDbError(
  const ::drogon::HttpRequestPtr &req,
  const std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>> &cb,
  const char *ctx,
  const std::exception &e
)
{
    respondError(req, cb, "DB_QUERY_ERROR", std::string("github login: ") + ctx + ": " + e.what());
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

    auto callbackPtr = CallbackPtr(std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(
      std::move(callback)
    ));

#ifdef WITH_SOCIAL
    // Task 24 slice 5: prefer the injected GitHubAuthService for the
    // code-exchange + userinfo-fetch + local-account find-or-create
    // steps, falling back to the pre-Task-24 drogon::HttpClient-direct
    // path when unwired. Token issuance (issueTokensForUser below) stays
    // in this controller either way -- GitHubAuthService::login() deliberately
    // stops short of it (identity <-> oauth2 boundary, see
    // SocialAuthService.h's own scope-boundary comment).
    if (gitHubAuthService_)
    {
        gitHubAuthService_->login(
          code, [this, req, callbackPtr](authforge::identity::GitHubLoginResult result) {
              if (!result.errorCode.empty())
              {
                  respondError(
                    req, callbackPtr, result.errorCode, "github login: " + result.errorCode
                  );
                  return;
              }
              issueTokensForUser(req, callbackPtr, result.userId);
          }
        );
        return;
    }
#endif  // WITH_SOCIAL

    // Fallback path: raw drogon::HttpClient + direct DB. Previously this was
    // a single ~560-line method with 7 nested callbacks; it now reads as the
    // linear step sequence below (see the step helpers' header comments).
    exchangeCodeForToken(req, callbackPtr, clientId, clientSecret, code);
}

// ---------------------------------------------------------------------------
// Token issuance (shared by both WITH_SOCIAL and fallback paths)
// ---------------------------------------------------------------------------

void GitHubController::issueTokensForUser(
  const ::drogon::HttpRequestPtr &req,
  const CallbackPtr &callbackPtr,
  int64_t userId
)
{
    auto plugin = resolvePlugin();
    if (!plugin)
    {
        respondError(req, callbackPtr, "INTERNAL_ERROR", "github login: OAuth2Plugin not available");
        return;
    }
    // Phase 4 follow-up (coverage push, product defect B): route token issuance
    // through OAuth2Plugin::saveTokenPair (the same API TokenEndpointController's
    // device-code flow uses, TokenEndpointController.cc:1116) instead of calling
    // drogon::app().getDbClient() + Mapper<Oauth2AccessTokens/RefreshTokens>
    // directly. The direct path (a) bypassed the storage abstraction so memory
    // storage mode crashed the process via an uncatchable getDbClient() assert,
    // and (b) made the happy-path untestable. saveTokenPair forwards to the
    // ITokenRepository selected by storage_type (Memory/Postgres/Redis), so this
    // now works in every storage mode and is mock-testable.
    auto accessTokenStr = ::authforge::drogon::utils::generateSecureToken();
    auto refreshTokenStr = ::authforge::drogon::utils::generateSecureToken();
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch()
    )
                 .count();
    const long long accessTokenTtl = plugin->getAccessTokenTtl();
    const long long refreshTokenTtl = plugin->getRefreshTokenTtl();
    const std::string clientId = "vue-client";
    const std::string scope = "openid profile email";

    authforge::oauth2::model::OAuth2AccessToken accessToken;
    // Preserve GitHub's pre-existing behavior of storing the raw (unhashed)
    // token value; hashing it would change what already-issued tokens look up
    // against and is out of scope for this storage-abstraction fix. (The
    // canonical token-endpoint path hashes via hashToken; GitHub can be aligned
    // separately.)
    accessToken.token = accessTokenStr;
    accessToken.clientId = clientId;
    accessToken.userId = std::to_string(userId);
    accessToken.scope = scope;
    accessToken.issuedAt = now;
    accessToken.expiresAt = now + accessTokenTtl;

    authforge::oauth2::model::OAuth2RefreshToken refreshToken;
    refreshToken.token = refreshTokenStr;
    refreshToken.accessToken = accessTokenStr;
    refreshToken.clientId = clientId;
    refreshToken.userId = std::to_string(userId);
    refreshToken.scope = scope;
    refreshToken.expiresAt = now + refreshTokenTtl;

    plugin->saveTokenPair(
      accessToken,
      refreshToken,
      [callbackPtr, accessTokenStr, refreshTokenStr, accessTokenTtl]() {
          Json::Value result;
          result["access_token"] = accessTokenStr;
          result["refresh_token"] = refreshTokenStr;
          result["token_type"] = "Bearer";
          result["expires_in"] = (Json::Int64)accessTokenTtl;
          (*callbackPtr)(::drogon::HttpResponse::newHttpJsonResponse(result));
      }
    );
}

// ---------------------------------------------------------------------------
// Fallback path steps
// ---------------------------------------------------------------------------

void GitHubController::exchangeCodeForToken(
  const ::drogon::HttpRequestPtr &req,
  const CallbackPtr &callbackPtr,
  const std::string &clientId,
  const std::string &clientSecret,
  const std::string &code
)
{
    // Step 1: Exchange code for access token
    auto client = ::drogon::HttpClient::newHttpClient("https://github.com");
    auto request = ::drogon::HttpRequest::newHttpRequest();
    request->setMethod(::drogon::Post);
    request->setPath("/login/oauth/access_token");
    request->addHeader("Accept", "application/json");
    request->setParameter("client_id", clientId);
    request->setParameter("client_secret", clientSecret);
    request->setParameter("code", code);

    client->sendRequest(
      request,
      [this, req, callbackPtr](::drogon::ReqResult result, const ::drogon::HttpResponsePtr &response) {
          try
          {
              if (
                result != ::drogon::ReqResult::Ok || !response ||
                response->getStatusCode() != ::drogon::k200OK
              )
              {
                  respondError(
                    req, callbackPtr, "NET_CONNECTION_FAILED", "github login: failed to contact GitHub Token API"
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
              fetchGitHubUserInfo(req, callbackPtr, accessToken);
          }
          catch (const std::exception &e)
          {
              LOG_ERROR << "GitHubController::login async callback exception: " << e.what();
              respondError(req, callbackPtr, "INTERNAL_ERROR", "github login: " + std::string(e.what()));
          }
          catch (...)
          {
              LOG_ERROR << "GitHubController::login async callback unknown exception";
              respondError(req, callbackPtr, "INTERNAL_ERROR", "github login: unknown error");
          }
      }
    );
}

void GitHubController::fetchGitHubUserInfo(
  const ::drogon::HttpRequestPtr &req,
  const CallbackPtr &callbackPtr,
  const std::string &accessToken
)
{
    // Step 2: Fetch user info from GitHub API
    auto apiClient = ::drogon::HttpClient::newHttpClient("https://api.github.com");
    auto userReq = ::drogon::HttpRequest::newHttpRequest();
    userReq->setPath("/user");
    userReq->addHeader("Authorization", "Bearer " + accessToken);
    userReq->addHeader("User-Agent", "OAuth2Server");
    userReq->addHeader("Accept", "application/json");

    apiClient->sendRequest(
      userReq,
      [this, req, callbackPtr](::drogon::ReqResult res2, const ::drogon::HttpResponsePtr &resp2) {
          try
          {
              if (
                res2 != ::drogon::ReqResult::Ok || !resp2 ||
                resp2->getStatusCode() != ::drogon::k200OK
              )
              {
                  respondError(
                    req, callbackPtr, "NET_CONNECTION_FAILED", "github login: failed to fetch GitHub user info"
                  );
                  return;
              }

              auto githubData = resp2->getJsonObject();
              // GitHub can return a non-JSON body (e.g. an HTML error page on
              // a 5xx), in which case getJsonObject() yields an empty
              // shared_ptr; dereferencing it would crash. Mirror the null
              // check already done for the token response above.
              if (!githubData)
              {
                  respondError(
                    req,
                    callbackPtr,
                    "VALIDATION_INVALID_INPUT",
                    "github login: GitHub returned non-JSON user info"
                  );
                  return;
              }
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

              resolveSubjectMapping(req, callbackPtr, githubLogin, githubEmail, githubId);
          }
          catch (const std::exception &e)
          {
              LOG_ERROR << "GitHubController::login inner async callback exception: " << e.what();
              respondError(req, callbackPtr, "INTERNAL_ERROR", "github login: " + std::string(e.what()));
          }
          catch (...)
          {
              LOG_ERROR << "GitHubController::login inner async callback unknown exception";
              respondError(req, callbackPtr, "INTERNAL_ERROR", "github login: unknown error");
          }
      }
    );
}

void GitHubController::resolveSubjectMapping(
  const ::drogon::HttpRequestPtr &req,
  const CallbackPtr &callbackPtr,
  const std::string &githubLogin,
  const std::string &githubEmail,
  int64_t githubId
)
{
    // Step 3: Find or create local user linked to this GitHub account
    auto db = ::drogon::app().getDbClient();
    std::string provider = "github";
    std::string subject = std::to_string(githubId);

    try
    {
        Criteria crit(
          Oauth2SubjectMappings::Cols::_provider, CompareOperator::EQ, provider
        );
        crit = crit &&
               Criteria(
                 Oauth2SubjectMappings::Cols::_subject, CompareOperator::EQ, subject
               );

        Mapper<Oauth2SubjectMappings>(db).findBy(
          crit,
          [this, req, callbackPtr, githubLogin, githubEmail, provider, subject](
            const std::vector<Oauth2SubjectMappings> &mappings
          ) {
              if (!mappings.empty())
              {
                  // Existing linked account
                  int32_t userId = mappings[0].getValueOfInternalUserId();
                  linkExistingUser(req, callbackPtr, userId);
              }
              else
              {
                  // New GitHub user - create local account + link
                  createNewLinkedUser(req, callbackPtr, githubLogin, githubEmail, provider, subject);
              }
          },
          [req, callbackPtr](const ::drogon::orm::DrogonDbException &e) {
              respondDbError(req, callbackPtr, "database error during account linking", e);
          }
        );
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "GitHubController::login Mapper exception: " << e.what();
        respondDbError(req, callbackPtr, "database error", e);
    }
    catch (...)
    {
        LOG_ERROR << "GitHubController::login Mapper unknown exception";
        respondError(req, callbackPtr, "DB_QUERY_ERROR", "github login: unknown database error");
    }
}

void GitHubController::linkExistingUser(
  const ::drogon::HttpRequestPtr &req,
  const CallbackPtr &callbackPtr,
  int32_t userId
)
{
    // Step 4a: existing mapping -- look up the user, then issue tokens.
    //
    // NOTE: this findBy is preserved verbatim from the pre-refactor code even
    // though its result is unused. The original issueTokens lambda took a
    // `username` parameter but never read it; dropping the lookup would be a
    // behaviour change (it would also remove the "user row actually exists"
    // guard that today surfaces a DB error via the error callback if the row
    // is missing). Keeping it is the strictly-equivalent choice; the (void)
    // suppresses the unused-result warning.
    auto db = ::drogon::app().getDbClient();
    // Guard: runs inside the subject-mapping findBy's async success callback;
    // the caller's try/catch cannot reach it.
    try
    {
        Mapper<Users>(db).findBy(
          Criteria(Users::Cols::_id, CompareOperator::EQ, userId),
          [this, req, callbackPtr, userId](const std::vector<Users> &users) {
              (void)users;  // username unused; match original no-op-on-empty behaviour
              issueTokensForUser(req, callbackPtr, static_cast<int64_t>(userId));
          },
          [req, callbackPtr](const ::drogon::orm::DrogonDbException &e) {
              respondDbError(req, callbackPtr, "failed to fetch user", e);
          }
        );
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "GitHubController::linkExistingUser Mapper exception: " << e.what();
        respondDbError(req, callbackPtr, "failed to fetch user", e);
    }
    catch (...)
    {
        LOG_ERROR << "GitHubController::linkExistingUser Mapper unknown exception";
        respondError(req, callbackPtr, "DB_QUERY_ERROR", "github login: failed to fetch user");
    }
}

void GitHubController::createNewLinkedUser(
  const ::drogon::HttpRequestPtr &req,
  const CallbackPtr &callbackPtr,
  const std::string &githubLogin,
  const std::string &githubEmail,
  const std::string &provider,
  const std::string &subject
)
{
    // Step 4b: no mapping -- create local user, then subject mapping, then
    // default role, then issue tokens.
    auto db = ::drogon::app().getDbClient();
    std::string username = "gh_" + githubLogin;
    std::string passwordHash = ::authforge::drogon::utils::generateSecureToken();
    // Exemption (db-operations.md §3): INSERT...RETURNING to capture the
    // auto-generated user id for subsequent subject-mapping and role inserts.
    db->execSqlAsync(
      "INSERT INTO users (username, password_hash, salt, email, email_verified) "
      "VALUES ($1, $2, '', $3, true) "
      "ON CONFLICT (username) DO UPDATE SET email = EXCLUDED.email, "
      "email_verified = true "
      "RETURNING id",
      [this, req, callbackPtr, db, provider, subject, username](const ::drogon::orm::Result &userResult) {
          int32_t userId = userResult[0]["id"].as<int32_t>();
          // Create subject mapping.
          // Guard: runs inside the execSqlAsync success callback; caller's
          // try/catch cannot reach it.
          try
          {
              Oauth2SubjectMappings mapping;
              mapping.setSubject(subject);
              mapping.setInternalUserId(userId);
              mapping.setProvider(provider);
              Mapper<Oauth2SubjectMappings>(db).insert(
                mapping,
                [this, req, callbackPtr, db, userId, username](const Oauth2SubjectMappings &) {
                    // Assign default 'user' role. Mirroring the original code:
                    // both the success and error callbacks proceed to token
                    // issuance (best-effort role grant -- a role-insert failure
                    // is logged via the Mapper but must not block login).
                    //
                    // Guard: runs inside the subject-mapping insert's async
                    // success callback; caller's try/catch cannot reach it.
                    UserRoles ur;
                    ur.setUserId(userId);
                    try
                    {
                        Mapper<UserRoles>(db).insert(
                          ur,
                          [this, req, callbackPtr, userId, username](const UserRoles &) {
                              issueTokensForUser(req, callbackPtr, static_cast<int64_t>(userId));
                          },
                          [this, req, callbackPtr, userId](const ::drogon::orm::DrogonDbException &) {
                              // Best-effort: role grant failed, but the account
                              // exists and is linked -- proceed with login.
                              issueTokensForUser(req, callbackPtr, static_cast<int64_t>(userId));
                          }
                        );
                    }
                    catch (const std::exception &e)
                    {
                        LOG_ERROR << "GitHubController::createNewLinkedUser UserRoles Mapper exception: "
                                  << e.what();
                        // Best-effort, same as the async error path above.
                        issueTokensForUser(req, callbackPtr, static_cast<int64_t>(userId));
                    }
                    catch (...)
                    {
                        LOG_ERROR << "GitHubController::createNewLinkedUser UserRoles Mapper unknown exception";
                        issueTokensForUser(req, callbackPtr, static_cast<int64_t>(userId));
                    }
                },
                [req, callbackPtr](const ::drogon::orm::DrogonDbException &e) {
                    respondDbError(req, callbackPtr, "failed to link GitHub account", e);
                }
              );
          }
          catch (const std::exception &e)
          {
              LOG_ERROR << "GitHubController::createNewLinkedUser SubjectMappings Mapper exception: "
                        << e.what();
              respondDbError(req, callbackPtr, "failed to link GitHub account", e);
          }
          catch (...)
          {
              LOG_ERROR << "GitHubController::createNewLinkedUser SubjectMappings Mapper unknown exception";
              respondError(req, callbackPtr, "DB_QUERY_ERROR", "github login: failed to link GitHub account");
          }
      },
      [req, callbackPtr](const ::drogon::orm::DrogonDbException &e) {
          respondDbError(req, callbackPtr, "failed to create user account", e);
      },
      username,
      passwordHash,
      githubEmail
    );
}

}  // namespace authforge::drogon::controllers
