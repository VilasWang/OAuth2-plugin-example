-- V021: Widen email_verification_tokens.email to match users.email
-- V019 extended users.email to VARCHAR(254) (RFC 5321) but left this table at
-- VARCHAR(100) from V010. A long-but-valid address that registers successfully
-- then fails to insert its verification token, so the verification email is
-- never sent and the account can never be verified. Align the widths.
-- password_reset_tokens (V009) has NO email column, so it is unaffected.

ALTER TABLE email_verification_tokens ALTER COLUMN email TYPE VARCHAR(254);
