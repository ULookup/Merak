#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace merak::cli {

// ── Status output ──
void ok(const std::string& msg);
void warn(const std::string& msg);
void err(const std::string& msg);
void info(const std::string& msg);

// ── Structure ──
void section(const std::string& title);
void dim(const std::string& msg);
void rule();
void kv(const std::string& label, const std::string& value);
void bullet(const std::string& text);
void numbered(int index, const std::string& text);

// ── Table ──
void table(const std::vector<std::string>& headers,
           const std::vector<std::vector<std::string>>& rows);

// ── Formatters ──
std::string duration_ms(uint64_t ms);
std::string byte_size(size_t bytes);
std::string truncate_path(const std::string& path, size_t max_chars);

} // namespace merak::cli
