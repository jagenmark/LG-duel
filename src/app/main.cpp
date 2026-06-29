#include "app/GameApp.hpp"

#include <charconv>
#include <exception>
#include <cstdint>
#include <iostream>
#include <new>
#include <string_view>

int main(int argc, char** argv) {
  std::string_view host = "127.0.0.1";
  std::uint16_t port = 27960;
  if (argc >= 2) {
    host = argv[1];
  }
  if (argc >= 3) {
    unsigned int parsedPort = 0;
    const std::string_view text = argv[2];
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsedPort);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsedPort > 65535U) {
      std::cerr << "Invalid UDP port: " << text << '\n';
      return 1;
    }
    port = static_cast<std::uint16_t>(parsedPort);
  }

  try {
    const lg::GameApp app(std::string(host), port);
    return app.run();
  } catch (const std::bad_alloc& exception) {
    std::cerr << "Fatal allocation failure: " << exception.what() << '\n';
    return 1;
  } catch (const std::exception& exception) {
    std::cerr << "Fatal error: " << exception.what() << '\n';
    return 1;
  }
}
