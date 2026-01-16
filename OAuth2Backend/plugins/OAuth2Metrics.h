#pragma once
#include <string>
#include <chrono>

namespace oauth2 {

class Metrics {
public:
    // Counter: oauth2_requests_total{type, code}
    static void incRequest(const std::string& endpoint, int statusCode);

    // Counter: oauth2_login_failures_total{reason}
    static void incLoginFailure(const std::string& reason);

    // Histogram: oauth2_latency_seconds{operation, storage}
    // We will use a helper struct for RAII timing if needed, or just manual call
    static void observeLatency(const std::string& operation, const std::string& storage, double seconds);

    // Gauge: oauth2_active_tokens
    static void updateActiveTokens(int count);
};

// Simple RAII timer
class OperationTimer {
public:
    OperationTimer(std::string op, std::string store) 
        : operation_(std::move(op)), storage_(std::move(store)), start_(std::chrono::steady_clock::now()) {}
    
    ~OperationTimer();

private:
    std::string operation_;
    std::string storage_;
    std::chrono::steady_clock::time_point start_;
};

}
