#include "server/ServerApp.hpp"

#include <iostream>

int main(int argc, char** argv) {
  const lg::ServerCommandLineResult parsed =
    lg::parseServerCommandLine(argc, argv);
  if (!parsed.ok) {
    std::cerr << parsed.error << '\n';
    return 1;
  }
  return lg::ServerApp(parsed.options).run();
}
