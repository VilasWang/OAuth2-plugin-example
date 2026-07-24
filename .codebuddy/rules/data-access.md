---
description: Storage/data-access path-scoped pointer to db-operations rule
paths: "OAuth2Plugin/**/storage/**,OAuth2Server/**/*.cc"
alwaysApply: false
---

DB access rules (async + Mapper + Criteria combo, the three raw-SQL exemptions)
live in `.codebuddy/rules/db-operations.md` — see that file. The full statement is
also in `CODEBUDDY.md`. This file is only the path-scoped
trigger for storage code.
