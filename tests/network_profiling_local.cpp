#include <async_simple/coro/Collect.h>
#include <async_simple/coro/SyncAwait.h>
#include <ylt/util/concurrentqueue.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>
#include <ylt/coro_http/coro_http_client.hpp>
#include <ylt/coro_http/coro_http_server.hpp>
#include "../src/common/log.hpp"
#include "../src/common/types.hpp"
#include "../src/network/api_client.hpp"
#include "../src/network/api_client_profiling.hpp"
#include "../src/utils/crypto.hpp"

// Performance metrics for single request measurement
struct RequestMetrics {
    double total_time_ms;
    double network_time_ms;
    double cpu_time_ms;
    size_t bytes_received;
    bool success;

    // Detailed CAPTCHA processing breakdown
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
    std::cout << "\nTime Breakdown:" << std::endl;
    std::cout << "  Avg Network Time: " << stats.avg_network_time_ms << " ms" << std::endl;
    std::cout << "  Avg CPU Time:     " << stats.avg_cpu_time_ms << " ms" << std::endl;

    if (stats.avg_captcha_get_ms > 0) {
        std::cout << "\nDetailed CAPTCHA Breakdown:" << std::endl;
        std::cout << "  GET /captcha/get:   " << stats.avg_captcha_get_ms << " ms" << std::endl;
        std::cout << "  Parse + Solve:      " << stats.avg_captcha_parse_ms << " ms" << std::endl;
        std::cout << "  POST /captcha/check:" << stats.avg_captcha_check_ms << " ms" << std::endl;
        std::cout << "  Final Encrypt:      " << stats.avg_captcha_encrypt_ms << " ms" << std::endl;
    }
    std::cout << "================================\n" << std::endl;
}

AggregatedStats calculate_stats(const std::vector<RequestMetrics>& metrics, double wall_time_ms) {
    AggregatedStats stats{};
    if (metrics.empty()) return stats;

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

    // Simple stdev calc
    double sq_sum = std::inner_product(times.begin(), times.end(), times.begin(), 0.0);
    stats.stdev_ms = std::sqrt(sq_sum / times.size() - stats.mean_ms * stats.mean_ms);

    stats.requests_per_sec = (stats.total_requests / wall_time_ms) * 1000.0;
    stats.avg_network_time_ms = network_sum / metrics.size();
    stats.avg_cpu_time_ms = cpu_sum / metrics.size();

    stats.avg_captcha_get_ms = captcha_get_sum / metrics.size();
    stats.avg_captcha_parse_ms = captcha_parse_sum / metrics.size();
    stats.avg_captcha_check_ms = captcha_check_sum / metrics.size();
    stats.avg_captcha_encrypt_ms = captcha_encrypt_sum / metrics.size();

    return stats;
}

// Global Image Data
std::string jigsaw_b64;
std::string original_b64;
std::string captcha_response_json;

std::string read_file_as_base64(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!f.read(buffer.data(), size)) {
        throw std::runtime_error("Failed to read file: " + path);
    }

    std::vector<uint8_t> ubuffer(buffer.begin(), buffer.end());
    return Crypto::base64_encode(ubuffer);
}

void init_mock_data() {
    std::cout << "Loading test images..." << std::endl;
    // Assume run from project root, or try relative paths
    std::string jigsaw_path = "tests/jigsaw_image.png";
    std::string original_path = "tests/original_image.png";

    if (!std::filesystem::exists(jigsaw_path)) {
        // Fallback or absolute path check if needed
        std::cerr << "Warning: " << jigsaw_path << " not found!" << std::endl;
    }

    jigsaw_b64 = read_file_as_base64(jigsaw_path);
    original_b64 = read_file_as_base64(original_path);

    // Pre-construct the JSON response
    captcha_response_json = fmt::format(
        R"({{
            "success": true,
            "repCode": "0000",
            "repMsg": null,
            "repData": {{
                "originalImageBase64": "{}",
                "jigsawImageBase64": "{}",
                "token": "MOCK_TOKEN_123456",
                "secretKey": "MOCK_SECRET_KEY_123456"
            }}
        }})",
        original_b64, jigsaw_b64);

    std::cout << "Mock data initialized. Response size: " << captcha_response_json.size() << " bytes" << std::endl;
}

