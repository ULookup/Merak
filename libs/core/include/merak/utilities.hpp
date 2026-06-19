#pragma once

#include <string>
#include <optional>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace merak {

inline std::optional<int> safe_stoi(const std::string& s) {
    try { return std::stoi(s); }
    catch (const std::exception& e) {
        spdlog::debug("safe_stoi('{}'): {}", s, e.what());
        return std::nullopt;
    }
}

inline std::optional<nlohmann::json> safe_json_parse(const std::string& s) {
    try { return nlohmann::json::parse(s); }
    catch (const std::exception& e) {
        spdlog::debug("safe_json_parse failed for '{}...': {}",
              s.substr(0, 80), e.what());
        return std::nullopt;
    }
}

} // namespace merak
