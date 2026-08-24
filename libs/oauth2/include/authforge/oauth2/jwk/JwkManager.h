#pragma once

// M2b Task 17 slice 10 (authforge-sdk-refactor, design.md §6's directory
// layout: "pkce/ jwk/ # PKCE、JWK 签名（经 ICryptoProvider 端口）"):
// relocates JwkManager from OAuth2Plugin/include/oauth2/utils/JwkManager.h
// into libs/oauth2 (Domain layer), now that slices 8-9 removed its two
// blockers (hardcoded DrogonLogger dependency; hardcoded
// authforge::drogon::adapters::OpenSslCryptoProvider dependency for base64url).
//
// Behavior/API is otherwise UNCHANGED from the pre-move class (same
// method signatures, same init-once-then-read-only concurrency contract,
// same OpenSSL RS256 signing implementation) -- this is a pure relocation
// plus a default-logger-fallback change (see the constructor doc below).
// OAuth2Plugin/include/oauth2/utils/JwkManager.h becomes a thin
// compatibility shim (`using JwkManager = authforge::oauth2::JwkManager`)
// so the many existing `oauth2::JwkManager`/`using oauth2::JwkManager`
// call sites across OAuth2Plugin/OAuth2Server do not need to change in
// this slice (design.md's namespace-sync-everywhere pass is explicitly a
// later task, M8 Task 40).

#include <authforge/common/ports/ILogger.h>

#include <string>
#include <json/json.h>
#include <memory>

namespace authforge::oauth2
{

/**
 * @brief Manages RSA signing keys for OpenID Connect id_token.
 *
 * Loads RSA private key from PEM file or environment variable.
 * Signs JWT tokens using RS256 algorithm.
 * Exposes public key in JWK format for /.well-known/jwks.json.
 *
 * ── Concurrency contract: init-once, then read-only ─────────────────────
 * This class follows an "initialize exactly once during startup, read-only
 * at runtime" contract:
 *   - init() MUST be called exactly once, before the server begins
 *     accepting requests. init() is the ONLY mutating method; a second
 *     call logs an error and is a no-op (does NOT re-allocate rsaKey_ or
 *     overwrite kid_), removing the run-time mutation entry point.
 *   - signJwt(), getJwks(), getKeyId(), isInitialized() are all const and
 *     only read the key state; safe to call concurrently from many
 *     threads with no per-call locking, PROVIDED init() has already
 *     completed-before (happens-before) the first concurrent read (the
 *     production wiring publishes this object as
 *     std::shared_ptr<const JwkManager> after init() to enforce this at
 *     the type level -- see OAuth2Plugin::initAndStart()).
 *
 * OpenSSL concurrency assumption: see the comment on signJwt() --
 * concurrent signing is safe only under OpenSSL >= 1.1.0 with threads
 * enabled.
 */
class JwkManager
{
  public:
    /**
     * @brief Construct, optionally injecting the ILogger this class logs
     * through. Defaults to nullptr, in which case log() is a NO-OP (this
     * Domain-layer class has no Drogon-backed fallback to fall back to,
     * unlike the pre-move class -- see this header's own top comment).
     * Production call sites that want JwkManager's diagnostic log lines
     * (key-load source, init-once-violation warnings, sign failures)
     * MUST pass a real ILogger implementation explicitly (e.g.
     * authforge::drogon::adapters::DrogonLogger, from the Adapter layer).
     *
     * Does NOT take ownership: the caller must keep `*logger` alive for
     * at least this object's lifetime.
     */
    explicit JwkManager(authforge::common::ports::ILogger *logger = nullptr) : logger_(logger)
    {
    }

    ~JwkManager();

