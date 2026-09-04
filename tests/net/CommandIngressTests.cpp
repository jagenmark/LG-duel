#include "net/LoopbackTransport.hpp"
#include "net/UdpTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <thread>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

struct UdpFixture {
  lg::UdpServerTransport server{0};
  std::unique_ptr<lg::UdpClientTransport> client;

  bool connect() {
    if (!server.initialize()) return false;
    client = std::make_unique<lg::UdpClientTransport>("127.0.0.1", server.localPort());
    if (!client->initialize()) return false;
    for (int attempt = 0; attempt < 200; ++attempt) {
      server.update();
      client->update();
      if (client->connected()) {
        // Drain the initial ping before measuring command traffic.
        server.update();
        client->update();
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }

  void send(const lg::CommandPacket& packet) {
    client->sendCommand(packet);
    server.update();
  }
};

int resetOrdering() {
  int failures = 0;
  auto transport = std::make_unique<lg::LoopbackTransport>();
  auto server = std::make_unique<lg::ServerGame>(*transport);
  std::array<bool, lg::kDuelPlayerCount> connected = {true, true};
  std::array<std::uint32_t, lg::kDuelPlayerCount> sessions = {11U, 22U};
  server->setConnectedPlayers(connected, sessions);
  const auto send = [&](const lg::CommandPacket& packet) {
    transport->sendCommand(packet);
    server->tick(lg::kFixedTickSeconds);
  };

  lg::CommandPacket first;
  first.command.sequence = std::numeric_limits<std::uint32_t>::max();
  first.requestReset = true;
  send(first);
  lg::CommandPacket second;
  second.playerIndex = 1;
  second.command.sequence = 100U;
  second.requestReset = true;
  send(second);
  const auto revision = server->snapshot().damageFeedbackRevision;
  send(first);
  --first.command.sequence;
  send(first);
  failures += expect(server->snapshot().damageFeedbackRevision == revision,
    "another player's reset must not let duplicate or older resets run again");
  failures += expect(server->snapshot().hasAcknowledgedCommand[0] &&
      server->snapshot().acknowledgedCommand[0] == std::numeric_limits<std::uint32_t>::max(),
    "reset must preserve all players' command acknowledgements");

  first.command.sequence = 0U;
  first.requestReset = false;
  first.playerName = "WRAPPED";
  send(first);
  failures += expect(server->snapshot().playerNames[0] == "WRAPPED" &&
      server->snapshot().acknowledgedCommand[0] == 0U,
    "new commands must still work when the sequence wraps after reset");
  server->setArena(server->arena());
  first.playerName = "STALE";
  send(first);
  failures += expect(server->snapshot().playerNames[0] == "WRAPPED",
    "a map reset must also preserve duplicate rejection");

  // A new session in the same occupied slot must get a fresh input sequence,
  // even when it arrives immediately after a match reset.
  server->resetMatch();
  sessions[0] = 33U;
  server->setConnectedPlayers(connected, sessions);
  first.playerName = "RECONNECTED";
  send(first);
  failures += expect(server->snapshot().playerNames[0] == "RECONNECTED",
    "a new connection must be able to start its command sequence at zero");
  return failures;
}

int rosterOrdering() {
  int failures = 0;
  auto fixture = std::make_unique<UdpFixture>();
  if (!fixture->connect()) return expect(false, "roster fixture should connect");
  const auto player = fixture->client->playerIndex();
  lg::CommandPacket leave;
  leave.command.sequence = std::numeric_limits<std::uint32_t>::max();
  leave.requestSpectator = true;
  fixture->send(leave);
  failures += expect(!fixture->server.connectedPlayers()[player], "leave should release the body");
  lg::CommandPacket join;
  join.command.sequence = 0U;
  join.requestTeam = true;
  join.requestedTeam = lg::Team::Red;
  fixture->send(join);
  failures += expect(fixture->server.connectedPlayers()[player], "join should work across sequence wrap");
  fixture->send(leave);
  failures += expect(fixture->server.connectedPlayers()[player],
    "a delayed leave must not undo a newer join");
  leave.command.sequence = 1U;
  fixture->send(leave);
  fixture->send(join);
  failures += expect(!fixture->server.connectedPlayers()[player],
    "a delayed join must not undo a newer leave");
  return failures;
}

int boundedQueue() {
  int failures = 0;
  auto fixture = std::make_unique<UdpFixture>();
  if (!fixture->connect()) return expect(false, "queue fixture should connect");
  lg::CommandPacket command;
  for (std::size_t index = 0; index < lg::kMaxQueuedServerCommands; ++index) {
    command.command.sequence = static_cast<std::uint32_t>(index + 1U);
    fixture->send(command);
  }
  ++command.command.sequence;
  fixture->send(command);
  const std::uint32_t retrySequence = command.command.sequence;
  std::size_t count = 0;
  lg::CommandPacket received;
  while (fixture->server.receiveCommand(received)) ++count;
  failures += expect(count == lg::kMaxQueuedServerCommands,
    "redundant command bundles must not exceed the server queue limit");
  // Retransmission must remain possible: dropping for capacity must not mark a
  // command consumed before the game has had a chance to receive it.
  fixture->send(command);
  failures += expect(fixture->server.receiveCommand(received) &&
      received.command.sequence == retrySequence,
    "a command dropped by a full queue must be accepted on retry");
  failures += expect(!fixture->server.receiveCommand(received),
    "retried bundle history must not replay already consumed commands");
  return failures;
}

int boundedPump() {
  auto fixture = std::make_unique<UdpFixture>();
  if (!fixture->connect()) return expect(false, "packet fixture should connect");
  lg::CommandPacket command;
  for (std::size_t index = 0; index < lg::kMaxServerDatagramsPerUpdate + 1U; ++index) {
    command.command.sequence = static_cast<std::uint32_t>(index + 1U);
    fixture->client->sendCommand(command);
  }
  fixture->server.update();
  std::size_t count = 0;
  lg::CommandPacket received;
  while (fixture->server.receiveCommand(received)) ++count;
  int failures = expect(count > 0 && count <= lg::kMaxServerDatagramsPerUpdate,
    "one update must yield after its packet budget, even with more traffic queued");
  // UDP may drop excess packets on platforms with small socket buffers. After
  // the burst, a retried newest input must still reach the game.
  fixture->send(command);
  std::uint32_t newest = 0;
  while (fixture->server.receiveCommand(received)) newest = received.command.sequence;
  failures += expect(newest == command.command.sequence,
    "packet budget exhaustion must allow progress on the next update");
  return failures;
}

} // namespace

int main() {
  const int failures = resetOrdering() + rosterOrdering() + boundedQueue() + boundedPump();
  return failures == 0 ? 0 : 1;
}
