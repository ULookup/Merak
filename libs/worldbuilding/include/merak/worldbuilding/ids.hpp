#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace merak::worldbuilding {

std::string make_id(std::string_view prefix);
std::string now_iso_utc();

inline std::string base64_encode(const std::vector<unsigned char>& data) {
    static const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64;
    b64.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<unsigned char>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<unsigned char>(data[i+1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<unsigned char>(data[i+2]);
        b64 += kBase64[(n >> 18) & 63];
        b64 += kBase64[(n >> 12) & 63];
        b64 += kBase64[(n >> 6) & 63];
        b64 += kBase64[n & 63];
    }
    if (data.size() % 3) { b64[b64.size() - 1] = '='; }
    if (data.size() % 3 == 1) { b64[b64.size() - 2] = '='; }
    return b64;
}

inline std::optional<std::vector<unsigned char>> base64_decode(std::string_view input) {
    static const auto kDecodeTable = []() {
        std::array<int, 256> t;
        t.fill(-1);
        for (int i = 0; i < 64; i++) {
            t[static_cast<unsigned char>(
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i])] = i;
        }
        return t;
    }();

    std::vector<unsigned char> out;
    out.reserve((input.size() / 4) * 3);

    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (c == '=') break;
        int idx = kDecodeTable[c];
        if (idx == -1) continue; // skip non-base64 chars (e.g. whitespace)
        val = (val << 6) | idx;
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

inline void remove_all_no_throw(const std::filesystem::path& path) noexcept {
    try { std::filesystem::remove_all(path); } catch (...) {}
}

} // namespace merak::worldbuilding
