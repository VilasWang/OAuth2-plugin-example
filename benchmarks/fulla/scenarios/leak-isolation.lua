-- leak-isolation.lua: GET a fixed 404 path — exercises the full Drogon
-- framework per-request machinery (parse/route/response) WITHOUT entering any
-- Fulla controller. Diagnostic for the memory-retention investigation.
local path = os.getenv("LEAK_PATH") or "/nonexistent-404-probe"
local idx = 0
request = function()
    idx = idx + 1
    wrk.method = "GET"
    wrk.path = path
    wrk.body = nil
    wrk.headers["Content-Type"] = nil
    return wrk.format()
end
