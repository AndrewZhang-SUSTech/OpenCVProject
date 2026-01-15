#pragma once
// #include <quill/LogMacros.h>
// #include <quill/Logger.h>

// inline quill::Logger* logger = nullptr;

struct DummyLogger {};
inline DummyLogger* logger = nullptr;

#define LOG_INFO(logger, ...)
#define LOG_DEBUG(logger, ...)
#define LOG_WARNING(logger, ...)
#define LOG_ERROR(logger, ...)
#define LOG_CRITICAL(logger, ...)
#define LOG_NOTICE(logger, ...)
