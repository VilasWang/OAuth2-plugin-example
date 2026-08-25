-- s1-discovery.lua
-- S1: discovery — round-robin between two stateless OAuth2 endpoints.
--
--   GET /.well-known/openid-configuration  (JSON doc + issuer)
--   GET /.well-known/jwks.json             (RSA public keys, JwkManager init-once)
--
-- Neither endpoint touches the DB or Redis (DiscoveryController.cc), so this
-- scenario measures the pure Drogon framework ceiling: HTTP parse + JSON
-- construction / JWK read + TLS-less write. It is the upper-bound baseline
-- against which S2–S6 (which add DB/Redis/signing cost) are compared.
--
-- Usage: wrk -t<T> -c<C> -d30s --latency -s s1-discovery.lua <URL>

local idx = 0
local paths = {
  "/.well-known/openid-configuration",
  "/.well-known/jwks.json",
}

request = function()
  -- round-robin across the two endpoints (1-based index)
  idx = idx + 1
  wrk.method = "GET"
  wrk.path = paths[(idx % #paths) + 1]
  wrk.body = nil
  wrk.headers["Content-Type"] = nil
  return wrk.format()
end
