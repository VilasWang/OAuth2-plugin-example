#include "AdminController.h"

void AdminController::dashboard(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback)
{
    Json::Value json;
    json["message"] = "Welcome to Admin Dashboard";
    json["status"] = "success";
    auto resp = HttpResponse::newHttpJsonResponse(json);
    callback(resp);
}
