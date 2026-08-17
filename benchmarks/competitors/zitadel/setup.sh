#!/usr/bin/env bash
# benchmarks/competitors/zitadel/setup.sh
#
# Boot Zitadel (official production image + PostgreSQL) via the documented
# `start-from-init` bootstrap (init -> setup -> serve in one process;
# start-from-setup requires an already-initialized DB), rendering
# setup-steps.yaml with runtime credentials. The FirstInstance.Machine block
# bootstraps a Service User with a JSON key (JWT profile — Zitadel's official
# M2M path) and a PAT without touching the login UI.
#
# Config sources (D2, fairness-annotated in COMPARISON.md):
#   * compose:   https://zitadel.com/docs/self-hosting/deploy/compose
#   * service users / JWT profile: https://zitadel.com/docs/guides/integrate/service-users
#
# Scenario coverage decisions (design D3/M2 + decision gates DG-2):
#   * S1 discovery     — plain GET
#   * S2 cc            — RFC 7523 jwt-bearer GRANT with a PRE-SIGNED
#                        assertion (wrk Lua cannot sign; assertion minted
#                        here, exp covers the session). Zitadel's official
#                        M2M path; the token endpoint does NOT accept
#                        client_credentials + client_assertion for machine
#                        users — annotated in COMPARISON.md
#   * S3 introspect    — OIDC app with PRIVATE_KEY_JWT auth (app key via
#                        Management API; client_assertion per request).
#                        Official perf guidance (#6220): secret-based auth
#                        hashes on every request — private key avoids it.
#                        Token pool minted with the app's project-aud scope
#                        (required for service-user tokens to introspect)
#   * S5               — N/A: machine users get no refresh tokens (RFC 6749
#                        §4.4.3) and Zitadel removed the password grant
#   * S6 userinfo      — service-user tokens ARE accepted at userinfo
#                        (verified) — smoke-gated at run time anyway
#
# Version: v4.17.1 (current stable line 2026-08; same generation as Keycloak
# 26.7 / Hydra 26.2 — benchmarking the superseded v2.x would misrepresent
# Zitadel). FirstInstance.Features.ImprovedPerformance enables all five
# officially documented perf flags, matching Zitadel's own v4 benchmark
# baseline (benchmarks docs, see setup-steps.yaml).
#
# Usage:
#   bash benchmarks/competitors/zitadel/setup.sh
#
# Env overrides:
#   ZITADEL_URL   default http://localhost:8080 (host MUST equal ExternalDomain
#                 — JWT-profile assertion aud == issuer)
#   POOL_CC       default 2000 (S3 introspect pool)
set -euo pipefail

ZA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ZITADEL_URL="${ZITADEL_URL:-http://localhost:8080}"
POOL_CC="${POOL_CC:-2000}"
GEN_DIR="$ZA_DIR/lib/generated"
mkdir -p "$GEN_DIR"

# runtime-generated throwaway credentials
export ZITADEL_MASTERKEY="$(head -c 32 /dev/urandom | base64 | tr -d '/+=' | head -c 32)"
# The admin password must satisfy the default complexity policy
# (upper+lower+digit+symbol) — 03_default_instance otherwise fails with
# Errors.User.PasswordComplexityPolicy.HasSymbol. Fixed prefix/suffix chars
# also avoid sed-special bytes in the render step below.
ADMIN_PASS="B!$(head -c 18 /dev/urandom | base64 | tr -d '/+=' | head -c 16)aZ9"

# --- 0a. residue guard: a STOPPED stack with stale volumes boots fine but
# silently skips FirstInstance (instance already in the DB) and produces no
# machinekey — always start from a clean slate (idempotency, AC-M2.3).
if [ -n "$(docker ps -aq --filter 'name=zitadel-bench-' 2>/dev/null)" ] \
   || [ -n "$(docker volume ls -q --filter 'name=zitadel-bench-' 2>/dev/null)" ]; then
    echo "[setup] residue from a previous run found — tearing down first"
    bash "$ZA_DIR/teardown.sh"
