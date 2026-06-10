#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lg {

class GameApp {
public:
  GameApp(std::string serverHost, std::uint16_t serverPort);

  [[nodiscard]] int run() const;
  [[nodiscard]] std::string_view name() const;

private:
  std::string serverHost_;
  std::uint16_t serverPort_ = 0;
};

} // namespace lg
