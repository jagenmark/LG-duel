#include "app/ClientAudio.hpp"

#include "shared/Math.hpp"

#if LG_DUEL_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace lg {
namespace {

struct StereoGains {
  float left = 1.0F;
  float right = 1.0F;
};

[[nodiscard]] StereoGains stereoGains(float pan) {
  const float clampedPan = std::clamp(pan, -1.0F, 1.0F);
  return {
    clampedPan <= 0.0F ? 1.0F : 1.0F - clampedPan,
    clampedPan >= 0.0F ? 1.0F : 1.0F + clampedPan
  };
}

void appendStereoSample(
  std::vector<float>& samples,
  float sample,
  StereoGains gains
) {
  samples.push_back(std::clamp(sample * gains.left, -0.98F, 0.98F));
  samples.push_back(std::clamp(sample * gains.right, -0.98F, 0.98F));
}

} // namespace

std::size_t selectFootstepVariantIndex(
  std::size_t variantCount,
  std::size_t previousVariantIndex,
  std::uint32_t randomValue
) {
  if (variantCount == 0U) {
    return std::numeric_limits<std::size_t>::max();
  }
  if (variantCount == 1U) {
    return 0U;
  }
  const std::size_t candidate =
    static_cast<std::size_t>(randomValue % static_cast<std::uint32_t>(variantCount - 1U));
  if (candidate >= previousVariantIndex && previousVariantIndex < variantCount) {
    return candidate + 1U;
  }
  return candidate;
}

SpatialAudio worldAudio(
  float baseVolume,
  Vec3 sourcePosition,
  const PlayerState& listener
) {
  constexpr float kWorldAudioFullFadeDistance = 28.0F;
  constexpr float kWorldAudioMinimumGain = 0.25F;
  const Vec3 listenerDelta = sourcePosition - listener.position;
  const float distance = std::hypot(listenerDelta.x, listenerDelta.y);
  const float gain = std::clamp(
    1.0F - (distance / kWorldAudioFullFadeDistance),
    kWorldAudioMinimumGain,
    1.0F
  );
  const Vec3 horizontalDirection =
    normalize(Vec3{listenerDelta.x, listenerDelta.y, 0.0F});
  const float pan = length(horizontalDirection) > 0.0F
    ? std::clamp(dot(horizontalDirection, yawRight(listener.viewYawRadians)), -1.0F, 1.0F)
    : 0.0F;
  return {baseVolume * gain, pan};
}

bool ClientAudio::initialize(const std::filesystem::path& assetBasePath) {
#if LG_DUEL_HAS_SDL3
  const SDL_AudioSpec spec{SDL_AUDIO_F32, kOutputChannels, kSampleRate};
  stream_ = SDL_OpenAudioDeviceStream(
    SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
    &spec,
    nullptr,
    nullptr
  );
  if (stream_ == nullptr || !SDL_ResumeAudioStreamDevice(stream_)) {
    return false;
  }
  loadCueAssets(assetBasePath);
  return true;
#else
  (void)assetBasePath;
  return false;
#endif
}

void ClientAudio::playHit(float volume, int damageApplied, bool headshot) {
  if (headshot && queueClip(headshotConfirmClip_, volume, 0.0F)) {
    return;
  }
  if (damageApplied >= 80) {
    queueClip(hitConfirmHeavyClip_, volume, 0.0F);
  } else if (damageApplied >= 40) {
    queueClip(hitConfirmMediumClip_, volume, 0.0F);
  } else {
    queueClip(hitConfirmLightClip_, volume, 0.0F);
  }
}

void ClientAudio::playFrag(float volume) {
  queueClip(fragClip_, volume, 0.0F);
}

void ClientAudio::playPainGrunt(float volume, float pan) {
  if (painGruntFramesRemaining_ > 0U) {
    return;
  }
  if (queueClip(painGruntClip_, volume, pan)) {
    painGruntFramesRemaining_ = painGruntClip_.samples.size();
  }
}

void ClientAudio::playRailFire(float volume, float pan) {
  queueClip(railgunFireClip_, volume, pan);
}

void ClientAudio::playRailReady(float volume) {
  queueClip(railgunReadyClip_, volume, 0.0F);
}

void ClientAudio::playRocketFire(float volume, float pan) {
  queueClip(rocketLauncherFireClip_, volume, pan);
}

void ClientAudio::playMachineGunFire(float volume, float pan) {
  queueClip(machineGunFireClip_, volume, pan);
}

