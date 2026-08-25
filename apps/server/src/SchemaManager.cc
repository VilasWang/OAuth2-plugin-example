#include "SchemaManager.h"
#include <drogon/drogon.h>
#include <fulla/drogon/adapters/OpenSslCryptoProvider.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>

namespace schema
{

std::vector<std::string> SchemaManager::splitSqlStatements(const std::string &sql)
{
    std::vector<std::string> statements;
    std::string current;
    size_t i = 0;
    const size_t n = sql.size();

    while (i < n)
    {
        char c = sql[i];

        // Line comment: -- ... \n
        if (c == '-' && i + 1 < n && sql[i + 1] == '-')
        {
            current += "--";
            i += 2;
            while (i < n && sql[i] != '\n')
            {
                current += sql[i];
                ++i;
            }
            continue;
        }

        // Block comment: /* ... */ (Postgres does not nest block comments).
        if (c == '/' && i + 1 < n && sql[i + 1] == '*')
        {
            current += "/*";
            i += 2;
            while (i < n && !(sql[i] == '*' && i + 1 < n && sql[i + 1] == '/'))
            {
                current += sql[i];
                ++i;
            }
            if (i < n)
            {
                current += "*/";
                i += 2;
            }
            continue;
        }

        // Single-quoted string: '...'. '' is an escaped quote, not a close.
        if (c == '\'')
        {
            current += c;
            ++i;
            while (i < n)
            {
                current += sql[i];
                if (sql[i] == '\'' && i + 1 < n && sql[i + 1] == '\'')
                {
                    // Escaped quote — consume both, stay in string.
                    current += sql[i + 1];
                    i += 2;
                    continue;
                }
                if (sql[i] == '\'')
                {
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }

        // Dollar-quote: $$..$$ or $tag$..$tag$. Triggered by '$' that begins
        // a valid dollar-quote opener (not inside a string/comment).
        if (c == '$')
        {
            // Try to read an opener: $ [tag] $
            size_t j = i + 1;
            std::string tag;
            while (j < n && sql[j] != '$' && sql[j] != '\n')
            {
                char tc = sql[j];
                if (tag.empty())
                {
                    if (!(std::isalpha(static_cast<unsigned char>(tc)) || tc == '_'))
                        break;
                }
                else
                {
                    if (!(std::isalnum(static_cast<unsigned char>(tc)) || tc == '_'))
                        break;
                }
                tag += tc;
                ++j;
            }
            if (j < n && sql[j] == '$')
            {
                // Valid opener: from i to j inclusive is the delimiter.
                std::string delim = sql.substr(i, j - i + 1);
                current += delim;
                i = j + 1;
                // Scan until the matching delimiter appears again.
                while (i < n)
                {
                    if (sql[i] == '$' && sql.compare(i, delim.size(), delim) == 0)
                    {
                        current += delim;
                        i += delim.size();
                        break;
                    }
                    current += sql[i];
                    ++i;
                }
                continue;
            }
            // Not a dollar-quote opener — fall through to treat '$' literally.
        }

        // Top-level semicolon terminates a statement.
        if (c == ';')
        {
            current += c;
            // Trim and emit non-empty statements.
            size_t start = current.find_first_not_of(" \t\r\n");
            size_t end = current.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos && end >= start)
            {
                std::string trimmed = current.substr(start, end - start + 1);
                // Drop if the whole trimmed string is just the semicolon.
                if (trimmed != ";" && !trimmed.empty())
                {
                    statements.push_back(trimmed);
                }
            }
            current.clear();
            ++i;
            continue;
        }

        current += c;
        ++i;
    }

    // Trailing content without a closing semicolon: emit only if non-empty
    // after trimming. (Migrations should always end with ';', but be lenient.)
    size_t start = current.find_first_not_of(" \t\r\n");
    size_t end = current.find_last_not_of(" \t\r\n");
    if (start != std::string::npos && end != std::string::npos && end >= start)
    {
        statements.push_back(current.substr(start, end - start + 1));
    }

    return statements;
}

bool SchemaManager::migrate(const std::string &migrationsDir)
{
    auto db = drogon::app().getDbClient();
    if (!db)
    {
        LOG_ERROR << "SchemaManager: No database client available";
        return false;
    }
    return migrate(db, migrationsDir);
}

bool SchemaManager::migrate(const drogon::orm::DbClientPtr &db, const std::string &migrationsDir)
{
    LOG_INFO << "SchemaManager: Starting migration from " << migrationsDir;

    if (!db)
    {
        LOG_ERROR << "SchemaManager: No database client available";
        return false;
    }

    // Scan for migration files first -- a missing directory should not open a
    // transaction (and a no-op run should not hold a write transaction).
    auto migrations = scanMigrationFiles(migrationsDir);
    if (migrations.empty())
    {
        LOG_INFO << "SchemaManager: No migration files found in " << migrationsDir;
        // Still ensure the tracking table exists (idempotent).
        return ensureMigrationsTable(db);
    }

    // #46: run the ENTIRE migration pass inside ONE transaction on ONE
    // connection. The previous per-migration transactions could run on
    // different pool connections; a later migration's transaction snapshot
    // then failed to see an earlier migration's committed tables, failing
    // (e.g.) V3 because it could not see V2's tables. A single transaction
    // makes all DDL visible within the run and commits atomically -- either
    // every pending migration lands or none does.
    //
    // A Transaction IS-A DbClient, so it can be passed to the helpers below in
    // place of the pool client.
    auto trans = db->newTransaction();
    if (!trans)
    {
        LOG_ERROR << "SchemaManager: Failed to begin transaction";
        return false;
    }

    // On any failure path: roll back explicitly so the atomic all-or-nothing
    // guarantee holds even though `trans` is still in scope when we return.
    auto rollbackAndReturn = [&trans]() {
        try
        {
            trans->rollback();
        }
        catch (const std::exception &e)
        {
            LOG_ERROR << "SchemaManager: rollback failed: " << e.what();
        }
        return false;
    };

    // Step 1: Ensure schema_migrations table exists (inside the transaction)
    if (!ensureMigrationsTable(trans))
    {
        return rollbackAndReturn();
    }

    // Step 2: Get already-applied versions (inside the transaction)
    auto applied = getAppliedVersions(trans);

    // Step 3: Filter and apply unapplied migrations
    int appliedCount = 0;
    for (const auto &migration : migrations)
    {
        bool alreadyApplied =
          std::find(applied.begin(), applied.end(), migration.version) != applied.end();
        if (alreadyApplied)
        {
            continue;
        }

        if (!applyMigration(trans, migration))
        {
            LOG_ERROR << "SchemaManager: Migration V" << std::to_string(migration.version)
                      << " failed. Stopping.";
            return rollbackAndReturn();
        }
        ++appliedCount;
    }

    // Commit. Drogon commits asynchronously when the Transaction shared_ptr is
    // destroyed (no public commit() method). To make the commit OBSERVABLE --
    // so the caller (notably `--migrate-only`, which may exit the process
    // right after this returns) can rely on the schema actually being durable
    // -- register a commit callback that fulfills a promise and block on it.
    std::shared_ptr<std::promise<bool>> commitPromise = std::make_shared<std::promise<bool>>();
    std::future<bool> commitFuture = commitPromise->get_future();
    trans->setCommitCallback([commitPromise](bool committed) {
        commitPromise->set_value(committed);
    });

    // Drop the last reference so the destructor enqueues the COMMIT.
    trans.reset();

    const bool committed = commitFuture.get();
    if (!committed)
    {
        LOG_ERROR << "SchemaManager: Transaction commit failed";
        return false;
    }

    if (appliedCount > 0)
    {
        LOG_INFO << "SchemaManager: Applied " << appliedCount << " migration(s) successfully";
    }
    else
    {
        LOG_INFO << "SchemaManager: Database is up to date";
    }

    return true;
}

int SchemaManager::countPendingMigrations(
  const drogon::orm::DbClientPtr &db,
  const std::string &migrationsDir
)
{
    if (!db)
        return -1;

    auto migrations = scanMigrationFiles(migrationsDir);
    if (migrations.empty())
        return 0;

    try
    {
        // If schema_migrations doesn't exist yet, every migration is pending.
        auto reg = db->execSqlSync("SELECT to_regclass('schema_migrations') AS t");
        if (reg.empty() || reg[0]["t"].isNull())
            return static_cast<int>(migrations.size());

        auto result = db->execSqlSync("SELECT version FROM schema_migrations");
        std::vector<int> applied;
        for (const auto &row : result)
            applied.push_back(row["version"].as<int>());

        int pending = 0;
        for (const auto &migration : migrations)
        {
            if (std::find(applied.begin(), applied.end(), migration.version) == applied.end())
                ++pending;
        }
        return pending;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "SchemaManager: Pending-migration check failed: " << e.what();
        return -1;
    }
}

bool SchemaManager::ensureMigrationsTable(const drogon::orm::DbClientPtr &db)
{
    try
    {
        db->execSqlSync(
          "CREATE TABLE IF NOT EXISTS schema_migrations ("
          "  version INTEGER PRIMARY KEY,"
          "  filename VARCHAR(255) NOT NULL,"
          "  checksum VARCHAR(64) NOT NULL,"
          "  applied_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP"
          ")"
        );
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "SchemaManager: Failed to create schema_migrations table: " << e.what();
        return false;
    }
}

std::vector<SchemaManager::MigrationFile> SchemaManager::scanMigrationFiles(const std::string &dir)
{
    std::vector<MigrationFile> files;
    namespace fs = std::filesystem;

    if (!fs::exists(dir) || !fs::is_directory(dir))
    {
        LOG_WARN << "SchemaManager: Migrations directory does not exist: " << dir;
        return files;
    }

    // Match V{NNN}__description.sql pattern
    std::regex pattern(R"(V(\d+)__.+\.sql)", std::regex::icase);

    for (const auto &entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;

        std::string filename = entry.path().filename().string();
        std::smatch match;

        if (std::regex_match(filename, match, pattern))
        {
            MigrationFile mf;
            mf.version = std::stoi(match[1].str());
            mf.filename = filename;
            mf.filepath = entry.path().string();
            files.push_back(mf);
        }
    }

    // Sort by version number
    std::sort(files.begin(), files.end(), [](const MigrationFile &a, const MigrationFile &b) {
        return a.version < b.version;
    });

    return files;
}

std::vector<int> SchemaManager::getAppliedVersions(const drogon::orm::DbClientPtr &db)
{
    std::vector<int> versions;
    try
    {
        auto result = db->execSqlSync("SELECT version FROM schema_migrations ORDER BY version");
        for (const auto &row : result)
        {
            versions.push_back(row["version"].as<int>());
        }
    }
    catch (const std::exception &e)
    {
        LOG_WARN << "SchemaManager: Could not query applied versions: " << e.what();
    }
    return versions;
}

bool SchemaManager::applyMigration(
  const drogon::orm::DbClientPtr &trans,
  const MigrationFile &migration
)
{
    // Read migration file
    std::ifstream file(migration.filepath);
    if (!file.is_open())
    {
        LOG_ERROR << "SchemaManager: Cannot open migration file: " << migration.filepath;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string sql = buffer.str();

    if (sql.empty())
    {
        LOG_WARN << "SchemaManager: Empty migration file: " << migration.filename;
        return true;  // Skip empty files
    }

    std::string checksum = computeChecksum(sql);

    LOG_INFO << "SchemaManager: Applying V" << std::to_string(migration.version) << " ("
             << migration.filename << ")";

    try
    {
        // Execute migration SQL against the caller's transaction (#46: the
        // whole run shares one transaction/connection). PostgreSQL prepared
        // statements accept only one statement, so split the file on
        // top-level semicolons and execute each statement separately. If any
        // statement fails it throws; the caller rolls back the transaction.
        auto statements = splitSqlStatements(sql);
        for (const auto &stmt : statements)
        {
            trans->execSqlSync(stmt);
        }

        // Record the migration (same transaction)
        trans->execSqlSync(
          "INSERT INTO schema_migrations (version, filename, checksum) VALUES ($1, $2, $3)",
          migration.version,
          migration.filename,
          checksum
        );

        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "SchemaManager: Error applying " << migration.filename << ": " << e.what();
        return false;
    }
}

std::string SchemaManager::computeChecksum(const std::string &content)
{
    // Task 37 (fulla-sdk-refactor): unified on
    // OpenSslCryptoProvider::sha256Hex() -- the previous implementation was
    // Windows-only BCrypt with a std::hash placeholder on Linux/macOS, which
    // made checksums recorded from a K8s migration Job meaningless. Both the
    // old BCrypt path and sha256Hex() emit lowercase hex, so Windows-recorded
    // checksums stay stable across this change.
    static fulla::drogon::adapters::OpenSslCryptoProvider cryptoProvider;
    return cryptoProvider.sha256Hex(content);
}

}  // namespace schema
