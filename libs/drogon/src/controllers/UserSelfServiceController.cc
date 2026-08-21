#include <authforge/drogon/controllers/UserSelfServiceController.h>
#include <authforge/storage/postgres/models/Oauth2AccessTokens.h>
#include <authforge/storage/postgres/models/Oauth2Clients.h>
#include <authforge/storage/postgres/models/Oauth2RefreshTokens.h>
#include <authforge/storage/postgres/models/Oauth2UserConsents.h>
#include <authforge/storage/postgres/models/Users.h>
#include <authforge/drogon/utils/PasswordHasher.h>
#include <authforge/drogon/utils/CryptoUtils.h>
#include <authforge/drogon/adapters/DrogonAuditSink.h>
#include <authforge/drogon/admin/UserAdminService.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <authforge/drogon/error/ErrorResponder.h>
#include <drogon/drogon.h>
#include <trantor/utils/Date.h>
#include <chrono>

#ifdef WITH_SOCIAL
// B2 social link/unlink: orchestration service + audit sink. The service is
// injected by IdentityAssembly (or SocialMockFixture in tests).
#include <authforge/identity/SocialLinkService.h>
#endif  // WITH_SOCIAL

namespace authforge::drogon::controllers
{

using namespace ::drogon::orm;
using namespace ::drogon_model::oauth2_db;

namespace
{
// Forward an ErrorResponder-built response through a shared callback. Application
// errors are emitted exclusively via the unified ErrorResponder entry point so
// every body is an Error Envelope (Requirement 7.1 / 7.3 / 7.5).
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

namespace openapi = ::authforge::drogon::observability::openapi;

// #43 resource-scope authorization: all /api/me routes require the OIDC
// `profile` scope. NO impliedBy -- a bare `admin` token does NOT satisfy
// these (a self-service request always needs an actual user token, per
// RFC 6749 §3.3 / OIDC Core §5.4).
openapi::EndpointInfo selfServiceEp(
  const char *path, const char *method, const char *summary, const char *description)
{
    openapi::EndpointInfo ep;
    ep.path = path;
    ep.method = method;
    ep.summary = summary;
    ep.description = description;
    ep.tags = {"User Profile"};
    ep.requiresAuth = true;
    ep.requiredScopes = {"profile"};
    // impliedBy intentionally empty (see comment above).
    return ep;
}
}  // namespace

void UserSelfServiceController::initApiDocs()
{
    static std::once_flag docsOnce;
    std::call_once(docsOnce, [] { initApiDocsImpl(); });
}

void UserSelfServiceController::initApiDocsImpl()
{
    openapi::OpenApiGenerator::addEndpoint(
      selfServiceEp("/api/me", "GET", "Get User Profile", "Get current user's profile information."));
    openapi::OpenApiGenerator::addEndpoint(
      selfServiceEp("/api/me", "DELETE", "Delete Account", "Soft-delete the current user's account."));
    openapi::OpenApiGenerator::addEndpoint(
      selfServiceEp("/api/me/password", "PUT", "Change Password", "Change the current user's password."));
    openapi::OpenApiGenerator::addEndpoint(selfServiceEp(
      "/api/me/authorized-apps", "GET", "List Authorized Apps",
      "List OAuth2 clients authorized by the current user."));
    openapi::OpenApiGenerator::addEndpoint(selfServiceEp(
      "/api/me/authorized-apps/{clientId}", "DELETE", "Revoke App Authorization",
      "Revoke the current user's authorization for a specific OAuth2 client."));
#ifdef WITH_SOCIAL
    openapi::OpenApiGenerator::addEndpoint(selfServiceEp(
      "/api/me/social/links", "GET", "List Linked Social Accounts",
      "List the social provider identities linked to the current user."));
    openapi::OpenApiGenerator::addEndpoint(selfServiceEp(
      "/api/me/social/links/{provider}", "POST", "Link Social Account",
      "Verify a provider authorization code and link that provider identity "
      "to the current user."));
    openapi::OpenApiGenerator::addEndpoint(selfServiceEp(
      "/api/me/social/links/{provider}", "DELETE", "Unlink Social Account",
      "Remove the current user's linked identity for a provider."));
#endif  // WITH_SOCIAL
}

void UserSelfServiceController::getProfile(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();
        // Task B5: replaced raw SQL with Mapper<Users>. #54: findBy (not
        // findOne) so "user missing/soft-deleted" (404) is distinguishable
        // from a DB exception (DB_QUERY_ERROR); deleted users are excluded
        // from all queries per the V024 soft-delete contract.
        Criteria crit(Users::Cols::_public_sub, CompareOperator::EQ, userId);
        Criteria deletedCrit(Users::Cols::_deleted_at, CompareOperator::IsNull);
        try
        {
            Mapper<Users>(db).findBy(
              crit && deletedCrit,
              [sharedCb, req](const std::vector<Users> &users) {
                  if (users.empty())
                  {
                      respondError(
                        req,
                        sharedCb,
                        "VALIDATION_RESOURCE_NOT_FOUND",
                        "getProfile: user not found"
                      );
                      return;
                  }
                  const Users &user = users[0];
                  Json::Value json;
                  json["username"] = user.getValueOfUsername();
                  json["email"] = user.getValueOfEmail().empty() ? "" : user.getValueOfEmail();
                  json["email_verified"] = user.getValueOfEmailVerified();
                  json["mfa_enabled"] = user.getValueOfMfaEnabled();
                  auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                  (*sharedCb)(resp);
              },
              [sharedCb, req](const DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("getProfile failed: ") + e.base().what()
                  );
              }
            );
        }
        catch (...)
        {
            respondError(req, sharedCb, "DB_QUERY_ERROR", "getProfile: Mapper construction failed");
        }
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "getProfile: database unavailable");
    }
}

