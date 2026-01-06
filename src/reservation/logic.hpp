#pragma once

#include <ylt/coro_io/coro_io.hpp>

inline coro_io::channel<std::string> captcha_ch = coro_io::create_channel<std::string>(200);