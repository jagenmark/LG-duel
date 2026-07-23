#include "scenario/ScenarioRuntime.hpp"

#include "net/NetTransport.hpp"
#include "server/ServerGame.hpp"
#include "shared/Constants.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <deque>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace lg::scenario {
namespace {

constexpr float kPi = 3.14159265358979323846F;

class CommandQueueTransport final : public NetTransport {
public:
  void sendCommand(const CommandPacket& packet) override {
    if (commands_.size() >= kDuelPlayerCount) {
      overflowed_ = true;
      return;
    }
    commands_.push_back(packet);
  }

  bool receiveCommand(CommandPacket& packet) override {
    if (commands_.empty()) return false;
    packet = std::move(commands_.front());
    commands_.pop_front();
    return true;
  }

  // Headless runs inspect ServerGame directly and never retain snapshots.
  void sendSnapshot(const ServerSnapshot&) override {}
  bool receiveSnapshot(ServerSnapshot&) override { return false; }
  [[nodiscard]] bool overflowed() const { return overflowed_; }

private:
  std::deque<CommandPacket> commands_;
  bool overflowed_ = false;
};

class StableHash {
public:
  void byte(std::uint8_t value) {
    value_ ^= value;
    value_ *= 1099511628211ULL;
  }

  template<typename T>
  void scalar(T value) {
    if constexpr (std::is_enum_v<T>) {
      scalar(std::underlying_type_t<T>(value));
    } else if constexpr (std::is_same_v<T, bool>) {
      byte(value ? 1U : 0U);
    } else if constexpr (std::is_integral_v<T>) {
      using U = std::make_unsigned_t<T>;
      const U bits = static_cast<U>(value);
      for (std::size_t index = 0; index < sizeof(U); ++index)
        byte(static_cast<std::uint8_t>(bits >> (index * 8U)));
    } else if constexpr (std::is_same_v<T, float>) {
      if (value == 0.0F) value = 0.0F;
      scalar(std::bit_cast<std::uint32_t>(value));
    } else if constexpr (std::is_same_v<T, double>) {
      if (value == 0.0) value = 0.0;
      scalar(std::bit_cast<std::uint64_t>(value));
    }
  }

  void text(std::string_view value) {
    scalar(value.size());
    for (const unsigned char character : value) byte(character);
  }

