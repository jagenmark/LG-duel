#include "net/UdpTransport.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

bool parsePort(std::string_view text, std::uint16_t& port) {
  unsigned int parsedPort = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), parsedPort);
  if (
    result.ec != std::errc{} ||
    result.ptr != text.data() + text.size() ||
    parsedPort == 0 ||
    parsedPort > 65535U
  ) {
    return false;
  }

  port = static_cast<std::uint16_t>(parsedPort);
  return true;
}

} // namespace

int main(int argc, char** argv) {
  const std::string host = argc >= 2 ? argv[1] : "127.0.0.1";
  std::uint16_t port = 27960;
  if (argc >= 3 && !parsePort(argv[2], port)) {
    std::cerr << "Invalid UDP port: " << argv[2] << '\n';
    return 2;
  }

  lg::UdpClientTransport transport(host, port);
  if (!transport.initialize()) {
    std::cerr << "Probe initialization failed: " << transport.lastError() << '\n';
    return 1;
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  lg::ServerSnapshot snapshot;
  while (std::chrono::steady_clock::now() < deadline) {
    transport.update();
    if (transport.connected() && transport.receiveSnapshot(snapshot)) {
      std::cout
        << "OK: connected to " << host << ':' << port
        << " as player " << static_cast<unsigned int>(transport.playerIndex() + 1)
        << "; server tick " << snapshot.serverTick << '\n';
      transport.disconnect();
      return 0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::cerr << "FAILED: no LG Duel handshake and snapshot from "
            << host << ':' << port << " within 3 seconds\n";
  return 1;
}