    /**
     * @brief Initialize from configuration (call EXACTLY ONCE, at startup).
     *
     * Loads the RSA private key from an env var / file path, or generates
     * an ephemeral dev key as a fallback. This is the only mutating
     * method.
     *
     * @param config JSON config with "signing_key_path" / "kid" (or env
     * vars).
     * @return true if a key is loaded and the manager is initialized
     * (including the no-op case where it was already initialized); false
     * on first-time initialization failure.
     */
    bool init(const Json::Value &config);

    /**
     * @brief Sign a JWT payload with RS256.
     * @param payload JSON payload (claims).
     * @return Signed JWT string (header.payload.signature), or "" on
     * failure (not initialized, or an OpenSSL signing error).
     */
    std::string signJwt(const Json::Value &payload) const;

    /// Outcome of verifyJwt(): one value per distinct rejection reason so
    /// callers can surface precise diagnostics (logged as Internal_Detail
    /// only; the client always gets the same generic error envelope).
    enum class JwtVerificationResult
    {
        Ok,
        NotInitialized,  ///< no key loaded; fail closed (cannot verify)
        Malformed,       ///< not 3 non-empty segments / bad base64url / bad JSON / missing exp
        BadAlg,          ///< header alg absent or != RS256 (strict: no "none"/HS256 confusion)
        KidMismatch,     ///< header kid present but != the current key id
        BadSignature,    ///< RS256 signature check failed
        IssuerMismatch,  ///< payload iss != expectedIssuer
        Expired,         ///< payload exp <= nowSecs
        MissingSubject   ///< payload sub absent or empty
    };

    /**
     * @brief Verify one of OUR RS256 JWTs end-to-end: signature + claim policy.
     *
     * The symmetric counterpart of signJwt() (#78: /oauth2/end_session must
     * not trust an id_token_hint's claims before the signature verifies).
     * Checks, in order: structure; header alg strictly RS256; header kid (if
     * present) equals the current kid; RS256 signature over header.payload;
     * payload iss == expectedIssuer; payload exp > nowSecs; payload sub
     * non-empty. Absent kid is tolerated (tokens signed before a kid was
     * configured still verify); absent exp is Malformed.
     *
     * Concurrency contract: same as signJwt() -- const, read-only after
     * init(), safe to call concurrently.
     *
     * @param jwt            The compact JWT (header.payload.signature).
     * @param expectedIssuer The OP issuer the iss claim must match.
     * @param nowSecs        Current unix time (seconds) for exp checking.
     * @return Ok, or the first failing reason. Never throws.
     */
    JwtVerificationResult verifyJwt(
      const std::string &jwt,
      const std::string &expectedIssuer,
      long long nowSecs
    ) const;

    /**
     * @brief Get the JWKS (JSON Web Key Set) containing public key(s).
     * @return JSON object with "keys" array.
     */
    Json::Value getJwks() const;

    /// Get the current key ID.
    const std::string &getKeyId() const
    {
        return kid_;
    }

    /// Check if manager is initialized with a valid key.
    bool isInitialized() const
    {
        return initialized_;
    }

  private:
    void *rsaKey_ = nullptr;  // EVP_PKEY* (opaque to avoid OpenSSL header in public API)
    std::string kid_;
    bool initialized_ = false;
    authforge::common::ports::ILogger *logger_ = nullptr;  // non-owning; may be nullptr

    bool generateEphemeralKey();
    bool loadFromPem(const std::string &pemData);

    static std::string base64UrlEncode(const unsigned char *data, size_t len);
    static std::string base64UrlEncode(const std::string &data);
    /// Strict base64url decode (no padding, '-'/'_' alphabet). Returns false
    /// on any non-alphabet character or impossible length; output is cleared
    /// on failure. Domain-layer constraint: hand-rolled, no drogon/3rd-party.
    static bool base64UrlDecode(const std::string &input, std::string &output);

    bool getPublicKeyComponents(std::string &n, std::string &e) const;

    /// Log through the injected logger_ if set; no-op if nullptr.
    void log(authforge::common::ports::LogLevel level, const std::string &message) const;
};

}  // namespace authforge::oauth2