  [[nodiscard]] std::string hex() const {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << value_;
    return output.str();
  }

private:
  std::uint64_t value_ = 14695981039346656037ULL;
};

void hashVec(StableHash& hash, Vec3 value) {
  hash.scalar(value.x); hash.scalar(value.y); hash.scalar(value.z);
}

void hashPlayer(StableHash& hash, const PlayerState& player) {
  hashVec(hash, player.position); hashVec(hash, player.velocity);
  hash.scalar(player.viewYawRadians); hash.scalar(player.viewPitchRadians);
  hash.scalar(player.health); hash.scalar(player.freezeLevel);
  hash.scalar(player.bounds.radius); hash.scalar(player.bounds.halfHeight);
  hash.scalar(player.movementMode);
  hash.scalar(player.knockbackTicksRemaining);
  hash.scalar(player.dashCooldownTicksRemaining);
  hash.scalar(player.dashActiveTicksRemaining);
  hashVec(hash, player.dashDirection);
  hash.scalar(player.jumpPadCooldownTicksRemaining);
  hash.scalar(player.onGround); hash.scalar(player.jumpHeld);
  hash.scalar(player.dashHeld); hash.scalar(player.crouched);
  hash.scalar(player.sneaking);
}

void hashCommand(StableHash& hash, const UserCommand& command) {
  hash.scalar(command.sequence); hash.scalar(command.clientTick);
  hash.scalar(command.viewYawRadians); hash.scalar(command.viewPitchRadians);
  hash.scalar(command.forwardMove); hash.scalar(command.rightMove);
  hash.scalar(command.upMove); hash.scalar(command.attack);
  hash.scalar(command.jump); hash.scalar(command.dash);
  hash.scalar(command.crouch); hash.scalar(command.sneak);
  hash.scalar(command.zoomed); hash.scalar(command.planarAim);
  hash.scalar(command.weapon);
}

void hashActionEdges(StableHash& hash, const ActionEdgeState& edges) {
  hash.scalar(edges.jump); hash.scalar(edges.dash);
  hash.scalar(edges.reset); hash.scalar(edges.ready);
  hash.scalar(edges.mcguffinThrow);
  hash.scalar(edges.mcguffinThrowYawRadians);
  hash.scalar(edges.mcguffinThrowPitchRadians);
  hash.scalar(edges.attack);
  hash.scalar(edges.attackYawRadians);
  hash.scalar(edges.attackPitchRadians);
  hash.scalar(edges.attackViewedServerTick);
  hash.scalar(edges.attackWeapon);
  hash.scalar(edges.attackZoomed);
}

void hashStats(StableHash& hash, const RoundCombatStats& stats) {
  for (const WeaponCombatStats& weapon : stats.weapons) {
    hash.scalar(weapon.damageDealt);
    hash.scalar(weapon.attempts);
    hash.scalar(weapon.hits);
  }
}

void hashScenarioState(StableHash& hash, const ScenarioState& state) {
  hash.scalar(state.serverTick); hash.scalar(state.mapRevision);
  hash.text(state.mapName); hash.scalar(state.mapContentHash);
  for (const ScenarioPlayerState& player : state.players) {
    hash.scalar(player.slot); hash.scalar(player.connected);
    hash.scalar(player.bot); hash.scalar(player.participating);
    hash.scalar(player.ready); hash.scalar(player.team);
    hash.scalar(player.alive); hashPlayer(hash, player.player);
    const ScenarioWeaponState& weapon = player.weapon;
    hash.scalar(weapon.selectedWeapon);
    for (const int ammo : weapon.ammo) hash.scalar(ammo);
    hash.scalar(weapon.lightningGun.fractionalDamage);
    hash.scalar(weapon.lightningGun.shotCredit);
    hash.scalar(weapon.freezeGun.fractionalDamage);
    hash.scalar(weapon.freezeGun.shotCredit);
    hash.scalar(weapon.lightningAmmoCredit);
    hash.scalar(weapon.freezeAmmoCredit);
    hash.scalar(weapon.fractionalVampirismHealing);
    hash.scalar(weapon.railgunCooldownTicks);
    hash.scalar(weapon.revolverCooldownTicks);
    hash.scalar(weapon.sniperAdsFraction);
    hash.scalar(weapon.sniperChargeFraction);
    hash.scalar(weapon.machineGunCooldownTicks);
    hash.scalar(weapon.shotgunCooldownTicks);
    hash.scalar(weapon.rocketCooldownTicks);
    hash.scalar(weapon.grenadeCooldownTicks);
    hash.scalar(weapon.plasmaGunCooldownTicks);
    hash.scalar(weapon.weaponPulloutTicks);
    hash.scalar(player.respawnTicksRemaining);
    hashCommand(hash, player.command);
    hashActionEdges(hash, player.consumedActionEdges);
    hash.scalar(player.viewedServerTick); hash.scalar(player.hasCommand);
    hash.scalar(player.botState.dodgeDirection);
    hash.scalar(player.botState.dodgeSwitchSeconds);
    hash.scalar(player.botState.reactionSecondsRemaining);
    hash.scalar(player.botState.targetPlayerIndex);
    hash.scalar(player.botState.desiredYawRadians);
    hash.scalar(player.botState.desiredPitchRadians);
    hash.scalar(player.botState.aimErrorYawRadians);
    hash.scalar(player.botState.aimErrorPitchRadians);
    hash.scalar(player.botState.nextAimErrorRefreshSeconds);
    hash.scalar(player.botState.initialized);
  }
  for (const ScenarioProjectileState& projectile : state.projectiles) {
    hash.scalar(projectile.slot); hash.scalar(projectile.active);
    hash.scalar(projectile.owner); hash.scalar(projectile.weapon);
    hashVec(hash, projectile.position);
    hashVec(hash, projectile.previousPosition);
    hashVec(hash, projectile.velocity);
    hash.scalar(projectile.projectileRadius);
    hash.scalar(projectile.projectileHitboxRadius);
    hash.scalar(projectile.ownerCollisionArmed);
    hash.scalar(projectile.resting); hash.scalar(projectile.ageTicks);
  }
  const ScenarioMatchState& match = state.match;
  hash.scalar(match.gameMode); hash.scalar(match.phase);
  hash.scalar(match.phaseTicksRemaining); hash.scalar(match.liveTicksElapsed);
  for (const auto score : match.scores) hash.scalar(score);
  for (const auto score : match.teamScores) hash.scalar(score);
  for (const auto score : match.mcguffinScores) hash.scalar(score);
  for (const auto score : match.mcguffinRoundsWon) hash.scalar(score);
  hash.scalar(match.mcguffinRound); hash.scalar(match.roundWinner);
  hash.scalar(match.matchWinner); hash.scalar(match.roundWinningTeam);
  hash.scalar(match.matchWinningTeam);
  for (const auto& stats : match.roundCombatStats) hashStats(hash, stats);
  for (const auto& stats : match.matchCombatStats) hashStats(hash, stats);
  for (const bool available : state.healthPickupAvailable) hash.scalar(available);
  for (const auto ticks : state.healthPickupCooldownTicks) hash.scalar(ticks);
  for (const IcePool& pool : state.icePools) {
    hash.scalar(pool.active); hashVec(hash, pool.center);
    hashVec(hash, pool.normal); hash.scalar(pool.radius);
    hash.scalar(pool.lifetimeSeconds);
  }
  hash.scalar(state.mcguffin.state);
  hash.scalar(state.mcguffin.associatedTeam);
  hash.scalar(state.mcguffin.carrierTeam);
  hash.scalar(state.mcguffin.carrierIndex);
  hashVec(hash, state.mcguffin.position);
  hashVec(hash, state.mcguffin.velocity);
  hashVec(hash, state.mcguffin.spawnPosition);
  hash.scalar(state.mcguffin.stateTicks);
  hash.scalar(state.mcguffin.scoreSubPoints);
  hash.scalar(state.mcguffinRedBaseOwner);
  hash.scalar(state.mcguffinBlueBaseOwner);
  for (const auto ticks : state.mcguffinStealTicks) hash.scalar(ticks);
  hash.scalar(state.mcguffinCarrySubPoints);
  hash.scalar(state.mcguffinCarriedPoints);
  hash.scalar(state.mcguffinFinalHoldTicks);
  hash.scalar(state.mcguffinRoundLiveTicks);
  hash.scalar(state.mcguffinThrowPickupLockoutTicks);
  hash.scalar(state.botRandomState); hash.scalar(state.spawnRandomState);
  for (const auto sequence : state.rocketExplosionSequences)
    hash.scalar(sequence);
  for (const auto sequence : state.fragEventSequences)
    hash.scalar(sequence);
  for (const auto sequence : state.localHitFeedbackSequences)
    hash.scalar(sequence);
  for (const auto sequence : state.footstepSequences)
    hash.scalar(sequence);
  for (const auto sequence : state.grenadeBounceSequences)
    hash.scalar(sequence);
  for (const auto ticks : state.spawnLastUsedTicks) hash.scalar(ticks);
  for (const bool used : state.spawnWasUsed) hash.scalar(used);
  hash.scalar(state.nextDeathmatchSpawnIndex);
  hash.scalar(state.playersColliding);
  hash.scalar(state.history.size());
  for (const ScenarioHistoryState& frame : state.history) {
    hash.scalar(frame.serverTick);
    for (const PlayerState& player : frame.players) hashPlayer(hash, player);
  }
}

dev::JsonValue vectorJson(Vec3 value) {
  return dev::JsonValue::arrayValue({
    dev::JsonValue::numberValue(value.x),
    dev::JsonValue::numberValue(value.y),
    dev::JsonValue::numberValue(value.z),
  });
}

dev::JsonValue projectileJson(const ScenarioProjectileState& projectile) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["slot"] = dev::JsonValue::numberValue(projectile.slot);
  value.object["active"] = dev::JsonValue::booleanValue(projectile.active);
  value.object["owner"] = dev::JsonValue::numberValue(projectile.owner);
  value.object["weapon"] =
    dev::JsonValue::stringValue(std::string(weaponName(projectile.weapon)));
  value.object["position"] = vectorJson(projectile.position);
  value.object["velocity"] = vectorJson(projectile.velocity);
  value.object["age_ticks"] = dev::JsonValue::numberValue(projectile.ageTicks);
  return value;
}

