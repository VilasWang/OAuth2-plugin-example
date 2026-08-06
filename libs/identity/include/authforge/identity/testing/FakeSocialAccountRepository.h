// libs/identity/include/authforge/identity/testing/FakeSocialAccountRepository.h
//
// Shared test double for authforge::identity::ISocialAccountRepository.
// Promoted (verbatim behavior) from the anonymous-namespace fake at
// libs/identity/test/SocialAuthServiceTest.cc:88-134 so HTTP integration tests
// can reuse it for the GitHub find-or-create-linked-account path without a DB.
// See FakeOAuthHttpClient.h's header comment for why these fakes live here
// (header-only under libs/identity/include/.../testing/) rather than in
// libs/common/testing.

#pragma once

#include <authforge/identity/ISocialAccountRepository.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace authforge::identity::testing
{

class FakeSocialAccountRepository : public ISocialAccountRepository
{
  public:
    // key: provider + "|" + subject
    std::unordered_map<std::string, SocialAccountLookup> linked;
    int32_t nextUserId = 100;
    bool failCreate = false;

    static std::string key(const std::string &provider, const std::string &subject)
    {
        return provider + "|" + subject;
    }

    void findLinkedUser(
      const std::string &provider,
      const std::string &subject,
      LookupCallback &&cb) override
    {
        auto it = linked.find(key(provider, subject));
        cb(it == linked.end() ? std::nullopt : std::make_optional(it->second));
    }

    void createLinkedUser(
      const std::string &provider,
      const std::string &subject,
      const std::string &username,
      const std::string & /*email*/,
      CreateCallback &&cb) override
    {
        if (failCreate)
        {
            cb(std::nullopt);
            return;
        }
        SocialAccountLookup entry;
        entry.userId = nextUserId++;
        entry.username = username;
        linked[key(provider, subject)] = entry;

        LinkNewSocialAccountResult result;
        result.userId = entry.userId;
        result.username = entry.username;
        cb(result);
    }
};

}  // namespace authforge::identity::testing
