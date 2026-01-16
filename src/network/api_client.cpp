#include "api_client.hpp"
#include "../captcha/captcha.hpp"
#include "../common/constants.hpp"
#include "../common/log.hpp"
#include "../config/config.hpp"
#include "../utils/crypto.hpp"

#include <fmt/chrono.h>
#include <fmt/core.h>
#include <simdjson.h>
#include <asio.hpp>
#include <asio/use_awaitable.hpp>
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

    async_simple::coro::Lazy<Result<void>> validate_token(const User& user) {
        coro_http::coro_http_client client{};
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
            LOG_DEBUG(logger, "Token is valid.");
            co_return Result<void>{};
        } catch (...) {
            co_return tl::unexpected(BotError("Failed to parse token validation response"));
        }
    }

    async_simple::coro::Lazy<Result<std::chrono::system_clock::time_point>> get_server_time() {
        using namespace asio::ip;

        constexpr uint64_t NTP_UNIX_OFFSET = 2208988800ULL;
        std::array<uint8_t, 48> send_data{0x23}, recv_data{};
        asio::io_context::executor_type executor = coro_io::get_global_executor()->get_asio_executor();

        udp::resolver resolver(executor);
        udp::socket socket(executor, udp::v4());

        udp::endpoint receiver_endpoint = *resolver.resolve(udp::v4(), "ntp.aliyun.com", "123").begin();
        socket.connect(receiver_endpoint);

        socket.send(asio::buffer(send_data));
        size_t len = socket.receive(asio::buffer(recv_data));
        if (len < 48) {
            co_return tl::unexpected(BotError("Incomplete NTP response"));
        }

        uint32_t sec = (recv_data[40] << 24) | (recv_data[41] << 16) | (recv_data[42] << 8) | recv_data[43];
        uint32_t frac = (recv_data[44] << 24) | (recv_data[45] << 16) | (recv_data[46] << 8) | recv_data[47];
        uint64_t ntp_time = (static_cast<uint64_t>(sec) << 32) | frac;

        double ntp_seconds = static_cast<double>(ntp_time >> 32);
        double fractional_part = static_cast<double>(ntp_time & 0xFFFFFFFF) / 4294967296.0;
        double unix_time = ntp_seconds - NTP_UNIX_OFFSET + fractional_part;

        auto tp = std::chrono::system_clock::from_time_t(static_cast<std::time_t>(unix_time));
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(fractional_part));
        tp += ms;
        co_return tp;
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
                                                   std::string_view verification,
                                                   std::chrono::system_clock::time_point start_time,
                                                   std::chrono::system_clock::time_point end_time, int court_index) {
        if (court_index < 1 || court_index > (int)Constants::GROUND_NUM) {
            co_return tl::unexpected(BotError(fmt::format("Invalid court index: {}", court_index)));
        }
        const auto& ground = Constants::GROUNDS[court_index - 1];

        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::string order_time = fmt::format("{:%Y-%m-%d %H:%M:%S}", *std::localtime(&now_c));

        std::time_t start_t = std::chrono::system_clock::to_time_t(start_time);
        std::time_t end_t = std::chrono::system_clock::to_time_t(end_time);

        std::string full_start_time = fmt::format("{:%Y-%m-%d %H:%M:%S}", *std::localtime(&start_t));
        std::string full_end_time = fmt::format("{:%Y-%m-%d %H:%M:%S}", *std::localtime(&end_t));

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
        p["orderDate"] = order_time;
        p["startTime"] = full_start_time;
        p["endTime"] = full_end_time;
        p["tmpOrderDate"] = order_time;
        p["tmpStartTime"] = full_start_time;
        p["tmpEndTime"] = full_end_time;
        std::string payload_str = p.dump();

        std::string url = fmt::format(
            "https://reservation.sustech.edu.cn/api/blade-app/qywx/saveOrder?userid={}&token={}", user.id, user.token);

        client.add_header("Origin", "https://reservation.sustech.edu.cn");
        client.add_header("User-Agent",
                          "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) "
                          "Language/en ColorScheme/Light DistType/publish-store wxwork/4.1.32 (MicroMessenger/6.2) "
                          "WeChat/2.0.4 wwmver/3.26.15.616 Safari/605.1.15");

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