dev::JsonValue matchJson(const ScenarioMatchState& match) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["game_mode"] =
    dev::JsonValue::stringValue(std::string(gameModeName(match.gameMode)));
  value.object["phase"] =
    dev::JsonValue::numberValue(static_cast<std::uint8_t>(match.phase));
  value.object["phase_ticks_remaining"] =
    dev::JsonValue::numberValue(match.phaseTicksRemaining);
  value.object["live_ticks_elapsed"] =
    dev::JsonValue::numberValue(match.liveTicksElapsed);
  dev::JsonValue scores = dev::JsonValue::arrayValue();
  for (const auto score : match.scores)
    scores.array.push_back(dev::JsonValue::numberValue(score));
  value.object["scores"] = std::move(scores);
  dev::JsonValue teamScores = dev::JsonValue::arrayValue();
  for (const auto score : match.teamScores)
    teamScores.array.push_back(dev::JsonValue::numberValue(score));
  value.object["team_scores"] = std::move(teamScores);
  return value;
}

ScenarioSetup makeSetup(const ScenarioDefinition& scenario) {
  ScenarioSetup setup;
  setup.seed = scenario.world.seed;
  setup.match.gameMode = scenario.world.gameMode;
  setup.match.phase = MatchPhase::Live;
  for (const PlayerInitialState& source : scenario.players) {
    ScenarioPlayerSetup& target = setup.players[source.index];
    target.connected = source.connected && !source.bot;
    target.bot = source.bot;
    target.ready = true;
    target.team = source.team;
    target.position = source.position;
    target.velocity = source.velocity;
    target.viewYawRadians = source.viewYawDegrees * kPi / 180.0F;
    target.viewPitchRadians = source.viewPitchDegrees * kPi / 180.0F;
    target.health = source.health;
    target.alive = source.alive;
    target.onGround = false;
    target.selectedWeapon = source.selectedWeapon;
    target.ammo = source.ammo;
  }
  return setup;
}

std::uint32_t cooldownFor(const ScenarioPlayerState& player, Weapon weapon) {
  switch (weapon) {
  case Weapon::Railgun: return player.weapon.railgunCooldownTicks;
  case Weapon::Revolver: return player.weapon.revolverCooldownTicks;
  case Weapon::MachineGun: return player.weapon.machineGunCooldownTicks;
  case Weapon::Shotgun: return player.weapon.shotgunCooldownTicks;
  case Weapon::RocketLauncher: return player.weapon.rocketCooldownTicks;
  case Weapon::GrenadeLauncher: return player.weapon.grenadeCooldownTicks;
  case Weapon::PlasmaGun: return player.weapon.plasmaGunCooldownTicks;
  default: return 0;
  }
}

bool sameEvent(const EventEvidence& left, const EventEvidence& right) {
  return left.type == right.type && left.actor == right.actor &&
    left.target == right.target && left.entityId == right.entityId &&
    left.weapon == right.weapon && left.damage == right.damage;
}

bool appendEvent(
  std::vector<EventEvidence>& output,
  EventEvidence event,
  std::uint64_t& sequence,
  std::string& error
) {
  if (output.size() >= kMaxScenarioJournalEntries) {
    error = "event journal exceeded 100000 entries";
    return false;
  }
  event.sequence = ++sequence;
  output.push_back(std::move(event));
  return true;
}

bool deriveEvents(
  std::uint32_t run,
  std::uint32_t tick,
  const ScenarioState& before,
  const ScenarioState& after,
  const ServerSnapshot& snapshot,
  std::vector<EventEvidence>& events,
  std::vector<EventEvidence>& priorTickEvents,
  std::uint64_t& sequence,
  std::string& error
) {
  std::vector<EventEvidence> current;
  const auto add = [&](EventEvidence event) {
    event.run = run; event.tick = tick;
    current.push_back(std::move(event));
  };

  for (std::size_t actor = 0; actor < kDuelPlayerCount; ++actor) {
    const WeaponFireResult& fire = snapshot.weaponFires[actor];
    if (fire.fired) {
      EventEvidence event;
      event.type = "weapon_fired"; event.actor = actor;
      event.weapon = fire.weapon; event.position = fire.start;
      event.details.object["source_sequence"] =
        dev::JsonValue::numberValue(fire.visualSeed);
      add(std::move(event));
    }
  }
  for (std::size_t slot = 0; slot < after.projectiles.size(); ++slot) {
    const auto& oldProjectile = before.projectiles[slot];
    const auto& projectile = after.projectiles[slot];
    if (!oldProjectile.active && projectile.active) {
      EventEvidence event;
      event.type = "projectile_spawned"; event.actor = projectile.owner;
      event.entityId = slot; event.weapon = projectile.weapon;
      event.position = projectile.position; add(std::move(event));
    } else if (oldProjectile.active && !projectile.active) {
      EventEvidence event;
      event.type = "projectile_impacted"; event.actor = oldProjectile.owner;
      event.entityId = slot; event.weapon = oldProjectile.weapon;
      event.position = oldProjectile.position; add(std::move(event));
    }
  }
  for (std::size_t actor = 0; actor < kDuelPlayerCount; ++actor) {
    const RocketExplosionResult& explosion = snapshot.rocketExplosions[actor];
    if (explosion.active) {
      EventEvidence event;
      event.type = "explosion_created"; event.actor = actor;
      event.weapon = explosion.weapon; event.position = explosion.position;
      event.details.object["radius"] =
        dev::JsonValue::numberValue(explosion.radius);
      event.details.object["source_sequence"] =
        dev::JsonValue::numberValue(explosion.sequence);
      add(std::move(event));
    }
    for (const LocalHitFeedbackEvent& hit :
         snapshot.localHitFeedbackEvents[actor]) {
      if (!hit.active) continue;
      EventEvidence event;
      event.type = "damage_applied"; event.actor = actor;
      if (hit.targetPlayerIndex < kDuelPlayerCount)
        event.target = hit.targetPlayerIndex;
      event.weapon = hit.weapon; event.damage = hit.damageApplied;
      event.details.object["headshot"] =
        dev::JsonValue::booleanValue(hit.headshot);
      event.details.object["source_sequence"] =
        dev::JsonValue::numberValue(hit.sequence);
      add(std::move(event));
    }
    const FragEvent& frag = snapshot.fragEvents[actor];
    if (frag.active) {
      EventEvidence event;
      event.type = "player_killed"; event.actor = actor;
      if (frag.targetPlayerIndex < kDuelPlayerCount)
        event.target = frag.targetPlayerIndex;
      event.weapon = frag.weapon;
      event.details.object["source_sequence"] =
        dev::JsonValue::numberValue(frag.sequence);
      add(std::move(event));
    }
  }
  for (std::size_t player = 0; player < kDuelPlayerCount; ++player) {
    if (!before.players[player].alive && after.players[player].alive) {
      EventEvidence event; event.type = "player_respawned";
      event.actor = player; event.position = after.players[player].player.position;
      add(std::move(event));
    }
    if (before.match.scores[player] != after.match.scores[player]) {
      EventEvidence event; event.type = "score_changed"; event.actor = player;
      event.details.object["before"] =
        dev::JsonValue::numberValue(before.match.scores[player]);
      event.details.object["after"] =
        dev::JsonValue::numberValue(after.match.scores[player]);
      add(std::move(event));
    }
  }
  if (before.match.phase != after.match.phase) {
    EventEvidence event; event.type = "round_state_changed";
    event.details.object["before"] = dev::JsonValue::numberValue(
      static_cast<std::uint8_t>(before.match.phase));
    event.details.object["after"] = dev::JsonValue::numberValue(
      static_cast<std::uint8_t>(after.match.phase));
    add(std::move(event));
  }

  // Snapshots repeat short-lived events. Drop only an identical event from the
  // prior tick; stable source sequence fields still let a new hit or frag pass.
  for (EventEvidence& event : current) {
    const bool repeated = std::any_of(
      priorTickEvents.begin(), priorTickEvents.end(),
      [&](const EventEvidence& prior) {
        if (!sameEvent(event, prior)) return false;
        const auto* newSequence = event.details.find("source_sequence");
        const auto* oldSequence = prior.details.find("source_sequence");
        if (newSequence != nullptr || oldSequence != nullptr) {
          return newSequence != nullptr && oldSequence != nullptr &&
            newSequence->number == oldSequence->number;
        }
        if (event.type == "weapon_fired" && event.actor && event.weapon) {
          return cooldownFor(after.players[*event.actor], *event.weapon) <=
            cooldownFor(before.players[*event.actor], *event.weapon);
        }
        return true;
      });
    if (!repeated &&
        !appendEvent(events, event, sequence, error)) return false;
  }
  priorTickEvents = std::move(current);
  return true;
}

