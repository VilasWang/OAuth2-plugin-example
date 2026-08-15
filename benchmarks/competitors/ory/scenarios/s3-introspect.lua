-- s3-introspect.lua (ory hydra)
-- S3: token introspection — POST with Basic client auth + a REUSABLE pool of
-- cc access tokens minted by setup.sh over the token endpoint (design D5).
-- ⚠️ Hydra serves introspection on the ADMIN port (4445) — run with
-- TARGET_URL=http://127.0.0.1:4445 (semantic difference annotated in
-- COMPARISON.md per design D3).
-- Endpoint (official): POST /admin/oauth2/introspect
--   https://www.ory.sh/docs/hydra/reference/api

local LIB_DIR = os.getenv("WRK_LIB_DIR") or error("WRK_LIB_DIR not set")
local TOKEN_FILE = LIB_DIR .. "/generated/cc_tokens.txt"

local tokens, tok_start, tok_end, tok_idx, basic
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
    local f = io.open(LIB_DIR .. "/generated/basic_header_svc.txt", "r")
    basic = f:read("*l")
    f:close()

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
    wrk.path = "/admin/oauth2/introspect"
    wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
    wrk.headers["Authorization"] = basic
    wrk.body = "token=" .. token
    return wrk.format()
end