void UserSelfServiceController::changePassword(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    // Parse JSON body
    auto jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        respondError(
          req, sharedCb, "VALIDATION_INVALID_INPUT", "changePassword: JSON body is required"
        );
        return;
    }

    std::string oldPassword = jsonBody->get("old_password", "").asString();
    std::string newPassword = jsonBody->get("new_password", "").asString();

    if (oldPassword.empty() || newPassword.empty())
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "changePassword: old_password and new_password are required"
        );
        return;
    }

    if (newPassword.length() < 8)
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_FORMAT_ERROR",
          "changePassword: new password must be at least 8 characters"
        );
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();
        // Task B5: replaced raw SQL with Mapper<Users>. #54: findBy + deleted
        // filter — a soft-deleted user's token must not mutate their row
        // (V024: excluded from all queries); missing/deleted → 404.
        Criteria pwCrit(Users::Cols::_public_sub, CompareOperator::EQ, userId);
        Criteria deletedCrit(Users::Cols::_deleted_at, CompareOperator::IsNull);
        try
        {
            Mapper<Users>(db).findBy(
              pwCrit && deletedCrit,
              [sharedCb, oldPassword, newPassword, userId, req, db](const std::vector<Users> &users) {
                  if (users.empty())
                  {
                      respondError(
                        req,
                        sharedCb,
                        "VALIDATION_RESOURCE_NOT_FOUND",
                        "changePassword: user not found"
                      );
                      return;
                  }
                  const Users &user = users[0];
              std::string storedHash = user.getValueOfPasswordHash();
              std::string salt = user.getValueOfSalt();

              // Verify old password
              if (!::authforge::common::utils::PasswordHasher::verify(
                    oldPassword, storedHash, salt
                  ))
              {
                  ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                    ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                    "password_change_failed",
                    "failure",
                    req,
                    userId,
                    "user",
                    userId
                  );
                  respondError(
                    req,
                    sharedCb,
                    "AUTH_INVALID_CREDENTIALS",
                    "changePassword: current password is incorrect"
                  );
                  return;
              }

              // Hash new password
              std::string newHash;
              try
              {
                  newHash = ::authforge::common::utils::PasswordHasher::hash(newPassword);
              }
              catch (const std::exception &e)
              {
                  respondError(
                    req,
                    sharedCb,
                    "INTERNAL_ERROR",
                    std::string("Password hashing failed: ") + e.what()
                  );
                  return;
              }

              // Task B5: replaced UPDATE password with Mapper<Users>
              Users updatedUser = user;
              updatedUser.setPasswordHash(newHash);
              updatedUser.setSalt("");
              auto db2 = ::drogon::app().getDbClient();
              try
              {
                  Mapper<Users>(db2).update(
                updatedUser,
                [sharedCb, userId, req, db2](const size_t) {
                    // Exemption (db-operations.md §3): Security-critical
                    // cascade revoke of ALL tokens for a user on password
                    // change. Splitting into individual Mapper updates per
                    // token would be O(n) round-trips with no benefit.
                    // Revoke all access tokens for this user
                    db2->execSqlAsync(
                      "UPDATE oauth2_access_tokens SET revoked = true WHERE user_id = $1",
                      [sharedCb, userId, req, db2](const ::drogon::orm::Result &) {
                          // Revoke all refresh tokens for this user
                          db2->execSqlAsync(
                            "UPDATE oauth2_refresh_tokens SET revoked = true WHERE user_id = $1",
                            [sharedCb, userId, req](const ::drogon::orm::Result &) {
                                ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                                  ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                                  "password_changed",
                                  "success",
                                  req,
                                  userId,
                                  "user",
                                  userId
                                );
                                Json::Value json;
                                json["message"] = "Password changed successfully";
                                json["note"] = "All existing sessions have been revoked";
                                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                                (*sharedCb)(resp);
                            },
                            [sharedCb, userId, req](const ::drogon::orm::DrogonDbException &) {
                                ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                                  ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                                  "password_changed",
                                  "success",
                                  req,
                                  userId,
                                  "user",
                                  userId
                                );
                                Json::Value json;
                                json["message"] = "Password changed successfully";
                                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                                (*sharedCb)(resp);
                            },
                            userId
                          );
                      },
                      [sharedCb, userId, req, db2](const ::drogon::orm::DrogonDbException &) {
                          db2->execSqlAsync(
                            "UPDATE oauth2_refresh_tokens SET revoked = true WHERE user_id = $1",
                            [sharedCb, userId, req](const ::drogon::orm::Result &) {
                                ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                                  ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                                  "password_changed",
                                  "success",
                                  req,
                                  userId,
                                  "user",
                                  userId
                                );
                                Json::Value json;
                                json["message"] = "Password changed successfully";
                                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                                (*sharedCb)(resp);
                            },
                            [sharedCb, userId, req](const ::drogon::orm::DrogonDbException &) {
                                ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                                  ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                                  "password_changed",
                                  "success",
                                  req,
                                  userId,
                                  "user",
                                  userId
                                );
                                Json::Value json;
                                json["message"] = "Password changed successfully";
                                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                                (*sharedCb)(resp);
                            },
                            userId
                          );
                      },
                      userId
                    );
                },
                [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                    respondError(
                      req,
                      sharedCb,
                      "DB_QUERY_ERROR",
                      std::string("Password update failed: ") + e.base().what()
                    );
                }
              );
              }
              catch (...)
              {
                  respondError(
                    req, sharedCb, "DB_QUERY_ERROR",
                    "changePassword: update Mapper construction failed"
                  );
                  return;
              }
          },
          [sharedCb, req](const DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("changePassword lookup failed: ") + e.base().what()
              );
          }
        );
        }
        catch (...)
        {
            respondError(
              req, sharedCb, "DB_QUERY_ERROR", "changePassword: find Mapper construction failed"
            );
        }
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "changePassword: database unavailable");
    }
}

