#!/usr/bin/env bash
# benchmarks/competitors/keycloak/setup.sh
#
# Boot Keycloak (official production image + PostgreSQL) for the competitor
# benchmark, initialize realm/client/user headlessly via kcadm, heat the JVM
# (60s client_credentials load — design D2 JIT exemption, implemented here in
# setup so every measured scenario can use the same 5s warmup as Fulla),
# then mint the API-issued token pools (design D5: competitor tokens MUST come
# from the product itself, never SQL).
#
# Config sources (D2, fairness-annotated in COMPARISON.md):
#   * container:      https://www.keycloak.org/server/containers
#   * PostgreSQL:     https://www.keycloak.org/server/db
#   * direct grants:  https://www.keycloak.org/docs/latest/securing_apps/#direct-access-grants
#
# Deviations from stock defaults (all listed in COMPARISON.md appendix):
#   * KC_DB_POOL_MAX_SIZE=25 (D1 pool alignment; default 100)
#   * realm accessTokenLifespan=3600 (pools must outlive the ~15min staircase;
#     default 300s expires pools mid-run; signature path unaffected)
#
# All credentials (bootstrap admin, bench client secret) are generated at
# runtime for this local-only stack — nothing committed. The bench client
# secret is written to lib/generated/client_secret.txt (gitignored) for the
# Lua scenarios + reissue hook to read.
#
# Usage:
#   bash benchmarks/competitors/keycloak/setup.sh
#
# Env overrides:
#   KC_URL          default http://127.0.0.1:8080
#   KC_REALM        FIXED 'bench' — scenarios/*.lua and reissue-rt-pool.sh
#                   hardcode the realm name (design: fixed constants); do not override
#   POOL_CC / POOL_USER   default 2000 each (reusable S3/S6 pools)
#   MINT_PARALLEL   default 8
set -euo pipefail

KC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$KC_DIR/../../.." && pwd)"

KC_URL="${KC_URL:-http://127.0.0.1:8080}"
KC_REALM="${KC_REALM:-bench}"
CLIENT_ID="bench-svc"
BENCH_USER="bench-user"
BENCH_PASS="bench-pass-local"
POOL_CC="${POOL_CC:-2000}"
POOL_USER="${POOL_USER:-2000}"
MINT_PARALLEL="${MINT_PARALLEL:-8}"

GEN_DIR="$KC_DIR/lib/generated"
mkdir -p "$GEN_DIR"

# runtime-generated throwaway credentials (local-only bench stack)
ADMIN_USER="admin"
ADMIN_PASS="$(head -c 18 /dev/urandom | base64 | tr -d '/+=' | head -c 20)"
CLIENT_SECRET="$(head -c 24 /dev/urandom | base64 | tr -d '/+=' | head -c 28)"
printf '%s' "$CLIENT_SECRET" > "$GEN_DIR/client_secret.txt"
chmod 600 "$GEN_DIR/client_secret.txt"
# precomputed Authorization header (wrk Lua has no base64)
printf 'Basic %s' "$(printf '%s:%s' "$CLIENT_ID" "$CLIENT_SECRET" | base64 -w0)" > "$GEN_DIR/basic_header.txt"
export KC_BOOTSTRAP_ADMIN_USERNAME="$ADMIN_USER" KC_BOOTSTRAP_ADMIN_PASSWORD="$ADMIN_PASS"

# --- 0. port-free assertion (D1: serial stacks, pinned ports) ---
# Ready probe = /realms/master on the app port 8080: KC 26 serves /health/*
# on a separate management port (9000, container-internal). The compose-level
# `--wait` already gates on the image's own management-port healthcheck.
if curl -s -o /dev/null --max-time 2 "$KC_URL/realms/master" 2>/dev/null; then
    echo "[setup] ERROR: something already answers at $KC_URL — run teardown.sh first."
    exit 1
fi

