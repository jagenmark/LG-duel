#include "input/InputBindings.hpp"

#include <algorithm>
#include <cctype>

namespace lg {
namespace {

std::string quote(std::string_view value) {
  return '"' + std::string(value) + '"';
}

} // namespace

bool InputBindings::bind(std::string key, std::string command) {
  key = normalizeKey(key);
  if (key.empty() || command.empty()) {
    return false;
  }
  bindings_[std::move(key)] = std::move(command);
  return true;
}

std::vector<std::string> InputBindings::unbind(std::string_view key) {
  const std::string normalized = normalizeKey(key);
  std::vector<std::string> releases = releaseKey(normalized);
  bindings_.erase(normalized);
  return releases;
}

std::vector<std::string> InputBindings::unbindAll() {
  std::vector<std::string> releases = releaseAll();
  bindings_.clear();
  return releases;
}

std::string InputBindings::binding(std::string_view key) const {
  const auto match = bindings_.find(normalizeKey(key));
  return match == bindings_.end() ? std::string{} : match->second;
}

std::vector<std::string> InputBindings::list() const {
  std::vector<std::string> lines;
  lines.reserve(bindings_.size());
  for (const auto& [key, command] : bindings_) {
    lines.push_back(key + " = " + command);
  }
  std::sort(lines.begin(), lines.end());
  return lines;
}

std::vector<std::string> InputBindings::configLines() const {
  std::vector<std::string> lines;
  lines.reserve(bindings_.size());
  for (const auto& [key, command] : bindings_) {
    lines.push_back("bind " + key + ' ' + quote(command));
  }
  std::sort(lines.begin(), lines.end());
  return lines;
}

std::vector<std::string> InputBindings::handleKey(std::string_view key, bool pressed) {
  const std::string normalized = normalizeKey(key);
  if (normalized.empty()) {
    return {};
  }

  if (!pressed) {
    return releaseKey(normalized);
  }
  if (!pressedKeys_.insert(normalized).second) {
    return {};
  }

  const auto bindingMatch = bindings_.find(normalized);
  if (bindingMatch == bindings_.end()) {
    return {};
  }
  return {bindingMatch->second};
}

std::vector<std::string> InputBindings::releaseAll() {
  std::vector<std::string> commands;
  std::vector<std::string> keys(pressedKeys_.begin(), pressedKeys_.end());
  for (const std::string& key : keys) {
    std::vector<std::string> releases = releaseKey(key);
    commands.insert(commands.end(), releases.begin(), releases.end());
  }
  return commands;
}

std::string InputBindings::normalizeKey(std::string_view key) {
  if (key == "\xC2\xA7") {
    return "section";
  }

  std::string normalized;
  normalized.reserve(key.size());
  for (const unsigned char character : key) {
    if (character == ' ' || character == '_' || character == '-') {
      continue;
    }
    normalized.push_back(static_cast<char>(std::tolower(character)));
  }

  if (normalized == "leftarrow") {
    return "left";
  }
  if (normalized == "rightarrow") {
    return "right";
  }
  if (normalized == "uparrow") {
    return "up";
  }
  if (normalized == "downarrow") {
    return "down";
  }
  if (normalized == "grave" || normalized == "backquote" || normalized == "`") {
    return "section";
  }
  return normalized;
}

std::vector<std::string> InputBindings::releaseKey(const std::string& key) {
  if (pressedKeys_.erase(key) == 0) {
    return {};
  }
  const auto bindingMatch = bindings_.find(key);
  if (
    bindingMatch == bindings_.end() ||
    bindingMatch->second.empty() ||
    bindingMatch->second.front() != '+'
  ) {
    return {};
  }
  return {'-' + bindingMatch->second.substr(1)};
}

} // namespace lg
