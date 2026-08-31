#include <fulla/identity/AuthService.h>
#include <fulla/identity/IUserRepository.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace fulla::identity
{

namespace
{

// PBKDF2-SHA256 parameters -- verbatim match of
// OAuth2Plugin/src/utils/PasswordHasher.cc's constants, so hashes produced
// here are byte-for-byte interchangeable with the existing production
// PasswordHasher (same DB, same stored-hash format:
// "$pbkdf2-sha256$<iterations>$<hex-salt>$<hex-hash>").
constexpr int kPbkdf2Iterations = 310000;
constexpr size_t kPbkdf2KeyLength = 32;
constexpr size_t kPbkdf2SaltLength = 16;

std::string bytesToHex(const unsigned char *data, size_t len)
{
    static const char hexChars[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        hex.push_back(hexChars[data[i] >> 4]);
        hex.push_back(hexChars[data[i] & 0x0F]);
    }
    return hex;
}

std::vector<unsigned char> hexToBytes(const std::string &hex)
{
    std::vector<unsigned char> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        char hi = hex[i];
        char lo = hex[i + 1];
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return 0;
        };
        bytes.push_back(static_cast<unsigned char>((nibble(hi) << 4) | nibble(lo)));
    }
    return bytes;
}

bool isLegacyHash(const std::string &storedHash)
{
    return storedHash.find("$pbkdf2-sha256$") != 0;
}

std::string hashPassword(
  const std::string &password,
  fulla::common::ports::ICryptoProvider &crypto
)
{
    unsigned char salt[kPbkdf2SaltLength];
    crypto.secureRandomBytes(salt, kPbkdf2SaltLength);

    auto derived = crypto.pbkdf2HmacSha256(
      password,
      std::string(reinterpret_cast<char *>(salt), kPbkdf2SaltLength),
      kPbkdf2Iterations,
      kPbkdf2KeyLength
    );
    if (derived.size() != kPbkdf2KeyLength)
    {
        throw std::runtime_error("PBKDF2 hashing failed");
    }

    return "$pbkdf2-sha256$" + std::to_string(kPbkdf2Iterations) + "$" +
           bytesToHex(salt, kPbkdf2SaltLength) + "$" + bytesToHex(derived.data(), derived.size());
}

bool verifyPassword(
  const std::string &password,
  const std::string &storedHash,
  const std::string &legacySalt,
  fulla::common::ports::ICryptoProvider &crypto
)
{
    if (!isLegacyHash(storedHash))
    {
        // Parse "$pbkdf2-sha256$<iterations>$<hexsalt>$<hexhash>"
        std::vector<std::string> parts;
        std::string token;
        std::istringstream stream(storedHash);
        while (std::getline(stream, token, '$'))
        {
            if (!token.empty())
                parts.push_back(token);
        }
        if (parts.size() != 4)
            return false;

        int iterations = 0;
        try
        {
            iterations = std::stoi(parts[1]);
        }
        catch (...)
        {
            return false;
        }
        auto saltBytes = hexToBytes(parts[2]);
        auto expectedHash = hexToBytes(parts[3]);

        auto derived = crypto.pbkdf2HmacSha256(
          password, std::string(saltBytes.begin(), saltBytes.end()), iterations, kPbkdf2KeyLength
        );

        if (derived.size() != expectedHash.size())
            return false;

        int diff = 0;
        for (size_t i = 0; i < derived.size(); ++i)
            diff |= derived[i] ^ expectedHash[i];
        return diff == 0;
    }

    // Legacy SHA-256(password + salt) hex-digest verification.
    std::string inputHash = crypto.sha256Hex(password + legacySalt);
    if (inputHash.length() != storedHash.length())
        return false;

    std::string inputLower = inputHash;
    std::string storedLower = storedHash;
    std::transform(inputLower.begin(), inputLower.end(), inputLower.begin(), ::tolower);
    std::transform(storedLower.begin(), storedLower.end(), storedLower.begin(), ::tolower);

    int diff = 0;
    for (size_t i = 0; i < inputLower.length(); ++i)
        diff |= inputLower[i] ^ storedLower[i];
    return diff == 0;
}

}  // namespace

AuthService::AuthService(
  std::shared_ptr<IUserRepository> userRepo,
  std::shared_ptr<fulla::common::ports::ICryptoProvider> crypto,
  std::shared_ptr<fulla::common::ports::IClock> clock
)
    : userRepo_(std::move(userRepo)), crypto_(std::move(crypto)), clock_(std::move(clock))
{
}

