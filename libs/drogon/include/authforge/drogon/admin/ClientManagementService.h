#pragma once

// M5 Task 29b (authforge-sdk-refactor): application-service extraction for the
// client-management admin domain (design.md §5.8 / Task 29). The raw
// `db->execSqlAsync("SELECT/INSERT/UPDATE/DELETE ...")` calls that used to be
// inline in ClientAdminController are now Mapper<T> + Criteria operations on
// the ORM models in libs/storage-postgres (per .claude/rules/db-operations.md:
// async callback + Mapper API + Criteria, no hand-rolled CRUD SQL). Behavior is
// byte-for-byte equivalent to the pre-29b controller (Admin API tests must stay
// green); only the access pattern changed.
//
// Lives in libs/drogon (the Adapter/SDK layer, namespace authforge::drogon::
// admin) so the SDK stays self-contained -- it does NOT reach into the product
// app (no SDK->product dependency cycle).

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace authforge::drogon::admin
{

/**
 * @brief Application service for OAuth2 client CRUD + scope assignment.
 *
 * Thin DB-access wrapper: each method performs the Mapper+Criteria operation(s)
 * for one admin route and invokes the shared callback with the final
 * HttpResponse (success JSON or error). The controller (ClientAdminController)
 * only parses the request + dispatches here.
 *
 * Async continuation safety: the service is stateless (constructed per-request
 * on the stack), and each method captures the result-building state into the
 * async callback closure directly (no shared `this` across the async hop).
 */
class ClientManagementService
{
  public:
    using ResponseCallback =
      std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>>;

    // ---- GET /api/admin/clients ----
    static void listClients(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);

    // ---- POST /api/admin/clients ----
    static void createClient(const ::drogon::HttpRequestPtr &req, ResponseCallback cb);

    // ---- GET /api/admin/clients/{clientId} ----
    static void getClient(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &clientId
    );

    // ---- PUT /api/admin/clients/{clientId} ----
    static void updateClient(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &clientId
    );

    // ---- DELETE /api/admin/clients/{clientId} ----
    static void deleteClient(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &clientId
    );

    // ---- POST /api/admin/clients/{clientId}/reset-secret ----
    static void resetClientSecret(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &clientId
    );

    // ---- GET /api/admin/clients/{clientId}/scopes ----
    static void getClientScopes(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &clientId
    );

    // ---- PUT /api/admin/clients/{clientId}/scopes ----
    static void updateClientScopes(
      const ::drogon::HttpRequestPtr &req,
      ResponseCallback cb,
      const std::string &clientId
    );
};

}  // namespace authforge::drogon::admin
