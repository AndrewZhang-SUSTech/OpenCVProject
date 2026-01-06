#pragma once

#include <string_view>

namespace Constants {
    struct GroundInfo {
        std::string_view id;
        std::string_view name;
    };

    constexpr GroundInfo GROUNDS[] = {{"1298272433186332673", "1号场"}, {"1298272520994086913", "2号场"},
                                      {"1298272615009411073", "3号场"}, {"1298272709167341570", "4号场"},
                                      {"1298272791098875905", "5号场"}, {"1298273087183183874", "6号场"},
                                      {"1298273175146127362", "7号场"}, {"1298273265650819073", "8号场"},
                                      {"1298273399927267330", "9号场"}, {"1298273500317933570", "10号场"}};

    constexpr size_t GROUND_NUM = sizeof(GROUNDS) / sizeof(GROUNDS[0]);

    constexpr std::string_view get_ground_name(std::string_view id) {
        for (const auto& ground : GROUNDS) {
            if (ground.id == id) return ground.name;
        }
        return "未知场地";
    }

    constexpr std::string_view CAPTCHA_PAYLOAD = R"({
        "captchaType": "blockPuzzle",
        "clientUid": "slider-3c6ee719-99f3-48aa-a51e-f553f9b00c7e",
        "ts": 1740655642708
    })";
}  // namespace Constants
