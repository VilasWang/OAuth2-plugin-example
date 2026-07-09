#include <authforge/drogon/controllers/PasswordResetController.h>
#include <oauth2/utils/CryptoUtils.h>
#include <oauth2/utils/PasswordHasher.h>
#include <oauth2/utils/EmailNormalizer.h>
#include <oauth2/utils/EmailService.h>
#include <oauth2/plugin/OAuth2Plugin.h>
#include <oauth2/observability/AuditLogger.h>
#include <oauth2/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <chrono>

namespace authforge::drogon::controllers
{

namespace
{

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

struct PasswordResetControllerDocs
{
    PasswordResetControllerDocs()
    {
        ::oauth2::observability::openapi::EndpointInfo requestDocs;
        requestDocs.path = "/api/password-reset/request";
        requestDocs.method = "POST";
        requestDocs.summary = "Request Password Reset";
        requestDocs.description = "Request a password reset link to be sent via email.";
        requestDocs.tags = {"User Verification"};
        requestDocs.requiresAuth = false;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(requestDocs);

        ::oauth2::observability::openapi::EndpointInfo confirmDocs;
        confirmDocs.path = "/api/password-reset/confirm";
        confirmDocs.method = "POST";
        confirmDocs.summary = "Confirm Password Reset";
        confirmDocs.description = "Confirm a password reset using the token sent via email.";
        confirmDocs.tags = {"User Verification"};
        confirmDocs.requiresAuth = false;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(confirmDocs);
    }
};

PasswordResetControllerDocs docs_;

}  // namespace

}  // namespace authforge::drogon::controllers

// Lazy accessor - avoids static init order crash (see P5 bugfix).
static ::oauth2::IEmailService &getEmailSvc()
{
    return ::oauth2::getEmailService();
}

namespace authforge::drogon::controllers
{

void PasswordResetController::request(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    std::string email;
    if (req->contentType() == ::drogon::CT_APPLICATION_JSON)
    {
        auto json = req->getJsonObject();
        if (json)
            email = json->get("email", "").asString();
    }
    else
    {
        email = req->getParameter("email");
    }

    if (email.empty())
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "password-reset request: email is required"
        );
        return;
    }

    email = ::oauth2::utils::normalizeEmail(email);

    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "SELECT id, email FROM users WHERE email = $1",
      [sharedCb, email](const ::drogon::orm::Result &r) {
          Json::Value json;
          json["message"] = "If the email exists, a reset link has been sent";

          if (r.empty())
          {
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              (*sharedCb)(resp);
              return;
          }

          int userId = r[0]["id"].as<int>();

          std::string rawToken = ::oauth2::utils::generateSecureToken();
          std::string tokenHash = ::oauth2::utils::hashToken(rawToken);

          auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch()
          )
                       .count();
          int64_t expiresAt = now + 900;  // 15 minutes

          auto db2 = ::drogon::app().getDbClient();
          db2->execSqlAsync(
            "INSERT INTO password_reset_tokens (token_hash, user_id, expires_at) "
            "VALUES ($1, $2, $3)",
            [sharedCb, json, rawToken, email](const ::drogon::orm::Result &) {
                auto customConfig = ::drogon::app().getCustomConfig();
                std::string frontendUrl = "http://localhost:5173";
                if (customConfig.isMember("frontend") && customConfig["frontend"].isMember("url"))
                    frontendUrl = customConfig["frontend"]["url"].asString();
                std::string resetLink = frontendUrl + "/reset-password?token=" + rawToken;
                std::string emailBody = "Click the following link to reset your password:\n\n" +
                                        resetLink + "\n\nThis link expires in 15 minutes.";

                getEmailSvc().sendEmail(email, "Password Reset Request", emailBody, [](bool) {});

                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                (*sharedCb)(resp);
            },
            [sharedCb, json](const ::drogon::orm::DrogonDbException &e) {
                LOG_ERROR << "Failed to store reset token: " << e.base().what();
                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                (*sharedCb)(resp);
            },
            tokenHash,
            userId,
            expiresAt
          );
      },
      [sharedCb](const ::drogon::orm::DrogonDbException &e) {
          LOG_ERROR << "Password reset lookup failed: " << e.base().what();
          Json::Value json;
          json["message"] = "If the email exists, a reset link has been sent";
          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
          (*sharedCb)(resp);
      },
      email
    );
}

