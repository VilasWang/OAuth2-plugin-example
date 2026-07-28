#include <drogon/drogon_test.h>
#include <string>
#include <vector>

#include "SchemaManager.h"

using schema::SchemaManager;

// Note: CHECK() expands to use drogon_test_ctx_, which only exists inside a
// DROGON_TEST(...) body. So assertions must live directly inside each test
// case below — not in a shared free helper.

// 1. Single statement
DROGON_TEST(Unit_P0_Schema_SplitSql_SingleStatement)
{
    auto r = SchemaManager::splitSqlStatements("CREATE TABLE t (id int);");
    CHECK(r.size() == 1);
    CHECK(r[0] == "CREATE TABLE t (id int);");
}

// 2. Two simple statements
DROGON_TEST(Unit_P0_Schema_SplitSql_TwoStatements)
{
    auto r = SchemaManager::splitSqlStatements("CREATE TABLE a (x int); CREATE TABLE b (y int);");
    CHECK(r.size() == 2);
    CHECK(r[0] == "CREATE TABLE a (x int);");
    CHECK(r[1] == "CREATE TABLE b (y int);");
}

// 3. Semicolon inside single-quoted string does not split
DROGON_TEST(Unit_P0_Schema_SplitSql_SemicolonInString)
{
    auto r =
      SchemaManager::splitSqlStatements("INSERT INTO t VALUES ('a;b'); INSERT INTO t VALUES (2);");
    CHECK(r.size() == 2);
    CHECK(r[0] == "INSERT INTO t VALUES ('a;b');");
    CHECK(r[1] == "INSERT INTO t VALUES (2);");
}

// 4. '' escaped quote keeps the string open across a ';'
DROGON_TEST(Unit_P0_Schema_SplitSql_EscapedQuote)
{
    auto r = SchemaManager::splitSqlStatements("INSERT INTO t VALUES ('a''b;c');");
    CHECK(r.size() == 1);
    CHECK(r[0] == "INSERT INTO t VALUES ('a''b;c');");
}

// 5. Line comment with semicolon does not split
DROGON_TEST(Unit_P0_Schema_SplitSql_LineComment)
{
    auto r = SchemaManager::splitSqlStatements("-- this; is a comment\nSELECT 1;");
    CHECK(r.size() == 1);
    CHECK(r[0] == "-- this; is a comment\nSELECT 1;");
}

// 6. Block comment with semicolon does not split
DROGON_TEST(Unit_P0_Schema_SplitSql_BlockComment)
{
    auto r = SchemaManager::splitSqlStatements("SELECT /* ; */ 1;");
    CHECK(r.size() == 1);
    CHECK(r[0] == "SELECT /* ; */ 1;");
}

// 7. Dollar-quote function body ($$ ... $$) is one statement
DROGON_TEST(Unit_P0_Schema_SplitSql_DollarQuote)
{
    std::string sql =
      "CREATE FUNCTION f() RETURNS void AS $$ "
      "BEGIN IF x; THEN END IF; END; "
      "$$ LANGUAGE plpgsql;";
    auto r = SchemaManager::splitSqlStatements(sql);
    CHECK(r.size() == 1);
    CHECK(r[0] == sql);
}

// 8. Tagged dollar-quote ($body$ ... $body$) is one statement
DROGON_TEST(Unit_P0_Schema_SplitSql_TaggedDollarQuote)
{
    std::string sql =
      "CREATE FUNCTION f() RETURNS void AS $body$ "
      "BEGIN x; END; "
      "$body$ LANGUAGE plpgsql;";
    auto r = SchemaManager::splitSqlStatements(sql);
    CHECK(r.size() == 1);
    CHECK(r[0] == sql);
}

// 9. Trailing whitespace after last semicolon yields no empty statement
DROGON_TEST(Unit_P0_Schema_SplitSql_TrailingWhitespace)
{
    auto r = SchemaManager::splitSqlStatements("SELECT 1;   \n\n");
    CHECK(r.size() == 1);
    CHECK(r[0] == "SELECT 1;");
}

// 10. Empty / whitespace-only input yields zero statements
DROGON_TEST(Unit_P0_Schema_SplitSql_EmptyInput)
{
    auto r = SchemaManager::splitSqlStatements("");
    CHECK(r.empty());

    auto r2 = SchemaManager::splitSqlStatements("   \n\t  \n");
    CHECK(r2.empty());
}
