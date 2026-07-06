-- V022: MFA pending client/redirect_uri binding
-- Records the client_id/redirect_uri used on the first-factor login that
-- triggered mfa_required, so verifyLogin can reject a second-factor request
-- that supplies a different (even if independently valid) client/redirect_uri
-- pair. Fixes cross-client authorization confusion (P0-1).

ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_pending_client_id VARCHAR(50);
ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_pending_redirect_uri TEXT;