fi

# --- 0b. port-free assertion (something FOREIGN may still hold the port) ---
if curl -s -o /dev/null --max-time 2 "$ZITADEL_URL/debug/healthz" 2>/dev/null; then
    echo "[setup] ERROR: something already answers at $ZITADEL_URL — run teardown.sh first."
    exit 1
fi

# --- 1. render setup-steps.yaml ---
PAT_EXPIRY="$(date -u -d '+48 hours' +%Y-%m-%dT%H:%M:%SZ)"
sed -e "s|__ADMIN_PASS__|$ADMIN_PASS|g" -e "s|__ADMIN_USER__|bench-admin|g" \
    -e "s|__PAT_EXPIRY__|$PAT_EXPIRY|g" \
    "$ZA_DIR/setup-steps.yaml" > "$ZA_DIR/setup-steps.rendered.yaml"

# machinekey bind mount: must exist BEFORE `up` (compose would create it as
# root) and be world-writable — the container user is non-root and the
# FirstInstance step writes the machine key + PAT here. Throwaway keys,
# gitignored (benchmarks/competitors/.gitignore). A root-owned leftover from
# a previous aborted run is removable because its parent dir is ours.
mkdir -p "$ZA_DIR/machinekey"
chmod 0777 "$ZA_DIR/machinekey" 2>/dev/null \
    || { rm -rf "$ZA_DIR/machinekey" && mkdir -p "$ZA_DIR/machinekey" && chmod 0777 "$ZA_DIR/machinekey"; }

echo "[setup] booting zitadel + postgres (start-from-init; first boot ~1-2 min)..."
docker compose -f "$ZA_DIR/docker-compose.yml" -p zitadel-bench up -d --wait --wait-timeout 600

# --- 2. health gate ---
CODE=000
for i in $(seq 1 120); do
    CODE="$(curl -s -o /dev/null -w '%{http_code}' "$ZITADEL_URL/debug/healthz" 2>/dev/null || echo 000)"
    [ "$CODE" = "200" ] && break
    sleep 1
done
[ "$CODE" = "200" ] || { echo "[setup] ERROR: zitadel not ready (code=$CODE)"; docker logs zitadel-bench-zitadel 2>&1 | tail -30; exit 1; }
# /debug/healthz returns plain "ok" in v2.71 (no version JSON) — take the
# version from the startup banner, falling back to the pinned image tag.
ZA_VERSION="$(docker logs zitadel-bench-zitadel 2>&1 \
    | grep -oE 'Version[[:space:]]+:[[:space:]]+v?[0-9][0-9A-Za-z._-]*' \
    | head -1 | awk '{print $NF}' || true)"
[ -n "$ZA_VERSION" ] || ZA_VERSION="$(docker inspect zitadel-bench-zitadel \
    --format '{{.Config.Image}}' 2>/dev/null | sed 's|.*zitadel:||' || true)"
[ -n "$ZA_VERSION" ] || ZA_VERSION="unknown"
echo "[setup] zitadel ready (version: $ZA_VERSION)"

# --- 3. machine key + PAT: prefer the MachineKeyPath/PatPath files (v4,
# written into the world-writable ./machinekey bind mount), fall back to
# scraping the setup stdout (v2.x behavior) ---
docker cp zitadel-bench-zitadel:/machinekey/bench-svc.json "$GEN_DIR/machinekey.json" 2>/dev/null || true
docker cp zitadel-bench-zitadel:/machinekey/bench-svc.pat "$GEN_DIR/pat.txt" 2>/dev/null || true
if [ ! -s "$GEN_DIR/machinekey.json" ]; then
    docker logs zitadel-bench-zitadel 2>&1 \
        | grep -oE '\{"type":"serviceaccount".*\}' | head -1 \
        | python3 -c 'import json,sys; d=json.load(sys.stdin); d["id"]=d.get("keyId",""); print(json.dumps(d))' \
        > "$GEN_DIR/machinekey.json"
