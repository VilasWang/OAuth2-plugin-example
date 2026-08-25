#pragma once
#include <string>
#include <chrono>

namespace fulla::drogon::observability
{

// =============================================================================
// THREAD-SAFETY CONTRACT (defect 1.7 guardrail — preventive, latent)
// -----------------------------------------------------------------------------
// Metrics are emitted concurrently from multiple Drogon IO-loop threads.
//
// CONTRACT:
//   * Metric counters MUST be thread-safe.
//   * Any future process-wide shared counter MUST use std::atomic<...>, or a
//     thread-safe metrics library (e.g. Drogon's PromExporter Counter/Gauge,
//     already configured in config.json).
//   * Do NOT introduce non-atomic shared mutable state in this component.
//
// Current implementation is log-based (LOG_INFO), which is thread-safe and
// holds no shared mutable counter, so there is no data race today. This
// contract exists to prevent a race from being introduced if a real counter is
// added later.
// =============================================================================
class Metrics
{
  public:
    // Counter: oauth2_requests_total{type, code}
    static void incRequest(const std::string &endpoint, int statusCode);

    // Counter: oauth2_login_failures_total{reason}
    static void incLoginFailure(const std::string &reason);

    // Histogram: oauth2_latency_seconds{operation, storage}
    // We will use a helper struct for RAII timing if needed, or just manual
    // call
    static void observeLatency(
      const std::string &operation,
      const std::string &storage,
      double seconds
    );

    // Gauge: oauth2_active_tokens
    static void updateActiveTokens(int count);

    // ========== P1: Token Introspection & Revocation Metrics ==========

    // Counter: oauth2_introspect_requests_total{client_id}
    static void incrementIntrospectRequests(const std::string &clientId);

    // Counter: oauth2_introspect_errors_total{client_id, error}
    static void incrementIntrospectErrors(const std::string &clientId, const std::string &error);

    // Counter: oauth2_revocation_requests_total{client_id}
    static void incrementRevocationRequests(const std::string &clientId);

    // Counter: oauth2_revocation_errors_total{client_id, error}
    static void incrementRevocationErrors(const std::string &clientId, const std::string &error);
};

// Simple RAII timer
class OperationTimer
{
  public:
    OperationTimer(std::string op, std::string store)
        : operation_(std::move(op)),
          storage_(std::move(store)),
          start_(std::chrono::steady_clock::now())
    {
    }

    ~OperationTimer();

  private:
    std::string operation_;
    std::string storage_;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace fulla::drogon::observability
