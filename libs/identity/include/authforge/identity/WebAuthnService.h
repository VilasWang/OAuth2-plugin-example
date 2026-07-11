#pragma once

// M2.5 identity completion (authforge-sdk-refactor, design.md §5.1/§6):
// real (non-placeholder) implementation, replacing the previous
// `#ifdef WITH_WEBAUTHN ... TODO` placeholder. Ports the business logic
// out of libs/drogon/src/controllers/WebAuthnController.cc's
// registerBegin/registerFinish/authenticateBegin/authenticateFinish/
// listCredentials handlers into a framework-independent service,
// following AuthService.h/MfaService.h's established pattern:
// dependencies (repository + ports) are injected through the
// constructor, no Drogon/DB-client/HTTP types appear anywhere in this
// class, and the async std::function<void(...)> &&callback convention is
// used throughout even where an individual step happens to be
// synchronous (challenge generation), for consistency with
// AuthService/MfaService's own async surface.
//
// Shape differences from the controller, and why:
//   - No drogon::HttpRequestPtr/HttpResponsePtr anywhere; methods take
//     plain values in and invoke a callback with a plain result struct
//     or std::optional<...>, mirroring AuthService::validateUser's
//     "optional on failure, populated struct on success" convention.
//   - Session-based challenge storage (req->session()->insert(...)) is
//     Drogon-specific and out of scope for a Domain-layer class. Instead,
//     beginRegistration()/beginAuthentication() generate a challenge and
//     hand it back to the caller as a plain string field on the returned
//     struct -- the caller (future production wiring, e.g. a controller
//     in libs/drogon) is responsible for storing/retrieving it from
//     wherever is appropriate (a Drogon session, same as today, or
//     anything else), exactly mirroring how the controller currently
//     treats req->session() as an external concern this class does not
//     own.
//   - Internal user id vs public_sub: every method here is keyed by the
//     internal auto-increment user id (int64_t), not the public_sub
//     string the controller happens to read out of the "userId" request
//     attribute -- same convention as IWebAuthnRepository.h's header
//     comment and AuthService/MfaService's existing precedent. Resolving
//     public_sub -> internal id is the caller's job.
//   - RP (Relying Party) id/name are constructor parameters instead of
//     being read from drogon::app().getCustomConfig() on every call
//     (mirrors MfaService's issuerName_ constructor parameter for the
//     analogous otpauth:// issuer field).
//
// Explicitly NOT ported (matches an existing, pre-existing simplification
// in the controller, not something introduced or required to be fixed
// here): this class does not perform real WebAuthn/FIDO2 cryptographic
// attestation/assertion verification. The current production controller
// does not do this either -- it trusts the client-submitted
// credential_id/public_key directly at registerFinish, and at
// authenticateFinish it does not verify a signed assertion against the
// stored public_key/challenge at all, only that the credential_id exists.
// finishRegistration()/finishAuthentication() below preserve that exact
// (simplified) behavioral contract; a real FIDO2 verifier would need to
// use the stored public_key and the challenge returned by
// beginAuthentication() to verify a signature, which is future work
// tracked by tasks.md Task 31 (WebAuthn crypto/CBOR dependencies), not
// this task.
//
// Scope boundary (design.md §4.1 rule 2, identity <-> oauth2 互不依赖):
// see IWebAuthnRepository.h's header comment -- this class only handles
// the identity-owned credential state and does not drive "authenticate,
// then issue OAuth2 tokens" orchestration (that crosses the
// identity/oauth2 boundary and belongs to future product-level assembly,
// same as MfaService.h's identical rationale for its
// verifyLoginCode()/token-issuance split).

