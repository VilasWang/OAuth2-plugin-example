#pragma once

#ifdef WITH_SOCIAL

// B2 social account link/unlink (docs/productization-evolution/in-progress/
// social-link-unlink-design.md §4.2): framework-independent orchestration
// behind UserSelfServiceController's /api/me/social/links* routes. Verifies a
// provider authorization code via the existing per-provider services
// (exchange + profile only -- NO local-account find-or-create), then manages
// the (provider, subject) -> internal_user_id mapping rows through
// ISocialAccountRepository's link-lifecycle methods.
//
// Boundary (same rationale as SocialAuthService.h's own scope note): this
// service stops at "mapping row inserted/deleted/listed". Token issuance,
// audit emission, and public_sub -> internal-id resolution stay with the
// controller/assembly layer.
//
// Callback-capture discipline (db-operations.md): stateless service; every
// async chain copies its shared_ptr dependencies + the shared callback into
// the lambda by value. No [this], no [&] captures.

#include <authforge/identity/ISocialAccountRepository.h>
#include <authforge/identity/SocialAuthService.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace authforge::identity
{

/**
 * @brief Outcome of a link/unlink orchestration. Controller maps these onto
 * the HTTP error-envelope table (design doc §3.4); RepositoryError is always
 * a 500, exchange failures carry a provider-level errorCode.
 */
enum class SocialLinkOpStatus
{
    Ok,                      ///< Mapping inserted (link) / deleted (unlink).
    InvalidProvider,         ///< provider not in {github, google, wechat}.
    NotConfigured,           ///< The provider's service was never injected.
    ExchangeFailed,          ///< Code exchange / userinfo fetch failed
                             ///< (errorCode carries e.g. NET_CONNECTION_FAILED).
    AlreadyLinkedToSelf,     ///< (provider, subject) maps to this user.
    AlreadyLinkedToOtherUser,///< (provider, subject) maps to another user
                             ///  (or to a dead account -- same wording, no
                             ///  account-status leak).
    ProviderConflictForUser, ///< User already has a DIFFERENT mapping for
                             ///  this provider (unlink first).
    NoLink,                  ///< Unlink: no mapping for (user, provider).
    LastCredentialGuard,     ///< Unlink: last social link and no usable
                             ///  password -- refusing to avoid lockout.
    RepositoryError
};

/**
 * @brief Result of SocialLinkService::linkAccount / unlinkAccount.
 */
struct SocialLinkOpResult
{
    SocialLinkOpStatus status = SocialLinkOpStatus::RepositoryError;
    std::string errorCode;     ///< Provider-level error code (ExchangeFailed).
    SocialLinkEntry entry;     ///< Populated on Ok (link: provider+subject;
                               ///< unlink: provider only).
};

/**
 * @brief Self-service social account link/unlink orchestration.
 */
class SocialLinkService
{
  public:
    SocialLinkService(
      std::shared_ptr<GitHubAuthService> gitHubService,
      std::shared_ptr<GoogleAuthService> googleService,
      std::shared_ptr<WeChatAuthService> weChatService,
      std::shared_ptr<ISocialAccountRepository> accountRepo
    );

    /**
     * @brief Verify @p code against @p provider and link the resolved
     * provider identity to @p internalUserId.
     *
     * Flow: provider exchange -> subject; findLinkedUser conflict pre-check;
     * one-link-per-provider pre-check; insertLink (UNIQUE constraint is the
     * race backstop). Never creates or mutates the local user row.
     */
    void linkAccount(
      const std::string &provider,
      const std::string &code,
      int32_t internalUserId,
      std::function<void(SocialLinkOpResult)> &&cb
    );

    /**
     * @brief Remove the user's mapping for @p provider, guarding against
     * removing the user's last usable credential (no usable password + no
     * other social link -> LastCredentialGuard).
     */
    void unlinkAccount(
      const std::string &provider,
      int32_t internalUserId,
      std::function<void(SocialLinkOpResult)> &&cb
    );

    /**
     * @brief List the user's linked provider identities.
     * @param cb Ok + entries, or RepositoryError with an empty vector.
     */
    void listAccounts(
      int32_t internalUserId,
      std::function<void(SocialLinkOpStatus, std::vector<SocialLinkEntry>)> &&cb
    );

    /// The provider names linkAccount/unlinkAccount accept.
    static bool isValidProvider(const std::string &provider);

  private:
    std::shared_ptr<GitHubAuthService> gitHubService_;
    std::shared_ptr<GoogleAuthService> googleService_;
    std::shared_ptr<WeChatAuthService> weChatService_;
    std::shared_ptr<ISocialAccountRepository> accountRepo_;
};

}  // namespace authforge::identity

#endif  // WITH_SOCIAL
