-- V020: Make username optional (email-first auth)
-- Evolution toward email as the primary identifier: username becomes an optional
-- display name rather than a required login key.
--
-- 1. Drop NOT NULL on username (users may register with email only)
-- 2. Extend username from VARCHAR(50) to VARCHAR(100) to align with USERNAME_PATTERN
--    (the ORM already permits up to 100, but the column was only 50 — fixing the mismatch)
-- 3. Relax CHECK constraint: NULL is now allowed; a non-null username still must not be empty
--
-- UNIQUE constraint is INTENTIONALLY KEPT: PostgreSQL exempts NULL from uniqueness,
-- so "optional but no duplicates for those who set one" works correctly. This prevents
-- confusing display-name collisions while allowing multiple username-less accounts.
--
-- Prerequisite (verified pre-migration): no existing NULL usernames, max length <= 50.

ALTER TABLE users ALTER COLUMN username DROP NOT NULL;

ALTER TABLE users ALTER COLUMN username TYPE VARCHAR(100);

ALTER TABLE users DROP CONSTRAINT IF EXISTS users_username_check;
ALTER TABLE users ADD CONSTRAINT users_username_check
    CHECK (username IS NULL OR username <> '');
