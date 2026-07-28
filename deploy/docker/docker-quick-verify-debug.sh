#!/bin/bash
# docker-quick-verify-debug.sh - Quick verification script for the debug environment

set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

MAX_RETRIES=30

echo -e "${GREEN}[0/4] Verifying environment...${NC}"
echo "Current User: $(whoami)"
echo "Working Dir: $(pwd)"
echo "OAUTH2_DB_HOST: $OAUTH2_DB_HOST"
echo "OAUTH2_REDIS_HOST: $OAUTH2_REDIS_HOST"

if [ -f "/usr/local/include/drogon/drogon.h" ]; then
    echo -e "✓ Drogon found (headers and library installed)"
else
    echo -e "${RED}✗ Drogon NOT found${NC}"
fi

echo -e "\n${GREEN}[1/4] Waiting for databases...${NC}"

# Wait for PostgreSQL with timeout
COUNT=0
until pg_isready -h "$OAUTH2_DB_HOST" -p "${OAUTH2_DB_PORT:-5432}" -U "$OAUTH2_DB_USER" > /dev/null 2>&1 || [ $COUNT -eq $MAX_RETRIES ]; do
    echo -e "${YELLOW}Waiting for PostgreSQL at $OAUTH2_DB_HOST ($COUNT/$MAX_RETRIES)...${NC}"
    sleep 1
    COUNT=$((COUNT + 1))
done

if [ $COUNT -eq $MAX_RETRIES ]; then
    echo -e "${RED}✗ PostgreSQL timeout.${NC}"
    exit 1
fi
echo -e "✓ PostgreSQL is ready"

# Wait for Redis with timeout
COUNT=0
until redis-cli -h "$OAUTH2_REDIS_HOST" -p "${OAUTH2_REDIS_PORT:-6379}" -a "$OAUTH2_REDIS_PASSWORD" ping > /dev/null 2>&1 || [ $COUNT -eq $MAX_RETRIES ]; do
    echo -e "${YELLOW}Waiting for Redis at $OAUTH2_REDIS_HOST ($COUNT/$MAX_RETRIES)...${NC}"
    sleep 1
    COUNT=$((COUNT + 1))
done

if [ $COUNT -eq $MAX_RETRIES ]; then
    echo -e "${RED}✗ Redis timeout.${NC}"
    exit 1
fi
echo -e "✓ Redis is ready"

echo -e "\n${GREEN}[2/4] Initializing database...${NC}"
export PGPASSWORD=$OAUTH2_DB_PASSWORD
# Apply all migrations in order
for migration in /app/sql/migrations/V*.sql; do
    psql -h "$OAUTH2_DB_HOST" -p "${OAUTH2_DB_PORT:-5432}" -U "$OAUTH2_DB_USER" -d "$OAUTH2_DB_NAME" -f "$migration" > /dev/null 2>&1
done
# Apply seed data
for seed in /app/sql/seed/*.sql; do
    psql -h "$OAUTH2_DB_HOST" -p "${OAUTH2_DB_PORT:-5432}" -U "$OAUTH2_DB_USER" -d "$OAUTH2_DB_NAME" -f "$seed" > /dev/null 2>&1
done
echo -e "✓ Database initialized (migrations + seed)"

echo -e "\n${GREEN}[3/4] Building Project...${NC}"
bash /app/scripts/backend/build.sh --debug

echo -e "\n${GREEN}[4/4] Running test...${NC}"
cd /app/build
ctest -V -C Debug --output-on-failure --timeout 120

echo -e "\n${GREEN}✅ SUCCESS: No crash during teardown!${NC}"
echo -e "The fix is working correctly."
