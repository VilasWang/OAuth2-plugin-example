// Task 15 (authforge-sdk-refactor, design.md §6/§8): pure gtest unit tests
// for FakeMetrics.

#include <authforge/common/testing/FakeMetrics.h>

#include <gtest/gtest.h>

using namespace authforge::common::testing;

TEST(FakeMetricsTest, CapturesIncrementCounter)
{
    FakeMetrics metrics;
    metrics.incrementCounter("requests_total", {{"path", "/token"}}, 1.0);

    ASSERT_EQ(metrics.count(), 1u);
    EXPECT_EQ(metrics.samples()[0].kind, FakeMetrics::Kind::Counter);
    EXPECT_EQ(metrics.samples()[0].name, "requests_total");
    EXPECT_EQ(metrics.samples()[0].labels.at("path"), "/token");
    EXPECT_DOUBLE_EQ(metrics.samples()[0].value, 1.0);
}

TEST(FakeMetricsTest, CapturesGaugeAndHistogram)
{
    FakeMetrics metrics;
    metrics.setGauge("active_tokens", {}, 42.0);
    metrics.observeHistogram("latency_seconds", {{"op", "sign"}}, 0.05);

    ASSERT_EQ(metrics.count(), 2u);
    EXPECT_EQ(metrics.samples()[0].kind, FakeMetrics::Kind::Gauge);
    EXPECT_EQ(metrics.samples()[1].kind, FakeMetrics::Kind::Histogram);
}

TEST(FakeMetricsTest, ClearRemovesSamples)
{
    FakeMetrics metrics;
    metrics.incrementCounter("x", {});
    metrics.clear();
    EXPECT_EQ(metrics.count(), 0u);
}
