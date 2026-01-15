# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SUSTechBadmintonReverser is a C++20 automated badminton court reservation system that interacts with a booking API. The system handles token validation, CAPTCHA solving using OpenCV, and concurrent reservation attempts using coroutines.

## Build System

This project uses CMake with CPM (CMake Package Manager) for dependency management. All dependencies are fetched automatically during configuration.

### Build Commands

```bash
# Configure (Debug)
cmake -B out/build/debug -DCMAKE_BUILD_TYPE=Debug

# Configure (Release)
cmake -B out/build/release -DCMAKE_BUILD_TYPE=Release

# Configure (RelWithDebInfo) - recommended for profiling
cmake -B out/build/relwithdebinfo -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build
cmake --build out/build/debug
cmake --build out/build/release
cmake --build out/build/relwithdebinfo

# Run main executable
./out/build/debug/bin/MyProject
./out/build/release/bin/MyProject
.out/build/relwithdebinfo/bin/MyProject

# Run network benchmark test
./out/build/debug/network_benchmark [total_requests] [concurrency]
# Example: ./out/build/debug/network_benchmark 200 10
```

### Build Options

- `YLT_ENABLE_IO_URING`: Enable io_uring for yalantinglibs (Linux only, default: OFF)
- `RESERVATION_SINGLETHREAD_ENABLED`: Enable single-threaded reservation mode (default: OFF)

Example:
```bash
cmake -B out/build/debug -DCMAKE_BUILD_TYPE=Debug -DYLT_ENABLE_IO_URING=ON
```

## Architecture

### Core Components

The codebase is organized into distinct modules under `src/`:

1. **Network Layer** (`src/network/`)
   - `api_client.hpp/cpp`: HTTP client for API interactions using yalantinglibs coroutines
   - Functions: `validate_token()`, `get_verified_token()`, `reserve()`
   - All network operations are async coroutines returning `async_simple::coro::Lazy<Result<T>>`

2. **Configuration** (`src/config/`)
   - `config.hpp/cpp`: YAML-based configuration loader
   - Loads user credentials and reservation settings from `config.yml`
   - Can generate template config if file is missing

3. **CAPTCHA Solver** (`src/captcha/`)
   - `captcha.hpp/cpp`: OpenCV-based jigsaw CAPTCHA solver
   - `Captcha::solve()`: Takes jigsaw and original image bytes, returns offset position

4. **Reservation Logic** (`src/reservation/`)
   - `logic.hpp/cpp`: Orchestrates the reservation workflow
   - Uses producer-consumer pattern with channels for CAPTCHA tokens
   - `captcha_producer()`: Continuously generates verified tokens
   - `reservation_worker()`: Consumes tokens and attempts reservations
   - Global `success_flag` atomic to coordinate workers

5. **Utilities** (`src/utils/`)
   - `crypto.hpp/cpp`: Cryptographic operations (likely for API authentication)

6. **Common** (`src/common/`)
   - `types.hpp`: Core data structures (`User`, `BotError`, `Result<T>`)
   - `log.hpp`: Logging macros (currently using fmt, Quill integration commented out)
   - `constants.hpp`: Application constants

### Key Design Patterns

**Error Handling**: Uses `Result<T>` type alias for `tl::expected<T, BotError>`. All fallible operations return `Result<T>` instead of throwing exceptions. `BotError` captures source location automatically.

**Async/Coroutines**: Heavy use of `async_simple::coro::Lazy<T>` for async operations. Main function uses `async_simple::coro::syncAwait()` to run coroutines synchronously.

**Concurrency**:
- Producer-consumer pattern using `coro_io::channel<std::string>` for CAPTCHA tokens
- Channel capacity: 200 tokens
- Workers coordinate via `std::atomic_bool success_flag`

**Configuration**: User data loaded from `config.yml` with structure:
```yaml
name: string
id: int64
phone: int64
token: string (UUID format)
is_scheduled_mode: bool
time: array of datetime strings
```

## Dependencies

Major dependencies (auto-fetched by CPM):
- **yalantinglibs** (0.5.7): Async HTTP client with coroutine support
- **OpenCV** (4.13.0): Image processing for CAPTCHA solving (minimal build: core, imgproc, imgcodecs)
- **OpenSSL**: SSL/TLS support via openssl-cmake
- **nlohmann/json** (3.12.0): JSON parsing
- **fmt** (12.1.0): String formatting
- **yaml-cpp** (0.8.0): YAML configuration parsing
- **simdjson** (4.2.4): Fast JSON parsing
- **async_simple**: Coroutine runtime

## Testing

The project includes a network benchmark test:

```bash
# Run with defaults (200 requests, 10 concurrent)
./out/build/debug/network_benchmark

# Custom parameters
./out/build/debug/network_benchmark 500 20
```

The benchmark measures API performance with configurable request count and concurrency level.

## Platform-Specific Notes

**Windows**:
- Console output set to UTF-8 in main()
- Static linking configured for MinGW/Clang builds
- MSVC uses `/bigobj` flag for large object files

**Linux**:
- Can enable io_uring for better async I/O performance
- Static linking of libstdc++ and libgcc in Release builds

## Code Style

- C++20 standard required
- Uses coroutines extensively
- Prefer `Result<T>` over exceptions
- Source location tracking in errors via `std::source_location`
- Header-only fmt library disabled (`FMT_HEADER_ONLY=0`)