void ClientAudio::playShotgunFire(float volume, float pan) {
  queueClip(shotgunFireClip_, volume, pan);
}

void ClientAudio::playGrenadeLauncherFire(float volume, float pan) {
  queueClip(grenadeLauncherFireClip_, volume, pan);
}

void ClientAudio::playGrenadeBounce(float volume, float pan) {
  queueClip(grenadeBounceClip_, volume, pan);
}

void ClientAudio::playPlasmaGunFire(float volume, float pan) {
  queueClip(plasmaGunFireClip_, volume, pan);
}

void ClientAudio::playRocketExplosion(float volume, float pan) {
  queueClip(rocketExplosionClip_, volume, pan);
}

void ClientAudio::playFootstep(
  float volume,
  std::uint32_t stepIndex,
  float pan
) {
  const std::size_t clipIndex = selectFootstepVariantIndex(
    footstepClips_.size(),
    lastFootstepClipIndex_,
    footstepRng_()
  );
  if (clipIndex >= footstepClips_.size()) {
    return;
  }
  if (queueClip(footstepClips_[clipIndex], volume * footstepGain(stepIndex), pan)) {
    lastFootstepClipIndex_ = clipIndex;
  }
}

void ClientAudio::playLand(float volume, float pan) {
  queueClip(landClip_, volume, pan);
}

void ClientAudio::playJump(float volume, float pan) {
  queueClip(jumpClip_, volume, pan);
}

void ClientAudio::setLightningGunFire(bool active, float volume, float pan) {
  lightningGunFireActive_ = active && volume > 0.0F;
  lightningGunFireTargetVolume_ = lightningGunFireActive_
    ? std::clamp(volume * 0.52F, 0.0F, 1.0F)
    : 0.0F;
  lightningGunFireTargetPan_ =
    lightningGunFireActive_ ? std::clamp(pan, -1.0F, 1.0F) : 0.0F;
}

void ClientAudio::resetLightningGunFire() {
  lightningGunFireActive_ = false;
  lightningGunFireTargetVolume_ = 0.0F;
  lightningGunFireTargetPan_ = 0.0F;
  lightningGunFireGain_ = 0.0F;
  lightningGunFirePan_ = 0.0F;
  lightningGunSampleIndex_ = 0;
  painGruntFramesRemaining_ = 0;
}

void ClientAudio::playRoundResult(bool won, float volume) {
  if (won) {
    queueClip(roundWinClip_, volume, 0.0F);
  } else {
    queueClip(roundLossClip_, volume, 0.0F);
  }
}

void ClientAudio::playCountdown(std::uint32_t seconds, float volume) {
  switch (std::min(seconds, 5U)) {
  case 1:
    queueClip(countdownOneClip_, volume, 0.0F);
    break;
  case 2:
    queueClip(countdownTwoClip_, volume, 0.0F);
    break;
  case 3:
    queueClip(countdownThreeClip_, volume, 0.0F);
    break;
  case 4:
    queueClip(countdownFourClip_, volume, 0.0F);
    break;
  default:
    queueClip(countdownFiveClip_, volume, 0.0F);
    break;
  }
}

void ClientAudio::update() {
  pumpAudio();
}

void ClientAudio::shutdown() {
  setLightningGunFire(false, 0.0F);
#if LG_DUEL_HAS_SDL3
  if (stream_ != nullptr) {
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
  }
#endif
  voices_.clear();
}

