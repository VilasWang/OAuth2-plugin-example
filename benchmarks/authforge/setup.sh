#!/usr/bin/env bash
# benchmarks/authforge/setup.sh
#
# Boot the AuthForge full stack (postgres + redis + backend) as the benchmark
# target, then gate on /health/ready and validate that the seed data the S1/S2
# scenarios depend on is reachable. Part of benchmark-facility-design.md M1.
#
# Reuses: deploy/docker/docker-compose.yml (backend:5555 + PG15 + Redis7 +
# Prometheus, OAUTH2_AUTO_MIGRATE=true), paths.env (path single source of
# truth). The seed/*.sql files (including bench_users.sql) are auto-injected by
# the postgres entrypoint on a fresh pgdata volume.
#
# Usage:
#   bash benchmarks/authforge/setup.sh
#
# Env overrides:
#   TARGET_URL   default http://127.0.0.1:5555
#   KEEP_VOLUME  =1 to skip the clean-volume reset (re-run on existing stack)
set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$BENCH_DIR/../.." && pwd)"
TARGET_URL="${TARGET_URL:-http://127.0.0.1:5555}"

# --- source paths.env (same pattern as manage.sh) ---
PATHS_ENV_FILE="$REPO_ROOT/paths.env"
if [ ! -f "$PATHS_ENV_FILE" ]; then
    echo "[setup] ERROR: paths.env not found at $PATHS_ENV_FILE"
    exit 1
fi
set -a
# shellcheck disable=SC1090
source "$PATHS_ENV_FILE"
set +a
COMPOSE_FILE_ABS="$REPO_ROOT/$COMPOSE_FILE_REL"

# cd to repo root before any docker compose call: build-context relative paths
# in the compose file (e.g. context: ../..) resolve against the CWD, not against
# --project-directory, so a wrong CWD makes buildx bake look in the wrong place
# (e.g. lstat /home/<user>/deploy). manage.sh does the same (cd "$SCRIPT_DIR").
cd "$REPO_ROOT"

echo "[setup] compose file: $COMPOSE_FILE_ABS"

# --- clean volume for determinism (skip if KEEP_VOLUME=1) ---
if [ "${KEEP_VOLUME:-0}" != "1" ]; then
    echo "[setup] resetting volumes (docker compose down -v) for schema/seed determinism..."
    docker compose -f "$COMPOSE_FILE_ABS" --project-directory "$REPO_ROOT" down -v \
        --remove-orphans >/dev/null 2>&1 || true
fi

# --- boot the benchmark target (backend + postgres + redis only) ---
# Only the backend, its PG and Redis deps are load-tested. The admin/frontend
# SPAs and Prometheus are irrelevant to backend throughput (design D1/N4) and
# are deliberately NOT started — this also avoids building their (slow, separate)
# images. Same service-subset pattern as scripts/backend/full-test-docker.sh:55.
echo "[setup] bringing up backend + postgres + redis (docker compose up -d)..."
docker compose -f "$COMPOSE_FILE_ABS" --project-directory "$REPO_ROOT" up -d \
    oauth2-postgres oauth2-redis oauth2-backend

# --- poll /health/ready until 200 or timeout ---
# postgres storage mode requires DB + Redis both reachable for /health/ready
# to return 200 (HealthController.cc:70-170). This is the real readiness gate.
echo "[setup] waiting for $TARGET_URL/health/ready ..."
READY=0
for i in $(seq 1 60); do
    CODE="$(curl -s -o /dev/null -w '%{http_code}' "$TARGET_URL/health/ready" 2>/dev/null || echo 000)"
    if [ "$CODE" = "200" ]; then
        READY=1
        echo "[setup] ready after ${i} polls (~$((i*2))s)"
        break
    fi
    sleep 2
done
if [ "$READY" != "1" ]; then
    echo "[setup] ERROR: target did not become ready within ~120s (last code: $CODE)"
    echo "[setup] dumping backend logs (last 40 lines):"
    docker compose -f "$COMPOSE_FILE_ABS" --project-directory "$REPO_ROOT" logs --tail=40 oauth2-backend 2>/dev/null || true
    exit 1
fi

# --- apply seed data (NOT auto-loaded by the stack) ---
# IMPORTANT: the docker-compose stack does NOT auto-seed. The postgres entrypoint
# mounts seed/*.sql under initdb.d/seed/, but postgres does NOT recurse into
# subdirectories (compose comment docker-compose.yml:79-81 calls this a no-op),
# and the app's MigrationRunner (OAUTH2_AUTO_MIGRATE=true) runs schema migrations
# ONLY, never seed. Seed data (backend-svc client, bench users, etc.) must be
# applied explicitly via psql — same pattern as scripts/backend/setup-database.sh
# and deploy/docker/docker-quick-verify-debug.sh:62-64.
#
# Race note: MigrationRunner runs migrations on a detached thread with a 500ms
# startup delay (MigrationRunner.cc:82-91), so /health/ready returning 200 only
# proves the DB is reachable, NOT that migrations finished. Seed SQL targets
# migration-created tables (oauth2_clients, users, ...), so we retry the whole
# seed bundle until it applies cleanly. All seed files are idempotent
# (ON CONFLICT DO NOTHING), so retries are safe.
SEED_DIR_HOST="$REPO_ROOT/$OAUTH2_SERVER_DIR/seed"
echo "[setup] applying seed SQL from $SEED_DIR_HOST ..."
PG_CONTAINER="$(docker compose -f "$COMPOSE_FILE_ABS" --project-directory "$REPO_ROOT" ps -q oauth2-postgres 2>/dev/null || true)"
if [ -z "$PG_CONTAINER" ]; then
    echo "[setup] ERROR: could not resolve the postgres container to apply seed."
    exit 1