void UserSelfServiceController::listAuthorizedApps(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();
        // Split 3-way JOIN: resolve public_sub → user → consents → clients
        // #54: deleted_at filter — deleted users are excluded from all
        // queries (V024); a deleted/missing user is a 404, not an empty
        // app list.
        try
        {
            Mapper<Users>(db).findBy(
              Criteria(Users::Cols::_public_sub, CompareOperator::EQ, userId) &&
                Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull),
              [sharedCb, db, req](const std::vector<Users> &users) {
                  if (users.empty())
                  {
                      respondError(
                        req,
                        sharedCb,
                        "VALIDATION_RESOURCE_NOT_FOUND",
                        "listAuthorizedApps: user not found"
                      );
                      return;
                  }
                  int32_t internalId = users[0].getValueOfId();

                  try
                  {
                      Mapper<Oauth2UserConsents>(db).findBy(
                        Criteria(
                          Oauth2UserConsents::Cols::_internal_user_id, CompareOperator::EQ, internalId
                        ),
                        [sharedCb, db, req](const std::vector<Oauth2UserConsents> &consents) {
                            // Collect distinct client_ids for batch clients query
                            std::set<std::string> clientIdSet;
                            for (const auto &uc : consents)
                                clientIdSet.insert(uc.getValueOfClientId());

                            if (clientIdSet.empty())
                            {
                                Json::Value json;
                                json["authorized_apps"] = Json::Value(Json::arrayValue);
                                json["total"] = 0;
                                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                                (*sharedCb)(resp);
                                return;
                            }

                            std::vector<std::string> clientIds(clientIdSet.begin(), clientIdSet.end());
                            try
                            {
                                Mapper<Oauth2Clients>(db).findBy(
                                  Criteria(Oauth2Clients::Cols::_client_id, CompareOperator::In, clientIds),
                                  [sharedCb](const std::vector<Oauth2Clients> &clients) {
                                      Json::Value json;
                                      Json::Value apps(Json::arrayValue);
                                      for (const auto &c : clients)
                                      {
                                          Json::Value app;
                                          app["client_id"] = c.getValueOfClientId();
                                          app["name"] = c.getValueOfName();
                                          apps.append(app);
                                      }
                                      json["authorized_apps"] = apps;
                                      json["total"] = static_cast<int>(apps.size());
                                      (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                                  },
                                  [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                                      respondError(
                                        req,
                                        sharedCb,
                                        "DB_QUERY_ERROR",
                                        std::string("listAuthorizedApps failed: ") + e.base().what()
                                      );
                                  }
                                );
                            }
                            catch (...)
                            {
                                respondError(
                                  req, sharedCb, "DB_QUERY_ERROR",
                                  "listAuthorizedApps: clients Mapper construction failed"
                                );
                            }
                        },
                        [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                            respondError(
                              req,
                              sharedCb,
                              "DB_QUERY_ERROR",
                              std::string("listAuthorizedApps failed: ") + e.base().what()
                            );
                        }
                      );
                  }
                  catch (...)
                  {
                      respondError(
                        req, sharedCb, "DB_QUERY_ERROR",
                        "listAuthorizedApps: consents Mapper construction failed"
                      );
                  }
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("listAuthorizedApps failed: ") + e.base().what()
                  );
              }
            );
        }
        catch (...)
        {
            respondError(
              req, sharedCb, "DB_QUERY_ERROR",
              "listAuthorizedApps: users Mapper construction failed"
            );
        }
    }
    catch (...)
    {
        respondError(
          req, sharedCb, "DB_CONNECTION_ERROR", "listAuthorizedApps: database unavailable"
        );
    }
}

