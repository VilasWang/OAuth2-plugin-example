#pragma once

// M2b Task 17 slice 10 (authforge-sdk-refactor): JwkManager has been
// relocated to libs/oauth2 (authforge::oauth2::JwkManager, design.md §6:
// "pkce/ jwk/ # PKCE、JWK 签名"), now that slices 8-9 removed its two
// Drogon-adjacent dependencies (hardcoded DrogonLogger; hardcoded
// OpenSslCryptoProvider for base64url). This header is now a thin
// compatibility shim so the many existing `oauth2::JwkManager` /
// `using oauth2::JwkManager` call sites across OAuth2Plugin/OAuth2Server
// do not need to change in this slice -- the full namespace-sync pass
// (`oauth2::` -> `authforge::oauth2::` everywhere) is design.md's own
// explicitly later task (M8 Task 40), not this one.

#include <authforge/oauth2/jwk/JwkManager.h>

namespace oauth2
{
using JwkManager = authforge::oauth2::JwkManager;
}  // namespace oauth2