# --- 1. boot the stack ---
echo "[setup] booting keycloak + postgres..."
docker compose -f "$KC_DIR/docker-compose.yml" -p kc-bench up -d --wait --wait-timeout 300

# --- 2. health gate (first boot creates the PG schema) ---
echo "[setup] waiting for /realms/master (app-port ready probe)..."
CODE=000
for i in $(seq 1 300); do
    CODE="$(curl -s -o /dev/null -w '%{http_code}' "$KC_URL/realms/master" 2>/dev/null || echo 000)"
    [ "$CODE" = "200" ] && break
    sleep 1
done
if [ "$CODE" != "200" ]; then
    echo "[setup] ERROR: keycloak not ready after 300s (last code=$CODE). Logs:"
    docker logs kc-bench-keycloak 2>&1 | tail -30
    exit 1
fi
KC_VERSION="$(docker inspect kc-bench-keycloak --format '{{.Config.Image}}' 2>/dev/null | sed 's/.*://' || true)"
[ -n "$KC_VERSION" ] || KC_VERSION="unknown"
echo "[setup] keycloak ready (version: $KC_VERSION)"

KCADM=(docker exec -e KC_BOOTSTRAP_ADMIN_USERNAME -e KC_BOOTSTRAP_ADMIN_PASSWORD \
    kc-bench-keycloak /opt/keycloak/bin/kcadm.sh)

# --- 3. headless init via kcadm (official container pattern) ---
echo "[setup] kcadm: configure credentials..."
"${KCADM[@]}" config credentials --server "$KC_URL" --realm master \
    --user "$ADMIN_USER" --password "$ADMIN_PASS" >/dev/null

echo "[setup] kcadm: create realm '$KC_REALM' (accessTokenLifespan=3600, ssoSessionIdle=3600 — pools must outlive the staircase session; see header note)..."
if "${KCADM[@]}" create realms -s realm="$KC_REALM" -s enabled=true \
        -s accessTokenLifespan=3600 -s ssoSessionIdleTimeout=3600 -s ssoSessionMaxLifespan=7200 2>/dev/null; then
    echo "  realm created"
else
    echo "  realm exists (idempotent rerun) — updating lifespans"
    "${KCADM[@]}" update realms/"$KC_REALM" -s accessTokenLifespan=3600 \
        -s ssoSessionIdleTimeout=3600 -s ssoSessionMaxLifespan=7200
fi

echo "[setup] kcadm: create confidential client '$CLIENT_ID' (service account + direct grants)..."
CLIENT_UUID="$("${KCADM[@]}" get clients -r "$KC_REALM" -q clientId="$CLIENT_ID" 2>/dev/null | python3 -c 'import json,sys; rows=json.load(sys.stdin); print(rows[0]["id"] if rows else "")' || true)"
if [ -z "$CLIENT_UUID" ]; then
    "${KCADM[@]}" create clients -r "$KC_REALM" \
        -s clientId="$CLIENT_ID" -s enabled=true -s protocol=openid-connect \
        -s publicClient=false -s secret="$CLIENT_SECRET" \
        -s serviceAccountsEnabled=true -s directAccessGrantsEnabled=true \
        -s standardFlowEnabled=false >/dev/null
    CLIENT_UUID="$("${KCADM[@]}" get clients -r "$KC_REALM" -q clientId="$CLIENT_ID" | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["id"])')"
    echo "  client created"
else
    "${KCADM[@]}" update clients/"$CLIENT_UUID" -r "$KC_REALM" -s secret="$CLIENT_SECRET" \
        -s serviceAccountsEnabled=true -s directAccessGrantsEnabled=true >/dev/null
    echo "  client exists (idempotent rerun, secret rotated)"
fi

