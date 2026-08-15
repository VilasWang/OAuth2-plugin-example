-- s5-refresh-token.lua (keycloak)
-- S5: refresh token rotation — POST token endpoint, grant_type=refresh_token.
-- Endpoint (official): POST /realms/{realm}/protocol/openid-connect/token
--   https://www.keycloak.org/docs/latest/securing_apps/#refresh-token
--
-- Keycloak rotates refresh tokens by default (Revoke Refresh Token=ON): each
-- RT is consumed EXACTLY ONCE. Threads advance linearly through their slice
-- and return nil when exhausted; run-scenario.sh --reissue re-mints the pool
-- per level (equivalent of AuthForge's SQL --reseed, but via the API — D5).
--
-- RT pool: refresh_tokens.txt, minted via ROPC (direct access grants) by
-- setup.sh / reissue-rt-pool.sh (RFC 6749 §4.4.3: client_credentials never
-- issues refresh tokens, so the pool must come from a user-context flow).

local LIB_DIR = os.getenv("WRK_LIB_DIR") or error("WRK_LIB_DIR not set")
local TOKEN_FILE = LIB_DIR .. "/generated/refresh_tokens.txt"
local BASIC_FILE = LIB_DIR .. "/generated/basic_header.txt"

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
    -- plain read: the header value contains a meaningful space ("Basic xxx")
    local basic = io.open(BASIC_FILE, "r"):read("*l")
    wrk.headers["Authorization"] = basic

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
    -- ONE-SHOT pool: each RT used exactly once (rotation). When exhausted,
    -- return nil (wrk closes that connection; expected at pool tail).
    if tok_idx > tok_end then
        return nil
    end
    local token = tokens[tok_idx]
    tok_idx = tok_idx + 1

    wrk.method = "POST"
    wrk.path = "/realms/bench/protocol/openid-connect/token"
    wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
    wrk.body = "grant_type=refresh_token&refresh_token=" .. token
    return wrk.format()
end