void UserSelfServiceController::revokeAuthorizedApp(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &clientId
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (clientId.empty())
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "revokeAuthorizedApp: clientId is required"
        );
        return;
    }

    try
    {
        auto db = ::drogon::app().getDbClient();

        // First get the internal user id. #54: deleted_at filter — deleted
        // users are excluded from all queries (V024).
        try
        {
            Mapper<Users>(db).findBy(
              Criteria(Users::Cols::_public_sub, CompareOperator::EQ, userId) &&
                Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull),
              [sharedCb, userId, clientId, req, db](const std::vector<Users> &users) {
                  if (users.empty())
                  {
                      respondError(
                        req,
                        sharedCb,
                        "VALIDATION_RESOURCE_NOT_FOUND",
                        "revokeAuthorizedApp: user not found"
                      );
                      return;
                  }

                  int32_t internalUserId = users[0].getValueOfId();

                  // Delete consents
                  try
                  {
                      Mapper<Oauth2UserConsents>(db).deleteBy(
                Criteria(
                  Oauth2UserConsents::Cols::_internal_user_id, CompareOperator::EQ, internalUserId
                ) &&
                  Criteria(Oauth2UserConsents::Cols::_client_id, CompareOperator::EQ, clientId),
                [sharedCb, userId, clientId, req, db](const size_t) {
                    // Exemption (db-operations.md §3): Security-critical batch
                    // revoke of ALL tokens for user+client.  Per-token
                    // Mapper::update would be O(n) round-trips with no benefit.
                    db->execSqlAsync(
                      "UPDATE oauth2_access_tokens SET revoked = true "
                      "WHERE user_id = $1 AND client_id = $2",
                      [sharedCb, userId, clientId, req](const ::drogon::orm::Result &) {
                          ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                            ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                            "app_authorization_revoked",
                            "success",
                            req,
                            userId,
                            "client",
                            clientId
                          );
                          Json::Value json;
                          json["message"] = "Authorization revoked successfully";
                          json["client_id"] = clientId;
                          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                          (*sharedCb)(resp);
                      },
                      [sharedCb, userId, clientId, req](const ::drogon::orm::DrogonDbException &) {
                          ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                            ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                            "app_authorization_revoked",
                            "success",
                            req,
                            userId,
                            "client",
                            clientId
                          );
                          Json::Value json;
                          json["message"] = "Authorization revoked successfully";
                          json["client_id"] = clientId;
                          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                          (*sharedCb)(resp);
                      },
                      userId,
                      clientId
                    );
                },
                [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                    respondError(
                      req,
                      sharedCb,
                      "DB_QUERY_ERROR",
                      std::string("revokeAuthorizedApp: consent DELETE failed: ") + e.base().what()
                    );
                }
                      );
                  }
                  catch (...)
                  {
                      respondError(
                        req, sharedCb, "DB_QUERY_ERROR",
                        "revokeAuthorizedApp: consents Mapper construction failed"
                      );
                  }
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("revokeAuthorizedApp: user lookup failed: ") + e.base().what()
                  );
              }
            );
        }
        catch (...)
        {
            respondError(
              req, sharedCb, "DB_QUERY_ERROR",
              "revokeAuthorizedApp: users Mapper construction failed"
            );
        }
    }
    catch (...)
    {
        respondError(
          req, sharedCb, "DB_CONNECTION_ERROR", "revokeAuthorizedApp: database unavailable"
        );
    }
}

