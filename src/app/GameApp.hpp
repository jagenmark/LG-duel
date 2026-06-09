#pragma once

#include <string_view>

namespace lg {

class GameApp {
public:
  [[nodiscard]] int run() const;
  [[nodiscard]] std::string_view name() const;
};

} // namespace lg
