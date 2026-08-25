# examples/third-party-host

Minimal standalone consumer that proves the **fulla SDK is independently
consumable** (fulla-sdk-refactor design.md §1.1: "第三方 Drogon 应用仅
`find_package(fulla-oauth2)` + 实现端口即可跑通授权码流").

## What this proves

A third party can take the SDK packages — with **no** access to the product's
`OAuth2Plugin`, `libs/drogon`, or `libs/storage-postgres` — and:

1. Assemble the Domain-layer protocol engine (`AuthorizationService` +
   `TokenService`) from only the SDK packages.
2. Back it with in-memory repositories + the testing crypto provider (no DB,
   no Redis, no Drogon HTTP layer).
3. Run the authorization-code flow's **core steps** programmatically:
   `evaluateScopes` → `generateAuthorizationCode` → `exchangeCodeForToken`.

This is the build-tree smoke gate for SDK consumability. A heavier HTTP
end-to-end variant (real `/authorize` `/token` endpoints in a Drogon host) is
deferred.

## Packages it links (and only these)

- `fulla::oauth2` — the protocol engine (`AuthorizationService`,
  `TokenService`, repository interfaces).
- `fulla::common` — shared ports / value objects.
- `fulla::common::testing` — `FakeCryptoProvider` (real OpenSSL hashing/
  HMAC/base64url; only `secureRandomBytes` determinized → tokens are
  real-crypto correct, just reproducible).
- `fulla::storage::memory` — in-memory repository implementations.

It deliberately does **not** link `OAuth2Plugin`, `fulla::drogon`, or
`fulla::storage::postgres`. If it ever needs to, that is a regression in
SDK independence and must be investigated.

## Build and run

```bash
cmake --preset windows-msvc
cmake --build --preset windows-msvc --config Debug --target third-party-host-smoke
./build/windows-msvc/examples/third-party-host/Debug/third-party-host-smoke
echo $?   # 0 = smoke passed, 1 = a check failed (see stderr)
```

Gated by the `BUILD_EXAMPLES` CMake option (default `ON`).