fi
[ -s "$GEN_DIR/machinekey.json" ] || { echo "[setup] ERROR: machine key not found"; exit 1; }
# normalize keyId -> id (mint_tokens.py expects the Management-API shape)
python3 - "$GEN_DIR/machinekey.json" <<'EOF'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d.setdefault("id", d.get("keyId", ""))
json.dump(d, open(p, "w"))
EOF
chmod 600 "$GEN_DIR/machinekey.json"
if [ ! -s "$GEN_DIR/pat.txt" ]; then
    # PAT = first standalone base64url line right after the machine-key JSON
    docker logs zitadel-bench-zitadel 2>&1 | grep -A2 'serviceaccount' \
        | grep -oE '^[A-Za-z0-9_-]{40,}$' | head -1 > "$GEN_DIR/pat.txt"
fi
[ -s "$GEN_DIR/pat.txt" ] || { echo "[setup] ERROR: PAT not found"; exit 1; }
chmod 600 "$GEN_DIR/pat.txt"
echo "[setup] machine key + PAT extracted"

# --- 3b. introspection client: OIDC app (PRIVATE_KEY_JWT) via the Management API ---
# Zitadel's introspection endpoint authenticates OIDC/API APPS, not bare
# service users. Official performance guidance
# (github.com/zitadel/zitadel/discussions/6220): secret-based auth hashes the
# secret on EVERY request — use private-key JWT instead (no hashing).
# The PAT from FirstInstance.Machine bootstraps Management API access.
# Retry: right after first boot the management projections may lag a few
# seconds and the create call can return a transient error object.
mgmt() {  # <method> <path> <json-body> -> response JSON
    curl -s -X "$1" -H "Authorization: Bearer $(cat "$GEN_DIR/pat.txt")" \
        -H 'Content-Type: application/json' ${3:+-d "$3"} "$ZITADEL_URL$2"
}
PROJ_ID=""
for attempt in 1 2 3 4 5; do
    PROJ_ID="$(mgmt POST /management/v1/projects/_search \
        '{"queries":[{"nameQuery":{"name":"bench-s3","method":"TEXT_QUERY_METHOD_EQUALS"}}]}' \
        | python3 -c 'import json,sys; r=json.load(sys.stdin).get("result") or []; print(r[0]["id"] if r else "")')"
    if [ -z "$PROJ_ID" ]; then
        PROJ_ID="$(mgmt POST /management/v1/projects '{"name":"bench-s3"}' \
            | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d.get("id") or "")')"
    fi
    [ -n "$PROJ_ID" ] && break
    echo "[setup] management API not ready (attempt $attempt), retrying in 3s..."
    sleep 3
done
[ -n "$PROJ_ID" ] || { echo "[setup] ERROR: could not create/find bench project"; exit 1; }
echo "$PROJ_ID" > "$GEN_DIR/project_id.txt"
APP_JSON=""
for attempt in 1 2 3; do
    APP_JSON="$(mgmt POST "/management/v1/projects/$PROJ_ID/apps/oidc" \
        '{"name":"bench-introspect","authMethodType":"OIDC_AUTH_METHOD_TYPE_PRIVATE_KEY_JWT","appType":"OIDC_APP_TYPE_WEB","devMode":true,"accessTokenType":"OIDC_TOKEN_TYPE_BEARER","responseTypes":["OIDC_RESPONSE_TYPE_CODE"],"grantTypes":["OIDC_GRANT_TYPE_AUTHORIZATION_CODE"]}')"
    CLIENT_ID="$(echo "$APP_JSON" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("clientId",""))')"
    APP_ID="$(echo "$APP_JSON" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("appId",""))')"
    [ -n "$CLIENT_ID" ] && [ -n "$APP_ID" ] && break
    sleep 3
done
[ -n "$CLIENT_ID" ] && [ -n "$APP_ID" ] \
    || { echo "[setup] ERROR: OIDC app creation failed: $APP_JSON"; exit 1; }
