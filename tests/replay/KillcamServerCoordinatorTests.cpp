#include "net/UdpTransport.hpp"
#include "replay/KillcamServerCoordinator.hpp"
#include "server/ServerGame.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  int failures = 0;
  lg::UdpServerTransport transport(0U);
  failures += expect(transport.initialize(), "coordinator test server should bind");
  if (failures != 0) return failures;

  lg::ServerGame server(transport);
  failures += expect(server.loadRequestedMap("dev_cuboids"),
                     "coordinator test server should load a map");
  lg::replay::KillcamServerCoordinator coordinator(server, transport);

  lg::replay::KillcamServerCoordinatorConfig config;
  config.beforeTicks = 20U;
  config.afterTicks = 2U;
  config.maximumSegmentBytes = 256U * 1024U;
  config.rolling.retainedTicks = 400U;
  config.rolling.maximumBytes = 2U * 1024U * 1024U;
  std::string error;
  failures += expect(coordinator.configure(config, &error),
                     "coordinator should enable rolling replay");
  failures += expect(server.rollingReplayStats().enabled,
                     "coordinator should own rolling replay enablement");
  failures += expect(coordinator.stats().activeTransfers == 0U,
                     "coordinator should start without active transfers");

  config.enabled = false;
  failures += expect(coordinator.configure(config, &error),
                     "coordinator should disable rolling replay");
  failures += expect(!server.rollingReplayStats().enabled,
                     "disabled coordinator should release rolling replay");
  failures += expect(coordinator.stats().pendingEvents == 0U,
                     "disabled coordinator should clear pending events");
  coordinator.shutdown();
  return failures == 0 ? 0 : 1;
}
