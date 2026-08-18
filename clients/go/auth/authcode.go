package auth

import (
	"context"
	"net/http"
	"net/url"
	"strings"
)

// TokenResponse is the token-endpoint success body (RFC 6749 section 5.1).
// Only the fields the authorization server actually emits are modelled;
// unknown fields are ignored.
type TokenResponse struct {
	AccessToken  string `json:"access_token"`
	TokenType    string `json:"token_type"`
	ExpiresIn    int    `json:"expires_in"`
	RefreshToken string `json:"refresh_token,omitempty"`
	Scope        string `json:"scope,omitempty"`
	IDToken      string `json:"id_token,omitempty"`
}

// AuthCodeFlow is the helper for the authorization-code grant (RFC 6749
// section 4.1 + PKCE RFC 7636). The browser round-trip is NOT automated:
// build the authorize URL, send the user there, then exchange the code.
//
// A non-empty clientSecret makes the flow CONFIDENTIAL (HTTP Basic on every
// token request, F-017); an empty one models a PUBLIC client that
// identifies itself via the client_id form field.
type AuthCodeFlow struct {
	BaseURL      string
	ClientID     string
	ClientSecret string
	RedirectURI  string
	Scopes       []string
	HTTPClient   *http.Client
}

// NewAuthCodeFlow constructs the flow. httpClient may be nil (http.DefaultClient).
func NewAuthCodeFlow(baseURL, clientID, clientSecret, redirectURI string, scopes []string) *AuthCodeFlow {
	return &AuthCodeFlow{
		BaseURL:      NormalizeBaseURL(baseURL),
		ClientID:     clientID,
		ClientSecret: clientSecret,
		RedirectURI:  redirectURI,
		Scopes:       scopes,
		HTTPClient:   http.DefaultClient,
	}
}

// BuildAuthorizeURL builds the GET /oauth2/authorize URL to send the user's
// browser to. state is RECOMMENDED (anti-CSRF, RFC 6749 section 10.12) and
// must be verified on the way back; pkce may be nil.
func (f *AuthCodeFlow) BuildAuthorizeURL(state string, pkce *PkcePair) (string, error) {
	if f.BaseURL == "" {
		return "", errInvalidBaseURL(f.BaseURL)
	}
	params := url.Values{}
	params.Set("response_type", "code")
	params.Set("client_id", f.ClientID)
	if f.RedirectURI != "" {
		params.Set("redirect_uri", f.RedirectURI)
	}
	if len(f.Scopes) > 0 {
		params.Set("scope", strings.Join(f.Scopes, " "))
	}
	if state != "" {
		params.Set("state", state)
	}
	if pkce != nil {
		params.Set("code_challenge", pkce.Challenge)
		params.Set("code_challenge_method", "S256")
	}
	return f.BaseURL + AuthorizeURLPath + "?" + params.Encode(), nil
}

// ExchangeCode POSTs grant_type=authorization_code. codeVerifier is REQUIRED
// when the authorize request carried a code_challenge (the server enforces
// PKCE completion).
func (f *AuthCodeFlow) ExchangeCode(ctx context.Context, code, codeVerifier string) (*TokenResponse, error) {
	form := url.Values{}
	form.Set("grant_type", "authorization_code")
	form.Set("code", code)
	if f.RedirectURI != "" {
		form.Set("redirect_uri", f.RedirectURI)
	}
	if codeVerifier != "" {
		form.Set("code_verifier", codeVerifier)
	}
	if f.ClientSecret == "" && f.ClientID != "" {
		form.Set("client_id", f.ClientID)
	}
	return postFormWithBasic(ctx, f.HTTPClient, f.BaseURL+TokenURLPath, form, f.ClientID, f.ClientSecret)
}

// Refresh POSTs grant_type=refresh_token. AuthForge rotates refresh tokens
// on every use (migration V008): always adopt the new RefreshToken from the
// returned response.
func (f *AuthCodeFlow) Refresh(ctx context.Context, refreshToken string) (*TokenResponse, error) {
	form := url.Values{}
	form.Set("grant_type", "refresh_token")
	form.Set("refresh_token", refreshToken)
	if f.ClientSecret == "" && f.ClientID != "" {
		form.Set("client_id", f.ClientID)
	}
	return postFormWithBasic(ctx, f.HTTPClient, f.BaseURL+TokenURLPath, form, f.ClientID, f.ClientSecret)
}

// ParseAuthorizationResponse extracts code/state/error from the
// redirect_uri the server sent the browser back to. The caller MUST verify
// state matches before trusting code.
func ParseAuthorizationResponse(redirectURL string) (code, state, errCode string, err error) {
	parsed, err := url.Parse(redirectURL)
	if err != nil {
		return "", "", "", err
	}
	q := parsed.Query()
	return q.Get("code"), q.Get("state"), q.Get("error"), nil
}
