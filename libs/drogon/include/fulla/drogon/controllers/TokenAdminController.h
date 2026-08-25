#pragma once

// M5 Task 29a (fulla-sdk-refactor): the token-management routes
// (`/api/admin/tokens*` + `/api/admin/oidc/keys`) carved out of the former
// AdminController (design.md §5.8 / Task 29). Verbatim move -- behavior
// unchanged, Admin API tests must stay green. The raw-SQL -> ORM Mapper +
// business-logic-to-service extraction is deferred to Task 29b.

#include <drogon/HttpController.h>

namespace fulla::drogon::controllers
{

class TokenAdminController : public ::drogon::HttpController<TokenAdminController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(
      TokenAdminController::listTokens,
      "/api/admin/tokens",
      ::drogon::Get,
      "fulla::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      TokenAdminController::revokeTokensByClient,
      "/api/admin/tokens/revoke-by-client",
      ::drogon::Post,
      "fulla::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      TokenAdminController::revokeTokensByUser,
      "/api/admin/tokens/revoke-by-user",
      ::drogon::Post,
      "fulla::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      TokenAdminController::revokeToken,
      "/api/admin/tokens/{tokenPrefix}",
      ::drogon::Delete,
      "fulla::drogon::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      TokenAdminController::getOidcKeys,
      "/api/admin/oidc/keys",
      ::drogon::Get,
      "fulla::drogon::filters::AuthorizationFilter"
    );
    METHOD_LIST_END

    void listTokens(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void revokeTokensByClient(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void revokeTokensByUser(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void revokeToken(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &tokenPrefix
    );

    void getOidcKeys(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    // #43 resource-scope authorization: explicit, order-independent endpoint
    // + scope-requirement registration (replaces the former file-scope
    // static-init struct, defect 1.1 SIOF). Called from main()/test_main()
    // alongside the other controllers' initApiDocs().
    static void initApiDocs();

  private:
    static void initApiDocsImpl();
};

}  // namespace fulla::drogon::controllers
