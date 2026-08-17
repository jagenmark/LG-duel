#pragma once

#include "net/UdpTransport.hpp"
#include "replay/ReplayIoService.hpp"
#include "replay/ReplayTransferServer.hpp"
#include "server/ServerGame.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

namespace lg::replay {

// Two 512 KiB transfers need 452 packets each. At 125 Hz, two packets per
// tick let both complete inside the five-second default timeout.
inline constexpr std::size_t kDefaultKillcamPacketsPerTick = 2U;

struct KillcamServerCoordinatorConfig {
  bool enabled = true;
  std::uint32_t beforeTicks = 375U;
  std::uint32_t afterTicks = 0U;
  std::uint32_t transferTimeoutMilliseconds = 5000U;
  std::size_t maximumSegmentBytes = kReplayTransferMaxSegmentBytes;
  std::size_t packetsPerTick = kDefaultKillcamPacketsPerTick;
  ReplayRollingBufferConfig rolling = {};
};

struct KillcamServerCoordinatorStats {
  std::size_t pendingEvents = 0;
  std::size_t pendingEncodes = 0;
  std::size_t activeTransfers = 0;
  std::uint64_t acceptedEvents = 0;
  std::uint64_t skippedEvents = 0;
  std::uint64_t encodedSegments = 0;
  std::uint64_t rejectedSegments = 0;
  std::uint64_t sentPackets = 0;
};

// Server-side orchestration for the remote duel slice. It only observes the
// authoritative game after tick(), and its sole worker handles replay encode
// jobs. It never reads or writes files and never runs from ServerGame::tick().
class KillcamServerCoordinator {
public:
  KillcamServerCoordinator(ServerGame& server, UdpServerTransport& transport);
  ~KillcamServerCoordinator();

  KillcamServerCoordinator(const KillcamServerCoordinator&) = delete;
  KillcamServerCoordinator& operator=(const KillcamServerCoordinator&) = delete;

  [[nodiscard]] bool configure(
      KillcamServerCoordinatorConfig config,
      std::string* error = nullptr);
  void update(std::uint64_t nowMilliseconds);
  void shutdown();

  [[nodiscard]] const KillcamServerCoordinatorConfig& config() const {
    return config_;
  }
  [[nodiscard]] KillcamServerCoordinatorStats stats() const;
  [[nodiscard]] std::size_t activeTransfers() const {
    return transfers_.activeCount();
  }

private:
  struct PendingEvent {
    ReplayLethalEvent event = {};
    std::uint8_t clientIndex = kNoAssignedPlayer;
    std::uint32_t sessionId = 0U;
    std::uint32_t mapRevision = 0U;
    std::uint32_t readyTick = 0U;
  };
  struct PendingEncode {
    std::uint8_t clientIndex = kNoAssignedPlayer;
    std::uint32_t sessionId = 0U;
    std::uint32_t mapRevision = 0U;
    ReplayLethalEvent event = {};
  };

  [[nodiscard]] bool sameRollingConfig(
      const ReplayRollingBufferConfig& left,
      const ReplayRollingBufferConfig& right) const;
  [[nodiscard]] bool currentClientMatches(
      const PendingEvent& pending) const;
  void invalidateStaleReplayState();
  void drainCompletedEncodes(std::uint64_t nowMilliseconds);
  void drainLethalEvents();
  void startReadyEncodes();
  void processClientMessages();
  void expireDisconnectedTransfers();
  void sendDuePackets(std::uint64_t nowMilliseconds);

  ServerGame& server_;
  UdpServerTransport& transport_;
  ReplayIoService io_;
  ReplayTransferServer transfers_;
  KillcamServerCoordinatorConfig config_ = {};
  std::deque<PendingEvent> pendingEvents_;
  std::unordered_map<ReplayIoService::JobId, PendingEncode> pendingEncodes_;
  KillcamServerCoordinatorStats stats_ = {};
  std::uint32_t observedReplayGeneration_ = 0U;
  std::uint32_t observedMapRevision_ = 0U;
  bool configured_ = false;
  bool stopped_ = false;
};

} // namespace lg::replay
