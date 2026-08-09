-- s2-client-credentials.lua
-- S2: client_credentials — the simplest token-issuance path.
--
--   POST /oauth2/token
--   Content-Type: application/x-www-form-urlencoded
--   body: grant_type=client_credentials&client_id=backend-svc
--         &client_secret=test-secret&scope=read
--
-- Single-step, no user/session/refresh state. Measures client authentication
-- (SHA256 secret verify against the seeded backend-svc) + RS256 access_token
-- signing. This is the closest single-request measure of "token issuance
-- throughput".
--
-- Constraints verified against the codebase:
--   * token endpoint reads form params via getParameter(), NOT JSON
--     (TokenEndpointController.cc:896-917). A JSON body silently yields empty
--     params, so the form-encoded Content-Type below is mandatory.
--   * seed backend-svc (CONFIDENTIAL) + scope grant 'read' comes from
--     dev_backend_client.sql (initdb auto-run).
--
-- Usage: wrk -t<T> -c<C> -d30s --latency -s s2-client-credentials.lua <URL>

wrk.method = "POST"
wrk.path = "/oauth2/token"
wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
wrk.body = "grant_type=client_credentials&client_id=backend-svc"
  .. "&client_secret=test-secret&scope=read"

request = function()
  return wrk.format()
end
