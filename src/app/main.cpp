#include "app/GameApp.hpp"
#include "app/AimTrainerApp.hpp"

#include <charconv>
#include <exception>
#include <cstdint>
#include <iostream>
#include <new>
#include <string>
#include <string_view>

namespace {

bool parsePort(std::string_view text, std::uint16_t& port) {
  unsigned int parsedPort = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), parsedPort);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsedPort > 65535U) {
    return false;
  }
  port = static_cast<std::uint16_t>(parsedPort);
  return true;
}

} // namespace

int main(int argc, char** argv) {
  std::string_view host = "127.0.0.1";
  std::uint16_t port = 27960;
  lg::DeveloperControlOptions developerControl;
  lg::BenchmarkOptions benchmark;
  bool aimTrainer = false;
  int positional = 0;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--dev-control") {
      developerControl.enabled = true;
      continue;
    }
    if (argument == "--benchmark") {
      benchmark.enabled = true;
      developerControl.enabled = true;
      continue;
    }
    if (argument == "--aim-trainer") {
      aimTrainer = true;
      continue;
    }
    if (argument == "--control-port") {
      if (index + 1 >= argc || !parsePort(argv[++index], developerControl.port)) {
        std::cerr << "Invalid or missing --control-port value\n";
        return 1;
      }
      developerControl.enabled = true;
      continue;
    }
    if (argument == "--help" || argument == "-h") {
      std::cout
        << "Usage: lg_duel_client [server-host] [server-port] "
           "[--aim-trainer] [--dev-control] [--benchmark] [--control-port port]\n";
      return 0;
    }
    if (argument.starts_with("--")) {
      std::cerr << "Unknown option: " << argument << '\n';
      return 1;
    }
    if (positional == 0) {
      host = argument;
    } else if (positional == 1) {
      if (!parsePort(argument, port)) {
        std::cerr << "Invalid UDP port: " << argument << '\n';
        return 1;
      }
    } else {
      std::cerr << "Unexpected positional argument: " << argument << '\n';
      return 1;
    }
    ++positional;
  }

  try {
    if (aimTrainer) {
      if (positional != 0 || benchmark.enabled) {
        std::cerr << "--aim-trainer cannot be combined with network or benchmark options\n";
        return 1;
      }
      const lg::AimTrainerApp app(developerControl);
      return app.run();
    }
    const lg::GameApp app(std::string(host), port, developerControl, benchmark);
    return app.run();
  } catch (const std::bad_alloc& exception) {
    std::cerr << "Fatal allocation failure: " << exception.what() << '\n';
    return 1;
  } catch (const std::exception& exception) {
    std::cerr << "Fatal error: " << exception.what() << '\n';
    return 1;
  }
}
