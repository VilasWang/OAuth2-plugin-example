// B2 social account link/unlink -- orchestration implementation.
// See SocialLinkService.h's header comment for the scope boundary and the
// design doc (docs/productization-evolution/in-progress/
// social-link-unlink-design.md §4.2) for the flow diagrams this file follows
// step by step.

#ifdef WITH_SOCIAL

#include <fulla/identity/SocialLinkService.h>

#include <utility>

namespace fulla::identity
{

namespace
{
bool isGithub(const std::string &provider)
{
    return provider == "github";
}
bool isGoogle(const std::string &provider)
{
    return provider == "google";
}
bool isWeChat(const std::string &provider)
{
    return provider == "wechat";
}
}  // namespace

SocialLinkService::SocialLinkService(
  std::shared_ptr<GitHubAuthService> gitHubService,
  std::shared_ptr<GoogleAuthService> googleService,
  std::shared_ptr<WeChatAuthService> weChatService,
  std::shared_ptr<ISocialAccountRepository> accountRepo,
  std::shared_ptr<IWebAuthnRepository> webAuthnRepo,
  std::shared_ptr<fulla::common::ports::ICryptoProvider> cryptoProvider
)
    : gitHubService_(std::move(gitHubService)),
      googleService_(std::move(googleService)),
      weChatService_(std::move(weChatService)),
      accountRepo_(std::move(accountRepo)),
      webAuthnRepo_(std::move(webAuthnRepo)),
      cryptoProvider_(std::move(cryptoProvider))
{
}

bool SocialLinkService::isValidProvider(const std::string &provider)
{
    return isGithub(provider) || isGoogle(provider) || isWeChat(provider);
}

void SocialLinkService::linkAccount(
  const std::string &provider,
  const std::string &code,
  int32_t internalUserId,
  std::function<void(SocialLinkOpResult)> &&cb
)
{
    auto sharedCb = std::make_shared<std::function<void(SocialLinkOpResult)>>(std::move(cb));

    // Split the two failure causes: an unsupported provider is a client
    // error, a missing repository is a wiring defect that must surface as
    // 500-class (NotConfigured), not as "unsupported provider" (#74).
    if (!isValidProvider(provider))
    {
        SocialLinkOpResult invalid;
        invalid.status = SocialLinkOpStatus::InvalidProvider;
        (*sharedCb)(std::move(invalid));
        return;
    }
    if (!accountRepo_)
    {
        SocialLinkOpResult unwired;
        unwired.status = SocialLinkOpStatus::NotConfigured;
        (*sharedCb)(std::move(unwired));
        return;
    }

    // Step 1: provider code exchange -> subject. Google/WeChat's login() is
    // already profile-only; GitHub uses fetchProfile (login() would
    // find-or-create a local account -- exactly the side effect linking an
    // EXISTING user must not trigger).
    auto onSubject =
      [accountRepo = accountRepo_, sharedCb, provider, internalUserId](
        const std::string &errorCode, const std::string &subject) mutable {
          // W2 (PR review): a 200 userinfo response that is missing its
          // identifier (Google's .get("sub", ""), WeChat's .get("openid",
          // "")) must be treated as a failed exchange -- an empty subject
          // reaching insertLink would permanently claim the
          // UNIQUE(provider, '') slot. GitHub is guarded upstream already
          // (fetchProfile rejects id <= 0).
          if (!errorCode.empty() || subject.empty())
          {
              SocialLinkOpResult result;
              result.status = SocialLinkOpStatus::ExchangeFailed;
              result.errorCode =
                errorCode.empty() ? "VALIDATION_INVALID_INPUT" : errorCode;
              (*sharedCb)(std::move(result));
              return;
          }

          // Step 2: (provider, subject) conflict pre-check. AccountUnavailable
          // (mapping owned by a soft-deleted/locked account) is reported with
          // the same AlreadyLinkedToOtherUser wording as a live conflict -- no
          // account-status enumeration (mirrors findLinkedUser's own #54 rule).
          accountRepo->findLinkedUser(
            provider,
            subject,
            [accountRepo, sharedCb, provider, subject, internalUserId](
              SocialLinkStatus status, const SocialAccountLookup &existing) mutable {
                switch (status)
                {
                case SocialLinkStatus::Linked:
                {
                    SocialLinkOpResult result;
                    result.status = existing.userId == internalUserId
                                      ? SocialLinkOpStatus::AlreadyLinkedToSelf
                                      : SocialLinkOpStatus::AlreadyLinkedToOtherUser;
                    (*sharedCb)(std::move(result));
                    return;
                }
                case SocialLinkStatus::AccountUnavailable:
                {
                    SocialLinkOpResult result;
                    result.status = SocialLinkOpStatus::AlreadyLinkedToOtherUser;
                    (*sharedCb)(std::move(result));
                    return;
                }
                case SocialLinkStatus::RepositoryError:
                {
                    SocialLinkOpResult result;
                    result.status = SocialLinkOpStatus::RepositoryError;
                    (*sharedCb)(std::move(result));
                    return;
                }
                case SocialLinkStatus::NoMapping:
                    break;
                }

                // Step 3: one-link-per-provider pre-check (service-level rule;
                // the DB deliberately has no (provider, internal_user_id)
                // constraint -- design D5).
                accountRepo->listForUser(
                  internalUserId,
                  [accountRepo, sharedCb, provider, subject, internalUserId](
                    std::optional<std::vector<SocialLinkEntry>> entries) mutable {
                      if (!entries)
                      {
                          SocialLinkOpResult result;
                          result.status = SocialLinkOpStatus::RepositoryError;
                          (*sharedCb)(std::move(result));
                          return;
                      }
                      for (const auto &e : *entries)
                      {
                          if (e.provider == provider)
                          {
                              SocialLinkOpResult result;
                              result.status = SocialLinkOpStatus::ProviderConflictForUser;
                              (*sharedCb)(std::move(result));
                              return;
                          }
                      }

                      // Step 4: insert. The UNIQUE(provider, subject)
                      // constraint backstops the pre-check race; Conflict ==
                      // another user claimed the subject in between.
                      accountRepo->insertLink(
                        provider,
                        subject,
                        internalUserId,
                        [sharedCb, provider, subject](LinkMutationStatus mutation) mutable {
                            SocialLinkOpResult result;
                            switch (mutation)
                            {
                            case LinkMutationStatus::Inserted:
                                result.status = SocialLinkOpStatus::Ok;
                                result.entry.provider = provider;
                                result.entry.subject = subject;
                                break;
                            case LinkMutationStatus::Conflict:
                                result.status = SocialLinkOpStatus::AlreadyLinkedToOtherUser;
                                break;
                            case LinkMutationStatus::Deleted:
                            case LinkMutationStatus::NoLink:
                            case LinkMutationStatus::Error:
                                result.status = SocialLinkOpStatus::RepositoryError;
                                break;
                            }
                            (*sharedCb)(std::move(result));
                        }
                      );
                  }
                );
            }
          );
      };

    if (isGithub(provider))
    {
        if (!gitHubService_)
        {
            SocialLinkOpResult result;
            result.status = SocialLinkOpStatus::NotConfigured;
            (*sharedCb)(std::move(result));
            return;
        }
        auto service = gitHubService_;
        service->fetchProfile(
          code,
          [onSubject = std::move(onSubject)](GitHubProfileResult profile) mutable {
              onSubject(profile.errorCode, std::to_string(profile.githubId));
          }
        );
        return;
    }

    if (isGoogle(provider))
    {
        if (!googleService_)
        {
            SocialLinkOpResult result;
            result.status = SocialLinkOpStatus::NotConfigured;
            (*sharedCb)(std::move(result));
            return;
        }
        auto service = googleService_;
        service->login(
          code,
          [onSubject = std::move(onSubject)](GoogleLoginResult result) mutable {
              onSubject(result.errorCode, result.profile.sub);
          }
        );
        return;
    }

    // isValidProvider() filtered everything but wechat by now.
    if (!weChatService_)
    {
        SocialLinkOpResult result;
        result.status = SocialLinkOpStatus::NotConfigured;
        (*sharedCb)(std::move(result));
        return;
    }
    auto service = weChatService_;
    service->login(
      code,
      [onSubject = std::move(onSubject)](WeChatLoginResult result) mutable {
          onSubject(result.errorCode, result.profile.openid);
      }
    );
}

void SocialLinkService::unlinkAccount(
  const std::string &provider,
  int32_t internalUserId,
  std::function<void(SocialLinkOpResult)> &&cb
)
{
    auto sharedCb = std::make_shared<std::function<void(SocialLinkOpResult)>>(std::move(cb));

    // Same split as linkAccount (#74): unsupported provider -> client error;
    // missing repository -> wiring defect (RepositoryError, 500-class --
    // matching listAccounts' existing null-repo answer).
    if (!isValidProvider(provider))
    {
        SocialLinkOpResult invalid;
        invalid.status = SocialLinkOpStatus::InvalidProvider;
        (*sharedCb)(std::move(invalid));
        return;
    }
    if (!accountRepo_)
    {
        SocialLinkOpResult unwired;
        unwired.status = SocialLinkOpStatus::RepositoryError;
        (*sharedCb)(std::move(unwired));
        return;
    }

    // Step 1: the user's links -- both the NoLink check and the guard's
    // "is this the last one" count come from the same read.
    accountRepo_->listForUser(
      internalUserId,
      [accountRepo = accountRepo_, webAuthnRepo = webAuthnRepo_, sharedCb, provider, internalUserId](
        std::optional<std::vector<SocialLinkEntry>> entries) mutable {
          if (!entries)
          {
              SocialLinkOpResult result;
              result.status = SocialLinkOpStatus::RepositoryError;
              (*sharedCb)(std::move(result));
              return;
          }
          const SocialLinkEntry *target = nullptr;
          for (const auto &e : *entries)
          {
              if (e.provider == provider)
              {
                  target = &e;
                  break;
              }
          }
          if (!target)
          {
              SocialLinkOpResult result;
              result.status = SocialLinkOpStatus::NoLink;
              (*sharedCb)(std::move(result));
              return;
          }

          // Step 2: last-credential guard -- removing the user's ONLY social
          // link with no other usable credential would lock them out
          // permanently (social-created accounts carry a random password
          // placeholder). Usable = password (#68) OR registered WebAuthn
          // credential (#73b; passkey-only users were false-refused before).
          // webAuthnRepo == nullptr keeps the password-only guard: an
          // unverifiable dependency must not widen the refusal.
          // Copy target->subject by value: `target` points into `entries`,
          // whose lifetime ends when this callback returns, while doDelete's
          // continuation runs later.
          const std::string targetSubject = target->subject;
          const size_t originalLinkCount = entries->size();
          // Step 3 (#73a): after a multi-link delete, re-read the list; a
          // concurrent unlink of the OTHER link can leave the user with zero
          // credentials. The delete is already committed -- this is detection
          // for the controller to alert on, not a rollback.
          auto doDelete =
            [accountRepo, webAuthnRepo, sharedCb, provider, internalUserId, targetSubject,
             originalLinkCount]() mutable {
                accountRepo->deleteLink(
                  provider,
                  internalUserId,
                  [accountRepo, webAuthnRepo, sharedCb, provider, targetSubject, internalUserId,
                   originalLinkCount](LinkMutationStatus mutation) mutable {
                      auto finishOk = [sharedCb, provider, targetSubject](bool lockoutRisk) {
                          SocialLinkOpResult result;
                          result.status = SocialLinkOpStatus::Ok;
                          result.entry.provider = provider;
                          result.entry.subject = targetSubject;
                          result.lockoutRiskObserved = lockoutRisk;
                          (*sharedCb)(std::move(result));
                      };
                      switch (mutation)
                      {
                      case LinkMutationStatus::Deleted:
                      {
                          // #73a: only multi-link unlinks can race the guard
                          // (single-link deletions passed it explicitly, and
                          // the single link remaining empty is the expected
                          // outcome there). Re-check only that window.
                          if (originalLinkCount < 2)
                          {
                              finishOk(false);
                              return;
                          }
                          accountRepo->listForUser(
                            internalUserId,
                            [accountRepo, webAuthnRepo, internalUserId, sharedCb, finishOk](
                              std::optional<std::vector<SocialLinkEntry>> remaining) mutable {
                                if (!remaining || !remaining->empty())
                                {
                                    // Re-check failed or links remain: no
                                    // confirmed lockout -- do not cry wolf.
                                    finishOk(false);
                                    return;
                                }
                                // Zero links now. Password still usable?
                                accountRepo->userHasUsablePassword(
                                  internalUserId,
                                  [webAuthnRepo, internalUserId, sharedCb, finishOk](
                                    std::optional<bool> passwordUsable) mutable {
                                      if (!passwordUsable)
                                      {
                                          // Indeterminate: alert ONLY on a
                                          // confirmed zero-credential state.
                                          finishOk(false);
                                          return;
                                      }
                                      if (*passwordUsable)
                                      {
                                          finishOk(false);
                                          return;
                                      }
                                      if (!webAuthnRepo)
                                      {
                                          // Passkeys unverifiable (dep not
                                          // wired): skip the alert rather
                                          // than false-positive on
                                          // passkey-only users.
                                          finishOk(false);
                                          return;
                                      }
                                      webAuthnRepo->listCredentials(
                                        internalUserId,
                                        [finishOk](std::vector<WebAuthnCredentialSummary> creds) {
                                            finishOk(creds.empty());
                                        }
                                      );
                                  }
                                );
                            }
                          );
                          return;
                      }
                      case LinkMutationStatus::NoLink:  // raced with another unlink
                      {
                          SocialLinkOpResult result;
                          result.status = SocialLinkOpStatus::NoLink;
                          (*sharedCb)(std::move(result));
                          break;
                      }
                      case LinkMutationStatus::Inserted:
                      case LinkMutationStatus::Conflict:
                      case LinkMutationStatus::Error:
                      {
                          SocialLinkOpResult result;
                          result.status = SocialLinkOpStatus::RepositoryError;
                          (*sharedCb)(std::move(result));
                          break;
                      }
                      }
                  }
                );
            };

          if (entries->size() == 1)
          {
              accountRepo->userHasUsablePassword(
                internalUserId,
                [doDelete = std::move(doDelete), webAuthnRepo, sharedCb, internalUserId](
                  std::optional<bool> usable) mutable {
                    if (!usable)
                    {
                        SocialLinkOpResult result;
                        result.status = SocialLinkOpStatus::RepositoryError;
                        (*sharedCb)(std::move(result));
                        return;
                    }
                    if (*usable)
                    {
                        doDelete();
                        return;
                    }
                    if (!webAuthnRepo)
                    {
                        SocialLinkOpResult result;
                        result.status = SocialLinkOpStatus::LastCredentialGuard;
                        (*sharedCb)(std::move(result));
                        return;
                    }
                    // #73b: passkeys count as a usable credential.
                    webAuthnRepo->listCredentials(
                      internalUserId,
                      [doDelete = std::move(doDelete), sharedCb](
                        std::vector<WebAuthnCredentialSummary> creds) mutable {
                          if (creds.empty())
                          {
                              SocialLinkOpResult result;
                              result.status = SocialLinkOpStatus::LastCredentialGuard;
                              (*sharedCb)(std::move(result));
                              return;
                          }
                          doDelete();
                      }
                    );
                }
              );
              return;
          }
          doDelete();
      }
    );
}

void SocialLinkService::listAccounts(
  int32_t internalUserId,
  std::function<void(SocialLinkOpStatus, std::vector<SocialLinkEntry>)> &&cb
)
{
    if (!accountRepo_)
    {
        cb(SocialLinkOpStatus::RepositoryError, {});
        return;
    }
    accountRepo_->listForUser(
      internalUserId,
      [cb = std::move(cb)](std::optional<std::vector<SocialLinkEntry>> entries) mutable {
          if (!entries)
          {
              cb(SocialLinkOpStatus::RepositoryError, {});
              return;
          }
          cb(SocialLinkOpStatus::Ok, std::move(*entries));
      }
    );
}

}  // namespace fulla::identity

#endif  // WITH_SOCIAL
