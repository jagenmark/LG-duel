#include "app/GameApp.hpp"

#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  const lg::GameApp app("127.0.0.1", 27960);
  int failures = 0;

  failures += expect(app.name() == "LG Duel Client", "app name should be stable");

  return failures == 0 ? 0 : 1;
}