KEY_JSON=""
for attempt in 1 2 3; do
    KEY_JSON="$(mgmt POST "/management/v1/projects/$PROJ_ID/apps/$APP_ID/keys" \
        '{"type":"KEY_TYPE_JSON"}')"
    echo "$KEY_JSON" | grep -q 'keyDetails' && break
    sleep 3
done
echo "$KEY_JSON" | grep -q 'keyDetails' \
    || { echo "[setup] ERROR: app key creation failed: $KEY_JSON"; exit 1; }
# v4 returns {"id":..., "keyDetails": "<base64 JSON {type,keyId,key,appId,clientId}>"}
echo "$KEY_JSON" | python3 -c '
import json, sys, base64
outer = json.load(sys.stdin)
d = json.loads(base64.b64decode(outer["keyDetails"]))
d.setdefault("id", d.get("keyId", "") or outer.get("id", ""))
json.dump(d, open(sys.argv[1], "w"))' "$GEN_DIR/introspect_appkey.json"
chmod 600 "$GEN_DIR/introspect_appkey.json"
echo "$CLIENT_ID" > "$GEN_DIR/introspect_client_id.txt"
# introspect assertion: iss=sub=app clientId, kid=app key id (TTL 50min — the
# verifier rejects assertions issued more than 1h ago; run-all finishes well
# inside the window)
python3 "$ZA_DIR/mint_tokens.py" --issuer "$ZITADEL_URL" \
    --key "$GEN_DIR/introspect_appkey.json" --mode assertion --count 1 \
    --login-name "$CLIENT_ID" --assertion-ttl 3000 \
    > "$GEN_DIR/introspect_assertion.txt"
[ -s "$GEN_DIR/introspect_assertion.txt" ] \
    || { echo "[setup] ERROR: introspection assertion mint failed"; exit 1; }
echo "[setup] introspection client ready (project $PROJ_ID, OIDC app private-key-jwt)"

# --- 4. warm validation: jwt-bearer grant through the pipeline ---
AUD_SCOPE="urn:zitadel:iam:org:project:id:$PROJ_ID:aud"
python3 "$ZA_DIR/mint_tokens.py" --issuer "$ZITADEL_URL" \
    --key "$GEN_DIR/machinekey.json" --mode cc-token --count 1 \
    --scope "openid profile $AUD_SCOPE" \
    > "$GEN_DIR/warm_token.txt" 2>/tmp/za_mint_err || {
        cat /tmp/za_mint_err; echo "[setup] ERROR: warm jwt-bearer validation failed"; exit 1; }
# and the introspection chain end-to-end with that token (a bare HTTP 200
# is NOT enough — introspection reports {"active":false} with 200 when the
# token's audience doesn't cover the app's project)
INTRO_RES="$(curl -sf \
    -d "client_id=$(cat "$GEN_DIR/introspect_client_id.txt")" \
    -d "client_assertion_type=urn:ietf:params:oauth:client-assertion-type:jwt-bearer" \
    -d "client_assertion=$(cat "$GEN_DIR/introspect_assertion.txt")" \
    -d "token=$(cat "$GEN_DIR/warm_token.txt")" "$ZITADEL_URL/oauth/v2/introspect")" \
    || { echo "[setup] ERROR: warm introspection validation failed"; exit 1; }
echo "$INTRO_RES" | grep -q '"active":true' \
    || { echo "[setup] ERROR: introspection returned inactive: $INTRO_RES"; exit 1; }
echo "[setup] warm validation OK (jwt-bearer mint + private-key introspect active:true)"

# --- 5. assertion pools FIRST (the warm phase replays the S2 assertion; a
# stale file from a previous instance would fail with AuthNKey.NotFound
# against this boot). TTL 50min: the assertion verifiers reject tokens
# issued more than 1h ago; the whole session fits comfortably inside. ---
echo "[setup] minting S2 assertion pool (1 reusable assertion, exp 50min)..."
python3 "$ZA_DIR/mint_tokens.py" --issuer "$ZITADEL_URL" \
    --key "$GEN_DIR/machinekey.json" --mode assertion --count 1 --assertion-ttl 3000 \
    > "$GEN_DIR/assertion.txt"

