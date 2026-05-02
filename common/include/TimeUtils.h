#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace aicam {
namespace timeutil {

inline uint64_t nowUnixMicros() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count()
    );
}

inline std::string formatUnixMicros(uint64_t micros) {
    const auto tp = std::chrono::system_clock::time_point(std::chrono::microseconds(micros));
    const std::time_t seconds = std::chrono::system_clock::to_time_t(tp);
    const auto fractional = micros % 1000000ULL;

    std::tm tm_snapshot {};
#if defined(_WIN32)
    gmtime_s(&tm_snapshot, &seconds);
#else
    gmtime_r(&seconds, &tm_snapshot);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_snapshot, "%Y-%m-%dT%H:%M:%S")
        << '.'
        << std::setw(6)
        << std::setfill('0')
        << fractional
        << "Z";
    return oss.str();
}

inline double microsToMillis(uint64_t micros) {
    return static_cast<double>(micros) / 1000.0;
}

}  // namespace timeutil
}  // namespace aicam
