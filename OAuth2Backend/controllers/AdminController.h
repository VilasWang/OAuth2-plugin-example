#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class AdminController : public drogon::HttpController<AdminController>
{
  public:
    METHOD_LIST_BEGIN
    // Protect this path with AuthorizationFilter
    ADD_METHOD_TO(AdminController::dashboard,
                  "/api/admin/dashboard",
                  Get,
                  "AuthorizationFilter");
    METHOD_LIST_END

    void dashboard(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);
};
