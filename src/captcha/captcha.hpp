#pragma once

#include <span>
#include <cstdint>
#include "../common/types.hpp"

namespace Captcha {
    Result<int64_t> solve(std::span<uint8_t> jigsaw_bytes, std::span<uint8_t> original_bytes);
}
