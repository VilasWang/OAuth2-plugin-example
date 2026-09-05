#pragma once

// M5 Task 29b batch 2 (fulla-sdk-refactor): application-service extraction
// for the token-management admin domain (design.md §5.8 / Task 29). The raw
// `db->execSqlAsync("SELECT/DELETE ...")` calls inline in TokenAdminController
// are now Mapper<T> + Criteria operations on the ORM access/refresh-token
// models (per .claude/rules/db-operations.md). Behavior is equivalent to the
// pre-29b controller (Admin API tests must stay green).
//
// Lives in libs/drogon (Adapter/SDK layer, namespace fulla::drogon::admin)
// so the SDK stays self-contained -- no SDK->product dependency cycle.

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <fulla/oauth2/jwk/JwkManager.h>

#include <functional>
#include <memory>
#include <string>

namespace fulla::drogon::admin
{

/**
 * @brief Application service for token admin operations (list + revoke).
 *
 * Thin DB-access wrapper: each method performs the Mapper+Criteria operation(s)
 * for one admin route and invokes the shared callback with the final
 * HttpResponse. The controller (TokenAdminController) only parses the request +
 * dispatches here.
 */
class TokenManagementService
{
  public:
    using ResponseCallback =
      std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>>;

    // ---- GET /api/admin/tokens (paginated, filterable) ----
    static void listTokens(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);

    // ---- DELETE /api/admin/tokens/{tokenPrefix} ----
    static void revokeToken(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &tokenPrefix
    );

    // ---- POST /api/admin/tokens/revoke-by-client ----
    static void revokeTokensByClient(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);

    // ---- POST /api/admin/tokens/revoke-by-user ----
    static void revokeTokensByUser(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);

    // ---- GET /api/admin/oidc/keys (no DB access, pure metadata) ----
    // #110-B: reports the LIVE keystore state (every loaded kid, which one
    // is active) from the provided JwkManager instead of the hardcoded stub.
    static void getOidcKeys(
      ResponseCallback cb,
      const std::shared_ptr<const fulla::oauth2::JwkManager> &jwkManager
    );
};

}  // namespace fulla::drogon::admin
