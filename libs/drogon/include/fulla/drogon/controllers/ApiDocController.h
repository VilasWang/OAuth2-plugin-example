#pragma once

// M3 Task 20 slice 7 (fulla-sdk-refactor): relocated from
// OAuth2Server/controllers/ApiDocController.h into
// fulla::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <functional>

namespace fulla::drogon::controllers
{

/**
 * @brief API Documentation Controller
 *
 * Serves OpenAPI specification and Swagger UI for API documentation
 */
class ApiDocController : public ::drogon::HttpController<ApiDocController, false>
{
  public:
    METHOD_LIST_BEGIN
    // Serve OpenAPI JSON specification
    ADD_METHOD_TO(ApiDocController::openApiSpec, "/docs/api/openapi.json", ::drogon::Get);

    // Serve Swagger UI main page
    ADD_METHOD_TO(ApiDocController::swaggerUi, "/docs/api", ::drogon::Get);
    ADD_METHOD_TO(ApiDocController::swaggerUi, "/docs/api/", ::drogon::Get);
    METHOD_LIST_END

    void openApiSpec(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );

    void swaggerUi(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
};

}  // namespace fulla::drogon::controllers
