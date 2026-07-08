#pragma once

// Task 14 (authforge-sdk-refactor, design.md §5.6): Adapter-side default
// implementation of authforge::common::ports::IUuidGenerator, backed by
// OpenSSL's RAND_bytes (design.md port table: "OpenSSL/标准库实现"). See
// OpenSslCryptoProvider.h's header comment for the placement rationale
// (kept under OAuth2Plugin/include/oauth2/adapters/ for now; no Drogon
// dependency).
//
// UUID v4 generation directly via RAND_bytes + RFC 4122 §4.4 version/
// variant bit fixups, rather than delegating to
// oauth2::adapters::OpenSslCryptoProvider::secureRandomBytes -- kept
// self-contained (no cross-adapter-class dependency) since UUID formatting
// is a one-off concern, not a general crypto primitive.

#include <authforge/common/ports/IUuidGenerator.h>

namespace oauth2::adapters
{

class OpenSslUuidGenerator : public authforge::common::ports::IUuidGenerator
{
  public:
    std::string generate() override;
};

}  // namespace oauth2::adapters
