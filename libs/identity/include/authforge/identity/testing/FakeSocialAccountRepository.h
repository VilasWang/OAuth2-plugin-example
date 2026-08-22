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
#include <vector>

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

    // B2 link/unlink seams. usersWithUsablePassword mirrors the production
    // `$pbkdf2-sha256$%` prefix test (absent = no usable password, the
    // fail-safe default social-created accounts get). The fail* flags make
    // each new method answer its repository-failure variant.
    std::set<int32_t> usersWithUsablePassword;
    bool failList = false;
    bool failInsert = false;
    bool failDelete = false;
    bool failPasswordCheck = false;
    // Test seam: make insertLink answer Conflict even though the map lookup
    // said NoMapping -- simulates the UNIQUE(provider, subject) race where
    // another user claims the subject between the pre-check and the insert.
    bool forceInsertConflict = false;

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

    // B2 link/unlink (memory semantics; the `linked` map is the single
    // source of truth so findLinkedUser and the new methods stay coherent).
    // Like the Postgres implementation, listForUser skips provider=='local'
    // rows (the seeded password-subject mapping is not a social identity).
    void listForUser(int32_t internalUserId, LinkEntriesCallback &&cb) override
    {
        if (failList)
        {
            cb(std::nullopt);
            return;
        }
        std::vector<SocialLinkEntry> entries;
        for (const auto &kv : linked)
        {
            if (kv.second.userId != internalUserId)
            {
                continue;
            }
            const size_t bar = kv.first.find('|');
            SocialLinkEntry e;
            e.provider = kv.first.substr(0, bar);
            if (e.provider == "local")
            {
                continue;
            }
            e.subject = kv.first.substr(bar + 1);
            e.linkedAt = "2026-01-01T00:00:00Z";
            entries.push_back(std::move(e));
        }
        cb(std::move(entries));
    }

    void insertLink(
      const std::string &provider,
      const std::string &subject,
      int32_t internalUserId,
      LinkMutationCallback &&cb
    ) override
    {
        if (failInsert)
        {
            cb(LinkMutationStatus::Error);
            return;
        }
        if (forceInsertConflict)
        {
            cb(LinkMutationStatus::Conflict);
            return;
        }
        const std::string k = key(provider, subject);
        if (linked.find(k) != linked.end())
        {
            // Mirrors the DB's UNIQUE(provider, subject) constraint.
            cb(LinkMutationStatus::Conflict);
            return;
        }
        SocialAccountLookup entry;
        entry.userId = internalUserId;
        entry.username = "user" + std::to_string(internalUserId);
        linked[k] = entry;
        cb(LinkMutationStatus::Inserted);
    }

    void deleteLink(
      const std::string &provider,
      int32_t internalUserId,
      LinkMutationCallback &&cb
    ) override
    {
        if (failDelete)
        {
            cb(LinkMutationStatus::Error);
            return;
        }
        bool deleted = false;
        for (auto it = linked.begin(); it != linked.end();)
        {
            const size_t bar = it->first.find('|');
            const bool matches =
              it->first.substr(0, bar) == provider && it->second.userId == internalUserId;
            if (matches)
            {
                it = linked.erase(it);
                deleted = true;
            }
            else
            {
                ++it;
            }
        }
        cb(deleted ? LinkMutationStatus::Deleted : LinkMutationStatus::NoLink);
    }

    void userHasUsablePassword(int32_t internalUserId, PasswordUsableCallback &&cb) override
    {
        if (failPasswordCheck)
        {
            cb(std::nullopt);
            return;
        }
        cb(usersWithUsablePassword.count(internalUserId) > 0);
    }
};

}  // namespace authforge::identity::testing
