#include "sim/BalanceConfig.hpp"

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

[[nodiscard]] bool applyFloat(
  BalanceConfig& config,
  const std::string& key,
  float value
) {
  if (key == "weapon.lg.range" && inRange(value, 0.1F, 1000.0F)) {
    config.lightningGun.range = value;
  } else if (key == "weapon.lg.eye_height" && inRange(value, 0.0F, 10.0F)) {
    config.lightningGun.eyeHeight = value;
  } else if (key == "weapon.rg.range" && inRange(value, 0.1F, 5000.0F)) {
    config.railgun.range = value;
  } else if (key == "weapon.rg.eye_height" && inRange(value, 0.0F, 10.0F)) {
    config.railgun.eyeHeight = value;
  } else if (key == "weapon.rg.knockback" && inRange(value, 0.0F, 1000.0F)) {
    config.railgun.knockback = value;
  } else if (key == "weapon.mg.range" && inRange(value, 0.1F, 5000.0F)) {
    config.machineGun.range = value;
  } else if (key == "weapon.mg.eye_height" && inRange(value, 0.0F, 10.0F)) {
    config.machineGun.eyeHeight = value;
  } else if (key == "weapon.mg.knockback" && inRange(value, 0.0F, 1000.0F)) {
    config.machineGun.knockback = value;
  } else if (key == "weapon.mg.spread_radians" && inRange(value, 0.0F, 1.5F)) {
    config.machineGun.spreadRadians = value;
  } else if (key == "weapon.sg.range" && inRange(value, 0.1F, 5000.0F)) {
    config.shotgun.range = value;
  } else if (key == "weapon.sg.spread_radians" && inRange(value, 0.0F, 1.5F)) {
    config.shotgun.spreadRadians = value;
  } else if (key == "weapon.sg.eye_height" && inRange(value, 0.0F, 10.0F)) {
    config.shotgun.eyeHeight = value;
  } else if (key == "weapon.sg.knockback" && inRange(value, 0.0F, 1000.0F)) {
    config.shotgun.knockback = value;
  } else if (key == "weapon.rl.speed" && inRange(value, 0.1F, 500.0F)) {
    config.rocketLauncher.speed = value;
  } else if (key == "weapon.rl.radius" && inRange(value, 0.1F, 100.0F)) {
    config.rocketLauncher.radius = value;
  } else if (key == "weapon.rl.eye_height" && inRange(value, 0.0F, 10.0F)) {
    config.rocketLauncher.eyeHeight = value;
  } else if (key == "weapon.gl.speed" && inRange(value, 0.1F, 500.0F)) {
    config.grenadeLauncher.speed = value;
  } else if (key == "weapon.gl.vertical_boost" && inRange(value, -100.0F, 100.0F)) {
    config.grenadeLauncher.verticalBoost = value;
  } else if (key == "weapon.gl.gravity" && inRange(value, 0.0F, 500.0F)) {
    config.grenadeLauncher.gravity = value;
  } else if (key == "weapon.gl.bounce_damping" && inRange(value, 0.0F, 1.5F)) {
    config.grenadeLauncher.bounceDamping = value;
  } else if (key == "weapon.gl.rest_speed" && inRange(value, 0.0F, 20.0F)) {
    config.grenadeLauncher.restSpeed = value;
  } else if (key == "weapon.gl.bounce_sound_min_speed" && inRange(value, 0.0F, 50.0F)) {
    config.grenadeLauncher.bounceSoundMinSpeed = value;
  } else if (key == "weapon.gl.projectile_radius" && inRange(value, 0.01F, 5.0F)) {
    config.grenadeLauncher.projectileRadius = value;
  } else if (key == "weapon.gl.projectile_hitbox_radius" && inRange(value, 0.0F, 5.0F)) {
    config.grenadeLauncher.projectileHitboxRadius = value;
  } else if (key == "weapon.gl.radius" && inRange(value, 0.1F, 100.0F)) {
    config.grenadeLauncher.radius = value;
  } else if (key == "weapon.gl.eye_height" && inRange(value, 0.0F, 10.0F)) {
    config.grenadeLauncher.eyeHeight = value;
  } else if (key == "weapon.gl.fuse_seconds" && inRange(value, kFixedTickSeconds, 30.0F)) {
    config.grenadeLauncher.fuseTicks =
      std::max<std::uint32_t>(1U, static_cast<std::uint32_t>(std::lround(value / kFixedTickSeconds)));
  } else if (key == "weapon.pg.speed" && inRange(value, 0.1F, 500.0F)) {
    config.plasmaGun.speed = value;
  } else if (key == "weapon.pg.radius" && inRange(value, 0.01F, 50.0F)) {
    config.plasmaGun.radius = value;
  } else if (key == "weapon.pg.knockback" && inRange(value, 0.0F, 1000.0F)) {
    config.plasmaGun.knockback = value;
  } else if (key == "weapon.pg.eye_height" && inRange(value, 0.0F, 10.0F)) {
    config.plasmaGun.eyeHeight = value;
  } else {
    return false;
  }
  return true;
}

