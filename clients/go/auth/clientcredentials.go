// Package auth provides the handwritten authentication layer for the
// Fulla Go client (the generated API surface lives in the sibling
// package github.com/voidvec/fulla/clients/go/generated).
//
// Token lifecycle is deliberately handwritten, never generated (client-sdk
// design D5): refresh rotation, lockout and family revocation are too
// sensitive to template.
//
// Confidential client credentials always travel as HTTP Basic:
// token_endpoint_auth_method=client_secret_basic; the server rejects the
// body form for such clients (F-017).
package auth

import (
	"context"
	"fmt"
	"net/http"
	"strings"

	"golang.org/x/oauth2"
	"golang.org/x/oauth2/clientcredentials"

	"github.com/voidvec/fulla/clients/go/generated"
)

// TokenURLPath and AuthorizeURLPath are the stable request shapes this
// package depends on (never the generated code's internals).
const (
	TokenURLPath     = "/oauth2/token"
	AuthorizeURLPath = "/oauth2/authorize"
)

// NormalizeBaseURL strips trailing slashes so endpoint concatenation is safe.
func NormalizeBaseURL(baseURL string) string {
	return strings.TrimRight(baseURL, "/")
}

// NewM2MClient returns a generated client whose every request carries a
// valid client_credentials Bearer token. Tokens are fetched lazily, cached,
// refreshed ahead of expiry and across concurrent requests (all handled by
// golang.org/x/oauth2 clientcredentials -- deliberately not reinvented).
func NewM2MClient(ctx context.Context, baseURL, clientID, clientSecret string, scopes []string) (*generated.ClientWithResponses, error) {
	base := NormalizeBaseURL(baseURL)
	cfg := clientcredentials.Config{
		ClientID:     clientID,
		ClientSecret: clientSecret,
		TokenURL:     base + TokenURLPath,
		Scopes:       scopes,
		// F-017: the server requires client_secret_basic and rejects
		// credentials in the request body.
		AuthStyle: oauth2.AuthStyleInHeader,
	}
	return generated.NewClientWithResponses(base, generated.WithHTTPClient(cfg.Client(ctx)))
}

// FetchClientCredentialsToken performs a one-shot client_credentials token
// fetch (no caching). Useful for scripts and benchmarks; long-lived callers
// should prefer NewM2MClient.
func FetchClientCredentialsToken(ctx context.Context, baseURL, clientID, clientSecret string, scopes []string) (*oauth2.Token, error) {
	cfg := clientcredentials.Config{
		ClientID:     clientID,
		ClientSecret: clientSecret,
		TokenURL:     NormalizeBaseURL(baseURL) + TokenURLPath,
		Scopes:       scopes,
		AuthStyle:    oauth2.AuthStyleInHeader,
	}
	token, err := cfg.Token(ctx)
	if err != nil {
		return nil, asAuthError(err)
	}
	return token, nil
}

// basicAuthTransport stamps HTTP Basic client credentials on every request.
type basicAuthTransport struct {
	base     http.RoundTripper
	clientID string
	secret   string
}

func (t *basicAuthTransport) RoundTrip(req *http.Request) (*http.Response, error) {
	clone := req.Clone(req.Context())
	clone.SetBasicAuth(t.clientID, t.secret)
	base := t.base
	if base == nil {
		base = http.DefaultTransport
	}
	return base.RoundTrip(clone)
}

// NewBasicAuthClient returns a generated client whose every request carries
// HTTP Basic client credentials -- for endpoints that authenticate the
// CALLING CLIENT rather than a user bearer token (POST /oauth2/introspect
// RFC 7662 section 2.1, POST /oauth2/revoke), where credentials must travel
// via HTTP Basic (F-017; the body form is rejected).
func NewBasicAuthClient(baseURL, clientID, clientSecret string) (*generated.ClientWithResponses, error) {
	return generated.NewClientWithResponses(
		NormalizeBaseURL(baseURL),
		generated.WithHTTPClient(&http.Client{Transport: &basicAuthTransport{clientID: clientID, secret: clientSecret}}),
	)
}

// errInvalidBaseURL is returned for URLs the endpoint concatenation cannot
// use (empty).
func errInvalidBaseURL(baseURL string) error {
	return fmt.Errorf("fulla: invalid base URL %q", baseURL)
}
