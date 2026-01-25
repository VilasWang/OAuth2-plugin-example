#pragma once

#include <drogon/drogon.h>
#include <functional>
#include <string>

namespace services
{

class AuthService
{
  public:
    // 异步验证用户凭据
    // callback: 成功返回 true，失败返回 false
    static void validateUser(
        const std::string &username,
        const std::string &password,
        std::function<void(std::optional<int> userId)> &&callback);

    // 异步注册用户
    // callback: 成功返回空字符串，失败返回错误消息
    static void registerUser(
        const std::string &username,
        const std::string &password,
        const std::string &email,
        std::function<void(const std::string &error)> &&callback);
};

}  // namespace services
