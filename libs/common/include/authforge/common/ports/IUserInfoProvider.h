#pragma once

// Task 13 (authforge-sdk-refactor, design.md §5.2/§5.3/§6): port interfaces
// for the shared Domain kernel.
//
// IUserInfoProvider is the port oauth2's OIDC userinfo endpoint support
// uses to fetch OIDC userinfo claims for an internal user id, without
// oauth2 compiling a dependency on libs/identity. See ISubjectResolver.h
// for the full 方案 A rationale and the async-callback design-consistency
// note (identity's future implementation is expected to be backed by the
// existing IUserRepository::getUserInfo(int32_t, ...), which is async).
//
// Return type: Json::Value (design.md §4.1 rule 1 explicitly allows jsoncpp
// in the Domain layer: "禁止 #include <drogon/...>；允许依赖 jsoncpp"), kept
// as a JSON claims bag rather than a fixed C++ struct with named fields --
// OIDC userinfo claims (RFC/OIDC Core §5.1: sub, name, email, ... plus
// arbitrary custom claims) are an open-ended, provider/deployment-specific
// set that the existing IUserRepository::getUserInfo already returns as
// Json::Value (OptionalJsonCallback), and this port preserves that same
// shape rather than inventing a closed DTO that would need to be extended
// every time a deployment adds a custom claim.

#include <cstdint>
#include <functional>
#include <json/json.h>
#include <optional>

namespace authforge::common::ports
{

/**
 * @brief Provides OIDC userinfo claims for an internal user id.
 */
class IUserInfoProvider
{
  public:
    using UserInfoCallback = std::function<void(std::optional<Json::Value>)>;

    virtual ~IUserInfoProvider() = default;

    /**
     * @brief Get OIDC userinfo claims for a user by internal user id.
     * Invokes `cb` with the claims JSON object, or std::nullopt if the
     * user does not exist.
     */
    virtual void getUserInfo(int32_t internalUserId, UserInfoCallback &&cb) = 0;
};

}  // namespace authforge::common::ports