void hashEvent(StableHash& hash, const EventEvidence& event) {
  hash.scalar(event.tick); hash.scalar(event.sequence); hash.text(event.type);
  hash.scalar(event.actor.has_value());
  if (event.actor) hash.scalar(*event.actor);
  hash.scalar(event.target.has_value());
  if (event.target) hash.scalar(*event.target);
  hash.scalar(event.entityId.has_value());
  if (event.entityId) hash.scalar(*event.entityId);
  hash.scalar(event.weapon.has_value());
  if (event.weapon) hash.scalar(*event.weapon);
  hash.scalar(event.damage.has_value());
  if (event.damage) hash.scalar(*event.damage);
  hash.scalar(event.position.has_value());
  if (event.position) hashVec(hash, *event.position);
  hash.text(dev::writeJson(event.details));
}

bool eventMatches(const EventEvidence& event, const EventAssertion& expected) {
  return event.type == expected.type &&
    (!expected.actor || event.actor == expected.actor) &&
    (!expected.target || event.target == expected.target) &&
    (!expected.weapon || event.weapon == expected.weapon) &&
    (!expected.damage || event.damage == expected.damage);
}

dev::JsonValue eventContextJson(const EventEvidence& event) {
  dev::JsonValue value = dev::JsonValue::objectValue();
  value.object["type"] = dev::JsonValue::stringValue(event.type);
  value.object["tick"] = dev::JsonValue::numberValue(event.tick);
  if (event.actor)
    value.object["actor"] = dev::JsonValue::numberValue(*event.actor);
  if (event.target)
    value.object["target"] = dev::JsonValue::numberValue(*event.target);
  if (event.entityId)
    value.object["entity_id"] = dev::JsonValue::numberValue(*event.entityId);
  if (event.weapon) {
    value.object["weapon"] =
      dev::JsonValue::stringValue(std::string(weaponName(*event.weapon)));
  }
  if (event.damage)
    value.object["damage"] = dev::JsonValue::numberValue(*event.damage);
  if (event.position)
    value.object["position"] = vectorJson(*event.position);
  return value;
}

