CREATE TABLE IF NOT EXISTS oauth2_device_codes (
    device_code_hash VARCHAR(64) PRIMARY KEY,
    user_code VARCHAR(8) NOT NULL UNIQUE,
    client_id VARCHAR(50) NOT NULL REFERENCES oauth2_clients(client_id),
    scope TEXT,
    status VARCHAR(20) DEFAULT 'pending',
    user_id VARCHAR(50),
    expires_at BIGINT NOT NULL,
    interval_seconds INTEGER DEFAULT 5,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_device_codes_user_code ON oauth2_device_codes(user_code);
CREATE INDEX IF NOT EXISTS idx_device_codes_expires ON oauth2_device_codes(expires_at);
