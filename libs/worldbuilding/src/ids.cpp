#include <merak/worldbuilding/ids.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <random>
#include <stdexcept>
#include <sstream>

namespace merak::worldbuilding {
namespace {

std::string random_hex(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

} // namespace

std::string make_id(std::string_view prefix) {
    static std::atomic<std::uint64_t> counter{0};
    static thread_local std::mt19937_64 rng{std::random_device{}()};

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now).count();

    std::ostringstream out;
    out << prefix << '_' << micros << '_' << counter.fetch_add(1) << '_'
        << random_hex(rng());
    return out.str();
}

std::string now_iso_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto time = std::chrono::system_clock::to_time_t(seconds);

    std::tm utc{};
    if (gmtime_r(&time, &utc) == nullptr) {
        throw std::runtime_error("format UTC timestamp failed");
    }

    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

} // namespace merak::worldbuilding
