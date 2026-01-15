#include "api_client.hpp"
#include "../captcha/captcha.hpp"
#include "../common/constants.hpp"
#include "../common/log.hpp"
#include "../config/config.hpp"
#include "../utils/crypto.hpp"

#include <fmt/core.h>
#include <simdjson.h>
#include <exception>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <thread>

static inline std::random_device rd{};

namespace Network {
    struct CaptchaContext {
        std::string token;
        std::string secret_key;
        std::string point_json;
    };

    async_simple::coro::Lazy<Result<void>> validate_token(coro_http::coro_http_client& client, const User& user) {
        auto result = co_await client.async_get(
            fmt::format("https://reservation.sustech.edu.cn/api/blade-app/qywx/"
                        "getAppGymPageByCustomerId?userid={}&token={}&current=1&size=50&gymName=",
                        user.id, user.token));

        if (result.status != 200) {
            co_return tl::unexpected(BotError(fmt::format("HTTP Error: {}", result.status)));
        }

        try {
            nlohmann::json j = nlohmann::json::parse(result.resp_body);
            if (j["code"].get<int>() != 200) {
                co_return tl::unexpected(BotError(fmt::format("Token validation failed: {}", result.resp_body)));
            }
            LOG_INFO(logger, "Token is valid.");
            co_return Result<void>{};
        } catch (...) {
            co_return tl::unexpected(BotError("Failed to parse token validation response"));
        }
    }

    async_simple::coro::Lazy<Result<std::string>> get_verified_token(coro_http::coro_http_client& client,
                                                                     const User& user) {
        CaptchaContext ctx;
        std::string resp_body_get;

        auto result =
            co_await client.async_post("https://reservation.sustech.edu.cn/api/captcha/get",
                                       std::string(Constants::CAPTCHA_PAYLOAD), coro_http::req_content_type::json);
        if (result.status != 200)
            co_return tl::unexpected(BotError(fmt::format("HTTP Error fetching captcha: {}", result.status)));

        resp_body_get = std::string(result.resp_body);

        auto solve_res = co_await coro_io::post([&resp_body_get, &ctx]() -> Result<int64_t> {
            static thread_local simdjson::dom::parser parser;
            std::string jigsaw_b64, original_b64;

            try {
                simdjson::dom::element doc = parser.parse(resp_body_get);
                bool success;
                if (doc["success"].get(success) != simdjson::SUCCESS || !success)
                    return tl::unexpected(BotError("Server returned failure"));

                simdjson::dom::element data;
                if (doc["repData"].get(data) != simdjson::SUCCESS) return tl::unexpected(BotError("Missing repData"));

                std::string_view j_sv, o_sv, t_sv, s_sv;
                if (data["jigsawImageBase64"].get(j_sv) != simdjson::SUCCESS ||
                    data["originalImageBase64"].get(o_sv) != simdjson::SUCCESS ||
                    data["token"].get(t_sv) != simdjson::SUCCESS || data["secretKey"].get(s_sv) != simdjson::SUCCESS) {
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

            return Captcha::solve(*jigsaw_bytes, *original_bytes);
        });

        if (solve_res.hasError()) {
            try {
                std::rethrow_exception(solve_res.getException());
            } catch (const std::exception& e) {
                co_return tl::unexpected(BotError(fmt::format("Failed to solve captcha: {}", e.what())));
            }
        }

        {
            static thread_local std::mt19937_64 gen{rd()};
            static thread_local std::uniform_real_distribution<double> dis(-2.0, 2.0);
            double final_x = *solve_res.value() + dis(gen);
            ctx.point_json = fmt::format(R"({{"x":{:.6f},"y":5}})", final_x);
        }

        auto enc_point = Crypto::aes_encrypt_ecb(ctx.point_json, ctx.secret_key);
        if (!enc_point) co_return tl::unexpected(enc_point.error());

        nlohmann::json check_payload;
        check_payload["captchaType"] = "blockPuzzle";
        check_payload["pointJson"] = *enc_point;
        check_payload["token"] = ctx.token;
        std::string payload_str = check_payload.dump();

        auto check_result = co_await client.async_post("https://reservation.sustech.edu.cn/api/captcha/check",
                                                       payload_str, coro_http::req_content_type::json);

        try {
            auto j = nlohmann::json::parse(check_result.resp_body);
            if (!j["success"].get<bool>()) {
                co_return tl::unexpected(BotError(fmt::format("Captcha check failed: {}", check_result.resp_body)));
            }
        } catch (...) {
            co_return tl::unexpected(BotError("Failed to parse check response"));
        }

        LOG_DEBUG(logger, "{} is validated.", ctx.point_json);

        std::string combine = fmt::format("{}---{}", ctx.token, ctx.point_json);
        auto final_result = co_await coro_io::post(
            [&combine, secret_key = ctx.secret_key]() { return Crypto::aes_encrypt_ecb(combine, secret_key); });

        if (final_result.hasError()) {
            try {
                std::rethrow_exception(final_result.getException());
            } catch (const std::exception& e) {
                co_return tl::unexpected(BotError(fmt::format("Failed to encrypt final result: {}", e.what())));
            }
        }

        co_return final_result.value();
    }

    // 3. 抢票 (单次 IO)
    async_simple::coro::Lazy<Result<void>> reserve(coro_http::coro_http_client& client, const User& user,
                                                   std::string_view verification, std::string_view time_start,
                                                   std::string_view time_end, std::string_view date, int court_index) {
        if (court_index < 1 || court_index > (int)Constants::GROUND_NUM) {
            co_return tl::unexpected(BotError(fmt::format("Invalid court index: {}", court_index)));
        }
        const auto& ground = Constants::GROUNDS[court_index - 1];

        nlohmann::json p;
        p["captchaVerification"] = verification;
        p["customerId"] = "1433554008613634050";
        p["customerName"] = user.name;
        p["customerTel"] = user.phone;
        p["userNum"] = 1;
        p["customerEmail"] = "";
        p["gymId"] = "1297443858304540673";
        p["gymName"] = "润杨羽毛球馆";
        p["groundId"] = ground.id;
        p["groundName"] = ground.name;
        p["groundType"] = "0";
        p["messagePushType"] = "0";
        p["isIllegal"] = "0";
        p["orderDate"] = date;
        p["startTime"] = time_start;
        p["endTime"] = time_end;
        p["tmpOrderDate"] = date;
        p["tmpStartTime"] = time_start;
        p["tmpEndTime"] = time_end;
        std::string payload_str = p.dump();

        std::string url = fmt::format(
            "https://reservation.sustech.edu.cn/api/blade-app/qywx/saveOrder?userid={}&token={}", user.id, user.token);

        auto res = co_await client.async_post(url, payload_str, coro_http::req_content_type::json);

        try {
            auto j = nlohmann::json::parse(res.resp_body);
            if (j["success"].get<bool>()) {
                co_return Result<void>{};
            } else {
                std::string msg = j.value("msg", "Unknown error");
                co_return tl::unexpected(BotError(msg));
            }
        } catch (...) {
            co_return tl::unexpected(BotError("Failed to parse order response"));
        }
    }
}  // namespace Network
