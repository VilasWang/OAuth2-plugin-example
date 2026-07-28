-- V013: Account lockout and progressive backoff
-- Tracks failed login attempts and locks accounts after threshold

ALTER TABLE users ADD COLUMN IF NOT EXISTS failed_login_count INTEGER DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS locked_until BIGINT DEFAULT 0;
ALTER TABLE users ADD COLUMN IF NOT EXISTS last_failed_login BIGINT DEFAULT 0;