void UserSelfServiceController::deleteAccount(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    try
    {
        auto db = ::drogon::app().getDbClient();

        // #54: resolve the acting user first (deleted_at IS NULL — a deleted
        // account cannot be deleted again), then enforce the last-admin guard
        // (a sole active admin deleting their own account is a management-
        // plane lockout), then revoke + soft-delete.
        try
        {
            Mapper<Users>(db).findBy(
              Criteria(Users::Cols::_public_sub, CompareOperator::EQ, userId) &&
                Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull),
              [sharedCb, userId, req, db](const std::vector<Users> &users) {
                  if (users.empty())
                  {
                      respondError(
                        req,
                        sharedCb,
                        "VALIDATION_RESOURCE_NOT_FOUND",
                        "deleteAccount: user not found"
                      );
                      return;
                  }
                  int32_t internalId = users[0].getValueOfId();

                  auto proceedWithDelete = [sharedCb, userId, req, db, internalId]() {
                      // Helper: anonymize + SOFT-DELETE the user record. V024
                      // contract: a deleted user is excluded from all queries
                      // and can no longer log in — anonymization alone (the
                      // old behavior) left a live row with a garbage username.
                      auto anonymizeAndSoftDelete =
                        [sharedCb, userId, req, internalId](
                          ::drogon::orm::DbClientPtr dbClient, const std::string &anonUsername
                        ) {
                            try
                            {
                                Mapper<Users>(dbClient).findBy(
                                  Criteria(Users::Cols::_public_sub, CompareOperator::EQ, userId) &&
                                    Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull),
                                  [sharedCb, userId, req, anonUsername, dbClient](
                                    const std::vector<Users> &users
                                  ) {
                                      if (users.empty())
                                      {
                                          respondError(
                                            req,
                                            sharedCb,
                                            "VALIDATION_RESOURCE_NOT_FOUND",
                                            "deleteAccount: user not found"
                                          );
                                          return;
                                      }
                                      Users u = users[0];
                                      u.setUsername(anonUsername);
                                      u.setEmailToNull();
                                      u.setPasswordHash("DELETED");
                                      u.setDeletedAt(::trantor::Date::now());
                                      try
                                      {
                                          Mapper<Users>(dbClient).update(
                                            u,
                                            [sharedCb, userId, req](const size_t) {
                                                ::authforge::drogon::adapters::DrogonAuditSink::
                                                  logFromRequest(
                                                    ::drogon::app()
                                                      .getPlugin<::OAuth2Plugin>()
                                                      ->getAuditSink(),
                                                    "account_deleted",
                                                    "success",
                                                    req,
                                                    userId,
                                                    "user",
                                                    userId
                                                  );
                                                Json::Value json;
                                                json["message"] = "Account deleted successfully";
                                                auto resp =
                                                  ::drogon::HttpResponse::newHttpJsonResponse(json);
                                                (*sharedCb)(resp);
                                            },
                                            [sharedCb, req](
                                              const ::drogon::orm::DrogonDbException &e
                                            ) {
                                                respondError(
                                                  req,
                                                  sharedCb,
                                                  "DB_QUERY_ERROR",
                                                  std::string("deleteAccount user update failed: ")
                                                    + e.base().what()
                                                );
                                            }
                                          );
                                      }
                                      catch (...)
                                      {
                                          respondError(
                                            req, sharedCb, "DB_QUERY_ERROR",
                                            "deleteAccount: update Mapper construction failed"
                                          );
                                      }
                                  },
                                  [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                                      respondError(
                                        req,
                                        sharedCb,
                                        "DB_QUERY_ERROR",
                                        std::string("deleteAccount user lookup failed: ")
                                          + e.base().what()
                                      );
                                  }
                                );
                            }
                            catch (...)
                            {
                                respondError(
                                  req, sharedCb, "DB_QUERY_ERROR",
                                  "deleteAccount: lookup Mapper construction failed"
                                );
                            }
                        };

                      auto makeAnonUsername = []() {
                          auto timestamp = std::to_string(
                            std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch()
                            )
                              .count()
                          );
                          return "deleted_" + timestamp;
                      };

                      // Exemption (db-operations.md §3): Security-critical cascade revoke
                      // of ALL tokens for a user on account deletion. Bulk UPDATE is the
                      // only correct approach — splitting into per-token Mapper::update
                      // would be O(n) round-trips with no benefit.
                      //
                      // Dual key (#54/#56): password-flow tokens store the public
                      // sub in user_id; social-flow tokens store the internal id.
                      // Revocation stays best-effort (anonymization proceeds on
                      // failure) but failures are now LOG_ERROR'd instead of
                      // silently swallowed.
                      db->execSqlAsync(
                        "UPDATE oauth2_access_tokens SET revoked = true "
                        "WHERE user_id = $1 OR user_id = $2",
                        [db, userId, internalId, anonymizeAndSoftDelete, makeAnonUsername](
                          const ::drogon::orm::Result &
                        ) {
                            db->execSqlAsync(
                              "UPDATE oauth2_refresh_tokens SET revoked = true "
                              "WHERE user_id = $1 OR user_id = $2",
                              [db, anonymizeAndSoftDelete, makeAnonUsername](
                                const ::drogon::orm::Result &
                              ) {
                                  anonymizeAndSoftDelete(db, makeAnonUsername());
                              },
                              [db, anonymizeAndSoftDelete, makeAnonUsername](
                                const ::drogon::orm::DrogonDbException &e
                              ) {
                                  LOG_ERROR << "deleteAccount: refresh-token revocation failed: "
                                            << e.base().what();
                                  anonymizeAndSoftDelete(db, makeAnonUsername());
                              },
                              userId,
                              std::to_string(internalId)
                            );
                        },
                        [db, anonymizeAndSoftDelete, makeAnonUsername](
                          const ::drogon::orm::DrogonDbException &e
                        ) {
                            LOG_ERROR << "deleteAccount: access-token revocation failed: "
                                      << e.base().what();
                            anonymizeAndSoftDelete(db, makeAnonUsername());
                        },
                        userId,
                        std::to_string(internalId)
                      );
                  };

                  // Last-admin guard (#60 item 2 / #54 review F3): without
                  // this, the sole active admin could delete their own
                  // account via /api/me and lock out the management plane.
                  ::authforge::drogon::admin::isLastActiveAdmin(
                    db,
                    internalId,
                    [proceedWithDelete, sharedCb, req](bool lastAdmin) {
                        if (lastAdmin)
                        {
                            respondError(
                              req,
                              sharedCb,
                              "VALIDATION_RESOURCE_CONFLICT",
                              "Cannot delete the last active admin account"
                            );
                            return;
                        }
                        proceedWithDelete();
                    },
                    [sharedCb, req]() {
                        respondError(
                          req, sharedCb, "DB_QUERY_ERROR",
                          "deleteAccount: failed to evaluate last-admin guard"
                        );
                    }
                  );
              },
              [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                  respondError(
                    req,
                    sharedCb,
                    "DB_QUERY_ERROR",
                    std::string("deleteAccount user lookup failed: ") + e.base().what()
                  );
              }
            );
        }
        catch (...)
        {
            respondError(
              req, sharedCb, "DB_QUERY_ERROR", "deleteAccount: lookup Mapper construction failed"
            );
        }
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "deleteAccount: database unavailable");
    }
}

