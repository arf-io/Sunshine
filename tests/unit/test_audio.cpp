/**
 * @file tests/unit/test_audio.cpp
 * @brief Test src/audio.*.
 */

// test includes
#include "../tests_common.h"

// lib includes
#include <opus/opus_multistream.h>

// local includes
#include <src/audio.h>
#include <src/rtsp.h>
#include <src/stream.h>

namespace audio {
  using captured_samples_t = std::pair<std::vector<float>, std::chrono::steady_clock::time_point>;
  void encodeThread(std::shared_ptr<safe::queue_t<captured_samples_t>> samples, config_t config, stream::session_t *channel_data);
}  // namespace audio

using namespace audio;

TEST(AudioLayoutTest, CustomMappingRemainsContiguousAndUsesAllEightChannels) {
  const stream_params_t params {8, 6, 2, {0, 1, 4, 5, 2, 3, 6, 7}};
  auto copy = params;
  EXPECT_EQ(copy.channelCount, 8);
  EXPECT_EQ(copy.streams, 6);
  EXPECT_EQ(copy.coupledStreams, 2);
  EXPECT_EQ(copy.mapping.size(), 8U);
  EXPECT_EQ(copy.mapping.back(), 7);
  EXPECT_EQ(copy.mapping.data() + 7, &copy.mapping[7]);
  copy.mapping[0] = 7;
  EXPECT_EQ(params.mapping[0], 0);
  EXPECT_EQ(stream_configs.size(), MAX_STREAM_CONFIG);
  EXPECT_EQ(stream_configs[HIGH_SURROUND71].channelCount, 8);
}

TEST(AudioPacketSessionTest, EncoderPreservesTheReceivingSession) {
  stream::config_t stream_config {};
  rtsp_stream::launch_session_t launch_session {};
  launch_session.gcm_key.resize(16);
  launch_session.iv.resize(16);
  auto session = stream::session::alloc(stream_config, launch_session);
  ASSERT_NE(session.get(), nullptr);
  config_t config {.packetDuration = 5, .channels = 2, .mask = 0x3};
  auto samples = std::make_shared<safe::queue_t<captured_samples_t>>();
  auto packets = mail::man->queue<packet_t>(mail::audio_packets);
  std::jthread worker([&samples, &config, &session] {
    audio::encodeThread(samples, config, session.get());
  });
  auto stop_worker = util::fail_guard([&samples] {
    samples->stop();
  });
  ASSERT_TRUE(samples->raise(captured_samples_t {std::vector<float>(480, 0.0F), std::chrono::steady_clock::now()}));
  auto packet = packets->pop(2s);
  ASSERT_TRUE(packet);
  EXPECT_EQ(packet->first, session.get());
  EXPECT_GT(packet->second.size(), 0U);
}

TEST(AudioPacketSessionTest, NegotiatedCaptureTimestampWrapsAValidOpusPacket) {
  stream::config_t stream_config {};
  rtsp_stream::launch_session_t launch_session {};
  launch_session.gcm_key.resize(16);
  launch_session.iv.resize(16);
  launch_session.arf_av_sync = true;
  auto session = stream::session::alloc(stream_config, launch_session);
  ASSERT_NE(session.get(), nullptr);
  config_t config {.packetDuration = 5, .channels = 2, .mask = 0x3};
  auto samples = std::make_shared<safe::queue_t<captured_samples_t>>();
  auto packets = mail::man->queue<packet_t>(mail::audio_packets);
  std::jthread worker([&]() {
    audio::encodeThread(samples, config, session.get());
  });
  auto stop_worker = util::fail_guard([&]() {
    samples->stop();
  });
  constexpr uint64_t captured_us = 0x0000000102030405ULL;
  const std::chrono::steady_clock::time_point captured {std::chrono::microseconds(captured_us)};
  ASSERT_TRUE(samples->raise(captured_samples_t {std::vector<float>(480, 0.0F), captured}));
  auto packet = packets->pop(2s);
  ASSERT_TRUE(packet);
  ASSERT_GT(packet->second.size(), 12U);
  const std::array<uint8_t, 12> golden {'A', 'R', 'F', '1', 0, 0, 0, 1, 2, 3, 4, 5};
  EXPECT_TRUE(std::equal(golden.begin(), golden.end(), std::begin(packet->second)));
  const unsigned char mapping[] {0, 1};
  int status = OPUS_OK;
  OpusMSDecoder *decoder = opus_multistream_decoder_create(48000, 2, 1, 1, mapping, &status);
  ASSERT_EQ(status, OPUS_OK);
  ASSERT_NE(decoder, nullptr);
  std::array<float, 480> pcm;
  EXPECT_EQ(opus_multistream_decode_float(decoder, std::begin(packet->second) + 12, packet->second.size() - 12, pcm.data(), 240, 0), 240);
  opus_multistream_decoder_destroy(decoder);
}

struct AudioTest: PlatformTestSuite, testing::WithParamInterface<std::tuple<std::basic_string_view<char>, config_t>> {
  void SetUp() override {
    BaseTest::SetUp();
    m_config = std::get<1>(GetParam());
    m_mail = std::make_shared<safe::mail_raw_t>();
  }

  config_t m_config;
  safe::mail_t m_mail;
};

constexpr std::bitset<config_t::MAX_FLAGS> config_flags(const int flag = -1) {
  auto result = std::bitset<config_t::MAX_FLAGS>();
  if (flag >= 0) {
    result.set(flag);
  }
  return result;
}

INSTANTIATE_TEST_SUITE_P(
  Configurations,
  AudioTest,
  testing::Values(
    std::make_tuple("HIGH_STEREO", config_t {.packetDuration = 5, .channels = 2, .mask = 0x3, .customStreamParams = {}, .flags = config_flags(config_t::HIGH_QUALITY)}),
    std::make_tuple("SURROUND51", config_t {.packetDuration = 5, .channels = 6, .mask = 0x3F, .customStreamParams = {}, .flags = config_flags()}),
    std::make_tuple("SURROUND71", config_t {.packetDuration = 5, .channels = 8, .mask = 0x63F, .customStreamParams = {}, .flags = config_flags()}),
    std::make_tuple("SURROUND51_CUSTOM", config_t {5, 6, 0x3F, {6, 4, 2, {0, 1, 4, 5, 2, 3}}, config_flags(config_t::CUSTOM_SURROUND_PARAMS)})
  ),
  [](const auto &info) {
    return std::string(std::get<0>(info.param));
  }
);

TEST_P(AudioTest, TestEncode) {
  std::jthread timer([this] {
    // Terminate the audio capture after 100 ms
    std::this_thread::sleep_for(100ms);
    const auto shutdown_event = m_mail->event<bool>(mail::shutdown);
    const auto audio_packets = m_mail->queue<packet_t>(mail::audio_packets);
    shutdown_event->raise(true);
    audio_packets->stop();
  });
  std::jthread capture([this] {
    const auto packets = m_mail->queue<packet_t>(mail::audio_packets);
    const auto shutdown_event = m_mail->event<bool>(mail::shutdown);
    while (const auto packet = packets->pop()) {
      if (shutdown_event->peek()) {
        break;
      }
      if (auto packet_data = packet->second; packet_data.size() == 0) {
        FAIL() << "Empty packet data";
      }
    }
  });
  audio::capture(m_mail, m_config, nullptr);

  timer.join();
  capture.join();
}
