-- V029: Forced first-login password change (issue #145)
-- Adds must_change_password to users; TRUE = the account must change its
-- password via POST /oauth2/password/change (or PUT /api/me/password)
-- before any authorization codes are issued for it.
-- Set on bootstrap-created admin accounts and optionally via the admin
-- create/update user API. Cleared automatically on successful password change.
-- NOT NULL (PR #157 review MINOR 10): a nullable boolean flag with DEFAULT
-- FALSE invites `= false` predicates that silently miss NULL rows; every
-- reader treats the flag as total.
ALTER TABLE users ADD COLUMN IF NOT EXISTS must_change_password BOOLEAN NOT NULL DEFAULT FALSE;
