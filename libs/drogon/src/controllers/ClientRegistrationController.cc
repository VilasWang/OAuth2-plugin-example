#include <authforge/drogon/controllers/ClientRegistrationController.h>
#include <authforge/drogon/services/ClientRegistrationService.h>
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>

#include <drogon/drogon.h>

namespace authforge::drogon::controllers
{

namespace
{
struct ClientRegistrationControllerDocs
{
    ClientRegistrationControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo registerDocs;
        registerDocs.path = "/api/oauth2/register";
        registerDocs.method = "POST";
        registerDocs.summary = "Register OAuth2 Client (RFC 7591)";
        registerDocs.description = "Dynamically register a new OAuth2 client.";
        registerDocs.tags = {"OAuth2"};
        registerDocs.requiresAuth = false;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(registerDocs);
    }
};

ClientRegistrationControllerDocs docs_;
}  // namespace

}  // namespace authforge::drogon::controllers

namespace authforge::drogon::controllers
{

// Task B5: business logic extracted to ClientRegistrationService.

void ClientRegistrationController::registerClient(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    services::ClientRegistrationService::registerClient(req, sharedCb);
}

}  // namespace authforge::drogon::controllers
