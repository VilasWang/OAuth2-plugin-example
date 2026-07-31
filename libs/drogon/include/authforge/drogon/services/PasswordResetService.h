#pragma once

// Task B5 (authforge-sdk-refactor): application-service extraction for the
// password-reset non-admin domain. Replaces raw SQL in PasswordResetController
// with Mapper<T> + Criteria on ORM Users/PasswordResetTokens models (per
// db-operations.md). The UPDATE...RETURNING and bulk token-revocation UPDATEs
// are documented raw-SQL exemptions.
//
// Lives in libs/drogon (Adapter layer, namespace
// authforge::drogon::services).

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>
#include <memory>
#include <string>

namespace authforge::drogon::services
{

class PasswordResetService
{
  public:
    using ResponseCallback =
      std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>>;

    // ---- POST /api/password-reset/request ----
    static void requestReset(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);

    // ---- POST /api/password-reset/confirm ----
    static void confirmReset(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);
};

}  // namespace authforge::drogon::services
