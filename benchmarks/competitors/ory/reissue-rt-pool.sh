#!/usr/bin/env bash
# benchmarks/competitors/ory/reissue-rt-pool.sh — --reissue hook for S5 (D5).
# Sizes the one-shot RT pool from calibration + observed staircase levels
# (same adaptive scheme as the Keycloak hook; see that file for rationale).
set -euo pipefail

ORY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GEN_DIR="$ORY_DIR/lib/generated"

PUBLIC_URL="${PUBLIC_URL:-https://127.0.0.1:4444}"   # TLS direct serve — see hydra.yml
ADMIN_URL="${ADMIN_URL:-https://127.0.0.1:4445}"     # serve.tls shared: admin TLS too
MINT_PARALLEL="${MINT_PARALLEL:-16}"
PHASE="${WRK_REISSUE_PHASE:-pre-measure}"
CONN="${WRK_LEVEL_CONN:-8}"
WARMUP_S="${WARMUP_S:-5}"
DURATION_S="${DURATION_S:-10}"

CAL_QPS="$(cat "$GEN_DIR/s5_calibration_qps.txt")"
WEB_SECRET="$(python3 -c "
import base64
hdr = open('$GEN_DIR/basic_header_web.txt').read().strip()
print(base64.b64decode(hdr.split(' ', 1)[1]).decode().split(':', 1)[1])")"

OBS_QPS="$(python3 - "${RESULTS_DIR:-/dev/null}" <<'PY' 2>/dev/null || echo 0
import glob, json, os, sys
best = 0.0
for f in glob.glob(os.path.join(sys.argv[1], "*-ory-s5-refresh-token-c*.json")):
    try:
        q = (json.load(open(f)) or {}).get("qps") or 0
        best = max(best, float(q))
    except Exception:
        pass
print(int(best) if best else 0)
PY
)"
EST_QPS=$(( CAL_QPS > OBS_QPS ? CAL_QPS : OBS_QPS ))

SCALE="$(python3 -c "print(min(max(1.0, $CONN / 8), 1.6))")"
case "$PHASE" in
    pre-warmup) SECS="$WARMUP_S"; MARGIN=1.2 ;;
    *)          SECS="$DURATION_S"; MARGIN="${S5_POOL_MULTIPLIER:-1.3}" ;;
esac
COUNT="$(python3 -c "print(int($EST_QPS * $SCALE * $SECS * $MARGIN) + 50)")"

echo "    [reissue] phase=$PHASE c=$CONN calib=$CAL_QPS obs=$OBS_QPS est=$EST_QPS -> $COUNT RTs"
python3 "$ORY_DIR/mint_tokens.py" --public "$PUBLIC_URL" --admin "$ADMIN_URL" \
    --client-id bench-web --client-secret "$WEB_SECRET" --mode user-rt \
    --count "$COUNT" --parallel "$MINT_PARALLEL" > "$GEN_DIR/refresh_tokens.txt"

ACTUAL="$(wc -l < "$GEN_DIR/refresh_tokens.txt")"
[ "$ACTUAL" -ge $((COUNT * 99 / 100)) ] || { echo "    [reissue] ERROR: pool short ($ACTUAL < $COUNT)"; exit 1; }
