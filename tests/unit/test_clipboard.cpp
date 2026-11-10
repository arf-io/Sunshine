/** @file tests/unit/test_clipboard.cpp
 * @brief Regression tests for clipboard format, encoding, limit and conflict digests.
 */

// test includes
#include "../tests_common.h"

// local includes
#include "src/clipboard.h"

TEST(DesktopClipboardTest, AcceptsUnicodePayloadAndEmptySnapshot) {
  EXPECT_TRUE(desktop_clipboard::valid_items(nlohmann::json::object()));
  EXPECT_TRUE(desktop_clipboard::valid_items({{"text/plain", "8J+MjQo="}}));
  EXPECT_TRUE(desktop_clipboard::valid_items({{"text/plain", ""}, {"text/html", "YQ=="}, {"text/rtf", "YWI="}, {"image/png", "YWJj"}}));
}

TEST(DesktopClipboardTest, RejectsUnsupportedFormatsAndInvalidEncodings) {
  for (const auto &items : {nlohmann::json(nullptr), nlohmann::json::array(), nlohmann::json {{"text/uri-list", "YQ=="}}, nlohmann::json {{"text/plain", 1}}, nlohmann::json {{"text/plain", "A"}}, nlohmann::json {{"text/plain", "A==="}}, nlohmann::json {{"text/plain", "Y Q="}}, nlohmann::json {{"text/plain", "YQ=_"}}, nlohmann::json {{"text/plain", "YR=="}}, nlohmann::json {{"text/plain", "YWJ="}}}) {
    EXPECT_FALSE(desktop_clipboard::valid_items(items)) << items;
  }
}

TEST(DesktopClipboardTest, EnforcesAggregateDecodedLimit) {
  const std::string encoded((desktop_clipboard::max_bytes / 3) * 4, 'A');
  EXPECT_TRUE(desktop_clipboard::valid_items({{"text/plain", encoded}}));
  EXPECT_FALSE(desktop_clipboard::valid_items({{"text/plain", encoded}, {"text/html", "AAAA"}}));
}

TEST(DesktopClipboardTest, UsesStableContentDigestForConflictDetection) {
  const nlohmann::json first {{"text/plain", "YQ=="}, {"text/html", "Yg=="}};
  const nlohmann::json reversed {{"text/html", "Yg=="}, {"text/plain", "YQ=="}};
  EXPECT_EQ(desktop_clipboard::digest(first), desktop_clipboard::digest(reversed));
  EXPECT_NE(desktop_clipboard::digest(first), desktop_clipboard::digest({{"text/plain", "Yg=="}}));
  EXPECT_EQ(desktop_clipboard::digest(nlohmann::json::object()), "44136fa355b3678a1146ad16f7e8649e94fb4fc21fe77e8310c060f61caaff8a");
}

#ifdef __APPLE__
TEST(DesktopClipboardNativeTest, PreservesUnicodeAndRejectsStaleWrites) {
  desktop_clipboard::testing::begin();
  const auto empty = desktop_clipboard::digest(desktop_clipboard::read());
  const nlohmann::json first {{"text/plain", "8J+MjQo="}, {"text/html", "PGI+aGk8L2I+"}};
  EXPECT_TRUE(desktop_clipboard::write(first, empty));
  EXPECT_EQ(desktop_clipboard::read(), first);
  EXPECT_FALSE(desktop_clipboard::write({{"text/plain", "YQ=="}}, empty));
  EXPECT_EQ(desktop_clipboard::read(), first);
  EXPECT_TRUE(desktop_clipboard::write(nlohmann::json::object(), desktop_clipboard::digest(first)));
  EXPECT_EQ(desktop_clipboard::read(), nlohmann::json::object());
  desktop_clipboard::testing::end();
}

TEST(DesktopClipboardNativeTest, DoesNotExposeOrOverwriteConcealedContents) {
  desktop_clipboard::testing::begin();
  desktop_clipboard::testing::conceal();
  EXPECT_TRUE(desktop_clipboard::read().is_null());
  EXPECT_FALSE(desktop_clipboard::write({{"text/plain", "YQ=="}}, ""));
  desktop_clipboard::testing::end();
}

TEST(DesktopClipboardNativeTest, ConvertsNativeTiffToShareablePng) {
  desktop_clipboard::testing::begin();
  desktop_clipboard::testing::tiff();
  const auto snapshot = desktop_clipboard::read();
  EXPECT_TRUE(snapshot.contains("image/png"));
  EXPECT_TRUE(desktop_clipboard::valid_items(snapshot));
  desktop_clipboard::testing::end();
}
#endif
