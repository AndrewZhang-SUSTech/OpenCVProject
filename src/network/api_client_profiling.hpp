#pragma once

#include <chrono>
#include <string>
#include "../common/types.hpp"
#include "api_client.hpp"

namespace Network {

    // Detailed timing metrics for performance analysis
    struct ProfilingMetrics {
        double captcha_get_ms = 0;      // Time spent on GET /api/captcha/get
        double captcha_parse_ms = 0;    // Time spent on JSON parsing and image decoding
        double captcha_solve_ms = 0;    // Time spent on OpenCV solver
        double captcha_encrypt_ms = 0;  // Time spent on AES encryption
        double captcha_check_ms = 0;    // Time spent on POST /api/captcha/check
        double total_network_ms = 0;    // Aggregate network transmission time
        double total_cpu_ms = 0;        // Aggregate CPU processing time
        double total_ms = 0;            // Total end-to-end execution time
    };

    // Profiling version of get_verified_token with detailed timing
    async_simple::coro::Lazy<Result<std::pair<std::string, ProfilingMetrics>>> get_verified_token_profiling(
        coro_http::coro_http_client& client, const User& user,
        const std::string& base_url = "https://reservation.sustech.edu.cn");

}  // namespace Network
