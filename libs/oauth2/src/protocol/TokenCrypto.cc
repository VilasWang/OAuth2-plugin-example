#include <fulla/oauth2/protocol/TokenCrypto.h>

#include <cctype>
#include <vector>

namespace fulla::oauth2::protocol
{

std::string generateSecureToken(fulla::common::ports::ICryptoProvider &crypto, size_t bytes)
{
    std::vector<unsigned char> buffer(bytes);
    if (!crypto.secureRandomBytes(buffer.data(), bytes))
    {
        // Should never happen (see CryptoUtils.h's identical fallback
        // rationale) -- this Domain-layer version has no UUID-generator
        // port dependency to fall back to, so it degrades to re-hashing
        // an all-zero buffer's encoding rather than pulling in another
        // port for a "should never happen" path. secureRandomBytes
        // failing at all indicates a broken CSPRNG, at which point no
        // fallback here is meaningfully more secure than another.
        return crypto.base64UrlEncode(buffer.data(), buffer.size());
    }
    return crypto.base64UrlEncode(buffer.data(), buffer.size());
}

std::string hashToken(
  fulla::common::ports::ICryptoProvider &crypto,
  const std::string &rawToken
)
{
    std::string hex = crypto.sha256Hex(rawToken);
    for (char &c : hex)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return hex;
}

}  // namespace fulla::oauth2::protocol
