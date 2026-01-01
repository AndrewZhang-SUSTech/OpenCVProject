#include "async_simple/coro/SyncAwait.h"
#include <ylt/util/tl/expected.hpp>
#include <fmt/core.h>
#include <ylt/coro_http/coro_http_client.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

import Bot.Types;
import Bot.Logger;
import Bot.Config;
import Bot.ApiClient;

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (auto result = Logger::initialize(); result) {
        Logger::debug("Logger initialized in main successfully");
    } else {
        fmt::println(stderr, "Logger initialization failed: {}\n", result.error().to_string());
        return 1;
    }

    User user_data;
    if (auto result = Config::load_user_config("config.yml"); result) {
        user_data = result.value();
        Logger::info("User config loaded: Name='{}', ID={}, Phone={}", 
            StringRef{ user_data.name }, user_data.id, user_data.phone);
    } else {
        Logger::critical("Failed to load user config: {}", result.error().to_string());
        if (auto gen_result = Config::generate_template_config(); gen_result) {
            Logger::notice("Template config file 'config_template.yml' generated successfully.");
        } else {
            Logger::warning("Failed to generate template config: {}", StringRef{gen_result.error().to_string()});
        }
        return 1;
    }


    async_simple::coro::syncAwait([&user_data = std::as_const(user_data)]() -> async_simple::coro::Lazy<void>
        {
            if (auto validate_res = co_await Network::validate_token(user_data); !validate_res) {
                Logger::critical("Failed to validate token: {}", validate_res.error().to_string());
            }
        }()
    );

    async_simple::coro::syncAwait([&user_data = std::as_const(user_data)]() -> async_simple::coro::Lazy<void> {
        if (auto token_res = co_await Network::get_verified_token(user_data); !token_res) {
            Logger::error("Failed to obtain verified token: {}", token_res.error().to_string());
        }
    }());
}