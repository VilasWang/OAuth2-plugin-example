-- V016: Token table optimization for high-QPS scenarios
-- Adds composite indexes for efficient token queries and cleanup

-- Composite index for active token lookups (most common query pattern)
CREATE INDEX IF NOT EXISTS idx_access_tokens_active 
    ON oauth2_access_tokens(token) 
    WHERE revoked = false;

-- Composite index for user-specific token queries
CREATE INDEX IF NOT EXISTS idx_access_tokens_user_active 
    ON oauth2_access_tokens(user_id, revoked, expires_at);

-- Composite index for client-specific token queries
CREATE INDEX IF NOT EXISTS idx_access_tokens_client_active 
    ON oauth2_access_tokens(client_id, revoked, expires_at);

-- Refresh token: composite index for family-based queries
CREATE INDEX IF NOT EXISTS idx_refresh_tokens_family_active 
    ON oauth2_refresh_tokens(family_id, revoked);

-- Function to archive expired tokens (move to archive table)
CREATE TABLE IF NOT EXISTS oauth2_access_tokens_archive (
    LIKE oauth2_access_tokens INCLUDING ALL
);

-- Archive function: moves expired+revoked tokens older than N days
CREATE OR REPLACE FUNCTION archive_expired_tokens(days_old INTEGER DEFAULT 30)
RETURNS INTEGER AS $$
DECLARE
    archived_count INTEGER;
    cutoff_time BIGINT;
BEGIN
    cutoff_time := EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - (days_old || ' days')::INTERVAL))::BIGINT;
    
    -- Move to archive
    INSERT INTO oauth2_access_tokens_archive
    SELECT * FROM oauth2_access_tokens
    WHERE expires_at < cutoff_time OR (revoked = true AND revoked_at < cutoff_time);
    
    GET DIAGNOSTICS archived_count = ROW_COUNT;
    
    -- Delete from main table
    DELETE FROM oauth2_access_tokens
    WHERE expires_at < cutoff_time OR (revoked = true AND revoked_at < cutoff_time);
    
    RETURN archived_count;
END;
$$ LANGUAGE plpgsql;

COMMENT ON FUNCTION archive_expired_tokens IS 'Archives expired/revoked tokens older than N days. Call periodically via cleanup service.';
