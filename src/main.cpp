#include "async_simple/coro/SyncAwait.h"
#include <ylt/util/tl/expected.hpp>
#include <fmt/core.h>
#include <ylt/coro_http/coro_http_client.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/types.hpp"
#include "common/log.hpp"
#include "config/config.hpp"
#include "network/api_client.hpp"

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/LogFunctions.h>
#include "quill/backend/BackendOptions.h"
#include "quill/core/LogLevel.h"

quill::Logger* logger = nullptr;

Result<void> init_logger() noexcept {
    try {
        logger = quill::Frontend::create_or_get_logger(
            "Logger", 
            quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink"),
            quill::PatternFormatterOptions{
                "%(time) [%(thread_id)] %(short_source_location:<28) %(log_level:<9) %(message)",
                "%H:%M:%S.%Qms",
                quill::Timezone::LocalTime,
                false
            }
        );
        quill::BackendOptions backend_options;
        backend_options.check_printable_char = {};
        quill::Backend::start(backend_options);
        return Result<void>{};
    } catch (const std::exception& e) {
        return tl::unexpected(
            BotError(fmt::format("Logger initialization failed: {}", e.what()))
        );
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
        LOG_INFO(logger, "User config loaded: Name='{}', ID={}, Phone={}", 
            user_data.name, user_data.id, user_data.phone);
    } else {
        LOG_CRITICAL(logger, "Failed to load user config: {}", result.error().to_string());
        if (auto gen_result = Config::generate_template_config(); gen_result) {
            LOG_NOTICE(logger, "Template config file 'config_template.yml' generated successfully.");
        } else {
            LOG_WARNING(logger, "Failed to generate template config: {}", gen_result.error().to_string());
        }
        return 1;
    }


    async_simple::coro::syncAwait([&user_data = std::as_const(user_data)]() -> async_simple::coro::Lazy<void>
        {
            if (auto validate_res = co_await Network::validate_token(user_data); !validate_res) {
                LOG_CRITICAL(logger, "Failed to validate token: {}", validate_res.error().to_string());
            }
        }()
    );

    async_simple::coro::syncAwait([&user_data = std::as_const(user_data)]() -> async_simple::coro::Lazy<void> {
        if (auto token_res = co_await Network::get_verified_token(user_data); !token_res) {
            LOG_ERROR(logger, "Failed to obtain verified token: {}", token_res.error().to_string());
        }
    }());
}