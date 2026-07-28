#pragma once

#include <string>

namespace oauth2::test
{

/**
 * @brief Test Category Enum
 * Used to categorize tests for better organization and selective execution.
 */
enum class TestCategory
{
    UNIT,         // Unit tests: No external dependencies (or sandboxed)
    INTEGRATION,  // Integration tests: Multiple components with external dependencies
    E2E,          // End-to-end tests: Full business flows from client perspective
    PERFORMANCE,  // Performance tests: Benchmarks and load tests
    SECURITY,     // Security tests: Injection, bypass, and vulnerability tests
    API,          // API tests: Interface and contract compliance
    DATABASE,     // Database tests: Schema and migration tests
    ACCEPTANCE    // Acceptance tests: User scenario validation
};

/**
 * @brief Test Priority Enum
 */
enum class TestPriority
{
    P0,  // Critical: Blocking release, core functionality
    P1,  // Important: Main branches, edge cases
    P2,  // Normal: Minor features
    P3   // Optional: Lowest priority
};

/**
 * @brief Get category string representation
 */
inline std::string getCategoryString(TestCategory category)
{
    switch (category)
    {
        case TestCategory::UNIT:
            return "Unit";
        case TestCategory::INTEGRATION:
            return "Integration";
        case TestCategory::E2E:
            return "E2E";
        case TestCategory::PERFORMANCE:
            return "Performance";
        case TestCategory::SECURITY:
            return "Security";
        case TestCategory::API:
            return "API";
        case TestCategory::DATABASE:
            return "Database";
        case TestCategory::ACCEPTANCE:
            return "Acceptance";
        default:
            return "Unknown";
    }
}

/**
 * @brief Get priority string representation
 */
inline std::string getPriorityString(TestPriority priority)
{
    switch (priority)
    {
        case TestPriority::P0:
            return "P0";
        case TestPriority::P1:
            return "P1";
        case TestPriority::P2:
            return "P2";
        case TestPriority::P3:
            return "P3";
        default:
            return "PX";
    }
}

}  // namespace oauth2::test
