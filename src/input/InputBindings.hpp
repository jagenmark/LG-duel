#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lg {

class InputBindings {
public:
  [[nodiscard]] bool bind(std::string key, std::string command);
  [[nodiscard]] std::vector<std::string> unbind(std::string_view key);
  [[nodiscard]] std::vector<std::string> unbindAll();

  [[nodiscard]] std::string binding(std::string_view key) const;
  [[nodiscard]] std::vector<std::string> list() const;
  [[nodiscard]] std::vector<std::string> configLines() const;
  [[nodiscard]] std::vector<std::string> handleKey(std::string_view key, bool pressed);
  [[nodiscard]] std::vector<std::string> releaseAll();

  [[nodiscard]] static std::string normalizeKey(std::string_view key);

private:
  [[nodiscard]] std::vector<std::string> releaseKey(const std::string& key);

  std::unordered_map<std::string, std::string> bindings_;
  std::unordered_set<std::string> pressedKeys_;
};

} // namespace lg
