#pragma once

#include <atomic>
#include <ylt/coro_io/coro_io.hpp>
#include "../common/types.hpp"

inline coro_io::channel<std::string> captcha_ch = coro_io::create_channel<std::string>(200);

namespace Reservation {
    extern std::atomic_bool success_flag;

    async_simple::coro::Lazy<void> captcha_producer(const User& user);
    async_simple::coro::Lazy<void> reservation_worker(const User& user, std::string time_start, std::string time_end,
                                                      std::string date, std::string court_id, std::string court_name);
}  // namespace Reservation