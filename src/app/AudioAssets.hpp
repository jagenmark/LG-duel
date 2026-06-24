#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lg {

enum class AudioCue {
  LightningGunFireLoop,
  MachineGunFire,
  ShotgunFire,
  GrenadeLauncherFire,
  PlasmaGunFire,
  Footstep,
};

struct AudioClip {
  std::vector<float> samples;
  int sampleRate = 0;
  std::filesystem::path sourcePath;
};

[[nodiscard]] const char* audioCueFileName(AudioCue cue);
[[nodiscard]] std::filesystem::path audioCuePath(
  const std::filesystem::path& basePath,
  AudioCue cue
);
[[nodiscard]] std::optional<AudioClip> loadAudioCue(
  const std::filesystem::path& basePath,
  AudioCue cue
);
[[nodiscard]] std::optional<AudioClip> loadWavFile(
  const std::filesystem::path& path
);

} // namespace lg
