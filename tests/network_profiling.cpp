#include <async_simple/coro/Collect.h>
#include <async_simple/coro/SyncAwait.h>
#include <ylt/util/concurrentqueue.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <numeric>
#include <vector>
#include "../src/common/log.hpp"
#include "../src/common/types.hpp"
#include "../src/network/api_client.hpp"
#include "../src/network/api_client_profiling.hpp"

// Performance metrics structure
struct RequestMetrics {
    double total_time_ms;
    double network_time_ms;
    double cpu_time_ms;
    size_t bytes_received;
    bool success;

    // Detailed breakdown (for CAPTCHA profiling)
    double captcha_get_ms = 0;
    double captcha_parse_ms = 0;
    double captcha_check_ms = 0;
    double captcha_encrypt_ms = 0;
};

struct AggregatedStats {
    size_t total_requests;
    size_t successful_requests;
    double total_wall_time_ms;
    double min_ms, max_ms, mean_ms, median_ms, p95_ms, p99_ms, stdev_ms;
    double requests_per_sec;
    double avg_network_time_ms;
    double avg_cpu_time_ms;
    double total_bytes_received;
    double bandwidth_mbps;

    // Detailed CAPTCHA breakdown
    double avg_captcha_get_ms = 0;
    double avg_captcha_parse_ms = 0;
    double avg_captcha_check_ms = 0;
    double avg_captcha_encrypt_ms = 0;
};

void print_stats(const std::string& test_name, const AggregatedStats& stats) {
    std::cout << "\n=== " << test_name << " ===" << std::endl;
    std::cout << "Total Wall Time:     " << stats.total_wall_time_ms << " ms" << std::endl;
    std::cout << "Total Requests:      " << stats.total_requests << std::endl;
    std::cout << "Successful:          " << stats.successful_requests << std::endl;
    std::cout << "Throughput:          " << stats.requests_per_sec << " req/s" << std::endl;
    std::cout << "\nLatency (ms):" << std::endl;
    std::cout << "  Min:     " << stats.min_ms << std::endl;
    std::cout << "  Max:     " << stats.max_ms << std::endl;
    std::cout << "  Mean:    " << stats.mean_ms << std::endl;
    std::cout << "  Median:  " << stats.median_ms << std::endl;
    std::cout << "  Stdev:   " << stats.stdev_ms << std::endl;
    std::cout << "  P95:     " << stats.p95_ms << std::endl;
    std::cout << "  P99:     " << stats.p99_ms << std::endl;

    if (stats.avg_network_time_ms > 0) {
        std::cout << "\nTime Breakdown:" << std::endl;
        std::cout << "  Avg Network Time: " << stats.avg_network_time_ms << " ms ("
                  << (stats.avg_network_time_ms / stats.mean_ms * 100) << "%)" << std::endl;
        std::cout << "  Avg CPU Time:     " << stats.avg_cpu_time_ms << " ms ("
                  << (stats.avg_cpu_time_ms / stats.mean_ms * 100) << "%)" << std::endl;

        // Show detailed CAPTCHA breakdown if available
        if (stats.avg_captcha_get_ms > 0) {
            std::cout << "\nDetailed CAPTCHA Breakdown:" << std::endl;
            std::cout << "  GET /captcha/get:   " << stats.avg_captcha_get_ms << " ms" << std::endl;
            std::cout << "  Parse + Solve:      " << stats.avg_captcha_parse_ms << " ms" << std::endl;
            std::cout << "  POST /captcha/check:" << stats.avg_captcha_check_ms << " ms" << std::endl;
            std::cout << "  Final Encrypt:      " << stats.avg_captcha_encrypt_ms << " ms" << std::endl;
        }
    }

    if (stats.total_bytes_received > 0) {
        std::cout << "\nBandwidth:" << std::endl;
        std::cout << "  Total Data:       " << (stats.total_bytes_received / 1024.0 / 1024.0) << " MB" << std::endl;
        std::cout << "  Bandwidth:        " << stats.bandwidth_mbps << " Mbps" << std::endl;
    }
    std::cout << "================================\n" << std::endl;
}

