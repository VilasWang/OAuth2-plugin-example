#pragma once
#include <drogon/HttpController.h>
using namespace drogon;

class WeChatController : public drogon::HttpController<WeChatController>
{
public:
    METHOD_LIST_BEGIN
    // Endpoint to exchange WeChat code for User Info
    ADD_METHOD_TO(WeChatController::login, "/api/wechat/login", Post, Options);
    METHOD_LIST_END

    void login(const HttpRequestPtr &req,
               std::function<void(const HttpResponsePtr &)> &&callback);
};
