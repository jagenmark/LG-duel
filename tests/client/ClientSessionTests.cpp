#include "client/ClientSession.hpp"
#include "net/UdpTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool pumpUntilConnected(
  lg::ClientSession& session,
  lg::UdpServerTransport& transport,
  lg::ServerGame& server
) {
  for (int iteration = 0; iteration < 200; ++iteration) {
    session.update();
    transport.update();
    server.setConnectedPlayers(transport.connectedPlayers());
    server.tick(lg::kFixedTickSeconds);
    session.update();
    if (session.readyForPlay()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

} // namespace

int main() {
  int failures = 0;
  lg::UdpServerTransport transport(0);
  failures += expect(transport.initialize(), "server transport should initialize");
  if (failures != 0) {
    return 1;
  }

  lg::ServerGame server(transport);
  server.setConnectedPlayers({false, false});
  lg::ClientSession session;
  failures += expect(
    session.connect("127.0.0.1", transport.localPort()),
    "session should start connecting"
  );
  failures += expect(
    pumpUntilConnected(session, transport, server),
    "session should connect and receive a snapshot"
  );

  session.disconnect();
  transport.update();
  server.setConnectedPlayers(transport.connectedPlayers());
  server.tick(lg::kFixedTickSeconds);
  failures += expect(
    session.state() == lg::ClientConnectionState::Disconnected &&
      transport.connectedClientCount() == 0,
    "disconnect should release the server slot"
  );

  failures += expect(session.reconnect(), "reconnect should reuse the latest endpoint");
  failures += expect(
    pumpUntilConnected(session, transport, server),
    "reconnect should establish a new playable session"
  );

  return failures == 0 ? 0 : 1;
}
