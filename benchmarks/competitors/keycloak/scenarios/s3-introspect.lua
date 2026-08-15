-- s3-introspect.lua (keycloak)
-- S3: token introspection — POST with Basic client auth + a REUSABLE pool of
-- service-account access tokens minted by setup.sh over the token endpoint
-- (design D5: competitor tokens are self-issued, never SQL-seeded).
-- Endpoint (official): POST /realms/{realm}/protocol/openid-connect/token/introspect
--   https://www.keycloak.org/docs/latest/securing_apps/#token-introspection
--
-- Pool: cc_tokens.txt (reusable — introspection does not consume the token).
-- Threads slice the pool and wrap around (unlike S5's one-shot semantics).

local LIB_DIR = os.getenv("WRK_LIB_DIR") or error("WRK_LIB_DIR not set")
local TOKEN_FILE = LIB_DIR .. "/generated/cc_tokens.txt"
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
    -- REUSABLE pool: wrap around the thread's slice.
    if tok_idx > tok_end then tok_idx = tok_start end
    local token = tokens[tok_idx]
    tok_idx = tok_idx + 1

    wrk.method = "POST"
    wrk.path = "/realms/bench/protocol/openid-connect/token/introspect"
    wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
    wrk.body = "token=" .. token
    return wrk.format()
end
