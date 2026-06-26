#include "app/AudioAssets.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <utility>
#include <string>

namespace {

void expect(bool condition, const char* context) {
  if (condition) {
    return;
  }
  std::cerr << context << '\n';
  std::exit(1);
}

void writeU16(std::ofstream& file, std::uint16_t value) {
  const std::array<char, 2> bytes{
    static_cast<char>(value & 0xFFU),
    static_cast<char>((value >> 8U) & 0xFFU),
  };
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU32(std::ofstream& file, std::uint32_t value) {
  const std::array<char, 4> bytes{
    static_cast<char>(value & 0xFFU),
    static_cast<char>((value >> 8U) & 0xFFU),
    static_cast<char>((value >> 16U) & 0xFFU),
    static_cast<char>((value >> 24U) & 0xFFU),
  };
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeI24(std::ofstream& file, std::int32_t value) {
  const std::uint32_t encoded =
    static_cast<std::uint32_t>(value) & 0x00FFFFFFU;
  const std::array<char, 3> bytes{
    static_cast<char>(encoded & 0xFFU),
    static_cast<char>((encoded >> 8U) & 0xFFU),
    static_cast<char>((encoded >> 16U) & 0xFFU),
  };
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeStereoPcm16Wav(const std::filesystem::path& path) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  constexpr std::uint16_t channels = 2;
  constexpr std::uint32_t sampleRate = 48000;
  constexpr std::uint16_t bitsPerSample = 16;
  constexpr std::uint32_t dataBytes = 8;
  file.write("RIFF", 4);
  writeU32(file, 36U + dataBytes);
  file.write("WAVE", 4);
  file.write("fmt ", 4);
  writeU32(file, 16);
  writeU16(file, 1);
  writeU16(file, channels);
  writeU32(file, sampleRate);
  writeU32(file, sampleRate * channels * (bitsPerSample / 8U));
  writeU16(file, channels * (bitsPerSample / 8U));
  writeU16(file, bitsPerSample);
  file.write("data", 4);
  writeU32(file, dataBytes);
  writeU16(file, 32767);
  writeU16(file, 32767);
  writeU16(file, 0);
  writeU16(file, 0);
}

void writeStereoPcm24Wav(const std::filesystem::path& path) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  constexpr std::uint16_t channels = 2;
  constexpr std::uint32_t sampleRate = 44100;
  constexpr std::uint16_t bitsPerSample = 24;
  constexpr std::uint32_t dataBytes = 12;
  file.write("RIFF", 4);
  writeU32(file, 36U + dataBytes);
  file.write("WAVE", 4);
  file.write("fmt ", 4);
  writeU32(file, 16);
  writeU16(file, 1);
  writeU16(file, channels);
  writeU32(file, sampleRate);
  writeU32(file, sampleRate * channels * (bitsPerSample / 8U));
  writeU16(file, channels * (bitsPerSample / 8U));
  writeU16(file, bitsPerSample);
  file.write("data", 4);
  writeU32(file, dataBytes);
  writeI24(file, 8388607);
  writeI24(file, 8388607);
  writeI24(file, -8388608);
  writeI24(file, -8388608);
}

void writeStereoPcm24ExtensibleWav(const std::filesystem::path& path) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  constexpr std::uint16_t channels = 2;
  constexpr std::uint32_t sampleRate = 44100;
  constexpr std::uint16_t bitsPerSample = 24;
  constexpr std::uint32_t dataBytes = 12;
  file.write("RIFF", 4);
  writeU32(file, 4U + 48U + 20U);
  file.write("WAVE", 4);
  file.write("fmt ", 4);
  writeU32(file, 40);
  writeU16(file, 0xFFFE);
  writeU16(file, channels);
  writeU32(file, sampleRate);
  writeU32(file, sampleRate * channels * (bitsPerSample / 8U));
  writeU16(file, channels * (bitsPerSample / 8U));
  writeU16(file, bitsPerSample);
  writeU16(file, 22);
  writeU16(file, bitsPerSample);
  writeU32(file, 0x3);
  const std::array<char, 16> pcmGuid{
    static_cast<char>(0x01), static_cast<char>(0x00),
    static_cast<char>(0x00), static_cast<char>(0x00),
    static_cast<char>(0x00), static_cast<char>(0x00),
    static_cast<char>(0x10), static_cast<char>(0x00),
    static_cast<char>(0x80), static_cast<char>(0x00),
    static_cast<char>(0x00), static_cast<char>(0xAA),
    static_cast<char>(0x00), static_cast<char>(0x38),
    static_cast<char>(0x9B), static_cast<char>(0x71),
  };
  file.write(pcmGuid.data(), static_cast<std::streamsize>(pcmGuid.size()));
  file.write("data", 4);
  writeU32(file, dataBytes);
  writeI24(file, 8388607);
  writeI24(file, 8388607);
  writeI24(file, -8388608);
  writeI24(file, -8388608);
}

[[nodiscard]] std::optional<std::filesystem::path> findRepoRoot(
  std::filesystem::path path
) {
  path = std::filesystem::absolute(path);
  for (int depth = 0; depth < 8; ++depth) {
    if (std::filesystem::exists(path / "assets" / "audio" / "README.md")) {
      return path;
    }
    if (!path.has_parent_path() || path == path.parent_path()) {
      break;
    }
    path = path.parent_path();
  }
  return std::nullopt;
}

} // namespace

