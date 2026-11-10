/** @file tests/unit/test_capture_clock.cpp
 * @brief Regression coverage for timestamp conversion without shared native/C++ epochs.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <limits>

// local includes
#include "src/capture_clock.h"

TEST(CaptureClockTest, PreservesNativeCaptureAge) {
  const auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(1234));
  const auto result = capture_clock::from_age(0.025, now);
  ASSERT_TRUE(result);
  EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(now - *result).count(), 25);
  EXPECT_EQ(capture_clock::from_age(-0.001, now), now);
}

TEST(CaptureClockTest, RejectsInvalidOrUnrelatedClockValues) {
  const auto now = std::chrono::steady_clock::now();
  for (const auto age : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), -0.051, 5.001}) {
    EXPECT_FALSE(capture_clock::from_age(age, now));
  }
  EXPECT_TRUE(capture_clock::from_age(5.0, now));
}