void PasswordResetController::confirm(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    std::string token, newPassword;
    if (req->contentType() == ::drogon::CT_APPLICATION_JSON)
    {
        auto json = req->getJsonObject();
        if (json)
        {
            token = json->get("token", "").asString();
            newPassword = json->get("new_password", "").asString();
        }
    }
    else
    {
        token = req->getParameter("token");
        newPassword = req->getParameter("new_password");
    }

    if (token.empty() || newPassword.empty())
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_MISSING_REQUIRED_FIELD",
          "password-reset confirm: token and new_password are required"
        );
        return;
    }

    if (newPassword.length() < 8)
    {
        respondError(
          req,
          sharedCb,
          "VALIDATION_FORMAT_ERROR",
          "password-reset confirm: password must be at least 8 characters"
        );
        return;
    }

    std::string tokenHash = ::oauth2::utils::hashToken(token);
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch()
    )
                 .count();

    auto db = ::drogon::app().getDbClient();

    db->execSqlAsync(
      "UPDATE password_reset_tokens SET used = true "
      "WHERE token_hash = $1 AND used = false AND expires_at > $2 "
      "RETURNING user_id",
      [sharedCb, newPassword, db, req](const ::drogon::orm::Result &r) {
          if (r.empty())
          {
              respondError(
                req,
                sharedCb,
                "VALIDATION_RESET_TOKEN_INVALID",
                "password-reset confirm: token is invalid, expired, or already used"
              );
              return;
          }

          int userId = r[0]["user_id"].as<int>();

          std::string newHash;
          try
          {
              newHash = ::oauth2::utils::PasswordHasher::hash(newPassword);
          }
          catch (const std::exception &e)
          {
              respondError(
                req, sharedCb, "INTERNAL_ERROR", std::string("Password hashing failed: ") + e.what()
              );
              return;
          }

          db->execSqlAsync(
            "UPDATE users SET password_hash = $1, salt = '' WHERE id = $2",
            [sharedCb, userId, db, req](const ::drogon::orm::Result &) {
                std::string userIdStr = std::to_string(userId);
                db->execSqlAsync(
                  "UPDATE oauth2_access_tokens SET revoked = true WHERE user_id = $1",
                  [sharedCb, userId, db, userIdStr, req](const ::drogon::orm::Result &) {
                      db->execSqlAsync(
                        "UPDATE oauth2_refresh_tokens SET revoked = true WHERE user_id = $1",
                        [sharedCb, userId, req](const ::drogon::orm::Result &) {
                            ::oauth2::observability::AuditLogger::log(
                              "password_reset",
                              "success",
                              req,
                              std::to_string(userId),
                              "user",
                              std::to_string(userId)
                            );
                            Json::Value json;
                            json["message"] = "Password reset successful";
                            json["note"] = "All existing sessions have been revoked";
                            auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                            (*sharedCb)(resp);
                        },
                        [sharedCb, userId, req](const ::drogon::orm::DrogonDbException &) {
                            ::oauth2::observability::AuditLogger::log(
                              "password_reset",
                              "success",
                              req,
                              std::to_string(userId),
                              "user",
                              std::to_string(userId)
                            );
                            Json::Value json;
                            json["message"] = "Password reset successful";
                            auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                            (*sharedCb)(resp);
                        },
                        userIdStr
                      );
                  },
                  [sharedCb, userId, req](const ::drogon::orm::DrogonDbException &) {
                      ::oauth2::observability::AuditLogger::log(
                        "password_reset",
                        "success",
                        req,
                        std::to_string(userId),
                        "user",
                        std::to_string(userId)
                      );
                      Json::Value json;
                      json["message"] = "Password reset successful";
                      auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                      (*sharedCb)(resp);
                  },
                  userIdStr
                );
            },
            [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req,
                  sharedCb,
                  "DB_QUERY_ERROR",
                  std::string("Failed to update password: ") + e.base().what()
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
            std::string("Reset token lookup failed: ") + e.base().what()
          );
      },
      tokenHash,
      now
    );
}

}  // namespace authforge::drogon::controllers
