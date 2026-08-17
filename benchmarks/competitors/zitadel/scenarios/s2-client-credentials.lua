-- s2-client-credentials.lua (zitadel)
-- S2: machine-to-machine token — POST token endpoint with a PRE-SIGNED
-- assertion under the RFC 7523 jwt-bearer GRANT (Zitadel's official M2M
-- path for Service Users: https://zitadel.com/docs/guides/integrate/service-accounts/private-key-jwt).
-- The token endpoint does NOT accept client_credentials with a JWT
-- assertion (v2.71 routes that grant to the Basic-secret machine path),
-- hence the grant-type annotation in COMPARISON.md. wrk Lua cannot sign,
-- so setup.sh mints the assertion (exp covers the whole benchmark session;
-- reuse within exp is standard JWT behavior — annotated in COMPARISON.md).
-- Endpoint (official): POST /oauth/v2/token
--   https://zitadel.com/docs/apis/openidoauth/endpoints

local LIB_DIR = os.getenv("WRK_LIB_DIR") or error("WRK_LIB_DIR not set")
local f = io.open(LIB_DIR .. "/generated/assertion.txt", "r")
local ASSERTION = f:read("*l")
f:close()

wrk.method = "POST"
wrk.path = "/oauth/v2/token"
wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
wrk.body = "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer"
    .. "&scope=openid+profile"
    .. "&assertion=" .. ASSERTION

request = function()
    return wrk.format()
end
