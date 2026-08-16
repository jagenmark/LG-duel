#include "replay/ReplayRuntime.hpp"

#include "shared/Constants.hpp"
#include "shared/Math.hpp"
#include "sim/Combat.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace lg::replay {

class ReplayRuntime::NullTransport final : public NetTransport {
public:
    void sendCommand(const CommandPacket&) override {}
    [[nodiscard]] bool receiveCommand(CommandPacket&) override { return false; }
    void sendSnapshot(const ServerSnapshot&) override {}
    [[nodiscard]] bool receiveSnapshot(ServerSnapshot&) override { return false; }
};

ReplayRuntime::ReplayRuntime(ReplayDemo demo, ReplayRuntimeConfig config)
    : demo_(std::move(demo)), config_(std::move(config)) {
    if (config_.maxTicksPerUpdate == 0) {
        config_.maxTicksPerUpdate = 1;
    }
}

ReplayRuntime::ReplayRuntime(ReplayDemo demo)
    : ReplayRuntime(std::move(demo), ReplayRuntimeConfig{}) {}

ReplayRuntime::~ReplayRuntime() {
    stop();
}

bool ReplayRuntime::start(std::string* error) {
    if (started_) {
        return fail("replay runtime is already started", error);
    }
    if (demo_.metadata.protocolRevision != kReplayProtocolRevision ||
        demo_.metadata.buildFingerprint != kReplayBuildFingerprint ||
        demo_.metadata.simulationRevision != kReplaySimulationRevision) {
        return fail("replay metadata is incompatible with this build", error);
    }
    if (demo_.metadata.mapName.empty() || demo_.checkpoints.empty() || demo_.ticks.empty()) {
        return fail("replay has no map, checkpoint, or input data", error);
    }

    transport_ = std::make_unique<NullTransport>();
    game_ = std::make_unique<ServerGame>(*transport_);
    game_->setMapDirectory(config_.mapDirectory);
    game_->setMatchRules(demo_.metadata.matchRules);
    if (!game_->loadRequestedMap(demo_.metadata.mapName)) {
        return fail("replay map could not be loaded", error);
    }
    if (game_->snapshot().map.mapName != demo_.metadata.mapName ||
        game_->snapshot().map.contentHash != demo_.metadata.mapContentHash) {
        return fail("replay map content does not match the demo", error);
    }

    runner_ = std::make_unique<ReplayPlaybackRunner>(*game_, demo_);
    if (!runner_->initialize(error)) {
        lastError_ = error == nullptr ? "replay runner failed to initialize" : *error;
        return false;
    }
    if (!session_.begin(demo_, config_.initialFollowSlot, error)) {
        lastError_ = error == nullptr ? "replay presentation failed to initialize" : *error;
        runner_->stop();
        runner_.reset();
        game_.reset();
        transport_.reset();
        return false;
    }

    started_ = true;
    active_ = true;
    catchingUp_ = false;
    divergence_ = {};
    lastError_.clear();
    updateFrame();
    if (config_.autoplay) {
        session_.setPaused(false);
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void ReplayRuntime::stop(ReplayPresentationStopReason reason) {
    if (runner_ != nullptr) {
        runner_->stop();
    }
    if (started_ && session_.state().active) {
        session_.abort(reason);
    }
    active_ = false;
    started_ = false;
    catchingUp_ = false;
    runner_.reset();
    game_.reset();
    transport_.reset();
    frame_ = {};
}

bool ReplayRuntime::advance(double elapsedSeconds, std::string* error) {
    if (!started_ || !active_) {
        return fail("replay runtime is not active", error);
    }
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) {
        return fail("replay elapsed time is invalid", error);
    }
    (void)session_.advance(elapsedSeconds);
    return syncRunner(error);
}

bool ReplayRuntime::pause() {
    if (!started_ || !active_) return false;
    session_.setPaused(true);
    return true;
}

bool ReplayRuntime::resume() {
    if (!started_ || !active_) return false;
    session_.setPaused(false);
    return true;
}

bool ReplayRuntime::togglePause() {
    if (!started_ || !active_) return false;
    session_.setPaused(!session_.state().paused);
    return true;
}

bool ReplayRuntime::step(std::int32_t ticks, std::string* error) {
    if (!started_ || !active_ || ticks == 0 || !session_.step(ticks)) {
        return fail("replay step is out of range", error);
    }
    return syncRunner(error);
}

bool ReplayRuntime::seekTick(std::uint32_t tick, std::string* error) {
    if (!started_ || !active_ || !session_.seek(tick)) {
        return fail("replay seek is out of range", error);
    }
    return syncRunner(error);
}

bool ReplayRuntime::seekSeconds(double seconds, std::string* error) {
    if (!started_ || !active_ || !std::isfinite(seconds) || seconds < 0.0) {
        return fail("replay seek time is invalid", error);
    }
    const double tick = seconds * static_cast<double>(kReplayTickRate);
    if (tick > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return fail("replay seek time is too large", error);
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(tick);
    if (offset > std::numeric_limits<std::uint32_t>::max() - session_.state().startTick) {
        return fail("replay seek time is too large", error);
    }
    const std::uint32_t target = session_.state().startTick + offset;
    return seekTick(target, error);
}

bool ReplayRuntime::setSpeed(float speed) {
    return started_ && active_ && session_.setSpeed(speed);
}

bool ReplayRuntime::setCameraMode(ReplayCameraMode mode) {
    if (!started_ || !active_ || !session_.setCameraMode(mode)) return false;
    updateFrame();
    return true;
}

bool ReplayRuntime::setFollowSlot(std::uint8_t slot) {
    if (!started_ || !active_ || !session_.setFollowSlot(slot)) return false;
    updateFrame();
    return true;
}

bool ReplayRuntime::syncRunner(std::string* error) {
    if (runner_ == nullptr || game_ == nullptr) {
        return fail("replay runner is not available", error);
    }

    const std::uint32_t target = session_.state().currentTick;
    if (runner_->currentTick() > target) {
        if (!runner_->seek(target, error)) {
            divergence_ = runner_->divergence();
            lastError_ = error == nullptr ? "replay seek failed" : *error;
            if (divergence_.diverged) {
                session_.abort(ReplayPresentationStopReason::PlaybackDiverged);
            }
            active_ = false;
            updateFrame();
            return false;
        }
    }
    std::size_t steps = 0;
    while (runner_->currentTick() < target && steps < config_.maxTicksPerUpdate) {
        if (!runner_->step(error)) {
            divergence_ = runner_->divergence();
            if (divergence_.diverged) {
                session_.abort(ReplayPresentationStopReason::PlaybackDiverged);
            } else if (session_.state().active) {
                session_.abort(ReplayPresentationStopReason::InvalidDemo);
            }
            lastError_ = error == nullptr ? "replay playback failed" : *error;
            active_ = false;
            updateFrame();
            return false;
        }
        ++steps;
    }
    catchingUp_ = runner_->currentTick() < target;
    if (!catchingUp_ && !session_.state().active &&
        session_.state().stopReason == ReplayPresentationStopReason::Complete &&
        runner_->currentTick() == session_.state().endTick) {
        active_ = false;
    }
    updateFrame();
    if (error != nullptr) error->clear();
    return true;
}

bool ReplayRuntime::fail(std::string message, std::string* error) {
    lastError_ = std::move(message);
    if (error != nullptr) *error = lastError_;
    return false;
}

void ReplayRuntime::updateFrame() {
    if (game_ == nullptr) {
        frame_ = {};
        return;
    }
    frame_.valid = true;
    frame_.replay = true;
    frame_.cameraMode = session_.state().cameraMode;
    frame_.followSlot = session_.state().followSlot;
    frame_.serverTick = game_->snapshot().serverTick;
    frame_.fractionalTick = session_.fractionalTick();
    frame_.arena = &game_->arena();
    frame_.snapshot = game_->snapshot();
    frame_.players = frame_.snapshot.players;
    frame_.projectiles = {};
    for (std::size_t index = 0; index < game_->projectiles().size(); ++index) {
        const RocketProjectile& source = game_->projectiles()[index];
        frame_.projectiles[index] = {
            source.active,
            source.owner,
            source.weapon,
            source.position,
            source.velocity,
            std::max(source.projectileRadius, source.projectileHitboxRadius),
        };
    }
    updateCamera();
}

void ReplayRuntime::updateCamera() {
    if (frame_.followSlot >= frame_.players.size()) {
        frame_.cameraPlayer = {};
        return;
    }
    const PlayerState subject = frame_.players[frame_.followSlot];
    frame_.cameraPlayer = subject;
    if (frame_.cameraMode == ReplayCameraMode::FirstPerson) {
        return;
    }
    if (frame_.cameraMode == ReplayCameraMode::Free) {
        // Free mode starts at the followed body. A later app input layer may
        // move this camera without changing replay simulation state.
        frame_.cameraPlayer = subject;
        return;
    }

    const Vec3 target = subject.position + Vec3{0.0F, 0.0F, 0.25F};
    const Vec3 forward = yawForward(subject.viewYawRadians);
    const Vec3 wanted = target - (forward * 3.0F) + Vec3{0.0F, 0.0F, 0.75F};
    const Vec3 delta = wanted - target;
    const float distance = length(delta);
    Vec3 cameraPosition = wanted;
    if (distance > 0.001F) {
        const WorldTrace trace = traceWorld(*frame_.arena, target, delta / distance, distance);
        if (trace.hit) {
            cameraPosition = trace.end - (normalize(delta) * 0.08F);
        }
    }
    const Vec3 look = target - cameraPosition;
    const float horizontal = std::sqrt((look.x * look.x) + (look.y * look.y));
    frame_.cameraPlayer.position = cameraPosition;
    frame_.cameraPlayer.velocity = {};
    frame_.cameraPlayer.viewYawRadians = std::atan2(look.y, look.x);
    frame_.cameraPlayer.viewPitchRadians = std::atan2(look.z, horizontal);
}

} // namespace lg::replay
