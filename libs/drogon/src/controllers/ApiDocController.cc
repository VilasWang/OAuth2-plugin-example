#include <authforge/drogon/controllers/ApiDocController.h>
#include <oauth2/observability/openapi/OpenApiGenerator.h>
#include <oauth2/error/ErrorResponder.h>
#include <drogon/utils/Utilities.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace authforge::drogon::controllers
{

namespace
{
struct ApiDocControllerDocs
{
    ApiDocControllerDocs()
    {
        ::oauth2::observability::openapi::EndpointInfo spec;
        spec.path = "/docs/api/openapi.json";
        spec.method = "GET";
        spec.summary = "Get OpenAPI Specification";
        spec.description =
          "Returns the dynamically generated OpenAPI 3.0 specification in JSON format.";
        spec.tags = {"Documentation"};
        spec.requiresAuth = false;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(spec);

        ::oauth2::observability::openapi::EndpointInfo ui;
        ui.path = "/docs/api/";
        ui.method = "GET";
        ui.summary = "Swagger UI";
        ui.description = "Serves the Swagger UI HTML page for interactive API documentation.";
        ui.tags = {"Documentation"};
        ui.requiresAuth = false;
        ::oauth2::observability::openapi::OpenApiGenerator::addEndpoint(ui);
    }
};

ApiDocControllerDocs docs_;
}  // namespace

void ApiDocController::openApiSpec(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    try
    {
        // Use document root from Drogon config as base, fallback to current
        // directory
        std::filesystem::path baseDir = ::drogon::app().getDocumentRoot();
        if (baseDir.empty() || baseDir == "./" || baseDir == ".")
        {
            baseDir = std::filesystem::current_path();
        }
        std::string filePath = (baseDir / "docs" / "api" / "openapi.json").string();

        // Read the OpenAPI specification file
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            ::common::error::ErrorResponder::respond(
              req,
              std::move(callback),
              "VALIDATION_RESOURCE_NOT_FOUND",
              "openApiSpec: OpenAPI specification not found at " + filePath
            );
            return;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        auto resp = ::drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(::drogon::k200OK);
        resp->setContentTypeString("application/json");
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setBody(content);
        callback(resp);
    }
    catch (const std::exception &e)
    {
        ::common::error::ErrorResponder::respondException(
          req, std::move(callback), e, ::common::error::ErrorCategory::INTERNAL
        );
    }
}

void ApiDocController::swaggerUi(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    try
    {
        // Use document root from Drogon config as base, fallback to current
        // directory
        std::filesystem::path baseDir = ::drogon::app().getDocumentRoot();
        if (baseDir.empty() || baseDir == "./" || baseDir == ".")
        {
            baseDir = std::filesystem::current_path();
        }
        std::string filePath = (baseDir / "docs" / "api" / "swagger-ui" / "index.html").string();

        // Read the Swagger UI HTML file
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            ::common::error::ErrorResponder::respond(
              req,
              std::move(callback),
              "VALIDATION_RESOURCE_NOT_FOUND",
              "swaggerUi: Swagger UI not found at " + filePath
            );
            return;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        auto resp = ::drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(::drogon::k200OK);
        resp->setContentTypeString("text/html");
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->setBody(content);
        callback(resp);
    }
    catch (const std::exception &e)
    {
        ::common::error::ErrorResponder::respondException(
          req, std::move(callback), e, ::common::error::ErrorCategory::INTERNAL
        );
    }
}

}  // namespace authforge::drogon::controllers
