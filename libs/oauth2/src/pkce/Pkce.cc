#include <authforge/oauth2/pkce/Pkce.h>

#include <cctype>

namespace authforge::oauth2::pkce
{

using authforge::common::model::PkceChallenge;
using authforge::common::ports::ICryptoProvider;

std::string computeCodeChallenge(
  const std::string &codeVerifier,
  const std::string &method,
  ICryptoProvider &crypto
)
{
    if (method == "S256")
    {
        auto digest = crypto.sha256(codeVerifier);
        return crypto.base64UrlEncode(digest.data(), digest.size());
    }
    // "plain" (or any other value, per RFC 7636 falling back to identity --
    // matching the pre-existing CryptoUtils.h::computeCodeChallenge's
    // else-branch behavior).
    return codeVerifier;
}

bool verifyCodeVerifier(
  const std::string &codeVerifier,
  const PkceChallenge &challenge,
  ICryptoProvider &crypto
)
{
    const std::string recomputed = computeCodeChallenge(codeVerifier, challenge.method(), crypto);
    return recomputed == challenge.challenge();
}

namespace
{
bool isValidPkceCharsetAndLength(const std::string &value)
{
    if (value.length() < 43 || value.length() > 128)
    {
        return false;
    }
    for (char c : value)
    {
        if (
          !std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '.' && c != '_' &&
          c != '~')
        {
            return false;
        }
    }
    return true;
}
}  // namespace

bool isValidCodeVerifierFormat(const std::string &codeVerifier)
{
    return isValidPkceCharsetAndLength(codeVerifier);
}

bool isValidCodeChallengeFormat(const std::string &codeChallenge)
{
    return isValidPkceCharsetAndLength(codeChallenge);
}

}  // namespace authforge::oauth2::pkce
