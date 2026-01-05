#include "captcha.hpp"
#include <fmt/core.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include "../common/log.hpp"

namespace Captcha {
    Result<int64_t> solve(std::span<uint8_t> jigsaw_bytes, std::span<uint8_t> original_bytes) {
        try {
            cv::Mat jigsaw_buf(1, (int)jigsaw_bytes.size(), CV_8UC1, jigsaw_bytes.data());
            cv::Mat jigsaw = cv::imdecode(jigsaw_buf, cv::IMREAD_UNCHANGED);

            cv::Mat original_buf(1, (int)original_bytes.size(), CV_8UC1, original_bytes.data());
            cv::Mat original = cv::imdecode(original_buf, cv::IMREAD_UNCHANGED);

            if (jigsaw.empty() || original.empty()) {
                return tl::unexpected(BotError("Failed to decode captcha images from memory"));
            }

            // Theoretically, the server provides 4-channel BGRA images
            if (jigsaw.channels() != 4) {
                return tl::unexpected(BotError("Jigsaw image must have 4 channels (BGRA)"));
            }

            cv::Mat& jigsaw_with_alpha = jigsaw;

            // Extract the vertical position of the jigsaw piece
            cv::Mat jigsaw_alpha_channel;
            cv::extractChannel(jigsaw_with_alpha, jigsaw_alpha_channel, 3);
            cv::Rect jigsaw_bbox = cv::boundingRect(jigsaw_alpha_channel);
            int jigsaw_top_y = jigsaw_bbox.y;
            int jigsaw_bottom_y = jigsaw_bbox.y + jigsaw_bbox.height;

            // Crop the original image, considering only the vertical position of the jigsaw piece
            // Expand the crop range slightly to ensure the top and bottom boundaries of the jigsaw piece are included
            int crop_y_start = std::max(0, jigsaw_top_y - 5);
            int crop_y_end = std::min(original.rows, jigsaw_bottom_y + 5);
            cv::Rect crop_roi(0, crop_y_start, original.cols, crop_y_end - crop_y_start);

            cv::Mat cropped_image = original(crop_roi);

            // Convert the cropped image to grayscale and perform binarization
            cv::Mat gray_cropped, binary_cropped;
            cv::cvtColor(cropped_image, gray_cropped, cv::COLOR_BGR2GRAY);
            cv::threshold(gray_cropped, binary_cropped, 240, 255, cv::THRESH_BINARY);

            // Contour detection
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(binary_cropped, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

            int result = -1;
            for (const auto& contour : contours) {
                if (cv::contourArea(contour) < 50) {
                    continue;  // Filter out small noise
                }

                cv::Rect contour_bbox = cv::boundingRect(contour);
                int contour_y_in_original = contour_bbox.y + crop_y_start;

                // Check if the vertical position of the contour aligns with the jigsaw piece
                if (contour_y_in_original >= jigsaw_top_y - 3 && contour_y_in_original <= jigsaw_bottom_y + 3) {
                    result = contour_bbox.x;
                    break;  // Found matching contour, return x coordinate
                }
            }

            if (result == -1) {
                return tl::unexpected(BotError("No suitable contour found"));
            }

            LOG_DEBUG(logger, "Captcha solved: x position = {}", result);

            return result;
        } catch (const std::exception& e) {
            return tl::unexpected(BotError(fmt::format("Captcha solving error: {}", e.what())));
        }
    }
}  // namespace Captcha