#include <authforge/common/ports/ICryptoProvider.h>
#include <authforge/identity/IWebAuthnRepository.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace authforge::identity
{

/**
 * @brief Result of WebAuthnService::beginRegistration -- the fields the
 * controller's registerBegin currently assembles into a
 * PublicKeyCredentialCreationOptions JSON payload (challenge/rp
 * id+name/timeout). The user.id/name/displayName + pubKeyCredParams +
 * authenticatorSelection fields the controller also emits are static
 * protocol boilerplate / view-construction concerns, not business logic,
 * so they are left to the caller rather than duplicated here.
 */
struct WebAuthnRegistrationChallenge
{
    std::string challenge;  // Base64url random challenge (32 bytes / 256 bits of entropy).
    std::string rpId;
    std::string rpName;
    int timeoutMs = 60000;
};

/**
 * @brief Result of WebAuthnService::beginAuthentication -- the fields the
 * controller's authenticateBegin assembles (challenge/rpId/timeout;
 * allowCredentials is left empty for the discoverable/resident-key flow,
 * same as the controller).
 */
struct WebAuthnAuthenticationChallenge
{
    std::string challenge;
    std::string rpId;
    int timeoutMs = 60000;
};

/**
 * @brief Result of a successful WebAuthnService::finishAuthentication --
 * mirrors the fields the controller's authenticateFinish returns
 * (user_id [public_sub] + the post-increment sign_count).
 */
struct WebAuthnAuthResult
{
    int64_t userId = 0;     // Internal user id (for the caller's own follow-up lookups).
    std::string publicSub;  // users.public_sub -- what the controller today calls "user_id".
    int signCount = 0;      // sign_count AFTER this authentication's increment.
};

/**
 * @brief WebAuthn / passkey business logic: challenge generation,
 * credential registration, authentication (sign_count bookkeeping), and
 * credential listing. Framework-independent -- persistence and crypto
 * are injected through the constructor.
 */
class WebAuthnService
{
  public:
    /**
     * @brief Construct the service with dependencies.
     * @param repo Persistence for credential records (required).
     * @param crypto Randomness/encoding port for challenge generation
     * (required).
     * @param rpId WebAuthn Relying Party id (defaults to "localhost",
     * matching the controller's own default).
     * @param rpName WebAuthn Relying Party display name (defaults to
     * "OAuth2 Server", matching the controller's own default).
     */
    WebAuthnService(
      std::shared_ptr<IWebAuthnRepository> repo,
      std::shared_ptr<authforge::common::ports::ICryptoProvider> crypto,
      std::string rpId = "localhost",
      std::string rpName = "OAuth2 Server"
    );

    /**
     * @brief Begin passkey registration: generate a fresh challenge.
     * @param callback Result with the challenge + RP info, or nullopt if
     * the crypto dependency is missing.
     */
    void beginRegistration(
      std::function<void(std::optional<WebAuthnRegistrationChallenge>)> &&callback
    );

    /**
     * @brief Finish passkey registration: validate and store a new
     * credential.
     * @param userId Internal user id the credential belongs to.
     * @param credentialId Client-submitted credential identifier
     * (required).
     * @param publicKey Client-submitted public key material (required).
     * @param name Human-readable label for the credential; defaults to
     * "Passkey" if empty (mirrors the controller's JSON-parsing default).
     * @param callback Invoked with an empty string on success, or a
     * structured Error_Code (registered in ErrorCatalog) on failure --
     * mirrors AuthService::registerUser's existing contract so callers
     * can forward the value verbatim to ErrorResponder. Possible codes:
     * VALIDATION_MISSING_REQUIRED_FIELD (credentialId/publicKey empty),
     * VALIDATION_CREDENTIAL_ALREADY_REGISTERED (duplicate credentialId),
     * DB_QUERY_ERROR (any other repository failure).
     */
    void finishRegistration(
      int64_t userId,
      const std::string &credentialId,
      const std::string &publicKey,
      const std::string &name,
      std::function<void(const std::string &errorCode)> &&callback
    );

    /**
     * @brief Begin passkey authentication: generate a fresh challenge.
     * @param callback Result with the challenge + RP id, or nullopt if
     * the crypto dependency is missing.
     */
    void beginAuthentication(
      std::function<void(std::optional<WebAuthnAuthenticationChallenge>)> &&callback
    );

    /**
     * @brief Finish passkey authentication: look up the credential and,
     * if found, increment its sign_count.
     * @param credentialId Client-submitted credential identifier.
     * @param callback Result with the owning user's info + new
     * sign_count on success; nullopt if credentialId is empty or does
     * not match any stored credential (mirrors
     * AUTH_INVALID_CREDENTIALS -- collapsed into a single failure signal,
     * same security-conscious convention as AuthService::validateUser).
     */
    void finishAuthentication(
      const std::string &credentialId,
      std::function<void(std::optional<WebAuthnAuthResult>)> &&callback
    );

    /**
     * @brief List all credentials belonging to a user (for the
     * credentials-management endpoint).
     * @param userId Internal user id.
     * @param callback The user's credentials, most recently created
     * first. Empty vector if the user has none (or the repository is
     * missing).
     */
    void listCredentials(
      int64_t userId,
      std::function<void(std::vector<WebAuthnCredentialSummary>)> &&callback
    );

  private:
    std::shared_ptr<IWebAuthnRepository> repo_;
    std::shared_ptr<authforge::common::ports::ICryptoProvider> crypto_;
    std::string rpId_;
    std::string rpName_;
};

}  // namespace authforge::identity