// ---------------------------------------------------------------------------
// B2 social link/unlink (design doc §3/§4.5). All three handlers follow the
// same shape: fast pre-validation (no DB), numeric-dispatch user resolution,
// then the injected SocialLinkService does the orchestration.
// ---------------------------------------------------------------------------
#ifdef WITH_SOCIAL

namespace
{
// Mirror of OAuth2Plugin's userinfo dispatch (#54/#56 dual-key): password-flow
// tokens carry users.public_sub in the `userId` attribute; GitHub social-flow
// tokens carry the INTERNAL id as a string. Without the numeric branch, the
// primary persona of this feature (social-created users managing their own
// links) would 404 on every call. Both paths enforce the V024 soft-delete
// contract (deleted_at IS NULL).
using InternalUserCallback = std::function<void(bool found, int32_t internalId)>;

void resolveInternalUserId(
  const ::drogon::orm::DbClientPtr &db,
  const std::string &userId,
  const ::drogon::HttpRequestPtr &req,
  const std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>> &sharedCb,
  InternalUserCallback &&onResolved
)
{
    bool isNumeric = false;
    int32_t numericId = 0;
    try
    {
        size_t pos = 0;
        int parsed = std::stoi(userId, &pos);
        isNumeric = (pos == userId.length());
        if (isNumeric)
        {
            numericId = parsed;
        }
    }
    catch (...)
    {
        isNumeric = false;
    }

    try
    {
        Mapper<Users>(db).findBy(
          (isNumeric ? Criteria(Users::Cols::_id, CompareOperator::EQ, numericId)
                     : Criteria(Users::Cols::_public_sub, CompareOperator::EQ, userId)) &&
            Criteria(Users::Cols::_deleted_at, CompareOperator::IsNull),
          [sharedCb = sharedCb, req, onResolved = std::move(onResolved)](
            const std::vector<Users> &users) mutable {
              if (users.empty())
              {
                  respondError(
                    req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND",
                    "social links: user not found"
                  );
                  return;
              }
              onResolved(true, users[0].getValueOfId());
          },
          [sharedCb = sharedCb, req](const DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("social links: user lookup failed: ") + e.base().what()
              );
          }
        );
    }
    catch (...)
    {
        respondError(
          req, sharedCb, "DB_QUERY_ERROR", "social links: user lookup Mapper construction failed"
        );
    }
}

