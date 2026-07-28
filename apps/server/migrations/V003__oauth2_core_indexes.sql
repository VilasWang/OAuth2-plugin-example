-- V003: Indexes for OAuth2 Core Tables

CREATE INDEX IF NOT EXISTS idx_access_tokens_token ON oauth2_access_tokens(token);
CREATE INDEX IF NOT EXISTS idx_access_tokens_client_id ON oauth2_access_tokens(client_id);
CREATE INDEX IF NOT EXISTS idx_access_tokens_expires_at ON oauth2_access_tokens(expires_at);
CREATE INDEX IF NOT EXISTS idx_access_tokens_revoked ON oauth2_access_tokens(revoked, expires_at);
CREATE INDEX IF NOT EXISTS idx_refresh_tokens_token ON oauth2_refresh_tokens(token);
CREATE INDEX IF NOT EXISTS idx_refresh_tokens_revoked ON oauth2_refresh_tokens(revoked, expires_at);
