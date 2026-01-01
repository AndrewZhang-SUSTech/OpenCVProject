#pragma once

#include <thread>
#include <ylt/util/tl/expected.hpp>
#include <ylt/coro_http/coro_http_client.hpp>
#include <async_simple/coro/Lazy.h>
#include <string>
#include "../common/types.hpp"

namespace Network {
    async_simple::coro::Lazy<Result<void>> validate_token(const User& user);
    async_simple::coro::Lazy<Result<std::string>> get_verified_token(const User& user);
}