void ClientAudio::loadCueAssets(const std::filesystem::path& assetBasePath) {
  lightningGunLoop_ = loadCueClip(assetBasePath, AudioCue::LightningGunFireLoop);
  hitConfirmLightClip_ = loadCueClip(assetBasePath, AudioCue::HitConfirmLight);
  hitConfirmMediumClip_ = loadCueClip(assetBasePath, AudioCue::HitConfirmMedium);
  hitConfirmHeavyClip_ = loadCueClip(assetBasePath, AudioCue::HitConfirmHeavy);
  headshotConfirmClip_ = loadCueClip(assetBasePath, AudioCue::HeadshotConfirm);
  fragClip_ = loadCueClip(assetBasePath, AudioCue::Frag);
  painGruntClip_ = loadCueClip(assetBasePath, AudioCue::PainGrunt);
  railgunFireClip_ = loadCueClip(assetBasePath, AudioCue::RailgunFire);
  railgunReadyClip_ = loadCueClip(assetBasePath, AudioCue::RailgunReady);
  rocketLauncherFireClip_ =
    loadCueClip(assetBasePath, AudioCue::RocketLauncherFire);
  rocketExplosionClip_ = loadCueClip(assetBasePath, AudioCue::RocketExplosion);
  machineGunFireClip_ = loadCueClip(assetBasePath, AudioCue::MachineGunFire);
  shotgunFireClip_ = loadCueClip(assetBasePath, AudioCue::ShotgunFire);
  grenadeLauncherFireClip_ =
    loadCueClip(assetBasePath, AudioCue::GrenadeLauncherFire);
  grenadeBounceClip_ = loadCueClip(assetBasePath, AudioCue::GrenadeBounce);
  plasmaGunFireClip_ = loadCueClip(assetBasePath, AudioCue::PlasmaGunFire);
  footstepClips_ = loadFootstepClips(assetBasePath);
  jumpClip_ = loadCueClip(assetBasePath, AudioCue::Jump);
  landClip_ = loadCueClip(assetBasePath, AudioCue::Land);
  roundWinClip_ = loadCueClip(assetBasePath, AudioCue::RoundWin);
  roundLossClip_ = loadCueClip(assetBasePath, AudioCue::RoundLoss);
  countdownFiveClip_ = loadCueClip(assetBasePath, AudioCue::CountdownFive);
  countdownFourClip_ = loadCueClip(assetBasePath, AudioCue::CountdownFour);
  countdownThreeClip_ = loadCueClip(assetBasePath, AudioCue::CountdownThree);
  countdownTwoClip_ = loadCueClip(assetBasePath, AudioCue::CountdownTwo);
  countdownOneClip_ = loadCueClip(assetBasePath, AudioCue::CountdownOne);
}

ClientAudio::LoadedClip ClientAudio::loadCueClip(
  const std::filesystem::path& assetBasePath,
  AudioCue cue
) {
  if (std::optional<AudioClip> clip = loadAudioCue(assetBasePath, cue)) {
    return LoadedClip{resampleToMixerRate(*clip)};
  }
  return {};
}

ClientAudio::LoadedClip ClientAudio::loadClipFile(
  const std::filesystem::path& path
) {
  if (std::optional<AudioClip> clip = loadAudioFile(path)) {
    return LoadedClip{resampleToMixerRate(*clip)};
  }
  return {};
}

std::vector<ClientAudio::LoadedClip> ClientAudio::loadFootstepClips(
  const std::filesystem::path& assetBasePath
) {
  std::vector<LoadedClip> clips;
  for (const auto& path : footstepCuePaths(assetBasePath)) {
    LoadedClip clip = loadClipFile(path);
    if (!clip.samples.empty()) {
      clips.push_back(std::move(clip));
    }
  }
  return clips;
}

std::vector<float> ClientAudio::resampleToMixerRate(const AudioClip& clip) {
  if (clip.samples.empty() || clip.sampleRate <= 0) {
    return {};
  }
  if (clip.sampleRate == kSampleRate) {
    return clip.samples;
  }

  const double sourceStep =
    static_cast<double>(clip.sampleRate) / static_cast<double>(kSampleRate);
  const auto outputCount = static_cast<std::size_t>(
    std::max(1.0, static_cast<double>(clip.samples.size()) / sourceStep)
  );
  std::vector<float> output(outputCount);
  for (std::size_t index = 0; index < output.size(); ++index) {
    const double sourcePosition = static_cast<double>(index) * sourceStep;
    const auto sourceIndex = static_cast<std::size_t>(sourcePosition);
    const std::size_t nextIndex =
      std::min(sourceIndex + 1U, clip.samples.size() - 1U);
    const float blend =
      static_cast<float>(sourcePosition - static_cast<double>(sourceIndex));
    output[index] =
      (clip.samples[sourceIndex] * (1.0F - blend)) +
      (clip.samples[nextIndex] * blend);
  }
  return output;
}

bool ClientAudio::queueClip(const LoadedClip& clip, float volume, float pan) {
  if (stream_ == nullptr || volume <= 0.0F || clip.samples.empty()) {
    return false;
  }
  const StereoGains gains = stereoGains(pan);
  std::vector<float> samples;
  samples.reserve(clip.samples.size() * kOutputChannels);
  for (float sample : clip.samples) {
    appendStereoSample(samples, sample * volume, gains);
  }
  addVoice(std::move(samples));
  return true;
}

