#include "server/ServerApp.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  std::uint16_t port = 27960;
  if (argc >= 2) {
    unsigned int parsedPort = 0;
    const std::string_view text = argv[1];
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsedPort);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsedPort > 65535U) {
      std::cerr << "Invalid UDP port: " << text << '\n';
      return 1;
    }
    port = static_cast<std::uint16_t>(parsedPort);
  }

  return lg::ServerApp(port).run();
}
