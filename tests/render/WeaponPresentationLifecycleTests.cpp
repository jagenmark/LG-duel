#include "render/WeaponPresentationLifecycle.hpp"

#include <iostream>
#include <optional>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  int failures = 0;

  lg::WeaponPresentationLifecycle local;
  failures += expect(
    local.observe(12U, 0U, 0U) && !local.observe(12U, 0U, 0U),
    "the first authority key should reset once and a steady key should not"
  );
  failures += expect(
    local.observe(13U, 0U, 0U),
    "a map revision change should reset even when the map name is unchanged"
  );
  failures += expect(
    local.observe(13U, 1U, 2U),
    "a followed-player change should reset the first-person timeline"
  );
  failures += expect(
    local.observe(13U, 1U, 1U) && local.observe(13U, 1U, 0U),
    "death and respawn camera edges should each reset the timeline"
  );
  local.reset();
  failures += expect(
    local.observe(13U, 1U, 0U),
    "an explicit reset or replay seek should force direct reinitialization"
  );

  lg::RemoteWeaponPresentationLifecycle remote;
  failures += expect(
    remote.observe(true, "worker-a", true) &&
      !remote.observe(true, "worker-a", true),
    "a remote body should initialize once and remain steady"
  );
  failures += expect(
    remote.observe(true, "worker-a", false) &&
      remote.observe(true, "worker-a", true),
    "remote death and respawn should each clear old switch state"
  );
  failures += expect(
    remote.observe(false, {}, false) &&
      remote.observe(true, "worker-a", true),
    "disconnect and same-name slot reuse should not inherit old state"
  );
  failures += expect(
    remote.observe(true, "worker-b", true),
    "an in-place body-token change should reset a reused slot"
  );
  remote.reset();
  failures += expect(
    remote.observe(true, "worker-b", true),
    "a hard map or replay reset should clear every remote slot"
  );

  return failures == 0 ? 0 : 1;
}
