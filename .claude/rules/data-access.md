---
description: Storage/data-access path-scoped pointer to db-operations rule
globs:
  - "libs/storage-*/**"
  - "apps/server/**/*.cc"
---

DB access rules (async + Mapper + Criteria combo, the three raw-SQL exemptions)
live in `.claude/rules/db-operations.md` — see that file. The full statement is
also in the `project-conventions` skill. This file is only the path-scoped
trigger for storage code.
