#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace authforge::common::utils
{
/**
 * @brief TOTP (Time-based One-Time Password) utility (RFC 6238)
 * Uses HMAC-SHA1 with 30-second time steps and 6-digit codes.
 */
class TotpUtils
{
  public:
    /**
     * @brief Generate a random TOTP secret (base32 encoded, 20 bytes)
     * @return Base32 encoded secret string (32 characters)
     */
    static std::string generateSecret();

    /**
     * @brief Generate TOTP code for current time
     * @param secret Base32 encoded secret
     * @return 6-digit TOTP code string
     */
    static std::string generateCode(const std::string &secret);

    /**
     * @brief Verify a TOTP code (allows +/- 1 time step for clock skew)
     * @param secret Base32 encoded secret
     * @param code 6-digit code to verify
     * @return true if code is valid
     */
    static bool verifyCode(const std::string &secret, const std::string &code);

    /**
     * @brief Generate otpauth:// URI for QR code scanning
     * @param secret Base32 encoded secret
     * @param accountName User identifier (email or username)
     * @param issuer Service name
     * @return otpauth:// URI string
     */
    static std::string generateOtpAuthUri(
      const std::string &secret,
      const std::string &accountName,
      const std::string &issuer = "OAuth2Server"
    );

    /**
     * @brief Generate backup codes (one-time use)
     * @param count Number of codes to generate (default 10)
     * @return Vector of 8-character alphanumeric codes
     */
    static std::vector<std::string> generateBackupCodes(int count = 10);

  private:
    static std::vector<uint8_t> base32Decode(const std::string &encoded);
    static std::string base32Encode(const uint8_t *data, size_t len);
    static uint32_t generateOtp(const std::vector<uint8_t> &key, uint64_t counter);
};

}  // namespace authforge::common::utils
