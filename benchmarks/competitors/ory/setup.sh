#!/usr/bin/env bash
# benchmarks/competitors/ory/setup.sh
#
# Boot Ory Hydra (official production image + PostgreSQL), migrate the schema,
# create the bench clients via the official CLI, then mint API-issued token
# pools (design D5). User-context tokens come from Hydra's official headless
# mock pattern (login/consent accept via the ADMIN API — Hydra has no user
# store; see mint_tokens.py header for the flow reference).
#
# Config sources (D2, fairness-annotated in COMPARISON.md):
#   * deploy:   https://www.ory.sh/docs/hydra/self-hosted/deploy-hydra
#   * login/consent flow (admin-API accept): https://www.ory.sh/docs/hydra/guides/login-login-consent-flow
#
# Deviations from stock defaults (COMPARISON.md appendix):
#   * DSN max_conns=25 (D1 pool alignment)
#   * no login/consent app deployed (accept flow driven headlessly)
#
# Runtime-generated throwaway credentials — nothing committed. Secrets are
# written to lib/generated/ (gitignored) for the Lua scenarios + reissue hook.
#
# Usage:
#   bash benchmarks/competitors/ory/setup.sh
#
# Env overrides:
#   PUBLIC_URL / ADMIN_URL   default http://127.0.0.1:4444 / :4445
#   POOL_CC / POOL_USER      default 2000 each
#   MINT_PARALLEL            default 16
set -euo pipefail

ORY_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PUBLIC_URL="${PUBLIC_URL:-http://127.0.0.1:4444}"
ADMIN_URL="${ADMIN_URL:-http://127.0.0.1:4445}"
POOL_CC="${POOL_CC:-2000}"
POOL_USER="${POOL_USER:-2000}"
MINT_PARALLEL="${MINT_PARALLEL:-16}"

GEN_DIR="$ORY_DIR/lib/generated"
mkdir -p "$GEN_DIR"

export HYDRA_SECRETS_SYSTEM="$(head -c 32 /dev/urandom | base64 | tr -d '/+=' | head -c 32)"
SVC_SECRET="$(head -c 24 /dev/urandom | base64 | tr -d '/+=' | head -c 28)"
WEB_SECRET="$(head -c 24 /dev/urandom | base64 | tr -d '/+=' | head -c 28)"
printf 'Basic %s' "$(printf '%s:%s' bench-svc "$SVC_SECRET" | base64 -w0)" > "$GEN_DIR/basic_header_svc.txt"
printf 'Basic %s' "$(printf '%s:%s' bench-web "$WEB_SECRET" | base64 -w0)" > "$GEN_DIR/basic_header_web.txt"
chmod 600 "$GEN_DIR"/basic_header_*.txt

# --- 0. port-free assertion ---
if curl -s -o /dev/null --max-time 2 "$PUBLIC_URL/health/ready" 2>/dev/null; then
    echo "[setup] ERROR: something already answers at $PUBLIC_URL — run teardown.sh first."
    exit 1
fi

# --- 1. boot + migrate (official pattern: migrate before serve) ---
echo "[setup] schema migration..."
docker compose -f "$ORY_DIR/docker-compose.yml" -p ory-bench run --rm hydra \
    migrate sql -e -y --config /etc/hydra/hydra.yml >/dev/null

echo "[setup] booting hydra + postgres..."
docker compose -f "$ORY_DIR/docker-compose.yml" -p ory-bench up -d --wait --wait-timeout 180

# --- 2. health gate ---
CODE=000
for i in $(seq 1 120); do
    CODE="$(curl -s -o /dev/null -w '%{http_code}' "$PUBLIC_URL/health/ready" 2>/dev/null || echo 000)"
    [ "$CODE" = "200" ] && break
    sleep 1
done
[ "$CODE" = "200" ] || { echo "[setup] ERROR: hydra not ready (code=$CODE)"; docker logs ory-bench-hydra 2>&1 | tail -30; exit 1; }
HYDRA_VERSION="$(docker exec ory-bench-hydra hydra version 2>/dev/null | head -1 || echo unknown)"
echo "[setup] hydra ready (version: $HYDRA_VERSION)"

# --- 3. create clients via official CLI ---
echo "[setup] creating clients (bench-svc: client_credentials; bench-web: auth_code+refresh)..."
docker exec ory-bench-hydra hydra create oauth2-client \
    --endpoint "$ADMIN_URL" --name bench-svc \
    --grant-type client_credentials \
    --token-endpoint-auth-method client_secret_basic \
    --secret "$SVC_SECRET" >/dev/null
docker exec ory-bench-hydra hydra create oauth2-client \
    --endpoint "$ADMIN_URL" --name bench-web \
    --grant-type authorization_code,refresh_token --response-type code \
    --token-endpoint-auth-method client_secret_basic \
    --redirect-uri http://127.0.0.1:4444/unused \
    --scope openid,offline_access \
    --secret "$WEB_SECRET" >/dev/null
