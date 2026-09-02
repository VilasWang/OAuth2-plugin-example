<!-- Thanks for contributing! See CONTRIBUTING.md for the full guide. -->

## Summary

<!-- What changed and why. Reference the issue ("Closes #N") when one exists. -->

## Type of change

- [ ] Bug fix (non-breaking)
- [ ] New feature (non-breaking)
- [ ] Breaking change (API, config, or protocol surface)
- [ ] Docs / tests / CI only

## Checklist

General:

- [ ] Tests added or updated; full suite green locally (or N/A — say why)
- [ ] `CHANGELOG.md` entry added (user-visible changes)
- [ ] Docs updated (incl. `website/i18n/zh-CN` mirrors when `docs/` content changed)

If this touches the API surface (skip otherwise):

- [ ] OpenAPI spec kept in sync (authoritative `apps/server/openapi.yaml` and its mirror points)
- [ ] Error codes synced at all five points (spec, catalog test, SDKs where applicable)
- [ ] Endpoint fingerprints / golden baselines regenerated if endpoints changed
- [ ] `api-diff` and `oasdiff` gates addressed (a justified `--force` / ignore entry is fine — explain below)
- [ ] Breaking changes curated into the `Breaking changes` section of `CHANGELOG.md`
