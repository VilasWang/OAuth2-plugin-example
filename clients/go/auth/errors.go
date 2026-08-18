package auth

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"strings"

	"golang.org/x/oauth2"
)

// AuthError carries an RFC 6749 section 5.2 token-endpoint rejection.
type AuthError struct {
	ErrorCode        string
	ErrorDescription string
	StatusCode       int
}

func (e *AuthError) Error() string {
	msg := fmt.Sprintf("authforge: token endpoint rejected client: %s", e.ErrorCode)
	if e.ErrorDescription != "" {
		msg += " (" + e.ErrorDescription + ")"
	}
	if e.StatusCode != 0 {
		msg += fmt.Sprintf(" [HTTP %d]", e.StatusCode)
	}
	return msg
}

// asAuthError unwraps golang.org/x/oauth2 retrieval failures (which carry
// the raw RFC 6749 body) into *AuthError; other errors pass through.
func asAuthError(err error) error {
	var re *oauth2.RetrieveError
	if errors.As(err, &re) {
		var body struct {
			Error            string `json:"error"`
			ErrorDescription string `json:"error_description"`
		}
		if json.Unmarshal(re.Body, &body) == nil && body.Error != "" {
			return &AuthError{
				ErrorCode:        body.Error,
				ErrorDescription: body.ErrorDescription,
				StatusCode:       re.Response.StatusCode,
			}
		}
	}
	return err
}

// postFormWithBasic issues the token-endpoint form POST with explicit HTTP
// Basic client authentication (F-017: client_secret_basic; the body form is
// rejected for confidential clients) and decodes either a token response or
// an RFC 6749 error.
func postFormWithBasic(ctx context.Context, httpClient *http.Client, tokenURL string, form url.Values, clientID, clientSecret string) (*TokenResponse, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, tokenURL, strings.NewReader(form.Encode()))
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	if clientSecret != "" {
		req.SetBasicAuth(clientID, clientSecret)
	}
	resp, err := httpClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		var body struct {
			Error            string `json:"error"`
			ErrorDescription string `json:"error_description"`
		}
		_ = json.NewDecoder(resp.Body).Decode(&body)
		return nil, &AuthError{
			ErrorCode:        body.Error,
			ErrorDescription: body.ErrorDescription,
			StatusCode:       resp.StatusCode,
		}
	}
	var tokens TokenResponse
	if err := json.NewDecoder(resp.Body).Decode(&tokens); err != nil {
		return nil, fmt.Errorf("authforge: decoding token response: %w", err)
	}
	return &tokens, nil
}
