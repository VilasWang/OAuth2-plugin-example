-- V008: Add family_id to refresh tokens for reuse detection
-- All refresh tokens from the same authorization code share a family_id.
-- If a revoked token is reused, the entire family is cascade-revoked.

ALTER TABLE oauth2_refresh_tokens ADD COLUMN IF NOT EXISTS family_id VARCHAR(64);

CREATE INDEX IF NOT EXISTS idx_refresh_tokens_family ON oauth2_refresh_tokens(family_id);
