#pragma once

namespace lg {

// Separate client-local application path. It has no connection/session state.
class AimTrainerApp {
public:
  [[nodiscard]] int run() const;
};

} // namespace lg
