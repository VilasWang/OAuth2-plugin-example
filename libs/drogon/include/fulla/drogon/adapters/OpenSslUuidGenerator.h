#pragma once

// Task 14 (fulla-sdk-refactor, design.md §5.6): Adapter-side default
// implementation of fulla::common::ports::IUuidGenerator, backed by
// OpenSSL's RAND_bytes (design.md port table: "OpenSSL/标准库实现"). See
// OpenSslCryptoProvider.h's header comment for the placement rationale
// (kept under OAuth2Plugin/include/oauth2/adapters/ for now; no Drogon
// dependency).
//
// UUID v4 generation directly via RAND_bytes + RFC 4122 §4.4 version/
// variant bit fixups, rather than delegating to
// fulla::drogon::adapters::OpenSslCryptoProvider::secureRandomBytes -- kept
// self-contained (no cross-adapter-class dependency) since UUID formatting
// is a one-off concern, not a general crypto primitive.

#include <fulla/common/ports/IUuidGenerator.h>

namespace fulla::drogon::adapters
{

class OpenSslUuidGenerator : public fulla::common::ports::IUuidGenerator
{
  public:
    std::string generate() override;
};

}  // namespace fulla::drogon::adapters
