#!/usr/bin/env bash
# benchmarks/competitors/keycloak/reissue-rt-pool.sh
#
# --reissue hook for run-scenario.sh (S5 refresh_token): re-mint the one-shot
# RT pool sized for the upcoming phase. Invoked by the runner with:
#   WRK_REISSUE_PHASE = pre-warmup | pre-measure
#   WRK_LEVEL_CONN    = concurrency of the level about to run
#   WARMUP_S / DURATION_S exported by the runner
#
# Sizing (design D5 v1.1): pool = calib_QPS(c=8) × conn-scale × phase-seconds ×
# margin, where conn-scale saturates at 1.6 (QPS grows sublinearly past c=8),
# margin 1.2 for the warmup phase / 1.3 for the measured phase. Override the
# whole multiplier with S5_POOL_MULTIPLIER if a level still exhausts early.
set -euo pipefail

KC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GEN_DIR="$KC_DIR/lib/generated"

KC_URL="${KC_URL:-http://127.0.0.1:8080}"
MINT_PARALLEL="${MINT_PARALLEL:-8}"
PHASE="${WRK_REISSUE_PHASE:-pre-measure}"
CONN="${WRK_LEVEL_CONN:-8}"
WARMUP_S="${WARMUP_S:-5}"
DURATION_S="${DURATION_S:-10}"

CAL_QPS="$(cat "$GEN_DIR/s5_calibration_qps.txt")"
CLIENT_SECRET="$(cat "$GEN_DIR/client_secret.txt")"

# Adaptive sizing: one-shot pools cap observed throughput at pool/duration, so
# the calibration may underestimate. If earlier staircase levels produced
# result JSONs, use the best observed QPS as a second estimate.
OBS_QPS="$(python3 - "${RESULTS_DIR:-/dev/null}" <<'PY' 2>/dev/null || echo 0
import glob, json, os, sys
best = 0.0
for f in glob.glob(os.path.join(sys.argv[1], "*-keycloak-s5-refresh-token-c*.json")):
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

echo "    [reissue] phase=$PHASE c=$CONN calib=$CAL_QPS obs=$OBS_QPS est=$EST_QPS scale=$SCALE secs=$SECS -> $COUNT RTs"
python3 "$KC_DIR/mint_tokens.py" --url "$KC_URL" --realm bench \
    --client-id bench-svc --client-secret "$CLIENT_SECRET" \
    --grant password --username bench-user --password bench-pass-local \
    --scope openid --extract refresh_token --count "$COUNT" --parallel "$MINT_PARALLEL" \
    > "$GEN_DIR/refresh_tokens.txt"

ACTUAL="$(wc -l < "$GEN_DIR/refresh_tokens.txt")"
[ "$ACTUAL" -ge "$COUNT" ] || { echo "    [reissue] ERROR: pool short ($ACTUAL < $COUNT)"; exit 1; }
