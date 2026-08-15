-- s5-refresh-token.lua (ory hydra)
-- S5: refresh token rotation — POST token endpoint (public :4444), grant_type
-- =refresh_token, Basic auth of the confidential client bench-web.
-- Endpoint (official): POST /oauth2/token
--   https://www.ory.sh/docs/hydra/reference/api
--
-- Hydra rotates refresh tokens: each RT consumed EXACTLY ONCE. Threads
-- advance linearly, return nil at pool tail; run-scenario.sh --reissue
-- re-mints per level (D5). RT pool minted through the official headless
-- accept flow (mint_tokens.py; RFC 6749 §4.4.3 bars cc-grant refresh tokens).

local LIB_DIR = os.getenv("WRK_LIB_DIR") or error("WRK_LIB_DIR not set")
local TOKEN_FILE = LIB_DIR .. "/generated/refresh_tokens.txt"

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
    local f = io.open(LIB_DIR .. "/generated/basic_header_web.txt", "r")
    local basic = f:read("*l")
    f:close()
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
    if tok_idx > tok_end then
        return nil
    end
    local token = tokens[tok_idx]
    tok_idx = tok_idx + 1

    wrk.method = "POST"
    wrk.path = "/oauth2/token"
    wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
    wrk.body = "grant_type=refresh_token&refresh_token=" .. token
    return wrk.format()
end
