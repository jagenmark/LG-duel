#include "sim/GameplayConfig.hpp"

#include "shared/Constants.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace lg {
namespace {

[[nodiscard]] std::string trimComment(std::string line) {
  const std::size_t comment = line.find('#');
  if (comment != std::string::npos) {
    line.erase(comment);
  }
  return line;
}

[[nodiscard]] bool parseFloat(std::string_view text, float& value) {
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end && std::isfinite(value);
}

[[nodiscard]] bool parseInt(std::string_view text, int& value) {
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end;
}

[[nodiscard]] std::string lineError(int lineNumber, const std::string& message) {
  return "line " + std::to_string(lineNumber) + ": " + message;
}

[[nodiscard]] bool inRange(float value, float minimum, float maximum) {
  return std::isfinite(value) && value >= minimum && value <= maximum;
}

[[nodiscard]] bool applyGrenadeFloat(
  GrenadeLauncherTuning& tuning,
  const std::string& key,
  float value
) {
  if (key == "grenade.speed" && inRange(value, 0.1F, 200.0F)) {
    tuning.speed = value;
  } else if (key == "grenade.vertical_boost" && inRange(value, -100.0F, 100.0F)) {
    tuning.verticalBoost = value;
  } else if (key == "grenade.gravity" && inRange(value, 0.0F, 200.0F)) {
    tuning.gravity = value;
  } else if (key == "grenade.bounce_damping" && inRange(value, 0.0F, 1.5F)) {
    tuning.bounceDamping = value;
  } else if (key == "grenade.rest_speed" && inRange(value, 0.0F, 20.0F)) {
    tuning.restSpeed = value;
  } else if (key == "grenade.bounce_sound_min_speed" && inRange(value, 0.0F, 50.0F)) {
    tuning.bounceSoundMinSpeed = value;
  } else if (key == "grenade.radius" && inRange(value, 0.1F, 100.0F)) {
    tuning.radius = value;
  } else if (key == "grenade.knockback" && inRange(value, 0.0F, 1000.0F)) {
    tuning.knockback = value;
  } else if (key == "grenade.fuse_seconds" && inRange(value, kFixedTickSeconds, 30.0F)) {
    tuning.fuseTicks =
      std::max<std::uint32_t>(1U, static_cast<std::uint32_t>(std::lround(value / kFixedTickSeconds)));
  } else {
    return false;
  }
  return true;
}

[[nodiscard]] bool applyGrenadeInt(
  GrenadeLauncherTuning& tuning,
  const std::string& key,
  int value
) {
  if (key == "grenade.direct_damage" && value >= 0 && value <= 500) {
    tuning.directDamage = value;
  } else if (key == "grenade.splash_damage" && value >= 0 && value <= 500) {
    tuning.splashDamage = value;
  } else if (key == "grenade.cooldown_ticks" && value >= 1 && value <= 1000) {
    tuning.cooldownTicks = static_cast<std::uint32_t>(value);
  } else {
    return false;
  }
  return true;
}

} // namespace

GameplayConfigLoadResult loadGameplayConfigFromText(std::string_view text) {
  GameplayConfigLoadResult result;
  result.config = {};

  bool hasVersion = false;
  std::istringstream input{std::string(text)};
  std::string line;
  int lineNumber = 0;
  while (std::getline(input, line)) {
    ++lineNumber;
    line = trimComment(std::move(line));

    std::istringstream lineStream(line);
    std::vector<std::string> tokens;
    std::string token;
    while (lineStream >> token) {
      tokens.push_back(std::move(token));
    }
    if (tokens.empty()) {
      continue;
    }

    if (tokens[0] == "version") {
      int version = 0;
      if (tokens.size() != 2 || !parseInt(tokens[1], version)) {
        result.error = lineError(lineNumber, "version expects one integer");
        return result;
      }
      if (version != 1) {
        result.error = lineError(lineNumber, "only gameplay config version 1 is supported");
        return result;
      }
      hasVersion = true;
      continue;
    }

    if (tokens.size() != 2) {
      result.error = lineError(lineNumber, "expected '<key> <value>'");
      return result;
    }

    int intValue = 0;
    if (parseInt(tokens[1], intValue) && applyGrenadeInt(result.config.grenadeLauncher, tokens[0], intValue)) {
      continue;
    }

    float floatValue = 0.0F;
    if (parseFloat(tokens[1], floatValue) && applyGrenadeFloat(result.config.grenadeLauncher, tokens[0], floatValue)) {
      continue;
    }

    result.error = lineError(lineNumber, "unknown key or out-of-range value '" + tokens[0] + "'");
    return result;
  }

  if (!hasVersion) {
    result.error = "gameplay config is missing version";
    return result;
  }

  result.ok = true;
  return result;
}

GameplayConfigLoadResult loadGameplayConfigFromFile(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    return {{}, false, "could not open gameplay config file '" + path + "'"};
  }

  std::ostringstream text;
  text << file.rdbuf();
  GameplayConfigLoadResult result = loadGameplayConfigFromText(text.str());
  if (!result.ok) {
    result.error = path + ": " + result.error;
  }
  return result;
}

} // namespace lg
