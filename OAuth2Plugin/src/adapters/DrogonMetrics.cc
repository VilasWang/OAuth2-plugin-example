#include <oauth2/adapters/DrogonMetrics.h>

#include <drogon/drogon.h>

#include <sstream>

namespace authforge::drogon::adapters
{

namespace
{
// Format the metric name + labels into the same "[METRIC] <name> k=v ..."
// log line the legacy authforge::drogon::observability::Metrics statics
// produced, so output consumers (log scrapers, PromExporter readers) see no
// change. value is appended as val= (matching observeLatency/updateActiveTokens).
std::string formatMetric(
  const std::string &name,
  const authforge::common::ports::MetricLabels &labels,
  double value
)
{
    std::ostringstream oss;
    oss << "[METRIC] " << name;
    for (const auto &kv : labels)
    {
        oss << " " << kv.first << "=" << kv.second;
    }
    oss << " val=" << value;
    return oss.str();
}
}  // namespace

void DrogonMetrics::incrementCounter(
  const std::string &name,
  const authforge::common::ports::MetricLabels &labels,
  double value
)
{
    try
    {
        LOG_INFO << formatMetric(name, labels, value);
    }
    catch (...)
    {
    }
}

void DrogonMetrics::setGauge(
  const std::string &name,
  const authforge::common::ports::MetricLabels &labels,
  double value
)
{
    try
    {
        LOG_INFO << formatMetric(name, labels, value);
    }
    catch (...)
    {
    }
}

void DrogonMetrics::observeHistogram(
  const std::string &name,
  const authforge::common::ports::MetricLabels &labels,
  double value
)
{
    try
    {
        LOG_INFO << formatMetric(name, labels, value);
    }
    catch (...)
    {
    }
}

}  // namespace authforge::drogon::adapters
