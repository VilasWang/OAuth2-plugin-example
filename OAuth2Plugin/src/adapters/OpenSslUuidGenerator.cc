#include <oauth2/adapters/OpenSslUuidGenerator.h>

#include <openssl/rand.h>

#include <cstdio>

namespace authforge::drogon::adapters
{

std::string OpenSslUuidGenerator::generate()
{
    unsigned char bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1)
    {
        // RAND_bytes failing is an unrecoverable CSPRNG failure on this
        // platform; there is no safe fallback that preserves the
        // "cryptographically unique identifier" contract, so surface it
        // rather than silently returning a low-entropy/degenerate id.
        throw std::runtime_error("OpenSslUuidGenerator: RAND_bytes failed");
    }

    // RFC 4122 §4.4: set version (4) and variant (10xx) bits.
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

    char buf[37];
    std::snprintf(
      buf,
      sizeof(buf),
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      bytes[0],
      bytes[1],
      bytes[2],
      bytes[3],
      bytes[4],
      bytes[5],
      bytes[6],
      bytes[7],
      bytes[8],
      bytes[9],
      bytes[10],
      bytes[11],
      bytes[12],
      bytes[13],
      bytes[14],
      bytes[15]
    );

    return std::string(buf);
}

}  // namespace authforge::drogon::adapters
