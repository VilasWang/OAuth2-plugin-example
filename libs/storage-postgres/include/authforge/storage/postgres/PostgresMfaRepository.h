#pragma once

// Task 24 slice 5 (authforge-sdk-refactor, design.md §5.1/§5.4/§6):
// Adapter-layer Postgres implementation of IMfaRepository
// (libs/identity), backing authforge::identity::MfaService. Mirrors
// PostgresIdentityRepository's placement rationale (libs/storage-postgres,
// not libs/identity, because it depends on drogon::orm -- design.md §4.1
// rule 1: Domain forbids drogon::orm).
//
// Ports the raw SQL libs/drogon/src/controllers/MfaController.cc issues
// directly against the `users` table's mfa_* columns
// (mfa_secret/mfa_enabled/mfa_backup_codes/mfa_pending_client_id/
// mfa_pending_redirect_uri) into this repository.

#include <authforge/identity/IMfaRepository.h>

#include <drogon/orm/DbClient.h>

#include <memory>

namespace authforge::storage::postgres
{

class PostgresMfaRepository : public authforge::identity::IMfaRepository,
                              public std::enable_shared_from_this<PostgresMfaRepository>
{
  public:
    explicit PostgresMfaRepository(::drogon::orm::DbClientPtr dbClient)
        : dbClient_(std::move(dbClient))
    {
    }

    void getMfaData(int32_t userId, MfaDataCallback &&cb) override;
    void setSecret(int32_t userId, const std::string &secret, BoolCallback &&cb) override;
    void enable(
      int32_t userId,
      const std::vector<std::string> &hashedBackupCodes,
      BoolCallback &&cb
    ) override;
    void disable(int32_t userId, BoolCallback &&cb) override;
    void setPendingBinding(
      int32_t userId,
      const std::string &clientId,
      const std::string &redirectUri,
      BoolCallback &&cb
    ) override;
    void clearPendingBinding(int32_t userId, BoolCallback &&cb) override;

  private:
    ::drogon::orm::DbClientPtr dbClient_;
};

}  // namespace authforge::storage::postgres
