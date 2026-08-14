#pragma once

// M5 Task 29a (authforge-sdk-refactor): the client-management routes
// (`/api/admin/clients*`) carved out of the former 2914-line AdminController
// (design.md §5.8 / Task 29: "协议端点控制器按职责/资源命名"). Verbatim move --
// behavior unchanged, the Admin API tests must stay green. The raw-SQL ->
// ORM Mapper + business-logic-to-service extraction is deferred to Task 29b
// (pre-existing tech debt documented in tasks.md Task 29 实地摸底).

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

class ClientAdminController : public ::drogon::HttpController<ClientAdminController, false>
{
  public:
    METHOD_LIST_BEGIN
    // Client Management
    ADD_METHOD_TO(
      ClientAdminController::listClients,
      "/api/admin/clients",
      ::drogon::Get,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      ClientAdminController::createClient,
      "/api/admin/clients",
      ::drogon::Post,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      ClientAdminController::getClient,
      "/api/admin/clients/{clientId}",
      ::drogon::Get,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      ClientAdminController::updateClient,
      "/api/admin/clients/{clientId}",
      ::drogon::Put,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      ClientAdminController::deleteClient,
      "/api/admin/clients/{clientId}",
      ::drogon::Delete,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      ClientAdminController::resetClientSecret,
      "/api/admin/clients/{clientId}/reset-secret",
      ::drogon::Post,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      ClientAdminController::getClientScopes,
      "/api/admin/clients/{clientId}/scopes",
      ::drogon::Get,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      ClientAdminController::updateClientScopes,
      "/api/admin/clients/{clientId}/scopes",
      ::drogon::Put,
      "authforge::drogon::filters::AuthorizationFilter"
    );
    METHOD_LIST_END

    void listClients(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void createClient(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void getClient(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &clientId
    );

    void updateClient(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &clientId
    );

    void deleteClient(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &clientId
    );

    void resetClientSecret(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &clientId
    );

    void getClientScopes(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &clientId
    );

    void updateClientScopes(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &clientId
    );

    // #43 resource-scope authorization: explicit, order-independent endpoint
    // + scope-requirement registration (replaces the former file-scope
    // static-init struct, defect 1.1 SIOF). Called from main()/test_main()
    // alongside the other controllers' initApiDocs().
    static void initApiDocs();

  private:
    static void initApiDocsImpl();
};

}  // namespace authforge::drogon::controllers
