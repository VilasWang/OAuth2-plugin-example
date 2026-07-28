#pragma once

// OAuth2Server/test/contract/ContractFixtures.h
//
// Spec: authforge-sdk-refactor -- Task 12 (分档契约测试套件, design.md §7.3 / F5).
//
// Shared plumbing for the tiered repository contract tests
// (ClientRepositoryContractTest.cc / GrantRepositoryContractTest.cc /
// TokenRepositoryContractTest.cc / ConsentRepositoryContractTest.cc).
//
// PARAMETERIZATION APPROACH (see task summary for the full rationale):
// DROGON_TEST (the framework this backend suite is built on, see
// <drogon/drogon_test.h> and every other test under OAuth2Server/test/) has
// NO parameterized-test facility -- it is a flat macro that expands to one
// DrObject-derived class per invocation, discovered via DrClassMap and run by
// name (`-r <exact-name>`, verified against the actual drogon_test.cc
// shipped by this project's pinned Conan drogon package: the runner does
// `test->name() == targetTest`, an EXACT match, not a prefix/substring
// match). There is no official "table test" or "parameterized case" macro to
// reach for.
//
// This file's answer to that gap: shared, backend-agnostic assertion
// functions (`runXxxContract(TEST_CTX, repo, ...)`) that take a
// `std::shared_ptr<drogon::test::Case> TEST_CTX` as their first parameter --
// mirroring the existing convention already used by
// OAuth2Server/test/integration/error/ApplicationEndpointErrorEnvelopeTest.cc
// (`assertErrorEnvelope(TEST_CTX, ...)`) and
// OAuth2ProtocolEndpointRfcComplianceTest.cc (`assertRfc6749ErrorBody`,
// `assertLiveRfcError`). CHECK/REQUIRE are macros that reference an
// in-scope identifier literally named `TEST_CTX` (see
// drogon/drogon_test.h's ERROR_MSG/TEST_INTERNAL__ macros); as long as a
// parameter or local variable is named exactly `TEST_CTX`, those macros work
// correctly from inside a plain helper function, not just inside a
// DROGON_TEST(...) body. That is what makes this whole approach work without
// any custom macro machinery.
//
// Each backend x each interface then gets its own tiny `DROGON_TEST(...)`
// case that just constructs that backend's concrete repository and forwards
// to the shared assertion function -- this is the "N backends x M shared
// behaviors" cross product materialized as N*M discrete named test cases
// (required anyway, since CTest-level filtering by label needs individually
// add_test()-registered names -- see OAuth2Server/test/CMakeLists.txt's
// CONTRACT_TEST_NAMES loop).

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <drogon/nosql/RedisClient.h>

#include <authforge/drogon/plugin/OAuth2Plugin.h>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace oauth2::test::contract
{

// ---------------------------------------------------------------------------
// Backend availability / skip helpers
//
// Mirrors the existing pattern in
// OAuth2Server/test/integration/storage/{Postgres,Redis}StorageTest.cc: a
// missing DB/Redis client (memory-only CI legs, local dev without those
// services) means the corresponding backend's contract tests SKIP (return
// early, record zero assertions) rather than fail. This is a deliberate,
// pre-existing convention this task follows rather than invents.
// ---------------------------------------------------------------------------

/**
 * @brief Returns the "default" Postgres DbClient, or nullptr if unavailable
 * (no configured client, connection refused, etc). Callers should
 * `if (!client) return;` to skip the calling DROGON_TEST case.
 *
 * Bug fix: the memory-only test run (config.ci.json, "storage_type":
 * "memory", "db_clients": []) has NO db_clients configured at all.
 * drogon::app().getDbClient() on an empty db_clients list does not throw a
 * catchable exception in every Drogon build -- some paths assert()
 * directly (see drogon::nosql::RedisClientManager::getRedisClient's
 * `assert(redisClientsMap_.find(name) != ...)` for the Redis analog,
 * which crashes the whole test process rather than being caught below).
 * The pre-existing convention in
 * OAuth2Server/test/integration/storage/{Postgres,Redis}StorageTest.cc
 * guards this with an explicit storage-type check BEFORE ever calling
 * getDbClient()/getRedisClient() -- this function previously only had the
 * try/catch half of that pattern, not the storage-type check, so it could
 * still crash the process during a memory-only run instead of skipping
 * cleanly. Mirrors that pre-existing pattern now.
 */
inline drogon::orm::DbClientPtr getPostgresClientOrNull()
{
    auto plugin = drogon::app().getPlugin<::OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        return nullptr;
    }

    try
    {
        return drogon::app().getDbClient();
    }
    catch (...)
    {
        LOG_WARN << "[contract] Postgres DbClient not available. Skipping Postgres contract test.";
        return nullptr;
    }
}