# --- 6. warm phase (brief, Go runtime; also proves assertion replay) ---
echo "[setup] warm phase: 15s jwt-bearer token load..."
WRK_LIB_DIR="$ZA_DIR/lib" wrk -t2 -c16 -d15s \
    -s "$ZA_DIR/scenarios/s2-client-credentials.lua" "$ZITADEL_URL" >/dev/null 2>&1 || true

# --- 7. mint cc token pool (transient endpoint blips → top up the shortfall;
# mint exits non-zero on shortfall — tolerated here, the 90% self-check below
# is the real gate) ---
echo "[setup] minting cc token pool ($POOL_CC, S3 introspect / S6 userinfo)..."
python3 "$ZA_DIR/mint_tokens.py" --issuer "$ZITADEL_URL" \
    --key "$GEN_DIR/machinekey.json" --mode cc-token --count "$POOL_CC" \
    --scope "openid profile $AUD_SCOPE" \
    > "$GEN_DIR/cc_tokens.txt" || true
for _ in 1 2 3; do
    HAVE="$(wc -l < "$GEN_DIR/cc_tokens.txt")"
    [ "$HAVE" -ge "$POOL_CC" ] && break
    echo "[setup] pool short ($HAVE/$POOL_CC), topping up..."
    python3 "$ZA_DIR/mint_tokens.py" --issuer "$ZITADEL_URL" \
        --key "$GEN_DIR/machinekey.json" --mode cc-token --count "$((POOL_CC - HAVE))" \
        --scope "openid profile $AUD_SCOPE" \
        >> "$GEN_DIR/cc_tokens.txt" || true
    sleep 2
done

# --- 8. self-checks ---
for f in assertion.txt cc_tokens.txt machinekey.json pat.txt \
         introspect_appkey.json introspect_client_id.txt introspect_assertion.txt \
         project_id.txt; do
    [ -s "$GEN_DIR/$f" ] || { echo "[setup] ERROR: missing $f"; exit 1; }
done
HAVE_CC="$(wc -l < "$GEN_DIR/cc_tokens.txt")"
[ "$HAVE_CC" -ge "$((POOL_CC * 90 / 100))" ] \
    || { echo "[setup] ERROR: cc pool too small ($HAVE_CC < 90% of $POOL_CC)"; exit 1; }
# pool sanity: a pool token must introspect active and answer userinfo
# (catches scope/audience bugs that HTTP-200-with-active:false would hide)
POOL_CHK="$(curl -sf \
    -d "client_id=$(cat "$GEN_DIR/introspect_client_id.txt")" \
    -d "client_assertion_type=urn:ietf:params:oauth:client-assertion-type:jwt-bearer" \
    -d "client_assertion=$(cat "$GEN_DIR/introspect_assertion.txt")" \
    -d "token=$(head -1 "$GEN_DIR/cc_tokens.txt")" "$ZITADEL_URL/oauth/v2/introspect")" \
    || { echo "[setup] ERROR: pool token introspection failed"; exit 1; }
echo "$POOL_CHK" | grep -q '"active":true' \
    || { echo "[setup] ERROR: pool token inactive: $POOL_CHK"; exit 1; }
curl -sf -o /dev/null -H "Authorization: Bearer $(head -1 "$GEN_DIR/cc_tokens.txt")" \
    "$ZITADEL_URL/oidc/v1/userinfo" \
    || { echo "[setup] ERROR: pool token rejected at userinfo"; exit 1; }
echo "[setup] pool cc_tokens.txt: $(wc -l < "$GEN_DIR/cc_tokens.txt") lines (introspect+userinfo verified)"
echo "$ZA_VERSION" > "$GEN_DIR/product_version.txt"
echo "[setup] DONE."
