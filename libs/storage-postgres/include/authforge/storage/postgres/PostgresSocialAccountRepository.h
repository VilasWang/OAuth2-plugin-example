#pragma once

#ifdef WITH_SOCIAL

// Task 24 slice 5 (authforge-sdk-refactor, design.md §5.1/§5.4/§6):
// Adapter-layer Postgres implementation of ISocialAccountRepository
// (libs/identity), backing authforge::identity::GitHubAuthService's
// find-or-create local account flow. Ports the raw SQL
// libs/drogon/src/controllers/GitHubController.cc issues against
// `oauth2_subject_mappings`/`users`/`user_roles` into this repository.
// Guarded by WITH_SOCIAL (propagated PUBLIC from authforge::identity, see
// libs/identity/CMakeLists.txt) to match ISocialAccountRepository.h's own
// guard -- this header cannot compile without that interface.

#include <authforge/identity/ISocialAccountRepository.h>

#include <drogon/orm/DbClient.h>

#include <memory>

namespace authforge::storage::postgres
{

class PostgresSocialAccountRepository
    : public authforge::identity::ISocialAccountRepository,
      public std::enable_shared_from_this<PostgresSocialAccountRepository>
{
  public:
    explicit PostgresSocialAccountRepository(::drogon::orm::DbClientPtr dbClient)
        : dbClient_(std::move(dbClient))
    {
    }

    void findLinkedUser(
      const std::string &provider,
      const std::string &subject,
      LookupCallback &&cb
    ) override;

    void createLinkedUser(
      const std::string &provider,
      const std::string &subject,
      const std::string &username,
      const std::string &email,
      CreateCallback &&cb
    ) override;

  private:
    ::drogon::orm::DbClientPtr dbClient_;
};

}  // namespace authforge::storage::postgres

#endif  // WITH_SOCIAL
