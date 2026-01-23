#include "logic.hpp"
#include <async_simple/coro/Sleep.h>
#include "../common/constants.hpp"
#include "../common/log.hpp"
#include "../network/api_client.hpp"

#include <chrono>
#include <thread>

namespace Reservation {
    std::atomic_bool success_flag{false};

    async_simple::coro::Lazy<void> captcha_producer(const User& user) noexcept {
        try {
            if (user.is_scheduled_mode) {
                auto now = std::chrono::system_clock::now();
                std::time_t now_t = std::chrono::system_clock::to_time_t(now);
                std::tm now_tm = *std::localtime(&now_t);
                now_tm.tm_hour = 20;
                now_tm.tm_min = 0;
                now_tm.tm_sec = 0;
                auto target_time = std::chrono::system_clock::from_time_t(std::mktime(&now_tm));
                auto start_time = target_time - std::chrono::seconds(30);

                if (now < start_time) {
                    std::chrono::duration<double> diff = start_time - now;
                    LOG_INFO(logger, "Producer waiting for {:.2f} seconds...", diff.count());
                    co_await async_simple::coro::sleep(start_time - now);
                }
            } else {
                LOG_INFO(logger, "Immediate mode: Producer starting immediately.");
            }
        } catch (const std::exception& e) {
            LOG_CRITICAL(logger, "Producer initialization failed: {}", e.what());
            co_return;
        } catch (...) {
            LOG_CRITICAL(logger, "Producer initialization failed with unknown error");
            co_return;
        }

        coro_http::coro_http_client client;
        int retry_count = 0;
        while (!success_flag.load(std::memory_order_relaxed)) {
            bool has_error = false;
            try {
                auto result = co_await Network::get_verified_token(client, user);
                if (result) {
                    captcha_queue.enqueue(std::move(result.value()));
                    LOG_DEBUG(logger, "Captcha solved and enqueued. Queue size approx: {}",
                              captcha_queue.size_approx());
                    retry_count = 0;
                } else {
                    LOG_DEBUG(logger, "Failed to solve captcha: {}", result.error().to_string());
                    co_await async_simple::coro::sleep(std::chrono::milliseconds(100));
                }
            } catch (const std::exception& e) {
                LOG_ERROR(logger, "Producer iteration failed: {}", e.what());
                has_error = true;
            } catch (...) {
                LOG_ERROR(logger, "Producer iteration failed with unknown error");
                has_error = true;
            }

            if (has_error) {
                retry_count++;
                if (retry_count >= 3) {
                    LOG_CRITICAL(logger, "Producer failed 3 times in a row, exiting.");
                    co_return;
                }
                co_await async_simple::coro::sleep(std::chrono::milliseconds(100));
            }
        }
    }

    async_simple::coro::Lazy<void> reservation_worker(const User& user,
                                                      std::chrono::system_clock::time_point start_time,
                                                      std::chrono::system_clock::time_point end_time,
                                                      int court_index) noexcept {
        std::chrono::system_clock::time_point stop_time;
        try {
            // Calculate target time: Today at 20:00:00 - 50ms
            // We just need to synchronize the action to 19:59:59.950 today.

            auto now = std::chrono::system_clock::now();

            if (user.is_scheduled_mode) {
                std::time_t now_t = std::chrono::system_clock::to_time_t(now);
                std::tm now_tm = *std::localtime(&now_t);

                now_tm.tm_hour = 20;
                now_tm.tm_min = 0;
                now_tm.tm_sec = 0;

                auto target_time = std::chrono::system_clock::from_time_t(std::mktime(&now_tm));
                auto wake_up_time = target_time - std::chrono::milliseconds(50);
                stop_time = target_time + std::chrono::seconds(10);

                if (now > wake_up_time) {
                    // If already past time, just go immediately and log warning
                    LOG_WARNING(logger, "Current time is already past the target start time!");
                } else {
                    std::chrono::duration<double> diff = wake_up_time - now;
                    LOG_INFO(logger, "Worker waiting for {:.2f} seconds...", diff.count());
                    co_await async_simple::coro::sleep(wake_up_time - std::chrono::system_clock::now());
                }
            } else {
                LOG_INFO(logger, "Immediate mode: Worker starting immediately.");
                stop_time = now + std::chrono::seconds(60);
            }
        } catch (const std::exception& e) {
            LOG_CRITICAL(logger, "Reservation worker initialization failed: {}", e.what());
            co_return;
        } catch (...) {
            LOG_CRITICAL(logger, "Reservation worker initialization failed with unknown error");
            co_return;
        }

        LOG_DEBUG(logger, "Worker started reservation sequence!");

        coro_http::coro_http_client client;
        int retry_count = 0;

        // Try to consume logic
        while (!success_flag.load(std::memory_order_relaxed)) {
            bool has_error = false;
            try {
                std::string verification;
                if (captcha_queue.try_dequeue(verification)) {
                    auto result =
                        co_await Network::reserve(client, user, verification, start_time, end_time, court_index);
                    if (result) {
                        LOG_INFO(logger, "Reservation SUCCESS!");
                        success_flag.store(true, std::memory_order_relaxed);
                        co_return;
                    } else {
                        LOG_DEBUG(logger, "Reservation failed: {}", result.error().to_string());
                        retry_count = 0;
                    }
                } else {
                    co_await async_simple::coro::sleep(std::chrono::nanoseconds(100));

                    if (std::chrono::system_clock::now() > stop_time) {
                        LOG_INFO(logger, "Timeout reached, stopping worker.");
                        co_return;
                    }
                }
            } catch (const std::exception& e) {
                LOG_ERROR(logger, "Reservation worker iteration failed: {}", e.what());
                has_error = true;
            } catch (...) {
                LOG_ERROR(logger, "Reservation worker iteration failed with unknown error");
                has_error = true;
            }

            if (has_error) {
                retry_count++;
                if (retry_count >= 3) {
                    LOG_CRITICAL(logger, "Reservation worker failed 3 times in a row, exiting.");
                    co_return;
                }
            }
        }
    }
}  // namespace Reservation
