#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace authforge::common::utils
{

// Normalize an email address for storage and uniqueness comparison.
//
// Rules:
//   1. Trim leading/trailing whitespace, lowercase the whole address.
//   2. For Gmail addresses (@gmail.com / @googlemail.com) only:
//        - strip dots from the local part
//        - drop everything from '+' onward in the local part
//      (Gmail ignores both, so user.x+tag@gmail.com == userx@gmail.com.)
//   3. Other domains are left structurally unchanged (only lowercased) to
//      avoid false collisions — not every provider treats '.' / '+' as
//      insignificant.
//
// Returns the normalized address. If input has no '@', it is returned
// trimmed and lowercased unchanged (callers should have validated format
// beforehand).
inline std::string normalizeEmail(const std::string &input)
{
    // 1. Trim
    size_t start = 0;
    size_t end = input.size();
    while (start < end && std::isspace(static_cast<unsigned char>(input[start])))
        ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])))
        --end;

    std::string trimmed = input.substr(start, end - start);

    // 2. Lowercase
    std::transform(trimmed.begin(), trimmed.end(), trimmed.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    // 3. Split on '@'
    size_t at = trimmed.find('@');
    if (at == std::string::npos)
        return trimmed;  // malformed — caller should have validated

    std::string local = trimmed.substr(0, at);
    std::string domain = trimmed.substr(at + 1);

    // 4. Gmail-only alias folding
    if (domain == "gmail.com" || domain == "googlemail.com")
    {
        // Drop '+...' suffix first
        size_t plus = local.find('+');
        if (plus != std::string::npos)
            local = local.substr(0, plus);
        // Then remove dots
        local.erase(std::remove(local.begin(), local.end(), '.'), local.end());
    }

    return local + "@" + domain;
}

}  // namespace authforge::common::utils
