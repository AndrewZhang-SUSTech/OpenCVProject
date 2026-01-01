module;

#include <quill/Logger.h>
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/sinks/ConsoleSink.h>
#include <quill/LogFunctions.h>
#include <quill/StringRef.h>
#include "quill/core/SourceLocation.h"
#include "quill/backend/BackendOptions.h"
#include "quill/core/LogLevel.h"
#include <ylt/util/tl/expected.hpp>
#include <fmt/core.h>

export module Bot.Logger;
import Bot.Types;

export inline quill::Logger* logger;

export using StringRef = quill::utility::StringRef;

export namespace Logger {
    Result<void> initialize() noexcept;

    struct FormatString {
        const char* fmt;
        const quill::SourceLocation loc;

        consteval FormatString(const char* s, quill::SourceLocation l = quill::SourceLocation::current()) 
            : fmt(s), loc(l) {}
    };

    template<typename ... Args>
    void info(FormatString fmt, Args&& ... args) {
        quill::log(logger, "", quill::LogLevel::Info, fmt.fmt, fmt.loc, std::forward<Args>(args)...);
    }

    template<typename ... Args>
    void notice(FormatString fmt, Args&& ... args) {
        quill::log(logger, "", quill::LogLevel::Notice, fmt.fmt, fmt.loc, std::forward<Args>(args)...);
    }

    template<typename ... Args>
    void debug(FormatString fmt, Args&& ... args) {
        quill::log(logger, "", quill::LogLevel::Debug, fmt.fmt, fmt.loc, std::forward<Args>(args)...);
    }

    template<typename ... Args>
    void warning(FormatString fmt, Args&& ... args) {
        quill::log(logger, "", quill::LogLevel::Warning, fmt.fmt, fmt.loc, std::forward<Args>(args)...);
    }

    template<typename ... Args>
    void error(FormatString fmt, Args&& ... args) {
        quill::log(logger, "", quill::LogLevel::Error, fmt.fmt, fmt.loc, std::forward<Args>(args)...);
    }

    template<typename ... Args>
    void critical(FormatString fmt, Args&& ... args) {
        quill::log(logger, "", quill::LogLevel::Critical, fmt.fmt, fmt.loc, std::forward<Args>(args)...);
    }
}

Result<void> Logger::initialize() noexcept {
    try {
        logger = quill::Frontend::create_or_get_logger(
            "Logger", 
            quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink"),
            quill::PatternFormatterOptions{
                "%(time) [%(thread_id)] %(short_source_location:<28) %(log_level:<9) %(message)",
                "%H:%M:%S.%Qms",
                quill::Timezone::LocalTime,
                false
            }
        );
        quill::BackendOptions backend_options;
        backend_options.check_printable_char = {};
        quill::Backend::start(backend_options);
        return Result<void>{};
    } catch (const std::exception& e) {
        return tl::unexpected(
            BotError(fmt::format("Logger initialization failed: {}", e.what()))
        );
    }
}