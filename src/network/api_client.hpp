#pragma once

#include <async_simple/coro/Lazy.h>
#include <string>
#include <thread>
#include <ylt/coro_http/coro_http_client.hpp>
#include <ylt/util/tl/expected.hpp>
#include "../common/types.hpp"

namespace Network {
    async_simple::coro::Lazy<Result<void>> validate_token(const User& user) noexcept;
    async_simple::coro::Lazy<Result<std::chrono::system_clock::time_point>> get_server_time() noexcept;
    async_simple::coro::Lazy<Result<std::string>> get_verified_token(coro_http::coro_http_client& client,
                                                                     const User& user) noexcept;
    async_simple::coro::Lazy<Result<void>> reserve(coro_http::coro_http_client& client, const User& user,
                                                   std::string_view verification,
                                                   std::chrono::system_clock::time_point start_time,
                                                   std::chrono::system_clock::time_point end_time,
                                                   int court_index) noexcept;
}  // namespace Network
