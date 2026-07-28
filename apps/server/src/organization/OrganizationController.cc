#include "OrganizationController.h"
#include "OrganizationService.h"
#include <authforge/drogon/observability/openapi/OpenApiGenerator.h>

#include <memory>

// M5 Task 29b batch 3 (authforge-sdk-refactor): inline raw-SQL DB access from
// the Task 30 verbatim move is now delegated to OrganizationService (Mapper +
// Criteria, per .claude/rules/db-operations.md). Controller is now a thin HTTP
// adapter. Behavior equivalent (org CRUD routes/tests must stay green).

namespace organization
{

namespace
{
struct OrganizationControllerDocs
{
    OrganizationControllerDocs()
    {
        ::authforge::drogon::observability::openapi::EndpointInfo getOrgsDocs;
        getOrgsDocs.path = "/api/orgs";
        getOrgsDocs.method = "GET";
        getOrgsDocs.summary = "List Organizations";
        getOrgsDocs.description = "List all organizations.";
        getOrgsDocs.tags = {"Organization"};
        getOrgsDocs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(getOrgsDocs);

        ::authforge::drogon::observability::openapi::EndpointInfo postOrgsDocs;
        postOrgsDocs.path = "/api/orgs";
        postOrgsDocs.method = "POST";
        postOrgsDocs.summary = "Create Organization";
        postOrgsDocs.description = "Create a new organization.";
        postOrgsDocs.tags = {"Organization"};
        postOrgsDocs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(postOrgsDocs);

        ::authforge::drogon::observability::openapi::EndpointInfo postOrgUsersDocs;
        postOrgUsersDocs.path = "/api/orgs/{orgId}/users";
        postOrgUsersDocs.method = "POST";
        postOrgUsersDocs.summary = "Add User to Organization";
        postOrgUsersDocs.description = "Add a user to an organization.";
        postOrgUsersDocs.tags = {"Organization"};
        postOrgUsersDocs.requiresAuth = true;
        ::authforge::drogon::observability::openapi::OpenApiGenerator::addEndpoint(
          postOrgUsersDocs
        );
    }
};

OrganizationControllerDocs docs_;

}  // namespace

void OrganizationController::list(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    OrganizationService::list(req, sharedCb);
}

void OrganizationController::create(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    OrganizationService::create(req, sharedCb);
}

void OrganizationController::getBySlug(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
  const std::string &slug
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    OrganizationService::getBySlug(req, sharedCb, slug);
}

}  // namespace organization
