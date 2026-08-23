#pragma once

#include "replay/ReplayTypes.hpp"

#include <cstdint>
#include <optional>
#include <utility>

namespace lg::replay {

inline constexpr std::uint64_t kRemoteKillcamDeathBindingWaitMilliseconds =
  1500U;

struct RemoteKillcamIdentity {
  std::uint32_t sessionId = 0U;
  std::uint32_t generation = 0U;
  std::uint32_t lethalSequence = 0U;
  std::uint8_t victim = kNoReplayPlayer;

  [[nodiscard]] bool valid() const {
    return sessionId != 0U && generation != 0U && lethalSequence != 0U &&
      victim < kDuelPlayerCount;
  }

  friend bool operator==(
    const RemoteKillcamIdentity&,
    const RemoteKillcamIdentity&
  ) = default;
};

struct RemoteKillcamDecodedPlayback {
  ReplayDemo demo;
  std::uint8_t followSlot = kNoReplayPlayer;
};

// A completed remote decode may win the race against the authoritative death
// snapshot. Retain only the already-bounded, identity-validated candidate for
// a short interval; never weaken the death-state checks to make the race pass.
class PendingRemoteKillcamPlayback {
public:
  void begin(RemoteKillcamIdentity identity) {
    if (!identity.valid()) {
      reset();
      return;
    }
    if (!identity_.has_value() || *identity_ != identity) {
      reset();
      identity_ = identity;
    }
  }

  void reset() {
    identity_.reset();
    decoded_.reset();
    decodedAtMilliseconds_ = 0U;
  }

  [[nodiscard]] bool storeDecoded(
    RemoteKillcamIdentity identity,
    ReplayDemo demo,
    std::uint8_t followSlot,
    std::uint64_t nowMilliseconds
  ) {
    if (!identity.valid() || !identity_.has_value() ||
        *identity_ != identity || followSlot >= kDuelPlayerCount) {
      return false;
    }
    decoded_ = RemoteKillcamDecodedPlayback{
      std::move(demo),
      followSlot,
    };
    decodedAtMilliseconds_ = nowMilliseconds;
    return true;
  }

  [[nodiscard]] bool discardIfExpired(std::uint64_t nowMilliseconds) {
    if (!decoded_.has_value() || nowMilliseconds < decodedAtMilliseconds_ ||
        nowMilliseconds - decodedAtMilliseconds_ <
          kRemoteKillcamDeathBindingWaitMilliseconds) {
      return false;
    }
    reset();
    return true;
  }

  [[nodiscard]] std::optional<RemoteKillcamDecodedPlayback> takeReady(
    RemoteKillcamIdentity identity,
    bool deathSnapshotBound,
    std::uint64_t nowMilliseconds
  ) {
    if (discardIfExpired(nowMilliseconds) || !deathSnapshotBound ||
        !identity_.has_value() || *identity_ != identity ||
        !decoded_.has_value()) {
      return std::nullopt;
    }
    RemoteKillcamDecodedPlayback result = std::move(*decoded_);
    reset();
    return result;
  }

  [[nodiscard]] bool hasDecoded() const { return decoded_.has_value(); }

private:
  std::optional<RemoteKillcamIdentity> identity_;
  std::optional<RemoteKillcamDecodedPlayback> decoded_;
  std::uint64_t decodedAtMilliseconds_ = 0U;
};

} // namespace lg::replay
