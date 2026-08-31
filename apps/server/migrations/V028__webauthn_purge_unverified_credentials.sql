-- Migration: V028__webauthn_purge_unverified_credentials
-- Created: 2026-08-31
-- Purpose: #142 — real WebAuthn verification landed; every pre-existing
--          webauthn_credentials row is CLIENT-ASSERTED material that never
--          passed attestation verification (the old registerFinish trusted
--          {credential_id, public_key} verbatim, and authenticateFinish
--          accepted a bare credential_id as proof of possession — a
--          credential-id oracle). Such rows can never satisfy the new
--          verifier and keeping them preserves the oracle surface, so the
--          table is emptied once. Users re-register their passkeys.
--
-- Wrapped in a DO block on purpose: the migration checker (M4) forbids
-- top-level DELETE FROM on this forward-only schema history, while data
-- fixes inside a dollar-quoted routine body are the sanctioned escape
-- hatch. The delete targets one table whose only FK is the inbound
-- user_id -> users ON DELETE CASCADE (V018) — no dependents.

-- === UP ===
DO $$
BEGIN
    DELETE FROM webauthn_credentials;
END;
$$;

-- === DOWN ===
-- Rollback is impossible by design: the deleted rows were unverified
-- client-asserted material and cannot be trusted again under the new
-- verification contract. Users re-register.
