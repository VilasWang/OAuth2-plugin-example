#include <authforge/drogon/controllers/EmailVerificationController.h>
#include <oauth2/utils/CryptoUtils.h>
#include <oauth2/utils/EmailService.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <drogon/drogon.h>
#include <chrono>

namespace authforge::drogon::controllers
{

namespace
{
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

struct EmailVerificationControllerDocs
{
    EmailVerificationControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo verifyEmail;
        verifyEmail.path = "/api/verify-email";
        verifyEmail.method = "GET";
        verifyEmail.summary = "Verify Email";
        verifyEmail.description = "Verify an email address using a token.";
        verifyEmail.tags = {"User Verification"};
        verifyEmail.requiresAuth = false;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(verifyEmail);

        ::authforge::drogon::observability::openapi::EndpointInfo resendEmail;
        resendEmail.path = "/api/verify-email/resend";
        resendEmail.method = "POST";
        resendEmail.summary = "Resend Verification Email";
        resendEmail.description = "Resend the email verification link.";
        resendEmail.tags = {"User Verification"};
        resendEmail.requiresAuth = false;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(resendEmail);
    }
};

EmailVerificationControllerDocs docs_;
}  // namespace

}  // namespace authforge::drogon::controllers

// Lazy accessor - avoids static init order crash (see P5 bugfix).
static ::oauth2::IEmailService &getEmailSvc()
{
    return ::oauth2::getEmailService();
}

namespace authforge::drogon::controllers
{

void EmailVerificationController::sendVerificationEmail(int userId, const std::string &email)
{
    if (email.empty())
        return;

    std::string rawToken = ::authforge::drogon::utils::generateSecureToken();
    std::string tokenHash = ::authforge::drogon::utils::hashToken(rawToken);

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch()
    )
                 .count();
    int64_t expiresAt = now + 86400;  // 24 hours

    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "INSERT INTO email_verification_tokens (token_hash, user_id, email, expires_at) "
      "VALUES ($1, $2, $3, $4) "
      "ON CONFLICT (token_hash) DO NOTHING",
      [rawToken, email](const ::drogon::orm::Result &) {
          // Build verification link using frontend URL
          auto customConfig = ::drogon::app().getCustomConfig();
          std::string frontendUrl = "http://localhost:5173";
          if (customConfig.isMember("frontend") && customConfig["frontend"].isMember("url"))
          {
              frontendUrl = customConfig["frontend"]["url"].asString();
          }
          std::string verifyLink = frontendUrl + "/verify-email?token=" + rawToken;
          std::string body = "Please verify your email by clicking:\n\n" + verifyLink +
                             "\n\nThis link expires in 24 hours.";

          ::oauth2::getEmailService().sendEmail(email, "Verify Your Email", body, [](bool) {});
      },
      [](const ::drogon::orm::DrogonDbException &e) {
          LOG_ERROR << "Failed to store verification token: " << e.base().what();
      },
      tokenHash,
      userId,
      email,
      expiresAt
    );
}

void EmailVerificationController::verify(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    std::string token = req->getParameter("token");
    if (token.empty())
    {
        respondError(
          req, sharedCb, "VALIDATION_MISSING_REQUIRED_FIELD", "verify: token parameter is required"
        );
        return;
    }

    std::string tokenHash = ::authforge::drogon::utils::hashToken(token);
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
                 std::chrono::system_clock::now().time_since_epoch()
    )
                 .count();

    auto db = ::drogon::app().getDbClient();

    // Atomic consume and get user info
    db->execSqlAsync(
      "DELETE FROM email_verification_tokens "
      "WHERE token_hash = $1 AND expires_at > $2 "
      "RETURNING user_id, email",
      [sharedCb, db, req](const ::drogon::orm::Result &r) {
          if (r.empty())
          {
              respondError(
                req,
                sharedCb,
                "VALIDATION_VERIFICATION_TOKEN_INVALID",
                "verify: token is invalid or expired"
              );
              return;
          }

          int userId = r[0]["user_id"].as<int>();

          // Mark email as verified
          db->execSqlAsync(
            "UPDATE users SET email_verified = true WHERE id = $1",
            [sharedCb](const ::drogon::orm::Result &) {
                Json::Value json;
                json["message"] = "Email verified successfully";
                auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                (*sharedCb)(resp);
            },
            [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
                respondError(
                  req,
                  sharedCb,
                  "DB_QUERY_ERROR",
                  std::string("Failed to update email_verified: ") + e.base().what()
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
            std::string("Email verification failed: ") + e.base().what()
          );
      },
      tokenHash,
      now
    );
}

void EmailVerificationController::resend(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));

    // Get userId from OAuth2Middleware attributes
    std::string userId = req->getAttributes()->get<std::string>("userId");
    if (userId.empty())
    {
        respondError(req, sharedCb, "AUTH_TOKEN_INVALID", "resend: missing authenticated user");
        return;
    }

    auto db = ::drogon::app().getDbClient();
    db->execSqlAsync(
      "SELECT id, email, email_verified FROM users WHERE public_sub::text = $1::text",
      [sharedCb, req](const ::drogon::orm::Result &r) {
          if (r.empty())
          {
              respondError(
                req, sharedCb, "VALIDATION_RESOURCE_NOT_FOUND", "resend: user not found"
              );
              return;
          }

          if (r[0]["email_verified"].as<bool>())
          {
              Json::Value json;
              json["message"] = "Email is already verified";
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              (*sharedCb)(resp);
              return;
          }

          int internalId = r[0]["id"].as<int>();
          std::string email = r[0]["email"].isNull() ? "" : r[0]["email"].as<std::string>();

          if (email.empty())
          {
              respondError(
                req, sharedCb, "VALIDATION_INVALID_INPUT", "resend: no email address on file"
              );
              return;
          }

          sendVerificationEmail(internalId, email);

          Json::Value json;
          json["message"] = "Verification email sent";
          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
          (*sharedCb)(resp);
      },
      [sharedCb, req](const ::drogon::orm::DrogonDbException &e) {
          respondError(
            req,
            sharedCb,
            "DB_QUERY_ERROR",
            std::string("Resend verification failed: ") + e.base().what()
          );
      },
      userId
    );
}

}  // namespace authforge::drogon::controllers
