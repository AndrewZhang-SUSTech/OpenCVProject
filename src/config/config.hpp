#pragma once

#include <string>
#include "../common/types.hpp"

namespace Config {
    Result<User> load_user_config(const std::string& path);
    Result<void> generate_template_config();
}
