#include "SchemaManager.h"
#include <drogon/drogon.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>
#include <iomanip>

// Platform-specific SHA-256 implementation
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

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
    LOG_INFO << "SchemaManager: Starting migration from " << migrationsDir;

    auto db = drogon::app().getDbClient();
    if (!db)
    {
        LOG_ERROR << "SchemaManager: No database client available";
        return false;
    }

    // Step 1: Ensure schema_migrations table exists
    if (!ensureMigrationsTable(db))
    {
        return false;
    }

    // Step 2: Scan for migration files
    auto migrations = scanMigrationFiles(migrationsDir);
    if (migrations.empty())
    {
        LOG_INFO << "SchemaManager: No migration files found in " << migrationsDir;
        return true;
    }

    // Step 3: Get already-applied versions
    auto applied = getAppliedVersions(db);

    // Step 4: Filter and apply unapplied migrations
    int appliedCount = 0;
    for (const auto &migration : migrations)
    {
        bool alreadyApplied =
          std::find(applied.begin(), applied.end(), migration.version) != applied.end();
        if (alreadyApplied)
        {
            continue;
        }

        // Step 5: Apply and record
        if (!applyMigration(db, migration))
        {
            LOG_ERROR << "SchemaManager: Migration V" << std::to_string(migration.version)
                      << " failed. Stopping.";
            return false;
        }
        ++appliedCount;
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
  const drogon::orm::DbClientPtr &db,
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
        // Execute migration SQL within a transaction. PostgreSQL prepared
        // statements accept only one statement, so split the file on
        // top-level semicolons and execute each statement separately.
        // The whole migration runs inside one transaction: if any statement
        // fails, the transaction rolls back and the version is not recorded.
        auto trans = db->newTransaction();

        auto statements = splitSqlStatements(sql);
        for (const auto &stmt : statements)
        {
            trans->execSqlSync(stmt);
        }

        // Record the migration
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
#ifdef _WIN32
    // Use Windows BCrypt for SHA-256
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    NTSTATUS status;
    UCHAR hash[32];  // SHA-256 = 32 bytes

    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status))
        return "error";

    status = BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status))
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "error";
    }

    status = BCryptHashData(
      hHash,
      reinterpret_cast<PUCHAR>(const_cast<char *>(content.data())),
      static_cast<ULONG>(content.size()),
      0
    );
    if (!BCRYPT_SUCCESS(status))
    {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "error";
    }

    status = BCryptFinishHash(hHash, hash, sizeof(hash), 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status))
        return "error";

    // Convert to hex string
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 32; ++i)
    {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
#else
    // Fallback: simple hash for non-Windows (use OpenSSL in production)
    // This is a placeholder — on Linux/Mac, link against OpenSSL
    std::hash<std::string> hasher;
    auto h = hasher(content);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << h;
    return oss.str();
#endif
}

}  // namespace schema
