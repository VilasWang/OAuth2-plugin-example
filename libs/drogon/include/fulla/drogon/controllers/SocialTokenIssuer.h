#pragma once

// #70: shared first-party token issuance for social logins
// (GitHub/Google/WeChat). Extracted from GitHubController's private
// issueTokensForUser so all three providers mint tokens through ONE code
// path, with the subject-form bug fixed at the same time: token rows MUST
// store the platform subject (users.public_sub, a UUID) — the value every
// Bearer-authenticated handler (/api/me, change-password, MFA, WebAuthn)
// resolves via findByPublicSub. GitHub's original implementation stored
// std::to_string(internal id), so its tokens 404'd on every authenticated
// endpoint (#70 review BLOCKER); this issuer stores publicSub.
//
// Compliance trace (#70): issuance writes an AuditEvent
// (SOCIAL_LOGIN_TOKEN_ISSUED) instead of an oauth2_user_consents row. A
// consent row would silently satisfy the consent screen's Tier-3 check and
// pre-approve the configured client for scopes the user was never prompted
// for — fabricated consent evidence. The audit event records that a
// first-party token was issued after an upstream-provider authentication;
// an explicit social-consent interaction stays a registered follow-up
// (docs/domains/social-login.md).

#include <drogon/drogon.h>

#include <functional>
#include <memory>
#include <string>

// #70: forward declaration, mirroring GitHubController.h's pattern.
class OAuth2Plugin;

namespace fulla::drogon::controllers
{

class SocialTokenIssuer
{
  public:
    using CallbackPtr = std::shared_ptr<std::function<void(const ::drogon::HttpResponsePtr &)>>;

    /**
     * @brief Mint + persist a first-party opaque token pair for a social
     * login and answer 200 with the standard token JSON.
     *
     * @param req The originating login request (audit context).
     * @param callbackPtr Response callback (shared: async persistence).
     * @param plugin Resolved OAuth2Plugin (saveTokenPair, TTLs, issuer).
     * @param provider "github" | "google" | "wechat" (audit details).
     * @param internalUserId Internal user id (audit details only).
     * @param userPublicSub Platform subject (users.public_sub) — MUST be
     * non-empty; it is what gets stored in the token rows.
     */
    static void issueTokensForUser(
      const ::drogon::HttpRequestPtr &req,
      const CallbackPtr &callbackPtr,
      ::OAuth2Plugin *plugin,
      const std::string &provider,
      int64_t internalUserId,
      const std::string &userPublicSub
    );
};

}  // namespace fulla::drogon::controllers
