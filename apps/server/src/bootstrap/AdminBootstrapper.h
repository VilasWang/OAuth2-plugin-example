#pragma once

#include <functional>
#include <string>

namespace bootstrap
{
/**
 * #103: first-boot administrator bootstrap.
 *
 * When enabled (auth.bootstrap_admin.enabled, default true) and NO user
 * holds the admin role yet, creates `admin` with a PBKDF2-hashed password:
 * from FULLA_BOOTSTRAP_ADMIN_PASSWORD when provided, otherwise a randomly
 * generated one that is printed to the log EXACTLY once (Jenkins/Keycloak
 * bootstrap pattern — mind log retention; see deployment docs).
 *
 * Idempotent and safe to call at every startup: the "admin role exists"
 * check and the username unique constraint both make repeats no-ops. The
 * method is deliberately a direct-callable entry (not only a beginning
 * advice) so integration tests can exercise it in isolation.
 */
class AdminBootstrapper
{
  public:
    using DoneCallback = std::function<void(bool created, const std::string &detail)>;

    /**
     * @param explicitPassword  password from FULLA_BOOTSTRAP_ADMIN_PASSWORD
     *                          (may be empty -> generate random; never logged
     *                          back when explicit)
     */
    static void run(const std::string &explicitPassword, DoneCallback &&done);
};
}  // namespace bootstrap