void start_mock_server() {
    // Start local HTTP server in a separate thread to handle mock requests
    std::thread t([]() {
        coro_http::coro_http_server server(1, 9000);

        server.set_http_handler<coro_http::GET>(
            "/api/blade-app/qywx/getAppGymPageByCustomerId",
            [](coro_http::coro_http_request& req, coro_http::coro_http_response& resp) {
                // Return dummy data of typical size
                resp.set_status_and_content(coro_http::status_type::ok,
                                            R"({"code": 200, "success": true, "data": {"records": []}})");
            });

        server.set_http_handler<coro_http::POST>(
            "/api/captcha/get", [](coro_http::coro_http_request& req, coro_http::coro_http_response& resp) {
                resp.set_status_and_content(coro_http::status_type::ok, captcha_response_json);
            });

        server.set_http_handler<coro_http::POST>(
            "/api/captcha/check", [](coro_http::coro_http_request& req, coro_http::coro_http_response& resp) {
                resp.set_status_and_content(coro_http::status_type::ok,
                                            R"({"success": true, "repCode": "0000", "repMsg": "OK"})");
            });

        std::cout << "Mock server started on http://127.0.0.1:9000" << std::endl;
        server.sync_start();
    });
    t.detach();
    // Give it a moment to start
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

// Test 1: Pure Network
async_simple::coro::Lazy<AggregatedStats> test_pure_network(int total_requests, int concurrency,
                                                            const std::string& base_url) {
    ylt::detail::moodycamel::ConcurrentQueue<RequestMetrics> metrics_queue;
    std::atomic<int> current_idx{0};

    auto start_total = std::chrono::high_resolution_clock::now();

    auto worker = [&]() -> async_simple::coro::Lazy<void> {
        coro_http::coro_http_client client{};
        // No keep-alive for strict RTT measurement? Or use default.
        while (true) {
            int i = current_idx.fetch_add(1);
            if (i >= total_requests) co_return;

            RequestMetrics metrics{};
            auto start = std::chrono::high_resolution_clock::now();

            auto result = co_await client.async_get(
                base_url + "/api/blade-app/qywx/getAppGymPageByCustomerId?userid=123&token=dummy");

            auto end = std::chrono::high_resolution_clock::now();

            metrics.total_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
            metrics.network_time_ms = metrics.total_time_ms;
            metrics.success = (result.status == 200);
            metrics_queue.enqueue(metrics);
        }
    };

    std::vector<async_simple::coro::Lazy<void>> workers;
    for (int i = 0; i < concurrency; ++i) workers.push_back(worker());

    co_await async_simple::coro::collectAll(std::move(workers));

    auto end_total = std::chrono::high_resolution_clock::now();
    double wall_time_ms = std::chrono::duration<double, std::milli>(end_total - start_total).count();

    std::vector<RequestMetrics> all_metrics;
    RequestMetrics m;
    while (metrics_queue.try_dequeue(m)) all_metrics.push_back(m);

    co_return calculate_stats(all_metrics, wall_time_ms);
}

// Test 2: Full Captcha
async_simple::coro::Lazy<AggregatedStats> test_captcha(const User& user, int total_requests, int concurrency,
                                                       const std::string& base_url) {
    ylt::detail::moodycamel::ConcurrentQueue<RequestMetrics> metrics_queue;
    std::atomic<int> current_idx{0};

    auto start_total = std::chrono::high_resolution_clock::now();

    auto worker = [&]() -> async_simple::coro::Lazy<void> {
        coro_http::coro_http_client client{};
        while (true) {
            int i = current_idx.fetch_add(1);
            if (i >= total_requests) co_return;

            RequestMetrics metrics{};
            auto start = std::chrono::high_resolution_clock::now();

            auto result = co_await Network::get_verified_token_profiling(client, user, base_url);

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
            } else {
                metrics.network_time_ms = metrics.total_time_ms;
            }
            metrics_queue.enqueue(metrics);
        }
    };

    std::vector<async_simple::coro::Lazy<void>> workers;
    for (int i = 0; i < concurrency; ++i) workers.push_back(worker());

    co_await async_simple::coro::collectAll(std::move(workers));

    auto end_total = std::chrono::high_resolution_clock::now();
    double wall_time_ms = std::chrono::duration<double, std::milli>(end_total - start_total).count();

    std::vector<RequestMetrics> all_metrics;
    RequestMetrics m;
    while (metrics_queue.try_dequeue(m)) all_metrics.push_back(m);

    co_return calculate_stats(all_metrics, wall_time_ms);
}

int main(int argc, char* argv[]) {
    User user;
    user.name = "LocalTest";

    int total_requests = 200;
    int concurrency = 10;

    if (argc > 1) total_requests = std::stoi(argv[1]);
    if (argc > 2) concurrency = std::stoi(argv[2]);

    try {
        init_mock_data();
        start_mock_server();

        std::string local_url = "http://127.0.0.1:9000";

        std::cout << "\n>>> Starting LOCAL Benchmark <<<" << std::endl;

        auto stats_net = async_simple::coro::syncAwait(test_pure_network(total_requests, concurrency, local_url));
        print_stats("Local Pure Network (Reference for Min Latency)", stats_net);

        auto stats_cap = async_simple::coro::syncAwait(test_captcha(user, total_requests, concurrency, local_url));
        print_stats("Local Captcha Flow (Algorithm Performance)", stats_cap);

        std::cout << "------------------------------------------" << std::endl;
        std::cout << "PERFORMANCE SUMMARY:" << std::endl;
        std::cout << "- Isolated CPU Logic Time: " << stats_cap.avg_cpu_time_ms << " ms" << std::endl;
        std::cout << "- Minimum Local Network Overhead: " << stats_net.mean_ms << " ms" << std::endl;
        std::cout << "------------------------------------------" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
