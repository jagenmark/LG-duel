#include "server/ServerApp.hpp"

#include "net/UdpTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace lg {

ServerApp::ServerApp(std::uint16_t port) : port_(port) {}

int ServerApp::run() const {
  UdpServerTransport transport(port_);
  if (!transport.initialize()) {
    std::cerr << "UDP server initialization failed: " << transport.lastError() << '\n';
    return 1;
  }

  ServerGame server(transport);
  std::cout << "LG Duel server listening on UDP port " << transport.localPort() << '\n';

  using Clock = std::chrono::steady_clock;
  const auto tickDuration = std::chrono::duration_cast<Clock::duration>(
    std::chrono::duration<float>(kFixedTickSeconds)
  );
  auto nextTick = Clock::now();
  std::size_t previousClientCount = 0;

  while (true) {
    nextTick += tickDuration;
    transport.update();
    server.tick(kFixedTickSeconds);

    const std::size_t clientCount = transport.connectedClientCount();
    if (clientCount != previousClientCount) {
      std::cout << "Connected clients: " << clientCount << '\n';
      previousClientCount = clientCount;
    }

    std::this_thread::sleep_until(nextTick);
    const auto now = Clock::now();
    if (now - nextTick > tickDuration * 8) {
      nextTick = now;
    }
  }
}

} // namespace lg