AssertionEvidence evaluateAssertion(
  std::size_t index,
  std::uint32_t run,
  std::uint32_t tick,
  const ScenarioAssertion& assertion,
  const ScenarioState& state,
  std::string_view hash,
  const std::vector<EventEvidence>& events
) {
  AssertionEvidence result;
  result.assertionIndex = index; result.run = run; result.tick = tick;
  result.type = std::string(assertionTypeName(assertion.type));
  result.expected = dev::JsonValue::objectValue();
  result.actual = dev::JsonValue::objectValue();
  const auto finish = [&](bool passed, std::string message) {
    result.passed = passed; result.message = std::move(message);
  };
  switch (assertion.type) {
  case AssertionType::PlayerPosition:
  case AssertionType::PlayerVelocity: {
    const auto& expected = std::get<PlayerVectorAssertion>(assertion.payload);
    const Vec3 actual = assertion.type == AssertionType::PlayerPosition
      ? state.players[expected.player].player.position
      : state.players[expected.player].player.velocity;
    const float distance = length(actual - expected.value);
    result.expected.object["value"] = vectorJson(expected.value);
    result.expected.object["tolerance"] =
      dev::JsonValue::numberValue(expected.tolerance);
    result.actual.object["value"] = vectorJson(actual);
    result.actual.object["distance"] = dev::JsonValue::numberValue(distance);
    finish(distance <= expected.tolerance,
      "player[" + std::to_string(expected.player) + "] vector distance " +
      std::to_string(distance) + ", tolerance " +
      std::to_string(expected.tolerance));
    break;
  }
  case AssertionType::PlayerHealth: {
    const auto& expected = std::get<PlayerHealthAssertion>(assertion.payload);
    const int actual = state.players[expected.player].player.health;
    result.expected.object["health"] = dev::JsonValue::numberValue(expected.health);
    result.actual.object["health"] = dev::JsonValue::numberValue(actual);
    finish(actual == expected.health,
      "player[" + std::to_string(expected.player) + "].health expected " +
      std::to_string(expected.health) + ", actual " + std::to_string(actual));
    break;
  }
  case AssertionType::PlayerAlive: {
    const auto& expected = std::get<PlayerAliveAssertion>(assertion.payload);
    const bool actual = state.players[expected.player].alive;
    result.expected.object["alive"] = dev::JsonValue::booleanValue(expected.alive);
    result.actual.object["alive"] = dev::JsonValue::booleanValue(actual);
    finish(actual == expected.alive,
      "player[" + std::to_string(expected.player) + "].alive expected " +
      (expected.alive ? "true" : "false") + ", actual " +
      (actual ? "true" : "false"));
    break;
  }
  case AssertionType::PlayerWeapon: {
    const auto& expected = std::get<PlayerWeaponAssertion>(assertion.payload);
    const Weapon actual = state.players[expected.player].weapon.selectedWeapon;
    result.expected.object["weapon"] =
      dev::JsonValue::stringValue(std::string(weaponName(expected.weapon)));
    result.actual.object["weapon"] =
      dev::JsonValue::stringValue(std::string(weaponName(actual)));
    finish(actual == expected.weapon,
      "player[" + std::to_string(expected.player) + "].weapon expected " +
      std::string(weaponName(expected.weapon)) + ", actual " +
      std::string(weaponName(actual)));
    break;
  }
  case AssertionType::ProjectileExists:
  case AssertionType::ProjectileRemoved: {
    const auto& expected = std::get<ProjectileAssertion>(assertion.payload);
    const auto matches = [&](const ScenarioProjectileState& projectile) {
      return projectile.active &&
        (!expected.owner || projectile.owner == *expected.owner) &&
        (!expected.weapon || projectile.weapon == *expected.weapon);
    };
    const std::size_t count = static_cast<std::size_t>(std::count_if(
      state.projectiles.begin(), state.projectiles.end(), matches));
    const bool wantsExists = assertion.type == AssertionType::ProjectileExists;
    result.expected.object["exists"] = dev::JsonValue::booleanValue(wantsExists);
    result.actual.object["count"] = dev::JsonValue::numberValue(count);
    finish(wantsExists ? count > 0U : count == 0U,
      "matching active projectile count is " + std::to_string(count));
    break;
  }
  case AssertionType::Event: {
    const auto& expected = std::get<EventAssertion>(assertion.payload);
    const std::uint32_t count = static_cast<std::uint32_t>(std::count_if(
      events.begin(), events.end(),
      [&](const EventEvidence& event) { return eventMatches(event, expected); }));
    const std::uint32_t wanted = expected.count.value_or(1U);
    result.expected.object["type"] = dev::JsonValue::stringValue(expected.type);
    result.expected.object["count"] = dev::JsonValue::numberValue(wanted);
    if (expected.actor)
      result.expected.object["actor"] =
        dev::JsonValue::numberValue(*expected.actor);
    if (expected.target)
      result.expected.object["target"] =
        dev::JsonValue::numberValue(*expected.target);
    if (expected.weapon) {
      result.expected.object["weapon"] =
        dev::JsonValue::stringValue(std::string(weaponName(*expected.weapon)));
    }
    if (expected.damage)
      result.expected.object["damage"] =
        dev::JsonValue::numberValue(*expected.damage);
    result.actual.object["count"] = dev::JsonValue::numberValue(count);
    const auto sameType = std::find_if(
      events.begin(), events.end(),
      [&](const EventEvidence& event) { return event.type == expected.type; });
    if (sameType != events.end()) {
      result.actual.object["nearest_event"] = eventContextJson(*sameType);
    } else if (!events.empty()) {
      result.actual.object["nearest_event"] = eventContextJson(events.front());
    }
    finish(count == wanted,
      "event '" + expected.type + "' expected count " +
      std::to_string(wanted) + ", actual " + std::to_string(count));
    break;
  }
  case AssertionType::StateHash: {
    const auto& expected = std::get<StateHashAssertion>(assertion.payload);
    result.expected.object["hash"] = dev::JsonValue::stringValue(expected.hash);
    result.actual.object["hash"] = dev::JsonValue::stringValue(std::string(hash));
    finish(hash == expected.hash,
      "state hash expected " + expected.hash + ", actual " + std::string(hash));
    break;
  }
  }
  return result;
}

FinalStateEvidence finalEvidence(
  std::uint32_t run,
  const ScenarioState& state
) {
  FinalStateEvidence result; result.run = run; result.tick = state.serverTick;
  for (const ScenarioPlayerState& player : state.players) {
    if (!player.connected && !player.bot) continue;
    FinalPlayerEvidence output;
    output.index = player.slot; output.connected = player.connected || player.bot;
    output.team = player.team; output.position = player.player.position;
    output.velocity = player.player.velocity;
    output.viewYawDegrees = player.player.viewYawRadians * 180.0F / kPi;
    output.viewPitchDegrees = player.player.viewPitchRadians * 180.0F / kPi;
    output.health = player.player.health; output.alive = player.alive;
    output.selectedWeapon = player.weapon.selectedWeapon;
    output.ammo = player.weapon.ammo;
    result.players.push_back(output);
  }
  for (const ScenarioProjectileState& projectile : state.projectiles) {
    if (!projectile.active) continue;
    result.projectiles.push_back({
      projectile.slot, projectile.owner, projectile.weapon,
      projectile.position, projectile.velocity,
    });
  }
  result.match = matchJson(state.match);
  return result;
}

