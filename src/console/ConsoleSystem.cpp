#include "console/ConsoleSystem.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace lg {
namespace {

std::vector<std::string> tokenize(std::string_view line) {
  std::vector<std::string> tokens;
  std::string token;
  bool quoted = false;
  for (const char character : line) {
    if (character == '"') {
      quoted = !quoted;
    } else if (!quoted && (character == ' ' || character == '\t')) {
      if (!token.empty()) {
        tokens.push_back(token);
        token.clear();
      }
    } else {
      token.push_back(character);
    }
  }
  if (!token.empty()) {
    tokens.push_back(token);
  }
  return tokens;
}

std::string formatValue(const CvarValue& value) {
  return std::visit(
    [](const auto& typedValue) {
      using T = std::decay_t<decltype(typedValue)>;
      if constexpr (std::is_same_v<T, bool>) {
        return std::string(typedValue ? "1" : "0");
      } else if constexpr (std::is_same_v<T, float>) {
        std::ostringstream stream;
        stream << std::setprecision(6) << typedValue;
        return stream.str();
      } else if constexpr (std::is_same_v<T, int>) {
        return std::to_string(typedValue);
      } else {
        return typedValue;
      }
    },
    value
  );
}

bool nameLess(std::string_view lhs, std::string_view rhs) {
  return lhs < rhs;
}

} // namespace

bool ConsoleSystem::registerCvar(CvarDefinition definition) {
  if (
    definition.name.empty() ||
    findCvar(definition.name) != nullptr ||
    findCommand(definition.name) != nullptr
  ) {
    return false;
  }
  cvars_.push_back(Cvar{std::move(definition), {}});
  cvars_.back().value = cvars_.back().definition.defaultValue;
  return true;
}

bool ConsoleSystem::registerCommand(
  std::string name,
  std::string description,
  CommandCallback callback
) {
  if (
    name.empty() ||
    !callback ||
    findCvar(name) != nullptr ||
    findCommand(name) != nullptr
  ) {
    return false;
  }
  commands_.push_back(Command{std::move(name), std::move(description), std::move(callback)});
  return true;
}

std::string ConsoleSystem::execute(std::string_view line) {
  const std::vector<std::string> tokens = tokenize(line);
  if (tokens.empty()) {
    return {};
  }

  if (tokens[0] == "set") {
    if (tokens.size() < 2) {
      return "usage: set <cvar> [value]";
    }
    Cvar* cvar = findCvar(tokens[1]);
    if (cvar == nullptr) {
      return "unknown cvar: " + tokens[1];
    }
    if (tokens.size() == 2) {
      return describeValue(*cvar);
    }
    return setValue(*cvar, tokens[2]);
  }

  if (tokens[0] == "toggle") {
    if (tokens.size() != 2) {
      return "usage: toggle <bool cvar>";
    }
    Cvar* cvar = findCvar(tokens[1]);
    if (cvar == nullptr || !std::holds_alternative<bool>(cvar->value)) {
      return "toggle requires a bool cvar";
    }
    return setValue(*cvar, std::get<bool>(cvar->value) ? "0" : "1");
  }

  if (tokens[0] == "reset") {
    if (tokens.size() != 2) {
      return "usage: reset <cvar>";
    }
    Cvar* cvar = findCvar(tokens[1]);
    if (cvar == nullptr) {
      return "unknown cvar: " + tokens[1];
    }
    if (hasFlag(cvar->definition.flags, CvarFlag::ReadOnly)) {
      return cvar->definition.name + " is read-only";
    }
    cvar->value = cvar->definition.defaultValue;
    return cvar->definition.name + " = " + formatValue(cvar->value);
  }

  if (tokens[0] == "cvarlist") {
    std::vector<std::string> names;
    names.reserve(cvars_.size());
    for (const Cvar& cvar : cvars_) {
      names.push_back(cvar.definition.name);
    }
    std::sort(names.begin(), names.end(), nameLess);
    std::string result;
    for (const std::string& name : names) {
      result += name + " = " + valueString(name) + '\n';
    }
    return result;
  }

  if (tokens[0] == "cmdlist") {
    std::vector<std::string> names{"set", "toggle", "reset", "cvarlist", "cmdlist", "help"};
    for (const Command& command : commands_) {
      names.push_back(command.name);
    }
    std::sort(names.begin(), names.end(), nameLess);
    std::string result;
    for (const std::string& name : names) {
      result += name + '\n';
    }
    return result;
  }

  if (tokens[0] == "help") {
    if (tokens.size() != 2) {
      return "usage: help <cvar|command>";
    }
    if (const Cvar* cvar = findCvar(tokens[1])) {
      return cvar->definition.name + ": " + cvar->definition.description +
        " (default " + formatValue(cvar->definition.defaultValue) + ')';
    }
    if (const Command* command = findCommand(tokens[1])) {
      return command->name + ": " + command->description;
    }
    return "unknown cvar or command: " + tokens[1];
  }

  if (Cvar* cvar = findCvar(tokens[0])) {
    if (tokens.size() == 1) {
      return describeValue(*cvar);
    }
    return setValue(*cvar, tokens[1]);
  }

  if (const Command* command = findCommand(tokens[0])) {
    return command->callback(tokens);
  }
  return "unknown command: " + tokens[0];
}

