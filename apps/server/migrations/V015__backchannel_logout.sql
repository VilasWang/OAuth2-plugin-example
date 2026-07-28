ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS backchannel_logout_uri VARCHAR(512);
ALTER TABLE oauth2_clients ADD COLUMN IF NOT EXISTS backchannel_logout_session_required BOOLEAN DEFAULT FALSE;
