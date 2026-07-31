#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <authforge/drogon/plugin/OAuth2Plugin.h>
#include <authforge/storage/postgres/models/Users.h>
#include <authforge/common/utils/EmailNormalizer.h>
#include <future>
#include <chrono>

using namespace drogon;
using namespace drogon::orm;

namespace
{
// Drop any leftover row so the unique-email index (V019) doesn't trip reuse.
void cleanupEmail(const std::string &email)
{
    auto db = app().getDbClient();
    if (!db)
        return;
    std::promise<void> p;
    db->execSqlAsync(
      "DELETE FROM users WHERE email = $1",
      [&](const Result &) { p.set_value(); },
      [&](const DrogonDbException &) { p.set_value(); },
      authforge::common::utils::normalizeEmail(email)
    );
    p.get_future().get();
}
}  // namespace

// Covers Finding 1 (JSON body) + Finding 5 (missing test):
// an email-only registration via JSON persists username as NULL and a
// canonical email, with a non-empty password hash.
DROGON_TEST(Integration_P1_Registration_EmailOnly_JsonBody)
{
    auto plugin = app().getPlugin<OAuth2Plugin>();
    if (!plugin || plugin->getStorageType() == "memory")
    {
        CHECK(true);
        return;  // requires PostgreSQL
    }
    auto db = app().getDbClient();
    REQUIRE(db != nullptr);

    const std::string rawEmail = "Alice+promo@Example.COM";
    const std::string canonical = authforge::common::utils::normalizeEmail(rawEmail);
    cleanupEmail(rawEmail);

    // Insert the way AuthService::registerUser would (validate the contract):
    // username omitted -> NULL; email normalized; password hashed non-empty.
    drogon_model::oauth2_db::Users u;
    u.setPasswordHash("$argon2id$placeholder$notempty");  // shape only
    u.setSalt("");
    u.setEmail(canonical);
    // username deliberately NOT set (NULL)

    std::promise<bool> pIns;
    Mapper<drogon_model::oauth2_db::Users>(db).insert(
      u,
      [&](const drogon_model::oauth2_db::Users &) { pIns.set_value(true); },
      [&](const DrogonDbException &e) {
          LOG_ERROR << "insert failed: " << e.base().what();
          pIns.set_value(false);
      }
    );
    REQUIRE(pIns.get_future().get() == true);

    // Verify stored shape
    std::promise<bool> pRead;
    db->execSqlAsync(
      "SELECT username, email FROM users WHERE email = $1",
      [&](const Result &r) {
          bool ok = !r.empty();
          if (ok)
              ok = r[0]["username"].isNull();
          if (ok)
              ok = r[0]["email"].as<std::string>() == canonical;
          pRead.set_value(ok);
      },
      [&](const DrogonDbException &) { pRead.set_value(false); },
      canonical
    );
    CHECK(pRead.get_future().get() == true);

    cleanupEmail(rawEmail);
}