AggregatedStats calculate_stats(const std::vector<RequestMetrics>& metrics, double wall_time_ms) {
    AggregatedStats stats{};

    if (metrics.empty()) {
        return stats;
    }

    stats.total_requests = metrics.size();
    stats.total_wall_time_ms = wall_time_ms;
    stats.successful_requests = std::count_if(metrics.begin(), metrics.end(), [](const auto& m) { return m.success; });

    std::vector<double> times;
    double network_sum = 0, cpu_sum = 0;
    double captcha_get_sum = 0, captcha_parse_sum = 0, captcha_check_sum = 0, captcha_encrypt_sum = 0;

    for (const auto& m : metrics) {
        times.push_back(m.total_time_ms);
        network_sum += m.network_time_ms;
        cpu_sum += m.cpu_time_ms;
        stats.total_bytes_received += m.bytes_received;

        // Accumulate detailed breakdown
        captcha_get_sum += m.captcha_get_ms;
        captcha_parse_sum += m.captcha_parse_ms;
        captcha_check_sum += m.captcha_check_ms;
        captcha_encrypt_sum += m.captcha_encrypt_ms;
    }

    std::sort(times.begin(), times.end());

    stats.min_ms = times.front();
    stats.max_ms = times.back();
    stats.mean_ms = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
    stats.median_ms = times[times.size() / 2];
    stats.p95_ms = times[(size_t)(times.size() * 0.95)];
    stats.p99_ms = times[(size_t)(times.size() * 0.99)];

    double sq_sum = std::inner_product(times.begin(), times.end(), times.begin(), 0.0);
    stats.stdev_ms = std::sqrt(sq_sum / times.size() - stats.mean_ms * stats.mean_ms);

    stats.requests_per_sec = (stats.total_requests / wall_time_ms) * 1000.0;
    stats.avg_network_time_ms = network_sum / metrics.size();
    stats.avg_cpu_time_ms = cpu_sum / metrics.size();
    stats.bandwidth_mbps = (stats.total_bytes_received * 8.0 / 1000000.0) / (wall_time_ms / 1000.0);

    // Calculate detailed breakdown averages
    stats.avg_captcha_get_ms = captcha_get_sum / metrics.size();
    stats.avg_captcha_parse_ms = captcha_parse_sum / metrics.size();
    stats.avg_captcha_check_ms = captcha_check_sum / metrics.size();
    stats.avg_captcha_encrypt_ms = captcha_encrypt_sum / metrics.size();

    return stats;
}

// Forward declarations
async_simple::coro::Lazy<AggregatedStats> test_pure_network(int total_requests, int concurrency);
async_simple::coro::Lazy<AggregatedStats> test_captcha_with_timing(const User& user, int total_requests,
                                                                   int concurrency);

// Test 1: Pure network test - simple GET request to measure server response time
async_simple::coro::Lazy<AggregatedStats> test_pure_network(int total_requests, int concurrency) {
    std::cout << "\n[Test 1] Pure Network Test - GET request only" << std::endl;
    std::cout << "This measures pure server response time without CPU processing" << std::endl;

    ylt::detail::moodycamel::ConcurrentQueue<RequestMetrics> metrics_queue;
    std::atomic<int> current_idx{0};

    auto start_total = std::chrono::high_resolution_clock::now();

    auto worker = [&]() -> async_simple::coro::Lazy<void> {
        coro_http::coro_http_client client{};
        client.set_req_timeout(std::chrono::seconds(5));
        while (true) {
            int i = current_idx.fetch_add(1);
            if (i >= total_requests) {
                co_return;
            }

            RequestMetrics metrics{};
            auto start = std::chrono::high_resolution_clock::now();

            // Simple GET request to test pure network performance
            auto result = co_await client.async_get(
                "https://reservation.sustech.edu.cn/api/blade-app/qywx/"
                "getAppGymPageByCustomerId?userid=123456789&token=dummy&current=1&size=50&gymName=");

            auto end = std::chrono::high_resolution_clock::now();

            metrics.total_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
            metrics.network_time_ms = metrics.total_time_ms;  // All time is network time
            metrics.cpu_time_ms = 0;
            metrics.bytes_received = result.resp_body.size();
            metrics.success = (result.status == 200);

            metrics_queue.enqueue(metrics);
        }
    };

    std::vector<async_simple::coro::Lazy<void>> workers;
    workers.reserve(concurrency);
    for (int i = 0; i < concurrency; ++i) {
        workers.push_back(worker());
    }

    co_await async_simple::coro::collectAll(std::move(workers));

    auto end_total = std::chrono::high_resolution_clock::now();
    double wall_time_ms = std::chrono::duration<double, std::milli>(end_total - start_total).count();

    std::vector<RequestMetrics> all_metrics;
    RequestMetrics m;
    while (metrics_queue.try_dequeue(m)) {
        all_metrics.push_back(m);
    }

    co_return calculate_stats(all_metrics, wall_time_ms);
}

