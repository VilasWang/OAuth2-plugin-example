#pragma once
#include <drogon/HttpController.h>
using namespace drogon;

class GoogleController : public drogon::HttpController<GoogleController>
{
public:
    METHOD_LIST_BEGIN
    // Endpoint to exchange Google code for User Info
    ADD_METHOD_TO(GoogleController::login, "/api/google/login", Post, Options);
    METHOD_LIST_END

    void login(const HttpRequestPtr &req,
               std::function<void(const HttpResponsePtr &)> &&callback);
};
