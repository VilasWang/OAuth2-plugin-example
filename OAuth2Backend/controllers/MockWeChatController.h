#pragma once
#include <drogon/HttpController.h>
using namespace drogon;

class MockWeChatController : public drogon::HttpController<MockWeChatController>
{
  public:
    METHOD_LIST_BEGIN
    // Simulates: https://open.weixin.qq.com/connect/qrconnect
    ADD_METHOD_TO(MockWeChatController::authorize,
                  "/mock/wechat/authorize",
                  Get);

    // Simulates: https://api.weixin.qq.com/sns/oauth2/access_token
    ADD_METHOD_TO(MockWeChatController::token,
                  "/mock/wechat/token",
                  Post,
                  Options);

    // Simulates: https://api.weixin.qq.com/sns/userinfo
    ADD_METHOD_TO(MockWeChatController::userInfo,
                  "/mock/wechat/userinfo",
                  Get,
                  Options);
    METHOD_LIST_END

    void authorize(const HttpRequestPtr &req,
                   std::function<void(const HttpResponsePtr &)> &&callback);

    void token(const HttpRequestPtr &req,
               std::function<void(const HttpResponsePtr &)> &&callback);

    void userInfo(const HttpRequestPtr &req,
                  std::function<void(const HttpResponsePtr &)> &&callback);
};