int main() {
  const std::array<std::pair<lg::AudioCue, const char*>, 22> runtimeCues{{
    {lg::AudioCue::LightningGunFireLoop, "lg_fire_selected_low_drone.wav"},
    {lg::AudioCue::HitConfirmLight, "hit_confirm_light.wav"},
    {lg::AudioCue::HitConfirmMedium, "hit_confirm_medium.wav"},
    {lg::AudioCue::HitConfirmHeavy, "hit_confirm_heavy.wav"},
    {lg::AudioCue::PainGrunt, "pain_grunt.wav"},
    {lg::AudioCue::Frag, "frag.wav"},
    {lg::AudioCue::RailgunFire, "rg_fire_discharge.wav"},
    {lg::AudioCue::RailgunReady, "rg_ready_chime.wav"},
    {lg::AudioCue::RocketLauncherFire, "rl_fire_launch.wav"},
    {lg::AudioCue::RocketExplosion, "rl_explosion_pop.wav"},
    {lg::AudioCue::MachineGunFire, "mg_fire_selected_snap.wav"},
    {lg::AudioCue::ShotgunFire, "sg_fire_selected_blast.wav"},
    {lg::AudioCue::GrenadeLauncherFire, "gl_fire_selected_thump.wav"},
    {lg::AudioCue::PlasmaGunFire, "pg_fire_selected_pulse.wav"},
    {lg::AudioCue::Footstep, "footstep.wav"},
    {lg::AudioCue::RoundWin, "round_win_chime.wav"},
    {lg::AudioCue::RoundLoss, "round_loss_chime.wav"},
    {lg::AudioCue::CountdownFive, "countdown_5_beep.wav"},
    {lg::AudioCue::CountdownFour, "countdown_4_beep.wav"},
    {lg::AudioCue::CountdownThree, "countdown_3_beep.wav"},
    {lg::AudioCue::CountdownTwo, "countdown_2_beep.wav"},
    {lg::AudioCue::CountdownOne, "countdown_1_beep.wav"},
  }};
  for (const auto& [cue, fileName] : runtimeCues) {
    expect(
      std::string{lg::audioCueFileName(cue)} == fileName,
      "runtime audio cue should map to its selected checked-in WAV"
    );
  }

  const auto repoRoot = findRepoRoot(std::filesystem::current_path());
  expect(repoRoot.has_value(), "test should find repository root");
  for (const auto& [cue, fileName] : runtimeCues) {
    const auto clip = lg::loadAudioCue(*repoRoot, cue);
    if (!clip.has_value()) {
      std::cerr << "runtime audio cue WAV should load from assets/audio: "
                << fileName << '\n';
      return 1;
    }
    expect(clip->sourcePath.filename() == fileName, "loaded cue should use selected file");
    expect(!clip->samples.empty(), "loaded runtime audio cue should contain samples");
  }

  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "lg_duel_audio_assets_test";
  std::filesystem::remove_all(root);
  expect(
    !lg::loadAudioCue(root, lg::AudioCue::LightningGunFireLoop).has_value(),
    "missing cue WAV should fall back without loading a clip"
  );

  const std::filesystem::path cuePath =
    lg::audioCuePath(root, lg::AudioCue::LightningGunFireLoop);
  writeStereoPcm16Wav(cuePath);
  const auto clip = lg::loadAudioCue(root, lg::AudioCue::LightningGunFireLoop);
  expect(clip.has_value(), "valid PCM16 cue WAV should load");
  expect(clip->sampleRate == 48000, "loaded cue should keep the WAV sample rate");
  expect(clip->samples.size() == 2, "stereo WAV should downmix to mono frames");
  expect(clip->samples[0] > 0.99F, "first downmixed sample should preserve signal");
  expect(clip->samples[1] == 0.0F, "second downmixed sample should preserve silence");

  const std::filesystem::path pcm24Path = root / "assets" / "audio" / "pcm24.wav";
  writeStereoPcm24Wav(pcm24Path);
  const auto pcm24 = lg::loadWavFile(pcm24Path);
  expect(pcm24.has_value(), "valid PCM24 cue WAV should load");
  expect(pcm24->sampleRate == 44100, "loaded PCM24 cue should keep the WAV sample rate");
  expect(pcm24->samples.size() == 2, "stereo PCM24 WAV should downmix to mono frames");
  expect(pcm24->samples[0] > 0.99F, "positive PCM24 sample should preserve signal");
  expect(pcm24->samples[1] <= -1.0F, "negative PCM24 sample should preserve signal");

  const std::filesystem::path pcm24ExtensiblePath =
    root / "assets" / "audio" / "pcm24_extensible.wav";
  writeStereoPcm24ExtensibleWav(pcm24ExtensiblePath);
  const auto pcm24Extensible = lg::loadWavFile(pcm24ExtensiblePath);
  expect(
    pcm24Extensible.has_value(),
    "valid extensible PCM24 cue WAV should load"
  );
  expect(
    pcm24Extensible->samples.size() == 2,
    "stereo extensible PCM24 WAV should downmix to mono frames"
  );

  std::filesystem::remove_all(root);
  return 0;
}
