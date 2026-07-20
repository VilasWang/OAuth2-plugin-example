---
description: DB access must be the async + Mapper + Criteria combo; raw SQL only in 3 exemptions
globs:
  - "OAuth2Plugin/**"
  - "OAuth2Server/**"
---

Every database operation in this codebase MUST be the **async callback + Mapper
+ Criteria** combo. The three parts are jointly mandatory, not pick-one.

## The combo (all three, every query)

1. **Async callback** — prefer `Mapper::findOne` / `execSqlAsync` with a moved
   `std::function<...> &&callback` (last param). Synchronous
   `Mapper::findBy`-with-future is RESTRICTED, only when a sync result is truly
   needed. `CoroMapper` is FORBIDDEN.
2. **Mapper API** — SELECT via `Mapper::findBy` / `findOne`, INSERT via
   `Mapper::insert`, UPDATE via `Mapper::update`. No hand-rolled SQL for CRUD.
3. **Criteria** — build WHERE conditions with `Criteria`, never by concatenating
   strings into a query. For multi-row membership, use `Criteria::In(...)`. For
   compound conditions, chain `&&` / `||` on Criteria objects.

JOIN-in-a-single-query is forbidden — split into multiple queries (or
`Criteria::In`). Capture `auto self = shared_from_this()` in the callback to
avoid use-after-free.

## The three raw-SQL exemptions (and only these)

Raw SQL is allowed ONLY for:
- **DDL** (schema setup, migrations),
- **`UPDATE ... RETURNING`** (when you need the updated row back in one step),
- **documented batch operations** (state the justification in a comment).

Anything else as raw SQL is a violation. A PreToolUse hook also guards
credential placeholders in these files, so failures show up before runtime.

The `project-conventions` skill holds the full statement; this file is the
path-scoped reminder that loads when you edit `OAuth2Plugin/**` or
`OAuth2Server/**`.