float ClientAudio::footstepGain(std::uint32_t stepIndex) {
  return stepIndex % 2U == 0U ? 0.92F : 0.78F;
}

float ClientAudio::lightningGunSample() {
  if (!lightningGunLoop_.samples.empty()) {
    const float sample =
      lightningGunLoop_.samples[
        static_cast<std::size_t>(
          lightningGunSampleIndex_ % lightningGunLoop_.samples.size()
        )
      ];
    lightningGunSampleIndex_ =
      (lightningGunSampleIndex_ + 1U) % lightningGunLoop_.samples.size();
    return sample;
  }
  return 0.0F;
}

void ClientAudio::mixLightningGunLoop(std::vector<float>& buffer) {
  constexpr float kGainStepPerSample = 1.0F / 900.0F;
  constexpr float kPanStepPerSample = 1.0F / 900.0F;
  const std::size_t frameCount = buffer.size() / kOutputChannels;
  for (std::size_t frame = 0; frame < frameCount; ++frame) {
    if (lightningGunFireGain_ < lightningGunFireTargetVolume_) {
      lightningGunFireGain_ =
        std::min(lightningGunFireTargetVolume_, lightningGunFireGain_ + kGainStepPerSample);
    } else if (lightningGunFireGain_ > lightningGunFireTargetVolume_) {
      lightningGunFireGain_ =
        std::max(lightningGunFireTargetVolume_, lightningGunFireGain_ - kGainStepPerSample);
    }
    if (lightningGunFirePan_ < lightningGunFireTargetPan_) {
      lightningGunFirePan_ =
        std::min(lightningGunFireTargetPan_, lightningGunFirePan_ + kPanStepPerSample);
    } else if (lightningGunFirePan_ > lightningGunFireTargetPan_) {
      lightningGunFirePan_ =
        std::max(lightningGunFireTargetPan_, lightningGunFirePan_ - kPanStepPerSample);
    }
    if (lightningGunFireGain_ > 0.0005F) {
      const float sample = lightningGunSample() * lightningGunFireGain_;
      const StereoGains gains = stereoGains(lightningGunFirePan_);
      const std::size_t offset = frame * kOutputChannels;
      buffer[offset] += sample * gains.left;
      buffer[offset + 1U] += sample * gains.right;
    }
  }
}

bool ClientAudio::hasActiveLoop() const {
  return !lightningGunLoop_.samples.empty() &&
    (lightningGunFireActive_ || lightningGunFireGain_ > 0.0005F);
}

void ClientAudio::addVoice(std::vector<float> samples) {
  if (samples.empty()) {
    return;
  }

  removeFinishedVoices();
  if (voices_.size() >= kMaxActiveVoices) {
    const auto quietestTail = std::min_element(
      voices_.begin(),
      voices_.end(),
      [](const SoundVoice& lhs, const SoundVoice& rhs) {
        return remainingFrames(lhs) < remainingFrames(rhs);
      }
    );
    voices_.erase(quietestTail);
  }
  voices_.push_back(SoundVoice{std::move(samples), 0});
}

void ClientAudio::pumpAudio() {
#if LG_DUEL_HAS_SDL3
  if (stream_ == nullptr) {
    return;
  }

  removeFinishedVoices();
  const int queuedBytes = SDL_GetAudioStreamQueued(stream_);
  int queuedFrames = std::max(0, queuedBytes) /
    static_cast<int>(sizeof(float) * kOutputChannels);
  while ((!voices_.empty() || hasActiveLoop()) && queuedFrames < kTargetQueuedSamples) {
    const int sampleCount =
      std::min(kMixChunkSamples, kTargetQueuedSamples - queuedFrames);
    mixBuffer_.assign(
      static_cast<std::size_t>(sampleCount) * kOutputChannels,
      0.0F
    );

    for (SoundVoice& voice : voices_) {
      const std::size_t remaining = remainingFrames(voice);
      const std::size_t voiceFrames =
        std::min(remaining, static_cast<std::size_t>(sampleCount));
      for (std::size_t frame = 0; frame < voiceFrames; ++frame) {
        const std::size_t mixOffset = frame * kOutputChannels;
        const std::size_t voiceOffset =
          (voice.playhead + frame) * kOutputChannels;
        mixBuffer_[mixOffset] += voice.samples[voiceOffset];
        mixBuffer_[mixOffset + 1U] += voice.samples[voiceOffset + 1U];
      }
    }
    if (hasActiveLoop()) {
      mixLightningGunLoop(mixBuffer_);
    }
    for (float& sample : mixBuffer_) {
      sample = std::clamp(sample, -0.98F, 0.98F);
    }
    for (SoundVoice& voice : voices_) {
      voice.playhead += static_cast<std::size_t>(sampleCount);
    }
    if (painGruntFramesRemaining_ > 0U) {
      painGruntFramesRemaining_ =
        static_cast<std::size_t>(sampleCount) >= painGruntFramesRemaining_
        ? 0U
        : painGruntFramesRemaining_ - static_cast<std::size_t>(sampleCount);
    }
    removeFinishedVoices();
    SDL_PutAudioStreamData(
      stream_,
      mixBuffer_.data(),
      static_cast<int>(mixBuffer_.size() * sizeof(float))
    );
    queuedFrames += sampleCount;
  }
#endif
}