/**
 * @brief Returns the "default" Redis client, or nullptr if unavailable.
 * Callers should `if (!client) return;` to skip the calling DROGON_TEST case.
 *
 * Bug fix: see getPostgresClientOrNull()'s doc comment -- same storage-type
 * guard needed before calling drogon::app().getRedisClient(), since a
 * memory-only run's "redis_clients": [] means
 * drogon::nosql::RedisClientManager::getRedisClient() hits its internal
 * `assert(redisClientsMap_.find(name) != redisClientsMap_.end())` (an
 * uncatchable process-terminating assert, not a throw) rather than failing
 * gracefully.
 */
inline drogon::nosql::RedisClientPtr getRedisClientOrNull()
{
    auto plugin = drogon::app().getPlugin<::OAuth2Plugin>();
    if (plugin && plugin->getStorageType() == "memory")
    {
        return nullptr;
    }

    try
    {
        return drogon::app().getRedisClient("default");
    }
    catch (...)
    {
        LOG_WARN << "[contract] Redis client not available. Skipping Redis contract test.";
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Async-callback waiters
//
// Every IXxxRepository method is callback-based (design.md §7: "Implementations
// use ASYNCHRONOUS CALLBACKS"). Shared assertion functions need to block the
// test thread until a callback fires, exactly like every existing storage
// integration test (PostgresStorageTest.cc / RedisStorageTest.cc /
// MemoryStorageTest.cc) already does by hand with std::promise/std::future.
// These two helpers factor that pattern out instead of repeating it at every
// call site.
// ---------------------------------------------------------------------------

constexpr int kWaitTimeoutSeconds = 30;

/**
 * @brief Invoke `op` with a callback that fulfills a promise<T>, block for
 * the result (30s timeout matching the rest of the suite), and return it.
 * Throws std::runtime_error on timeout (same failure mode as the existing
 * storage tests -- a hang is a real bug, not a soft-fail condition).
 */
template <typename T, typename Op>
T waitForValue(Op &&op)
{
    std::promise<T> p;
    auto f = p.get_future();
    op([&p](T value) { p.set_value(std::move(value)); });
    if (f.wait_for(std::chrono::seconds(kWaitTimeoutSeconds)) == std::future_status::timeout)
    {
        throw std::runtime_error("contract test TIMEOUT waiting for async callback");
    }
    return f.get();
}

/**
 * @brief Same as waitForValue, but for VoidCallback-shaped operations
 * (`std::function<void()>`).
 */
template <typename Op>
void waitForVoid(Op &&op)
{
    std::promise<void> p;
    auto f = p.get_future();
    op([&p]() { p.set_value(); });
    if (f.wait_for(std::chrono::seconds(kWaitTimeoutSeconds)) == std::future_status::timeout)
    {
        throw std::runtime_error("contract test TIMEOUT waiting for async callback");
    }
    f.get();
}

// ---------------------------------------------------------------------------
// Unique-id generation
//
// Postgres/Redis fixture data persists across test runs (real DB/Redis
// instances, not reset between invocations the way an in-process Memory
// repository is). Every contract test that writes fixture data generates a
// fresh, collision-resistant identifier per call so repeated CI/local runs
// against the same Postgres/Redis instance don't collide with leftover rows
/// keys from a prior run (tests also clean up after themselves where
// practical -- see each contract test file -- but a unique suffix is the
// primary defense, matching the "test_pg_code_123"-style fixed literals in
// the pre-existing storage tests being upgraded here to be collision-safe
// across repeated runs).
// ---------------------------------------------------------------------------

inline std::string uniqueSuffix()
{
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::to_string(now) + "_" + std::to_string(tid % 100000);
}

inline int64_t nowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch()
    )
      .count();
}

}  // namespace oauth2::test::contract
