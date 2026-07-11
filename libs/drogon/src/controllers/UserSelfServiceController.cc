#include <authforge/drogon/controllers/UserSelfServiceController.h>
#include <oauth2/utils/PasswordHasher.h>
#include <oauth2/utils/CryptoUtils.h>
#include <oauth2/observability/AuditLogger.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <drogon/drogon.h>
#include <chrono>

namespace authforge::drogon::controllers
{

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
    ::common::error::ErrorResponder::respond(
      req,
      [cb](const ::drogon::HttpResponsePtr &r) { (*cb)(r); },
      std::move(code),
      std::move(detailForLog)
    );
}

struct UserSelfServiceControllerDocs
{
    UserSelfServiceControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo getProfile;
        getProfile.path = "/api/me";
        getProfile.method = "GET";
        getProfile.summary = "Get User Profile";
        getProfile.description = "Get current user's profile information.";
        getProfile.tags = {"User Profile"};
        getProfile.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(getProfile);

        ::authforge::drogon::observability::openapi::EndpointInfo deleteAccount;
        deleteAccount.path = "/api/me";
        deleteAccount.method = "DELETE";
        deleteAccount.summary = "Delete Account";
        deleteAccount.description = "Soft-delete the current user's account.";
        deleteAccount.tags = {"User Profile"};
        deleteAccount.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(deleteAccount);

        ::authforge::drogon::observability::openapi::EndpointInfo changePassword;
        changePassword.path = "/api/me/password";
        changePassword.method = "PUT";
        changePassword.summary = "Change Password";
        changePassword.description = "Change the current user's password.";
        changePassword.tags = {"User Profile"};
        changePassword.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(changePassword);

        ::authforge::drogon::observability::openapi::EndpointInfo listAuthorizedApps;
        listAuthorizedApps.path = "/api/me/authorized-apps";
        listAuthorizedApps.method = "GET";
        listAuthorizedApps.summary = "List Authorized Apps";
        listAuthorizedApps.description = "List OAuth2 clients authorized by the current user.";
        listAuthorizedApps.tags = {"User Profile"};
        listAuthorizedApps.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(
          listAuthorizedApps
        );

        ::authforge::drogon::observability::openapi::EndpointInfo revokeApp;
        revokeApp.path = "/api/me/authorized-apps/{clientId}";
        revokeApp.method = "DELETE";
        revokeApp.summary = "Revoke App Authorization";
        revokeApp.description =
          "Revoke the current user's authorization for a specific OAuth2 client.";
        revokeApp.tags = {"User Profile"};
        revokeApp.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(revokeApp);
    }
};