[[nodiscard]] bool applyInt(
  BalanceConfig& config,
  const std::string& key,
  int value
) {
  if (key == "weapon.rg.cooldown_ticks" && value >= 1 && value <= 5000) {
    config.railgunCooldownTicks = static_cast<std::uint32_t>(value);
  } else if (key == "weapon.mg.cooldown_ticks" && value >= 1 && value <= 5000) {
    config.machineGunCooldownTicks = static_cast<std::uint32_t>(value);
  } else if (key == "weapon.sg.pellet_count" && value >= 1 && value <= 255) {
    config.shotgun.pelletCount = static_cast<std::uint8_t>(value);
  } else if (key == "weapon.sg.cooldown_ticks" && value >= 1 && value <= 5000) {
    config.shotgunCooldownTicks = static_cast<std::uint32_t>(value);
  } else if (key == "weapon.rl.max_lifetime_ticks" && value >= 1 && value <= 10000) {
    config.rocketLauncher.maxLifetimeTicks = static_cast<std::uint32_t>(value);
  } else if (key == "weapon.rl.cooldown_ticks" && value >= 1 && value <= 5000) {
    config.rocketLauncherCooldownTicks = static_cast<std::uint32_t>(value);
  } else if (key == "weapon.gl.cooldown_ticks" && value >= 1 && value <= 5000) {
    config.grenadeLauncher.cooldownTicks = static_cast<std::uint32_t>(value);
  } else if (key == "weapon.pg.max_lifetime_ticks" && value >= 1 && value <= 10000) {
    config.plasmaGun.maxLifetimeTicks = static_cast<std::uint32_t>(value);
  } else if (key == "weapon.pg.cooldown_ticks" && value >= 1 && value <= 5000) {
    config.plasmaGun.cooldownTicks = static_cast<std::uint32_t>(value);
  } else if (key == "weapon.switch_pullout_ticks" && value >= 0 && value <= 5000) {
    config.weaponPulloutTicks = static_cast<std::uint32_t>(value);
  } else {
    return false;
  }
  return true;
}

} // namespace

BalanceConfigLoadResult loadBalanceConfigFromText(std::string_view text) {
  BalanceConfigLoadResult result;
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
        result.error = lineError(lineNumber, "only balance config version 1 is supported");
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
    if (parseInt(tokens[1], intValue) && applyInt(result.config, tokens[0], intValue)) {
      continue;
    }

    float floatValue = 0.0F;
    if (parseFloat(tokens[1], floatValue) && applyFloat(result.config, tokens[0], floatValue)) {
      continue;
    }

    result.error = lineError(lineNumber, "unknown key or out-of-range value '" + tokens[0] + "'");
    return result;
  }

  if (!hasVersion) {
    result.error = "balance config is missing version";
    return result;
  }

  result.ok = true;
  return result;
}

BalanceConfigLoadResult loadBalanceConfigFromFile(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    return {{}, false, "could not open balance config file '" + path + "'"};
  }

  std::ostringstream text;
  text << file.rdbuf();
  BalanceConfigLoadResult result = loadBalanceConfigFromText(text.str());
  if (!result.ok) {
    result.error = path + ": " + result.error;
  }
  return result;
}

} // namespace lg
