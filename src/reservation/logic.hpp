#pragma once

#include <ylt/util/concurrentqueue.h>
#include <atomic>
#include <ylt/coro_io/coro_io.hpp>
#include "../common/types.hpp"

inline ylt::detail::moodycamel::ConcurrentQueue<std::string> captcha_queue;

namespace Reservation {
    extern std::atomic_bool success_flag;

    async_simple::coro::Lazy<void> captcha_producer(const User& user) noexcept;
    async_simple::coro::Lazy<void> reservation_worker(const User& user,
                                                      std::chrono::system_clock::time_point start_time,
                                                      std::chrono::system_clock::time_point end_time,
                                                      int court_index) noexcept;
}  // namespace Reservation