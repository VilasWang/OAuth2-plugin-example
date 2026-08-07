// libs/identity/include/authforge/identity/testing/FakeWebAuthnRepository.h
//
// Shared test double for authforge::identity::IWebAuthnRepository. Promoted
// (verbatim behavior) from the anonymous-namespace fake at
// libs/identity/test/WebAuthnServiceTest.cc:29-116 so HTTP integration tests
// can reuse it. See FakeOAuthHttpClient.h's header comment for why these fakes
// live here (header-only under libs/identity/include/.../testing/).
//
// The StoredCredential helper struct is preserved from the original local fake
// (it is the fake's own bookkeeping type, not part of IWebAuthnRepository).

#pragma once

#include <authforge/identity/IWebAuthnRepository.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace authforge::identity::testing
{

// Local bookkeeping struct (preserved from WebAuthnServiceTest.cc:19-27).
struct StoredCredential
{
    int32_t userId = 0;
    std::string publicKey;
    std::string name;
    int signCount = 0;
    int64_t createdAt = 0;
    std::optional<int64_t> lastUsedAt;
};

class FakeWebAuthnRepository : public IWebAuthnRepository
{
  public:
    std::unordered_map<std::string, StoredCredential> credentials;  // keyed by credential_id
    int64_t nextCreatedAt = 1000;
    // P1 coverage knob: force storeCredential to report a generic Error so
    // the DB_QUERY_ERROR branch in finishRegistration is reachable.
    bool forceStoreError = false;

    void storeCredential(
      int32_t userId,
      const std::string &credentialId,
      const std::string &publicKey,
      const std::string &name,
      StoreCredentialCallback &&cb) override
    {
        if (forceStoreError)
        {
            cb(StoreCredentialOutcome::Error);
            return;
        }
        if (credentials.count(credentialId) != 0)
        {
            cb(StoreCredentialOutcome::DuplicateCredentialId);
            return;
        }

        StoredCredential cred;
        cred.userId = userId;
        cred.publicKey = publicKey;
        cred.name = name;
        cred.signCount = 0;
        cred.createdAt = nextCreatedAt++;
        credentials[credentialId] = cred;
        cb(StoreCredentialOutcome::Success);
    }

    void findByCredentialId(const std::string &credentialId, CredentialLookupCallback &&cb) override
    {
        auto it = credentials.find(credentialId);
        if (it == credentials.end())
        {
            cb(std::nullopt);
            return;
        }
        WebAuthnCredentialLookup lookup;
        lookup.userId = it->second.userId;
        lookup.publicSub = "sub-" + std::to_string(it->second.userId);
        lookup.signCount = it->second.signCount;
        cb(lookup);
    }

    void updateSignCount(
      const std::string &credentialId,
      int newSignCount,
      BoolCallback &&cb) override
    {
        auto it = credentials.find(credentialId);
        if (it == credentials.end())
        {
            cb(false);
            return;
        }
        it->second.signCount = newSignCount;
        it->second.lastUsedAt = 12345;
        cb(true);
    }

    void listCredentials(int32_t userId, ListCredentialsCallback &&cb) override
    {
        std::vector<WebAuthnCredentialSummary> result;
        for (const auto &[credentialId, cred] : credentials)
        {
            if (cred.userId != userId)
                continue;
            WebAuthnCredentialSummary summary;
            summary.credentialId = credentialId;
            summary.name = cred.name;
            summary.signCount = cred.signCount;
            summary.createdAt = cred.createdAt;
            summary.lastUsedAt = cred.lastUsedAt;
            result.push_back(summary);
        }
        cb(std::move(result));
    }
};

}  // namespace authforge::identity::testing
