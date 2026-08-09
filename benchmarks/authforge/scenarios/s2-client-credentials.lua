-- s2-client-credentials.lua
-- S2: client_credentials — the simplest token-issuance path.
--
--   POST /oauth2/token
--   Authorization: Basic base64("backend-svc:test-secret")
--   Content-Type: application/x-www-form-urlencoded
--   body: grant_type=client_credentials&scope=read
--
-- Single-step, no user/session/refresh state. Measures client authentication
-- (SHA256 secret verify against the seeded backend-svc) + RS256 access_token
-- signing. This is the closest single-request measure of "token issuance
-- throughput".
--
-- Constraints verified against the codebase (2026-08-09 live check):
--   * token endpoint reads form params via getParameter(), NOT JSON
--     (TokenEndpointController.cc:896-917). A JSON body silently yields empty
--     params, so the form-encoded Content-Type below is mandatory.
--   * the seeded backend-svc declares token_endpoint_auth_method=
--     client_secret_basic, so F-017 REQUIRES HTTP Basic auth — a secret in the
--     POST body is rejected with 401 invalid_client. The static Basic credential
--     below is base64("backend-svc:test-secret") = YmFja2VuZC1zdmM6dGVzdC1zZWNyZXQ=.
--   * the 'read' scope grant comes from dev_backend_client.sql.
--
-- Usage: wrk -t<T> -c<C> -d30s --latency -s s2-client-credentials.lua <URL>

wrk.method = "POST"
wrk.path = "/oauth2/token"
wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
wrk.headers["Authorization"] = "Basic YmFja2VuZC1zdmM6dGVzdC1zZWNyZXQ="
wrk.body = "grant_type=client_credentials&scope=read"

request = function()
  return wrk.format()
end
