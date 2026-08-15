-- s1-discovery.lua (zitadel)
-- S1: OIDC discovery document — GET, no auth.
-- Endpoint (official): GET /.well-known/openid-configuration
--   https://zitadel.com/docs/apis/openidoauth/endpoints

wrk.method = "GET"
wrk.path = "/.well-known/openid-configuration"

request = function()
    return wrk.format()
end
