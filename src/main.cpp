#include <fmt/core.h>
#include <ylt/coro_http/coro_http_client.hpp>
#include <ylt/util/tl/expected.hpp>
#include "async_simple/coro/Collect.h"
#include "async_simple/coro/SyncAwait.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/constants.hpp"
#include "common/log.hpp"
#include "common/types.hpp"
#include "config/config.hpp"
#include "network/api_client.hpp"
#include "reservation/logic.hpp"

#include <fmt/chrono.h>
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogFunctions.h>
#include <quill/sinks/ConsoleSink.h>
#include "quill/backend/BackendOptions.h"

Result<void> init_logger() noexcept {
    try {
        logger = quill::Frontend::create_or_get_logger(
            "Logger", quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink"),
            quill::PatternFormatterOptions{
                "%(time) [%(thread_id)] %(short_source_location:<28) %(log_level:<9) %(message)", "%H:%M:%S.%Qms",
                quill::Timezone::LocalTime, false});
        quill::BackendOptions backend_options;
        backend_options.check_printable_char = {};
        quill::Backend::start(backend_options);
        return Result<void>{};
    } catch (const std::exception& e) {
        return tl::unexpected(BotError(fmt::format("Logger initialization failed: {}", e.what())));
    }
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (auto result = init_logger(); result) {
        LOG_DEBUG(logger, "Logger initialized in main successfully");
    } else {
        fmt::println(stderr, "Logger initialization failed: {}\n", result.error().to_string());
        return 1;
    }

    User user_data;
    if (auto result = Config::load_user_config("config.yml"); result) {
        user_data = result.value();
        LOG_INFO(logger, "User config loaded: Name='{}', ID={}, Phone={}", user_data.name, user_data.id,
                 user_data.phone);
    } else {
        LOG_CRITICAL(logger, "Failed to load user config: {}", result.error().to_string());
        if (auto gen_result = Config::generate_template_config(); gen_result) {
            LOG_NOTICE(logger, "Template config file 'config_template.yml' generated successfully.");
        } else {
            LOG_WARNING(logger, "Failed to generate template config: {}", gen_result.error().to_string());
        }
        return 1;
    }

    async_simple::coro::syncAwait([&user_data = std::as_const(user_data)]() -> async_simple::coro::Lazy<void> {
        {
            coro_http::coro_http_client client1{}, client2{};
            LOG_INFO(logger, "Performing initial token validation and time synchronizing...");

            auto [validate_res, time_res] =
                co_await async_simple::coro::collectAll(Network::validate_token(user_data), Network::get_server_time());
            if (!validate_res.available()) {
                try {
                    std::rethrow_exception(validate_res.getException());
                } catch (const std::exception& e) {
                    LOG_CRITICAL(logger, "Failed to validate token: {}", e.what());
                    co_return;
                }
            }
            if (!validate_res.value()) {
                LOG_CRITICAL(logger, "Token validation failed: {}", validate_res.value().error().to_string());
                co_return;
            }
            LOG_INFO(logger, "Token validation successful.");

            if (!time_res.available()) {
                try {
                    std::rethrow_exception(time_res.getException());
                } catch (const std::exception& e) {
                    LOG_WARNING(logger, "Failed to check time synchronization: {}", e.what());
                }
            } else if (!time_res.value()) {
                LOG_WARNING(logger, "Failed to check time synchronization: {}", time_res.value().error().to_string());
            } else {
                auto server_time = time_res.value().value();
                auto local_time = std::chrono::system_clock::now();
                auto offset = std::chrono::duration_cast<std::chrono::milliseconds>(local_time - server_time);

                if (std::abs(offset.count()) > 1000) {
                    LOG_WARNING(logger, "Time offset detected! Local time is {}ms {} than server time.",
                                std::abs(offset.count()), (offset.count() > 0 ? "ahead" : "behind"));
                    LOG_WARNING(logger, "This may affect scheduled reservation accuracy.");
                } else {
                    LOG_INFO(logger, "Time is well synchronized (offset: {}ms).", offset.count());
                }
            }
        };

        struct ReserveTask {
            std::chrono::system_clock::time_point start;
            std::chrono::system_clock::time_point end;
            int court_idx;
        };
        std::vector<ReserveTask> all_possible_tasks;

        for (const auto& tp_start : user_data.time) {
            auto tp_end = tp_start + std::chrono::minutes(30);
            for (size_t c = 1; c <= Constants::GROUND_NUM; ++c) {
                all_possible_tasks.push_back({tp_start, tp_end, (int)c});
            }
        }

        LOG_INFO(logger, "Preparing to reserve. Generated {} possible tasks from {} time slots.",
                 all_possible_tasks.size(), user_data.time.size());

        std::vector<async_simple::coro::Lazy<void>> tasks;

        // Settings
        const int CAPTCHA_CONCURRENCY = 20;

        for (int i = 0; i < CAPTCHA_CONCURRENCY; ++i) {
            tasks.push_back(Reservation::captcha_producer(user_data));
        }

        for (const auto& task : all_possible_tasks) {
            tasks.push_back(Reservation::reservation_worker(user_data, task.start, task.end, task.court_idx));
        }

        LOG_INFO(logger, "Drafted {} tasks ({} consumers, {} producers). Starting...", tasks.size(),
                 all_possible_tasks.size(), CAPTCHA_CONCURRENCY);

        // Run all
        co_await async_simple::coro::collectAll(std::move(tasks));
    }());
}