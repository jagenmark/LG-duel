#pragma once

#include "net/NetProtocol.hpp"
#include "replay/ReplayPlayback.hpp"
#include "replay/ReplayPresentationSession.hpp"
#include "server/ServerGame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace lg::replay {

struct ReplayRuntimeConfig {
    std::string mapDirectory = "maps";
    std::uint8_t initialFollowSlot = 0;
    std::size_t maxTicksPerUpdate = 256;
    bool autoplay = false;
};

// One presentation input for both live and replay rendering. The app can
// switch this source at a frame boundary and then feed the same renderer,
// HUD, effects, and audio code from the selected snapshot.
struct ReplayPresentationFrame {
    bool valid = false;
    bool replay = true;
    ReplayCameraMode cameraMode = ReplayCameraMode::FirstPerson;
    std::uint8_t followSlot = 0;
    std::uint32_t serverTick = 0;
    double fractionalTick = 0.0;
    const Arena* arena = nullptr;
    ServerSnapshot snapshot = {};
    std::array<PlayerState, kDuelPlayerCount> players = {};
    std::array<RocketProjectileSnapshot, kMaxRocketProjectiles> projectiles = {};
    PlayerState cameraPlayer = {};
};

class ReplayRuntime {
public:
    ReplayRuntime(ReplayDemo demo, ReplayRuntimeConfig config);
    explicit ReplayRuntime(ReplayDemo demo);
    ~ReplayRuntime();

    ReplayRuntime(const ReplayRuntime&) = delete;
    ReplayRuntime& operator=(const ReplayRuntime&) = delete;

    [[nodiscard]] bool start(std::string* error = nullptr);
    void stop(ReplayPresentationStopReason reason = ReplayPresentationStopReason::UserSkipped);

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool started() const { return started_; }
    [[nodiscard]] bool diverged() const { return divergence_.diverged; }
    [[nodiscard]] bool catchingUp() const { return catchingUp_; }
    [[nodiscard]] const std::string& lastError() const { return lastError_; }
    [[nodiscard]] const ReplayDivergence& divergence() const { return divergence_; }

    [[nodiscard]] bool advance(double elapsedSeconds, std::string* error = nullptr);
    [[nodiscard]] bool pause();
    [[nodiscard]] bool resume();
    [[nodiscard]] bool togglePause();
    [[nodiscard]] bool step(std::int32_t ticks = 1, std::string* error = nullptr);
    [[nodiscard]] bool seekTick(std::uint32_t tick, std::string* error = nullptr);
    [[nodiscard]] bool seekSeconds(double seconds, std::string* error = nullptr);
    [[nodiscard]] bool setSpeed(float speed);
    [[nodiscard]] bool setCameraMode(ReplayCameraMode mode);
    [[nodiscard]] bool setFollowSlot(std::uint8_t slot);

    [[nodiscard]] const ReplayPresentationState& state() const { return session_.state(); }
    [[nodiscard]] const ReplayPresentationFrame& frame() const { return frame_; }
    [[nodiscard]] const ServerSnapshot& snapshot() const { return frame_.snapshot; }
    [[nodiscard]] const Arena* arena() const { return frame_.arena; }
    [[nodiscard]] const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>&
    projectiles() const { return frame_.projectiles; }
    [[nodiscard]] const ReplayDemo& demo() const { return demo_; }

private:
    class NullTransport;

    [[nodiscard]] bool syncRunner(std::string* error);
    [[nodiscard]] bool fail(std::string message, std::string* error);
    void updateFrame();
    void updateCamera();

    ReplayDemo demo_;
    ReplayRuntimeConfig config_;
    std::unique_ptr<NullTransport> transport_;
    std::unique_ptr<ServerGame> game_;
    std::unique_ptr<ReplayPlaybackRunner> runner_;
    ReplayPresentationSession session_;
    ReplayPresentationFrame frame_ = {};
    ReplayDivergence divergence_ = {};
    std::string lastError_;
    bool started_ = false;
    bool active_ = false;
    bool catchingUp_ = false;
};

} // namespace lg::replay
