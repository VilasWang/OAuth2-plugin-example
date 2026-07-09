#pragma once

// M3 Task 20 slice 6 (authforge-sdk-refactor): relocated from
// OAuth2Server/controllers/OrganizationController.h into
// authforge::drogon::controllers, following the AutoCreation=false
// pattern verified in slice 3 (HealthController) -- see PROGRESS.md.
//
// The ADD_METHOD_TO filter references ("oauth2::filters::AuthorizationFilter")
// are STRING lookups via DrClassMap -- they still resolve to the OLD
// oauth2::filters::AuthorizationFilter location (Task 20 slice 2 left the
// old filter classes in place, coexisting with the new
// authforge::drogon::filters::AuthorizationFilter copy; see PROGRESS.md's
// filter-vs-controller distinction). Not switched to the new filter name
// in this slice to keep the change minimal; a later cleanup slice can
// switch all such string references once every filter consumer has
// migrated.

#include <drogon/HttpController.h>

namespace authforge::drogon::controllers
{

class OrganizationController : public ::drogon::HttpController<OrganizationController, false>
{
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(
      OrganizationController::list,
      "/api/admin/organizations",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      OrganizationController::create,
      "/api/admin/organizations",
      ::drogon::Post,
      "oauth2::filters::AuthorizationFilter"
    );
    ADD_METHOD_TO(
      OrganizationController::getBySlug,
      "/api/admin/organizations/{slug}",
      ::drogon::Get,
      "oauth2::filters::AuthorizationFilter"
    );
    METHOD_LIST_END

    void list(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void create(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
    );
    void getBySlug(
      const ::drogon::HttpRequestPtr &req,
      std::function<void(const ::drogon::HttpResponsePtr &)> &&callback,
      const std::string &slug
    );
};

}  // namespace authforge::drogon::controllers
