#pragma once

#include "replay/ReplayTransfer.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace lg {
struct ServerSnapshot;
}

namespace lg::replay {

class ReplayRuntime;

enum class ClientReplayCommand {
  DemoPlay,
  DemoStop,
  DemoPause,
  DemoResume,
  DemoTogglePause,
  DemoStep,
  DemoSeek,
  DemoSpeed,
  DemoCamera,
  DemoFollow,
  DemoList,
  DemoDelete,
  KillcamSkip,
};

struct ClientKillcamLiveView {
  bool connected = false;
  bool spectator = true;
  std::uint32_t sessionId = 0U;
  std::size_t playerIndex = 0U;
  const ServerSnapshot* snapshot = nullptr;
};

struct ClientKillcamStatus {
  bool active = false;
  bool transferActive = false;
  bool decodePending = false;
  bool hasContext = false;
};

struct ClientKillcamHud {
  bool active = false;
  std::string killer;
  std::string weapon;
  std::string cause;
  float progress = 0.0F;
};

class ClientKillcamCoordinator {
public:
  explicit ClientKillcamCoordinator(std::filesystem::path mapDirectory);
  ~ClientKillcamCoordinator();

  ClientKillcamCoordinator(const ClientKillcamCoordinator&) = delete;
  ClientKillcamCoordinator& operator=(const ClientKillcamCoordinator&) = delete;

  void receiveTransfer(
    const ReplayTransferMessage& message,
    const ClientKillcamLiveView& live,
    std::uint64_t nowMilliseconds
  );
  void update(
    const ClientKillcamLiveView& live,
    std::uint64_t nowMilliseconds,
    double elapsedSeconds
  );
  [[nodiscard]] bool skip();

  [[nodiscard]] bool commandAllowed(ClientReplayCommand command) const;
  [[nodiscard]] ClientKillcamStatus status() const;
  [[nodiscard]] ClientKillcamHud hud() const;
  [[nodiscard]] ReplayRuntime* runtime();
  [[nodiscard]] const ReplayRuntime* runtime() const;
  [[nodiscard]] std::optional<ReplayTransferMessage> takeOutbound();
  [[nodiscard]] std::optional<std::string> takeMessage();
  [[nodiscard]] bool takePresentationReset();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace lg::replay
