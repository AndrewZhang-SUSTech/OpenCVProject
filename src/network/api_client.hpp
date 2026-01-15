#pragma once

#include <async_simple/coro/Lazy.h>
#include <string>
#include <thread>
#include <ylt/coro_http/coro_http_client.hpp>
#include <ylt/util/tl/expected.hpp>
#include "../common/types.hpp"

namespace Network {
    async_simple::coro::Lazy<Result<void>> validate_token(coro_http::coro_http_client& client, const User& user);
    async_simple::coro::Lazy<Result<std::string>> get_verified_token(coro_http::coro_http_client& client,
                                                                     const User& user);
    async_simple::coro::Lazy<Result<void>> reserve(coro_http::coro_http_client& client, const User& user,
                                                   std::string_view verification, std::string_view time_start,
                                                   std::string_view time_end, std::string_view date, int court_index);
}  // namespace Network
