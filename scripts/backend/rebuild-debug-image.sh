#!/bin/bash
# rebuild-debug-image.sh - Rebuild the OAuth2 debug Docker image

set -e

# Load common environment
source "$(dirname "$0")/env_common.sh"

echo "========================================"
echo "Rebuilding OAuth2 Backend Dev Image"
echo "========================================"
echo ""
echo "This will take a few minutes..."
echo ""

# Build the image using the unified Dockerfile and the backend-dev target.
# The Dockerfile was relocated to deploy/docker/ (repo-structure-refactor); use
# -f to point at it while keeping the repo root ("." / $PROJECT_DIR) as the
# build context so COPY paths still resolve.
cd "$PROJECT_DIR"
docker build --no-cache -f "$DOCKERFILE" --target backend-dev -t fulla-backend-debug:v1.9.13 .

echo ""
echo "========================================"
echo "Build completed!"
echo "========================================"
echo ""
echo "Verifying installation..."
docker run --rm fulla-backend-debug:v1.9.13 bash -c "
  echo 'Checking Drogon files:'
  # Note: Drogon is installed in /usr/local in the Dockerfile
  ls -la /usr/local/include/drogon/drogon.h && echo '  [PASS] Headers found'
  echo ''
  echo 'Drogon dev environment ready!'
"