int main(int argc, char* argv[]) {
    User user;
    user.name = "TestUser";
    user.id = 123456789;
    user.phone = 13800138000;
    user.token = "dummy_token";

    int total_requests = 200;
    int concurrency = 10;

    if (argc > 1) {
        total_requests = std::stoi(argv[1]);
    }
    if (argc > 2) {
        concurrency = std::stoi(argv[2]);
    }

    std::cout << "========================================" << std::endl;
    std::cout << "Network Performance Profiling Tool" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Config: Total Requests = " << total_requests << ", Concurrency = " << concurrency << std::endl;

    try {
        // Test 1: Pure network performance
        auto stats1 = async_simple::coro::syncAwait(test_pure_network(total_requests, concurrency));
        print_stats("Pure Network Test (GET Request)", stats1);

        std::cout << "\nAnalysis:" << std::endl;
        std::cout << "- If latency is high (>100ms) and throughput is low, the bottleneck is SERVER RESPONSE TIME"
                  << std::endl;
        std::cout << "- If bandwidth is much lower than your internet speed, the server is rate-limiting" << std::endl;

        // Test 2: Full CAPTCHA flow
        std::cout << "\n\nWarming up for CAPTCHA test..." << std::endl;
        try {
            coro_http::coro_http_client warmup_client{};
            auto warmup = async_simple::coro::syncAwait(Network::get_verified_token(warmup_client, user));
        } catch (...) {
        }

        auto stats2 = async_simple::coro::syncAwait(test_captcha_with_timing(user, total_requests / 2, concurrency));
        print_stats("Full CAPTCHA Flow", stats2);

        std::cout << "\nComparison:" << std::endl;
        std::cout << "- Pure Network Mean:  " << stats1.mean_ms << " ms" << std::endl;
        std::cout << "- CAPTCHA Flow Mean:  " << stats2.mean_ms << " ms" << std::endl;
        std::cout << "- Overhead:           " << (stats2.mean_ms - stats1.mean_ms * 2) << " ms (CPU processing)"
                  << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

// Test 2: Full CAPTCHA flow with detailed timing breakdown
async_simple::coro::Lazy<AggregatedStats> test_captcha_with_timing(const User& user, int total_requests,
                                                                   int concurrency) {
    std::cout << "\n[Test 2] Full CAPTCHA Flow with Timing Breakdown" << std::endl;
    std::cout << "This measures network vs CPU time in get_verified_token" << std::endl;

    ylt::detail::moodycamel::ConcurrentQueue<RequestMetrics> metrics_queue;
    std::atomic<int> current_idx{0};

    auto start_total = std::chrono::high_resolution_clock::now();

    auto worker = [&]() -> async_simple::coro::Lazy<void> {
        coro_http::coro_http_client client{};
        while (true) {
            int i = current_idx.fetch_add(1);
            if (i >= total_requests) {
                co_return;
            }

            RequestMetrics metrics{};
            auto start = std::chrono::high_resolution_clock::now();

            // Use profiling version to get detailed timing
            auto result = co_await Network::get_verified_token_profiling(client, user);

            auto end = std::chrono::high_resolution_clock::now();

            metrics.total_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
            metrics.success = result.has_value();

            if (result.has_value()) {
                const auto& prof = result.value().second;
                metrics.captcha_get_ms = prof.captcha_get_ms;
                metrics.captcha_parse_ms = prof.captcha_parse_ms;
                metrics.captcha_check_ms = prof.captcha_check_ms;
                metrics.captcha_encrypt_ms = prof.captcha_encrypt_ms;
                metrics.network_time_ms = prof.total_network_ms;
                metrics.cpu_time_ms = prof.total_cpu_ms;
                metrics.bytes_received = 5000;  // Rough estimate
            } else {
                metrics.network_time_ms = metrics.total_time_ms;
                metrics.cpu_time_ms = 0;
                metrics.bytes_received = 0;
            }

            metrics_queue.enqueue(metrics);
        }
    };

    std::vector<async_simple::coro::Lazy<void>> workers;
    workers.reserve(concurrency);
    for (int i = 0; i < concurrency; ++i) {
        workers.push_back(worker());
    }

    co_await async_simple::coro::collectAll(std::move(workers));

    auto end_total = std::chrono::high_resolution_clock::now();
    double wall_time_ms = std::chrono::duration<double, std::milli>(end_total - start_total).count();

    std::vector<RequestMetrics> all_metrics;
    RequestMetrics m;
    while (metrics_queue.try_dequeue(m)) {
        all_metrics.push_back(m);
    }

    co_return calculate_stats(all_metrics, wall_time_ms);
}
