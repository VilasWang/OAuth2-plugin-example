#pragma once

// Task 13 (authforge-sdk-refactor, design.md §3.3/§5.1/§5.4/§6): libs/common
// ports. IMetrics is the generic metrics-emission port design.md §5.4
// assigns to Adapter implementations ("Prometheus 导出 | authforge::
// observability::prometheus | 实现 common::IMetrics"), used by Domain code
// that wants to emit a counter/gauge/histogram sample without depending on
// Drogon's PromExporter or any other concrete metrics backend.
//
// Scope note (design.md §5.4's Prometheus-adapter row + §H7 evaluation
// referenced there): the existing authforge::drogon::observability::Metrics
// (libs/drogon/include/authforge/drogon/observability/Metrics.h) is a static
// class with hard-coded metric names/labels (oauth2_requests_total,
// oauth2_login_failures_total, etc), and all 4 existing config.*.json files
// use Drogon's native drogon::plugin::PromExporter directly with no custom
// exporter -- design.md notes there is "无自研导出器，无「双轨」冲突" and
// that a libs/observability-prometheus package "仅在需脱 Drogon metrics 时
// 才做". This port is therefore declared now (Task 13, so the shape exists
// for any Domain code migrated in Task 14+ that wants to depend on it) but
// deliberately generic (name + label map, not oauth2-specific method names
// like incLoginFailure) rather than a 1:1 port of Metrics's current
// hard-coded methods -- a 1:1 port would just move Domain-specific metric
// naming into common, which is not what a shared-kernel port should carry.
// The decision of whether/when to actually wire an implementation of this
// port into the existing call sites is out of scope for Task 13.

#include <string>
#include <unordered_map>

namespace authforge::common::ports
{

/// Metric label set (name -> value), passed by const-ref so callers can
/// build it inline (e.g. `{{"client_id", clientId}}`) at each call site.
using MetricLabels = std::unordered_map<std::string, std::string>;

/**
 * @brief Generic metrics-emission port for Domain code. The default
 * production implementation is Adapter-side (design.md §5.4: Prometheus
 * exporter backed by drogon::plugin::PromExporter or a bespoke exporter);
 * a test double can capture emitted samples without any metrics backend.
 */
class IMetrics
{
  public:
    virtual ~IMetrics() = default;

    /// Increment a counter metric by `value` (default 1).
    virtual void incrementCounter(
      const std::string &name,
      const MetricLabels &labels,
      double value = 1.0
    ) = 0;

    /// Set a gauge metric to `value`.
    virtual void setGauge(const std::string &name, const MetricLabels &labels, double value) = 0;

    /// Record an observation for a histogram metric.
    virtual void observeHistogram(
      const std::string &name,
      const MetricLabels &labels,
      double value
    ) = 0;
};

}  // namespace authforge::common::ports
