#pragma once

#include "replay/ReplayTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace lg {
class ServerGame;
}

namespace lg::replay {

struct ReplayDivergence {
  bool diverged = false;
  std::uint32_t tick = 0;
  std::uint64_t expectedHash = 0;
  std::uint64_t actualHash = 0;
  // Hashes cover only authoritative gameplay state. Bot planning is excluded.
  std::string category;
};

// This runner drives the real ServerGame fixed tick. It never creates a fake
// UDP client: ServerGame suppresses transport reads and bot generation while a
// recorded resolved-input frame is injected at the normal simulation seam.
class ReplayPlaybackRunner {
public:
  explicit ReplayPlaybackRunner(ServerGame& game, const ReplayDemo& demo);

  [[nodiscard]] bool initialize(std::string* error = nullptr);
  [[nodiscard]] bool step(std::string* error = nullptr);
  [[nodiscard]] bool seek(std::uint32_t tick, std::string* error = nullptr);
  [[nodiscard]] bool finished() const;
  [[nodiscard]] std::uint32_t currentTick() const;
  [[nodiscard]] const ReplayDivergence& divergence() const;
  void stop();

private:
  [[nodiscard]] bool compareHash(std::string* error);
  [[nodiscard]] std::size_t tickOffsetFor(std::uint32_t tick) const;

  ServerGame& game_;
  const ReplayDemo& demo_;
  std::size_t nextInput_ = 0;
  std::size_t nextHash_ = 0;
  bool initialized_ = false;
  bool finished_ = false;
  ReplayDivergence divergence_ = {};
};

} // namespace lg::replay
