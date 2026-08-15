-- s6-userinfo.lua (zitadel)
-- S6: userinfo — GET with a REUSABLE Bearer pool of service-user access
-- tokens (scope openid) minted by setup.sh. NOTE: Zitadel userinfo is
-- designed for USER tokens; service-user tokens are accepted at runtime
-- discretion — the setup smoke-gates this and run-all skips S6 with an
-- honest N/A annotation if they are rejected (design M2 decision gates).
-- Endpoint (official): GET /oidc/v1/userinfo
--   https://zitadel.com/docs/apis/openidoauth/endpoints

local LIB_DIR = os.getenv("WRK_LIB_DIR") or error("WRK_LIB_DIR not set")
local TOKEN_FILE = LIB_DIR .. "/generated/cc_tokens.txt"

local tokens, tok_start, tok_end, tok_idx
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

    wrk.method = "GET"
    wrk.path = "/oidc/v1/userinfo"
    wrk.headers["Authorization"] = "Bearer " .. token
    return wrk.format()
end
