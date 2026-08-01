#include "app/AudioAssets.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wtautological-compare"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4245 4456 4457 4701 4702)
#endif
#include "../../third_party/stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace lg {
namespace {

struct WavFormat {
  std::uint16_t formatTag = 0;
  std::uint16_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t bitsPerSample = 0;
};

[[nodiscard]] std::uint16_t readU16(const std::array<unsigned char, 4>& bytes) {
  return static_cast<std::uint16_t>(
    static_cast<std::uint16_t>(bytes[0]) |
    (static_cast<std::uint16_t>(bytes[1]) << 8U)
  );
}

[[nodiscard]] std::uint32_t readU32(const std::array<unsigned char, 4>& bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
    (static_cast<std::uint32_t>(bytes[1]) << 8U) |
    (static_cast<std::uint32_t>(bytes[2]) << 16U) |
    (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

[[nodiscard]] bool readExact(std::ifstream& file, char* data, std::streamsize size) {
  file.read(data, size);
  return file.gcount() == size;
}

[[nodiscard]] std::string lowerExtension(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::ranges::transform(extension, extension.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return extension;
}

[[nodiscard]] std::filesystem::path withExtension(
  std::filesystem::path path,
  const char* extension
) {
  path.replace_extension(extension);
  return path;
}

[[nodiscard]] std::vector<std::filesystem::path> cuePathCandidates(
  const std::filesystem::path& basePath,
  AudioCue cue
) {
  const std::filesystem::path selected = audioCuePath(basePath, cue);
  std::vector<std::filesystem::path> paths{selected};
  const std::filesystem::path alternate = lowerExtension(selected) == ".ogg"
    ? withExtension(selected, ".wav")
    : withExtension(selected, ".ogg");
  if (alternate != selected) {
    paths.push_back(alternate);
  }
  return paths;
}

[[nodiscard]] std::vector<std::filesystem::path> basenameCandidates(
  const std::filesystem::path& audioDir,
  const char* basename
) {
  return {
    audioDir / (std::string{basename} + ".ogg"),
    audioDir / (std::string{basename} + ".wav"),
  };
}

[[nodiscard]] std::vector<std::filesystem::path> footstepSlotCandidates(
  const std::filesystem::path& audioDir,
  const char* preferredBasename,
  const char* importedBasename
) {
  std::vector<std::filesystem::path> paths =
    basenameCandidates(audioDir, preferredBasename);
  std::vector<std::filesystem::path> imported =
    basenameCandidates(audioDir, importedBasename);
  paths.insert(paths.end(), imported.begin(), imported.end());
  return paths;
}

void logAudioWarning(
  const std::filesystem::path& path,
  const char* reason
) {
  std::cerr << "Audio asset warning: " << reason << ": "
            << path.string() << '\n';
}

[[nodiscard]] std::optional<std::uint32_t> readChunkHeader(
  std::ifstream& file,
  std::array<char, 4>& id
) {
  std::array<unsigned char, 4> sizeBytes = {};
  if (!readExact(file, id.data(), static_cast<std::streamsize>(id.size())) ||
      !readExact(file, reinterpret_cast<char*>(sizeBytes.data()), 4)) {
    return std::nullopt;
  }
  return readU32(sizeBytes);
}

void skipChunkPadding(std::ifstream& file, std::uint32_t chunkSize) {
  if ((chunkSize & 1U) != 0U) {
    file.seekg(1, std::ios::cur);
  }
}

[[nodiscard]] bool parseFormatChunk(
  const std::vector<unsigned char>& bytes,
  WavFormat& format
) {
  if (bytes.size() < 16) {
    return false;
  }
  std::array<unsigned char, 4> scratch = {};
  scratch = {bytes[0], bytes[1], 0, 0};
  format.formatTag = readU16(scratch);
  scratch = {bytes[2], bytes[3], 0, 0};
  format.channels = readU16(scratch);
  scratch = {bytes[4], bytes[5], bytes[6], bytes[7]};
  format.sampleRate = readU32(scratch);
  scratch = {bytes[14], bytes[15], 0, 0};
  format.bitsPerSample = readU16(scratch);
  constexpr std::uint16_t kExtensibleFormat = 0xFFFE;
  if (format.formatTag == kExtensibleFormat && bytes.size() >= 40) {
    scratch = {bytes[24], bytes[25], 0, 0};
    format.formatTag = readU16(scratch);
  }
  return format.channels > 0 && format.sampleRate > 0;
}

[[nodiscard]] float pcmSample(const unsigned char* bytes, std::uint16_t bitsPerSample) {
  if (bitsPerSample == 8) {
    return (static_cast<float>(*bytes) - 128.0F) / 128.0F;
  }
  if (bitsPerSample == 16) {
    const auto value = static_cast<std::int16_t>(
      static_cast<std::uint16_t>(bytes[0]) |
      (static_cast<std::uint16_t>(bytes[1]) << 8U)
    );
    return static_cast<float>(value) / 32768.0F;
  }
  if (bitsPerSample == 24) {
    std::int32_t value =
      static_cast<std::int32_t>(bytes[0]) |
      (static_cast<std::int32_t>(bytes[1]) << 8U) |
      (static_cast<std::int32_t>(bytes[2]) << 16U);
    if ((value & 0x00800000) != 0) {
      value |= static_cast<std::int32_t>(0xFF000000);
    }
    return static_cast<float>(value) / 8388608.0F;
  }
  if (bitsPerSample == 32) {
    const auto value = static_cast<std::int32_t>(
      static_cast<std::uint32_t>(bytes[0]) |
      (static_cast<std::uint32_t>(bytes[1]) << 8U) |
      (static_cast<std::uint32_t>(bytes[2]) << 16U) |
      (static_cast<std::uint32_t>(bytes[3]) << 24U)
    );
    return static_cast<float>(value) / 2147483648.0F;
  }
  return 0.0F;
}

[[nodiscard]] float floatSample(const unsigned char* bytes) {
  static_assert(sizeof(float) == 4);
  float value = 0.0F;
  auto* output = reinterpret_cast<unsigned char*>(&value);
  output[0] = bytes[0];
  output[1] = bytes[1];
  output[2] = bytes[2];
  output[3] = bytes[3];
  return std::clamp(value, -1.0F, 1.0F);
}

[[nodiscard]] std::optional<std::vector<float>> decodeSamples(
  const WavFormat& format,
  const std::vector<unsigned char>& bytes
) {
  constexpr std::uint16_t kPcmFormat = 1;
  constexpr std::uint16_t kIeeeFloatFormat = 3;
  const bool supportedPcm =
    format.formatTag == kPcmFormat &&
    (
      format.bitsPerSample == 8 ||
      format.bitsPerSample == 16 ||
      format.bitsPerSample == 24 ||
      format.bitsPerSample == 32
    );
  const bool supportedFloat =
    format.formatTag == kIeeeFloatFormat && format.bitsPerSample == 32;
  if (!supportedPcm && !supportedFloat) {
    return std::nullopt;
  }

  const std::size_t bytesPerSample = static_cast<std::size_t>(format.bitsPerSample / 8U);
  const std::size_t frameSize = bytesPerSample * format.channels;
  if (frameSize == 0 || bytes.size() < frameSize) {
    return std::nullopt;
  }
  const std::size_t frameCount = bytes.size() / frameSize;
  std::vector<float> samples(frameCount);
  for (std::size_t frame = 0; frame < frameCount; ++frame) {
    float mixed = 0.0F;
    const unsigned char* frameBytes = bytes.data() + (frame * frameSize);
    for (std::uint16_t channel = 0; channel < format.channels; ++channel) {
      const unsigned char* sampleBytes = frameBytes + (channel * bytesPerSample);
      mixed += supportedFloat
        ? floatSample(sampleBytes)
        : pcmSample(sampleBytes, format.bitsPerSample);
    }
    samples[frame] =
      std::clamp(mixed / static_cast<float>(format.channels), -1.0F, 1.0F);
  }
  return samples;
}

} // namespace

const char* audioCueFileName(AudioCue cue) {
  switch (cue) {
  case AudioCue::LightningGunFireLoop:
    return "lg_fire_selected_low_drone.wav";
  case AudioCue::HitConfirmLight:
    return "hit_confirm_light.wav";
  case AudioCue::HitConfirmMedium:
    return "hit_confirm_medium.wav";
  case AudioCue::HitConfirmHeavy:
    return "hit_confirm_heavy.wav";
  case AudioCue::HeadshotConfirm:
    return "headshot_confirm.wav";
  case AudioCue::PainGrunt:
    return "pain_grunt.wav";
  case AudioCue::Frag:
    return "frag.wav";
  case AudioCue::RailgunFire:
    return "rg_fire_discharge.wav";
  case AudioCue::RevolverFire:
    return "revolver_fire.wav";
  case AudioCue::RailgunReady:
    return "rg_ready_chime.wav";
  case AudioCue::RocketLauncherFire:
    return "rl_fire_launch.wav";
  case AudioCue::RocketExplosion:
    return "rl_explosion_pop.wav";
  case AudioCue::MachineGunFire:
    return "mg_fire_selected_snap.wav";
  case AudioCue::ShotgunFire:
    return "sshotf1b.ogg";
  case AudioCue::GrenadeLauncherFire:
    return "gl_fire.wav";
  case AudioCue::GrenadeBounce:
    return "gl_bounce.wav";
  case AudioCue::PlasmaGunFire:
    return "pg_fire_selected_pulse.wav";
  case AudioCue::Footstep:
    return "footstep.wav";
  case AudioCue::Jump:
    return "jump1_visor.wav";
  case AudioCue::Land:
    return "land1.ogg";
  case AudioCue::RoundWin:
    return "round_win_chime.wav";
  case AudioCue::RoundLoss:
    return "round_loss_chime.wav";
  case AudioCue::CountdownFive:
    return "countdown_5_beep.wav";
  case AudioCue::CountdownFour:
    return "countdown_4_beep.wav";
  case AudioCue::CountdownThree:
    return "countdown_3_beep.wav";
  case AudioCue::CountdownTwo:
    return "countdown_2_beep.wav";
  case AudioCue::CountdownOne:
    return "countdown_1_beep.wav";
  }
  return "";
}

std::filesystem::path audioCuePath(
  const std::filesystem::path& basePath,
  AudioCue cue
) {
  return basePath / "assets" / "audio" / audioCueFileName(cue);
}

std::vector<std::filesystem::path> footstepCuePaths(
  const std::filesystem::path& basePath
) {
  const std::filesystem::path audioDir = basePath / "assets" / "audio";
  std::vector<std::filesystem::path> paths;
  for (const auto& [preferredBasename, importedBasename] : {
         std::pair{"footstep_01", "step1"},
         std::pair{"footstep_02", "step2"},
         std::pair{"footstep_03", "step3"},
         std::pair{"footstep_04", "step4"},
       }) {
    for (const auto& path :
         footstepSlotCandidates(audioDir, preferredBasename, importedBasename)) {
      if (std::filesystem::exists(path)) {
        paths.push_back(path);
        break;
      }
    }
  }
  if (!paths.empty()) {
    return paths;
  }
  for (const auto& path : cuePathCandidates(basePath, AudioCue::Footstep)) {
    if (std::filesystem::exists(path)) {
      paths.push_back(path);
      break;
    }
  }
  return paths;
}

std::optional<AudioClip> loadAudioCue(
  const std::filesystem::path& basePath,
  AudioCue cue
) {
  for (const auto& path : cuePathCandidates(basePath, cue)) {
    if (std::filesystem::exists(path)) {
      return loadAudioFile(path);
    }
  }
  logAudioWarning(audioCuePath(basePath, cue), "missing cue file");
  return std::nullopt;
}

std::optional<AudioClip> loadAudioFile(const std::filesystem::path& path) {
  const std::string extension = lowerExtension(path);
  if (extension == ".wav") {
    return loadWavFile(path);
  }
  if (extension == ".ogg") {
    return loadOggFile(path);
  }
  logAudioWarning(path, "unsupported audio file extension");
  return std::nullopt;
}

std::optional<AudioClip> loadWavFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    logAudioWarning(path, "missing WAV file");
    return std::nullopt;
  }

  std::array<char, 4> id = {};
  std::array<unsigned char, 4> sizeBytes = {};
  std::array<char, 4> wave = {};
  if (!readExact(file, id.data(), 4) ||
      !readExact(file, reinterpret_cast<char*>(sizeBytes.data()), 4) ||
      !readExact(file, wave.data(), 4) ||
      id != std::array<char, 4>{'R', 'I', 'F', 'F'} ||
      wave != std::array<char, 4>{'W', 'A', 'V', 'E'}) {
    logAudioWarning(path, "invalid WAV header");
    return std::nullopt;
  }

  WavFormat format;
  bool hasFormat = false;
  std::vector<unsigned char> sampleBytes;
  while (file) {
    auto chunkSize = readChunkHeader(file, id);
    if (!chunkSize) {
      break;
    }
    if (*chunkSize > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
      logAudioWarning(path, "WAV chunk is too large");
      return std::nullopt;
    }
    std::vector<unsigned char> chunk(*chunkSize);
    if (!chunk.empty() &&
        !readExact(file, reinterpret_cast<char*>(chunk.data()), *chunkSize)) {
      logAudioWarning(path, "truncated WAV chunk");
      return std::nullopt;
    }
    skipChunkPadding(file, *chunkSize);
    if (id == std::array<char, 4>{'f', 'm', 't', ' '}) {
      hasFormat = parseFormatChunk(chunk, format);
    } else if (id == std::array<char, 4>{'d', 'a', 't', 'a'}) {
      sampleBytes = std::move(chunk);
    }
  }

  if (!hasFormat || sampleBytes.empty()) {
    logAudioWarning(path, "WAV is missing format or sample data");
    return std::nullopt;
  }
  std::optional<std::vector<float>> samples = decodeSamples(format, sampleBytes);
  if (!samples || samples->empty()) {
    logAudioWarning(path, "unsupported or empty WAV sample data");
    return std::nullopt;
  }
  return AudioClip{
    std::move(*samples),
    static_cast<int>(format.sampleRate),
    path,
  };
}

std::optional<AudioClip> loadOggFile(const std::filesystem::path& path) {
  int channels = 0;
  int sampleRate = 0;
  short* decoded = nullptr;
  const int frameCount = stb_vorbis_decode_filename(
    path.string().c_str(),
    &channels,
    &sampleRate,
    &decoded
  );
  if (frameCount <= 0 || channels <= 0 || sampleRate <= 0 || decoded == nullptr) {
    if (decoded != nullptr) {
      std::free(decoded);
    }
    logAudioWarning(path, "missing or invalid OGG Vorbis file");
    return std::nullopt;
  }

  std::vector<float> samples(static_cast<std::size_t>(frameCount));
  const auto channelCount = static_cast<std::size_t>(channels);
  for (std::size_t frame = 0; frame < samples.size(); ++frame) {
    float mixed = 0.0F;
    for (std::size_t channel = 0; channel < channelCount; ++channel) {
      const std::size_t index = (frame * channelCount) + channel;
      mixed += static_cast<float>(decoded[index]) / 32768.0F;
    }
    samples[frame] =
      std::clamp(mixed / static_cast<float>(channelCount), -1.0F, 1.0F);
  }
  std::free(decoded);
  if (samples.empty()) {
    logAudioWarning(path, "empty OGG Vorbis sample data");
    return std::nullopt;
  }
  return AudioClip{
    std::move(samples),
    sampleRate,
    path,
  };
}

} // namespace lg
