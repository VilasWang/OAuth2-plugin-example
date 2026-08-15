#!/usr/bin/env bash
# benchmarks/competitors/zitadel/setup.sh
#
# Boot Zitadel (official production image + PostgreSQL) via the documented
# `start-from-setup` bootstrap, rendering setup-steps.yaml with runtime
# credentials. The FirstInstance.Machine block bootstraps a Service User with
# a JSON key (JWT profile — Zitadel's official M2M path) and a PAT without
# touching the login UI.
#
# Config sources (D2, fairness-annotated in COMPARISON.md):
#   * compose:   https://zitadel.com/docs/self-hosting/deploy/compose
#   * service users / JWT profile: https://zitadel.com/docs/guides/integrate/service-users
#
# Scenario coverage decisions (design D3/M2 + decision gates DG-2):
#   * S1 discovery     — plain GET
#   * S2 cc            — JWT profile with a PRE-SIGNED assertion pool (wrk Lua
#                        cannot sign; assertions minted here, exp covers the
#                        session — annotated as Zitadel's official auth path)
#   * S3 introspect    — introspect machine tokens, auth via JWT-profile
#                        assertion (same official mechanism)
#   * S5/S6            — attempted at runtime (Session API → auth_code);
#                        N/A with an honest annotation if undrivable
#
# Usage:
#   bash benchmarks/competitors/zitadel/setup.sh
#
# Env overrides:
#   ZITADEL_URL   default http://127.0.0.1:8080
#   POOL_CC       default 2000 (S3 introspect pool)
set -euo pipefail

ZA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ZITADEL_URL="${ZITADEL_URL:-http://127.0.0.1:8080}"
POOL_CC="${POOL_CC:-2000}"
GEN_DIR="$ZA_DIR/lib/generated"
mkdir -p "$GEN_DIR"

# runtime-generated throwaway credentials
export ZITADEL_MASTERKEY="$(head -c 32 /dev/urandom | base64 | tr -d '/+=' | head -c 32)"
ADMIN_PASS="$(head -c 18 /dev/urandom | base64 | tr -d '/+=' | head -c 20)"

# --- 0. port-free assertion ---
if curl -s -o /dev/null --max-time 2 "$ZITADEL_URL/debug/healthz" 2>/dev/null; then
    echo "[setup] ERROR: something already answers at $ZITADEL_URL — run teardown.sh first."
    exit 1
fi

# --- 1. render setup-steps.yaml ---
PAT_EXPIRY="$(date -u -d '+48 hours' +%Y-%m-%dT%H:%M:%SZ)"
sed -e "s|__ADMIN_PASS__|$ADMIN_PASS|g" -e "s|__ADMIN_USER__|bench-admin|g" \
    -e "s|__PAT_EXPIRY__|$PAT_EXPIRY|g" \
    "$ZA_DIR/setup-steps.yaml" > "$ZA_DIR/setup-steps.rendered.yaml"

echo "[setup] booting zitadel + postgres (start-from-setup; first boot ~1-2 min)..."
docker compose -f "$ZA_DIR/docker-compose.yml" -p zitadel-bench up -d --wait --wait-timeout 600

# --- 2. health gate ---
CODE=000
for i in $(seq 1 120); do
    CODE="$(curl -s -o /dev/null -w '%{http_code}' "$ZITADEL_URL/debug/healthz" 2>/dev/null || echo 000)"
    [ "$CODE" = "200" ] && break
    sleep 1
done
[ "$CODE" = "200" ] || { echo "[setup] ERROR: zitadel not ready (code=$CODE)"; docker logs zitadel-bench-zitadel 2>&1 | tail -30; exit 1; }
ZA_VERSION="$(curl -s "$ZITADEL_URL/debug/healthz" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("version","unknown"))' 2>/dev/null || echo unknown)"
echo "[setup] zitadel ready (version: $ZA_VERSION)"

# --- 3. extract machine key + PAT from the bootstrap volume ---
docker cp zitadel-bench-zitadel:/machinekey/bench-svc.json "$GEN_DIR/machinekey.json" 2>/dev/null \
    || docker run --rm -v zitadel-bench-machinekey:/machinekey -v "$(cd "$GEN_DIR" && pwd)":/out \
       alpine:3.19 cp /machinekey/bench-svc.json /out/machinekey.json
[ -s "$GEN_DIR/machinekey.json" ] || { echo "[setup] ERROR: machine key not found after bootstrap"; exit 1; }
chmod 600 "$GEN_DIR/machinekey.json"
echo "[setup] machine key extracted"

# --- 4. warm validation: JWT-profile client_credentials through the pipeline ---
python3 "$ZA_DIR/mint_tokens.py" --issuer "$ZITADEL_URL" \
    --key "$GEN_DIR/machinekey.json" --mode cc-token --count 1 --scope "openid profile" \
    > "$GEN_DIR/warm_token.txt" 2>/tmp/za_mint_err || {
        cat /tmp/za_mint_err; echo "[setup] ERROR: warm JWT-profile validation failed"; exit 1; }
echo "[setup] warm validation OK (JWT-profile cc 200)"

# --- 5. warm phase (brief, Go runtime) ---
echo "[setup] warm phase: 15s client_credentials load..."
WRK_LIB_DIR="$ZA_DIR/lib" wrk -t2 -c16 -d15s \
    -s "$ZA_DIR/scenarios/s2-client-credentials.lua" "$ZITADEL_URL" >/dev/null 2>&1 || true

# --- 6. mint pools ---
echo "[setup] minting S2 assertion pool (1 reusable assertion, exp 2h)..."
python3 "$ZA_DIR/mint_tokens.py" --issuer "$ZITADEL_URL" \
    --key "$GEN_DIR/machinekey.json" --mode assertion --count 1 --assertion-ttl 7200 \
    > "$GEN_DIR/assertion.txt"
echo "[setup] minting cc token pool ($POOL_CC, S3 introspect / S6 candidate)..."
python3 "$ZA_DIR/mint_tokens.py" --issuer "$ZITADEL_URL" \
    --key "$GEN_DIR/machinekey.json" --mode cc-token --count "$POOL_CC" --scope "openid profile" \
    > "$GEN_DIR/cc_tokens.txt"

# --- 7. self-checks ---
for f in assertion.txt cc_tokens.txt machinekey.json; do
    [ -s "$GEN_DIR/$f" ] || { echo "[setup] ERROR: missing $f"; exit 1; }
    echo "[setup] pool $f: $(wc -l < "$GEN_DIR/$f") lines"
done
echo "$ZA_VERSION" > "$GEN_DIR/product_version.txt"
echo "[setup] DONE."
