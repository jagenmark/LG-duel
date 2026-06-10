#include "client/ClientGame.hpp"
#include "net/LoopbackTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include "sim/MovementModes.hpp"
#include "sim/UserCommand.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

constexpr float kPi = 3.14159265359F;

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::ServerSnapshot latestSnapshot(lg::LoopbackTransport& transport) {
  lg::ServerSnapshot latest;
  lg::ServerSnapshot received;
  while (transport.receiveSnapshot(received)) {
    latest = received;
  }
  return latest;
}

} // namespace

int main() {
  int failures = 0;

  {
    lg::LoopbackTransport transport;
    lg::CommandPacket first;
    first.command.sequence = 3;
    lg::CommandPacket second;
    second.command.sequence = 4;
    transport.sendCommand(first);
    transport.sendCommand(second);

    lg::CommandPacket received;
    failures += expect(transport.receiveCommand(received), "loopback should return first queued command");
    failures += expect(received.command.sequence == 3, "loopback command order should be FIFO");
    failures += expect(transport.receiveCommand(received), "loopback should return second queued command");
    failures += expect(received.command.sequence == 4, "loopback should preserve all queued commands");
    failures += expect(!transport.receiveCommand(received), "empty loopback command queue should report false");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::UserCommand beforeWrap;
    beforeWrap.sequence = std::numeric_limits<std::uint32_t>::max();
    transport.sendCommand(lg::CommandPacket{0, beforeWrap, false});
    server.tick(lg::kFixedTickSeconds);

    lg::UserCommand afterWrap;
    afterWrap.sequence = 0;
    afterWrap.viewYawRadians = kPi;
    transport.sendCommand(lg::CommandPacket{0, afterWrap, false});
    server.tick(lg::kFixedTickSeconds);

    const lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(snapshot.acknowledgedCommand[0] == 0, "sequence zero should follow uint32 wrap");
    failures += expect(snapshot.players[0].viewYawRadians == kPi, "wrapped command should be simulated");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    lg::ClientGame client(transport, 0);
    client.receiveSnapshots();

    failures += expect(client.hasSnapshot(), "server should publish an initial snapshot");
    failures += expect(client.snapshot().serverTick == 0, "initial snapshot should start at server tick zero");
    failures += expect(!client.hasAcknowledgedCommand(), "initial snapshot should not acknowledge a command");
    failures += expect(
      client.snapshot().players[0].movementMode == lg::MovementMode::Grounded,
      "snapshot should preserve local movement mode"
    );
    failures += expect(
      client.snapshot().players[1].movementMode == lg::MovementMode::Grounded,
      "snapshot should preserve remote movement mode"
    );

    lg::UserCommand command;
    command.sequence = 10;
    command.clientTick = 20;
    command.forwardMove = 1.0F;
    command.attack = true;
    client.sendCommand(command, false);
    server.tick(lg::kFixedTickSeconds);
    client.receiveSnapshots();

    failures += expect(client.snapshot().serverTick == 1, "server tick should advance once per simulation step");
    failures += expect(client.hasAcknowledgedCommand(), "accepted command should set ack validity");
    failures += expect(client.lastAcknowledgedCommand() == 10, "snapshot should acknowledge accepted command");
    failures += expect(client.snapshot().players[0].position.x > -3.0F, "server should simulate accepted movement");
    failures += expect(client.snapshot().lightningGuns[0].hit, "server should authoritatively trace LG");

    lg::UserCommand duplicate = command;
    duplicate.viewYawRadians = kPi;
    client.sendCommand(duplicate, false);
    server.tick(lg::kFixedTickSeconds);
    client.receiveSnapshots();

    failures += expect(client.lastAcknowledgedCommand() == 10, "duplicate command should not change ack");
    failures += expect(
      client.snapshot().players[0].viewYawRadians == 0.0F,
      "duplicate command should not overwrite authoritative view"
    );

    lg::UserCommand stale = duplicate;
    stale.sequence = 9;
    client.sendCommand(stale, false);
    server.tick(lg::kFixedTickSeconds);
    client.receiveSnapshots();

    failures += expect(client.lastAcknowledgedCommand() == 10, "out-of-order command should be ignored");
    failures += expect(
      client.snapshot().players[0].viewYawRadians == 0.0F,
      "out-of-order command should not change state"
    );

    lg::UserCommand reset;
    reset.sequence = 11;
    client.sendCommand(reset, true);
    server.tick(lg::kFixedTickSeconds);
    client.receiveSnapshots();

    failures += expect(client.snapshot().serverTick == 4, "reset should preserve monotonic server ticks");
    failures += expect(client.snapshot().players[0].position.x == -3.0F, "client should receive reset spawn");
    failures += expect(client.snapshot().players[1].health == 100, "client should receive reset health");

    lg::UserCommand postResetMove;
    postResetMove.sequence = 12;
    postResetMove.forwardMove = 1.0F;
    client.sendCommand(postResetMove, false);
    server.tick(lg::kFixedTickSeconds);
    client.receiveSnapshots();
    const float movedPosition = client.snapshot().players[0].position.x;

    lg::UserCommand staleReset;
    staleReset.sequence = 11;
    client.sendCommand(staleReset, true);
    server.tick(lg::kFixedTickSeconds);
    client.receiveSnapshots();

    failures += expect(client.lastAcknowledgedCommand() == 12, "stale reset should not change ack");
    failures += expect(
      client.snapshot().players[0].position.x >= movedPosition,
      "stale reset packet should not restore spawn"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    for (std::uint32_t sequence = 0; sequence < 20; ++sequence) {
      lg::UserCommand targetCommand;
      targetCommand.sequence = sequence;
      targetCommand.viewYawRadians = kPi;
      targetCommand.rightMove = 1.0F;
      transport.sendCommand(lg::CommandPacket{1, targetCommand, false, false, sequence});
      server.tick(lg::kFixedTickSeconds);
    }

    const lg::ServerSnapshot beforeAttack = latestSnapshot(transport);
    failures += expect(
      beforeAttack.players[1].position.y < -beforeAttack.players[1].bounds.radius,
      "moving target should leave the uncompensated beam path"
    );

    lg::UserCommand attack;
    attack.sequence = 0;
    attack.attack = true;
    transport.sendCommand(lg::CommandPacket{0, attack, false, 0});
    server.tick(lg::kFixedTickSeconds);

    const lg::ServerSnapshot compensated = latestSnapshot(transport);
    failures += expect(compensated.lightningGuns[0].hit, "rewound LG should hit historical target");
    failures += expect(
      compensated.players[1].health == 100,
      "first fixed-tick hit should retain fractional damage against current state"
    );
    failures += expect(
      compensated.lightningGuns[0].requestedRewindTicks == 20 &&
        compensated.lightningGuns[0].appliedRewindTicks == 20 &&
        !compensated.lightningGuns[0].rewindClamped,
      "LG should report an in-range historical rewind"
    );

    attack.sequence = 1;
    transport.sendCommand(lg::CommandPacket{0, attack, false, 0});
    server.tick(lg::kFixedTickSeconds);
    const lg::ServerSnapshot damaged = latestSnapshot(transport);
    failures += expect(damaged.lightningGuns[0].hit, "continuous rewound LG should remain active");
    failures += expect(
      damaged.players[1].health == 99,
      "rewound hit damage should apply to the current authoritative target"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    for (std::uint32_t sequence = 0; sequence < 40; ++sequence) {
      lg::UserCommand targetCommand;
      targetCommand.sequence = sequence;
      targetCommand.viewYawRadians = kPi;
      transport.sendCommand(lg::CommandPacket{1, targetCommand, false, false, sequence});
      server.tick(lg::kFixedTickSeconds);
    }
    latestSnapshot(transport);

    lg::UserCommand attack;
    attack.sequence = 0;
    attack.attack = true;
    transport.sendCommand(lg::CommandPacket{0, attack, false, 0});
    server.tick(lg::kFixedTickSeconds);

    const lg::ServerSnapshot clamped = latestSnapshot(transport);
    failures += expect(
      clamped.lightningGuns[0].requestedRewindTicks == 40,
      "LG should report the full requested rewind"
    );
    failures += expect(
      clamped.lightningGuns[0].appliedRewindTicks == 25 &&
        clamped.lightningGuns[0].rewindClamped,
      "LG rewind should clamp to 200 ms"
    );
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    for (std::uint32_t sequence = 0; sequence < 2; ++sequence) {
      lg::UserCommand firstCommand;
      firstCommand.sequence = sequence;
      firstCommand.attack = true;
      lg::UserCommand secondCommand = firstCommand;
      secondCommand.viewYawRadians = kPi;
      transport.sendCommand(lg::CommandPacket{0, firstCommand, false});
      transport.sendCommand(lg::CommandPacket{1, secondCommand, false});
      server.tick(lg::kFixedTickSeconds);
    }

    const lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(snapshot.players[0].health == 99, "player one beam should apply fixed-tick damage");
    failures += expect(snapshot.players[1].health == 99, "simultaneous beams should apply symmetrically");
  }

  {
    lg::LoopbackTransport transport;
    lg::ServerGame server(transport);
    latestSnapshot(transport);

    lg::MatchRules rules;
    rules.roundLimit = 2;
    rules.countdownTicks = 2;
    rules.roundEndTicks = 2;
    rules.matchEndTicks = 3;
    server.setMatchRules(rules);
    server.setConnectedPlayers({true, false});
    server.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::WaitingForPlayers,
      "one connected player should remain in the lobby"
    );
    failures += expect(
      snapshot.connectedPlayers == std::array<bool, 2>{true, false},
      "snapshot should replicate occupied player slots"
    );

    server.setConnectedPlayers({true, true});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::WaitingForReady,
      "two connected players should wait for ready-up"
    );

    lg::UserCommand firstReady;
    firstReady.sequence = 0;
    transport.sendCommand(lg::CommandPacket{0, firstReady, false, true, 0});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.readyPlayers == std::array<bool, 2>{true, false},
      "first ready request should only ready its player"
    );

    lg::UserCommand secondReady;
    secondReady.sequence = 0;
    transport.sendCommand(lg::CommandPacket{1, secondReady, false, true, 0});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::Countdown &&
        snapshot.phaseTicksRemaining == 2,
      "all ready players should begin the configured countdown"
    );

    lg::UserCommand countdownCommand;
    countdownCommand.sequence = 1;
    countdownCommand.forwardMove = 1.0F;
    countdownCommand.attack = true;
    transport.sendCommand(lg::CommandPacket{0, countdownCommand, false, false, 0});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.players[0].position.x > -3.0F,
      "players should be able to move during countdown"
    );
    failures += expect(
      !snapshot.lightningGuns[0].active && snapshot.players[1].health == 100,
      "weapons should remain locked during countdown"
    );

    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::Live,
      "countdown expiry should unlock live play"
    );

    std::uint32_t lastAttackSequence = 0;
    for (std::uint32_t sequence = 0; sequence < 200; ++sequence) {
      lg::UserCommand command;
      command.sequence = sequence + 2;
      command.clientTick = sequence;
      command.attack = true;
      transport.sendCommand(lg::CommandPacket{0, command, false});
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      lastAttackSequence = sequence;
      if (snapshot.players[1].health == 0) {
        break;
      }
    }

    failures += expect(snapshot.players[1].health == 0, "authoritative LG should kill the target");
    failures += expect(
      snapshot.acknowledgedCommand[0] == lastAttackSequence + 2,
      "server should ack latest combat command"
    );
    failures += expect(
      snapshot.scores[0] == 1 &&
        snapshot.matchPhase == lg::MatchPhase::RoundEnd &&
        snapshot.roundWinner == 0,
      "non-final kill should score and enter round end"
    );

    lg::UserCommand deadTargetCommand;
    deadTargetCommand.sequence = 1;
    deadTargetCommand.viewYawRadians = kPi;
    deadTargetCommand.attack = true;
    transport.sendCommand(lg::CommandPacket{1, deadTargetCommand, false});
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(!snapshot.lightningGuns[1].active, "weapons should be locked after round end");

    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::Countdown &&
        snapshot.scores == std::array<std::uint16_t, 2>{1, 0} &&
        snapshot.players[0].health == 100 &&
        snapshot.players[1].health == 100,
      "round-end expiry should respawn both players into a new countdown"
    );

    server.tick(lg::kFixedTickSeconds);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::Live,
      "new round countdown should return to live play"
    );

    std::uint32_t secondRoundSequence = lastAttackSequence + 3;
    for (int tick = 0; tick < 200; ++tick) {
      lg::UserCommand command;
      command.sequence = secondRoundSequence++;
      command.attack = true;
      transport.sendCommand(lg::CommandPacket{0, command, false});
      server.tick(lg::kFixedTickSeconds);
      snapshot = latestSnapshot(transport);
      if (snapshot.matchPhase == lg::MatchPhase::MatchEnd) {
        break;
      }
    }
    failures += expect(
      snapshot.scores[0] == 2 &&
        snapshot.matchPhase == lg::MatchPhase::MatchEnd &&
        snapshot.matchWinner == 0,
      "configured round limit should end the match"
    );

    server.tick(lg::kFixedTickSeconds);
    server.tick(lg::kFixedTickSeconds);
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);
    failures += expect(
      snapshot.matchPhase == lg::MatchPhase::WaitingForReady &&
        snapshot.scores == std::array<std::uint16_t, 2>{0, 0} &&
        snapshot.players[0].health == 100 &&
        snapshot.players[1].health == 100,
      "match-end expiry should reset scores, readiness, and both spawns"
    );

    lg::UserCommand resetCommand;
    resetCommand.sequence = secondRoundSequence;
    transport.sendCommand(lg::CommandPacket{0, resetCommand, true});
    const std::uint32_t tickBeforeReset = snapshot.serverTick;
    server.tick(lg::kFixedTickSeconds);
    snapshot = latestSnapshot(transport);

    failures += expect(snapshot.players[0].health == 100, "reset should restore local health");
    failures += expect(snapshot.players[1].health == 100, "reset should restore remote health");
    failures += expect(snapshot.players[0].position.x == -3.0F, "reset should restore local spawn");
    failures += expect(snapshot.players[1].position.x == 3.0F, "reset should restore remote spawn");
    failures += expect(
      snapshot.acknowledgedCommand[0] == resetCommand.sequence,
      "reset command should be acknowledged"
    );
    failures += expect(
      snapshot.serverTick == tickBeforeReset + 1,
      "match reset should not rewind server tick"
    );
  }

  return failures == 0 ? 0 : 1;
}
