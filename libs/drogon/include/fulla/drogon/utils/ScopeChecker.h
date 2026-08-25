#pragma once

// F-010 (RFC 6749 §3.3 / RFC 6750 §3.1): minimal path -> required-scope helpers.
//
// OAuth scopes are a space-separated token string (RFC 6749 §3.3). A token
// "has" a required scope iff that scope appears as a whole token in that
// string -- substring match would let `openidprofile` satisfy a check for
// `openid`/`profile`, so we tokenize on spaces.
//
// These helpers are deliberately framework-free (pure std) so the filters can
// call them without pulling Drogon into a wider surface.

#include <string>
#include <string_view>

namespace fulla::drogon::utils
{

// Returns true iff `requiredScope` is present as a space-delimited token in
// `tokenScopes`. Empty `tokenScopes` never satisfies a non-empty requirement;
// an empty `requiredScope` is satisfied vacuously (no requirement).
inline bool hasScope(std::string_view tokenScopes, std::string_view requiredScope)
{
    if (requiredScope.empty())
        return true;  // no requirement -> always satisfied

    std::string_view s = tokenScopes;
    while (!s.empty())
    {
        // Skip leading spaces.
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.remove_prefix(1);
        if (s.empty())
            break;

        // Find the end of the current token.
        auto pos = s.find(' ');
        std::string_view tok = (pos == std::string_view::npos) ? s : s.substr(0, pos);
        if (tok == requiredScope)
            return true;
        s.remove_prefix((pos == std::string_view::npos) ? s.size() : pos + 1);
    }
    return false;
}

// Returns true iff the token carries EVERY scope in the space-separated
// `requiredScopes` string (convenience for multi-scope requirements).
inline bool hasAllScopes(std::string_view tokenScopes, std::string_view requiredScopes)
{
    std::string_view s = requiredScopes;
    while (!s.empty())
    {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.remove_prefix(1);
        if (s.empty())
            break;
        auto pos = s.find(' ');
        std::string_view req = (pos == std::string_view::npos) ? s : s.substr(0, pos);
        if (!hasScope(tokenScopes, req))
            return false;
        s.remove_prefix((pos == std::string_view::npos) ? s.size() : pos + 1);
    }
    return true;
}

}  // namespace fulla::drogon::utils
