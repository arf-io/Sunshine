/**
 * @file tests/unit/test_nvhttp_https.cpp
 * @brief Verify HTTPS callbacks retain the guard protecting server lifetime.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>

// local includes
#include <src/crypto.h>
#include <src/nvhttp.h>
#include <src/utility.h>

namespace {
  using namespace std::chrono_literals;
  using boost::asio::ip::tcp;

  /**
   * @brief Run real HTTPS callbacks against an isolated loopback listener.
   */
  struct HttpsHandlerLifetimeTest: BaseTest {
    /** @brief Select whether this fixture requests and reports client certificates.
     * @return False for callback lifetime tests.
     */
    virtual bool certificate_mode() const {
      return false;
    }

    /**
     * @brief Create credentials and start the production HTTPS accept handler.
     */
    void SetUp() override {
      BaseTest::SetUp();
      const auto credentials = crypto::gen_creds("Sunshine HTTPS lifetime test", 2048);
      certificate_file = std::filesystem::path {SUNSHINE_TEST_BIN_DIR} / "https_lifetime_cert.pem";
      private_key_file = std::filesystem::path {SUNSHINE_TEST_BIN_DIR} / "https_lifetime_key.pem";
      std::ofstream {certificate_file} << credentials.x509;
      std::ofstream {private_key_file} << credentials.pkey;

      server = nvhttp::test_support::make_https_server(certificate_file.string(), private_key_file.string(), handler_scope, certificate_mode());
      server->io_service = std::make_shared<boost::asio::io_context>();
      server->config.address = "127.0.0.1";
      server->config.port = 0;
      server->start([this](unsigned short assigned_port) {
        port = assigned_port;
      });
      server->io_service->poll();
      ASSERT_NE(port, 0);
    }

    /**
     * @brief Drain canceled callbacks and remove the generated credentials.
     */
    void TearDown() override {
      if (server) {
        server->stop();
        server->io_service->restart();
        server->io_service->poll();
        server.reset();
      }
      if (stop_thread.joinable()) {
        stop_thread.join();
      }
      std::error_code cleanup_error;
      std::filesystem::remove(certificate_file, cleanup_error);
      std::filesystem::remove(private_key_file, cleanup_error);
      BaseTest::TearDown();
    }

    /**
     * @brief Check that stopping the scope runner waits until the error callback exits.
     *
     * @param callback_error Error delivered by the real accept or TLS handshake operation.
     */
    void check_handler_guard(const boost::system::error_code &callback_error) {
      EXPECT_TRUE(callback_error);
      ++error_callbacks;
      std::promise<void> stopping;
      auto stopping_future = stopping.get_future();
      std::promise<void> stopped;
      stopped_future = stopped.get_future();
      stop_thread = std::jthread([this, stopping = std::move(stopping), stopped = std::move(stopped)]() mutable {
        stopping.set_value();
        handler_scope->stop();
        stopped.set_value();
      });
      stopping_future.wait();
      EXPECT_EQ(stopped_future.wait_for(50ms), std::future_status::timeout);
    }

    std::unique_ptr<SimpleWeb::ServerBase<nvhttp::SunshineHTTPS>> server;  ///< Server running production accept and handshake callbacks.
    std::shared_ptr<SimpleWeb::ScopeRunner> handler_scope;  ///< Guard used by the server destructor.
    std::jthread stop_thread;  ///< Concurrent attempt to stop the handler scope.
    std::future<void> stopped_future;  ///< Signals that all callback guards have been released.
    unsigned short port = 0;  ///< Assigned loopback port.
    int error_callbacks = 0;  ///< Number of errors delivered while testing a handler.
    std::filesystem::path certificate_file;  ///< Generated server certificate.
    std::filesystem::path private_key_file;  ///< Generated server private key.
  };

  /** @brief Request client certificates so simultaneous TLS identities can be tested. */
  struct HttpsPeerIdentityTest: HttpsHandlerLifetimeTest {
    /** @brief Enable the isolated peer-identity endpoint.
     * @return True for peer-identity tests.
     */
    bool certificate_mode() const override {
      return true;
    }
  };

  TEST_F(HttpsPeerIdentityTest, InterleavedHandshakesKeepRequestCertificateOwnership) {
    const auto first = crypto::gen_creds("First desktop client", 2048);
    const auto second = crypto::gen_creds("Second desktop client", 2048);
    boost::asio::io_context client_io;
    boost::asio::ssl::context first_context {boost::asio::ssl::context::tls_client};
    boost::asio::ssl::context second_context {boost::asio::ssl::context::tls_client};
    first_context.set_verify_mode(boost::asio::ssl::verify_none);
    second_context.set_verify_mode(boost::asio::ssl::verify_none);
    first_context.use_certificate_chain(boost::asio::buffer(first.x509));
    first_context.use_private_key(boost::asio::buffer(first.pkey), boost::asio::ssl::context::pem);
    second_context.use_certificate_chain(boost::asio::buffer(second.x509));
    second_context.use_private_key(boost::asio::buffer(second.pkey), boost::asio::ssl::context::pem);
    boost::asio::ssl::stream<tcp::socket> first_socket {client_io, first_context};
    boost::asio::ssl::stream<tcp::socket> second_socket {client_io, second_context};
    std::jthread serving([this]() {
      server->io_service->run();
    });
    auto stop = util::fail_guard([this]() {
      server->stop();
      server->io_service->stop();
    });
    first_socket.lowest_layer().connect({boost::asio::ip::address_v4::loopback(), port});
    first_socket.handshake(boost::asio::ssl::stream_base::client);
    second_socket.lowest_layer().connect({boost::asio::ip::address_v4::loopback(), port});
    second_socket.handshake(boost::asio::ssl::stream_base::client);
    const auto fetch = [](auto &socket) {
      const std::string request = "GET /test-peer HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
      boost::asio::write(socket, boost::asio::buffer(request));
      boost::asio::streambuf response;
      boost::system::error_code ec;
      boost::asio::read(socket, response, boost::asio::transfer_all(), ec);
      // Complete the peer's close-notify before asking the single-threaded server
      // to service the other connection; Sunshine shuts TLS down synchronously.
      socket.shutdown(ec);
      socket.lowest_layer().close(ec);
      const std::string wire {boost::asio::buffers_begin(response.data()), boost::asio::buffers_end(response.data())};
      const auto header_end = wire.find("\r\n\r\n");
      return header_end == std::string::npos ? std::string {} : wire.substr(header_end + 4);
    };
    EXPECT_EQ(fetch(first_socket), first.x509);
    EXPECT_EQ(fetch(second_socket), second.x509);
  }

  /**
   * @brief Canceling a pending accept holds the server guard through its error callback.
   */
  TEST_F(HttpsHandlerLifetimeTest, AcceptErrorRetainsGuardUntilCallbackReturns) {
    server->on_error = [this](auto, const auto &callback_error) {
      check_handler_guard(callback_error);
    };
    server->stop();
    server->io_service->run_for(2s);
    ASSERT_EQ(error_callbacks, 1);
    ASSERT_TRUE(stopped_future.valid());
    EXPECT_EQ(stopped_future.wait_for(2s), std::future_status::ready);
  }

  /**
   * @brief A failed TLS handshake holds the server guard through its error callback.
   */
  TEST_F(HttpsHandlerLifetimeTest, HandshakeErrorRetainsGuardUntilCallbackReturns) {
    server->on_error = [this](auto, const auto &callback_error) {
      check_handler_guard(callback_error);
    };
    boost::asio::io_context client_io;
    tcp::socket client {client_io};
    client.connect({boost::asio::ip::address_v4::loopback(), port});
    client.close();
    server->io_service->run_for(2s);
    ASSERT_EQ(error_callbacks, 1);
    ASSERT_TRUE(stopped_future.valid());
    EXPECT_EQ(stopped_future.wait_for(2s), std::future_status::ready);
  }

  /**
   * @brief A stopped server scope prevents a canceled accept from dispatching an error.
   */
  TEST_F(HttpsHandlerLifetimeTest, StoppedScopeSkipsAcceptCallback) {
    server->on_error = [this](auto, const auto &) {
      ++error_callbacks;
    };
    handler_scope->stop();
    server->stop();
    server->io_service->run_for(2s);
    EXPECT_EQ(error_callbacks, 0);
  }

  /**
   * @brief A stopped server scope prevents an outstanding handshake from dispatching an error.
   */
  TEST_F(HttpsHandlerLifetimeTest, StoppedScopeSkipsHandshakeCallback) {
    server->on_error = [this](auto, const auto &) {
      ++error_callbacks;
    };
    boost::asio::io_context client_io;
    tcp::socket client {client_io};
    client.connect({boost::asio::ip::address_v4::loopback(), port});
    server->io_service->poll();
    handler_scope->stop();
    client.close();
    server->io_service->run_for(2s);
    EXPECT_EQ(error_callbacks, 0);
  }
}  // namespace
