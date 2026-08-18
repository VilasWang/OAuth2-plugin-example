-- Migration: V026__token_redundant_index_cleanup
-- Created: 2026-08-18
-- Purpose: Drop B-tree indexes that duplicate the token tables' primary keys
--          (introspect covering-index plan option C, subagent-reviewed:
--          docs/productization-evolution/in-progress/introspect-covering-index-plan.md).
--          Both token tables have PK(token) unique B-trees; every token-column
--          lookup in the codebase is a point lookup (or a prefix LIKE served by
--          the PK b-tree, TokenManagementService.cc:184), so these secondary
--          indexes only add INSERT/UPDATE write amplification: access tokens go
--          from 8 to 6 B-trees per INSERT. Plain DROP INDEX (no CONCURRENTLY):
--          SchemaManager applies migrations inside a single transaction where
--          CONCURRENTLY is invalid; the drops are instantaneous.

-- === UP ===
DROP INDEX IF EXISTS idx_access_tokens_token;   -- duplicate of oauth2_access_tokens PK(token) (V003)
DROP INDEX IF EXISTS idx_access_tokens_active;  -- ON (token) WHERE revoked=false (V016); PK serves the same EQ lookups
DROP INDEX IF EXISTS idx_refresh_tokens_token;  -- duplicate of oauth2_refresh_tokens PK(token) (V003)

-- === DOWN ===
-- Rollback (recreate per the V003/V016 originals):
-- CREATE INDEX IF NOT EXISTS idx_access_tokens_token ON oauth2_access_tokens(token);
-- CREATE INDEX IF NOT EXISTS idx_access_tokens_active ON oauth2_access_tokens(token) WHERE revoked = false;
-- CREATE INDEX IF NOT EXISTS idx_refresh_tokens_token ON oauth2_refresh_tokens(token);
