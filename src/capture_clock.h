/** @file src/capture_clock.h
 * @brief Convert a measured native capture age into the streaming monotonic clock.
 */
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

namespace capture_clock {
  /** @brief Preserve capture age without assuming native and C++ clock epochs match.
   * @param age_seconds Native host-clock time minus native capture time.
   * @param now C++ monotonic time sampled with the native host clock.
   * @return Capture timestamp, or no timestamp for an invalid or implausible age.
   */
  inline std::optional<std::chrono::steady_clock::time_point> from_age(double age_seconds, std::chrono::steady_clock::time_point now) {
    if (!std::isfinite(age_seconds) || age_seconds < -0.05 || age_seconds > 5.0) {
      return std::nullopt;
    }
    const auto age = std::chrono::duration<double>(std::max(0.0, age_seconds));
    return now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(age);
  }
}  // namespace capture_clock
