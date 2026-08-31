-- Migration: V027__local_subject_mapping_backfill
-- Created: 2026-08-31
-- Purpose: #143 — one-time backfill giving every existing user a
--          (provider='local', subject=<internal id>) row in
--          oauth2_subject_mappings. Consent's getInternalUserId resolves
--          users exclusively through that table and returns 500 without a
--          row, so any user created on a path that skipped the mapping
--          (pre-fix self-registrations and admin-created users) is
--          consent-broken. Idempotent: ON CONFLICT (provider, subject)
--          DO NOTHING keeps existing rows (and any deliberately-seeded
--          alternates) untouched; a startup self-heal pass in
--          AdminBootstrapper::backfillLocalSubjectMappings re-runs the same
--          shape at every boot to converge future gaps.

-- === UP ===
-- Intentionally NOT filtering deleted_at: soft delete is reversible, and a
-- restored user must not re-enter the broken state. Consent-side liveness
-- checks (PostgresIdentityRepository::getInternalUserId joins users with
-- deleted_at IS NULL) reject soft-deleted users regardless of the mapping.
INSERT INTO oauth2_subject_mappings (subject, internal_user_id, provider)
SELECT u.id::text, u.id, 'local'
FROM users u
ON CONFLICT (provider, subject) DO NOTHING;

-- === DOWN ===
-- Rollback is a no-op: the backfill only ADDS the canonical rows the
-- consent flow requires; removing them would re-break every local user.