echo "  clients created"

# --- 4. warm validation: one cc token + one full accept-flow token ---
curl -s -o /dev/null -w '' -u "bench-svc:$SVC_SECRET" -d 'grant_type=client_credentials' "$PUBLIC_URL/oauth2/token" \
    || { echo "[setup] ERROR: warm cc validation failed"; exit 1; }
python3 "$ORY_DIR/mint_tokens.py" --public "$PUBLIC_URL" --admin "$ADMIN_URL" \
    --client-id bench-web --client-secret "$WEB_SECRET" --mode user-at --count 1 \
    >/dev/null 2>/tmp/ory_warm_err || { cat /tmp/ory_warm_err; echo "[setup] ERROR: warm accept-flow validation failed"; exit 1; }
echo "[setup] warm validation OK (cc + accept-flow)"

# --- 5. allocator steady-state warm (Go has no JIT; brief 15s for symmetry) ---
echo "[setup] warm phase: 15s client_credentials load..."
WRK_LIB_DIR="$ORY_DIR/lib" wrk -t2 -c16 -d15s \
    -s "$ORY_DIR/scenarios/s2-client-credentials.lua" "$PUBLIC_URL" >/dev/null 2>&1 || true

# --- 6. mint pools (D5) ---
echo "[setup] minting cc pool ($POOL_CC)..."
python3 "$ORY_DIR/mint_tokens.py" --public "$PUBLIC_URL" --admin "$ADMIN_URL" \
    --client-id bench-svc --client-secret "$SVC_SECRET" --mode cc-token \
    --count "$POOL_CC" --parallel "$MINT_PARALLEL" > "$GEN_DIR/cc_tokens.txt"
echo "[setup] minting user AT pool ($POOL_USER)..."
python3 "$ORY_DIR/mint_tokens.py" --public "$PUBLIC_URL" --admin "$ADMIN_URL" \
    --client-id bench-web --client-secret "$WEB_SECRET" --mode user-at \
    --count "$POOL_USER" --parallel "$MINT_PARALLEL" > "$GEN_DIR/user_tokens.txt"

# --- 7. S5 calibration (c=8, 5s, one-shot mini pool) ---
echo "[setup] S5 calibration..."
python3 "$ORY_DIR/mint_tokens.py" --public "$PUBLIC_URL" --admin "$ADMIN_URL" \
    --client-id bench-web --client-secret "$WEB_SECRET" --mode user-rt \
    --count 800 --parallel "$MINT_PARALLEL" > "$GEN_DIR/refresh_tokens.txt"
CAL_OUT="$(mktemp)"
WRK_LIB_DIR="$ORY_DIR/lib" WRK_NTHREADS=1 wrk -t1 -c8 -d5s \
    -s "$ORY_DIR/scenarios/s5-refresh-token.lua" "$PUBLIC_URL" >"$CAL_OUT" 2>&1 || true
CAL_REQS="$(grep -oE '[0-9]+ requests in [0-9.]+s' "$CAL_OUT" | head -1 | awk '{print $1}')"
CAL_SECS="$(grep -oE '[0-9]+ requests in [0-9.]+s' "$CAL_OUT" | head -1 | awk '{print $4}' | tr -d s)"
rm -f "$CAL_OUT"
if [ -z "${CAL_REQS:-}" ] || [ "$CAL_REQS" -lt 100 ]; then
    echo "[setup] ERROR: S5 calibration failed (requests=${CAL_REQS:-none})"
    exit 1
fi
CAL_QPS="$(python3 -c "print(round($CAL_REQS / max($CAL_SECS, 0.001)))")"
echo "$CAL_QPS" > "$GEN_DIR/s5_calibration_qps.txt"
echo "[setup] S5 calibration: $CAL_QPS QPS @ c=8"

INIT_RT=$(( CAL_QPS * 20 ))
echo "[setup] minting initial RT pool ($INIT_RT)..."
python3 "$ORY_DIR/mint_tokens.py" --public "$PUBLIC_URL" --admin "$ADMIN_URL" \
    --client-id bench-web --client-secret "$WEB_SECRET" --mode user-rt \
    --count "$INIT_RT" --parallel "$MINT_PARALLEL" > "$GEN_DIR/refresh_tokens.txt"

# --- 8. self-checks ---
for f in cc_tokens.txt user_tokens.txt refresh_tokens.txt s5_calibration_qps.txt; do
    LINES=$(wc -l < "$GEN_DIR/$f")
    echo "[setup] pool $f: $LINES lines"
    [ "$LINES" -gt 0 ] || { echo "[setup] ERROR: empty pool $f"; exit 1; }
done
echo "$HYDRA_VERSION" > "$GEN_DIR/product_version.txt"
echo "[setup] DONE."
