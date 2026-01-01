#include "config.hpp"
#include "../common/log.hpp"
#include <yaml-cpp/yaml.h>
#include <quill/LogMacros.h>
#include <vector>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fmt/core.h>
#include "yaml-cpp/emittermanip.h"
#include <cstdio>

namespace YAML {
    template<>
    struct convert<std::chrono::system_clock::time_point> {
        static bool decode(const Node& node, std::chrono::system_clock::time_point& rhs) {
            if (!node.IsScalar()) return false;
            std::string str = node.as<std::string>();
            std::tm tm = {};
            std::istringstream ss(str);
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M");
            if (ss.fail()) return false;
            rhs = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            return true;
        }
    };
}

namespace Config {
    Result<User> load_user_config(const std::string& path) {
        try {
            YAML::Node config = YAML::LoadFile(path);
            User user;

            if (!config["name"]) {
                return tl::unexpected(BotError("Config error: Missing 'name' field"));
            }
            try {
                user.name = config["name"].as<std::string>();
            } catch (...) {
                return tl::unexpected(BotError("Config error: 'name' must be a string"));
            }

            if (!config["token"]) {
                return tl::unexpected(BotError("Config error: Missing 'token' field"));
            }
            try {
                user.token = config["token"].as<std::string>();
            }
            catch (...) {
                return tl::unexpected(BotError("Config error: 'token' must be a string"));
            }

            if (!config["id"]) {
                return tl::unexpected(BotError("Config error: Missing 'id' field"));
            }
            try {
                user.id = config["id"].as<long long>();
            } catch (...) {
                return tl::unexpected(BotError("Config error: 'id' must be an integer"));
            }

            if (!config["phone"]) {
                return tl::unexpected(BotError("Config error: Missing 'phone' field"));
            }
            try {
                user.phone = config["phone"].as<long long>();
            } catch (...) {
                return tl::unexpected(BotError("Config error: 'phone' must be an integer"));
            }

            user.is_scheduled_mode = false;
            if (config["is_scheduled_mode"]) {
                try {
                    user.is_scheduled_mode = config["is_scheduled_mode"].as<bool>();
                } catch (...) {
                    return tl::unexpected(BotError("Config error: 'is_scheduled_mode' must be a boolean"));
                }
            } else {
                LOG_NOTICE(logger, "Config warning: 'is_scheduled_mode' not found, defaulting to false");
            }

            if (config["time"]) {
                if (!config["time"].IsSequence()) {
                    return tl::unexpected(BotError("Config error: 'time' must be a sequence"));
                }
                try {
                    auto times = config["time"].as<std::vector<std::chrono::system_clock::time_point>>();
                    auto now = std::chrono::system_clock::now();
                    for (const auto& tp : times) {
                        if (tp <= now) {
                            return tl::unexpected(BotError("Config error: 'time' elements must be in the future"));
                        }
                        std::time_t t = std::chrono::system_clock::to_time_t(tp);
                        std::tm* tm_ptr = std::localtime(&t);
                        if (tm_ptr == nullptr || tm_ptr->tm_sec != 0 || (tm_ptr->tm_min != 0 && tm_ptr->tm_min != 30)) {
                            return tl::unexpected(BotError("Config error: 'time' elements must be on the hour or half-hour (e.g. 12:00 or 12:30)"));
                        }
                    }
                    user.time = times;
                } catch (...) {
                    return tl::unexpected(BotError("Config error: Failed to parse 'time' elements. Expected format: YYYY-MM-DD HH:MM"));
                }
            } else if (user.is_scheduled_mode) {
                return tl::unexpected(BotError("Config error: 'time' field is required when 'is_scheduled_mode' is true"));
            } 

            return user;
        } catch (const YAML::BadFile&) {
            return tl::unexpected(BotError(fmt::format("Config error: Could not find or open file '{}'", path)));
        } catch (const std::exception& e) {
            return tl::unexpected(BotError(fmt::format("Config error: {}", e.what())));
        }
    }

    Result<void> generate_template_config(){
        try {
            YAML::Emitter out;
            out << YAML::BeginMap;
            out << YAML::Key << "name" << YAML::Value << "Your Name Here";
            out << YAML::Key << "id" << YAML::Value << 123456789;
            out << YAML::Key << "phone" << YAML::Value << 18888888888;
            out << YAML::Key << "token" << YAML::Value << "Your Token Here";
            out << YAML::Key << "is_scheduled_mode" << YAML::Value << false;
            out << YAML::Comment("Optional. If true, times below must be provided.");
            out << YAML::Key << "time" << YAML::Value << YAML::BeginSeq;
            out << "2026-10-01 10:00";
            out << "2026-10-01 10:30";
            out << YAML::EndSeq;
            out << YAML::EndMap;

            if(!out.good()){
                return tl::unexpected(BotError("Failed to generate template config: YAML emission error"));
            }

            if(std::FILE* fout = std::fopen("config_template.yml", "w"); fout){
                fmt::print(fout, "{}", out.c_str());
                std::fclose(fout);
            } else {
                return tl::unexpected(BotError("Failed to create template config file"));
            }

            return Result<void>{};
        } catch (const std::exception& e) {
            return tl::unexpected(BotError(fmt::format("Failed to generate template config: {}", e.what())));
        }
    }
}
