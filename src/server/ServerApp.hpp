#pragma once

#include <cstdint>

namespace lg {

class ServerApp {
public:
  explicit ServerApp(std::uint16_t port);

  [[nodiscard]] int run() const;

private:
  std::uint16_t port_ = 0;
};

} // namespace lg
