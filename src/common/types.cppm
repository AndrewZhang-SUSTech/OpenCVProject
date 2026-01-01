module;

#include <fmt/core.h>
#include <ylt/util/tl/expected.hpp>
#include <source_location>
#include <type_traits>
#include <string>
#include <vector>
#include <chrono>

export module Bot.Types;

export struct User {
    std::string name;
    long long id;
    long long phone;
    bool is_scheduled_mode;
    std::string token;
    std::vector<std::chrono::system_clock::time_point> time;
};

export struct BotError {
private:
    std::source_location location;
    std::string message;

public:
    BotError(std::string_view msg, const std::source_location loc = std::source_location::current())
        : location(loc), message(msg) {}

    std::string to_string() const {
        std::string_view file = location.file_name();
        if (size_t pos = file.rfind("src"); pos != std::string_view::npos) {
            file = file.substr(pos + 3);
        }
        return fmt::format("[.{}:{}] {}", file, location.line(), message);
    }
};
static_assert(std::is_nothrow_move_constructible<BotError>::value);

export template<typename T>
using Result = tl::expected<T, BotError>;