-- s3-introspect.lua
-- S3: introspect — POST /oauth2/introspect with an active access token.
--
--   POST /oauth2/introspect
--   Authorization: Basic base64("backend-svc:test-secret")
--   Content-Type: application/x-www-form-urlencoded
--   body: token=<active AT>
--
-- Measures RS256 token verification + active-status DB lookup. This is the
-- SLOW path: the handler validates the client, hashes the token, looks up
-- oauth2_access_tokens, checks revoked=false AND expires_at >= now
-- (PostgresTokenRepository.cc:524-533), then builds the full introspection
-- response. An invalid/malformed token would hit the early-return fast path
-- ({"active": false}), which we deliberately AVOID — that would inflate
-- throughput with no business meaning.
--
-- The caller authenticates as backend-svc (CONFIDENTIAL, client_secret_basic).
-- The token being introspected was seeded with client_id=backend-svc.
--
-- Tokens are REUSABLE: introspect doesn't consume or revoke the token.
--
-- Usage:
--   WRK_LIB_DIR=/path/to/lib wrk -t<T> -c<C> -d30s --latency -s s3-introspect.lua <URL>

local LIB_DIR = os.getenv("WRK_LIB_DIR") or "benchmarks/fulla/lib"
local TOKEN_FILE = LIB_DIR .. "/generated/access_tokens.txt"

-- Per-thread state (loaded in init, NOT at module scope — wrk re-executes the
-- script per worker thread; setup() only runs in the main thread).
local tokens, tok_start, tok_end, tok_idx
local threads = {}

local function load_lines(filepath)
    local lines = {}
    local f = io.open(filepath, "r")
    if not f then error("cannot open " .. filepath) end
    for line in f:lines() do
        line = line:gsub("%s+", "")
        if line ~= "" then
            table.insert(lines, line)
        end
    end
    f:close()
    return lines
end

setup = function(thread)
    table.insert(threads, thread)
    thread:set("tid", #threads - 1)
end

init = function(args)
    -- tid is a global set by thread:set("tid", ...) in setup().
    if not tid then tid = 0 end

    tokens = load_lines(TOKEN_FILE)
    local n = #tokens

    local nthreads = tonumber(os.getenv("WRK_NTHREADS") or "1")
    if not nthreads or nthreads < 1 then nthreads = 1 end

    local per = math.floor(n / nthreads)
    local remainder = n % nthreads
    tok_start = tid * per + math.min(tid, remainder) + 1
    local extra = (tid < remainder) and 1 or 0
    tok_end = tok_start + per + extra - 1
    tok_idx = tok_start - 1
end

request = function()
    -- Cycle through this thread's token slice (reusable).
    tok_idx = tok_idx + 1
    if tok_idx > tok_end then
        tok_idx = tok_start
    end
    local token = tokens[tok_idx]

    wrk.method = "POST"
    wrk.path = "/oauth2/introspect"
    wrk.headers["Content-Type"] = "application/x-www-form-urlencoded"
    -- backend-svc:test-secret → base64 = YmFja2VuZC1zdmM6dGVzdC1zZWNyZXQ=
    wrk.headers["Authorization"] = "Basic YmFja2VuZC1zdmM6dGVzdC1zZWNyZXQ="
    wrk.body = "token=" .. token
    return wrk.format()
end
