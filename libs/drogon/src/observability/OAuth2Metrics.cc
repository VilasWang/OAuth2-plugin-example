#include <authforge/drogon/observability/OAuth2Metrics.h>
#include <drogon/drogon.h>
#include <drogon/plugins/PromExporter.h>

using namespace drogon;

namespace authforge::drogon::observability
{

// Thread-safety contract for this component is documented on the Metrics class
// declaration in OAuth2Metrics.h. In short: metric counters MUST be
// thread-safe; any future process-wide shared counter MUST use std::atomic<...>
// (or a thread-safe metrics library such as Drogon's PromExporter
// Counter/Gauge, already configured in config.json). Do NOT introduce
// non-atomic shared mutable state here. The current implementation is log-based
// (LOG_INFO, thread-safe) and holds no shared mutable counter, so there is no
// data race.

void Metrics::incRequest(const std::string &endpoint, int statusCode)
{
    // Labels: endpoint, status.
    // Log-based metric (no shared mutable counter). If a real shared counter is
    // ever introduced here, it MUST be std::atomic<...> or routed through the
    // PromExporter Counter/Gauge — see the contract above. Do NOT add
    // non-atomic shared mutable state.
    try
    {
        static std::string metric_req = "oauth2_requests_total";
        LOG_INFO << "[METRIC] " << metric_req << " endpoint=" << endpoint
                 << " status=" << statusCode;
    }
    catch (...)
    {
    }
}

void Metrics::incLoginFailure(const std::string &reason)
{
    LOG_INFO << "[METRIC] oauth2_login_failures_total reason=" << reason;
}

void Metrics::observeLatency(
  const std::string &operation,
  const std::string &storage,
  double seconds
)
{
    LOG_INFO << "[METRIC] oauth2_latency_seconds operation=" << operation << " storage=" << storage
             << " val=" << seconds;
}

void Metrics::updateActiveTokens(int count)
{
    LOG_INFO << "[METRIC] oauth2_active_tokens val=" << count;
}

// ========== P1: Token Introspection & Revocation Metrics ==========

void Metrics::incrementIntrospectRequests(const std::string &clientId)
{
    LOG_INFO << "[METRIC] oauth2_introspect_requests_total client_id=" << clientId;
}

void Metrics::incrementIntrospectErrors(const std::string &clientId, const std::string &error)
{
    LOG_INFO << "[METRIC] oauth2_introspect_errors_total client_id=" << clientId
             << " error=" << error;
}

void Metrics::incrementRevocationRequests(const std::string &clientId)
{
    LOG_INFO << "[METRIC] oauth2_revocation_requests_total client_id=" << clientId;
}

void Metrics::incrementRevocationErrors(const std::string &clientId, const std::string &error)
{
    LOG_INFO << "[METRIC] oauth2_revocation_errors_total client_id=" << clientId
             << " error=" << error;
}

OperationTimer::~OperationTimer()
{
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = end - start_;
    Metrics::observeLatency(operation_, storage_, diff.count());
}

}  // namespace authforge::drogon::observability