// SocialLinkOpStatus -> Error Envelope (design §3.4). Returns false when the
// status was an error (response sent); true for Ok (caller builds the 200).
bool respondLinkOpError(
  const ::drogon::HttpRequestPtr &req,
  const std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>> &sharedCb,
  const ::authforge::identity::SocialLinkOpResult &result,
  const std::string &provider
)
{
    using ::authforge::identity::SocialLinkOpStatus;
    switch (result.status)
    {
    case SocialLinkOpStatus::Ok:
        return false;
    case SocialLinkOpStatus::InvalidProvider:
        respondError(
          req, sharedCb, "VALIDATION_INVALID_INPUT",
          "social links: unsupported provider '" + provider + "'"
        );
        return true;
    case SocialLinkOpStatus::NotConfigured:
        respondError(
          req, sharedCb, "INTERNAL_ERROR",
          "social links: social linking is not configured"
        );
        return true;
    case SocialLinkOpStatus::ExchangeFailed:
        // Provider-level code (NET_CONNECTION_FAILED -> 502,
        // VALIDATION_INVALID_INPUT -> 400, ...) passes straight through the
        // catalog's category mapping.
        respondError(
          req, sharedCb, result.errorCode.empty() ? "NET_CONNECTION_FAILED" : result.errorCode,
          "social links: provider code verification failed"
        );
        return true;
    case SocialLinkOpStatus::AlreadyLinkedToSelf:
        respondError(
          req, sharedCb, "VALIDATION_RESOURCE_CONFLICT",
          "social links: this " + provider + " account is already linked to your account"
        );
        return true;
    case SocialLinkOpStatus::AlreadyLinkedToOtherUser:
        // Fixed wording -- no information about WHO owns the mapping.
        respondError(
          req, sharedCb, "VALIDATION_RESOURCE_CONFLICT",
          "social links: this " + provider + " account is already linked to another user"
        );
        return true;
    case SocialLinkOpStatus::ProviderConflictForUser:
        respondError(
          req, sharedCb, "VALIDATION_RESOURCE_CONFLICT",
          "social links: a different " + provider +
            " account is linked; unlink it before linking a new one"
        );
        return true;
    case SocialLinkOpStatus::NoLink:
        respondError(
          req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND",
          "social links: no linked " + provider + " account"
        );
        return true;
    case SocialLinkOpStatus::LastCredentialGuard:
        respondError(
          req, sharedCb, "VALIDATION_RESOURCE_CONFLICT",
          "social links: cannot remove your last sign-in method (set a password first)"
        );
        return true;
    case SocialLinkOpStatus::RepositoryError:
        respondError(req, sharedCb, "DB_QUERY_ERROR", "social links: repository failure");
        return true;
    }
    return false;  // unreachable; silences -Wreturn-type on some toolchains
}
}  // namespace

void UserSelfServiceController::listSocialLinks(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (!socialLinkService_)
    {
        respondError(
          req, sharedCb, "INTERNAL_ERROR", "social links: social linking is not configured"
        );
        return;
    }
    auto service = socialLinkService_;

    try
    {
        auto db = ::drogon::app().getDbClient();
        resolveInternalUserId(
          db,
          userId,
          req,
          sharedCb,
          [service, sharedCb, req](bool, int32_t internalId) mutable {
              service->listAccounts(
                internalId,
                [sharedCb, req](::authforge::identity::SocialLinkOpStatus status,
                           std::vector<::authforge::identity::SocialLinkEntry> entries) mutable {
                    if (status != ::authforge::identity::SocialLinkOpStatus::Ok)
                    {
                        respondError(req, sharedCb, "DB_QUERY_ERROR", "social links: repository failure");
                        return;
                    }
                    Json::Value json;
                    Json::Value links(Json::arrayValue);
                    for (const auto &e : entries)
                    {
                        Json::Value item;
                        item["provider"] = e.provider;
                        item["subject"] = e.subject;
                        item["linked_at"] = e.linkedAt;
                        links.append(item);
                    }
                    json["social_links"] = links;
                    json["total"] = static_cast<int>(links.size());
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                }
              );
          }
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "social links: database unavailable");
    }
}

