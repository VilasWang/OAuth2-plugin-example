#!/usr/bin/env bash
# benchmarks/authforge/pool-sweep.sh
#
# DB pool capacity sweep (noncode-performance-optimization.md §五.1 / §八 step 2).
# Pool 25 is the four-product ALIGNMENT value, not an AuthForge optimum — the
# high-concurrency P99 tail (430ms-class) is pool queuing. This sweep re-runs
# representative read/write scenarios at several db_clients pool sizes and
# lands structured results + a summary table for capacity planning.
#
# Usage:
#   bash benchmarks/authforge/pool-sweep.sh [pool ...]     # default: 25 64 100
#
# Protocol:
#   - pool #1 runs a FULL deterministic setup (down -v; fresh PG with current
#     migrations, seed, token pools, user warmup) — reuse benchmarks/authforge/
#     setup.sh machinery.
#   - later pools keep the stack (no volume reset), rewrite the db pool in the
#     swapped-in bench config.json and `docker restart oauth2-backend`
#     (config.json is bind-mounted read-only; a container restart re-reads it).
#   - scenarios: S2 (write), S3 (read), S6 (read) at levels ${LEVELS}.
#     S5 excluded: refresh consumes its token pool (reseed wiring is a
#     run-scenario feature, orthogonal to pool size).
#   - results: benchmarks/results/pool-sweep/pool<N>/<date>-<sha>-<sc>-c<C>.json
#     (RESULTS_DIR redirect; the canonical results/ stays untouched).
#
# Caveat (documented): pools run ascending, so later pools see a slightly
# larger token/audit table (~50k rows/pool iteration) — well under the effect
# size under test (pool queuing), but worth remembering on tiny deltas.
#
# Env overrides: LEVELS, WARMUP_S, DURATION_S, TARGET_URL.
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$BENCH_DIR/../.." && pwd)"
TARGET_URL="${TARGET_URL:-http://127.0.0.1:5555}"
LEVELS="${LEVELS:-16 32 64 128}"
export WARMUP_S="${WARMUP_S:-5}"
export DURATION_S="${DURATION_S:-10}"

POOLS=("$@")
if [ ${#POOLS[@]} -eq 0 ]; then
    POOLS=(25 64 100)
fi

SWEEP_DIR="$REPO_ROOT/benchmarks/results/pool-sweep"
mkdir -p "$SWEEP_DIR"

wait_ready() {
    local i code
    for i in $(seq 1 60); do
        code="$(curl -s -o /dev/null -w '%{http_code}' "$TARGET_URL/health/ready" 2>/dev/null || echo 000)"
        [ "$code" = "200" ] && return 0
        sleep 2
    done
    echo "[pool-sweep] ERROR: backend not ready within 120s (last $code)"
    return 1
}

set_pool() {
    local pool="$1"
    python3 - "$REPO_ROOT/apps/server/config/config.json" "$pool" <<'PYEOF'
import json, sys
path, pool = sys.argv[1], int(sys.argv[2])
cfg = json.load(open(path))
cfg["db_clients"][0]["number_of_connections"] = pool
json.dump(cfg, open(path, "w"), indent=4)
print(f"[pool-sweep] db_clients pool -> {pool}")
PYEOF
}

snapshot_env() {
    {
        echo "# pool-sweep environment snapshot ($(date -u +%Y-%m-%dT%H:%M:%SZ))"
        docker exec oauth2-postgres psql -U oauth2_user -d oauth2_db -tAc \
            "SELECT version()" | head -1
        for guc in shared_buffers effective_cache_size checkpoint_timeout max_wal_size wal_compression; do
            printf '%s = ' "$guc"
            docker exec oauth2-postgres psql -U oauth2_user -d oauth2_db -tAc "SHOW $guc"
        done
        echo "pg rows: tokens=$(docker exec oauth2-postgres psql -U oauth2_user -d oauth2_db -tAc 'SELECT count(*) FROM oauth2_access_tokens' | tr -d ' ') audit=$(docker exec oauth2-postgres psql -U oauth2_user -d oauth2_db -tAc 'SELECT count(*) FROM audit_logs' | tr -d ' ')"
    } >> "$SWEEP_DIR/env.txt" 2>/dev/null || true
}

for idx in "${!POOLS[@]}"; do
    pool="${POOLS[$idx]}"
    out_dir="$SWEEP_DIR/pool$pool"
    mkdir -p "$out_dir"

    if [ "$idx" -eq 0 ]; then
        echo "== [pool-sweep] pool=$pool: full deterministic setup =="
        bash "$BENCH_DIR/setup.sh"
    fi
    # rewrite the pool in the swapped-in bench config and restart the backend
    # (setup.sh swapped config.bench.json in; config.json is bind-mounted ro,
    # a container restart re-reads it)
    set_pool "$pool"
    docker restart oauth2-backend >/dev/null
    wait_ready
    snapshot_env

    export RESULTS_DIR="$out_dir"
    for sc in s2-client-credentials s3-introspect s6-userinfo; do
        echo "-- pool=$pool $sc (${LEVELS}) --"
        bash "$BENCH_DIR/run-scenario.sh" "$BENCH_DIR/scenarios/$sc.lua" $LEVELS 2>&1 | sed 's/^/   /' || true
    done
done

# --- restore the swapped-in config to the committed bench profile ---
# The sweep rewrote db_clients pool values in the LIVE swapped config.json
# (apps/server/config/config.json, currently the config.bench.json copy).
# Restore it from config.bench.json so a later setup.sh that skips its own
# swap (leftover .dev-backup guard) doesn't inherit the last sweep value —
# that exact leak once booted the stack at pool=100 and starved PG
# (max_connections) so hard even psql got "too many clients".
cp "$REPO_ROOT/apps/server/config/config.bench.json" "$REPO_ROOT/apps/server/config/config.json"
echo "[pool-sweep] restored swapped config.json from config.bench.json"

# --- summary table ---
python3 - "$SWEEP_DIR" "${POOLS[@]}" <<'PYEOF'
import json, sys, glob, re, os
swEEP = sys.argv[1]
pools = sys.argv[2:]
data = {}  # (scenario, c) -> {pool: (qps, p99_ms)}
for p in pools:
    for f in glob.glob(os.path.join(swEEP, f"pool{p}", "*.json")):
        m = re.search(r"-(s\d[^-]*)-c(\d+)\.json$", os.path.basename(f))
        if not m:
            continue
        d = json.load(open(f))
        qps = d.get("qps") or 0
        p99 = (d.get("latency_us") or {}).get("p99", 0) / 1000.0
        data.setdefault((m.group(1), int(m.group(2))), {})[p] = (qps, p99)
hdr = f"{'scenario':<22}{'c':>5}" + "".join(f"{('pool'+p):>16}" for p in pools)
print(hdr)
for k in sorted(data):
    row = f"{k[0]:<22}{k[1]:>5}"
    for p in pools:
        v = data[k].get(p)
        row += f"{(f'{v[0]:.0f} / {v[1]:.1f}ms' if v else '—'):>16}"
    print(row)
print("\n(cells: QPS / p99ms)")
PYEOF

echo "[pool-sweep] done. results under $SWEEP_DIR"
