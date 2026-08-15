-- s2-client-credentials.lua (keycloak)
-- S2: machine-to-machine token — POST token endpoint with HTTP Basic client
-- auth (Keycloak confidential client, client_authenticator_type=secret).
-- Endpoint (official): POST /realms/{realm}/protocol/openid-connect/token
--   https://www.keycloak.org/docs/latest/securing_apps/#endpoints
--
-- The Basic header is precomputed by setup.sh into lib/generated/basic_header.txt
-- (runtime-generated bench secret; wrk Lua has no base64).

local LIB_DIR = os.getenv("WRK_LIB_DIR") or error("WRK_LIB_DIR not set")
local BASIC = io.open(LIB_DIR .. "/generated/basic_header.txt", "r"):read("*l")

wrk.method = "POST"
wrk.path = "/realms/bench/protocol/openid-connect/token"
wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
wrk.headers["Authorization"] = BASIC
wrk.body = "grant_type=client_credentials"

request = function()
    return wrk.format()
end
