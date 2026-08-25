#pragma once

// Task 15 (fulla-sdk-refactor, design.md §6/§8): fake implementations
// of common::ports interfaces. See FakeClock.h for the placement
// rationale.
//
// FakeUserInfoProvider: an in-memory IUserInfoProvider for oauth2 Domain
// tests (M2b+) exercising OIDC userinfo-claims-dependent logic against a
// controllable claims set, without a real identity implementation.

#include <fulla/common/ports/IUserInfoProvider.h>

#include <unordered_map>

namespace fulla::common::testing
{

class FakeUserInfoProvider : public fulla::common::ports::IUserInfoProvider
{
  public:
    void getUserInfo(int32_t internalUserId, UserInfoCallback &&cb) override
    {
        auto it = claims_.find(internalUserId);
        if (it == claims_.end())
        {
            cb(std::nullopt);
            return;
        }
        cb(it->second);
    }

    /// Set the userinfo claims JSON returned for `internalUserId`.
    void setUserInfo(int32_t internalUserId, Json::Value claims)
    {
        claims_[internalUserId] = std::move(claims);
    }

    /// Remove all registered userinfo claims.
    void clear()
    {
        claims_.clear();
    }

  private:
    std::unordered_map<int32_t, Json::Value> claims_;
};

}  // namespace fulla::common::testing
