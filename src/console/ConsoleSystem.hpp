#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace lg {

enum class CvarFlag : std::uint8_t {
  None = 0,
  Archive = 1U << 0U,
  ReadOnly = 1U << 1U,
  Client = 1U << 2U,
};

constexpr CvarFlag operator|(CvarFlag lhs, CvarFlag rhs) {
  return static_cast<CvarFlag>(
    static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs)
  );
}

constexpr bool hasFlag(CvarFlag flags, CvarFlag flag) {
  return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(flag)) != 0;
}

using CvarValue = std::variant<bool, int, float, std::string>;

struct CvarDefinition {
  CvarDefinition(
    std::string name,
    std::string description,
    CvarValue defaultValue,
    CvarFlag flags = CvarFlag::None,
    std::optional<float> minimum = {},
    std::optional<float> maximum = {},
    std::string referenceValue = {}
  )
    : name(std::move(name)),
      description(std::move(description)),
      defaultValue(std::move(defaultValue)),
      flags(flags),
      minimum(minimum),
      maximum(maximum),
      referenceValue(std::move(referenceValue)) {}

  std::string name;
  std::string description;
  CvarValue defaultValue;
  CvarFlag flags = CvarFlag::None;
  std::optional<float> minimum;
  std::optional<float> maximum;
  std::string referenceValue;
};

class ConsoleSystem {
public:
  using CommandCallback = std::function<std::string(const std::vector<std::string>&)>;

  bool registerCvar(CvarDefinition definition);
  bool registerCommand(
    std::string name,
    std::string description,
    CommandCallback callback
  );

  [[nodiscard]] std::string execute(std::string_view line);
  [[nodiscard]] std::vector<std::string> complete(std::string_view prefix) const;
  [[nodiscard]] std::vector<std::string> archivedConfigLines() const;

  [[nodiscard]] bool getBool(std::string_view name) const;
  [[nodiscard]] int getInt(std::string_view name) const;
  [[nodiscard]] float getFloat(std::string_view name) const;
  [[nodiscard]] std::string getString(std::string_view name) const;
  [[nodiscard]] bool hasCvar(std::string_view name) const;
  [[nodiscard]] std::string valueString(std::string_view name) const;

private:
  struct Cvar {
    CvarDefinition definition;
    CvarValue value;
  };

  struct Command {
    std::string name;
    std::string description;
    CommandCallback callback;
  };

  [[nodiscard]] Cvar* findCvar(std::string_view name);
  [[nodiscard]] const Cvar* findCvar(std::string_view name) const;
  [[nodiscard]] const Command* findCommand(std::string_view name) const;
  [[nodiscard]] std::string setValue(Cvar& cvar, std::string_view text);
  [[nodiscard]] std::string describeValue(const Cvar& cvar) const;

  std::vector<Cvar> cvars_;
  std::vector<Command> commands_;
};

} // namespace lg
