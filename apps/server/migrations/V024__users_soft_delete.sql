-- V024: Soft-delete support for users (user-management feature)
-- Adds a nullable deleted_at column; NULL = active, non-NULL = soft-deleted.
-- All user queries filter WHERE deleted_at IS NULL to exclude deleted users.
ALTER TABLE users ADD COLUMN IF NOT EXISTS deleted_at TIMESTAMP WITH TIME ZONE;
CREATE INDEX IF NOT EXISTS idx_users_deleted_at ON users(deleted_at);
