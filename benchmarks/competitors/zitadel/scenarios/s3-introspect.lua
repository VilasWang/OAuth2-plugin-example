-- s3-introspect.lua (zitadel)
-- S3: token introspection — POST with private-key-JWT client auth (OIDC app
-- + app key created via the Management API by setup.sh; official perf
-- guidance github.com/zitadel/zitadel/discussions/6220: secret auth hashes
-- per request, private key does not) + a REUSABLE pool of service-user
-- access tokens minted with that app's project audience scope (design D5 —
-- the aud scope is required for service-account tokens to introspect).
-- Endpoint (official): POST /oauth/v2/introspect
--   https://zitadel.com/docs/apis/openidoauth/endpoints

local LIB_DIR = os.getenv("WRK_LIB_DIR") or error("WRK_LIB_DIR not set")
local TOKEN_FILE = LIB_DIR .. "/generated/cc_tokens.txt"

local tokens, tok_start, tok_end, tok_idx, client_id, assertion
local threads = {}

local function load_lines(filepath)
    local lines = {}
    local f = io.open(filepath, "r")
    if not f then error("cannot open " .. filepath) end
    for line in f:lines() do
        line = line:gsub("%s+", "")
        if line ~= "" then table.insert(lines, line) end
    end
    f:close()
    return lines
end

setup = function(thread)
    table.insert(threads, thread)
    thread:set("tid", #threads - 1)
end

init = function()
    if not tid then tid = 0 end
    client_id = io.open(LIB_DIR .. "/generated/introspect_client_id.txt", "r"):read("*l")
    assertion = io.open(LIB_DIR .. "/generated/introspect_assertion.txt", "r"):read("*l")

    tokens = load_lines(TOKEN_FILE)
    local n = #tokens
    local nthreads = tonumber(os.getenv("WRK_NTHREADS") or "1") or 1
    local per = math.floor(n / nthreads)
    local remainder = n % nthreads
    tok_start = tid * per + math.min(tid, remainder) + 1
    local extra = (tid < remainder) and 1 or 0
    tok_end = tok_start + per + extra - 1
    tok_idx = tok_start
end

request = function()
    if tok_idx > tok_end then tok_idx = tok_start end
    local token = tokens[tok_idx]
    tok_idx = tok_idx + 1

    wrk.method = "POST"
    wrk.path = "/oauth/v2/introspect"
    wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
    wrk.body = "token=" .. token
        .. "&client_id=" .. client_id
        .. "&client_assertion_type=urn%3Aietf%3Aparams%3Aoauth%3Aclient-assertion-type%3Ajwt-bearer"
        .. "&client_assertion=" .. assertion
    return wrk.format()
end
