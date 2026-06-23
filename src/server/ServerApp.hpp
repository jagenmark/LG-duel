#pragma once

#include <cstdint>
#include <string>

namespace lg {

class ServerApp {
public:
  ServerApp(std::uint16_t port, std::string executablePath = {});

  [[nodiscard]] int run() const;

private:
  std::uint16_t port_ = 0;
  std::string executablePath_;
};

} // namespace lg
