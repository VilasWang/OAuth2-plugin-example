package auth

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"fmt"
)

// CreateVerifier returns a new RFC 7636 section 4.1 code verifier
// (64 url-safe base64 characters, 384 bits of entropy).
func CreateVerifier() (string, error) {
	buf := make([]byte, 48)
	if _, err := rand.Read(buf); err != nil {
		return "", fmt.Errorf("fulla: reading random bytes for PKCE verifier: %w", err)
	}
	return base64.RawURLEncoding.EncodeToString(buf), nil
}

// CreateChallenge derives the S256 code challenge (RFC 7636 section 4.2):
// BASE64URL(SHA256(ASCII(verifier))) without padding.
func CreateChallenge(verifier string) string {
	digest := sha256.Sum256([]byte(verifier))
	return base64.RawURLEncoding.EncodeToString(digest[:])
}

// PkcePair bundles a generated verifier with its S256 challenge.
type PkcePair struct {
	Verifier  string
	Challenge string
}

// NewPkcePair generates a fresh verifier/challenge pair.
func NewPkcePair() (PkcePair, error) {
	verifier, err := CreateVerifier()
	if err != nil {
		return PkcePair{}, err
	}
	return PkcePair{Verifier: verifier, Challenge: CreateChallenge(verifier)}, nil
}
