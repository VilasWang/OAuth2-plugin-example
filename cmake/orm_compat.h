#pragma once

// C++17/20 compatibility for std::codecvt_utf8_utf16
// This header provides compatibility when std::codecvt_utf8_utf16 is not
// available

#include <locale>
#include <codecvt>
#include <algorithm>

// For C++20 environments, we provide a fallback implementation
// (On macOS/C++17, <codecvt> still provides these types as deprecated -
//  the force-include in CMakeLists.txt ensures <codecvt> is included)
#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
// C++20: codecvt_utf8_utf16 is removed or deprecated
// Provide a minimal compatible implementation for Drogon's ORM usage

#include <system_error>
#include <vector>
#include <string>
#include <cwchar>

namespace std
{
// Minimal codecvt_utf8_utf16 implementation for Drogon ORM compatibility
template <
  typename Elem,
  unsigned long Maxcode = 0x10ffff,
  std::codecvt_mode Mode = (std::codecvt_mode)0>
class codecvt_utf8_utf16 : public std::codecvt<Elem, char, std::mbstate_t>
{
  public:
    explicit codecvt_utf8_utf16(size_t refs = 0) : std::codecvt<Elem, char, std::mbstate_t>(refs)
    {
    }

  protected:
    // The critical method: convert UTF-8 to UTF-16 for string length validation
    typename std::codecvt<Elem, char, std::mbstate_t>::result do_in(
      std::mbstate_t &state,
      const char *from,
      const char *from_end,
      const char *&from_next,
      Elem *to,
      Elem *to_end,
      Elem *&to_next
    ) const override
    {
        // Simplified UTF-8 to UTF-16 conversion for Drogon's string validation
        while (from < from_end && to < to_end)
        {
            unsigned char c = *from;
            wchar_t wc;

            if (c < 0x80)
            {
                // ASCII character (1 byte)
                wc = c;
                from++;
            }
            else if ((c & 0xE0) == 0xC0 && from + 1 < from_end)
            {
                // 2-byte UTF-8 sequence
                unsigned char c2 = *(from + 1);
                if ((c2 & 0xC0) != 0x80)
                    break;  // Invalid UTF-8
                wc = ((c & 0x1F) << 6) | (c2 & 0x3F);
                from += 2;
            }
            else if ((c & 0xF0) == 0xE0 && from + 2 < from_end)
            {
                // 3-byte UTF-8 sequence
                unsigned char c2 = *(from + 1);
                unsigned char c3 = *(from + 2);
                if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80)
                    break;  // Invalid UTF-8
                wc = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                from += 3;
            }
            else if ((c & 0xF8) == 0xF0 && from + 3 < from_end)
            {
                // 4-byte UTF-8 sequence (emoji, symbols beyond U+FFFF)
                // These require surrogate pairs in UTF-16, which this
                // simple codecvt can't handle properly. Replace with
                // Unicode replacement character to avoid crashes.
                unsigned char c2 = *(from + 1);
                unsigned char c3 = *(from + 2);
                unsigned char c4 = *(from + 3);
                if ((c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80 || (c4 & 0xC0) != 0x80)
                    break;  // Invalid UTF-8
                // Use replacement character U+FFFD for 4-byte
                // sequences (TODO: full surrogate pair support)
                wc = 0xFFFD;
                from += 4;
            }
            else
            {
                break;  // Invalid or unsupported UTF-8
            }

            *to++ = wc;
        }

        from_next = from;
        to_next = to;
        return std::codecvt<Elem, char, std::mbstate_t>::ok;
    }

    // Reverse conversion (not used by Drogon but required for interface)
    typename std::codecvt<Elem, char, std::mbstate_t>::result do_out(
      std::mbstate_t &state,
      const Elem *from,
      const Elem *from_end,
      const Elem *&from_next,
      char *to,
      char *to_end,
      char *&to_next
    ) const override
    {
        // UTF-16 to UTF-8 conversion
        while (from < from_end && to < to_end - 3)
        {
            wchar_t wc = *from;
            if (wc < 0x80)
            {
                *to++ = static_cast<char>(wc);
            }
            else if (wc < 0x800)
            {
                *to++ = static_cast<char>(0xC0 | (wc >> 6));
                *to++ = static_cast<char>(0x80 | (wc & 0x3F));
            }
            else
            {
                *to++ = static_cast<char>(0xE0 | (wc >> 12));
                *to++ = static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
                *to++ = static_cast<char>(0x80 | (wc & 0x3F));
            }
            from++;
        }
        from_next = from;
        to_next = to;
        return std::codecvt<Elem, char, std::mbstate_t>::ok;
    }

    // Required codecvt interface methods
    typename std::codecvt<Elem, char, std::mbstate_t>::result do_unshift(
      std::mbstate_t &state,
      char *to,
      char *to_end,
      char *&to_next
    ) const override
    {
        to_next = to;
        return std::codecvt<Elem, char, std::mbstate_t>::noconv;
    }

    int do_encoding() const noexcept override
    {
        return 0;  // Variable-length encoding
    }

    bool do_always_noconv() const noexcept override
    {
        return false;
    }

    int do_length(
      std::mbstate_t &state,
      const char *from,
      const char *from_end,
      size_t max
    ) const override
    {
        return static_cast<int>(std::min(static_cast<size_t>(from_end - from), max));
    }

    int do_max_length() const noexcept override
    {
        return 4;  // Maximum UTF-8 to UTF-16 expansion
    }
};
}  // namespace std
#endif
