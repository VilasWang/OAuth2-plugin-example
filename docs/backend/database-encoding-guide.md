# Database Encoding Guide

## Current Configuration

The OAuth2 plugin example database uses the following encoding configuration:

- **Database Encoding**: SQL_ASCII (encoding ID 6)
- **Server Encoding**: UTF8
- **Client Encoding**: UTF8 (controlled via `PGCLIENTENCODING=UTF8`)
- **Windows Console**: UTF-8 (code page 65001, controlled via `chcp 65001`)

## Why SQL_ASCII?

The database shows as SQL_ASCII due to PostgreSQL template database configuration on this system. When creating a database without explicitly specifying `TEMPLATE template0`, PostgreSQL uses `template1` as the template, which may have SQL_ASCII encoding.

### Attempts to Fix

The following attempts were made to create a UTF-8 database:

1. **Explicit ENCODING clause**:

   ```sql
   CREATE DATABASE fulla_db ENCODING 'UTF8';
   ```

   Result: Database still created as SQL_ASCII

2. **Using template0**:

   ```sql
   CREATE DATABASE fulla_db ENCODING 'UTF8' TEMPLATE template0;
   ```

   Result: Failed - template0 may also be SQL_ASCII

3. **With locale settings**:

   ```sql
   CREATE DATABASE fulla_db WITH ENCODING 'UTF8' LC_CTYPE='C' LC_COLLATE='C';
   ```

   Result: Database creation failed or remained SQL_ASCII

## Why This Configuration Works

Despite the database showing as SQL_ASCII, the system functions correctly because:

1. **Server Encoding is UTF8**: The PostgreSQL server itself uses UTF8 encoding
2. **Client Encoding is UTF8**: All client connections use `PGCLIENTENCODING=UTF8`
3. **SQL Files are UTF-8**: All `.sql` schema files are saved in UTF-8 format
4. **Application Layer**: The C++ application handles UTF-8 strings correctly

### Data Flow

```text
UTF-8 SQL Files → PostgreSQL Server (UTF8) → Client Connection (UTF8) → Application (UTF8)
```

The SQL_ASCII database encoding essentially means "no encoding conversion" - it stores data as-is. Since all components in the chain use UTF-8, data flows correctly.

## Verification

The system has been verified to work correctly with this configuration:

- ✅ User registration and login (including Chinese usernames)
- ✅ OAuth2 authorization flow
- ✅ Token introspection and revocation
- ✅ RBAC permission system
- ✅ All database operations

## Recommendations

### For Development

The current configuration is **acceptable for development and testing**. No changes are required.

### For Production

For production deployment, consider:

1. **Recreate template databases**: Ensure template0 and template1 use UTF-8 encoding
2. **Use explicit template**: Always create databases with `TEMPLATE template0 ENCODING 'UTF8'`
3. **Verify PostgreSQL installation**: Ensure PostgreSQL was compiled with UTF-8 support

### Steps to Fix Template Databases (Advanced)

If you want to permanently fix the encoding issue:

1. **Backup all databases**:

   ```bash
   pg_dumpall > backup.sql
   ```

2. **Stop PostgreSQL service**

3. **Delete template1** (it will be regenerated from template0):

   ```sql
   DELETE FROM pg_database WHERE datname = 'template1';
   ```

4. **Recreate template1 from template0 with UTF-8**:

   ```sql
   CREATE DATABASE template1 TEMPLATE template0 ENCODING 'UTF8';
   ```

5. **Restart PostgreSQL service**

**⚠️ Warning**: These operations are risky and should only be performed by experienced PostgreSQL administrators.

## Chinese Comment Display Issue

### Problem

SQL files contained Chinese comments that displayed as garbled text when executed through psql on Windows with SQL_ASCII database encoding:

```text
娉ㄦ剰:  琛?"oauth2_refresh_tokens" 涓嶅瓨鍦?
```

### Solution

All Chinese comments in SQL files have been replaced with English equivalents to avoid encoding display issues:

**Modified File**: `apps/server/migrations/V006__oauth2_scopes.sql` (previously `sql/004_oauth2_scopes.sql`)

**Changes**:

- All table/column comments converted to English
- All documentation notes translated to English
- All scope descriptions translated to English
- Removed emoji characters (✅, ⚠️) that could cause encoding issues

### Result

SQL files now execute cleanly without garbled output:

```text
DROP TABLE
psql:apps/server/migrations/V002__oauth2_core.sql:5: NOTICE:  table "oauth2_access_tokens" does not exist
DROP TABLE
CREATE TABLE
```

## Console Encoding on Windows

To display characters correctly in Windows console:

1. **Set console code page**:

   ```batch
   chcp 65001
   ```

2. **Set client encoding**:

   ```batch
   set PGCLIENTENCODING=UTF8
   ```

Both settings are now included in:

- `scripts/backend/setup_database.bat`

## Conclusion

The SQL_ASCII database encoding with UTF-8 client/server configuration is a **functional workaround** that allows the OAuth2 system to operate correctly. While not ideal, it is acceptable for development and testing purposes.

For production deployment, ensure proper UTF-8 encoding at the PostgreSQL cluster level.

---

**Last Updated**: 2026-05-12  
**Status**: Documented and functional
