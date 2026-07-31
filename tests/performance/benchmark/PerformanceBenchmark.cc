#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>
#include <authforge/drogon/utils/SubjectGenerator.h>

using namespace authforge::common::utils;

DROGON_TEST(Performance_P1_Benchmark_SubjectGeneration_StressTest)
{
    const int iterations = 10000;
    std::vector<long long> latencies;
    latencies.reserve(iterations);

    auto start_all = std::chrono::steady_clock::now();

    for (int i = 0; i < iterations; ++i)
    {
        auto start = std::chrono::steady_clock::now();
        std::string subject = SubjectGenerator::forLocalUser("user_" + std::to_string(i));
        auto end = std::chrono::steady_clock::now();

        latencies.push_back(
          std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()
        );

        // Sanity check
        if (i % 1000 == 0)
        {
            CHECK(subject.substr(0, 6) == "local:");
        }
    }

    auto end_all = std::chrono::steady_clock::now();
    auto total_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_all - start_all).count();

    // Calculate statistics
    double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0) / iterations;
    std::sort(latencies.begin(), latencies.end());
    long long p95 = latencies[static_cast<int>(iterations * 0.95)];

    LOG_INFO << "Subject Generation Performance:";
    LOG_INFO << "  Total iterations: " << iterations;
    LOG_INFO << "  Total time: " << total_ms << "ms";
    LOG_INFO << "  Average latency: " << avg << "ns";
    LOG_INFO << "  P95 latency: " << p95 << "ns";

    // Performance requirement: average < 10ms (it's actually nanos here, so 10,000,000ns)
    CHECK(avg < 10000000.0);
}

DROGON_TEST(Performance_P1_Benchmark_SubjectParsing_StressTest)
{
    const int iterations = 10000;
    auto start_all = std::chrono::steady_clock::now();

    for (int i = 0; i < iterations; ++i)
    {
        auto [provider, sub] = SubjectGenerator::parse("google:abc_123_xyz_" + std::to_string(i));
        if (i == 0)
        {
            CHECK(provider == "google");
        }
    }

    auto end_all = std::chrono::steady_clock::now();
    auto total_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_all - start_all).count();

    LOG_INFO << "Subject Parsing Performance:";
    LOG_INFO << "  Total time for " << iterations << " iterations: " << total_ms << "ms";

    CHECK(total_ms < 1000);  // Should be well under 1 second
}