std::vector<std::string> ConsoleSystem::complete(std::string_view prefix) const {
  std::vector<std::string> matches;
  const auto addIfMatching = [&matches, prefix](std::string_view name) {
    if (name.starts_with(prefix)) {
      matches.emplace_back(name);
    }
  };
  for (const std::string_view builtIn :
       {"set", "toggle", "reset", "cvarlist", "cmdlist", "help"}) {
    addIfMatching(builtIn);
  }
  for (const Cvar& cvar : cvars_) {
    addIfMatching(cvar.definition.name);
  }
  for (const Command& command : commands_) {
    addIfMatching(command.name);
  }
  std::sort(matches.begin(), matches.end(), nameLess);
  return matches;
}

std::vector<std::string> ConsoleSystem::archivedConfigLines() const {
  std::vector<std::string> lines;
  for (const Cvar& cvar : cvars_) {
    if (!hasFlag(cvar.definition.flags, CvarFlag::Archive)) {
      continue;
    }
    std::string value = formatValue(cvar.value);
    if (std::holds_alternative<std::string>(cvar.value)) {
      value = '"' + value + '"';
    }
    lines.push_back("set " + cvar.definition.name + ' ' + value);
  }
  std::sort(lines.begin(), lines.end());
  return lines;
}

bool ConsoleSystem::getBool(std::string_view name) const {
  const Cvar* cvar = findCvar(name);
  return cvar != nullptr && std::holds_alternative<bool>(cvar->value)
    ? std::get<bool>(cvar->value)
    : false;
}

int ConsoleSystem::getInt(std::string_view name) const {
  const Cvar* cvar = findCvar(name);
  return cvar != nullptr && std::holds_alternative<int>(cvar->value)
    ? std::get<int>(cvar->value)
    : 0;
}

float ConsoleSystem::getFloat(std::string_view name) const {
  const Cvar* cvar = findCvar(name);
  return cvar != nullptr && std::holds_alternative<float>(cvar->value)
    ? std::get<float>(cvar->value)
    : 0.0F;
}

std::string ConsoleSystem::getString(std::string_view name) const {
  const Cvar* cvar = findCvar(name);
  return cvar != nullptr && std::holds_alternative<std::string>(cvar->value)
    ? std::get<std::string>(cvar->value)
    : std::string{};
}

std::string ConsoleSystem::valueString(std::string_view name) const {
  const Cvar* cvar = findCvar(name);
  return cvar == nullptr ? std::string{} : formatValue(cvar->value);
}

ConsoleSystem::Cvar* ConsoleSystem::findCvar(std::string_view name) {
  const auto match = std::find_if(
    cvars_.begin(),
    cvars_.end(),
    [name](const Cvar& cvar) { return cvar.definition.name == name; }
  );
  return match == cvars_.end() ? nullptr : &*match;
}

const ConsoleSystem::Cvar* ConsoleSystem::findCvar(std::string_view name) const {
  const auto match = std::find_if(
    cvars_.begin(),
    cvars_.end(),
    [name](const Cvar& cvar) { return cvar.definition.name == name; }
  );
  return match == cvars_.end() ? nullptr : &*match;
}

const ConsoleSystem::Command* ConsoleSystem::findCommand(std::string_view name) const {
  const auto match = std::find_if(
    commands_.begin(),
    commands_.end(),
    [name](const Command& command) { return command.name == name; }
  );
  return match == commands_.end() ? nullptr : &*match;
}

std::string ConsoleSystem::setValue(Cvar& cvar, std::string_view text) {
  if (hasFlag(cvar.definition.flags, CvarFlag::ReadOnly)) {
    return cvar.definition.name + " is read-only";
  }

  CvarValue parsed;
  bool valid = true;
  if (std::holds_alternative<bool>(cvar.value)) {
    if (text == "1" || text == "true" || text == "on") {
      parsed = true;
    } else if (text == "0" || text == "false" || text == "off") {
      parsed = false;
    } else {
      valid = false;
    }
  } else if (std::holds_alternative<int>(cvar.value)) {
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    valid = result.ec == std::errc{} && result.ptr == text.data() + text.size();
    parsed = value;
  } else if (std::holds_alternative<float>(cvar.value)) {
    float value = 0.0F;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    valid = result.ec == std::errc{} &&
      result.ptr == text.data() + text.size() &&
      std::isfinite(value);
    parsed = value;
  } else {
    parsed = std::string(text);
  }

  if (!valid) {
    return "invalid value for " + cvar.definition.name;
  }

  const float numeric = std::visit(
    [](const auto& value) -> float {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>) {
        return static_cast<float>(value);
      }
      return 0.0F;
    },
    parsed
  );
  if (
    (cvar.definition.minimum && numeric < *cvar.definition.minimum) ||
    (cvar.definition.maximum && numeric > *cvar.definition.maximum)
  ) {
    return "value out of range for " + cvar.definition.name;
  }

  cvar.value = std::move(parsed);
  return cvar.definition.name + " = " + formatValue(cvar.value);
}

std::string ConsoleSystem::describeValue(const Cvar& cvar) const {
  std::string result =
    cvar.definition.name + " = " + formatValue(cvar.value) +
    " (default " + formatValue(cvar.definition.defaultValue);
  if (!cvar.definition.referenceValue.empty()) {
    result += ", Q3/QL default " + cvar.definition.referenceValue;
  }
  return result + ')';
}

} // namespace lg
