#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace merak::worldbuilding {

std::string make_id(std::string_view prefix);
std::string now_iso_utc();

inline void remove_all_no_throw(const std::filesystem::path& path) noexcept {
    try { std::filesystem::remove_all(path); } catch (...) {}
}

} // namespace merak::worldbuilding
