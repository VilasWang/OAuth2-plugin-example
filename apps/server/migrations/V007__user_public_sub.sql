-- V007: Add public_sub UUID column to users table
-- This provides a non-enumerable public identifier for OAuth2 subject claims
-- instead of exposing the auto-increment internal ID.

ALTER TABLE users ADD COLUMN IF NOT EXISTS public_sub UUID DEFAULT gen_random_uuid();

-- Backfill existing users
UPDATE users SET public_sub = gen_random_uuid() WHERE public_sub IS NULL;

-- Make NOT NULL after backfill
ALTER TABLE users ALTER COLUMN public_sub SET NOT NULL;

-- Unique index for lookups
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_public_sub ON users(public_sub);
