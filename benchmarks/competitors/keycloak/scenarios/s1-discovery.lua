-- s1-discovery.lua (keycloak)
-- S1: OIDC discovery document — GET, no auth.
-- Endpoint (official): GET /realms/{realm}/.well-known/openid-configuration
--   https://www.keycloak.org/docs/latest/securing_apps/#endpoints
-- Realm is fixed as the constant `bench` (created by setup.sh; design §4.1).

wrk.method = "GET"
wrk.path = "/realms/bench/.well-known/openid-configuration"

request = function()
    return wrk.format()
end
