-- V011: MFA (Multi-Factor Authentication) support
-- Adds TOTP secret and backup codes to users table

ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_enabled BOOLEAN DEFAULT FALSE;
ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_secret VARCHAR(64);
ALTER TABLE users ADD COLUMN IF NOT EXISTS mfa_backup_codes TEXT;  -- JSON array of hashed codes