fi
SEED_APPLIED=0
for attempt in $(seq 1 30); do
    SEED_FAIL=0
    for seed_sql in "$SEED_DIR_HOST"/*.sql; do
        fname="$(basename "$seed_sql")"
        if ! docker exec -i "$PG_CONTAINER" \
            psql -U oauth2_user -d oauth2_db -v ON_ERROR_STOP=1 -q \
            < "$seed_sql" >/dev/null 2>&1; then
            SEED_FAIL=$((SEED_FAIL+1))
        fi
    done
    if [ "$SEED_FAIL" -eq 0 ]; then
        SEED_APPLIED=1
        echo "[setup] seed applied on attempt $attempt ($(ls "$SEED_DIR_HOST"/*.sql | wc -l) files)"
        break
    fi
    # migrations not finished yet — wait and retry
    sleep 2
done
if [ "$SEED_APPLIED" != "1" ]; then
    echo "[setup] ERROR: seed did not apply cleanly within ~60s ($SEED_FAIL file(s) failing; likely migrations unfinished)."
    echo "[setup] dumping backend migration logs (last 30 lines):"
    docker compose -f "$COMPOSE_FILE_ABS" --project-directory "$REPO_ROOT" logs --tail=30 oauth2-backend 2>/dev/null || true
    exit 1
fi

# --- seed validation: confirm S2 client_credentials is reachable ---
# backend-svc is seeded by dev_backend_client.sql (applied above). A 200 +
# access_token proves the token endpoint + RS256 signing + postgres client
# lookup are all wired.
echo "[setup] validating seed: POST /oauth2/token (client_credentials, backend-svc)..."
SEED_RESP="$(curl -s -X POST "$TARGET_URL/oauth2/token" \
    -H "Content-Type: application/x-www-form-urlencoded" \
    -d "grant_type=client_credentials&client_id=backend-svc&client_secret=test-secret&scope=read" 2>/dev/null || true)"
AT="$(python3 -c "import sys,json; print(json.loads(sys.argv[1]).get('access_token',''))" "$SEED_RESP" 2>/dev/null || true)"
if [ -z "$AT" ]; then
    echo "[setup] ERROR: seed validation failed — no access_token in response:"
    echo "        $SEED_RESP"
    exit 1
fi
echo "[setup] seed OK: client_credentials token issued (len=${#AT})"

# --- discovery endpoint smoke (S1 dependency) ---
DISC_CODE="$(curl -s -o /dev/null -w '%{http_code}' "$TARGET_URL/.well-known/openid-configuration" 2>/dev/null || echo 000)"
if [ "$DISC_CODE" != "200" ]; then
    echo "[setup] WARN: /.well-known/openid-configuration returned $DISC_CODE (S1 may fail)"
fi
JWKS_CODE="$(curl -s -o /dev/null -w '%{http_code}' "$TARGET_URL/.well-known/jwks.json" 2>/dev/null || echo 000)"
if [ "$JWKS_CODE" != "200" ]; then
    echo "[setup] WARN: /.well-known/jwks.json returned $JWKS_CODE (S1 may fail)"
fi

# --- bench_users.sql warmup-rehash hook (defined, not invoked by M1) ---
# M1 scenarios (S1 discovery, S2 client_credentials) do not consume the bench
# users, but the seed is in place so M2's auth_code scenario can use them
# without a schema change. The first login of each legacy-hash user triggers a
# PBKDF2 rehash (AuthService.cc:94-122); M2's setup will call warmup_bench_users
# BEFORE timed runs so that CPU cost lands in warmup, not measured throughput.
warmup_bench_users() {
    local n="${1:-512}"
    echo "[setup] warming up $n bench users (PBKDF2 rehash, first login)..."
    local ok=0 fail=0
    for i in $(seq 0 $((n-1))); do
        local uname
        uname="$(printf 'bench_user_%04d' "$i")"
        if curl -s -o /dev/null -X POST "$TARGET_URL/oauth2/login" \
            -d "username=${uname}&password=admin&client_id=vue-client&redirect_uri=http://127.0.0.1:5173/callback&scope=openid+profile&state=warmup-${i}&json=true" \
            2>/dev/null; then
            ok=$((ok+1))
        else
            fail=$((fail+1))
        fi
    done
    echo "[setup] warmup: $ok ok, $fail failed"
}

echo "[setup] done. target=$TARGET_URL  (bench users seeded; run warmup_bench_users before M2 auth_code scenarios)"
