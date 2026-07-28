-- V019: Email validation and uniqueness
-- 1. Extend users.email to VARCHAR(254) per RFC 5321 (was 100)
-- 2. Add unique index on non-null emails (NULL emails are exempt — email is optional)
--
-- Prerequisite: duplicate emails must be cleaned before applying.
-- The partial index (WHERE email IS NOT NULL) lets multiple users have no email.

ALTER TABLE users ALTER COLUMN email TYPE VARCHAR(254);

CREATE UNIQUE INDEX IF NOT EXISTS idx_users_email_unique
    ON users (email)
    WHERE email IS NOT NULL;
