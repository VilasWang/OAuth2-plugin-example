#include <fulla/identity/WebAuthnService.h>

#include "WebAuthnCrypto.h"

#include <chrono>
#include <vector>

namespace fulla::identity
{

namespace
{

// Generate a cryptographically secure random challenge, base64url encoded
// (no padding) -- same algorithm as
// libs/oauth2/src/protocol/TokenCrypto.cc's generateSecureToken() and the
// production controller's ::fulla::drogon::utils::generateSecureToken() call
// (both: secureRandomBytes(32) -> base64url), just expressed against the
// injected ICryptoProvider instead of a hardcoded Adapter-layer instance
// (see TokenCrypto.h's identical rationale).
std::string generateChallenge(fulla::common::ports::ICryptoProvider &crypto)
{
    constexpr size_t kChallengeBytes = 32;
    std::vector<unsigned char> buffer(kChallengeBytes);
    crypto.secureRandomBytes(buffer.data(), kChallengeBytes);
    return crypto.base64UrlEncode(buffer.data(), buffer.size());
}

int64_t nowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch()
    )
      .count();
}

// Base64url decode helper with the "raw bytes as std::string" shape the
// crypto layer consumes. Returns false on malformed input (the provider's
// decoder is strict).
bool b64Decode(
  fulla::common::ports::ICryptoProvider &crypto,
  const std::string &encoded,
  std::string &out
)
{
    try
    {
        auto bytes = crypto.base64UrlDecode(encoded);
        out.assign(bytes.begin(), bytes.end());
        return true;
    }
    catch (...)
    {
        return false;
    }
}

}  // namespace

WebAuthnService::WebAuthnService(
  std::shared_ptr<IWebAuthnRepository> repo,
  std::shared_ptr<fulla::common::ports::ICryptoProvider> crypto,
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
  int32_t userId,
  const std::string &credentialId,
  const std::string &publicKey,
  const std::string &name,
  std::function<void(const std::string &errorCode)> &&callback
)
{
    // #142: the legacy UNVERIFIED contract is retired — this method kept
    // its declaration (SDK baseline) but must never store client-asserted
    // key material again. Callers are migrated to
    // finishRegistrationVerified(); this fails closed with the
    // attestation error so any overlooked caller surfaces loudly.
    (void)userId;
    (void)credentialId;
    (void)publicKey;
    (void)name;
    callback("WEBAUTHN_INVALID_ATTESTATION");
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
    // #142: the legacy credential_id-only contract (knowing the id was
    // enough to authenticate) is retired. Declaration kept for the SDK
    // baseline; always fails closed.
    (void)credentialId;
    callback(std::nullopt);
}

