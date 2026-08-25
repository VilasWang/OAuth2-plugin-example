#!/bin/bash
# Generate RSA key pair for JWT signing
# The private key is used to sign tokens, the public key is exposed via JWKS

set -e

KEY_DIR="$(dirname "$0")/../deploy/keys"
mkdir -p "$KEY_DIR"

echo "Generating RSA-2048 key pair for JWT signing..."

# Generate private key
openssl genrsa -out "$KEY_DIR/signing.pem" 2048

# Extract public key
openssl rsa -in "$KEY_DIR/signing.pem" -pubout -out "$KEY_DIR/signing.pub.pem"

# Set restrictive permissions
chmod 600 "$KEY_DIR/signing.pem"
chmod 644 "$KEY_DIR/signing.pub.pem"

echo "JWT signing keys generated:"
echo "  Private: $KEY_DIR/signing.pem (keep secret!)"
echo "  Public:  $KEY_DIR/signing.pub.pem"
echo ""
echo "Set FULLA_JWT_KEY_PATH=$KEY_DIR/signing.pem in your environment."
