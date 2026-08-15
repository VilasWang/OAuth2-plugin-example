-- s2-client-credentials.lua (ory hydra)
-- S2: machine-to-machine token — POST token endpoint with HTTP Basic client
-- auth (Hydra OAuth2 client, token_endpoint_auth_method=client_secret_basic).
-- Endpoint (official): POST /oauth2/token (public :4444)
--   https://www.ory.sh/docs/hydra/reference/api
-- Basic header precomputed by setup.sh (runtime-generated bench secret).

local LIB_DIR = os.getenv("WRK_LIB_DIR") or error("WRK_LIB_DIR not set")
local f = io.open(LIB_DIR .. "/generated/basic_header_svc.txt", "r")
local BASIC = f:read("*l")
f:close()

wrk.method = "POST"
wrk.path = "/oauth2/token"
wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
wrk.headers["Authorization"] = BASIC
wrk.body = "grant_type=client_credentials"

request = function()
    return wrk.format()
end
