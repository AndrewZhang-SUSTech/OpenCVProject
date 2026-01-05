#pragma once

#include <cstdint>
#include <span>
#include "../common/types.hpp"

namespace Captcha {
    Result<int64_t> solve(std::span<uint8_t> jigsaw_bytes, std::span<uint8_t> original_bytes);
}
