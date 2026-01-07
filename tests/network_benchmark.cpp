#include <async_simple/coro/Collect.h>
#include <async_simple/coro/SyncAwait.h>
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogFunctions.h>
#include <quill/sinks/ConsoleSink.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>
#include "../src/common/log.hpp"
#include "../src/common/types.hpp"
#include "../src/network/api_client.hpp"
#include "quill/backend/BackendOptions.h"

void init_logger() {
    try {
        logger = quill::Frontend::create_or_get_logger(
            "Logger", quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink"),
            quill::PatternFormatterOptions{
                "%(time) [%(thread_id)] %(short_source_location:<28) %(log_level:<9) %(message)", "%H:%M:%S.%Qms",
                quill::Timezone::LocalTime, false});
        quill::BackendOptions backend_options;
        backend_options.check_printable_char = {};
        quill::Backend::start(backend_options);
    } catch (...) {
    }
}

int main() {
    init_logger();
    User user;
    user.name = "TestUser";
    user.id = 123456789;
    user.phone = 13800138000;
    user.token = "dummy_token";

    std::cout << "Starting Network Benchmark..." << std::endl;

    std::string verification = "dummy_verification_string";

    try {
        int Total = 200;
        int BatchSize = 20;
        std::cout << "1. Testing get_verified_token (" << Total << " requests, batch size " << BatchSize << ")..."
                  << std::endl;

        std::cout << "   Warming up pool..." << std::endl;
        try {
            auto warmup = async_simple::coro::syncAwait(Network::get_verified_token(user));
        } catch (...) {
        }

        std::vector<double> times;
        times.reserve(Total);

        auto start_total = std::chrono::high_resolution_clock::now();

        for (int b = 0; b < (Total + BatchSize - 1) / BatchSize; ++b) {
            std::vector<async_simple::coro::Lazy<double>> tasks;
            tasks.reserve(BatchSize);

            int current_batch_size = std::min(BatchSize, Total - b * BatchSize);
            for (int i = 0; i < current_batch_size; ++i) {
                tasks.push_back([&user]() -> async_simple::coro::Lazy<double> {
                    auto start = std::chrono::high_resolution_clock::now();
                    auto result = co_await Network::get_verified_token(user);
                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> elapsed = end - start;
                    co_return elapsed.count();
                }());
            }

            auto results = async_simple::coro::syncAwait(async_simple::coro::collectAll(std::move(tasks)));
            for (const auto& r : results) {
                times.push_back(r.value());
            }
        }

        auto end_total = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double, std::milli> elapsed_total = end_total - start_total;

        if (times.empty()) {
            std::cerr << "No results collected!" << std::endl;
        } else {
            // Calculate stats
            double sum = std::accumulate(times.begin(), times.end(), 0.0);
            double mean = sum / times.size();

            std::sort(times.begin(), times.end());
            double min = times.front();
            double max = times.back();
            double median = times[times.size() / 2];
            double p95 = times[(size_t)(times.size() * 0.95)];
            double p99 = times[(size_t)(times.size() * 0.99)];

            double sq_sum = std::inner_product(times.begin(), times.end(), times.begin(), 0.0);
            double stdev = std::sqrt(sq_sum / times.size() - mean * mean);

            std::cout << "\n--- Statistics (ms) ---" << std::endl;
            std::cout << "Total Wall Time: " << elapsed_total.count() << " ms" << std::endl;
            std::cout << "Count:  " << times.size() << std::endl;
            std::cout << "Min:    " << min << std::endl;
            std::cout << "Max:    " << max << std::endl;
            std::cout << "Mean:   " << mean << std::endl;
            std::cout << "Median: " << median << std::endl;
            std::cout << "Stdev:  " << stdev << std::endl;
            std::cout << "P95:    " << p95 << std::endl;
            std::cout << "P99:    " << p99 << std::endl;
            std::cout << "-----------------------\n" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Benchmark Exception (Phase 1): " << e.what() << std::endl;
    }

    std::cout << "\n----------------------------------------\n" << std::endl;

    std::cout << "2. Testing reserve request..." << std::endl;
    auto start_reserve = std::chrono::high_resolution_clock::now();

    try {
        auto reserve_result = async_simple::coro::syncAwait(Network::reserve(user, verification,
                                                                             "18:00",       // time_start
                                                                             "19:00",       // time_end
                                                                             "2026-01-01",  // date
                                                                             1              // court_index
                                                                             ));

        auto end_reserve = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_reserve = end_reserve - start_reserve;
        std::cout << "=> reserve took: " << elapsed_reserve.count() << " ms" << std::endl;

        if (!reserve_result) {
            std::cout << "   reserve result (expected failure): " << reserve_result.error().to_string() << std::endl;
        } else {
            std::cout << "   reserve success" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "   Exception in reserve: " << e.what() << std::endl;
    }

    return 0;
}
