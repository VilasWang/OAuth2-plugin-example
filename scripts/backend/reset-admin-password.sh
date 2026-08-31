#!/usr/bin/env bash
# reset-admin-password.sh - Reset admin password to default 'admin' (Linux/macOS)
set -euo pipefail

source "$(dirname "$0")/env_common.sh"
source "$(dirname "$0")/common-test-functions.sh"

echo "========================================"
echo "Reset Admin Password to Default"
echo "========================================"
echo ""

DEFAULT_HASH='$pbkdf2-sha256$310000$61646d696e5f736565645f73616c74$6c0307305e1390e1214b15f1f4d0250b2de86aa0e8aa0e008e5cca03084d3d62'
DEFAULT_SALT=""

QUERY="UPDATE users SET password_hash = '$DEFAULT_HASH', salt = '$DEFAULT_SALT', failed_login_count = 0, locked_until = 0 WHERE username = 'admin';"

if run_psql "$QUERY" >/dev/null 2>&1; then
    echo "Admin password reset successfully"
else
    echo "[Error] Failed to reset admin password"
    exit 1
fi

# Verify
CHECK_QUERY="SELECT username, LEFT(password_hash, 20) as hash_prefix, salt, failed_login_count, locked_until FROM users WHERE username = 'admin';"
echo ""
echo "Current status:"
run_psql "$CHECK_QUERY" || true

echo ""
echo "Admin password reset completed successfully"
echo "Username: admin"
echo "Password: admin"
echo ""
echo "WARNING: This is a development-only password. DO NOT use in production!"
