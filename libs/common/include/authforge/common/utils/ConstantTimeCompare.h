#pragma once

#include <cstddef>

namespace authforge::common::utils
{

/**
 * @brief Constant-time memory comparison
 *
 * Compares the first n bytes of s1 and s2 without early-exit on the first
 * differing byte, so the comparison time does not depend on where (or
 * whether) the buffers differ. Required for credential/hash comparisons to
 * defeat timing side channels (OAuth 2.0 Security BCP 2.1 §4.9).
 *
 * Shared by all storage backends (Postgres/Memory/Redis client-secret
 * validation) -- previously each backend carried its own verbatim copy.
 *
 * @return 0 if equal, non-zero otherwise (same contract as memcmp, but the
 * timing is data-independent)
 */
inline int constantTimeMemcmp(const void *s1, const void *s2, std::size_t n)
{
    const auto *p1 = static_cast<const unsigned char *>(s1);
    const auto *p2 = static_cast<const unsigned char *>(s2);
    unsigned char acc = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        acc |= static_cast<unsigned char>(p1[i] ^ p2[i]);
    }
    return static_cast<int>(acc);
}

}  // namespace authforge::common::utils
