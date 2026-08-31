// Internal (#70): the provider-agnostic four-state account resolution
// shared by the social login services (Google/WeChat/GitHub). Lives under
// src/ on purpose — it is an implementation detail of the social slice,
// not part of the installed identity API surface.
//
// State machine (mirrors ISocialAccountRepository::findLinkedUser's
// contract):
//   Linked            -> onLinked(existing id/username/publicSub, isNew=false)
//   AccountUnavailable-> onError("AUTH_INVALID_CREDENTIALS")  (soft-deleted
//                        or locked local user — generic, no status leak)
//   RepositoryError   -> onError("DB_QUERY_ERROR")            (never falls
//                        through to creation)
//   NoMapping         -> autoCreate gate:
//                        false -> onError("AUTH_SOCIAL_ACCOUNT_NOT_LINKED")
//                                 ("not linked" is an authorization state,
//                                 403, not an authentication failure)
//                        true  -> createLinkedUser; a username conflict is
//                                 retried ONCE with a random suffix, then
//                                 reported as VALIDATION_USERNAME_TAKEN
//                                 (a bare DB_QUERY_ERROR would be
//                                 non-actionable for the common case; the
//                                 repository logs the true cause).

#ifdef WITH_SOCIAL

#pragma once

#include <fulla/identity/ISocialAccountRepository.h>

#include <functional>
#include <memory>
#include <random>
#include <string>

namespace fulla::identity::social_detail
{

using LinkedAccountCallback =
  std::function<void(int32_t userId, const std::string &username, bool isNewUser, const std::string &publicSub)>;
using LinkErrorCallback = std::function<void(std::string errorCode)>;

inline std::string randomUsernameSuffix()
{
    // Not a secret — just collision avoidance for the retry attempt.
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(6);
    for (int i = 0; i < 6; ++i)
        out.push_back(hex[rng() & 0xF]);
    return out;
}

inline void resolveOrCreateAccount(
  const std::shared_ptr<ISocialAccountRepository> &repo,
  bool autoCreate,
  const std::string &provider,
  const std::string &subject,
  const std::string &username,
  const std::string &email,
  LinkedAccountCallback &&onLinked,
  LinkErrorCallback &&onError
)
{
    auto sharedLinked = std::make_shared<LinkedAccountCallback>(std::move(onLinked));
    auto sharedErr = std::make_shared<LinkErrorCallback>(std::move(onError));

    // Self-referencing async step: the function object outlives this frame
    // (repository callbacks fire later), so it holds itself via weak_ptr —
    // a shared_ptr self-capture would leak one node per login.
    auto sharedAttempt = std::make_shared<std::function<void(const std::string &, bool)>>();
    std::weak_ptr<std::function<void(const std::string &, bool)>> weakAttempt = sharedAttempt;
    *sharedAttempt =
      [repo, provider, subject, email, weakAttempt, sharedLinked, sharedErr](
        const std::string &candidate, bool isRetry
      ) {
          repo->createLinkedUser(
            provider,
            subject,
            candidate,
            email,
            [candidate, isRetry, weakAttempt, sharedLinked, sharedErr](
              std::optional<LinkNewSocialAccountResult> created
            ) {
                if (created)
                {
                    (*sharedLinked)(created->userId, created->username, true, created->publicSub);
                    return;
                }
                if (!isRetry)
                {
                    if (auto self = weakAttempt.lock())
                        (*self)(candidate + "_" + randomUsernameSuffix(), true);
                    return;
                }
                (*sharedErr)("VALIDATION_USERNAME_TAKEN");
            }
          );
      };

    repo->findLinkedUser(
      provider,
      subject,
      [repo, autoCreate, username, sharedAttempt, sharedLinked, sharedErr](
        SocialLinkStatus status, const SocialAccountLookup &existing
      ) {
          switch (status)
          {
          case SocialLinkStatus::Linked:
              (*sharedLinked)(existing.userId, existing.username, false, existing.publicSub);
              return;
          case SocialLinkStatus::AccountUnavailable:
              (*sharedErr)("AUTH_INVALID_CREDENTIALS");
              return;
          case SocialLinkStatus::RepositoryError:
              (*sharedErr)("DB_QUERY_ERROR");
              return;
          case SocialLinkStatus::NoMapping:
              break;
          }
          if (!autoCreate)
          {
              (*sharedErr)("AUTH_SOCIAL_ACCOUNT_NOT_LINKED");
              return;
          }
          (*sharedAttempt)(username, false);
      }
    );
}

}  // namespace fulla::identity::social_detail

#endif  // WITH_SOCIAL
