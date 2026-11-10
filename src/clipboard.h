/**
 * @file src/clipboard.h
 * @brief Bounded, versioned clipboard snapshots for active paired desktop clients.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>
#include <stdexcept>
#include <string>
#include <string_view>

namespace desktop_clipboard {
  constexpr std::size_t max_bytes = 4 * 1024 * 1024;  ///< Decoded snapshot limit.
  constexpr std::size_t max_wire_bytes = 6 * 1024 * 1024;  ///< JSON request limit.

  /** @brief Validate a MIME-keyed map of canonical base64 clipboard representations.
   * @param items Candidate snapshot.
   * @return True when every format and the aggregate size are supported.
   */
  inline bool valid_items(const nlohmann::json &items) {
    if (!items.is_object() || items.size() > 4) {
      return false;
    }
    std::size_t total = 0;
    for (const auto &[type, value] : items.items()) {
      if ((type != "text/plain" && type != "text/html" && type != "text/rtf" && type != "image/png") || !value.is_string()) {
        return false;
      }
      const auto &encoded = value.get_ref<const std::string &>();
      if (encoded.size() % 4 != 0) {
        return false;
      }
      auto padding = encoded.ends_with("==") ? 2U : encoded.ends_with("=") ? 1U :
                                                                             0U;
      for (std::size_t i = 0; i < encoded.size(); ++i) {
        const auto c = static_cast<unsigned char>(encoded[i]);
        if (i >= encoded.size() - padding ? c != '=' : !(std::isalnum(c) && c < 128) && c != '+' && c != '/') {
          return false;
        }
      }
      // Reject nonzero unused bits, so a payload has only one accepted representation.
      constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      if (padding && (alphabet.find(encoded[encoded.size() - padding - 1]) & (padding == 2 ? 15U : 3U))) {
        return false;
      }
      total += encoded.size() / 4 * 3 - padding;
      if (total > max_bytes) {
        return false;
      }
    }
    return true;
  }

  /** @brief Hash a canonical snapshot for conflict detection without logging its contents.
   * @param items Valid clipboard items.
   * @return Lowercase SHA-256 digest.
   */
  inline std::string digest(const nlohmann::json &items) {
    const auto wire = items.dump();
    std::array<unsigned char, SHA256_DIGEST_LENGTH> hash;
    SHA256(reinterpret_cast<const unsigned char *>(wire.data()), wire.size(), hash.data());
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(hash.size() * 2);
    for (const auto c : hash) {
      result += hex[c >> 4];
      result += hex[c & 15];
    }
    return result;
  }

#ifdef __APPLE__
  /** @brief Read supported clipboard types; return null for concealed or oversized content. */
  nlohmann::json read();
  /** @brief Replace supported clipboard contents after conflict and size validation.
   * @param items MIME-keyed base64 representations.
   * @param expected Digest of the previously observed host clipboard.
   * @return True only if the expected snapshot still matches and the write succeeds.
   */
  bool write(const nlohmann::json &items, const std::string &expected);
  #ifdef SUNSHINE_TESTS
  namespace testing {
    /** @brief Select a fresh isolated native board without touching the user's clipboard. */
    void begin();
    /** @brief Release the isolated native board. */
    void end();
    /** @brief Place a concealed test fixture on the isolated board. */
    void conceal();
    /** @brief Place a native TIFF test image on the isolated board. */
    void tiff();
  }  // namespace testing
  #endif
#endif
}  // namespace desktop_clipboard
