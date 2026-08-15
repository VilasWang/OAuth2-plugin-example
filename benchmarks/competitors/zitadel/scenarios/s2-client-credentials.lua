-- s2-client-credentials.lua (zitadel)
-- S2: machine-to-machine token — POST token endpoint with a PRE-SIGNED
-- JWT-profile client assertion (Zitadel's official M2M path: Service User +
-- private_key_jwt; the token endpoint does not offer Basic-auth
-- client_credentials). wrk Lua cannot sign, so setup.sh mints the assertion
-- (exp covers the whole benchmark session; reuse within exp is the standard
-- JWT-profile behavior — annotated in COMPARISON.md).
-- Endpoint (official): POST /oauth/v2/token
--   https://zitadel.com/docs/apis/openidoauth/endpoints

local LIB_DIR = os.getenv("WRK_LIB_DIR") or error("WRK_LIB_DIR not set")
local f = io.open(LIB_DIR .. "/generated/assertion.txt", "r")
local ASSERTION = f:read("*l")
f:close()

wrk.method = "POST"
wrk.path = "/oauth/v2/token"
wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
wrk.body = "grant_type=client_credentials&scope=openid+profile"
    .. "&client_assertion_type=urn%3Aietf%3Aparams%3Aoauth%3Aclient-assertion-type%3Ajwt-bearer"
    .. "&client_assertion=" .. ASSERTION

request = function()
    return wrk.format()
end
