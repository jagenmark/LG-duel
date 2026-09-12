#include "net/LoopbackTransport.hpp"
#include "net/NetCodec.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"
#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const char* message) {
  if (!ok) { ++failures; std::cerr << "FAILED: " << message << '\n'; }
}
lg::ScenarioSetup setup(int firstHealth = 100, int secondHealth = 100) {
  lg::ScenarioSetup result;
  result.match.gameMode = lg::GameMode::GlideDuel;
  result.match.phase = lg::MatchPhase::Live;
  result.match.phaseTicksRemaining = lg::kGlideRoundTicks;
  for (std::size_t i = 0; i < 2; ++i) {
    auto& p = result.players[i];
    p.connected = p.ready = p.alive = p.onGround = true;
    p.health = i == 0 ? firstHealth : secondHealth;
    p.position = {i == 0 ? -6.0F : 6.0F, 0.0F, 0.9F};
    p.viewYawRadians = i == 0 ? 0.0F : 3.14159265F;
  }
  return result;
}
void apply(lg::ServerGame& game, const lg::ScenarioSetup& state) {
  std::string error;
  check(game.applyScenarioSetup(state, &error), error.c_str());
}
void tick(lg::ServerGame& game, lg::LoopbackTransport& transport, int count = 1) {
  for (int i = 0; i < count; ++i) {
    game.tick(lg::kFixedTickSeconds);
    lg::ServerSnapshot ignored;
    while (transport.receiveSnapshot(ignored)) {}
  }
}
}
int main() {
  lg::Arena arena;
  arena.min = {-20, -20, 0}; arena.max = {20, 20, 10};
  arena.wallCount = 0;
  arena.spawnCount = 2;
  arena.spawnPositions[0] = {-6, 0, 0}; arena.spawnPositions[1] = {6, 0, 0};
  lg::LoopbackTransport transport;
  lg::ServerGame game(transport);
  game.setArena(arena);
  // Exercise the actual mode request, codec, and warmup transition.
  game.setConnectedPlayers({true, false});
  lg::CommandPacket request;
  request.playerIndex = 0; request.command.sequence = 1;
  request.requestGameMode = true; request.requestedGameMode = lg::GameMode::GlideDuel;
  lg::WirePacket wire;
  lg::CommandPacket decoded;
  check(lg::encodeCommandPacket(request, wire) && lg::decodeCommandPacket(wire, decoded) &&
    decoded.requestedGameMode == lg::GameMode::GlideDuel, "mode request roundtrips over the wire");
  transport.sendCommand(decoded); tick(game, transport);
  check(game.snapshot().gameMode == lg::GameMode::GlideDuel, "warmup accepts glide mode request");
  (void)game.addBots();
  check(game.isBotSlot(1) && !game.isBotSlot(2), "solo mode fills exactly one opponent slot");
  auto state = setup(100, 60);
  state.match.phaseTicksRemaining = 1;
  apply(game, state);
  check(game.snapshot().movementTuning.glideMovementEnabled, "mode enables replicated glide movement");
  check(game.snapshot().matchRules.roundLimit == 7 && game.matchRules().playerLimit == 2,
    "mode applies first-to-seven and two-player rules");
  lg::ServerSnapshot decodedSnapshot;
  check(lg::encodeServerSnapshot(game.snapshot(), wire) &&
    lg::decodeServerSnapshot(wire, decodedSnapshot) &&
    decodedSnapshot.gameMode == lg::GameMode::GlideDuel &&
    decodedSnapshot.phaseTicksRemaining == 1 && decodedSnapshot.movementTuning.glideMovementEnabled,
    "replicated mode, round clock and movement roundtrip");
  tick(game, transport);
  check(game.snapshot().scores[0] == 1 && game.snapshot().matchPhase == lg::MatchPhase::RoundEnd,
    "health leader wins at buzzer");
  tick(game, transport, 500);
  check(game.snapshot().matchPhase == lg::MatchPhase::Live &&
    game.snapshot().phaseTicksRemaining > 5500 && game.snapshot().players[1].health == 100,
    "next round resets health and the round clock after short intermission");
  state = setup(); state.match.phaseTicksRemaining = 1;
  apply(game, state); tick(game, transport);
  check(game.snapshot().scores[0] == 0 && game.snapshot().scores[1] == 0 &&
    game.snapshot().matchPhase == lg::MatchPhase::RoundEnd, "equal-health timeout is a scoreless draw");
  state = setup(80, 60); state.match.phaseTicksRemaining = 1; state.match.scores[0] = 6;
  apply(game, state); tick(game, transport);
  check(game.snapshot().matchPhase == lg::MatchPhase::MatchEnd && game.snapshot().matchWinner == 0,
    "seventh win ends the match");

  // Verify damage through ordinary command ingress, including Overdrive.
  auto fire = [&](std::uint16_t clock) {
    auto live = setup(); live.match.phaseTicksRemaining = clock;
    apply(game, live);
    lg::CommandPacket packet;
    packet.playerIndex = 0; packet.command.sequence = 1;
    packet.command.weapon = lg::Weapon::Railgun; packet.command.attack = true;
    packet.command.planarAim = false;
    packet.command.viewPitchRadians = std::atan2(-0.65F, 12.0F);
    transport.sendCommand(packet);
    tick(game, transport);
    return game.snapshot().players[1].health;
  };
  const int normal = fire(lg::kGlideRoundTicks);
  const int overdrive = fire(lg::kGlideOverdriveTicks);
  check(normal < 100 && 100 - overdrive == std::min(100, 2 * (100 - normal)),
    "Overdrive doubles ordinary weapon damage");

  // Two independent brains must make progress and land shots at glide speed.
  state = setup();
  for (std::size_t i = 0; i < 2; ++i) {
    state.players[i].connected = false; state.players[i].bot = true;
  }
  apply(game, state); game.setBotAttackMode(lg::BotAttackMode::Hard);
  bool moved = false, fought = false, roundEnded = false;
  int slideTicks = 0;
  for (int i = 0; i < 5600; ++i) {
    tick(game, transport);
    const auto& snap = game.snapshot();
    slideTicks += lg::isGlideSlideActive(snap.players[0], snap.movementTuning) ? 1 : 0;
    moved |= std::fabs(snap.players[0].position.x + 6.0F) > 1.0F;
    fought |= snap.players[0].health < 100 || snap.players[1].health < 100;
    roundEnded |= snap.scores[0] > 0 || snap.scores[1] > 0;
    check(std::isfinite(snap.players[0].position.x), "bot movement remains finite");
  }
  check(game.botHiddenAttackInvariantCount() == 0, "glide bots retain visibility restrictions");
  check(moved && fought && roundEnded, "glide bots move, deal damage, and resolve rounds");
  state = setup();
  state.players[0].connected = false;
  state.players[0].bot = true;
  state.players[0].position.x = -15.0F;
  state.players[1].position.x = 15.0F;
  apply(game, state);
  game.setBotAttackMode(lg::BotAttackMode::Off);
  for (int i = 0; i < 750; ++i) {
    tick(game, transport);
    slideTicks += lg::isGlideSlideActive(game.snapshot().players[0], game.snapshot().movementTuning) ? 1 : 0;
  }
  check(slideTicks > 0, "bot deliberately slides while closing a long safe route");
  std::cout << "Glide bot slide ticks: " << slideTicks << "\n";
  const auto loaded = lg::loadArenaFromFile("maps/glide_duel.map");
  check(loaded.ok, "packaged glide arena loads");
  if (loaded.ok) {
    // Sweep the same tangent route around every mirrored cover chamfer.
    // These points clipped the former square corners at player radius.
    for (const float sx : {-1.0F, 1.0F}) for (const float sy : {-1.0F, 1.0F}) {
      lg::PlayerState skater;
      skater.position = {sx * 6.3F, sy * 4.55F, skater.bounds.halfHeight};
      check(!lg::playerPositionSolid(loaded.arena, skater, skater.position),
        "all mirrored cover corner cutouts admit a player cylinder");
      skater.position = {sx * 7.5F, sy * 5.75F, skater.bounds.halfHeight};
      check(lg::playerPositionSolid(loaded.arena, skater, skater.position),
        "beveled cover still blocks its center");
      skater.position = {sx * 5.5F, sy * 5.3F, skater.bounds.halfHeight};
      skater.onGround = true;
      skater.velocity = {sx * 7.42462F, -sy * 7.42462F, 0};
      lg::MovementTuning glide;
      glide.glideMovementEnabled = true;
      lg::UserCommand slide;
      slide.crouch = true;
      for (int tick = 0; tick < 30; ++tick)
        lg::simulateMovement(skater, slide, loaded.arena, glide, lg::kFixedTickSeconds);
      check(std::hypot(skater.velocity.x, skater.velocity.y) > 9.0F &&
        sx * skater.position.x > 7.0F,
        "high-speed tangent slides clear every beveled cover corner without snagging");
    }
    game.setArena(loaded.arena);
    state = setup();
    for (std::size_t i = 0; i < 2; ++i) {
      state.players[i].connected = false;
      state.players[i].bot = true;
      state.players[i].position = loaded.arena.spawnPositions[i] + lg::Vec3{0, 0, 0.9F};
    }
    apply(game, state);
    game.setBotAttackMode(lg::BotAttackMode::Medium);
    int matchEnds = 0;
    int combatTicks = 0;
    int arenaSlideTicks = 0;
    int roundEnds = 0, draws = 0;
    auto previousScores = game.snapshot().scores;
    const auto initialStats = game.botRuntimeStats();
    auto previousPhase = game.snapshot().matchPhase;
    constexpr int progressTicks = 125 * 300;
    const auto& rules = game.snapshot().matchRules;
    const int fullRoundTicks = lg::kGlideRoundTicks + rules.roundEndTicks + rules.countdownTicks;
    // A close first-to-seven needs 13 decisive rounds, each allowed 45 seconds
    // plus its intermission/countdown. Five minutes can legitimately end 5:6.
    // This fixture must finish within that full contest budget; repeated draws
    // or navigation stalls must still fail, rather than extending it forever.
    const int completionTicks = (2 * rules.roundLimit - 1) * fullRoundTicks;
    bool finitePositions = true;
    int elapsedTicks = 0;
    for (int i = 0; i < completionTicks; ++i) {
      tick(game, transport);
      const auto& snap = game.snapshot();
      elapsedTicks = i + 1;
      for (std::size_t slot = 0; slot < 2; ++slot) {
        const auto& position = snap.players[slot].position;
        finitePositions &= std::isfinite(position.x) && std::isfinite(position.y) &&
          std::isfinite(position.z);
      }
      if (snap.matchPhase == lg::MatchPhase::MatchEnd && previousPhase != snap.matchPhase) ++matchEnds;
      if ((snap.matchPhase == lg::MatchPhase::RoundEnd ||
           snap.matchPhase == lg::MatchPhase::MatchEnd) && previousPhase == lg::MatchPhase::Live) {
        ++roundEnds;
        draws += snap.scores == previousScores ? 1 : 0;
        std::cout << "Arena round " << roundEnds << " at " << (i + 1) / 125.0F
          << "s score=" << snap.scores[0] << ':' << snap.scores[1]
          << " health=" << snap.players[0].health << ':' << snap.players[1].health << '\n';
      }
      if ((i + 1) % (125 * 60) == 0) {
        const auto& stats = game.botRuntimeStats();
        std::cout << "Arena progress " << (i + 1) / 125 << "s damage_events="
          << stats.acceptedDamageEvents[0][1] - initialStats.acceptedDamageEvents[0][1] << ':'
          << stats.acceptedDamageEvents[1][0] - initialStats.acceptedDamageEvents[1][0]
          << " recoveries=" << stats.recoveryEvents[0] - initialStats.recoveryEvents[0] << ':'
          << stats.recoveryEvents[1] - initialStats.recoveryEvents[1] << '\n';
        for (std::size_t slot = 0; slot < 2; ++slot) {
          std::cout << "  position=(" << snap.players[slot].position.x << ','
            << snap.players[slot].position.y << ',' << snap.players[slot].position.z
            << ") " << game.botDebugString(slot) << '\n';
        }
      }
      previousPhase = snap.matchPhase;
      previousScores = snap.scores;
      combatTicks += snap.players[0].health < 100 || snap.players[1].health < 100 ? 1 : 0;
      arenaSlideTicks += lg::isGlideSlideActive(snap.players[0], snap.movementTuning) ||
        lg::isGlideSlideActive(snap.players[1], snap.movementTuning) ? 1 : 0;
      if (elapsedTicks == progressTicks) {
        const auto& stats = game.botRuntimeStats();
        check(roundEnds - draws >= progressTicks / fullRoundTicks &&
          stats.acceptedDamageEvents[0][1] > initialStats.acceptedDamageEvents[0][1] &&
          stats.acceptedDamageEvents[1][0] > initialStats.acceptedDamageEvents[1][0] &&
          arenaSlideTicks > 0,
          "medium arena bots sustain decisive rounds, damage in both directions and slides over five minutes");
      }
      if (elapsedTicks >= progressTicks && matchEnds > 0) break;
    }
    check(matchEnds >= 1, "medium arena bots finish a match within a full first-to-seven contest budget");
    check(finitePositions, "arena bot positions remain finite throughout the soak");
    check(game.botHiddenAttackInvariantCount() == 0, "arena bot combat respects current visibility");
    std::cout << "Arena soak: elapsed_seconds=" << elapsedTicks / 125.0F
      << " completion_budget_seconds=" << completionTicks / 125.0F
      << " matches=" << matchEnds << " combat_ticks=" << combatTicks
      << " rounds=" << roundEnds << " draws=" << draws << " slide_ticks=" << arenaSlideTicks << '\n';
  }
  return failures == 0 ? 0 : 1;
}
