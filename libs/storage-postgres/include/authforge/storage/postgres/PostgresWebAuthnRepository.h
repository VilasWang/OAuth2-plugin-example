#pragma once

// Task 24 slice 5 (authforge-sdk-refactor, design.md §5.1/§5.4/§6):
// Adapter-layer Postgres implementation of IWebAuthnRepository
// (libs/identity), backing authforge::identity::WebAuthnService. Ports
// the raw SQL libs/drogon/src/controllers/WebAuthnController.cc issues
// directly against the `webauthn_credentials` table (joined with `users`
// for public_sub) into this repository.

#include <authforge/identity/IWebAuthnRepository.h>

#include <drogon/orm/DbClient.h>

#include <memory>

namespace authforge::storage::postgres
{

class PostgresWebAuthnRepository : public authforge::identity::IWebAuthnRepository,
                                   public std::enable_shared_from_this<PostgresWebAuthnRepository>
{
  public:
    explicit PostgresWebAuthnRepository(::drogon::orm::DbClientPtr dbClient)
        : dbClient_(std::move(dbClient))
    {
    }

    void storeCredential(
      int32_t userId,
      const std::string &credentialId,
      const std::string &publicKey,
      const std::string &name,
      StoreCredentialCallback &&cb
    ) override;

    void findByCredentialId(
      const std::string &credentialId,
      CredentialLookupCallback &&cb
    ) override;

    void updateSignCount(
      const std::string &credentialId,
      int newSignCount,
      BoolCallback &&cb
    ) override;

    void listCredentials(int32_t userId, ListCredentialsCallback &&cb) override;

  private:
    ::drogon::orm::DbClientPtr dbClient_;
};

}  // namespace authforge::storage::postgres
