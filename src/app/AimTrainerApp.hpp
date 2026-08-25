#pragma once

#include "app/DeveloperControlOptions.hpp"

namespace lg {

// Separate client-local application path. It has no connection/session state.
class AimTrainerApp {
public:
  explicit AimTrainerApp(DeveloperControlOptions developerControl = {});
  [[nodiscard]] int run() const;

private:
  DeveloperControlOptions developerControl_;
};

} // namespace lg
