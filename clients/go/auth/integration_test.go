//go:build integration

// Integration tests against a running AuthForge full stack (matrix I1-I5).
// Skipped unless AUTHFORGE_BASE_URL is set:
//
//	go test -tags integration ./...
//
// Boot recipe: see docs/productization-evolution/in-progress/client-sdk-implementation-plan.md
// section 4 (setup_database + start authforge-server + poll /health/live).
package auth

import (
	"context"
	"encoding/json"
	"net/url"
	"os"
	"strings"
	"testing"
)

var (
	baseURL      = os.Getenv("AUTHFORGE_BASE_URL")
	clientID     = envOr("AUTHFORGE_CLIENT_ID", "backend-svc")
	clientSecret = envOr("AUTHFORGE_CLIENT_SECRET", "test-secret")
)

func envOr(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

func requireServer(t *testing.T) {
	t.Helper()
	if baseURL == "" {
		t.Skip("AUTHFORGE_BASE_URL not set; integration tests need a running stack")
	}
}

// I1 -- client_credentials token fetch.
func TestI1ClientCredentialsToken(t *testing.T) {
	requireServer(t)
	token, err := FetchClientCredentialsToken(context.Background(), baseURL, clientID, clientSecret, []string{"tokens:read"})
	if err != nil {
		t.Fatalf("fetch: %v", err)
	}
	if token.AccessToken == "" || token.Expiry.IsZero() {
		t.Fatalf("bad token: %+v", token)
	}
}

// I2 -- freshly minted token introspects as active via the generated client.
func TestI2IntrospectOwnToken(t *testing.T) {
	requireServer(t)
	ctx := context.Background()
	token, err := FetchClientCredentialsToken(ctx, baseURL, clientID, clientSecret, []string{"tokens:read"})
	if err != nil {
		t.Fatalf("fetch: %v", err)
	}
	introspectClient, err := NewBasicAuthClient(baseURL, clientID, clientSecret)
	if err != nil {
		t.Fatalf("introspect client: %v", err)
	}
	form := url.Values{"token": {token.AccessToken}}
	resp, err := introspectClient.PostOauth2IntrospectWithBody(ctx, "application/x-www-form-urlencoded",
		strings.NewReader(form.Encode()))
	if err != nil {
		t.Fatalf("introspect: %v", err)
	}
	defer resp.Body.Close()
	var result struct {
		Active   bool   `json:"active"`
		ClientID string `json:"client_id"`
		Scope    string `json:"scope"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		t.Fatalf("decode: %v", err)
	}
	if !result.Active || result.ClientID != clientID || result.Scope != "tokens:read" {
		t.Fatalf("introspection = %+v", result)
	}
}

// I3 -- discovery document is served (issuer is the hardcoded fallback
// http://localhost:5555 unless metadata.issuer is configured).
func TestI3Discovery(t *testing.T) {
	requireServer(t)
	ctx := context.Background()
	client, err := NewBasicAuthClient(baseURL, clientID, clientSecret)
	if err != nil {
		t.Fatalf("client: %v", err)
	}
	resp, err := client.GetWellKnownOpenidConfigurationWithResponse(ctx)
	if err != nil {
		t.Fatalf("discovery: %v", err)
	}
	if resp.StatusCode() != 200 {
		t.Fatalf("status = %d", resp.StatusCode())
	}
	if doc := resp.JSON200; doc == nil || doc.Issuer == nil || *doc.Issuer == "" {
		t.Fatal("no discovery body")
	} else if !strings.HasSuffix(*doc.Issuer, ":5555") {
		t.Fatalf("issuer = %q", *doc.Issuer)
	}
}

// I4 -- M2M tokens have no user identity: userinfo must 401.
func TestI4M2MUserinfoRejected(t *testing.T) {
	requireServer(t)
	ctx := context.Background()
	client, err := NewM2MClient(ctx, baseURL, clientID, clientSecret, []string{"tokens:read"})
	if err != nil {
		t.Fatalf("client: %v", err)
	}
	resp, err := client.GetOauth2Userinfo(ctx)
	if err != nil {
		t.Fatalf("userinfo: %v", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 401 {
		t.Fatalf("status = %d, want 401", resp.StatusCode)
	}
	var body struct {
		Error string `json:"error"`
	}
	_ = json.NewDecoder(resp.Body).Decode(&body)
	if body.Error != "invalid_token" {
		t.Fatalf("error = %q", body.Error)
	}
}

// I5 -- bad credentials surface as *AuthError(invalid_client).
func TestI5WrongSecret(t *testing.T) {
	requireServer(t)
	_, err := FetchClientCredentialsToken(context.Background(), baseURL, clientID, "definitely-wrong", nil)
	authErr, ok := err.(*AuthError)
	if !ok {
		t.Fatalf("error type = %T, want *AuthError", err)
	}
	if authErr.ErrorCode != "invalid_client" || authErr.StatusCode != 401 {
		t.Fatalf("auth error = %+v", authErr)
	}
}
