#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "../common/types.hpp"

namespace Crypto {
    Result<std::vector<uint8_t>> base64_decode(std::string_view input) noexcept;
    std::string base64_encode(std::span<uint8_t> data) noexcept;
    Result<std::string> aes_encrypt_ecb(std::string_view plaintext, std::string_view key) noexcept;
}  // namespace Crypto
