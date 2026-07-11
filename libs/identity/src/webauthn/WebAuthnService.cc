#include <authforge/identity/WebAuthnService.h>

#include <vector>

namespace authforge::identity
{

namespace
{

// Generate a cryptographically secure random challenge, base64url encoded
// (no padding) -- same algorithm as
// libs/oauth2/src/protocol/TokenCrypto.cc's generateSecureToken() and the
// production controller's ::oauth2::utils::generateSecureToken() call
// (both: secureRandomBytes(32) -> base64url), just expressed against the
// injected ICryptoProvider instead of a hardcoded Adapter-layer instance
// (see TokenCrypto.h's identical rationale).
std::string generateChallenge(authforge::common::ports::ICryptoProvider &crypto)
{
    constexpr size_t kChallengeBytes = 32;
    std::vector<unsigned char> buffer(kChallengeBytes);
    crypto.secureRandomBytes(buffer.data(), kChallengeBytes);
    return crypto.base64UrlEncode(buffer.data(), buffer.size());
}

}  // namespace

WebAuthnService::WebAuthnService(
  std::shared_ptr<IWebAuthnRepository> repo,
  std::shared_ptr<authforge::common::ports::ICryptoProvider> crypto,
  std::string rpId,
  std::string rpName
)
    : repo_(std::move(repo)),
      crypto_(std::move(crypto)),
      rpId_(std::move(rpId)),
      rpName_(std::move(rpName))
{
}

void WebAuthnService::beginRegistration(
  std::function<void(std::optional<WebAuthnRegistrationChallenge>)> &&callback
)
{
    if (!crypto_)
    {
        callback(std::nullopt);
        return;
    }

    WebAuthnRegistrationChallenge result;
    result.challenge = generateChallenge(*crypto_);
    result.rpId = rpId_;
    result.rpName = rpName_;
    callback(result);
}

void WebAuthnService::finishRegistration(
  int64_t userId,
  const std::string &credentialId,
  const std::string &publicKey,
  const std::string &name,
  std::function<void(const std::string &errorCode)> &&callback
)
{
    if (!repo_)
    {
        callback("INTERNAL_ERROR");
        return;
    }

    if (credentialId.empty() || publicKey.empty())
    {
        callback("VALIDATION_MISSING_REQUIRED_FIELD");
        return;
    }

    // Mirrors the controller's JSON-parsing default of "Passkey" when the
    // caller does not supply a name.
    std::string credName = name.empty() ? "Passkey" : name;

    repo_->storeCredential(
      userId,
      credentialId,
      publicKey,
      credName,
      [callback = std::move(callback)](StoreCredentialOutcome outcome) {
          switch (outcome)
          {
              case StoreCredentialOutcome::Success:
                  callback("");
                  break;
              case StoreCredentialOutcome::DuplicateCredentialId:
                  callback("VALIDATION_CREDENTIAL_ALREADY_REGISTERED");
                  break;
              case StoreCredentialOutcome::Error:
              default:
                  callback("DB_QUERY_ERROR");
                  break;
          }
      }
    );
}

void WebAuthnService::beginAuthentication(
  std::function<void(std::optional<WebAuthnAuthenticationChallenge>)> &&callback
)
{
    if (!crypto_)
    {
        callback(std::nullopt);
        return;
    }

    WebAuthnAuthenticationChallenge result;
    result.challenge = generateChallenge(*crypto_);
    result.rpId = rpId_;
    callback(result);
}

void WebAuthnService::finishAuthentication(
  const std::string &credentialId,
  std::function<void(std::optional<WebAuthnAuthResult>)> &&callback
)
{
    if (!repo_ || credentialId.empty())
    {
        callback(std::nullopt);
        return;
    }

    auto repo = repo_;

    repo_->findByCredentialId(
      credentialId,
      [repo,
       credentialId,
       callback = std::move(callback)](std::optional<WebAuthnCredentialLookup> found) {
          if (!found)
          {
              callback(std::nullopt);
              return;
          }

          int newSignCount = found->signCount + 1;

          // Best-effort bookkeeping update -- mirrors the controller's
          // own fire-and-forget UPDATE (its success/failure callbacks are
          // both no-ops), so a sign_count persistence failure does not
          // block reporting a successful authentication back to the
          // caller.
          repo->updateSignCount(credentialId, newSignCount, [](bool) {});

          WebAuthnAuthResult result;
          result.userId = found->userId;
          result.publicSub = found->publicSub;
          result.signCount = newSignCount;
          callback(result);
      }
    );
}

void WebAuthnService::listCredentials(
  int64_t userId,
  std::function<void(std::vector<WebAuthnCredentialSummary>)> &&callback
)
{
    if (!repo_)
    {
        callback({});
        return;
    }
    repo_->listCredentials(userId, std::move(callback));
}

}  // namespace authforge::identity