std::string differenceMessage(
  const ScenarioState& expected,
  const ScenarioState& actual
) {
  for (std::size_t index = 0; index < expected.players.size(); ++index) {
    const auto& left = expected.players[index];
    const auto& right = actual.players[index];
    if (left.player.position.x != right.player.position.x)
      return "subsystem=players entity=player[" + std::to_string(index) +
        "] field=position.x";
    if (left.player.position.y != right.player.position.y)
      return "subsystem=players entity=player[" + std::to_string(index) +
        "] field=position.y";
    if (left.player.position.z != right.player.position.z)
      return "subsystem=players entity=player[" + std::to_string(index) +
        "] field=position.z";
    if (left.player.health != right.player.health)
      return "subsystem=players entity=player[" + std::to_string(index) +
        "] field=health";
    if (left.weapon.selectedWeapon != right.weapon.selectedWeapon)
      return "subsystem=players entity=player[" + std::to_string(index) +
        "] field=selected_weapon";
  }
  for (std::size_t index = 0; index < expected.projectiles.size(); ++index) {
    const auto& left = expected.projectiles[index];
    const auto& right = actual.projectiles[index];
    if (left.active != right.active)
      return "subsystem=projectiles entity=projectile[" +
        std::to_string(index) + "] field=active";
    if (left.active && (left.position.x != right.position.x ||
                        left.position.y != right.position.y ||
                        left.position.z != right.position.z))
      return "subsystem=projectiles entity=projectile[" +
        std::to_string(index) + "] field=position";
  }
  if (expected.match.scores != actual.match.scores)
    return "subsystem=match entity=scores field=value";
  if (expected.match.phase != actual.match.phase)
    return "subsystem=match entity=phase field=value";
  return "subsystem=authoritative_state field=hash";
}

} // namespace

std::string scenarioStateHash(const ScenarioState& state) {
  StableHash hash;
  hashScenarioState(hash, state);
  return hash.hex();
}

dev::JsonValue scenarioStateJson(const ScenarioState& state) {
  dev::JsonValue root = dev::JsonValue::objectValue();
  root.object["server_tick"] = dev::JsonValue::numberValue(state.serverTick);
  root.object["map_revision"] = dev::JsonValue::numberValue(state.mapRevision);
  root.object["map"] = dev::JsonValue::stringValue(state.mapName);
  root.object["map_content_hash"] =
    dev::JsonValue::numberValue(state.mapContentHash);
  root.object["hash"] = dev::JsonValue::stringValue(scenarioStateHash(state));
  dev::JsonValue players = dev::JsonValue::arrayValue();
  for (const ScenarioPlayerState& player : state.players) {
    dev::JsonValue value = dev::JsonValue::objectValue();
    value.object["slot"] = dev::JsonValue::numberValue(player.slot);
    value.object["connected"] =
      dev::JsonValue::booleanValue(player.connected || player.bot);
    value.object["alive"] = dev::JsonValue::booleanValue(player.alive);
    value.object["position"] = vectorJson(player.player.position);
    value.object["velocity"] = vectorJson(player.player.velocity);
    value.object["health"] = dev::JsonValue::numberValue(player.player.health);
    value.object["weapon"] = dev::JsonValue::stringValue(
      std::string(weaponName(player.weapon.selectedWeapon)));
    players.array.push_back(std::move(value));
  }
  root.object["players"] = std::move(players);
  dev::JsonValue projectiles = dev::JsonValue::arrayValue();
  for (const ScenarioProjectileState& projectile : state.projectiles)
    if (projectile.active) projectiles.array.push_back(projectileJson(projectile));
  root.object["projectiles"] = std::move(projectiles);
  root.object["match"] = matchJson(state.match);
  return root;
}

std::optional<std::uint32_t> firstDivergentTick(
  std::span<const std::string> reference,
  std::span<const std::string> candidate
) {
  const std::size_t count = std::min(reference.size(), candidate.size());
  for (std::size_t index = 0; index < count; ++index) {
    if (reference[index] != candidate[index]) {
      return static_cast<std::uint32_t>(index);
    }
  }
  if (reference.size() != candidate.size()) {
    return static_cast<std::uint32_t>(count);
  }
  return std::nullopt;
}

