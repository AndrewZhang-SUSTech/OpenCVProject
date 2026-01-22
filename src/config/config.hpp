#pragma once

#include <string>
#include "../common/types.hpp"

namespace Config {
    Result<User> load_user_config(const std::string& path) noexcept;
    Result<void> generate_template_config() noexcept;
}  // namespace Config