void WebAuthnService::listCredentials(
  int32_t userId,
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

// ---------------------------------------------------------------------------
// #142: challenge stores + verified finish flows
// ---------------------------------------------------------------------------

std::optional<std::string> WebAuthnService::issueRegistrationChallenge(const std::string &subject)
{
    if (!crypto_ || subject.empty())
        return std::nullopt;
    std::string challenge = generateChallenge(*crypto_);
    {
        std::lock_guard<std::mutex> lock(challengeMu_);
        // Last-write-wins: a subject's concurrent begin overwrites the
        // previous challenge (multi-tab registration races fail safe --
        // the overwritten challenge no longer verifies).
        registrationChallenges_[subject] = ChallengeEntry{challenge, nowSeconds()};
    }
    return challenge;
}

bool WebAuthnService::consumeRegistrationChallenge(
  const std::string &subject,
  const std::string &presentedChallenge
)
{
    ChallengeEntry entry;
    {
        std::lock_guard<std::mutex> lock(challengeMu_);
        auto it = registrationChallenges_.find(subject);
        if (it != registrationChallenges_.end())
        {
            entry = it->second;
            registrationChallenges_.erase(it);  // UNCONDITIONAL consumption
        }
    }
    if (entry.challenge.empty())
        return false;
    if (nowSeconds() - entry.issuedAt > challengeTtlSeconds_)
        return false;
    return entry.challenge == presentedChallenge;
}

std::optional<WebAuthnService::IssuedSessionChallenge>
WebAuthnService::issueAuthenticationChallenge()
{
    if (!crypto_)
        return std::nullopt;
    IssuedSessionChallenge result;
    result.challenge = generateChallenge(*crypto_);
    result.sessionValue = result.challenge + "|" + std::to_string(nowSeconds());
    return result;
}

bool WebAuthnService::verifyAuthenticationChallenge(
  const std::string &sessionValue,
  const std::string &presentedChallenge
)
{
    const size_t bar = sessionValue.rfind('|');
    if (bar == std::string::npos)
        return false;
    const std::string stored = sessionValue.substr(0, bar);
    int64_t issuedAt = 0;
    try
    {
        issuedAt = std::stoll(sessionValue.substr(bar + 1));
    }
    catch (...)
    {
        return false;
    }
    if (nowSeconds() - issuedAt > challengeTtlSeconds_)
        return false;
    return stored == presentedChallenge;
}

void WebAuthnService::finishRegistrationVerified(
  int32_t userId,
  const std::string &subject,
  const std::string &presentedChallenge,
  const RegistrationInput &input,
  std::function<void(const std::string &errorCode)> &&callback
)
{
    if (!repo_ || !crypto_)
    {
        callback("INTERNAL_ERROR");
        return;
    }
    // Challenge gate FIRST, and its consumption is unconditional — the
    // attestation body is never even parsed without a live bound
    // challenge (no replay oracle).
    if (!consumeRegistrationChallenge(subject, presentedChallenge))
    {
        callback("WEBAUTHN_CHALLENGE_MISMATCH");
        return;
    }

    std::string attestationObject, clientDataJson, rawId;
    if (!b64Decode(*crypto_, input.attestationObject, attestationObject) ||
        !b64Decode(*crypto_, input.clientDataJSON, clientDataJson) ||
        !b64Decode(*crypto_, input.rawId, rawId) || rawId.empty() || input.id != input.rawId)
    {
        callback("WEBAUTHN_INVALID_ATTESTATION");
        return;
    }

    // --- clientDataJSON (L2 §7.1 steps 5-9) ---
    std::string parseError;
    auto clientData = webauthn::parseClientDataJSON(clientDataJson, &parseError);
    if (!clientData || clientData->type != "webauthn.create" ||
        clientData->challenge != presentedChallenge)
    {
        callback("WEBAUTHN_CHALLENGE_MISMATCH");
        return;
    }
    bool originAllowed = false;
    for (const auto &o : rpOrigins_)
    {
        if (o == clientData->origin)
        {
            originAllowed = true;
            break;
        }
    }
    if (!originAllowed)
    {
        callback("WEBAUTHN_CHALLENGE_MISMATCH");
        return;
    }

    // --- attestationObject (§7.1 steps 10-11: fmt="none" only) ---
    auto attestation = webauthn::parseAttestationObject(attestationObject, &parseError);
    if (!attestation || attestation->fmt != "none" || !attestation->attStmtEmptyMap)
    {
        callback("WEBAUTHN_INVALID_ATTESTATION");
        return;
    }

    // --- authData (§7.1 steps 12, 15-19) ---
    auto authData = webauthn::parseAuthData(attestation->authData, true, &parseError);
    if (!authData || !authData->up || !authData->at)
    {
        callback("WEBAUTHN_INVALID_ATTESTATION");
        return;
    }
    const std::vector<unsigned char> rpIdHashRaw = crypto_->sha256(rpId_);
    const std::string expectedRpIdHash(reinterpret_cast<const char *>(rpIdHashRaw.data()), rpIdHashRaw.size());
    if (authData->rpIdHash != expectedRpIdHash)
    {
        callback("WEBAUTHN_CHALLENGE_MISMATCH");
        return;
    }
    if (authData->credentialId != rawId)
    {
        callback("WEBAUTHN_INVALID_ATTESTATION");
        return;
    }

    // --- COSE key (§7.1 steps 16-18, ES256-only) ---
    if (!webauthn::parseCoseKeyEs256(authData->coseKey, &parseError))
    {
        callback("WEBAUTHN_INVALID_ATTESTATION");
        return;
    }

    // Store the canonical base64url of the RAW credential id and COSE
    // bytes (client-submitted encodings are not trusted verbatim).
    const std::string credentialIdB64 = crypto_->base64UrlEncode(authData->credentialId);
    const std::string publicKeyB64 = crypto_->base64UrlEncode(authData->coseKey);
    const std::string credName = input.name.empty() ? "Passkey" : input.name;

    repo_->storeCredential(
      userId,
      credentialIdB64,
      publicKeyB64,
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

void WebAuthnService::finishAuthenticationVerified(
  const std::string &presentedChallenge,
  const AssertionInput &input,
  std::function<void(std::optional<WebAuthnAuthResult>)> &&callback
)
{
    if (!repo_ || !crypto_)
    {
        callback(std::nullopt);
        return;
    }
    if (input.id.empty() || input.id != input.rawId)
    {
        callback(std::nullopt);
        return;
    }
    std::string rawIdBytes, authDataBytes, clientDataJson, signature;
    if (!b64Decode(*crypto_, input.rawId, rawIdBytes) ||
        !b64Decode(*crypto_, input.authenticatorData, authDataBytes) ||
        !b64Decode(*crypto_, input.clientDataJSON, clientDataJson) ||
        !b64Decode(*crypto_, input.signature, signature))
    {
        callback(std::nullopt);
        return;
    }

    // Generic rejector: every failure after this point answers the same
    // nullopt — the caller maps it to a generic AUTH_INVALID_CREDENTIALS
    // so the response leaks nothing about WHICH check failed.
    auto sharedCb =
      std::make_shared<std::function<void(std::optional<WebAuthnAuthResult>)>>(std::move(callback));

    auto clientData = webauthn::parseClientDataJSON(clientDataJson, nullptr);
    if (!clientData || clientData->type != "webauthn.get" ||
        clientData->challenge != presentedChallenge)
    {
        (*sharedCb)(std::nullopt);
        return;
    }
    bool originAllowed = false;
    for (const auto &o : rpOrigins_)
    {
        if (o == clientData->origin)
        {
            originAllowed = true;
            break;
        }
    }
    if (!originAllowed)
    {
        (*sharedCb)(std::nullopt);
        return;
    }

    // Canonical credential id form (the store keys on the registration-
    // time base64url of the RAW bytes).
    const std::string credentialIdB64 = crypto_->base64UrlEncode(rawIdBytes);

    // Value-capture everything the async repository callbacks touch —
    // this member function has returned by the time they run.
    auto repo = repo_;
    auto crypto = crypto_;
    const auto cloneNotifier = cloneDetectorNotifier_;
    const std::string rpId = rpId_;

    repo_->findByCredentialId(
      credentialIdB64,
      [repo, crypto, cloneNotifier, rpId, credentialIdB64, input, authDataBytes, clientDataJson, signature, sharedCb](
        std::optional<WebAuthnCredentialLookup> found
      ) {
          if (!found)
          {
              (*sharedCb)(std::nullopt);
              return;
          }

          // userHandle (L2 §6.1 step 6): when the authenticator echoes it,
          // it must name this credential's owner — registration set
          // user.id to the internal id, so the decoded handle must equal
          // its decimal string.
          if (!input.userHandle.empty())
          {
              std::string handle;
              try
              {
                  auto bytes = crypto->base64UrlDecode(input.userHandle);
                  handle.assign(bytes.begin(), bytes.end());
              }
              catch (...)
              {
                  (*sharedCb)(std::nullopt);
                  return;
              }
              if (handle != std::to_string(found->userId))
              {
                  (*sharedCb)(std::nullopt);
                  return;
              }
          }

          // authData (§6.1 steps 9-11): assertion form (no attested
          // credential data), UP=1 AND UV=1 (begin advertises
          // userVerification=required — no-UV authenticators are outside
          // the supported surface, documented).
          auto authData = webauthn::parseAuthData(authDataBytes, false, nullptr);
          if (!authData || !authData->up || !authData->uv)
          {
              (*sharedCb)(std::nullopt);
              return;
          }
          const std::vector<unsigned char> rpIdHashRaw = crypto->sha256(rpId);
          const std::string expectedRpIdHash(reinterpret_cast<const char *>(rpIdHashRaw.data()), rpIdHashRaw.size());
          if (authData->rpIdHash != expectedRpIdHash)
          {
              (*sharedCb)(std::nullopt);
              return;
          }

          // Signature (§6.1 steps 15-16): ES256 over
          // authData || SHA256(clientDataJSON) against the STORED COSE key
          // (the client-submitted public_key was never stored, #142).
          std::string storedCose;
          try
          {
              auto bytes = crypto->base64UrlDecode(found->publicKey);
              storedCose.assign(bytes.begin(), bytes.end());
          }
          catch (...)
          {
              (*sharedCb)(std::nullopt);
              return;
          }
          auto coseKey = webauthn::parseCoseKeyEs256(storedCose, nullptr);
          if (!coseKey || !webauthn::verifyEs256Signature(*coseKey, authDataBytes, clientDataJson, signature))
          {
              (*sharedCb)(std::nullopt);
              return;
          }

          // signCount (§6.1 step 17): stored>0 and new<=stored means a
          // cloned authenticator — reject and fire the observability
          // hook. Both zero is the no-counter legal case.
          const uint32_t stored = static_cast<uint32_t>(found->signCount);
          const uint32_t presented = authData->signCount;
          if (stored > 0 && presented <= stored)
          {
              if (cloneNotifier)
                  cloneNotifier(found->userId, credentialIdB64);
              (*sharedCb)(std::nullopt);
              return;
          }
          const uint32_t newSignCount = (presented == 0 && stored == 0) ? 0 : presented;

          // Best-effort bookkeeping (a persistence failure does not
          // invalidate the just-verified authentication).
          repo->updateSignCount(credentialIdB64, static_cast<int>(newSignCount), [](bool) {});

          WebAuthnAuthResult result;
          result.userId = found->userId;
          result.publicSub = found->publicSub;
          result.signCount = static_cast<int>(newSignCount);
          (*sharedCb)(result);
      }
    );
}

}  // namespace fulla::identity
