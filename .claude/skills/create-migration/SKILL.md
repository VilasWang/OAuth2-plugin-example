---
name: create-migration
description: Create versioned SQL migration files for database schema changes, following project conventions
disable-model-invocation: true
---

# SQL Migration Skill

Create versioned SQL migration files for the OAuth2 database schema.

## When to Use

When adding, modifying, or dropping database tables/columns/indexes. Triggered by `/create-migration`.

## Migration Naming

Pattern: `apps/server/migrations/V{NNN}__descriptive_name.sql`

Check existing migrations before creating to determine the next number:
```bash
ls -1 apps/server/migrations/ | sort | tail -1
```

The next migration number is one higher than the latest file found above.

## File Template

```sql
-- Migration: V{NNN}__{description}
-- Created: {date}
-- Purpose: {why this change is needed}

-- === UP ===
-- Forward migration: apply changes
{SQL statements here}

-- === DOWN ===
-- Rollback migration: revert changes
-- (optional but recommended for production)
{ROLLBACK SQL here}
```

## Validation Checklist

Before finalizing, verify:

1. **Naming**: Follows `V{NNN}__snake_case.sql` pattern
2. **Numbering**: Next number after latest existing migration
3. **Idempotent**: Uses `IF NOT EXISTS` / `IF EXISTS` where appropriate
4. **No data loss**: ALTER COLUMN preserves existing data; DROP has DOWN section
5. **Schema consistency**: Matches ORM model expectations in `model.json`
6. **CMakeLists**: No update needed. Migrations are applied at runtime by `SchemaManager` (when `FULLA_AUTO_MIGRATE=true`) or via `fulla-server --migrate-only` / `scripts/backend/setup-database.{sh,bat}`; no build-file change required.

## Common Patterns

### Add Table
```sql
CREATE TABLE IF NOT EXISTS new_table (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### Add Column
```sql
ALTER TABLE existing_table ADD COLUMN IF NOT EXISTS new_column VARCHAR(50);
```

### Add Index
```sql
CREATE INDEX IF NOT EXISTS idx_table_column ON existing_table(column);
```

### Add Foreign Key
```sql
ALTER TABLE child_table
    ADD CONSTRAINT fk_child_parent
    FOREIGN KEY (parent_id) REFERENCES parent_table(id) ON DELETE CASCADE;
```

## After Creating

1. Test the migration: `/build-and-test` or run SchemaManager
2. If ORM models need updating: `/orm-gen`
3. Update `model.json` if new tables added
4. Update OpenAPI spec if API affected: `/openapi-update`