# KC 26 enforces audience checks on introspection ("Client is not in the token
# audience"): tokens must carry bench-svc in `aud` for our introspection calls
# to report active:true. A client-level audience mapper is the standard KC way
# (bench config, non-perf — fairness-annotated in COMPARISON.md).
if ! "${KCADM[@]}" get clients/"$CLIENT_UUID"/protocol-mappers/models -r "$KC_REALM" 2>/dev/null \
        | python3 -c 'import json,sys; raise SystemExit(0 if any(m.get("name")=="aud-bench-self" for m in json.load(sys.stdin)) else 1)'; then
    "${KCADM[@]}" create clients/"$CLIENT_UUID"/protocol-mappers/models -r "$KC_REALM" \
        -s name=aud-bench-self -s protocol=openid-connect \
        -s protocolMapper=oidc-audience-mapper \
        -s 'config={"access.token.claim":"true","included.client.audience":"'"$CLIENT_ID"'"}' >/dev/null
    echo "  audience mapper added (introspection audience fix)"
fi

echo "[setup] kcadm: create user '$BENCH_USER'..."
if ! "${KCADM[@]}" get users -r "$KC_REALM" -q username="$BENCH_USER" 2>/dev/null | python3 -c 'import json,sys; raise SystemExit(0 if json.load(sys.stdin) else 1)'; then
    "${KCADM[@]}" create users -r "$KC_REALM" \
        -s username="$BENCH_USER" -s enabled=true \
        -s firstName=bench -s lastName=user \
        -s email="bench-user@bench.invalid" -s emailVerified=true >/dev/null
fi
BENCH_USER_ID="$("${KCADM[@]}" get users -r "$KC_REALM" -q username="$BENCH_USER" | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["id"])')"
# KC 26 defaults new users to the "verify-profile" required action, which makes
# ROPC fail with "Account is not fully set up" — clear it (bench user, headless).
"${KCADM[@]}" update users/"$BENCH_USER_ID" -r "$KC_REALM" -s 'requiredActions=[]' >/dev/null
"${KCADM[@]}" set-password -r "$KC_REALM" --userid "$BENCH_USER_ID" --new-password "$BENCH_PASS" >/dev/null
echo "  user ready"

# --- 4. warm validation: one token request through the full pipeline ---
TOKEN_ENDPOINT="$KC_URL/realms/$KC_REALM/protocol/openid-connect/token"
WARM_CODE="$(curl -s -o /dev/null -w '%{http_code}' -u "$CLIENT_ID:$CLIENT_SECRET" \
    -d 'grant_type=client_credentials' "$TOKEN_ENDPOINT")"
if [ "$WARM_CODE" != "200" ]; then
    echo "[setup] ERROR: warm validation client_credentials request failed (code=$WARM_CODE)"
    exit 1
fi
echo "[setup] warm validation OK (client_credentials 200)"

# --- 5. JVM heat phase (design D2 JIT exemption, 60s) ---
# JIT compilation is process-wide: heat with anonymous client_credentials load
# here, so the measured staircase scenarios can use the same 5s warmup as the
# other three products. Fairness-annotated in COMPARISON.md.
echo "[setup] JVM heat: 60s of client_credentials load (JIT compile)..."
WRK_LIB_DIR="$KC_DIR/lib" wrk -t2 -c16 -d60s \
    -s "$KC_DIR/scenarios/s2-client-credentials.lua" "$KC_URL" >/dev/null 2>&1 || true

# --- 6. mint token pools (D5) ---
echo "[setup] minting pools (cc=$POOL_CC, user=$POOL_USER, via API)..."
python3 "$KC_DIR/mint_tokens.py" --url "$KC_URL" --realm "$KC_REALM" \
    --client-id "$CLIENT_ID" --client-secret "$CLIENT_SECRET" \
    --grant client_credentials --count "$POOL_CC" --parallel "$MINT_PARALLEL" \
    > "$GEN_DIR/cc_tokens.txt"
python3 "$KC_DIR/mint_tokens.py" --url "$KC_URL" --realm "$KC_REALM" \
    --client-id "$CLIENT_ID" --client-secret "$CLIENT_SECRET" \
    --grant password --username "$BENCH_USER" --password "$BENCH_PASS" \
    --scope openid --extract access_token --count "$POOL_USER" --parallel "$MINT_PARALLEL" \
    > "$GEN_DIR/user_tokens.txt"

