#include "input/InputBindings.hpp"
#include "input/MouseAim.hpp"

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
  int failures = 0;

  failures += expect(
    lg::relativeMouseYaw(0.0F, 10.0F, 1.0F) < 0.0F,
    "moving the mouse right should turn view yaw right"
  );
  failures += expect(
    lg::relativeMouseYaw(0.0F, -10.0F, 1.0F) > 0.0F,
    "moving the mouse left should turn view yaw left"
  );
  lg::InputBindings bindings;

  failures += expect(bindings.bind("A", "+moveleft"), "binding should accept a key");
  failures += expect(bindings.binding("a") == "+moveleft", "keys should be case insensitive");
  failures += expect(
    bindings.handleKey("a", true) == std::vector<std::string>{"+moveleft"},
    "key down should emit the bound command"
  );
  failures += expect(
    bindings.handleKey("a", true).empty(),
    "repeated key down should not repeat button commands"
  );
  failures += expect(
    bindings.handleKey("a", false) == std::vector<std::string>{"-moveleft"},
    "key up should release plus commands"
  );

  failures += expect(bindings.bind("leftarrow", "+moveleft"), "arrow alias should bind");
  failures += expect(bindings.binding("left") == "+moveleft", "arrow alias should normalize");
  failures += expect(bindings.bind("grave", "toggleconsole"), "grave alias should bind");
  failures += expect(
    bindings.binding("section") == "toggleconsole",
    "grave should map to the section-key binding"
  );
  failures += expect(
    bindings.binding("\xC2\xA7") == "toggleconsole",
    "literal section sign should map to the section-key binding"
  );

  (void)bindings.handleKey("left", true);
  failures += expect(
    bindings.unbind("left") == std::vector<std::string>{"-moveleft"},
    "unbinding a held button should release it"
  );
  failures += expect(bindings.binding("left").empty(), "unbind should remove the mapping");

  (void)bindings.bind("d", "+moveright");
  (void)bindings.handleKey("d", true);
  failures += expect(
    bindings.releaseAll() == std::vector<std::string>{"-moveright"},
    "release all should release held button commands"
  );

  const std::vector<std::string> config = bindings.configLines();
  failures += expect(
    config.size() == 3 && config[0] == "bind a \"+moveleft\"",
    "bindings should serialize deterministically"
  );

  return failures == 0 ? 0 : 1;
}