ScenarioRunResult runScenario(
  const ScenarioDefinition& scenario,
  const ScenarioRunOptions& options
) {
  ScenarioRunResult result;
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  result.evidence.runId = "run-" + std::to_string(stamp);
  result.evidence.expectedFailure = scenario.expectedFailure.has_value();
  const std::uint32_t repeat = options.repeat.value_or(scenario.execution.repeat);
  if (repeat == 0U) {
    result.error = "repeat must be at least 1";
    result.evidence.summary = result.error;
    return result;
  }

  std::vector<std::string> referenceHashes;
  std::vector<std::string> referenceEventHashes;
  ScenarioState referenceFinal;
  bool haveReferenceFinal = false;
  bool assertionFailureSeen = false;
  bool unexpectedAssertionFailure = false;
  std::string firstAssertionFailure;
  std::uint32_t expectedFailureRunCount = 0;
  bool infrastructureFailure = false;
  bool divergence = false;

  for (std::uint32_t run = 0; run < repeat; ++run) {
    RunEvidence runEvidence; runEvidence.run = run;
    CommandQueueTransport transport;
    ServerGame game(transport);
    game.setMapDirectory(options.mapsDirectory.string());
    const ScenarioSetup setup = makeSetup(scenario);
    std::string setupError;
    if (
      scenario.world.gameMode == GameMode::McGuffin &&
      !game.applyScenarioSetup(setup, &setupError)
    ) {
      runEvidence.failure = "scenario setup failed before map validation: " +
        setupError;
      infrastructureFailure = true;
      result.evidence.runs.push_back(std::move(runEvidence));
      break;
    }
    if (scenario.world.map == "default") {
      game.setArena(makeDefaultServerArena());
    } else if (!game.loadRequestedMap(scenario.world.map)) {
      runEvidence.failure = "could not load map '" + scenario.world.map +
        "' from '" + options.mapsDirectory.string() + "'";
      infrastructureFailure = true;
      result.evidence.runs.push_back(std::move(runEvidence));
      break;
    }
    if (
      scenario.world.gameMode == GameMode::McGuffin &&
      !hasValidMcGuffinLayout(game.arena())
    ) {
      runEvidence.failure =
        "McGuffin scenario map needs one neutral spawn, two bases, and team spawns";
      infrastructureFailure = true;
      result.evidence.runs.push_back(std::move(runEvidence));
      break;
    }
    if (!game.applyScenarioSetup(setup, &setupError)) {
      runEvidence.failure = "scenario setup failed: " + setupError;
      infrastructureFailure = true;
      result.evidence.runs.push_back(std::move(runEvidence));
      break;
    }

    ScenarioState state = game.captureScenarioState();
    std::vector<std::string> hashes;
    hashes.reserve(static_cast<std::size_t>(scenario.execution.maxTicks) + 1U);
    StableHash eventStreamHash;
    std::vector<EventEvidence> runEvents;
    std::vector<EventEvidence> priorTickEvents;
    std::size_t hashedEventCount = 0;
    std::uint64_t eventSequence = 0;
    bool runExpectedFailureSeen = false;
    bool runUnexpectedAssertionFailure = false;
    std::array<UserCommand, kDuelPlayerCount> commands = {};
    std::array<ActionEdgeState, kDuelPlayerCount> edges = {};
    for (const PlayerInitialState& player : scenario.players) {
      UserCommand& command = commands[player.index];
      command.weapon = player.selectedWeapon;
      command.viewYawRadians = player.viewYawDegrees * kPi / 180.0F;
      command.viewPitchRadians = player.viewPitchDegrees * kPi / 180.0F;
      command.planarAim = false;
    }

    const auto noteDivergence = [&](
      std::size_t tickIndex,
      std::string_view stateHash,
      std::string_view eventHash
    ) {
      if (
        run == 0U ||
        tickIndex >= referenceHashes.size() ||
        tickIndex >= referenceEventHashes.size()
      ) {
        return false;
      }
      const bool stateDiffers = referenceHashes[tickIndex] != stateHash;
      const bool eventsDiffer = referenceEventHashes[tickIndex] != eventHash;
      if (!stateDiffers && !eventsDiffer) return false;

      divergence = true;
      DivergenceEvidence evidence;
      evidence.run = run;
      evidence.referenceRun = 0U;
      evidence.tick = static_cast<std::uint32_t>(tickIndex);
      evidence.expectedHash = stateDiffers
        ? referenceHashes[tickIndex]
        : referenceEventHashes[tickIndex];
      evidence.actualHash = std::string(stateDiffers ? stateHash : eventHash);
      if (stateDiffers) {
        evidence.message =
          tickIndex + 1U == referenceHashes.size() && haveReferenceFinal
          ? differenceMessage(referenceFinal, state)
          : "subsystem=authoritative_state first differing tick";
      } else {
        evidence.message = "subsystem=events first differing tick";
      }
      evidence.expectedState = dev::JsonValue::objectValue();
      evidence.expectedState.object["tick"] =
        dev::JsonValue::numberValue(tickIndex);
      evidence.expectedState.object["hash"] =
        dev::JsonValue::stringValue(evidence.expectedHash);
      evidence.actualState = scenarioStateJson(state);
      result.evidence.divergence = std::move(evidence);
      runEvidence.failure = "run diverged at tick " +
        std::to_string(tickIndex);
      return true;
    };

    const auto checkAssertions = [&](std::uint32_t tick, bool completion) {
      const std::size_t eventBegin = completion ? 0U :
        static_cast<std::size_t>(std::lower_bound(
          runEvents.begin(), runEvents.end(), tick,
          [](const EventEvidence& event, std::uint32_t value) {
            return event.tick < value;
          }) - runEvents.begin());
      const std::size_t eventEnd = completion ? runEvents.size() :
        static_cast<std::size_t>(std::upper_bound(
          runEvents.begin(), runEvents.end(), tick,
          [](std::uint32_t value, const EventEvidence& event) {
            return value < event.tick;
          }) - runEvents.begin());
      std::vector<EventEvidence> scopedEvents(
        runEvents.begin() + eventBegin, runEvents.begin() + eventEnd);
      for (std::size_t index = 0; index < scenario.assertions.size(); ++index) {
        const ScenarioAssertion& assertion = scenario.assertions[index];
        if ((completion && !assertion.atCompletion) ||
            (!completion && assertion.atTick != tick)) continue;
        AssertionEvidence evidence = evaluateAssertion(
          index, run, tick, assertion, state, scenarioStateHash(state),
          scopedEvents);
        if (!evidence.passed) {
          assertionFailureSeen = true;
          if (firstAssertionFailure.empty()) {
            firstAssertionFailure =
              "scenario '" + scenario.name + "', tick " +
              std::to_string(tick) + ": " + evidence.message;
          }
          if (
            scenario.expectedFailure &&
            index == scenario.expectedFailure->assertionIndex
          ) {
            runExpectedFailureSeen = true;
          } else {
            unexpectedAssertionFailure = true;
            runUnexpectedAssertionFailure = true;
          }
          if (runEvidence.failure.empty()) runEvidence.failure = evidence.message;
        }
        result.evidence.assertions.push_back(std::move(evidence));
      }
    };

    hashes.push_back(scenarioStateHash(state));
    result.evidence.hashes.push_back({run, 0U, hashes.back(), eventStreamHash.hex()});
    if (!noteDivergence(0U, hashes.back(), eventStreamHash.hex())) {
      checkAssertions(0U, false);
    }

    for (
      std::uint32_t tick = 0;
      tick < scenario.execution.maxTicks && !divergence;
      ++tick
    ) {
      for (const PlayerInitialState& player : scenario.players) {
        if (player.bot || !player.connected) continue;
        UserCommand command = commands[player.index];
        command.forwardMove = 0.0F; command.rightMove = 0.0F;
        command.upMove = 0.0F; command.jump = false; command.crouch = false;
        command.dash = false; command.attack = false;
        for (const TimelineEntry& entry : scenario.timeline) {
          if (entry.player != player.index || tick < entry.atTick ||
              tick >= entry.atTick + entry.durationTicks) continue;
          command.forwardMove = entry.input.forward;
          command.rightMove = entry.input.right;
          command.jump = entry.input.jump;
          command.crouch = entry.input.crouch;
          command.dash = entry.input.dash;
          command.attack = entry.input.attack;
          if (entry.input.weapon) command.weapon = *entry.input.weapon;
          if (entry.input.yawDegrees)
            command.viewYawRadians = *entry.input.yawDegrees * kPi / 180.0F;
          if (entry.input.pitchDegrees)
            command.viewPitchRadians = *entry.input.pitchDegrees * kPi / 180.0F;
          if (entry.input.pitchDegrees)
            command.planarAim = false;
          if (tick == entry.atTick) {
            for (const OneTickEdge edge : entry.oneTickEdges) {
              switch (edge) {
              case OneTickEdge::Jump: ++edges[player.index].jump; break;
              case OneTickEdge::Dash: ++edges[player.index].dash; break;
              case OneTickEdge::Attack:
                ++edges[player.index].attack;
                edges[player.index].attackYawRadians = command.viewYawRadians;
                edges[player.index].attackPitchRadians = command.viewPitchRadians;
                edges[player.index].attackViewedServerTick = state.serverTick;
                edges[player.index].attackWeapon = command.weapon;
                break;
              case OneTickEdge::Crouch: command.crouch = true; break;
              }
            }
          }
        }
        command.sequence += 1U;
        command.clientTick = tick;
        commands[player.index] = command;
        CommandPacket packet;
        packet.playerIndex = static_cast<std::uint8_t>(player.index);
        packet.clientIndex = static_cast<std::uint8_t>(player.index);
        packet.command = command;
        packet.viewedServerTick = state.serverTick;
        packet.actionEdges = edges[player.index];
        transport.sendCommand(packet);
      }
      if (transport.overflowed()) {
        runEvidence.failure = "headless command queue exceeded player bound";
        infrastructureFailure = true;
        break;
      }
      const ScenarioState before = state;
      game.tick(kFixedTickSeconds);
      state = game.captureScenarioState();
      std::string eventError;
      if (!deriveEvents(
            run, tick + 1U, before, state, game.snapshot(), runEvents,
            priorTickEvents, eventSequence, eventError)) {
        runEvidence.failure = eventError;
        infrastructureFailure = true;
        break;
      }
      const std::string stateHash = scenarioStateHash(state);
      hashes.push_back(stateHash);
      while (hashedEventCount < runEvents.size())
        hashEvent(eventStreamHash, runEvents[hashedEventCount++]);
      result.evidence.hashes.push_back(
        {run, tick + 1U, stateHash, eventStreamHash.hex()});
      ++runEvidence.ticksExecuted;
      if (noteDivergence(tick + 1U, stateHash, eventStreamHash.hex())) {
        break;
      }
      checkAssertions(tick + 1U, false);
    }
    if (!infrastructureFailure && !divergence) {
      checkAssertions(state.serverTick, true);
    }
    for (const EventEvidence& event : runEvents) {
      if (result.evidence.events.size() >= kMaxScenarioJournalEntries) {
        runEvidence.failure = "combined event evidence exceeded 100000 entries";
        infrastructureFailure = true;
        break;
      }
      result.evidence.events.push_back(event);
    }
    runEvidence.finalStateHash = scenarioStateHash(state);
    runEvidence.eventStreamHash = eventStreamHash.hex();
    result.evidence.finalStates.push_back(finalEvidence(run, state));

    if (run == 0U) {
      referenceHashes = hashes;
      referenceEventHashes.reserve(result.evidence.hashes.size());
      for (const StateHashEvidence& evidence : result.evidence.hashes)
        if (evidence.run == 0U)
          referenceEventHashes.push_back(evidence.eventHash.value_or(""));
      referenceFinal = state; haveReferenceFinal = true;
    } else if (!infrastructureFailure && !divergence) {
      const std::size_t count = std::min(referenceHashes.size(), hashes.size());
      std::size_t firstDifference = count;
      for (std::size_t index = 0; index < count; ++index) {
        if (referenceHashes[index] != hashes[index] ||
            referenceEventHashes[index] !=
              result.evidence.hashes[
                result.evidence.hashes.size() - hashes.size() + index
              ].eventHash.value_or("")) {
          firstDifference = index;
          break;
        }
      }
      if (firstDifference != count || hashes.size() != referenceHashes.size()) {
        divergence = true;
        DivergenceEvidence evidence;
        evidence.run = run; evidence.referenceRun = 0U;
        evidence.tick = static_cast<std::uint32_t>(firstDifference);
        evidence.expectedHash = firstDifference < referenceHashes.size()
          ? referenceHashes[firstDifference] : "<missing>";
        evidence.actualHash = firstDifference < hashes.size()
          ? hashes[firstDifference] : "<missing>";
        evidence.message = haveReferenceFinal && firstDifference + 1U == hashes.size()
          ? differenceMessage(referenceFinal, state)
          : "first differing authoritative state or event hash";
        evidence.expectedState = haveReferenceFinal &&
          firstDifference + 1U == hashes.size()
          ? scenarioStateJson(referenceFinal) : dev::JsonValue::objectValue();
        evidence.actualState = scenarioStateJson(state);
        result.evidence.divergence = std::move(evidence);
        if (runEvidence.failure.empty())
          runEvidence.failure = "run diverged at tick " +
            std::to_string(firstDifference);
      }
    }
    runEvidence.expectedFailureObserved =
      scenario.expectedFailure.has_value() && runExpectedFailureSeen &&
      !runUnexpectedAssertionFailure && !divergence && !infrastructureFailure;
    if (runEvidence.expectedFailureObserved) {
      ++expectedFailureRunCount;
    }
    runEvidence.passed = runEvidence.failure.empty() ||
      runEvidence.expectedFailureObserved;
    result.evidence.runs.push_back(std::move(runEvidence));
    if (infrastructureFailure || divergence) break;
  }

  const bool expected = scenario.expectedFailure.has_value();
  const bool xfail = expected && expectedFailureRunCount == repeat &&
    !unexpectedAssertionFailure && !infrastructureFailure && !divergence;
  const bool xpass = expected && !assertionFailureSeen &&
    !infrastructureFailure && !divergence;
  result.passed = !infrastructureFailure && !divergence &&
    ((!expected && !assertionFailureSeen) || xfail);
  result.evidence.passed = result.passed;
  if (infrastructureFailure) result.evidence.summary = "ERROR";
  else if (divergence) result.evidence.summary = "DIVERGED";
  else if (xfail) result.evidence.summary = "XFAIL: " +
    scenario.expectedFailure->reason;
  else if (xpass) result.evidence.summary = "XPASS: expected failure was not observed";
  else if (assertionFailureSeen)
    result.evidence.summary = "FAIL: " + firstAssertionFailure;
  else result.evidence.summary = "PASS";
  if (!result.passed && result.error.empty())
    result.error = result.evidence.summary;
  return result;
}

} // namespace lg::scenario