# --- 7. S5 calibration: measure achievable refresh QPS at c=8 ---
# One-shot pools cap observed throughput at pool/duration, so the mini pool is
# generous (6000) and the window short (3s); reissue-rt-pool.sh refines the
# estimate adaptively from measured levels (max(calib, observed)).
echo "[setup] S5 calibration (c=8, 3s, 6000-token mini pool)..."
python3 "$KC_DIR/mint_tokens.py" --url "$KC_URL" --realm "$KC_REALM" \
    --client-id "$CLIENT_ID" --client-secret "$CLIENT_SECRET" \
    --grant password --username "$BENCH_USER" --password "$BENCH_PASS" \
    --scope openid --extract refresh_token --count 6000 --parallel "$MINT_PARALLEL" \
    > "$GEN_DIR/refresh_tokens.txt"
CAL_OUT="$(mktemp)"
WRK_LIB_DIR="$KC_DIR/lib" WRK_NTHREADS=1 wrk -t1 -c8 -d3s \
    -s "$KC_DIR/scenarios/s5-refresh-token.lua" "$KC_URL" >"$CAL_OUT" 2>&1 || true
CAL_REQS="$(grep -oE '[0-9]+ requests in [0-9.]+s' "$CAL_OUT" | head -1 | awk '{print $1}')"
CAL_SECS="$(grep -oE '[0-9]+ requests in [0-9.]+s' "$CAL_OUT" | head -1 | awk '{print $4}' | tr -d s)"
rm -f "$CAL_OUT"
if [ -z "${CAL_REQS:-}" ] || [ "$CAL_REQS" -lt 100 ]; then
    echo "[setup] ERROR: S5 calibration failed (requests=${CAL_REQS:-none}) — check s5 lua + pool"
    exit 1
fi
CAL_QPS="$(python3 -c "print(round($CAL_REQS / max($CAL_SECS, 0.001)))")"
echo "$CAL_QPS" > "$GEN_DIR/s5_calibration_qps.txt"
echo "[setup] S5 calibration: $CAL_REQS reqs in ${CAL_SECS}s -> $CAL_QPS QPS @ c=8"

# initial full RT pool for smoke tests (the staircase reissues per level)
INIT_RT=$(( CAL_QPS * 20 ))
echo "[setup] minting initial RT pool ($INIT_RT)..."
python3 "$KC_DIR/mint_tokens.py" --url "$KC_URL" --realm "$KC_REALM" \
    --client-id "$CLIENT_ID" --client-secret "$CLIENT_SECRET" \
    --grant password --username "$BENCH_USER" --password "$BENCH_PASS" \
    --scope openid --extract refresh_token --count "$INIT_RT" --parallel "$MINT_PARALLEL" \
    > "$GEN_DIR/refresh_tokens.txt"

# --- 8. final self-checks (AC-M1.2b) ---
for f in cc_tokens.txt user_tokens.txt refresh_tokens.txt s5_calibration_qps.txt; do
    LINES=$(wc -l < "$GEN_DIR/$f")
    echo "[setup] pool $f: $LINES lines"
    [ "$LINES" -gt 0 ] || { echo "[setup] ERROR: empty pool $f"; exit 1; }
done
[ "$(wc -l < "$GEN_DIR/cc_tokens.txt")" -ge "$POOL_CC" ] \
    || { echo "[setup] ERROR: cc pool short of $POOL_CC"; exit 1; }
[ "$(wc -l < "$GEN_DIR/user_tokens.txt")" -ge "$POOL_USER" ] \
    || { echo "[setup] ERROR: user pool short of $POOL_USER"; exit 1; }

echo "$KC_VERSION" > "$GEN_DIR/product_version.txt"
echo "[setup] DONE. Pools + calibration in $GEN_DIR"
