#pragma once

#include "scenario/LiveScenarioSession.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace lg {

struct ServerLaunchOptions {
  std::uint16_t port = 27960;
  std::string executablePath;
  std::optional<scenario::LiveScenarioOptions> liveScenario;
};

struct ServerCommandLineResult {
  ServerLaunchOptions options;
  bool ok = false;
  std::string error;
};

[[nodiscard]] ServerCommandLineResult parseServerCommandLine(
  int argc,
  const char* const* argv
);

class ServerApp {
public:
  ServerApp(std::uint16_t port, std::string executablePath = {});
  explicit ServerApp(ServerLaunchOptions options);

  [[nodiscard]] int run() const;

private:
  ServerLaunchOptions options_;
};

} // namespace lg
