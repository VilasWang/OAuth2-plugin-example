#pragma once

// Task 15 (fulla-sdk-refactor, design.md §6/§8): fake implementations
// of common::ports interfaces. See FakeClock.h for the placement
// rationale.
//
// FakeMetrics: a capturing IMetrics that records every emitted sample
// instead of forwarding to a real metrics backend, so a test can assert
// on exactly what Domain code reported.

#include <fulla/common/ports/IMetrics.h>

#include <string>
#include <vector>

namespace fulla::common::testing
{

class FakeMetrics : public fulla::common::ports::IMetrics
{
  public:
    enum class Kind
    {
        Counter,
        Gauge,
        Histogram,
    };

    struct Sample
    {
        Kind kind;
        std::string name;
        fulla::common::ports::MetricLabels labels;
        double value;
    };

    void incrementCounter(
      const std::string &name,
      const fulla::common::ports::MetricLabels &labels,
      double value = 1.0
    ) override
    {
        samples_.push_back(Sample{Kind::Counter, name, labels, value});
    }

    void setGauge(
      const std::string &name,
      const fulla::common::ports::MetricLabels &labels,
      double value
    ) override
    {
        samples_.push_back(Sample{Kind::Gauge, name, labels, value});
    }

    void observeHistogram(
      const std::string &name,
      const fulla::common::ports::MetricLabels &labels,
      double value
    ) override
    {
        samples_.push_back(Sample{Kind::Histogram, name, labels, value});
    }

    /// All captured samples, in emission order.
    const std::vector<Sample> &samples() const
    {
        return samples_;
    }

    /// Number of captured samples.
    size_t count() const
    {
        return samples_.size();
    }

    /// Discard all captured samples.
    void clear()
    {
        samples_.clear();
    }

  private:
    std::vector<Sample> samples_;
};

}  // namespace fulla::common::testing