void AuthService::validateUser(
  const std::string &identifier,
  const std::string &password,
  std::function<void(std::optional<AuthResult>)> &&callback
)
{
    if (!userRepo_ || !crypto_ || !clock_)
    {
        callback(std::nullopt);
        return;
    }

    auto sharedCb =
      std::make_shared<std::function<void(std::optional<AuthResult>)>>(std::move(callback));
    auto crypto = crypto_;
    auto clock = clock_;
    auto userRepo = userRepo_;

    // Login-identifier routing: contains '@' -> email lookup, else username.
    // (Callers are expected to have normalized the email already, matching
    // OAuth2Server/AuthService.cc's contract -- this service does not own
    // email-normalization policy, which is deployment-specific.)
    bool isEmail = identifier.find('@') != std::string::npos;

    // Value-capture the policy flag: async callbacks must not capture
    // `this` (db-operations rule 4 -- do not rely on the instance's
    // process-lifetime binding).
    const bool allowLegacy = allowLegacyHash_;
    const auto legacyRejectionNotifier = legacyHashRejectionNotifier_;
    auto onFound = [allowLegacy, legacyRejectionNotifier, sharedCb, crypto, clock, userRepo, password](std::optional<UserData> found) {
        if (!found)
        {
            (*sharedCb)(std::nullopt);
            return;
        }
        UserData user = *found;

        int64_t now = clock->nowSeconds();
        if (user.lockedUntil > now)
        {
            (*sharedCb)(std::nullopt);
            return;
        }

        // #103 gate: when the migration window is closed (auth.
        // allow_legacy_hash=false, the assembly default), legacy-format
        // hashes are rejected outright -- no verify, no rehash. This is a
        // POLICY rejection, not a wrong password: it must not advance the
        // lockout counter (a username alone would otherwise let an
        // attacker lock any legacy user out, and a reopened window would
        // still be blocked by locked_until). The optional notifier is the
        // observability hook for the assembly layer (WARN + audit with
        // the internal id); the response stays the generic failure -- no
        // oracle about which rejection fired.
        if (!allowLegacy && isLegacyHash(user.passwordHash))
        {
            if (legacyRejectionNotifier)
                legacyRejectionNotifier(user.id);
            (*sharedCb)(std::nullopt);
            return;
        }

        bool valid = verifyPassword(password, user.passwordHash, user.salt, *crypto);
        if (!valid)
        {
            userRepo->incrementFailedLogins(user.id, [](bool) {});
            (*sharedCb)(std::nullopt);
            return;
        }

        if (user.failedLoginCount > 0)
        {
            userRepo->resetFailedLogins(user.id, [](bool) {});
        }

        if (isLegacyHash(user.passwordHash))
        {
            try
            {
                std::string newHash = hashPassword(password, *crypto);
                userRepo->updatePasswordHash(user.id, newHash, [](bool) {});
            }
            catch (const std::exception &)
            {
                // Rehash failure is non-fatal to the login itself -- the
                // legacy hash still verified above.
            }
        }

        AuthResult result;
        result.internalId = user.id;
        result.publicSub = user.publicSub;
        result.emailVerified = user.emailVerified;
        result.mfaEnabled = user.mfaEnabled;
        (*sharedCb)(result);
    };

    if (isEmail)
        userRepo_->findByEmail(identifier, std::move(onFound));
    else
        userRepo_->findByUsername(identifier, std::move(onFound));
}

void AuthService::registerUser(
  const std::string &username,
  const std::string &password,
  const std::string &email,
  std::function<void(const std::string &errorCode)> &&callback
)
{
    if (!userRepo_ || !crypto_)
    {
        callback("INTERNAL_ERROR");
        return;
    }

    std::string passwordHash;
    try
    {
        passwordHash = hashPassword(password, *crypto_);
    }
    catch (const std::exception &)
    {
        callback("INTERNAL_ERROR");
        return;
    }

    UserData newUser;
    newUser.username = username;
    newUser.passwordHash = passwordHash;
    newUser.salt = "";  // PBKDF2 embeds its own salt in the hash string
    newUser.email = email;

    userRepo_->create(
      newUser,
      [callback = std::move(callback)](std::optional<int32_t> newUserId, std::string errorCode) {
          // IUserRepository::create() is responsible for default-role
          // assignment (repository-owned concern -- mirrors
          // OAuth2Server/AuthService.cc's registerUser, which assigns the
          // "user" role as part of the same transaction/continuation
          // chain rather than as a separate caller-driven step). It is
          // also responsible for classifying constraint-violation
          // failures into structured Error_Codes (e.g.
          // VALIDATION_USERNAME_TAKEN/VALIDATION_EMAIL_TAKEN) -- forward
          // verbatim, falling back to INTERNAL_ERROR only if the
          // repository didn't classify the failure.
          if (newUserId)
          {
              callback("");
              return;
          }
          callback(errorCode.empty() ? "INTERNAL_ERROR" : errorCode);
      }
    );
}

void AuthService::getUserInfo(
  int32_t userId,
  std::function<void(std::optional<Json::Value>)> &&callback
)
{
    if (!userRepo_)
    {
        callback(std::nullopt);
        return;
    }
    userRepo_->getUserInfoWithRoles(userId, std::move(callback));
}

}  // namespace fulla::identity
