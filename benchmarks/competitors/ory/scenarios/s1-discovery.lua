-- s1-discovery.lua (ory hydra)
-- S1: OIDC discovery document — GET, no auth (public port 4444).
-- Endpoint (official): GET /.well-known/openid-configuration
--   https://www.ory.sh/docs/hydra/reference/api

wrk.method = "GET"
wrk.path = "/.well-known/openid-configuration"

request = function()
    return wrk.format()
end
