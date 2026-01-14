#include "api_client_profiling.hpp"
#include "../captcha/captcha.hpp"
#include "../common/constants.hpp"
#include "../common/log.hpp"
#include "../utils/crypto.hpp"

#include <fmt/core.h>
#include <simdjson.h>
#include <nlohmann/json.hpp>
#include <random>

static inline std::random_device rd{};

namespace Network {

async_simple::coro::Lazy<Result<std::pair<std::string, ProfilingMetrics>>>
get_verified_token_profiling(coro_http::coro_http_client& client, const User& user) {
    ProfilingMetrics metrics;
    auto total_start = std::chrono::high_resolution_clock::now();

    struct CaptchaContext {
        std::string token;
        std::string secret_key;
        std::string point_json;
    };
    CaptchaContext ctx;

    // 1. GET Captcha - Network timing
    auto net_start = std::chrono::high_resolution_clock::now();
    auto result = co_await client.async_post(
        "https://reservation.sustech.edu.cn/api/captcha/get",
        std::string(Constants::CAPTCHA_PAYLOAD),
        coro_http::req_content_type::json);
    auto net_end = std::chrono::high_resolution_clock::now();
    metrics.captcha_get_ms = std::chrono::duration<double, std::milli>(net_end - net_start).count();

    if (result.status != 200) {
        co_return tl::unexpected(BotError(fmt::format("HTTP Error fetching captcha: {}", result.status)));
    }

    std::string resp_body_get(result.resp_body);

    // 2. CPU Task: Parse, Solve, Encrypt - with detailed timing
    auto cpu_start = std::chrono::high_resolution_clock::now();

    // 2a. JSON Parsing
    auto parse_start = std::chrono::high_resolution_clock::now();
    auto solve_ctx_res = co_await coro_io::post([&resp_body_get, &ctx]() -> Result<std::string> {
        static thread_local simdjson::dom::parser parser;
        std::string jigsaw_b64, original_b64;

        try {
            simdjson::dom::element doc = parser.parse(resp_body_get);
            bool success;
            if (doc["success"].get(success) != simdjson::SUCCESS || !success)
                return tl::unexpected(BotError("Server returned failure"));

            simdjson::dom::element data;
            if (doc["repData"].get(data) != simdjson::SUCCESS)
                return tl::unexpected(BotError("Missing repData"));

            std::string_view j_sv, o_sv, t_sv, s_sv;
            if (data["jigsawImageBase64"].get(j_sv) != simdjson::SUCCESS ||
                data["originalImageBase64"].get(o_sv) != simdjson::SUCCESS ||
                data["token"].get(t_sv) != simdjson::SUCCESS ||
                data["secretKey"].get(s_sv) != simdjson::SUCCESS) {
                return tl::unexpected(BotError("Missing fields"));
            }

            ctx.token = t_sv;
            ctx.secret_key = s_sv;
            jigsaw_b64 = j_sv;
            original_b64 = o_sv;
        } catch (const simdjson::simdjson_error& e) {
            return tl::unexpected(BotError(fmt::format("JSON Parse Error: {}", e.what())));
        }

        auto jigsaw_bytes = Crypto::base64_decode(jigsaw_b64);
        auto original_bytes = Crypto::base64_decode(original_b64);
        if (!jigsaw_bytes || !original_bytes) return tl::unexpected(jigsaw_bytes.error());

        // 2b. CAPTCHA Solving (OpenCV)
        auto solve_res = Captcha::solve(*jigsaw_bytes, *original_bytes);
        if (!solve_res) return tl::unexpected(solve_res.error());

        // 2c. Random adjustment and encryption
        {
            static thread_local std::mt19937_64 gen{rd()};
            static thread_local std::uniform_real_distribution<double> dis(-2.0, 2.0);
            double final_x = solve_res.value() + dis(gen);
            ctx.point_json = fmt::format(R"({{"x":{:.6f},"y":5}})", final_x);
        }

        auto enc_point = Crypto::aes_encrypt_ecb(ctx.point_json, ctx.secret_key);
        if (!enc_point) return tl::unexpected(enc_point.error());
        return *enc_point;
    });

    auto parse_end = std::chrono::high_resolution_clock::now();
    metrics.captcha_parse_ms = std::chrono::duration<double, std::milli>(parse_end - parse_start).count();

    if (solve_ctx_res.hasError()) {
        try {
            std::rethrow_exception(solve_ctx_res.getException());
        } catch (const std::exception& e) {
            co_return tl::unexpected(BotError(fmt::format("Task failed: {}", e.what())));
        }
    }
    if (!solve_ctx_res.value()) co_return tl::unexpected(solve_ctx_res.value().error());

    // 3. Prepare Check Payload
    nlohmann::json check_payload;
    check_payload["captchaType"] = "blockPuzzle";
    check_payload["pointJson"] = solve_ctx_res.value().value();
    check_payload["token"] = ctx.token;
    std::string payload_str = check_payload.dump();

    // 4. POST Check - Network timing
    auto check_start = std::chrono::high_resolution_clock::now();
    auto result2 = co_await client.async_post(
        "https://reservation.sustech.edu.cn/api/captcha/check",
        payload_str,
        coro_http::req_content_type::json);
    auto check_end = std::chrono::high_resolution_clock::now();
    metrics.captcha_check_ms = std::chrono::duration<double, std::milli>(check_end - check_start).count();

    try {
        auto j = nlohmann::json::parse(result2.resp_body);
        if (!j["success"].get<bool>()) {
            co_return tl::unexpected(BotError(fmt::format("Captcha check failed: {}", result2.resp_body)));
        }
    } catch (...) {
        co_return tl::unexpected(BotError("Failed to parse check response"));
    }

    // 5. Final Encrypt
    auto encrypt_start = std::chrono::high_resolution_clock::now();
    std::string combine = fmt::format("{}---{}", ctx.token, ctx.point_json);
    auto final_result_exp = Crypto::aes_encrypt_ecb(combine, ctx.secret_key);
    auto encrypt_end = std::chrono::high_resolution_clock::now();
    metrics.captcha_encrypt_ms = std::chrono::duration<double, std::milli>(encrypt_end - encrypt_start).count();

    if (!final_result_exp) {
        co_return tl::unexpected(final_result_exp.error());
    }

    // Calculate totals
    auto total_end = std::chrono::high_resolution_clock::now();
    metrics.total_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();
    metrics.total_network_ms = metrics.captcha_get_ms + metrics.captcha_check_ms;
    metrics.total_cpu_ms = metrics.captcha_parse_ms + metrics.captcha_encrypt_ms;

    co_return std::make_pair(final_result_exp.value(), metrics);
}

}  // namespace Network

