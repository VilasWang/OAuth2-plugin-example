#pragma once

// Task B5 (fulla-sdk-refactor): application-service extraction for the
// email-verification non-admin domain. Raw SQL inline in
// EmailVerificationController is now Mapper<T> + Criteria on the ORM
// EmailVerificationTokens/Users models (per db-operations.md). The
// DELETE...RETURNING on verifyToken is a documented raw-SQL exemption.
// Behavior equivalent (all existing tests must stay green).
//
// Lives in libs/drogon (Adapter layer, namespace
// fulla::drogon::services) so the SDK stays self-contained.

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>
#include <memory>
#include <string>

namespace fulla::drogon::services
{

/**
 * @brief Application service for email verification (token CRUD +
 * users.email_verified update). Follows the same static-method pattern
 * as admin::ClientManagementService.
 */
class EmailVerificationService
{
  public:
    using ResponseCallback =
      std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>>;

    // ---- POST /api/verify-email/resend ----
    static void resendVerification(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);

    // ---- GET /api/verify-email?token=xxx ----
    static void verifyToken(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);

  private:
    /// Utility: generate a token, hash it, INSERT into
    /// email_verification_tokens, and send the verification email.
    /// @param internalUserId The internal (integer) user id.
    /// @param email The recipient email address.
    static void sendVerificationEmail(int internalUserId, const std::string &email);
};

}  // namespace fulla::drogon::services
