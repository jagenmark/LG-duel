#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lg {

struct DeveloperControlOptions {
  bool enabled = false;
  std::uint16_t port = 27961;
};

struct BenchmarkOptions {
  bool enabled = false;
};

class GameApp {
public:
  GameApp(
    std::string serverHost,
    std::uint16_t serverPort,
    DeveloperControlOptions developerControl = {},
    BenchmarkOptions benchmark = {}
  );

  [[nodiscard]] int run() const;
  [[nodiscard]] std::string_view name() const;

private:
  std::string serverHost_;
  std::uint16_t serverPort_ = 0;
  DeveloperControlOptions developerControl_;
  BenchmarkOptions benchmark_;
};

} // namespace lg
