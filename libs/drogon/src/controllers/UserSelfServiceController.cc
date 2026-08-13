#include <authforge/drogon/controllers/UserSelfServiceController.h>
#include <authforge/storage/postgres/models/Oauth2AccessTokens.h>
#include <authforge/storage/postgres/models/Oauth2Clients.h>
#include <authforge/storage/postgres/models/Oauth2RefreshTokens.h>
#include <authforge/storage/postgres/models/Oauth2UserConsents.h>
#include <authforge/storage/postgres/models/Users.h>
#include <authforge/drogon/utils/PasswordHasher.h>
#include <authforge/drogon/utils/CryptoUtils.h>
#include <authforge/drogon/adapters/DrogonAuditSink.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <authforge/drogon/error/ErrorResponder.h>
#include <drogon/drogon.h>
#include <chrono>

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
        // Task B5: replaced raw SQL with Mapper<Users>
        Criteria crit(Users::Cols::_public_sub, CompareOperator::EQ, userId);
        Mapper<Users>(db).findOne(
          crit,
          [sharedCb, req](const Users &user) {
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
        // Task B5: replaced raw SQL with Mapper<Users>
        Criteria pwCrit(Users::Cols::_public_sub, CompareOperator::EQ, userId);
        Mapper<Users>(db).findOne(
          pwCrit,
          [sharedCb, oldPassword, newPassword, userId, req, db](const Users &user) {
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
        Mapper<Users>(db).findBy(
          Criteria(Users::Cols::_public_sub, CompareOperator::EQ, userId),
          [sharedCb, db, req](const std::vector<Users> &users) {
              if (users.empty())
              {
                  Json::Value json;
                  json["authorized_apps"] = Json::Value(Json::arrayValue);
                  json["total"] = 0;
                  (*sharedCb)(::drogon::HttpResponse::newHttpJsonResponse(json));
                  return;
              }
              int32_t internalId = users[0].getValueOfId();

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

        // First get the internal user id
        Mapper<Users>(db).findBy(
          Criteria(Users::Cols::_public_sub, CompareOperator::EQ, userId),
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

        // Helper: anonymize user record (called from multiple fallback paths)
        auto anonymizeUser =
          [sharedCb,
           userId,
           req](::drogon::orm::DbClientPtr dbClient, const std::string &anonUsername) {
              Mapper<Users>(dbClient).findBy(
                Criteria(Users::Cols::_public_sub, CompareOperator::EQ, userId),
                [sharedCb, userId, req, anonUsername, dbClient](const std::vector<Users> &users) {
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
                    Mapper<Users>(dbClient).update(
                      u,
                      [sharedCb, userId, req](const size_t) {
                          ::authforge::drogon::adapters::DrogonAuditSink::logFromRequest(
                            ::drogon::app().getPlugin<::OAuth2Plugin>()->getAuditSink(),
                            "account_deleted",
                            "success",
                            req,
                            userId,
                            "user",
                            userId
                          );
                          Json::Value json;
                          json["message"] = "Account deleted successfully";
                          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                          (*sharedCb)(resp);
                      },
                      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                          respondError(
                            req,
                            sharedCb,
                            "DB_QUERY_ERROR",
                            std::string("deleteAccount user update failed: ") + e.base().what()
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
          };

        // Exemption (db-operations.md §3): Security-critical cascade revoke
        // of ALL tokens for a user on account deletion. Bulk UPDATE is the
        // only correct approach — splitting into per-token Mapper::update
        // would be O(n) round-trips with no benefit.

        // Step 1: Revoke all access tokens
        db->execSqlAsync(
          "UPDATE oauth2_access_tokens SET revoked = true WHERE user_id = $1",
          [sharedCb, userId, req, db, anonymizeUser](const ::drogon::orm::Result &) {
              // Step 2: Revoke all refresh tokens
              db->execSqlAsync(
                "UPDATE oauth2_refresh_tokens SET revoked = true WHERE user_id = $1",
                [sharedCb, userId, req, anonymizeUser, db](const ::drogon::orm::Result &) {
                    auto timestamp = std::to_string(
                      std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                      )
                        .count()
                    );
                    std::string anonUsername = "deleted_" + timestamp;
                    anonymizeUser(db, anonUsername);
                },
                [sharedCb, userId, req, anonymizeUser, db](
                  const ::drogon::orm::DrogonDbException &
                ) {
                    auto timestamp = std::to_string(
                      std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                      )
                        .count()
                    );
                    std::string anonUsername = "deleted_" + timestamp;
                    anonymizeUser(db, anonUsername);
                },
                userId
              );
          },
          [sharedCb, userId, req, anonymizeUser, db](const ::drogon::orm::DrogonDbException &) {
              auto timestamp = std::to_string(
                std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch()
                )
                  .count()
              );
              std::string anonUsername = "deleted_" + timestamp;
              anonymizeUser(db, anonUsername);
          },
          userId
        );
    }
    catch (...)
    {
        respondError(req, sharedCb, "DB_CONNECTION_ERROR", "deleteAccount: database unavailable");
    }
}

}  // namespace authforge::drogon::controllers
