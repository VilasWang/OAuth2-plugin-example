#include <authforge/drogon/services/EmailVerificationService.h>

#include <authforge/storage/postgres/models/EmailVerificationTokens.h>
#include <authforge/storage/postgres/models/Users.h>
#include <authforge/drogon/utils/CryptoUtils.h>
#include <authforge/drogon/utils/EmailService.h>
#include <authforge/drogon/error/ErrorResponder.h>

#include <drogon/drogon.h>

// Wave-2 P1: email_verified is a cached profile field — revoke on write.
#include "../UserReadCache.h"

#include <chrono>

namespace authforge::drogon::services
{

namespace
{
// Emit an Application error via the unified ErrorResponder entry point.
// Verbatim from the pre-B5 EmailVerificationController -- behavior unchanged.
void respondError(
  const ::drogon::HttpRequestPtr &req,
  const EmailVerificationService::ResponseCallback &cb,
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

// Lazy accessor for EmailService -- avoids static init order issues.
::authforge::drogon::utils::IEmailService &getEmailSvc()
{
    return ::authforge::drogon::utils::getEmailService();
}

// Lazily resolve the DbClient. Kept identical to pre-B5 controller behavior.
::drogon::orm::DbClientPtr getDbOrRespond(
  const ::drogon::HttpRequestPtr &req,
  const EmailVerificationService::ResponseCallback &cb
)
{
    try
    {
        return ::drogon::app().getDbClient();
    }
    catch (...)
    {
        respondError(req, cb, "DB_CONNECTION_ERROR", "Database unavailable");
        return nullptr;
    }
}
}  // namespace

// Bring ORM + model names into scope. Fully qualified (::) to avoid
// namespace collision inside authforge::drogon::services.
using namespace ::drogon::orm;
using namespace ::drogon_model::oauth2_db;

// ---- internal helper ----

void EmailVerificationService::sendVerificationEmail(int internalUserId, const std::string &email)
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

    EmailVerificationTokens token;
    token.setTokenHash(tokenHash);
    token.setUserId(static_cast<int32_t>(internalUserId));
    token.setEmail(email);
    token.setExpiresAt(expiresAt);

    Mapper<EmailVerificationTokens> mapper(db);
    mapper.insert(
      token,
      [rawToken, email](const EmailVerificationTokens &) {
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

          getEmailSvc().sendEmail(email, "Verify Your Email", body, [](bool) {});
      },
      [](const DrogonDbException &e) {
          LOG_ERROR << "Failed to store verification token: " << e.base().what();
      }
    );
}

// ---- public methods ----

void EmailVerificationService::verifyToken(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback sharedCb
)
{
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

    auto db = getDbOrRespond(req, sharedCb);
    if (!db)
        return;

    // DELETE...RETURNING is a documented raw-SQL exemption (db-operations.md).
    // Atomic consume + get user info in one step.
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

          // Mark email as verified via Mapper
          Criteria crit(Users::Cols::_id, CompareOperator::EQ, userId);
          Mapper<Users>(db).findOne(
            crit,
            [sharedCb, db, req](const Users &user) {
                Users updated = user;
                updated.setEmailVerified(true);
                Mapper<Users>(db).update(
                  updated,
                  [sharedCb, user](const size_t) {
                      authforge::drogon::UserCacheInvalidator::instance().invalidateUser(
                        std::to_string(user.getValueOfId()), user.getValueOfPublicSub());
                      Json::Value json;
                      json["message"] = "Email verified successfully";
                      auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
                      (*sharedCb)(resp);
                  },
                  [sharedCb, req](const DrogonDbException &e) {
                      respondError(
                        req,
                        sharedCb,
                        "DB_QUERY_ERROR",
                        std::string("Failed to update email_verified: ") + e.base().what()
                      );
                  }
                );
            },
            [sharedCb, req](const DrogonDbException &e) {
                respondError(
                  req,
                  sharedCb,
                  "DB_QUERY_ERROR",
                  std::string("Failed to find user for email_verified update: ") + e.base().what()
                );
            }
          );
      },
      [sharedCb, req](const DrogonDbException &e) {
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

void EmailVerificationService::resendVerification(
  const ::drogon::HttpRequestPtr &req,
  ResponseCallback sharedCb
)
{
    // Get userId from request attributes (set by OAuth2 middleware)
    std::string userId = req->getAttributes()->get<std::string>("userId");
    if (userId.empty())
    {
        respondError(req, sharedCb, "AUTH_TOKEN_INVALID", "resend: missing authenticated user");
        return;
    }

    auto db = getDbOrRespond(req, sharedCb);
    if (!db)
        return;

    // userId attribute holds the OAuth2 subject (public_sub), not internal id.
    Criteria crit(Users::Cols::_public_sub, CompareOperator::EQ, userId);
    Mapper<Users> mapper(db);
    mapper.findOne(
      crit,
      [sharedCb, req](const Users &user) {
          if (user.getValueOfEmailVerified())
          {
              Json::Value json;
              json["message"] = "Email is already verified";
              auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
              (*sharedCb)(resp);
              return;
          }

          std::string email = user.getValueOfEmail().empty() ? "" : user.getValueOfEmail();

          if (email.empty())
          {
              respondError(
                req, sharedCb, "VALIDATION_INVALID_INPUT", "resend: no email address on file"
              );
              return;
          }

          sendVerificationEmail(user.getValueOfId(), email);

          Json::Value json;
          json["message"] = "Verification email sent";
          auto resp = ::drogon::HttpResponse::newHttpJsonResponse(json);
          (*sharedCb)(resp);
      },
      [sharedCb, req](const DrogonDbException &e) {
          respondError(
            req,
            sharedCb,
            "DB_QUERY_ERROR",
            std::string("Resend verification failed: ") + e.base().what()
          );
      }
    );
}

}  // namespace authforge::drogon::services
