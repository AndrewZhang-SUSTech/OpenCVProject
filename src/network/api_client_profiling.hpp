#pragma once

#include <chrono>
#include <string>
#include "api_client.hpp"
#include "../common/types.hpp"

namespace Network {

// Detailed timing breakdown for profiling
struct ProfilingMetrics {
    double captcha_get_ms = 0;          // Time for GET /api/captcha/get
    double captcha_parse_ms = 0;        // Time for JSON parsing
    double captcha_solve_ms = 0;        // Time for CAPTCHA solving (OpenCV)
    double captcha_encrypt_ms = 0;      // Time for AES encryption
    double captcha_check_ms = 0;        // Time for POST /api/captcha/check
    double total_network_ms = 0;        // Total network time
    double total_cpu_ms = 0;            // Total CPU time
    double total_ms = 0;                // Total end-to-end time
};

// Profiling version of get_verified_token with detailed timing
async_simple::coro::Lazy<Result<std::pair<std::string, ProfilingMetrics>>>
get_verified_token_profiling(coro_http::coro_http_client& client, const User& user);

}  // namespace Network