UserSelfServiceControllerDocs docs_;
}  // namespace

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
        db->execSqlAsync(
          "SELECT username, email, email_verified, mfa_enabled "
          "FROM users WHERE public_sub::text = $1::text",
          [sharedCb, req](const ::drogon::orm::Result &result) {
              if (result.empty())
              {
                  respondError(
                    req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "getProfile: user not found"
                  );
                  return;
              }

              const auto &row = result[0];
              Json::Value json;
              json["username"] = row["username"].as<std::string>();
              json["email"] = row["email"].isNull() ? "" : row["email"].as<std::string>();
              json["email_verified"] =
                row["email_verified"].isNull() ? false : row["email_verified"].as<bool>();
              json["mfa_enabled"] =
                row["mfa_enabled"].isNull() ? false : row["mfa_enabled"].as<bool>();
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              (*sharedCb)(resp);
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("getProfile failed: ") + e.base().what()
              );
          },
          userId
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
        db->execSqlAsync(
          "SELECT password_hash, salt FROM users WHERE public_sub::text = $1::text",
          [sharedCb, oldPassword, newPassword, userId, req](const ::drogon::orm::Result &result) {
              if (result.empty())
              {
                  respondError(
                    req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "changePassword: user not found"
                  );
                  return;
              }

              std::string storedHash = result[0]["password_hash"].as<std::string>();
              std::string salt =
                result[0]["salt"].isNull() ? "" : result[0]["salt"].as<std::string>();

              // Verify old password
              if (!::oauth2::utils::PasswordHasher::verify(oldPassword, storedHash, salt))
              {
                  ::oauth2::observability::AuditLogger::log(
                    "password_change_failed", "failure", req, userId, "user", userId
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
                  newHash = ::oauth2::utils::PasswordHasher::hash(newPassword);
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

              // Update password in DB (clear salt since PBKDF2 embeds it)
              auto db2 = ::drogon::app().getDbClient();
              db2->execSqlAsync(
                "UPDATE users SET password_hash = $1, salt = '' "
                "WHERE public_sub::text = $2::text",
                [sharedCb, userId, req, db2](const ::drogon::orm::Result &) {
                    // Revoke all access tokens for this user
                    db2->execSqlAsync(
                      "UPDATE oauth2_access_tokens SET revoked = true WHERE user_id = $1",
                      [sharedCb, userId, req, db2](const ::drogon::orm::Result &) {
                          // Revoke all refresh tokens for this user
                          db2->execSqlAsync(
                            "UPDATE oauth2_refresh_tokens SET revoked = true WHERE user_id = $1",
                            [sharedCb, userId, req](const ::drogon::orm::Result &) {
                                ::oauth2::observability::AuditLogger::log(
                                  "password_changed", "success", req, userId, "user", userId
                                );
                                Json::Value json;
                                json["message"] = "Password changed successfully";
                                json["note"] = "All existing sessions have been revoked";
                                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                                (*sharedCb)(resp);
                            },
                            [sharedCb, userId, req](const ::drogon::orm::DrogonDbException &) {
                                ::oauth2::observability::AuditLogger::log(
                                  "password_changed", "success", req, userId, "user", userId
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
                                ::oauth2::observability::AuditLogger::log(
                                  "password_changed", "success", req, userId, "user", userId
                                );
                                Json::Value json;
                                json["message"] = "Password changed successfully";
                                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                                (*sharedCb)(resp);
                            },
                            [sharedCb, userId, req](const ::drogon::orm::DrogonDbException &) {
                                ::oauth2::observability::AuditLogger::log(
                                  "password_changed", "success", req, userId, "user", userId
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
                },
                newHash,
                userId
              );
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("changePassword lookup failed: ") + e.base().what()
              );
          },
          userId
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
        db->execSqlAsync(
          "SELECT DISTINCT c.client_id, c.name "
          "FROM oauth2_user_consents uc "
          "JOIN oauth2_clients c ON uc.client_id = c.client_id "
          "WHERE uc.internal_user_id = "
          "(SELECT id FROM users WHERE public_sub::text = $1::text)",
          [sharedCb](const ::drogon::orm::Result &result) {
              Json::Value json;
              Json::Value apps(Json::arrayValue);

              for (const auto &row : result)
              {
                  Json::Value app;
                  app["client_id"] = row["client_id"].as<std::string>();
                  app["name"] = row["name"].isNull() ? "" : row["name"].as<std::string>();
                  apps.append(app);
              }

              json["authorized_apps"] = apps;
              json["total"] = static_cast<int>(result.size());
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              (*sharedCb)(resp);
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("listAuthorizedApps failed: ") + e.base().what()
              );
          },
          userId
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
        db->execSqlAsync(
          "SELECT id FROM users WHERE public_sub::text = $1::text",
          [sharedCb, userId, clientId, req, db](const ::drogon::orm::Result &result) {
              if (result.empty())
              {
                  respondError(
                    req,
                    sharedCb,
                    "VALIDATION_RESOURCE_NOT_FOUND",
                    "revokeAuthorizedApp: user not found"
                  );
                  return;
              }

              int internalUserId = result[0]["id"].as<int>();

              // Delete consents
              db->execSqlAsync(
                "DELETE FROM oauth2_user_consents "
                "WHERE internal_user_id = $1 AND client_id = $2",
                [sharedCb, userId, clientId, req, db](const ::drogon::orm::Result &) {
                    // Revoke access tokens for this user+client
                    db->execSqlAsync(
                      "UPDATE oauth2_access_tokens SET revoked = true "
                      "WHERE user_id = $1 AND client_id = $2",
                      [sharedCb, userId, clientId, req](const ::drogon::orm::Result &) {
                          ::oauth2::observability::AuditLogger::log(
                            "app_authorization_revoked", "success", req, userId, "client", clientId
                          );
                          Json::Value json;
                          json["message"] = "Authorization revoked successfully";
                          json["client_id"] = clientId;
                          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                          (*sharedCb)(resp);
                      },
                      [sharedCb, userId, clientId, req](const ::drogon::orm::DrogonDbException &) {
                          ::oauth2::observability::AuditLogger::log(
                            "app_authorization_revoked", "success", req, userId, "client", clientId
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
                      std::string("revokeAuthorizedApp consent delete failed: ") + e.base().what()
                    );
                },
                internalUserId,
                clientId
              );
          },
          [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
              respondError(
                req,
                sharedCb,
                "DB_QUERY_ERROR",
                std::string("revokeAuthorizedApp user lookup failed: ") + e.base().what()
              );
          },
          userId
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

        // Step 1: Revoke all access tokens
        db->execSqlAsync(
          "UPDATE oauth2_access_tokens SET revoked = true WHERE user_id = $1",
          [sharedCb, userId, req, db](const ::drogon::orm::Result &) {
              // Step 2: Revoke all refresh tokens
              db->execSqlAsync(
                "UPDATE oauth2_refresh_tokens SET revoked = true WHERE user_id = $1",
                [sharedCb, userId, req, db](const ::drogon::orm::Result &) {
                    // Step 3: Soft-delete user (anonymize)
                    auto timestamp = std::to_string(
                      std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                      )
                        .count()
                    );
                    std::string anonUsername = "deleted_" + timestamp;

                    db->execSqlAsync(
                      "UPDATE users SET username = $1, email = NULL, password_hash = 'DELETED' "
                      "WHERE public_sub::text = $2::text",
                      [sharedCb, userId, req](const ::drogon::orm::Result &result) {
                          if (result.affectedRows() == 0)
                          {
                              respondError(
                                req,
                                sharedCb,
                                "VALIDATION_RESOURCE_NOT_FOUND",
                                "deleteAccount: user not found"
                              );
                              return;
                          }

                          ::oauth2::observability::AuditLogger::log(
                            "account_deleted", "success", req, userId, "user", userId
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
                      },
                      anonUsername,
                      userId
                    );
                },
                [sharedCb, userId, req, db](const ::drogon::orm::DrogonDbException &) {
                    auto timestamp = std::to_string(
                      std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                      )
                        .count()
                    );
                    std::string anonUsername = "deleted_" + timestamp;

                    db->execSqlAsync(
                      "UPDATE users SET username = $1, email = NULL, password_hash = 'DELETED' "
                      "WHERE public_sub::text = $2::text",
                      [sharedCb, userId, req](const ::drogon::orm::Result &) {
                          ::oauth2::observability::AuditLogger::log(
                            "account_deleted", "success", req, userId, "user", userId
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
                      },
                      anonUsername,
                      userId
                    );
                },
                userId
              );
          },
          [sharedCb, userId, req, db](const ::drogon::orm::DrogonDbException &) {
              auto timestamp = std::to_string(
                std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch()
                )
                  .count()
              );
              std::string anonUsername = "deleted_" + timestamp;

              db->execSqlAsync(
                "UPDATE users SET username = $1, email = NULL, password_hash = 'DELETED' "
                "WHERE public_sub::text = $2::text",
                [sharedCb, userId, req](const ::drogon::orm::Result &) {
                    ::oauth2::observability::AuditLogger::log(
                      "account_deleted", "success", req, userId, "user", userId
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
                },
                anonUsername,
                userId
              );
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