void ClientAudio::removeFinishedVoices() {
  voices_.erase(
    std::remove_if(
      voices_.begin(),
      voices_.end(),
      [](const SoundVoice& voice) {
        return voice.playhead >= voice.samples.size() / kOutputChannels;
      }
    ),
    voices_.end()
  );
}

std::size_t ClientAudio::remainingFrames(const SoundVoice& voice) {
  const std::size_t frameCount = voice.samples.size() / kOutputChannels;
  if (voice.playhead >= frameCount) {
    return 0;
  }
  return frameCount - voice.playhead;
}

void updateFootstepAudio(
  FootstepAudioState& state,
  const PlayerState& player,
  const PlayerState& listener,
  bool localPlayer,
  float volume,
  ClientAudio& audio
) {
  constexpr float kMinimumStepSpeed = 1.15F;
  constexpr float kMinimumJumpAudioSpeed = 1.0F;
  constexpr float kBaseStrideDistance = 1.45F;
  constexpr float kMinimumStrideDistance = 0.95F;

  if (!state.initialized) {
    state.previousPosition = player.position;
    state.wasOnGround = player.onGround;
    state.initialized = true;
    return;
  }

  const float horizontalSpeed = std::hypot(player.velocity.x, player.velocity.y);
  const Vec3 delta = player.position - state.previousPosition;
  const float horizontalDistance = std::hypot(delta.x, delta.y);
  const bool quietMovement = player.crouched || player.sneaking;
  const bool movingOnGround =
    player.health > 0 && player.onGround && horizontalSpeed >= kMinimumStepSpeed;

  auto playStep = [&]() {
    const SpatialAudio spatial = localPlayer
      ? SpatialAudio{volume, 0.0F}
      : worldAudio(volume, player.position, listener);
    audio.playFootstep(spatial.volume, state.stepIndex++, spatial.pan);
  };

  auto playLand = [&]() {
    const SpatialAudio spatial = localPlayer
      ? SpatialAudio{volume, 0.0F}
      : worldAudio(volume, player.position, listener);
    audio.playLand(spatial.volume, spatial.pan);
  };

  auto playJump = [&]() {
    const SpatialAudio spatial = localPlayer
      ? SpatialAudio{volume, 0.0F}
      : worldAudio(volume, player.position, listener);
    audio.playJump(spatial.volume, spatial.pan);
  };

  if (
    !player.onGround &&
    state.wasOnGround &&
    player.health > 0 &&
    player.velocity.z >= kMinimumJumpAudioSpeed
  ) {
    playJump();
    state.distanceSinceStep = 0.0F;
  } else if (player.onGround && !state.wasOnGround && player.health > 0) {
    playLand();
    state.distanceSinceStep = 0.0F;
  } else if (movingOnGround && !quietMovement) {
    state.distanceSinceStep += horizontalDistance;
    const float strideDistance = std::max(
      kMinimumStrideDistance,
      kBaseStrideDistance - (horizontalSpeed * 0.045F)
    );
    if (state.distanceSinceStep >= strideDistance) {
      playStep();
      state.distanceSinceStep = std::fmod(state.distanceSinceStep, strideDistance);
    }
  } else if (quietMovement) {
    state.distanceSinceStep = 0.0F;
  } else if (!player.onGround || horizontalSpeed < 0.25F) {
    state.distanceSinceStep = 0.0F;
  }

  state.previousPosition = player.position;
  state.wasOnGround = player.onGround;
}

} // namespace lg