void UserSelfServiceController::linkSocialAccount(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &provider
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (!socialLinkService_)
    {
        respondError(
          req, sharedCb, "INTERNAL_ERROR", "social links: social linking is not configured"
        );
        return;
    }
    if (!::authforge::identity::SocialLinkService::isValidProvider(provider))
    {
        respondError(
          req, sharedCb, "VALIDATION_INVALID_INPUT",
          "social links: unsupported provider '" + provider + "'"
        );
        return;
    }
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
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD",
          "social links: code is required"
        );
        return;
    }

    auto service = socialLinkService_;
    try
    {
        auto db = ::drogon::app().getDbClient();
        resolveInternalUserId(
          db,
          userId,
          req,
          sharedCb,
          [service, sharedCb, provider, code, userId, req](bool, int32_t internalId) mutable {
              service->linkAccount(
                provider,
                code,
                internalId,
                [sharedCb, req, userId, provider](
                  ::authforge::identity::SocialLinkOpResult result) mutable {
                    auto plugin =
                      ::drogon::app().getPlugin<::OAuth2Plugin>();
                    if (plugin)
                    {
                        ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                          plugin->getAuditSink(),
                          result.status == ::authforge::identity::SocialLinkOpStatus::Ok
                            ? "social_account_linked"
                            : "social_account_link_failed",
                          result.status == ::authforge::identity::SocialLinkOpStatus::Ok
                            ? "success"
                            : "failure",
                          req,
                          userId,
                          "user",
                          userId,
                          [&result, &provider]() {
                              Json::Value details;
                              details["provider"] = result.entry.provider.empty()
                                                      ? provider
                                                      : result.entry.provider;
                              if (!result.entry.subject.empty())
                              {
                                  details["subject"] = result.entry.subject;
                              }
                              return details;
                          }()
                        );
                    }
                    if (respondLinkOpError(req, sharedCb, result, provider))
                    {
                        return;
                    }
                    Json::Value json;
                    json["provider"] = result.entry.provider;
                    json["subject"] = result.entry.subject;
                    json["message"] = "Social account linked successfully";
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                }
              );
          }
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "social links: database unavailable");
    }
}

void UserSelfServiceController::unlinkSocialAccount(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &provider
)
{
    std::string userId = req->getAttributes()->get<std::string>("userId");
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    if (!socialLinkService_)
    {
        respondError(
          req, sharedCb, "INTERNAL_ERROR", "social links: social linking is not configured"
        );
        return;
    }
    if (!::authforge::identity::SocialLinkService::isValidProvider(provider))
    {
        respondError(
          req, sharedCb, "VALIDATION_INVALID_INPUT",
          "social links: unsupported provider '" + provider + "'"
        );
        return;
    }

    auto service = socialLinkService_;
    try
    {
        auto db = ::drogon::app().getDbClient();
        resolveInternalUserId(
          db,
          userId,
          req,
          sharedCb,
          [service, sharedCb, provider, userId, req](bool, int32_t internalId) mutable {
              service->unlinkAccount(
                provider,
                internalId,
                [sharedCb, req, userId, provider](
                  ::authforge::identity::SocialLinkOpResult result) mutable {
                    auto plugin =
                      ::drogon::app().getPlugin<::OAuth2Plugin>();
                    if (plugin)
                    {
                        ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                          plugin->getAuditSink(),
                          result.status == ::authforge::identity::SocialLinkOpStatus::Ok
                            ? "social_account_unlinked"
                            : "social_account_unlink_blocked",
                          result.status == ::authforge::identity::SocialLinkOpStatus::Ok
                            ? "success"
                            : "failure",
                          req,
                          userId,
                          "user",
                          userId,
                          [&provider]() {
                              Json::Value details;
                              details["provider"] = provider;
                              return details;
                          }()
                        );
                    }
                    if (respondLinkOpError(req, sharedCb, result, provider))
                    {
                        return;
                    }
                    Json::Value json;
                    json["provider"] = result.entry.provider;
                    json["message"] = "Social account unlinked successfully";
                    (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                }
              );
          }
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "social links: database unavailable");
    }
}

#endif  // WITH_SOCIAL

}  // namespace authforge::drogon::controllers
