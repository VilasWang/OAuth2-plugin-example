-- s6-userinfo.lua
-- S6: userinfo — GET /oauth2/userinfo with a bearer access token.
--
--   GET /oauth2/userinfo
--   Authorization: Bearer <active user AT>
--
-- Exercises the OAuth2AuthFilter (bearer validation) + user record lookup.
-- The filter requires the "openid" scope (OAuth2AuthFilter.cc:41-42) and
-- rejects M2M tokens (subject starting with "client:"). The seeded ATs have
-- scope "openid profile" and user-scoped subjects (bench_user_NNNN), so they
-- pass both checks.
--
-- Tokens are REUSABLE: userinfo validates the AT (hash → lookup → check
-- revoked/expired) but does not consume it. So the pool cycles with modulo
-- wrap and never exhausts.
--
-- Usage:
--   WRK_LIB_DIR=/path/to/lib wrk -t<T> -c<C> -d30s --latency -s s6-userinfo.lua <URL>
--
-- The token file is loaded from $WRK_LIB_DIR/generated/access_tokens.txt
-- (set WRK_LIB_DIR to the benchmarks/authforge/lib directory; run-scenario.sh
-- exports it automatically).

local LIB_DIR = os.getenv("WRK_LIB_DIR") or "benchmarks/authforge/lib"
local TOKEN_FILE = LIB_DIR .. "/generated/access_tokens.txt"

-- Per-thread state. In wrk, each worker thread re-executes the whole script
-- and runs init(); setup() only runs in the main thread. So data must be
-- loaded in init(), NOT at module scope.
local tokens, tok_start, tok_end, tok_idx
local threads = {}

-- Load a file as a 1-indexed table of lines (stripped).
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

-- setup(thread) runs ONCE PER THREAD in the MAIN thread. We use it only to
-- assign each thread a 0-based tid (for pool slicing). The actual data loading
-- happens in init() on each worker thread.
setup = function(thread)
    table.insert(threads, thread)
    thread:set("tid", #threads - 1)
end

init = function(args)
    -- tid is a global set by thread:set("tid", ...) in setup().
    if not tid then tid = 0 end

    -- Load the token pool in THIS worker thread's Lua state.
    tokens = load_lines(TOKEN_FILE)
    local n = #tokens

    -- Slice the pool across threads.
    local nthreads = tonumber(os.getenv("WRK_NTHREADS") or "1")
    if not nthreads or nthreads < 1 then nthreads = 1 end

    local per = math.floor(n / nthreads)
    local remainder = n % nthreads
    tok_start = tid * per + math.min(tid, remainder) + 1
    local extra = (tid < remainder) and 1 or 0
    tok_end = tok_start + per + extra - 1
    tok_idx = tok_start - 1  -- incremented before first use → starts at tok_start
end

request = function()
    -- Cycle through this thread's token slice (reusable — userinfo doesn't
    -- consume tokens, so we wrap within [tok_start, tok_end]).
    tok_idx = tok_idx + 1
    if tok_idx > tok_end then
        tok_idx = tok_start
    end
    local token = tokens[tok_idx]

    wrk.method = "GET"
    wrk.path = "/oauth2/userinfo"
    wrk.headers["Content-Type"] = nil
    wrk.headers["Authorization"] = "Bearer " .. token
    wrk.body = nil
    return wrk.format()
end
