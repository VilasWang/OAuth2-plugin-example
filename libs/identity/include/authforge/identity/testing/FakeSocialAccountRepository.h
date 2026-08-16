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
#include <set>
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
    // Test seam: make findLinkedUser answer RepositoryError (a DB outage
    // must NOT fall through to account creation — PR-review finding 3).
    bool failFind = false;
    // #54 test seam: (provider, subject) keys whose "linked user" must be
    // rejected (soft-deleted / locked) — findLinkedUser answers
    // AccountUnavailable for these even though a mapping exists.
    std::set<std::string> unavailableKeys;
    // #54 test seam: usernames that behave like a conflicting row under the
    // production upsert's ON CONFLICT DO NOTHING — createLinkedUser fails
    // (nullopt) instead of adopting the row.
    std::set<std::string> conflictingUsernames;

    static std::string key(const std::string &provider, const std::string &subject)
    {
        return provider + "|" + subject;
    }

    void findLinkedUser(
      const std::string &provider,
      const std::string &subject,
      LookupCallback &&cb) override
    {
        const std::string k = key(provider, subject);
        if (failFind)
        {
            cb(SocialLinkStatus::RepositoryError, SocialAccountLookup{});
            return;
        }
        if (unavailableKeys.count(k) > 0)
        {
            cb(SocialLinkStatus::AccountUnavailable, SocialAccountLookup{});
            return;
        }
        auto it = linked.find(k);
        if (it == linked.end())
        {
            cb(SocialLinkStatus::NoMapping, SocialAccountLookup{});
            return;
        }
        cb(SocialLinkStatus::Linked, it->second);
    }

    void createLinkedUser(
      const std::string &provider,
      const std::string &subject,
      const std::string &username,
      const std::string & /*email*/,
      CreateCallback &&cb) override
    {
        if (failCreate || conflictingUsernames.count(username) > 0)
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
