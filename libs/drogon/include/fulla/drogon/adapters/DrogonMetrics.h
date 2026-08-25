#pragma once

// M8 Task 40 (fulla-sdk-refactor, design.md §5.2 decision b):
// Adapter-side default implementation of fulla::common::ports::IMetrics,
// the Domain-facing metrics port. Mirrors DrogonAuditSink's shape/rationale:
// Drogon-layer controllers (and any future Domain service) emit metrics through
// the IMetrics port (getMetrics()) instead of calling the fulla::drogon::
// observability::Metrics statics directly, keeping the call sites decoupled
// from the concrete log-based emitter.
//
// The port is generic (incrementCounter(name, labels, value)); this adapter
// formats name + labels into the SAME "[METRIC] <name> key=value ..." log line
// the legacy Metrics::* statics produced, so output semantics are unchanged.
// (The legacy Metrics class is retained for now as the OperationTimer RAII
// helper's backing; this adapter is the new primary emission path.)

#include <fulla/common/ports/IMetrics.h>

#include <string>

namespace fulla::drogon::adapters
{

/**
 * @brief Forwards IMetrics calls to Drogon's LOG_ emitter, formatting
 * name + labels into the same "[METRIC] <name> k=v ..." line the legacy
 * Metrics statics produced. Stateless; safe to share one instance across
 * threads (LOG_ is thread-safe, no shared mutable state).
 */
class DrogonMetrics : public fulla::common::ports::IMetrics
{
  public:
    void incrementCounter(
      const std::string &name,
      const fulla::common::ports::MetricLabels &labels,
      double value = 1.0
    ) override;

    void setGauge(
      const std::string &name,
      const fulla::common::ports::MetricLabels &labels,
      double value
    ) override;

    void observeHistogram(
      const std::string &name,
      const fulla::common::ports::MetricLabels &labels,
      double value
    ) override;
};

}  // namespace fulla::drogon::adapters
