// libs/identity/include/fulla/identity/testing/FakeSocialAccountRepository.h
//
// Shared test double for fulla::identity::ISocialAccountRepository.
// Promoted (verbatim behavior) from the anonymous-namespace fake at
// libs/identity/test/SocialAuthServiceTest.cc:88-134 so HTTP integration tests
// can reuse it for the GitHub find-or-create-linked-account path without a DB.
// See FakeOAuthHttpClient.h's header comment for why these fakes live here
// (header-only under libs/identity/include/.../testing/) rather than in
// libs/common/testing.

#pragma once

#include <fulla/identity/ISocialAccountRepository.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fulla::identity::testing
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
    // See deleteLink: post-delete, pre-callback hook for #73a race tests.
    std::function<void()> onDeleteStart;
    bool failPasswordCheck = false;
    // Test seam: make insertLink answer Conflict even though the map lookup
    // said NoMapping -- simulates the UNIQUE(provider, subject) race where
    // another user claims the subject between the pre-check and the insert.
    bool forceInsertConflict = false;
    // #70: platform subject per internal user id. Absent entries synthesize
    // "sub-<id>" (the InMemoryUserRepository convention) so social token
    // issuance always has a non-empty subject; PG-backed tests point real
    // users' rows at their actual public_sub.
    std::unordered_map<int32_t, std::string> userPublicSubs;

    std::string publicSubFor(int32_t userId) const
    {
        auto it = userPublicSubs.find(userId);
        return it != userPublicSubs.end() ? it->second : "sub-" + std::to_string(userId);
    }

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
        SocialAccountLookup entry = it->second;
        if (entry.publicSub.empty())
            entry.publicSub = publicSubFor(entry.userId);  // #70
        cb(SocialLinkStatus::Linked, entry);
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
        entry.publicSub = publicSubFor(entry.userId);  // #70
        linked[key(provider, subject)] = entry;

        LinkNewSocialAccountResult result;
        result.userId = entry.userId;
        result.username = entry.username;
        result.publicSub = entry.publicSub;
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
        // #73a race-simulation hook: invoked after the delete lands but
        // before the caller's callback -- lets tests mutate the map the way a
        // concurrent unlink of ANOTHER provider's link would.
        if (onDeleteStart)
            onDeleteStart();
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

}  // namespace fulla::identity::testing
